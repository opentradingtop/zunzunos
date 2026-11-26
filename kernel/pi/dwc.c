/* ZunzunOS - USB en la Raspberry Pi 2: el controlador Synopsys DesignWare USB 2.0 OTG ("DWC2") como anfitrion.
   Escrito a partir de la documentacion publica del nucleo y de la especificacion USB 2.0. Sin interrupciones: todo
   por sondeo, como el resto del kernel.
   - DMA en modo bufer: cada canal tiene su bufer intermedio de 64 KB sin cache, que el USB ve por el alias
     0xC0000000 del bus de la GPU (la cache del ARM no se entera de lo que escribe el USB).
   - Los NAK de control y bulk los reintenta el propio controlador; los de interrupcion paran el canal.
   - Hubs (en la placa, el LAN9514 es un hub con la tarjeta de red en el puerto 1) con transacciones divididas
     (split) para teclados y ratones de baja/plena velocidad detras de un hub de alta velocidad.
   - HID (hid.c), almacenamiento, impresoras (usbprn.c) y red (usbnet.c: CDC-ECM, RNDIS y el SMSC LAN95xx). */
#include "../kernel.h"
#include "../usb.h"
#include "../usbnet.h"

#define G(o) R32(PERI(0x980000 + (o)))
#define HC(c, o) R32(PERI(0x980500 + (c) * 0x20 + (o)))
#define GAHBCFG 0x08
#define GUSBCFG 0x0C
#define GRSTCTL 0x10
#define GINTSTS 0x14
#define GINTMSK 0x18
#define GRXFSIZ 0x24
#define GNPTXFSIZ 0x28
#define GSNPSID 0x40
#define GHWCFG2 0x48
#define GHWCFG3 0x4C
#define HPTXFSIZ 0x100
#define HCFG 0x400
#define HFNUM 0x408
#define HPRT 0x440
#define PCGCCTL 0xE00
#define HCCHAR 0x00
#define HCSPLT 0x04
#define HCINT 0x08
#define HCINTMSK 0x0C
#define HCTSIZ 0x10
#define HCDMA 0x14
/* HCINT */
#define I_XFER 0x001
#define I_HALT 0x002
#define I_STALL 0x008
#define I_NAK 0x010
#define I_ACK 0x020
#define I_NYET 0x040
#define I_FRM 0x200
/* PID en HCTSIZ */
#define DATA0 0
#define DATA1 2
#define SETUP 3
#define CHENA (1u << 31)
#define CHDIS (1u << 30)
/* HPRT: bits que se borran escribiendo 1 (conexion detectada, puerto activo, cambios) */
#define HPRT_W1C 0x2Eu
enum { HS, FS, LS };   /* el mismo orden que la velocidad del puerto en HPRT */
#define NCH 8
#define BB 65536

typedef struct { u8 *bb; u32 bus; volatile int busy; } chan_t;
static chan_t ch[NCH]; static int nch;
typedef struct { u8 num, in, type, tog; u16 mps; } dep_t;
typedef struct ddev {
    struct ddev *next, *hub, *child[16]; volatile int gone; int addr, speed, port, tt_addr, tt_port, is_hub, nports, nhid;
    dep_t ep0, hep[4], bin, bout, pout, nin, nout; u64 hnext[4]; int hival[4]; mutex_t lk;
    int prn_if, prn_alt, net_kind, net_ctl, net_data, net_alt, net_mac, rxch; u32 rxn, rxprog; u8 *rxbuf, *pbuf, *nbo;
    hidif_t hif[4]; u8 *cbw; u8 dd[18]; u32 tag; blkdev_t *blk; u8 lun;
} ddev_t;
static ddev_t *root, *devs; static int next_addr = 1;

/* --- canales --- */
static int ch_get(void) {
    for (;;) {
        u64 f = irq_save(); for (int i = 0; i < nch; i++) if (!ch[i].busy) { ch[i].busy = 1; irq_restore(f); return i; }
        irq_restore(f); yield();
    }
}
static void ch_put(int c) { ch[c].busy = 0; }
static void ch_halt(int c) {
    u32 v = HC(c, HCCHAR);
    if (v & CHENA) { HC(c, HCCHAR) = v | CHDIS; for (int t = 0; t < 20000 && !(HC(c, HCINT) & I_HALT); t++) udelay(1); }
    HC(c, HCINT) = ~0u;
}
/* programa el canal y lo pone en marcha; los datos ya estan en (o iran a) su bufer, a partir de off.
   Las entradas piden paquetes enteros (lo que espera el controlador); devuelve el tamano programado */
