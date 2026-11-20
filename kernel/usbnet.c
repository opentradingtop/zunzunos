/* Red por USB: anclaje de moviles Android (RNDIS) y adaptadores Ethernet CDC-ECM.
   Comun a xHCI y EHCI: el controlador aporta control, envio bulk y recepcion bulk asincrona. */
#include "kernel.h"
#include "usbnet.h"
#define RX 16384
typedef struct { usbnet_t u; netdev_t n; u8 *rb, *tb; int rlen, roff, pending, reqid; } un_t;
static int rndis_cmd(un_t *x, u8 *msg, int len, u32 want, u8 *resp) {
    usbnet_t *u = &x->u; *(u32 *)(msg + 8) = ++x->reqid;
    if (u->ctrl(u->dev, 0x21, 0, 0, u->ctl_if, len, msg)) { kprintf("usb-red: envio de orden %x fallo\n", *(u32 *)msg); return -5; }
    for (int t = 0; t < 100; t++) { memset(resp, 0, 1024); if (!u->ctrl(u->dev, 0xA1, 1, 0, u->ctl_if, 1024, resp)) { if (*(u32 *)resp == want) { if (*(u32 *)(resp + 12)) kprintf("usb-red: respuesta %x estado %x\n", want, *(u32 *)(resp + 12)); return *(u32 *)(resp + 12) == 0 ? 0 : -5; } kprintf("usb-red: mensaje %x\n", *(u32 *)resp); } msleep(10); if (*u->gone) return -1; }
    return -2;
}
static int un_tx(netdev_t *n, const void *p, u32 l) {
    un_t *x = n->priv; usbnet_t *u = &x->u; if (*u->gone) return -1;
    if (u->kind == 3) { memcpy(x->tb, p, l); memset(x->tb + l, 0, 1514 - l); return u->out(u->dev, x->tb, 1514) < 0 ? -1 : 0; }
    if (u->kind == 1) { u8 *h = x->tb; memset(h, 0, 44); *(u32 *)h = 1; *(u32 *)(h + 4) = 44 + l; *(u32 *)(h + 8) = 36; *(u32 *)(h + 12) = l; memcpy(h + 44, p, l); return u->out(u->dev, h, 44 + l) < 0 ? -1 : 0; }
    memcpy(x->tb, p, l); return u->out(u->dev, x->tb, l) < 0 ? -1 : 0;
}
static int un_rx(netdev_t *n, void *o) {
    un_t *x = n->priv; usbnet_t *u = &x->u; if (*u->gone) return 0;
    if (x->roff < x->rlen) {
        if (u->kind == 2) { int l = x->rlen; x->roff = x->rlen; if (l > 1514) return -1; memcpy(o, x->rb, l); return l; }
        if (u->kind == 3) { int l = x->rlen - 2; x->roff = x->rlen; if (l <= 0 || l > 1514) return -1; memcpy(o, x->rb + 2, l); return l; }
        u8 *m = x->rb + x->roff; u32 ml = *(u32 *)(m + 4), doff = *(u32 *)(m + 8), dl = *(u32 *)(m + 12);
        if (*(u32 *)m != 1 || ml < 44 || x->roff + ml > (u32)x->rlen) { x->roff = x->rlen; return -1; }
        x->roff += ml; if (dl > 1514 || 8 + doff + dl > ml) return -1; memcpy(o, m + 8 + doff, dl); return dl;
    }
    if (!x->pending) { if (u->in_start(u->dev, x->rb, u->kind == 3 ? 1516 : RX)) return 0; x->pending = 1; }
    int r = u->in_poll(u->dev); if (r == -1) return 0; x->pending = 0; if (r < 0) return -1; x->rlen = r; x->roff = 0; return un_rx(n, o);
}
static int un_link(netdev_t *n) { un_t *x = n->priv; if (*x->u.gone) return 0; if (x->u.kind != 3) return 1;
    static u8 cb[64]; static u64 last; static int on; if (ticks - last > 2000) { last = ticks; cb[0] = 0; if (!x->u.ctrl(x->u.dev, 0xC0, 0x45, 0, x->u.data_if, 64, cb)) { int c = cb[0] == 4; if (c != on) kprintf("usb-red: iPhone %s (estado %x)\n", c ? "comparte internet" : "sin compartir: activa Compartir internet y pulsa Confiar", cb[0]); on = c; } } return on; }
