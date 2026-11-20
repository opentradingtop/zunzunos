/* USB 3 (xHCI): hubs, HID (teclado, raton, tableta) y almacenamiento masivo */
#include "kernel.h"
#include "usb.h"
#include "usbnet.h"
#define TRB_NORMAL 1
#define TRB_SETUP 2
#define TRB_DATA 3
#define TRB_STATUS 4
#define TRB_LINK 6
#define TRB_ENSLOT 9
#define TRB_DISSLOT 10
#define TRB_ADDR 11
#define TRB_CFGEP 12
#define TRB_EVAL 13
#define TRB_RSTEP 14
#define TRB_SETDQ 16
#define RING 256
#define W64(a, v) do { u64 _v = (v); R32(a) = (u32)_v; R32((volatile u8 *)(a) + 4) = (u32)(_v >> 32); } while (0)
typedef struct { u32 *t; u64 ph; u32 i; u32 cyc; } ring_t;
typedef struct xhc xhc_t;
typedef struct usbdev usbdev_t;
typedef struct { volatile int busy; volatile u32 code, left; ring_t r; int mps, type, used; } ep_t;
struct usbdev {
    xhc_t *h; int slot, speed, port, addr_port, is_hub, nports, depth, gone; u32 route; usbdev_t *parent; int parent_port;
    u8 *in, *out, *buf; ep_t ep[32]; u16 vid, pid; u8 dd[18]; u32 hub_state[16]; int tt;
    hidif_t hif[4]; int nhid;
    /* MSC */
    int msc, bin, bout; u8 *cbw; u32 tag; blkdev_t *blk; u8 lun; struct usbdev *next;
    int prn_if, prn_alt, pout; u8 *pbuf;
    int net_kind, net_ctl, net_data, net_alt, net_mac, nin, nout; u8 *nbo; u32 nreq;
};
struct xhc {
    pci_t *pci; volatile u8 *cap, *op, *rt, *db; int csz, slots, ports; u64 *dcbaa; ring_t cmd; u32 *ev; u64 ev_ph; u32 ev_i, ev_cyc; u64 *erst;
    volatile int cmd_busy; volatile u32 cmd_code, cmd_slot; mutex_t cmd_lock; usbdev_t *dev[256]; usbdev_t *root[64]; usbdev_t *list; volatile u32 pchg;
};
#define OP(r) R32(h->op + (r))
#define PORT(n) R32(h->op + 0x400 + ((n) - 1) * 16)
#define PNEUTRAL 0x0E00C3E0u
static void ring_init(ring_t *r) { r->t = dma_alloc(RING * 16); r->ph = V2P(r->t); r->i = 0; r->cyc = 1; }
static void ring_put(ring_t *r, u32 a, u32 b, u32 c, u32 d) {
    u32 *t = r->t + r->i * 4; t[0] = a; t[1] = b; t[2] = c; mb(); t[3] = d | r->cyc; r->i++;
    if (r->i == RING - 1) { u32 *l = r->t + r->i * 4; l[0] = r->ph; l[1] = r->ph >> 32; l[2] = 0; mb(); l[3] = TRB_LINK << 10 | 2 | r->cyc | (d & 0x10); r->i = 0; r->cyc ^= 1; }
}
static void xpoll(xhc_t *h) {
    u64 fl = irq_save(); int any = 0;
    for (;;) {
        u32 *e = h->ev + h->ev_i * 4; u32 ctl = R32(&e[3]);
        if ((ctl & 1) != h->ev_cyc) break;
        u32 type = (ctl >> 10) & 63, st = R32(&e[2]);
        if (type == 33) { h->cmd_code = st >> 24; h->cmd_slot = ctl >> 24; h->cmd_busy = 0; }
        else if (type == 32) { usbdev_t *d = h->dev[ctl >> 24]; u32 dci = (ctl >> 16) & 31; if (d) { ep_t *p = &d->ep[dci]; p->code = st >> 24; p->left = st & 0xFFFFFF; p->busy = 0; } }
        else if (type == 34) h->pchg = 1;
        if (++h->ev_i == RING) { h->ev_i = 0; h->ev_cyc ^= 1; }
        any = 1;
    }
    if (any) { u64 p = (h->ev_ph + h->ev_i * 16) | 8; R32(h->rt + 0x38) = p; R32(h->rt + 0x3C) = p >> 32; }
    irq_restore(fl);
}
static int cmd(xhc_t *h, u32 a, u32 b, u32 c, u32 d, u32 *slot) {
    mutex_lock(&h->cmd_lock); h->cmd_busy = 1; ring_put(&h->cmd, a, b, c, d); mb(); R32(h->db) = 0;
    u64 end = ticks + 3000; while (h->cmd_busy && ticks < end) { xpoll(h); yield(); }
    int r = h->cmd_busy ? -1 : (h->cmd_code == 1 ? 0 : -(int)h->cmd_code); if (slot) *slot = h->cmd_slot;
    mutex_unlock(&h->cmd_lock); return r;
}
static u32 *ictx(usbdev_t *d, int i) { return (u32 *)(d->in + d->h->csz * i); }
static int wait_ep(usbdev_t *d, int dci, int ms) {
    xhc_t *h = d->h; ep_t *p = &d->ep[dci]; u64 end = ticks + ms;
    while (p->busy) { xpoll(h); if (d->gone) return -1; if (ms && ticks > end) return -2; yield(); }
    return p->code == 1 || p->code == 13 ? 0 : -(int)p->code;
}
static void ep_recover(usbdev_t *d, int dci);
static int ctrl(usbdev_t *d, u8 rt, u8 rq, u16 val, u16 idx, u16 len, void *data) {
    ep_t *p = &d->ep[1]; ring_t *r = &p->r; int in = rt & 0x80; xhc_t *h = d->h;
    if (len && !in && data) memcpy(d->buf, data, len);
    p->busy = 1;
    ring_put(r, rt | rq << 8 | (u32)val << 16, idx | (u32)len << 16, 8, TRB_SETUP << 10 | 1 << 6 | (len ? (in ? 3u : 2u) << 16 : 0));
    if (len) { u64 ph = V2P(d->buf); ring_put(r, ph, ph >> 32, len, TRB_DATA << 10 | (in ? 1 << 16 : 0)); }
    ring_put(r, 0, 0, 0, TRB_STATUS << 10 | 1 << 5 | ((len && in) ? 0 : 1 << 16));
    mb(); R32(h->db + d->slot * 4) = 1;
    int e = wait_ep(d, 1, 3000);
    if (e) { if (e != -1) ep_recover(d, 1); return e; }
    if (len && in && data) memcpy(data, d->buf, len);
    return 0;
}
static void ep_queue(usbdev_t *d, int dci, u64 ph, u32 len) {
    ep_t *p = &d->ep[dci]; ring_t *r = &p->r; int in = dci & 1; u32 mps = p->mps ? p->mps : 512; (void)in;
    p->busy = 1; u32 rest = len;
    do {
        u32 c = MIN(rest, 65536 - (ph & 0xFFFF)); u32 after = rest - c; u32 td = MIN((after + mps - 1) / mps, 31U);
        ring_put(r, ph, ph >> 32, c | td << 17, TRB_NORMAL << 10 | (after ? 1 << 4 : 1 << 5) | 1 << 2);
        ph += c; rest = after;
    } while (rest);
    mb(); R32(d->h->db + d->slot * 4) = dci;
}
static void ep_recover(usbdev_t *d, int dci) {
    xhc_t *h = d->h; ep_t *p = &d->ep[dci];
    cmd(h, 0, 0, 0, TRB_RSTEP << 10 | dci << 16 | d->slot << 24, 0);
    u64 q = (p->r.ph + p->r.i * 16) | p->r.cyc; cmd(h, q, q >> 32, 0, TRB_SETDQ << 10 | dci << 16 | d->slot << 24, 0);
    p->busy = 0;
}
static int ep_of(u8 addr) { return (addr & 15) * 2 + (addr >> 7); }
static void ep_cfg(usbdev_t *d, int dci, int type, int mps, int interval) {
    xhc_t *h = d->h; ep_t *p = &d->ep[dci]; ring_init(&p->r); p->mps = mps; p->type = type; p->used = 1; (void)h;
    u32 *e = ictx(d, dci + 1); memset(e, 0, h->csz);
    e[0] = interval << 16; e[1] = 3 << 1 | type << 3 | mps << 16; u64 q = p->r.ph | 1; e[2] = q; e[3] = q >> 32; e[4] = type == 7 || type == 3 ? mps | (u32)mps << 16 : 1024;
    ictx(d, 0)[1] |= 1u << dci;
}
static int cfg_commit(usbdev_t *d, int maxdci) {
    u32 *c = ictx(d, 0); c[0] = 0; c[1] |= 1;
    u32 *s = ictx(d, 1); memcpy(s, d->out, d->h->csz); s[0] = (s[0] & ~(31u << 27)) | (u32)maxdci << 27; s[3] &= ~0xFF000000u; s[3] &= 0x00FFFFFF;
    u64 ph = V2P(d->in); return cmd(d->h, ph, ph >> 32, 0, TRB_CFGEP << 10 | d->slot << 24, 0);
}