static u32 ch_start(int c, ddev_t *d, dep_t *e, int in, u32 pid, u32 len, u32 off, u32 split) {
    u32 pk = split ? 1 : len ? (len + e->mps - 1) / e->mps : 1; if (in && len) len = pk * e->mps;
    u32 chr = e->mps | (u32)e->num << 11 | (u32)in << 15 | (u32)(d->speed == LS) << 17 | (u32)e->type << 18 | 1u << 20 | (u32)d->addr << 22;
    if (e->type == 3 || e->type == 1) chr |= (~G(HFNUM) & 1) << 29;   /* periodico: en la (micro)trama siguiente */
    HC(c, HCINT) = ~0u; HC(c, HCINTMSK) = 0x7FF; HC(c, HCSPLT) = split;
    HC(c, HCTSIZ) = len | pk << 19 | pid << 29; HC(c, HCDMA) = ch[c].bus + off;
    HC(c, HCCHAR) = chr; mb(); HC(c, HCCHAR) = chr | CHENA; return len;
}
static u32 got_in(int c, u32 prog, u32 want) { u32 r = HC(c, HCTSIZ) & 0x7FFFF, g = prog > r ? prog - r : 0; return MIN(g, want); }
/* espera a que el canal se pare; 0 si se acaba el tiempo o el dispositivo ya no esta (el canal queda parado) */
static u32 ch_wait(int c, ddev_t *d, u64 end, int busy) {
    for (;;) {
        u32 i = HC(c, HCINT); if (i & I_HALT) return i;
        if (d->gone || ticks > end) { ch_halt(c); return 0; }
        if (!busy) yield();
    }
}
static int bad(ddev_t *d) { return d->gone ? -1 : -2; }
/* paquetes que se llevo una transferencia: decide el siguiente DATA0/DATA1 */
static void toggle(dep_t *e, u32 n, u32 moved, int in) { u32 b = in ? moved : n, p = b ? (b + e->mps - 1) / e->mps : 1; if (p & 1) e->tog ^= 1; }
/* sin split: una sola puesta en marcha para todo el trozo; el controlador reintenta los NAK de control y bulk */
static int run_plain(int c, ddev_t *d, dep_t *e, int in, u32 pid, u32 n, u64 end, u32 *moved) {
    for (;;) {
        u32 tl = ch_start(c, d, e, in, pid, n, 0, 0);
        u32 i = ch_wait(c, d, end, 0); if (!i) return bad(d);
        if (i & I_XFER) { *moved = in ? got_in(c, tl, n) : n; return 0; }
        if (i & I_STALL) return -6;
        if (i & (I_NAK | I_FRM)) { if (e->type == 3) return -3; if (ticks > end) return -2; yield(); continue; }
        return -4;
    }
}
/* periodico con split: el SSPLIT no debe caer en las ultimas microtramas, para que los CSPLIT entren en la misma trama */
static void uframe_ok(void) { for (int t = 0; t < 2000 && (G(HFNUM) & 7) > 4; t++) udelay(5); }
/* con split (baja/plena velocidad detras de un hub de alta): paquete a paquete, SSPLIT y luego CSPLIT */
static int run_split(int c, ddev_t *d, dep_t *e, int in, u32 pid, u32 n, u64 end, u32 *moved) {
    u32 sp = 1u << 31 | 3u << 14 | (u32)d->tt_addr << 7 | (u32)d->tt_port, off = 0; int per = e->type == 3;
    do {
        u32 pl = MIN(n - off, (u32)e->mps), got = 0; int err = 0;
        for (;;) {
            if (per) uframe_ok();
            ch_start(c, d, e, in, pid, pl, off, sp); u32 i = ch_wait(c, d, end + 5, 1); if (!i) return bad(d);
            if (i & I_STALL) return -6;
            if (i & (I_NAK | I_FRM)) { if (per) return -3; if (ticks > end) { if (off) goto done; return -2; } yield(); continue; }
            if (!(i & I_ACK)) { if (++err > 3) return -4; continue; }
            int ny = 0, again = 0;
            for (;;) {   /* CSPLIT */
                u32 tl = ch_start(c, d, e, in, pid, pl, off, sp | 1u << 16); u32 j = ch_wait(c, d, end + 5, 1); if (!j) return bad(d);
                if ((j & I_XFER) || (!in && (j & I_ACK))) { got = in ? got_in(c, tl, pl) : pl; break; }
                if (j & I_STALL) return -6;
                if (j & I_NYET) { if (++ny > (per ? 4 : 400)) { again = 1; break; } continue; }
                if (j & (I_NAK | I_FRM)) { if (per) return -3; again = 1; break; }
                if (++err > 3) return -4;
            }
            if (!again) break;
            if (ticks > end) { if (off) goto done; return -2; }
            yield();
        }
        off += got; if (pid != SETUP) pid = pid == DATA0 ? DATA1 : DATA0;
        if (in && got < pl) break;
    } while (off < n);
done: *moved = off; return 0;
}
/* transferencia por un endpoint: troceada a lo que cabe en el bufer del canal. pid < 0: DATA0/DATA1 del endpoint */
static int xfer(ddev_t *d, dep_t *e, int in, int pid, void *buf, u32 len, int ms, u32 *got) {
    if (d->gone) return -1;
    u8 *p = buf; u32 done = 0, chunk = MIN((u32)BB, 1023U * e->mps) / e->mps * e->mps; u64 end = ticks + ms; int r = 0, c = ch_get();
    do {
        u32 n = MIN(len - done, chunk), moved = 0, pv = pid >= 0 ? (u32)pid : e->tog ? DATA1 : DATA0;
        if (!in && n) memcpy(ch[c].bb, p + done, n);
        r = d->tt_addr ? run_split(c, d, e, in, pv, n, end, &moved) : run_plain(c, d, e, in, pv, n, end, &moved);
        if (r) { if (r == -6) e->tog = 0; break; }
        if (pid < 0) toggle(e, n, moved, in);
        if (in && moved && p) memcpy(p + done, ch[c].bb, moved);
        done += moved; if (in && moved < n) break;
    } while (done < len);
    ch_put(c); if (got) *got = done; return r;
}