static int hexv(int c) { return c >= '0' && c <= '9' ? c - '0' : (c | 32) - 'a' + 10; }
static void un_start(void *a) {
    un_t *x = a; usbnet_t *u = &x->u; u8 *m = kmalloc(1024), *r = kmalloc(1024); x->rb = dma_alloc(RX); x->tb = dma_alloc(2048);
    if (u->kind == 1) {
        memset(m, 0, 28); *(u32 *)m = 2; *(u32 *)(m + 4) = 24; *(u32 *)(m + 12) = 1; *(u32 *)(m + 20) = RX; int e = rndis_cmd(x, m, 24, 0x80000002, r);
        if (e) { kprintf("usb-red: RNDIS no se inicia (%d)\n", e); goto out; }
        memset(m, 0, 76); *(u32 *)m = 4; *(u32 *)(m + 4) = 76; *(u32 *)(m + 12) = 0x01010101; *(u32 *)(m + 16) = 48; *(u32 *)(m + 20) = 20; e = rndis_cmd(x, m, 76, 0x80000004, r);
        if (!e && *(u32 *)(r + 16) >= 6) memcpy(x->n.mac, r + 8 + *(u32 *)(r + 20), 6); else { x->n.mac[0] = 2; x->n.mac[1] = 0x5A; *(u32 *)(x->n.mac + 2) = (u32)rdtsc(); }
        memset(m, 0, 32); *(u32 *)m = 5; *(u32 *)(m + 4) = 32; *(u32 *)(m + 12) = 0x0001010E; *(u32 *)(m + 16) = 4; *(u32 *)(m + 20) = 20; *(u32 *)(m + 28) = 0x0B; e = rndis_cmd(x, m, 32, 0x80000005, r);
        if (e) { kprintf("usb-red: RNDIS filtro fallo (%d)\n", e); goto out; }
        x->n.kind = "USB RNDIS";
    } else if (u->kind == 3) {
        u->ctrl(u->dev, 0x01, 11, u->data_alt ? u->data_alt : 1, u->data_if, 0, 0); memset(r, 0, 16);
        if (!u->ctrl(u->dev, 0xC0, 0x00, 0, u->data_if, 6, r) && (r[0] | r[1] | r[2] | r[3] | r[4] | r[5])) memcpy(x->n.mac, r, 6); else { x->n.mac[0] = 2; x->n.mac[1] = 0x5A; *(u32 *)(x->n.mac + 2) = (u32)rdtsc(); }
        x->n.kind = "iPhone USB";
    } else {
        int ok = 0; if (u->mac_idx && !u->ctrl(u->dev, 0x80, 6, 0x300 | u->mac_idx, 0x0409, 255, r) && r[0] >= 26) { for (int i = 0; i < 6; i++) x->n.mac[i] = hexv(r[2 + i * 4]) << 4 | hexv(r[4 + i * 4]); ok = 1; }
        if (!ok) { x->n.mac[0] = 2; x->n.mac[1] = 0x5A; *(u32 *)(x->n.mac + 2) = (u32)rdtsc(); }
        if (u->data_alt) u->ctrl(u->dev, 0x01, 11, u->data_alt, u->data_if, 0, 0);
        u->ctrl(u->dev, 0x21, 0x43, 0x0E, u->ctl_if, 0, 0); x->n.kind = "USB Ethernet";
    }
    strncpy0(x->n.name, "usb0", 8); x->n.priv = x; x->n.tx = un_tx; x->n.rx = un_rx; x->n.link = un_link; x->n.gone = u->gone; net_register(&x->n);
out: kfree(m); kfree(r);
}
void usbnet_attach(const usbnet_t *u) { un_t *x = kzalloc(sizeof *x); x->u = *u; kprintf("usb-red: %s (interfaz %d/%d)\n", u->kind == 1 ? "RNDIS (movil Android)" : u->kind == 3 ? "iPhone (Apple)" : "CDC-ECM", u->ctl_if, u->data_if); kthread("usb-red", un_start, x); }