/* --- HID --- */
static void hid_arm(usbdev_t *d, hidif_t *i) { ep_queue(d, i->dci, V2P(i->rep), i->rep_len); }

/* --- almacenamiento: Bulk-Only + SCSI --- */
static int bot(usbdev_t *d, const u8 *cdb, int clen, void *buf, u32 len, int in) {
    u8 *w = d->cbw; memset(w, 0, 64); u32 tag = ++d->tag;
    *(u32 *)w = 0x43425355; *(u32 *)(w + 4) = tag; *(u32 *)(w + 8) = len; w[12] = in ? 0x80 : 0; w[13] = d->lun; w[14] = clen; memcpy(w + 15, cdb, clen);
    ep_queue(d, d->bout, V2P(w), 31); int e = wait_ep(d, d->bout, 5000);
    if (e) { if (e != -1) ep_recover(d, d->bout); return -1; }
    if (len) {
        int ep = in ? d->bin : d->bout; ep_queue(d, ep, V2P(buf), len); e = wait_ep(d, ep, 30000);
        if (e == -1) return -1;
        if (e) ep_recover(d, ep);
    }
    ep_queue(d, d->bin, V2P(w + 32), 13); e = wait_ep(d, d->bin, 5000);
    if (e == -6 || e == -4) { ep_recover(d, d->bin); ep_queue(d, d->bin, V2P(w + 32), 13); e = wait_ep(d, d->bin, 5000); }
    if (e) { if (e != -1) ep_recover(d, d->bin); return -1; }
    if (*(u32 *)(w + 32) != 0x53425355 || *(u32 *)(w + 36) != tag) { kprintf("usb: CSW invalido\n"); return -1; }
    if (*(u32 *)(w + 40)) kprintf("usb: residuo CSW %u de %u\n", *(u32 *)(w + 40), len);
    return w[32 + 12] ? -2 : 0;
}
static int scsi_rw(blkdev_t *b, u64 lba, u32 n, void *buf, int wr) {
    usbdev_t *d = b->drv; u8 c[10] = {wr ? 0x2A : 0x28, 0, lba >> 24, lba >> 16, lba >> 8, lba, 0, n >> 8, n, 0};
    for (int t = 0; t < 3; t++) {
        int r = bot(d, c, 10, buf, n * b->ssize, !wr); if (!r) return 0; if (r == -1 && d->gone) return -1;
        kprintf("usb: scsi %s lba=%lu n=%u fallo r=%d codigo=%u\n", wr ? "escritura" : "lectura", lba, n, r, d->ep[wr ? d->bout : d->bin].code);
        u8 s[6] = {3, 0, 0, 0, 18, 0}; bot(d, s, 6, d->cbw + 128, 18, 1); msleep(20);
    }
    return -1;
}
static int msc_rd(blkdev_t *b, u64 l, u32 n, void *p) { return scsi_rw(b, l, n, p, 0); }
static int msc_wr(blkdev_t *b, u64 l, u32 n, const void *p) { return scsi_rw(b, l, n, (void *)p, 1); }
static int msc_fl(blkdev_t *b) { u8 c[10] = {0x35}; bot(b->drv, c, 10, 0, 0, 0); return 0; }
static void msc_start(void *arg) {
    usbdev_t *d = arg; u8 *data = dma_alloc(4096); u8 inq[6] = {0x12, 0, 0, 0, 36, 0}, tur[6] = {0}, cap[10] = {0x25}, sense[6] = {3, 0, 0, 0, 18, 0};
    int ok = 0;
    if (bot(d, inq, 6, data, 36, 1)) { kprintf("usb: INQUIRY fallo\n"); return; }
    char model[41]; memcpy(model, data + 8, 24); model[24] = 0; for (int i = 23; i >= 0 && model[i] == ' '; i--) model[i] = 0;
    for (int t = 0; t < 40 && !d->gone; t++) { int r = bot(d, tur, 6, 0, 0, 0); if (!r) { ok = 1; break; } bot(d, sense, 6, data, 18, 1); msleep(100); }
    if (!ok || bot(d, cap, 10, data, 8, 1)) { kprintf("usb: el medio no esta listo\n"); return; }
    u32 last = data[0] << 24 | data[1] << 16 | data[2] << 8 | data[3], sz = data[4] << 24 | data[5] << 16 | data[6] << 8 | data[7];
    blkdev_t *b = blk_new("usb"); b->drv = d; b->ssize = sz; b->sectors = (u64)last + 1; strncpy0(b->model, model, 41); b->read = msc_rd; b->write = msc_wr; b->flush = msc_fl; b->removable = 1; b->maxsect = 120 * 1024 / sz;
    d->blk = b; blk_register(b); dma_free(data, 4096);
}