/* --- control, bulk --- */
static int ctrl(ddev_t *d, u8 rt, u8 rq, u16 val, u16 idx, u16 len, void *data) {
    int in = (rt & 0x80) != 0; u8 s[8] = {rt, rq, val, val >> 8, idx, idx >> 8, len, len >> 8}; u32 got = 0; if (len > 4096) return -22;
    mutex_lock(&d->lk);
    int r = xfer(d, &d->ep0, 0, SETUP, s, 8, 1000, 0);
    if (!r && len) { d->ep0.tog = 1; r = xfer(d, &d->ep0, in, -1, data, len, 2000, &got); if (!r && in && got < len) memset((u8 *)data + got, 0, len - got); }
    if (!r) r = xfer(d, &d->ep0, !(in && len), DATA1, 0, 0, 2000, 0);
    mutex_unlock(&d->lk); return r;
}
static int ctrl_retry(ddev_t *d, u8 rt, u8 rq, u16 v, u16 i, u16 l, void *p) { int r = -1; for (int t = 0; t < 3 && !d->gone; t++) { r = ctrl(d, rt, rq, v, i, l, p); if (!r) return 0; msleep(20); } return r; }
static int bulk(ddev_t *d, dep_t *e, void *buf, u32 len, int ms) { return xfer(d, e, e->in, -1, buf, len, ms, 0); }
static void clear_halt(ddev_t *d, dep_t *e) { ctrl(d, 2, 1, 0, e->num | (e->in ? 0x80 : 0), 0, 0); e->tog = 0; }
static void ep_set(dep_t *e, int num, int in, int mps, int type) { e->num = num; e->in = in; e->mps = mps ? mps : 8; e->type = type; e->tog = 0; }

