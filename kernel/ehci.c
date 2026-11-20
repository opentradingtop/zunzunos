/* USB 2.0 (EHCI). Incluye hubs y transacciones divididas (split) para teclados y
 * ratones de baja/plena velocidad detras del hub interno de los chipsets Intel. */
#include "kernel.h"
#include "usb.h"
#include "usbnet.h"
#define TERM 1u
#define ST_ACTIVE 0x80u
#define ST_HALT 0x40u
#define ST_ERR 0x7Cu
#define P_OUT 0
#define P_IN 1
#define P_SETUP 2
#define NTD 9
enum { FS, LS, HS };
typedef struct { volatile u32 next, alt, token, buf[5], bufh[5]; u32 pad[3]; } qtd_t;
typedef struct { volatile u32 link, info1, info2, cur, next, alt, token, buf[5], bufh[5]; u32 pad[15]; } qh_t;
typedef struct { qh_t *qh; qtd_t *td[NTD]; int mps, num, in, periodic; } eep_t;
typedef struct ehc ehc_t;
typedef struct edev {
    ehc_t *h; struct edev *next, *hub, *child[16]; int addr, speed, gone, port, tt_addr, tt_port, is_hub, nports, nhid, rootp;
    eep_t ep0, hep[4], bin, bout, pout, nin, nout; int prn_if, prn_alt, net_kind, net_ctl, net_data, net_alt, net_mac; u8 *pbuf, *nbo; u32 nreq; hidif_t hif[4]; u8 *buf, *cbw; u8 dd[18]; u32 tag; blkdev_t *blk; u8 lun;
} edev_t;
struct ehc {
    volatile u8 *cap, *op; int ports, next_addr, id; u32 *frames; qh_t *ahead, *phead[8]; int pload[8]; u8 *pool; int pool_off;
    mutex_t list; edev_t *root[16], *devs;
};
#define OP(r) R32(h->op + (r))
#define PSC(i) R32(h->op + 0x44 + (i) * 4)
#define PA(x) ((u32)V2P((const void *)(x)))
static void *chunk(ehc_t *h) {
    mutex_lock(&h->list);
    if (!h->pool || h->pool_off >= 4096) { h->pool = dma_alloc(4096); h->pool_off = 0; }
    void *p = h->pool + h->pool_off; h->pool_off += 128; mutex_unlock(&h->list); return p;
}
static void td_fill(qtd_t *t, int pid, int tog, u64 ph, u32 len, qtd_t *next, int ioc) {
    t->next = next ? PA(next) : TERM; t->alt = TERM; u64 pg = ph & ~0xFFFUL; t->buf[0] = ph;
    for (int i = 1; i < 5; i++) t->buf[i] = pg + i * 4096UL;
    for (int i = 0; i < 5; i++) t->bufh[i] = 0;
    mb(); t->token = (u32)tog << 31 | len << 16 | (u32)ioc << 15 | 3u << 10 | (u32)pid << 8 | ST_ACTIVE;
}
static void doorbell(ehc_t *h) {
    OP(4) = 1u << 5; OP(0) |= 1u << 6; u64 e = ticks + 100;
    while (!(OP(4) & (1u << 5)) && ticks < e) yield();
    OP(4) = 1u << 5;
}
static void async_add(ehc_t *h, qh_t *q) { mutex_lock(&h->list); q->link = h->ahead->link; mb(); h->ahead->link = PA(q) | 2; mb(); mutex_unlock(&h->list); }
static void list_del(qh_t *head, qh_t *q) { qh_t *p = head; for (int i = 0; i < 512; i++) { u32 l = p->link; if (l & 1) return; if ((l & ~31u) == PA(q)) { p->link = q->link; mb(); return; } p = P2V(l & ~31u); if (p == head) return; } }
static void async_del(ehc_t *h, qh_t *q) { mutex_lock(&h->list); list_del(h->ahead, q); doorbell(h); mutex_unlock(&h->list); }
static void ep_init(edev_t *d, eep_t *e, int num, int in, int mps, int type) {
    ehc_t *h = d->h; e->qh = chunk(h); e->mps = mps; e->num = num; e->in = in; e->periodic = type == 3; qh_t *q = e->qh;
    for (int i = 0; i < NTD; i++) if (!e->td[i]) e->td[i] = chunk(h);
    u32 i1 = (u32)d->addr | (u32)num << 8 | (u32)d->speed << 12 | (u32)mps << 16, i2 = 1u << 30;
    if (type == 0) { i1 |= 1 << 14; if (d->speed != HS) i1 |= 1u << 27; }
    if (d->speed != HS) i2 |= (u32)d->tt_port << 23 | (u32)d->tt_addr << 16;
    if (type == 3) i2 |= d->speed == HS ? 1u : (1u | 0x1Cu << 8);
    q->info1 = i1; q->info2 = i2; q->cur = 0; q->next = TERM; q->alt = TERM; q->token = 0; q->link = TERM;
    if (type != 3) async_add(h, q);
    else { mutex_lock(&h->list); int b = 0; for (int k = 1; k < 8; k++) if (h->pload[k] < h->pload[b]) b = k; h->pload[b]++; q->link = h->phead[b]->link; mb(); h->phead[b]->link = PA(q) | 2; mutex_unlock(&h->list); }
}
static void ep_drop(edev_t *d, eep_t *e) {
    ehc_t *h = d->h; if (!e->qh) return;
    if (e->periodic) { mutex_lock(&h->list); for (int b = 0; b < 8; b++) list_del(h->phead[b], e->qh); mutex_unlock(&h->list); msleep(12); } else async_del(h, e->qh);
    e->qh = 0;
}
static void ep_clear(eep_t *e, int keep) { for (int i = 0; i < NTD; i++) e->td[i]->token = 0; qh_t *q = e->qh; q->next = TERM; q->alt = TERM; mb(); q->token = keep ? q->token & 0x80000000u : 0; }
static int ep_wait(edev_t *d, eep_t *e, qtd_t *last, int ms) {
    u64 end = ticks + ms;
    for (;;) {
        u32 t = last->token; if (!(t & ST_ACTIVE)) { if (t & ST_ERR) { int stall = !(t & 0x3C); ep_clear(e, 1); return stall ? -6 : -4; } return 0; }
        u32 q = e->qh->token; if (q & ST_HALT) { int stall = !(q & 0x3C); ep_clear(e, 1); return stall ? -6 : -4; }
        if (d->gone) return -1;
        if (ticks > end) { if (!e->periodic) { async_del(d->h, e->qh); ep_clear(e, 1); async_add(d->h, e->qh); } return -2; }
        yield();
    }
}
static int ctrl(edev_t *d, u8 rt, u8 rq, u16 val, u16 idx, u16 len, void *data) {
    eep_t *e = &d->ep0; int in = rt & 0x80; u8 *s = d->buf + 3840; if (len > 3840) return -22;
    s[0] = rt; s[1] = rq; *(u16 *)(s + 2) = val; *(u16 *)(s + 4) = idx; *(u16 *)(s + 6) = len;
    if (len && !in && data) memcpy(d->buf, data, len);
    qtd_t *a = e->td[0], *b = e->td[1], *c = e->td[2];
    td_fill(c, in && len ? P_OUT : P_IN, 1, V2P(d->buf + 3900), 0, 0, 1);
    if (len) td_fill(b, in ? P_IN : P_OUT, 1, V2P(d->buf), len, c, 0);
    td_fill(a, P_SETUP, 0, V2P(s), 8, len ? b : c, 0);
    mb(); e->qh->next = PA(a); int r = ep_wait(d, e, c, 2000);
    if (!r && len && in && data) memcpy(data, d->buf, len);
    return r;
}
static int ctrl_retry(edev_t *d, u8 rt, u8 rq, u16 v, u16 i, u16 l, void *p) { int r = -1; for (int t = 0; t < 3 && !d->gone; t++) { r = ctrl(d, rt, rq, v, i, l, p); if (!r) return 0; msleep(20); } return r; }
static int bulk(edev_t *d, eep_t *e, void *buf, u32 len, int ms) {
    u64 ph = V2P(buf); int n = 0; u32 rest = len; qtd_t *prev = 0;
    do {
        u32 c = MIN(rest, 16384U); if (n >= NTD) return -22; qtd_t *t = e->td[n];
        td_fill(t, e->in ? P_IN : P_OUT, 0, ph, c, rest > c ? e->td[n + 1 < NTD ? n + 1 : n] : 0, rest <= c); prev = t; ph += c; rest -= c; n++;
    } while (rest);
    mb(); e->qh->next = PA(e->td[0]); return ep_wait(d, e, prev, ms);
}
static void clear_halt(edev_t *d, eep_t *e) { ctrl(d, 2, 1, 0, e->num | (e->in ? 0x80 : 0), 0, 0); ep_clear(e, 0); }
/* --- almacenamiento --- */
static int bot(edev_t *d, const u8 *cdb, int cl, void *buf, u32 len, int in) {
    u8 *w = d->cbw; memset(w, 0, 64); u32 tag = ++d->tag; *(u32 *)w = 0x43425355; *(u32 *)(w + 4) = tag; *(u32 *)(w + 8) = len; w[12] = in ? 0x80 : 0; w[13] = d->lun; w[14] = cl; memcpy(w + 15, cdb, cl);
    int e = bulk(d, &d->bout, w, 31, 5000); if (e) { if (e == -6) clear_halt(d, &d->bout); return -1; }
    if (len) { eep_t *p = in ? &d->bin : &d->bout; e = bulk(d, p, buf, len, 30000); if (e == -1) return -1; if (e == -6) clear_halt(d, p); else if (e) return -1; }
    e = bulk(d, &d->bin, w + 32, 13, 5000); if (e == -6) { clear_halt(d, &d->bin); e = bulk(d, &d->bin, w + 32, 13, 5000); } if (e) return -1;
    if (*(u32 *)(w + 32) != 0x53425355 || *(u32 *)(w + 36) != tag) return -1;
    return w[44] ? -2 : 0;
}
static int scsi_rw(blkdev_t *b, u64 lba, u32 n, void *buf, int wr) {
    edev_t *d = b->drv; u8 c[10] = {wr ? 0x2A : 0x28, 0, lba >> 24, lba >> 16, lba >> 8, lba, 0, n >> 8, n, 0};
    for (int t = 0; t < 3; t++) { int r = bot(d, c, 10, buf, n * b->ssize, !wr); if (!r) return 0; if (d->gone) return -1; kprintf("ehci: scsi lba=%lu fallo %d\n", lba, r); u8 s[6] = {3, 0, 0, 0, 18, 0}; bot(d, s, 6, d->cbw + 128, 18, 1); msleep(20); }
    return -1;
}
static int msc_rd(blkdev_t *b, u64 l, u32 n, void *p) { return scsi_rw(b, l, n, p, 0); }
static int msc_wr(blkdev_t *b, u64 l, u32 n, const void *p) { return scsi_rw(b, l, n, (void *)p, 1); }
static int msc_fl(blkdev_t *b) { u8 c[10] = {0x35}; bot(b->drv, c, 10, 0, 0, 0); return 0; }
static void msc_start(void *a) {
    edev_t *d = a; u8 *x = dma_alloc(4096); u8 inq[6] = {0x12, 0, 0, 0, 36, 0}, tur[6] = {0}, cap[10] = {0x25}, se[6] = {3, 0, 0, 0, 18, 0}; int ok = 0;
    if (bot(d, inq, 6, x, 36, 1)) { kprintf("ehci: INQUIRY fallo\n"); return; }
    char m[41]; memcpy(m, x + 8, 24); m[24] = 0; for (int i = 23; i >= 0 && m[i] == ' '; i--) m[i] = 0;
    for (int t = 0; t < 40 && !d->gone; t++) { if (!bot(d, tur, 6, 0, 0, 0)) { ok = 1; break; } bot(d, se, 6, x, 18, 1); msleep(100); }
    if (!ok || bot(d, cap, 10, x, 8, 1)) { kprintf("ehci: medio no listo\n"); return; }
    u32 l = x[0] << 24 | x[1] << 16 | x[2] << 8 | x[3], sz = x[4] << 24 | x[5] << 16 | x[6] << 8 | x[7];
    blkdev_t *b = blk_new("usb"); b->drv = d; b->ssize = sz; b->sectors = (u64)l + 1; strncpy0(b->model, m, 41); b->read = msc_rd; b->write = msc_wr; b->flush = msc_fl; b->removable = 1; b->maxsect = 120 * 1024 / sz; d->blk = b; blk_register(b); dma_free(x, 4096);
}
/* --- enumeracion --- */
static void hid_arm(edev_t *d, int i) { eep_t *e = &d->hep[i]; hidif_t *f = &d->hif[i]; td_fill(e->td[0], P_IN, 0, V2P(f->rep), f->rep_len, 0, 0); mb(); e->qh->next = PA(e->td[0]); }
static int eprn_wr(void *ctx, const void *p, u32 n) { edev_t *d = ctx; if (d->gone) return -1; memcpy(d->pbuf, p, n); int e = bulk(d, &d->pout, d->pbuf, n, 90000); if (e) { if (!d->gone) clear_halt(d, &d->pout); return -5; } return (int)n; }
static int en_ctrl(void *dv, u8 rt, u8 rq, u16 v, u16 i, u16 l, void *p) { return ctrl(dv, rt, rq, v, i, l, p); }
static int en_out(void *dv, const void *p, u32 n) { edev_t *d = dv; if (d->gone) return -1; memcpy(d->nbo, p, n); if (n % 64 == 0) d->nbo[n++] = 0; int e = bulk(d, &d->nout, d->nbo, n, 2000); if (e) { if (!d->gone) clear_halt(d, &d->nout); return -5; } return (int)n; }
static int en_in_start(void *dv, void *b, u32 n) { edev_t *d = dv; if (d->gone) return -1; d->nreq = n; eep_t *e = &d->nin; td_fill(e->td[0], P_IN, 0, V2P(b), n, 0, 1); mb(); e->qh->next = PA(e->td[0]); return 0; }
static int en_in_poll(void *dv) { edev_t *d = dv; eep_t *e = &d->nin; u32 t = e->td[0]->token; if (t & ST_ACTIVE) { if (e->qh->token & ST_HALT) { ep_clear(e, 1); if (!d->gone) clear_halt(d, e); return -2; } return d->gone ? -2 : -1; } if (t & ST_ERR) { ep_clear(e, 1); if (!d->gone) clear_halt(d, e); return -2; } return d->nreq - ((t >> 16) & 0x7FFF); }
static void configure(edev_t *d) {
    u8 *c = kmalloc(1024), *hr = kmalloc(1024); int hrl[4] = {0}, kind = 0, msc = 0, hub = 0; hidif_t *ch = 0; ehc_t *h = d->h; (void)h;
    int ci = 0, ncfg = d->dd[17]; if (ncfg > 1) for (int k = 0; k < ncfg && k < 8; k++) { int kn; if (ctrl_retry(d, 0x80, 6, 0x200 | k, 0, 9, c)) continue; int t2 = MIN(*(u16 *)(c + 2), 1024); if (ctrl_retry(d, 0x80, 6, 0x200 | k, 0, t2, c)) continue; int net = usbnet_pick_cfg(c, t2, k, &kn); if (k == 0 && kn && !net) break; if (net) { ci = k; break; } }
    if (ci) kprintf("ehci: se usa la configuracion %d (red)\n", ci + 1);
    if (ctrl_retry(d, 0x80, 6, 0x200 | ci, 0, 9, c)) goto out;
    int tl = MIN(*(u16 *)(c + 2), 1024); if (ctrl_retry(d, 0x80, 6, 0x200 | ci, 0, tl, c)) goto out; u8 cv = c[5];
    for (int i = 0; i + 2 <= tl && c[i]; i += c[i]) {
        u8 *x = c + i; int t = x[1];
        if (t == 4) { int cl = x[5], su = x[6], pr = x[7]; kind = 0; ch = 0; int iph = usbnet_is_ipheth(cl, su, pr); if (x[3] && !(cl == 0x0A && d->net_kind == 2 && !d->nin.qh) && !(iph && d->net_kind == 3 && !d->nin.qh)) continue; if (iph && !x[3] && !d->net_kind) { d->net_kind = 3; d->net_ctl = x[2]; kind = 0; continue; } int nk = usbnet_ctl_kind(cl, su, pr); if (x[3]) { kind = 5; d->net_alt = x[3]; d->net_data = x[2]; continue; } if (nk && !d->net_kind) { kind = 6; d->net_kind = nk; d->net_ctl = x[2]; continue; } if (cl == 0x0A && d->net_kind && !d->nin.qh) { kind = 5; d->net_data = x[2]; continue; } if (cl == 3 && d->nhid < 4) { ch = &d->hif[d->nhid]; memset(ch, 0, sizeof *ch); ch->ifn = x[2]; ch->is_kbd = su == 1 && pr == 1; kind = 1; } else if (cl == 8 && pr == 0x50 && !msc) kind = 2; else if (cl == 7 && su == 1 && (pr == 1 || pr == 2) && !d->pout.qh) { kind = 4; d->prn_if = x[2]; d->prn_alt = x[3]; } else if (cl == 9) { kind = 3; hub = 1; } }
        else if (t == 0x21 && kind == 1) hrl[d->nhid] = *(u16 *)(x + 7);
        else if (t == 0x24 && kind == 6 && x[2] == 0x0F) d->net_mac = x[3];
        else if (t == 5 && kind) {
            int a = x[2], at = x[3] & 3, mps = *(u16 *)(x + 4) & 0x7FF, in = a >> 7, n = a & 15;
            if (kind == 1 && ch && in && at == 3 && !ch->dci) { ch->dci = n; ch->rep_len = MIN(mps, 64); ep_init(d, &d->hep[d->nhid], n, 1, mps, 3); d->nhid++; ch = 0; kind = 0; }
            else if (kind == 2 && at == 2) { eep_t *e = in ? &d->bin : &d->bout; ep_init(d, e, n, in, mps, 2); if (d->bin.qh && d->bout.qh) msc = 1; }
            else if (kind == 4 && at == 2 && !in && !d->pout.qh) ep_init(d, &d->pout, n, 0, mps, 2);
            else if (kind == 5 && at == 2) { eep_t *e = in ? &d->nin : &d->nout; if (!e->qh) ep_init(d, e, n, in, mps, 2); }
        }
    }
    if (ctrl_retry(d, 0, 9, cv, 0, 0, 0)) { kprintf("ehci: SET_CONFIGURATION fallo\n"); goto out; }
    for (int i = 0; i < d->nhid; i++) {
        hidif_t *f = &d->hif[i]; f->rep = dma_alloc(4096); f->id = (u32)*(u16 *)(d->dd + 8) << 16 | *(u16 *)(d->dd + 10);
        if (f->is_kbd) ctrl(d, 0x21, 0x0B, 0, f->ifn, 0, 0); else if (hrl[i]) { int n = MIN(hrl[i], 1024); if (!ctrl_retry(d, 0x81, 6, 0x2200, f->ifn, n, hr)) hid_parse(f, hr, n); }
        ctrl(d, 0x21, 0x0A, 0, f->ifn, 0, 0); kprintf("ehci: HID %s dir %d velocidad %d%s\n", f->is_kbd || f->kb ? "teclado" : f->joy ? "mando" : f->xs ? "puntero" : "otro", d->addr, d->speed, f->absolute ? " absoluto" : ""); hid_arm(d, i);
    }
    if (d->nin.qh && d->nout.qh && d->net_kind) { d->nbo = dma_alloc(4096); usbnet_t u = {d, &d->gone, d->net_kind, d->net_ctl, d->net_data, d->net_alt, d->net_mac, en_ctrl, en_out, en_in_start, en_in_poll}; usbnet_attach(&u); }
    if (d->pout.qh) { d->pbuf = dma_alloc(16384); u8 *id = kmalloc(1024); memset(id, 0, 1024); if (ctrl(d, 0xA1, 0, 0, d->prn_if << 8 | d->prn_alt, 1023, id)) id[0] = id[1] = 0; usbprn_add(*(u16 *)(d->dd + 8), *(u16 *)(d->dd + 10), id, eprn_wr, d, &d->gone); kfree(id); }
    if (msc) { d->cbw = dma_alloc(4096); kprintf("ehci: almacenamiento dir %d\n", d->addr); kthread("usb-msc", msc_start, d); }
    else if (hub) {
        u8 hd[12]; if (ctrl_retry(d, 0xA0, 6, 0x2900, 0, 12, hd)) goto out; d->is_hub = 1; d->nports = MIN((int)hd[2], 15);
        for (int p = 1; p <= d->nports; p++) ctrl(d, 0x23, 3, 8, p, 0, 0);
        msleep(MAX(hd[5] * 2, 100)); kprintf("ehci: hub dir %d con %d puertos\n", d->addr, d->nports);
    }
out: kfree(c); kfree(hr);
}
static edev_t *enumerate(ehc_t *h, int speed, edev_t *hub, int port) {
    edev_t *d = kzalloc(sizeof *d); d->h = h; d->speed = speed; d->hub = hub; d->port = port; d->buf = dma_alloc(4096);
    if (speed != HS && hub) { if (hub->speed == HS) { d->tt_addr = hub->addr; d->tt_port = port; } else { d->tt_addr = hub->tt_addr; d->tt_port = hub->tt_port; } }
    ep_init(d, &d->ep0, 0, 0, speed == HS ? 64 : 8, 0);
    int r = ctrl_retry(d, 0x80, 6, 0x100, 0, 8, d->dd);
    if (r) { kprintf("ehci: sin respuesta en puerto %d velocidad %d (%d) tt=%d/%d\n", port, speed, r, d->tt_addr, d->tt_port); ep_drop(d, &d->ep0); return 0; }
    int mps = d->dd[7] ? d->dd[7] : 8, addr = h->next_addr; h->next_addr = addr >= 127 ? 1 : addr + 1;
    if (ctrl_retry(d, 0, 5, addr, 0, 0, 0)) { kprintf("ehci: SET_ADDRESS fallo\n"); ep_drop(d, &d->ep0); return 0; }
    msleep(10); ep_drop(d, &d->ep0); d->addr = addr; ep_init(d, &d->ep0, 0, 0, mps, 0);
    if (ctrl_retry(d, 0x80, 6, 0x100, 0, 18, d->dd)) { kprintf("ehci: descriptor fallo dir %d\n", addr); ep_drop(d, &d->ep0); return 0; }
    kprintf("ehci: %04x:%04x clase %d dir %d velocidad %d puerto %d hub %d\n", *(u16 *)(d->dd + 8), *(u16 *)(d->dd + 10), d->dd[4], addr, speed, port, hub ? hub->addr : 0);
    u64 f = irq_save(); d->next = h->devs; h->devs = d; irq_restore(f); configure(d); return d;
}
static void dev_gone(edev_t *d) {
    d->gone = 1; for (int i = 0; i < 16; i++) if (d->child[i]) { dev_gone(d->child[i]); d->child[i] = 0; }
    if (d->blk) blk_remove(d->blk); for (int i = 0; i < d->nhid; i++) ep_drop(d, &d->hep[i]); ep_drop(d, &d->bin); ep_drop(d, &d->bout); ep_drop(d, &d->ep0); kprintf("ehci: dispositivo %d retirado\n", d->addr);
}
static void root_poll(ehc_t *h, int i) {
    u32 v = PSC(i); u32 keep = v & ~(u32)0x2A; if (v & 0x2A) PSC(i) = keep | (v & 0x2A);
    edev_t *d = h->root[i]; int conn = v & 1;
    if (!conn && d) { h->root[i] = 0; dev_gone(d); return; }
    if ((v & 2) && conn && d) { h->root[i] = 0; dev_gone(d); d = 0; }
    if (!conn || d || (v & (1 << 13))) return;
    msleep(100); v = PSC(i); if (!(v & 1)) return;
    if (((v >> 10) & 3) == 1) { PSC(i) = (v & ~(u32)0x2A) | 1 << 13; kprintf("ehci%d: puerto %d baja velocidad, cedido\n", h->id, i + 1); return; }
    v &= ~(u32)0x2E; PSC(i) = v | 1 << 8; msleep(55); PSC(i) = (PSC(i) & ~(u32)0x2A) & ~(1u << 8);
    for (int t = 0; t < 100 && (PSC(i) & (1 << 8)); t++) msleep(1); msleep(20); v = PSC(i);
    if (v & 0x2A) { PSC(i) = v; v = PSC(i); }
    if (!(v & 4)) { PSC(i) = (v & ~(u32)0x2A) | 1 << 13; kprintf("ehci%d: puerto %d plena velocidad, cedido\n", h->id, i + 1); return; }
    h->root[i] = enumerate(h, HS, 0, i + 1);
}
static void hub_poll(edev_t *hb) {
    ehc_t *h = hb->h;
    for (int p = 1; p <= hb->nports && !hb->gone; p++) {
        u32 st; if (ctrl(hb, 0xA3, 0, 0, p, 4, &st)) return; int conn = st & 1; edev_t *c = hb->child[p];
        for (int b = 0; b < 5; b++) if (st & (1u << (16 + b))) ctrl(hb, 0x23, 1, 16 + b, p, 0, 0);
        if (c && (!conn || (st & (1 << 16)))) { hb->child[p] = 0; dev_gone(c); c = 0; }
        if (!conn || c) continue;
        msleep(100); ctrl(hb, 0x23, 3, 4, p, 0, 0); int ok = 0;
        for (int t = 0; t < 50; t++) { msleep(10); if (ctrl(hb, 0xA3, 0, 0, p, 4, &st)) return; if (st & (1 << 20)) { ok = 1; break; } }
        ctrl(hb, 0x23, 1, 20, p, 0, 0); ctrl(hb, 0x23, 1, 16, p, 0, 0); msleep(20); if (!ok || ctrl(hb, 0xA3, 0, 0, p, 4, &st) || !(st & 2)) { kprintf("ehci: reset fallo hub %d puerto %d st=%x\n", hb->addr, p, st); continue; }
        hb->child[p] = enumerate(h, (st & (1 << 10)) ? HS : (st & (1 << 9)) ? LS : FS, hb, p);
    }
}
static void thread_(void *a) {
    ehc_t *h = a; u64 nr = 0, nh = 0;
    for (;;) {
        for (edev_t *d = h->devs; d; d = d->next) { if (d->gone) continue;
            for (int i = 0; i < d->nhid; i++) { eep_t *e = &d->hep[i]; hidif_t *f = &d->hif[i]; if ((f->is_kbd || f->kb) && f->ledgen != kbd_ledgen) { f->ledgen = kbd_ledgen; u8 l = kbd_leds; ctrl(d, 0x21, 9, 0x200, f->ifn, 1, &l); } u32 t = e->td[0]->token, q = e->qh->token;
                if (q & ST_HALT) { ep_clear(e, 1); hid_arm(d, i); continue; }
                if (t & ST_ACTIVE) continue;
                if (!(t & ST_ERR)) hid_report(f, f->rep_len - ((t >> 16) & 0x7FFF)); hid_arm(d, i); } }
        if (ticks >= nr) { nr = ticks + 100; for (int i = 0; i < h->ports; i++) root_poll(h, i); }
        if (ticks >= nh) { nh = ticks + 300; for (edev_t *d = h->devs; d; d = d->next) if (!d->gone && d->is_hub) hub_poll(d); }
        msleep(2);
    }
}
static void start(pci_t *p, int id) {
    pci_enable(p, 1, 0, 1); ehc_t *h = kzalloc(sizeof *h); h->id = id; h->next_addr = 1; h->cap = ioremap(pci_bar(p, 0), 4096); u32 hcs = R32(h->cap + 4), hcc = R32(h->cap + 8); h->op = h->cap + R8(h->cap); h->ports = MIN(hcs & 15, 15U);
    for (u32 o = (hcc >> 8) & 255, g = 0; o >= 0x40 && g < 16; g++) { u32 v = pci_read(p, o); if ((v & 255) == 1) { pci_write(p, o, v | 1 << 24); for (int i = 0; i < 200 && (pci_read(p, o) & (1 << 16)); i++) msleep(5); pci_write(p, o + 4, 0); } o = (v >> 8) & 255; }
    OP(0) &= ~1u; for (int i = 0; i < 100 && !(OP(4) & (1 << 12)); i++) msleep(1); OP(0) |= 2; for (int i = 0; i < 250 && (OP(0) & 2); i++) msleep(1);
    OP(0x10) = 0; OP(8) = 0; OP(4) = 0x3F; h->frames = dma_alloc(4096);
    h->ahead = chunk(h); h->ahead->info1 = 1 << 15 | HS << 12; h->ahead->info2 = 1u << 30; h->ahead->next = TERM; h->ahead->alt = TERM; h->ahead->token = ST_HALT; h->ahead->link = PA(h->ahead) | 2;
    for (int i = 0; i < 8; i++) { qh_t *q = chunk(h); q->info1 = HS << 12; q->info2 = 1u << 30; q->next = TERM; q->alt = TERM; q->token = ST_HALT; q->link = TERM; h->phead[i] = q; }
    for (int i = 0; i < 1024; i++) h->frames[i] = PA(h->phead[i % 8]) | 2;
    OP(0x14) = PA(h->frames); OP(0x18) = PA(h->ahead); mb(); OP(0) = 8u << 16 | 1 << 5 | 1 << 4 | 1; for (int i = 0; i < 100 && (OP(4) & (1 << 12)); i++) msleep(1); OP(0x40) = 1; msleep(5);
    if (hcs & 16) for (int i = 0; i < h->ports; i++) PSC(i) = (PSC(i) & ~(u32)0x2A) | 1 << 12;
    msleep(30); kprintf("ehci%d: %04x:%04x listo, %d puertos\n", id, p->vendor, p->device, h->ports); char n[8]; snprintf(n, 8, "ehci%d", id); kthread(n, thread_, h);
}
void ehci_init(void) { int id = 0; for (int i = 0; i < pci_count; i++) { pci_t *p = &pci_devs[i]; if (p->cls == 0xC && p->sub == 3 && p->pif == 0x20) start(p, id++); } }