/* --- enumeracion --- */
static void dev_free(usbdev_t *d) {
    xhc_t *h = d->h; d->gone = 1; for (usbdev_t *c = h->list; c; c = c->next) if (c->parent == d && !c->gone) dev_free(c);
    if (d->blk) blk_remove(d->blk);
    kprintf("usb: dispositivo retirado (ranura %d)\n", d->slot);
    cmd(h, 0, 0, 0, TRB_DISSLOT << 10 | d->slot << 24, 0); h->dcbaa[d->slot] = 0; h->dev[d->slot] = 0;
}
static void enumerate(xhc_t *h, int port, int speed, usbdev_t *hub, int hport);
static int xprn_wr(void *ctx, const void *p, u32 n) { usbdev_t *d = ctx; if (d->gone) return -1; memcpy(d->pbuf, p, n); ep_queue(d, d->pout, V2P(d->pbuf), n); int e = wait_ep(d, d->pout, 90000); if (e && e != -1) ep_recover(d, d->pout); return e ? -5 : (int)n; }
static int xn_ctrl(void *dv, u8 rt, u8 rq, u16 v, u16 i, u16 l, void *p) { return ctrl(dv, rt, rq, v, i, l, p); }
static int xn_out(void *dv, const void *p, u32 n) { usbdev_t *d = dv; if (d->gone) return -1; memcpy(d->nbo, p, n); if (n % 64 == 0) d->nbo[n++] = 0; ep_queue(d, d->nout, V2P(d->nbo), n); int e = wait_ep(d, d->nout, 2000); if (e && e != -1) ep_recover(d, d->nout); return e ? -5 : (int)n; }
static int xn_in_start(void *dv, void *b, u32 n) { usbdev_t *d = dv; if (d->gone) return -1; d->nreq = n; ep_queue(d, d->nin, V2P(b), n); return 0; }
static int xn_in_poll(void *dv) { usbdev_t *d = dv; ep_t *p = &d->ep[d->nin]; if (p->busy) { xpoll(d->h); if (p->busy) return d->gone ? -2 : -1; } if (p->code == 1 || p->code == 13) return d->nreq - p->left; if (!d->gone) ep_recover(d, d->nin); return -2; }
static void setup_iface(usbdev_t *d) {
    u8 *cfg = kmalloc(1024), *hr = kmalloc(1024);
    int ci = 0, ncfg = d->dd[17]; if (ncfg > 1) for (int k = 0; k < ncfg && k < 8; k++) { int kn; if (ctrl(d, 0x80, 6, 0x200 | k, 0, 9, cfg)) continue; int t2 = MIN(*(u16 *)(cfg + 2), 1024); if (ctrl(d, 0x80, 6, 0x200 | k, 0, t2, cfg)) continue; int net = usbnet_pick_cfg(cfg, t2, k, &kn); if (k == 0 && kn && !net) break; if (net) { ci = k; break; } }
    if (ci) kprintf("usb: se usa la configuracion %d (red)\n", ci + 1);
    if (ctrl(d, 0x80, 6, 0x200 | ci, 0, 9, cfg)) goto out;
    int tl = MIN(*(u16 *)(cfg + 2), 1024); if (ctrl(d, 0x80, 6, 0x200 | ci, 0, tl, cfg)) goto out;
    u8 cv = cfg[5]; int kind = 0, maxdci = 1, msc_if = -1, hub = 0; hidif_t *ch = 0; int hrl[4] = {0}, msc_taken = 0;
    memset(ictx(d, 0), 0, d->h->csz);
    for (int i = 0; i + 2 <= tl && cfg[i]; i += cfg[i]) {
        u8 *x = cfg + i; int t = x[1];
        if (t == 4) {
            int cls = x[5], sub = x[6], prot = x[7]; kind = 0; ch = 0;
            int iph = usbnet_is_ipheth(cls, sub, prot);
            if (x[3] && !(cls == 0x0A && d->net_kind == 2 && !d->nin) && !(iph && d->net_kind == 3 && !d->nin)) continue;
            if (iph && !x[3] && !d->net_kind) { d->net_kind = 3; d->net_ctl = x[2]; kind = 0; continue; }
            int nk = usbnet_ctl_kind(cls, sub, prot);
            if (x[3]) { kind = 5; d->net_alt = x[3]; d->net_data = x[2]; continue; }
            if (nk && !d->net_kind) { kind = 6; d->net_kind = nk; d->net_ctl = x[2]; continue; }
            if (cls == 0x0A && d->net_kind && !d->nin) { kind = 5; d->net_data = x[2]; continue; }
            if (cls == 3 && d->nhid < 4) { ch = &d->hif[d->nhid]; memset(ch, 0, sizeof *ch); ch->ifn = x[2]; ch->is_kbd = sub == 1 && prot == 1; kind = 1; }
            else if (cls == 8 && prot == 0x50 && !msc_taken) { kind = 2; msc_if = x[2]; }
            else if (cls == 9) { kind = 3; hub = 1; }
            else if (cls == 7 && sub == 1 && (prot == 1 || prot == 2) && !d->pout) { kind = 4; d->prn_if = x[2]; d->prn_alt = x[3]; }
        } else if (t == 0x21 && kind == 1) hrl[d->nhid] = *(u16 *)(x + 7);
        else if (t == 0x24 && kind == 6 && x[2] == 0x0F) d->net_mac = x[3];
        else if (t == 5 && kind) {
            int a = x[2], at = x[3] & 3, mps = *(u16 *)(x + 4) & 0x7FF, iv = x[6], dci = ep_of(a), in = a >> 7, itv = 0;
            if (at != 2 && at != 3) continue;
            if (at == 3) { if (d->speed >= 3) itv = MIN(MAX(iv, 1) - 1, 15); else { int ms = MAX(iv, 1) * 8, l2 = 0; while ((1 << (l2 + 1)) <= ms) l2++; itv = MIN(MAX(l2, 3), 10); } }
            if (kind == 1 && !(in && at == 3)) continue;
            ep_cfg(d, dci, at == 2 ? (in ? 6 : 2) : (in ? 7 : 3), mps, itv); if (dci > maxdci) maxdci = dci;
            if (kind == 1 && ch && !ch->dci) { ch->dci = dci; ch->rep_len = MIN(mps, 64); d->nhid++; ch = 0; kind = 0; }
            else if (kind == 2 && at == 2) { if (in) d->bin = dci; else d->bout = dci; if (d->bin && d->bout) msc_taken = 1; }
            else if (kind == 4 && at == 2 && !in) d->pout = dci;
            else if (kind == 5 && at == 2) { if (in) d->nin = dci; else d->nout = dci; }
        }
    }
    if (maxdci > 1 && cfg_commit(d, maxdci)) { kprintf("usb: configure endpoint fallo\n"); goto out; }
    if (ctrl(d, 0, 9, cv, 0, 0, 0)) goto out;
    for (int i = 0; i < d->nhid; i++) {
        hidif_t *h = &d->hif[i]; h->rep = dma_alloc(4096); h->id = (u32)d->vid << 16 | d->pid;
        if (h->is_kbd) ctrl(d, 0x21, 0x0B, 0, h->ifn, 0, 0);
        else if (hrl[i]) { int n = MIN(hrl[i], 1024); if (!ctrl(d, 0x81, 6, 0x2200, h->ifn, n, hr)) hid_parse(h, hr, n); if (h->rep_len < 8) h->rep_len = 8; }
        ctrl(d, 0x21, 0x0A, 0, h->ifn, 0, 0);
        kprintf("usb: HID %s (ranura %d)%s\n", h->is_kbd || h->kb ? "teclado" : h->joy ? "mando" : h->xs ? "puntero" : "otro", d->slot, h->absolute ? " absoluto" : "");
        hid_arm(d, h);
    }
    if (d->nin && d->nout && d->net_kind) { d->nbo = dma_alloc(4096); usbnet_t u = {d, &d->gone, d->net_kind, d->net_ctl, d->net_data, d->net_alt, d->net_mac, xn_ctrl, xn_out, xn_in_start, xn_in_poll}; usbnet_attach(&u); }
    if (d->pout) { d->pbuf = dma_alloc(16384); u8 *id = kmalloc(1024); memset(id, 0, 1024); if (ctrl(d, 0xA1, 0, 0, d->prn_if << 8 | d->prn_alt, 1023, id)) id[0] = id[1] = 0; usbprn_add(d->vid, d->pid, id, xprn_wr, d, &d->gone); kfree(id); }
    if (msc_taken) { (void)msc_if; d->msc = 1; d->cbw = dma_alloc(4096); kprintf("usb: almacenamiento (ranura %d)\n", d->slot); kthread("usb-msc", msc_start, d); }
    else if (hub) {
        d->is_hub = 1; u8 hd[12]; int ss = d->speed >= 4;
        if (ss) ctrl(d, 0, 12, d->depth, 0, 0, 0);
        if (ctrl(d, 0xA0, 6, (ss ? 0x2A : 0x29) << 8, 0, 12, hd)) goto out;
        d->nports = MIN((int)hd[2], 15);
        u32 *c0 = ictx(d, 0); c0[0] = 0; c0[1] = 1; u32 *s = ictx(d, 1); memcpy(s, d->out, d->h->csz); s[0] |= 1 << 26; s[1] = (s[1] & 0xFFFFFF) | (u32)d->nports << 24; if (d->speed == 3) s[2] = (s[2] & ~(3u << 16)) | ((hd[3] >> 5) & 3) << 16;
        u64 ph = V2P(d->in); cmd(d->h, ph, ph >> 32, 0, TRB_EVAL << 10 | d->slot << 24, 0);
        for (int p = 1; p <= d->nports; p++) ctrl(d, 0x23, 3, 8, p, 0, 0);
        kprintf("usb: hub con %d puertos\n", d->nports);
    }
out: kfree(cfg); kfree(hr);
}
static const int ep0mps[] = {0, 8, 8, 64, 512, 512, 512, 512};
static void enumerate(xhc_t *h, int port, int speed, usbdev_t *hub, int hport) {
    u32 slot = 0; if (cmd(h, 0, 0, 0, TRB_ENSLOT << 10, &slot) || !slot) { kprintf("usb: sin ranuras\n"); return; }
    usbdev_t *d = kzalloc(sizeof *d); d->h = h; d->slot = slot; d->speed = speed; d->port = port; d->parent = hub; d->parent_port = hport;
    d->in = dma_alloc(33 * h->csz + 4096); d->out = dma_alloc(32 * h->csz + 4096); d->buf = dma_alloc(4096); h->dcbaa[slot] = V2P(d->out); h->dev[slot] = d;
    d->route = hub ? hub->route | (u32)MIN(hport, 15) << (hub->depth * 4) : 0; d->depth = hub ? hub->depth + 1 : 0;
    ring_init(&d->ep[1].r); int mps = ep0mps[speed & 7]; d->ep[1].mps = mps;
    u32 *ic = ictx(d, 0), *s = ictx(d, 1), *e = ictx(d, 2); ic[1] = 3;
    s[0] = d->route | speed << 20 | 1u << 27; s[1] = (u32)port << 16;
    if (hub && speed < 3) { usbdev_t *tt = hub->speed == 3 ? hub : hub->tt ? h->dev[hub->tt] : 0; if (tt) { s[2] = tt->slot | (hub->speed == 3 ? hport : hub->parent_port) << 8; d->tt = tt->slot; } }
    e[1] = 3 << 1 | 4 << 3 | mps << 16; u64 q = d->ep[1].r.ph | 1; e[2] = q; e[3] = q >> 32; e[4] = 8;
    u64 ph = V2P(d->in);
    int r = cmd(h, ph, ph >> 32, 0, TRB_ADDR << 10 | slot << 24, 0);
    if (r) { kprintf("usb: address device fallo %d\n", r); cmd(h, 0, 0, 0, TRB_DISSLOT << 10 | slot << 24, 0); h->dev[slot] = 0; h->dcbaa[slot] = 0; return; }
    msleep(10);
    if (ctrl(d, 0x80, 6, 0x100, 0, 8, d->dd)) { kprintf("usb: no responde (ranura %u)\n", slot); h->dev[slot] = 0; return; }
    int real = speed >= 4 ? 1 << d->dd[7] : d->dd[7];
    if (real != mps && real) { memset(ic, 0, h->csz); ic[1] = 2; memcpy(e, (u8 *)d->out + h->csz, h->csz); e[1] = (e[1] & 0xFFFF) | real << 16; d->ep[1].mps = real; cmd(h, ph, ph >> 32, 0, TRB_EVAL << 10 | slot << 24, 0); }
    if (ctrl(d, 0x80, 6, 0x100, 0, 18, d->dd)) return;
    d->vid = *(u16 *)(d->dd + 8); d->pid = *(u16 *)(d->dd + 10);
    kprintf("usb: %04x:%04x clase %d velocidad %d puerto %d\n", d->vid, d->pid, d->dd[4], speed, hub ? hport : port);
    u64 fl = irq_save(); d->next = h->list; h->list = d; irq_restore(fl);
    if (hub) hub->hub_state[hport] = (u64)d >> 0 ? slot : 0; else h->root[port] = d;
    setup_iface(d);
}
static void root_port(xhc_t *h, int p) {
    u32 v = PORT(p);
    if (v & (1 << 17) || v & (1 << 21) || v & (1 << 22) || v & (1 << 19) || v & (1 << 18) || v & (1 << 23) || v & (1 << 20)) PORT(p) = (v & PNEUTRAL) | (v & 0xFE0000);
    v = PORT(p); int conn = v & 1; usbdev_t *d = h->root[p];
    if (!conn && d) { h->root[p] = 0; dev_free(d); return; }
    if (!conn || d) return;
    if (!(v & 2)) { PORT(p) = (v & PNEUTRAL) | 1 << 4; for (int i = 0; i < 100; i++) { msleep(5); if (PORT(p) & (1 << 21)) break; } msleep(20); v = PORT(p); PORT(p) = (v & PNEUTRAL) | 1 << 21 | 1 << 17; v = PORT(p); }
    if (!(v & 2)) return;
    msleep(20); enumerate(h, p, (PORT(p) >> 10) & 15, 0, 0);
}
static void hub_poll(usbdev_t *hb) {
    xhc_t *h = hb->h; int ss = hb->speed >= 4;
    for (int p = 1; p <= hb->nports && !hb->gone; p++) {
        u32 st; if (ctrl(hb, 0xA3, 0, 0, p, 4, &st)) return;
        int conn = st & 1; usbdev_t *c = hb->hub_state[p] ? h->dev[hb->hub_state[p]] : 0;
        if (st >> 16) { for (int b = 0; b < 8; b++) if (st & (1u << (16 + b))) ctrl(hb, 0x23, 1, 16 + b, p, 0, 0); }
        if (!conn && c) { hb->hub_state[p] = 0; dev_free(c); continue; }
        if (!conn || c) continue;
        ctrl(hb, 0x23, 3, 4, p, 0, 0);
        for (int t = 0; t < 50; t++) { msleep(10); if (ctrl(hb, 0xA3, 0, 0, p, 4, &st)) return; if (st & (1 << 20)) break; }
        ctrl(hb, 0x23, 1, 20, p, 0, 0); msleep(20); if (ctrl(hb, 0xA3, 0, 0, p, 4, &st)) return;
        if (!(st & 2)) continue;
        int sp = ss ? 4 : (st & (1 << 9)) ? 2 : (st & (1 << 10)) ? 3 : 1;
        enumerate(h, hb->port, sp, hb, p);
    }
}
static void xhc_thread(void *a) {
    xhc_t *h = a; u64 next_hub = 0;
    for (int p = 1; p <= h->ports; p++) root_port(h, p);
    kprintf("usb: controlador listo, %d puertos\n", h->ports);
    for (;;) {
        xpoll(h);
        if (h->pchg) { h->pchg = 0; for (int p = 1; p <= h->ports; p++) root_port(h, p); }
        for (usbdev_t *d = h->list; d; d = d->next) {
            if (d->gone) continue;
            for (int i = 0; i < d->nhid; i++) {
                hidif_t *hi = &d->hif[i]; if ((hi->is_kbd || hi->kb) && hi->ledgen != kbd_ledgen) { hi->ledgen = kbd_ledgen; u8 l = kbd_leds; ctrl(d, 0x21, 9, 0x200, hi->ifn, 1, &l); } ep_t *p = &d->ep[hi->dci]; if (p->busy) continue;
                if (p->code == 1 || p->code == 13) hid_report(hi, hi->rep_len - p->left); else ep_recover(d, hi->dci);
                hid_arm(d, hi);
            }
        }
        if (ticks >= next_hub) { next_hub = ticks + 250; for (usbdev_t *d = h->list; d; d = d->next) if (d->is_hub && !d->gone && d->nports) hub_poll(d); }
        msleep(2);
    }
}
static void xhc_start(pci_t *p) {
    if (p->vendor == 0x8086) { pci_write(p, 0xD8, pci_read(p, 0xDC)); pci_write(p, 0xD0, pci_read(p, 0xD4)); }
    pci_enable(p, 1, 0, 1);
    xhc_t *h = kzalloc(sizeof *h); h->pci = p; h->cap = ioremap(pci_bar(p, 0), 0x10000);
    u32 cl = R8(h->cap), hcs1 = R32(h->cap + 4), hcs2 = R32(h->cap + 8), hcc = R32(h->cap + 16);
    h->op = h->cap + cl; h->rt = h->cap + (R32(h->cap + 0x18) & ~31u); h->db = h->cap + (R32(h->cap + 0x14) & ~3u); h->csz = hcc & 4 ? 64 : 32;
    h->slots = MIN(hcs1 & 255, 64U); h->ports = MIN((hcs1 >> 24) & 255, 63U);
    /* traspaso desde la BIOS */
    u32 x = (hcc >> 16) * 4;
    while (x) { volatile u32 *c = (volatile u32 *)(h->cap + x); u32 v = *c;
        if ((v & 255) == 1) { *c = v | 1 << 24; for (int i = 0; i < 200 && (*c & (1 << 16)); i++) msleep(5); c[1] &= 0xE1EE; }
        u32 n = (v >> 8) & 255; x = n ? x + n * 4 : 0; }
    OP(0) &= ~1u; for (int i = 0; i < 200 && !(OP(4) & 1); i++) msleep(2);
    OP(0) |= 2; for (int i = 0; i < 500 && (OP(0) & 2); i++) msleep(2);
    for (int i = 0; i < 500 && (OP(4) & (1 << 11)); i++) msleep(2);
    OP(0x38) = h->slots; h->dcbaa = dma_alloc(4096);
    u32 sp = ((hcs2 >> 27) & 31) | ((hcs2 >> 21) & 31) << 5;
    if (sp) { u64 *arr = dma_alloc(4096); for (u32 i = 0; i < sp; i++) arr[i] = V2P(dma_alloc(4096)); h->dcbaa[0] = V2P(arr); }
    W64(h->op + 0x30, V2P(h->dcbaa)); ring_init(&h->cmd); W64(h->op + 0x18, h->cmd.ph | 1);
    h->ev = dma_alloc(RING * 16); h->ev_ph = V2P(h->ev); h->ev_cyc = 1; h->erst = dma_alloc(4096); h->erst[0] = h->ev_ph; h->erst[1] = RING;
    R32(h->rt + 0x28) = 1; W64(h->rt + 0x38, h->ev_ph); W64(h->rt + 0x30, V2P(h->erst)); OP(0) |= 1;
    for (int i = 0; i < 100 && (OP(4) & 1); i++) msleep(2);
    for (int i = 1; i <= h->ports; i++) if (!(PORT(i) & (1 << 9))) PORT(i) = (PORT(i) & PNEUTRAL) | 1 << 9;
    msleep(30);
    kthread("xhci", xhc_thread, h);
}
void xhci_init(void) { for (int i = 0; i < pci_count; i++) { pci_t *p = &pci_devs[i]; if (p->cls == 0xC && p->sub == 3 && p->pif == 0x30) xhc_start(p); } }