/* --- almacenamiento (Bulk-Only y SCSI) --- */
static int bot(ddev_t *d, const u8 *cdb, int cl, void *buf, u32 len, int in) {
    u8 *w = d->cbw; memset(w, 0, 64); u32 tag = ++d->tag; *(u32 *)w = 0x43425355; *(u32 *)(w + 4) = tag; *(u32 *)(w + 8) = len; w[12] = in ? 0x80 : 0; w[13] = d->lun; w[14] = cl; memcpy(w + 15, cdb, cl);
    int e = bulk(d, &d->bout, w, 31, 5000); if (e) { if (e == -6) clear_halt(d, &d->bout); return -1; }
    if (len) { dep_t *p = in ? &d->bin : &d->bout; e = bulk(d, p, buf, len, 30000); if (e == -1) return -1; if (e == -6) clear_halt(d, p); else if (e) return -1; }
    e = bulk(d, &d->bin, w + 32, 13, 5000); if (e == -6) { clear_halt(d, &d->bin); e = bulk(d, &d->bin, w + 32, 13, 5000); } if (e) return -1;
    if (*(u32 *)(w + 32) != 0x53425355 || *(u32 *)(w + 36) != tag) return -1;
    return w[44] ? -2 : 0;
}
static int scsi_rw(blkdev_t *b, u64 lba, u32 n, void *buf, int wr) {
    ddev_t *d = b->drv; u8 c[10] = {wr ? 0x2A : 0x28, 0, lba >> 24, lba >> 16, lba >> 8, lba, 0, n >> 8, n, 0};
    for (int t = 0; t < 3; t++) { int r = bot(d, c, 10, buf, n * b->ssize, !wr); if (!r) return 0; if (d->gone) return -1; kprintf("dwc: scsi lba=%lu fallo %d\n", lba, r); u8 s[6] = {3, 0, 0, 0, 18, 0}; bot(d, s, 6, d->cbw + 128, 18, 1); msleep(20); }
    return -1;
}
static int msc_rd(blkdev_t *b, u64 l, u32 n, void *p) { return scsi_rw(b, l, n, p, 0); }
static int msc_wr(blkdev_t *b, u64 l, u32 n, const void *p) { return scsi_rw(b, l, n, (void *)p, 1); }
static int msc_fl(blkdev_t *b) { u8 c[10] = {0x35}; bot(b->drv, c, 10, 0, 0, 0); return 0; }
static void msc_start(void *a) {
    ddev_t *d = a; u8 *x = kmalloc(4096); u8 inq[6] = {0x12, 0, 0, 0, 36, 0}, tur[6] = {0}, cap[10] = {0x25}, se[6] = {3, 0, 0, 0, 18, 0}; int ok = 0;
    if (bot(d, inq, 6, x, 36, 1)) { kprintf("dwc: INQUIRY fallo\n"); kfree(x); return; }
    char m[41]; memcpy(m, x + 8, 24); m[24] = 0; for (int i = 23; i >= 0 && m[i] == ' '; i--) m[i] = 0;
    for (int t = 0; t < 40 && !d->gone; t++) { if (!bot(d, tur, 6, 0, 0, 0)) { ok = 1; break; } bot(d, se, 6, x, 18, 1); msleep(100); }
    if (!ok || bot(d, cap, 10, x, 8, 1)) { kprintf("dwc: medio no listo\n"); kfree(x); return; }
    u32 l = x[0] << 24 | x[1] << 16 | x[2] << 8 | x[3], sz = x[4] << 24 | x[5] << 16 | x[6] << 8 | x[7];
    blkdev_t *b = blk_new("usb"); b->drv = d; b->ssize = sz; b->sectors = (u64)l + 1; strncpy0(b->model, m, 41); b->read = msc_rd; b->write = msc_wr; b->flush = msc_fl; b->removable = 1; b->maxsect = 120 * 1024 / sz; d->blk = b; blk_register(b); kfree(x);
}

/* --- impresoras y red --- */
static int eprn_wr(void *ctx, const void *p, u32 n) { ddev_t *d = ctx; if (d->gone) return -1; int e = bulk(d, &d->pout, (void *)p, n, 90000); if (e) { if (!d->gone) clear_halt(d, &d->pout); return -5; } return (int)n; }
static int en_ctrl(void *dv, u8 rt, u8 rq, u16 v, u16 i, u16 l, void *p) { return ctrl(dv, rt, rq, v, i, l, p); }
static int en_out(void *dv, const void *p, u32 n) {
    ddev_t *d = dv; if (d->gone) return -1;
    /* CDC y RNDIS: un envio que llena paquetes justos lleva un byte mas para acabar en paquete corto (el LAN95xx lleva su longitud) */
    memcpy(d->nbo, p, n); if (d->net_kind != 4 && n % d->nout.mps == 0) d->nbo[n++] = 0;
    int e = bulk(d, &d->nout, d->nbo, n, 2000); if (e) { if (!d->gone) clear_halt(d, &d->nout); return -5; } return (int)n;
}
/* recepcion: el canal queda esperando datos (el controlador reintenta los NAK) y en_in_poll mira si termino */
static int en_in_start(void *dv, void *b, u32 n) {
    ddev_t *d = dv; if (d->gone) return -1; d->rxbuf = b; d->rxn = MIN(n, (u32)BB / d->nin.mps * d->nin.mps); d->rxch = -1;
    if (d->tt_addr) return 0;   /* con split se recibe en en_in_poll, sin esperar */
    int c = ch_get(); d->rxch = c; d->rxprog = ch_start(c, d, &d->nin, 1, d->nin.tog ? DATA1 : DATA0, d->rxn, 0, 0); return 0;
}
static int en_in_poll(void *dv) {
    ddev_t *d = dv; u32 got = 0;
    if (d->rxch < 0) { int r = xfer(d, &d->nin, 1, -1, d->rxbuf, d->rxn, 0, &got); if (!r) return got; if (r == -2 || r == -3) return -1; if (r == -6 && !d->gone) clear_halt(d, &d->nin); return -2; }
    int c = d->rxch; u32 i = HC(c, HCINT);
    if (!(i & I_HALT)) { if (!d->gone) return -1; ch_halt(c); ch_put(c); d->rxch = -1; return -2; }
    d->rxch = -1;
    if (i & I_XFER) { got = got_in(c, d->rxprog, d->rxn); toggle(&d->nin, d->rxn, got, 1); memcpy(d->rxbuf, ch[c].bb, got); ch_put(c); return got; }
    ch_put(c); if ((i & I_STALL) && !d->gone) clear_halt(d, &d->nin);
    return -2;
}
/* tarjetas de red SMSC LAN95xx (la de la Pi 2 es la parte de red del LAN9514) */
static int is_smsc(u16 v, u16 p) { return v == 0x0424 && (p == 0xEC00 || p == 0x9500 || p == 0x9E00 || p == 0x9E01); }

/* --- enumeracion --- */
static void configure(ddev_t *d) {
    u8 *c = kmalloc(1024), *hr = kmalloc(1024); int hrl[4] = {0}, kind = 0, msc = 0, hub = 0; hidif_t *chf = 0;
    u16 vid = *(u16 *)(d->dd + 8), pid = *(u16 *)(d->dd + 10); int smsc = is_smsc(vid, pid);
    int ci = 0, ncfg = d->dd[17]; if (ncfg > 1) for (int k = 0; k < ncfg && k < 8; k++) { int kn; if (ctrl_retry(d, 0x80, 6, 0x200 | k, 0, 9, c)) continue; int t2 = MIN(*(u16 *)(c + 2), 1024); if (ctrl_retry(d, 0x80, 6, 0x200 | k, 0, t2, c)) continue; int net = usbnet_pick_cfg(c, t2, k, &kn); if (k == 0 && kn && !net) break; if (net) { ci = k; break; } }
    if (ci) kprintf("dwc: se usa la configuracion %d (red)\n", ci + 1);
    if (ctrl_retry(d, 0x80, 6, 0x200 | ci, 0, 9, c)) goto out;
    int tl = MIN(*(u16 *)(c + 2), 1024); if (ctrl_retry(d, 0x80, 6, 0x200 | ci, 0, tl, c)) goto out; u8 cv = c[5];
    for (int i = 0; i + 2 <= tl && c[i]; i += c[i]) {
        u8 *x = c + i; int t = x[1];
        if (t == 4) {
            int cl = x[5], su = x[6], pr = x[7]; kind = 0; chf = 0; int iph = usbnet_is_ipheth(cl, su, pr);
            if (smsc) { if (!x[3] && !d->net_kind) { kind = 5; d->net_kind = 4; d->net_data = x[2]; } continue; }
            if (x[3] && !(cl == 0x0A && d->net_kind == 2 && !d->nin.mps) && !(iph && d->net_kind == 3 && !d->nin.mps)) continue;
            if (iph && !x[3] && !d->net_kind) { d->net_kind = 3; d->net_ctl = x[2]; continue; }
            int nk = usbnet_ctl_kind(cl, su, pr);
            if (x[3]) { kind = 5; d->net_alt = x[3]; d->net_data = x[2]; continue; }
            if (nk && !d->net_kind) { kind = 6; d->net_kind = nk; d->net_ctl = x[2]; continue; }
            if (cl == 0x0A && d->net_kind && !d->nin.mps) { kind = 5; d->net_data = x[2]; continue; }
            if (cl == 3 && d->nhid < 4) { chf = &d->hif[d->nhid]; memset(chf, 0, sizeof *chf); chf->ifn = x[2]; chf->is_kbd = su == 1 && pr == 1; kind = 1; }
            else if (cl == 8 && pr == 0x50 && !msc) kind = 2;
            else if (cl == 7 && su == 1 && (pr == 1 || pr == 2) && !d->pout.mps) { kind = 4; d->prn_if = x[2]; d->prn_alt = x[3]; }
            else if (cl == 9) { kind = 3; hub = 1; }
        }
        else if (t == 0x21 && kind == 1) hrl[d->nhid] = *(u16 *)(x + 7);
        else if (t == 0x24 && kind == 6 && x[2] == 0x0F) d->net_mac = x[3];
        else if (t == 5 && kind) {
            int a = x[2], at = x[3] & 3, mps = *(u16 *)(x + 4) & 0x7FF, in = a >> 7, n = a & 15;
            if (kind == 1 && chf && in && at == 3 && !chf->dci) {
                int iv = x[6]; iv = d->speed == HS ? (1 << MIN(MAX(iv, 1) - 1, 15)) / 8 : iv;
                chf->dci = n; chf->rep_len = MIN(mps, 64); ep_set(&d->hep[d->nhid], n, 1, mps, 3); d->hival[d->nhid] = MIN(MAX(iv, 4), 32); d->nhid++; chf = 0; kind = 0;
            }
            else if (kind == 2 && at == 2) { ep_set(in ? &d->bin : &d->bout, n, in, mps, 2); if (d->bin.mps && d->bout.mps) msc = 1; }
            else if (kind == 4 && at == 2 && !in && !d->pout.mps) ep_set(&d->pout, n, 0, mps, 2);
            else if (kind == 5 && at == 2) { dep_t *e = in ? &d->nin : &d->nout; if (!e->mps) ep_set(e, n, in, mps, 2); }
        }
    }
    if (ctrl_retry(d, 0, 9, cv, 0, 0, 0)) { kprintf("dwc: SET_CONFIGURATION fallo\n"); goto out; }
    for (int i = 0; i < d->nhid; i++) {
        hidif_t *f = &d->hif[i]; f->rep = kmalloc(64); f->id = (u32)vid << 16 | pid;
        if (f->is_kbd) ctrl(d, 0x21, 0x0B, 0, f->ifn, 0, 0); else if (hrl[i]) { int n = MIN(hrl[i], 1024); if (!ctrl_retry(d, 0x81, 6, 0x2200, f->ifn, n, hr)) hid_parse(f, hr, n); }
        ctrl(d, 0x21, 0x0A, 0, f->ifn, 0, 0);
        kprintf("dwc: HID %s dir %d velocidad %d%s\n", f->is_kbd || f->kb ? "teclado" : f->joy ? "mando" : f->xs ? "puntero" : "otro", d->addr, d->speed, f->absolute ? " absoluto" : "");
    }
    if (d->nin.mps && d->nout.mps && d->net_kind) {
        extern u8 net_first_mac[6]; d->nbo = kmalloc(4096); d->rxch = -1;
        usbnet_t u = {d, &d->gone, d->net_kind, d->net_ctl, d->net_data, d->net_alt, d->net_mac, en_ctrl, en_out, en_in_start, en_in_poll, d->net_kind == 4 ? net_first_mac : 0};
        usbnet_attach(&u);
    }
    if (d->pout.mps) { u8 *id = kmalloc(1024); memset(id, 0, 1024); if (ctrl(d, 0xA1, 0, 0, d->prn_if << 8 | d->prn_alt, 1023, id)) id[0] = id[1] = 0; usbprn_add(vid, pid, id, eprn_wr, d, &d->gone); kfree(id); }
    if (msc) { d->cbw = kmalloc(4096); kprintf("dwc: almacenamiento dir %d\n", d->addr); kthread("usb-msc", msc_start, d); }
    else if (hub) {
        u8 hd[12]; if (ctrl_retry(d, 0xA0, 6, 0x2900, 0, 12, hd)) goto out; d->nports = MIN((int)hd[2], 15);
        for (int p = 1; p <= d->nports; p++) ctrl(d, 0x23, 3, 8, p, 0, 0);   /* PORT_POWER */
        msleep(MAX(hd[5] * 2, 100)); d->is_hub = 1; kprintf("dwc: hub dir %d con %d puertos\n", d->addr, d->nports);
    }
out: kfree(c); kfree(hr);
}
static ddev_t *enumerate(int speed, ddev_t *hub, int port) {
    ddev_t *d = kzalloc(sizeof *d); d->speed = speed; d->hub = hub; d->port = port; d->rxch = -1;
    if (speed != HS && hub) { if (hub->speed == HS) { d->tt_addr = hub->addr; d->tt_port = port; } else { d->tt_addr = hub->tt_addr; d->tt_port = hub->tt_port; } }
    ep_set(&d->ep0, 0, 0, speed == HS ? 64 : 8, 0);
    int r = ctrl_retry(d, 0x80, 6, 0x100, 0, 8, d->dd);
    if (r) { kprintf("dwc: sin respuesta en puerto %d velocidad %d (%d) tt=%d/%d\n", port, speed, r, d->tt_addr, d->tt_port); kfree(d); return 0; }
    int mps = d->dd[7] ? d->dd[7] : 8, addr = next_addr; next_addr = addr >= 127 ? 1 : addr + 1;
    if (ctrl_retry(d, 0, 5, addr, 0, 0, 0)) { kprintf("dwc: SET_ADDRESS fallo\n"); kfree(d); return 0; }
    msleep(10); d->addr = addr; d->ep0.mps = mps;
    if (ctrl_retry(d, 0x80, 6, 0x100, 0, 18, d->dd)) { kprintf("dwc: descriptor fallo dir %d\n", addr); kfree(d); return 0; }
    kprintf("dwc: %04x:%04x clase %d dir %d velocidad %d puerto %d hub %d\n", *(u16 *)(d->dd + 8), *(u16 *)(d->dd + 10), d->dd[4], addr, speed, port, hub ? hub->addr : 0);
    u64 f = irq_save(); d->next = devs; devs = d; irq_restore(f); configure(d); return d;
}
static void dev_gone(ddev_t *d) {
    d->gone = 1; for (int i = 0; i < 16; i++) if (d->child[i]) { dev_gone(d->child[i]); d->child[i] = 0; }
    if (d->blk) blk_remove(d->blk); kprintf("dwc: dispositivo %d retirado\n", d->addr);
}
/* el unico puerto del controlador (en la Pi 2, el hub LAN9514 esta siempre ahi) */
static void root_poll(void) {
    u32 v = G(HPRT); if (v & 0x2A) G(HPRT) = (v & ~HPRT_W1C) | (v & 0x2A);
    int conn = v & 1;
    if (root && (!conn || (v & 2))) { dev_gone(root); root = 0; }
    if (!conn || root) return;
    msleep(100); v = G(HPRT); if (!(v & 1)) return;
    G(HPRT) = (v & ~HPRT_W1C) | 1u << 8; msleep(60); G(HPRT) = G(HPRT) & ~HPRT_W1C & ~(1u << 8); msleep(20);
    v = G(HPRT); if (v & 0x2A) G(HPRT) = (v & ~HPRT_W1C) | (v & 0x2A);
    if (!(v & 4)) { kprintf("dwc: el puerto no se activa tras el reinicio (%x)\n", v); return; }
    root = enumerate((v >> 17) & 3, 0, 1);
}
static void hub_poll(ddev_t *hb) {
    for (int p = 1; p <= hb->nports && !hb->gone; p++) {
        u32 st; if (ctrl(hb, 0xA3, 0, 0, p, 4, &st)) return; int conn = st & 1; ddev_t *c = hb->child[p];
        for (int b = 0; b < 5; b++) if (st & (1u << (16 + b))) ctrl(hb, 0x23, 1, 16 + b, p, 0, 0);
        if (c && (!conn || (st & (1 << 16)))) { hb->child[p] = 0; dev_gone(c); c = 0; }
        if (!conn || c) continue;
        msleep(100); ctrl(hb, 0x23, 3, 4, p, 0, 0); int ok = 0;   /* PORT_RESET */
        for (int t = 0; t < 50; t++) { msleep(10); if (ctrl(hb, 0xA3, 0, 0, p, 4, &st)) return; if (st & (1 << 20)) { ok = 1; break; } }
        ctrl(hb, 0x23, 1, 20, p, 0, 0); ctrl(hb, 0x23, 1, 16, p, 0, 0); msleep(20);
        if (!ok || ctrl(hb, 0xA3, 0, 0, p, 4, &st) || !(st & 2)) { kprintf("dwc: reinicio fallido hub %d puerto %d st=%x\n", hb->addr, p, st); continue; }
        hb->child[p] = enumerate((st & (1 << 10)) ? HS : (st & (1 << 9)) ? LS : FS, hb, p);
    }
}
static void hid_poll(ddev_t *d) {
    for (int i = 0; i < d->nhid && !d->gone; i++) {
        hidif_t *f = &d->hif[i];
        if ((f->is_kbd || f->kb) && f->ledgen != kbd_ledgen) { f->ledgen = kbd_ledgen; u8 l = kbd_leds; ctrl(d, 0x21, 9, 0x200, f->ifn, 1, &l); }
        if (ticks < d->hnext[i]) continue;
        d->hnext[i] = ticks + d->hival[i]; u32 got = 0;
        int r = xfer(d, &d->hep[i], 1, -1, f->rep, f->rep_len, 20, &got);
        if (!r && got) hid_report(f, got); else if (r == -6 && !d->gone) clear_halt(d, &d->hep[i]);
    }
}
static void thread_(void *a) {
    u64 nr = 0, nh = 0;
    for (;;) {
        for (ddev_t *d = devs; d; d = d->next) if (!d->gone && d->nhid) hid_poll(d);
        if (ticks >= nr) { nr = ticks + 100; root_poll(); }
        if (ticks >= nh) { nh = ticks + 300; for (ddev_t *d = devs; d; d = d->next) if (!d->gone && d->is_hub) hub_poll(d); }
        msleep(2);
    }
}

/* --- puesta en marcha --- */
static int wait_clear(u32 reg, u32 bits, int ms) { for (int t = 0; t < ms * 10; t++) { if (!(G(reg) & bits)) return 0; udelay(100); } return -1; }
void dwc_init(void) {
    static u32 m[8] __attribute__((aligned(16)));
    /* encender el USB (dispositivo 3) y esperar a que este encendido; el codigo de peticion va a 0, como en la SD */
    int on = 0;
    for (int k = 0; k < 3 && !on; k++) {
        m[0] = 32; m[1] = 0; m[2] = 0x28001; m[3] = 8; m[4] = 0; m[5] = 3; m[6] = 3; m[7] = 0;
        int r = mbox_call(m); on = r && (m[6] & 1);
        kprintf("dwc: encendido por el firmware: %s (respuesta %x, estado %x)\n", on ? "si" : "no", m[1], m[6]);
        if (!on) msleep(100);
    }
    u32 id = G(GSNPSID);   /* aunque el firmware no conteste, si el nucleo responde se sigue */
    if ((id & 0xFFFFF000u) != 0x4F542000u) { kprintf("dwc: no hay controlador DWC2 (%x)\n", id); return; }
    G(GAHBCFG) = 0; G(GINTMSK) = 0;
    for (int t = 0; t < 1000 && !(G(GRSTCTL) & (1u << 31)); t++) udelay(100);   /* bus AHB en reposo */
    G(GRSTCTL) = 1; if (wait_clear(GRSTCTL, 1, 100)) kprintf("dwc: el reinicio del nucleo no termina\n");
    for (int t = 0; t < 1000 && !(G(GRSTCTL) & (1u << 31)); t++) udelay(100);
    msleep(100);
    u32 u = G(GUSBCFG); u &= ~(1u << 30 | 3u << 8); u |= 1u << 29; G(GUSBCFG) = u; msleep(50);   /* solo anfitrion, sin SRP/HNP */
    G(PCGCCTL) = 0; G(HCFG) = G(HCFG) & ~7u;   /* reloj de 30/60 MHz para la PHY UTMI+ de alta velocidad */
    u32 depth = G(GHWCFG3) >> 16; if (depth < 1024) depth = 4096;
    u32 rx = depth / 4 > 1024 ? 1024 : depth / 4, np = rx, pt = rx;
    G(GRXFSIZ) = rx; G(GNPTXFSIZ) = np << 16 | rx; G(HPTXFSIZ) = pt << 16 | (rx + np);
    G(GRSTCTL) = 1u << 5 | 0x10u << 6; wait_clear(GRSTCTL, 1u << 5, 100);   /* vaciar las FIFO de envio */
    G(GRSTCTL) = 1u << 4; wait_clear(GRSTCTL, 1u << 4, 100);                 /* y la de recepcion */
    G(GAHBCFG) = 1u << 5 | 3u << 1;   /* DMA, rafagas INCR4; sin interrupcion global */
    G(GINTSTS) = ~0u;
    nch = MIN((int)((G(GHWCFG2) >> 14) & 15) + 1, NCH);
    for (int c = 0; c < nch; c++) {
        if (HC(c, HCCHAR) & CHENA) ch_halt(c);
        u8 *v = dma_alloc(BB); dma_sync(v, BB);   /* desde aqui solo se usa la vista sin cache */
        ch[c].bb = ioremap_wc(V2P(v), BB); ch[c].bus = (u32)V2P(v) | 0xC0000000u;
    }
    u32 h = G(HPRT); G(HPRT) = (h & ~HPRT_W1C) | 1u << 12;   /* alimentacion del puerto */
    kprintf("dwc: USB 2.0 OTG %x listo, %d canales, FIFO %u palabras\n", id, nch, depth);
    kthread("usb-dwc", thread_, 0);
}
