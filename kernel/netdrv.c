/* Drivers de red: e1000/e1000e, RTL8139, RTL8168/8169 y virtio-net */
#include "kernel.h"
static int ndev;
static netdev_t *mk(const char *kind, void *priv) { netdev_t *n = kzalloc(sizeof *n); snprintf(n->name, 8, "eth%d", ndev++); n->kind = kind; n->priv = priv; return n; }
#define BUFSZ 2048
/* ===== e1000 ===== */
typedef struct { u64 addr; u16 len, csum; u8 st, err; u16 sp; } PACKED rxd_t;
typedef struct { u64 addr; u16 len; u8 cso, cmd, st, css; u16 sp; } PACKED txd_t;
typedef struct { volatile u8 *r; rxd_t *rx; txd_t *tx; u8 *rb, *tb; u32 ri, ti; } e1k_t;
#define NRX 64
#define NTX 64
#define ER(o) R32(e->r + (o))
static int e_tx(netdev_t *n, const void *p, u32 l) {
    e1k_t *e = n->priv; txd_t *d = &e->tx[e->ti];
    if (!(d->st & 1) && d->cmd) return -1;
    memcpy(e->tb + e->ti * BUFSZ, p, l); d->len = MAX(l, 60U); d->st = 0; d->cmd = 1 | 2 | 8; e->ti = (e->ti + 1) % NTX; mb(); ER(0x3818) = e->ti; return 0;
}
static int e_rx(netdev_t *n, void *o) {
    e1k_t *e = n->priv; rxd_t *d = &e->rx[e->ri]; if (!(d->st & 1)) return 0;
    int l = d->len; if (d->err || !(d->st & 2)) l = -1; else memcpy(o, e->rb + e->ri * BUFSZ, l);
    d->st = 0; ER(0x2818) = e->ri; e->ri = (e->ri + 1) % NRX; return l < 0 ? -1 : l;
}
static int e_link(netdev_t *n) { e1k_t *e = n->priv; return !!(ER(8) & 2); }
static int is_e1k(u16 d) { static const u16 l[] = {0x100E,0x100F,0x1004,0x1008,0x1009,0x100C,0x100D,0x1010,0x1011,0x1012,0x1013,0x1015,0x1016,0x1017,0x1018,0x1019,0x101A,0x101D,0x101E,0x1026,0x1027,0x1028,0x1075,0x1076,0x1077,0x1078,0x1079,0x107A,0x107B,0x107C,0x108A,0x10B9,0x10D3,0x10F6,0x150C}; for (unsigned i = 0; i < sizeof l / 2; i++) if (l[i] == d) return 1; return 0; }
static void e1k_init(pci_t *p) {
    pci_enable(p, 1, 1, 1); e1k_t *e = kzalloc(sizeof *e); e->r = ioremap(pci_bar(p, 0), 0x20000);
    ER(0xD8) = ~0u; ER(0) |= 1u << 26; msleep(10); for (int i = 0; i < 100 && (ER(0) & (1u << 26)); i++) msleep(1); msleep(5); ER(0xD8) = ~0u; (void)ER(0xC0);
    ER(0) = (ER(0) | 1 << 6 | 1 << 5) & ~(1u << 3 | 1u << 31 | 1u << 7);
    netdev_t *n = mk(p->device == 0x10D3 || p->device == 0x10F6 ? "e1000e" : "e1000", e);
    u32 lo = ER(0x5400), hi = ER(0x5404);
    if (!lo && !(hi & 0xFFFF)) { for (int w = 0; w < 3; w++) { ER(0x14) = w << 8 | 1; u32 v; int t = 0; do v = ER(0x14); while (!(v & 16) && ++t < 100000); n->mac[w * 2] = v >> 16; n->mac[w * 2 + 1] = v >> 24; } ER(0x5400) = *(u32 *)n->mac; ER(0x5404) = *(u16 *)(n->mac + 4) | 1u << 31; }
    else { memcpy(n->mac, &lo, 4); memcpy(n->mac + 4, &hi, 2); }
    for (int i = 0; i < 128; i++) ER(0x5200 + i * 4) = 0;
    e->rx = dma_alloc(NRX * 16); e->tx = dma_alloc(NTX * 16); e->rb = dma_alloc(NRX * BUFSZ); e->tb = dma_alloc(NTX * BUFSZ);
    for (int i = 0; i < NRX; i++) e->rx[i].addr = V2P(e->rb + i * BUFSZ);
    for (int i = 0; i < NTX; i++) { e->tx[i].addr = V2P(e->tb + i * BUFSZ); e->tx[i].st = 1; }
    u64 a = V2P(e->rx); ER(0x2800) = a; ER(0x2804) = a >> 32; ER(0x2808) = NRX * 16; ER(0x2810) = 0; ER(0x2818) = NRX - 1;
    a = V2P(e->tx); ER(0x3800) = a; ER(0x3804) = a >> 32; ER(0x3808) = NTX * 16; ER(0x3810) = 0; ER(0x3818) = 0;
    ER(0x100) = 2 | 1 << 15 | 1 << 26; ER(0x400) = 2 | 8 | 15 << 4 | 0x40 << 12; ER(0x410) = 10 | 8 << 10 | 6 << 20;
    n->tx = e_tx; n->rx = e_rx; n->link = e_link; net_register(n);
}
/* ----- PCH: 82577/82578/82579 (X220) e I217/I218. Conserva la configuracion del PHY ----- */
static int is_pch(u16 d) { static const u16 l[] = {0x10EA,0x10EB,0x10EF,0x10F0,0x1502,0x1503,0x153A,0x153B,0x1559,0x155A,0x15A0,0x15A1,0x15A2,0x15A3}; for (unsigned i = 0; i < sizeof l / 2; i++) if (l[i] == d) return 1; return 0; }
static int sw_lock(e1k_t *e) { for (int i = 0; i < 1000; i++) { ER(0xF00) |= 1 << 5; udelay(50); if (ER(0xF00) & (1 << 5)) return 1; msleep(1); } kprintf("red: PHY ocupado por el firmware\n"); return 0; }
static void sw_unlock(e1k_t *e) { ER(0xF00) &= ~(1u << 5); }
static int mdic(e1k_t *e, int op, int reg, u16 v) { ER(0x20) = v | (u32)reg << 16 | 2u << 21 | (u32)op << 26; for (int i = 0; i < 2000; i++) { udelay(5); u32 r = ER(0x20); if (r & (1u << 28)) { udelay(100); return r & (1u << 30) ? -1 : (int)(r & 0xFFFF); } } return -1; }
#define PHYR(r) mdic(e, 2, r, 0)
#define PHYW(r, v) mdic(e, 1, r, v)
static int phy_ok(e1k_t *e) { int a = PHYR(2); return a > 0 && a != 0xFFFF; }
static void pch_dbg2(netdev_t *n);
/* Estado de transmision que funciono en el X220 (prueba con variante 5): TXDCTL con GRAN sin umbrales, TCTL.MULR, TARC bit 0 y K1 activo */
#define PCH_TXDCTL (1u << 22 | 1u << 24)
static void pch_dbg(netdev_t *n) { e1k_t *e = n->priv; static int k, kicked; u32 tdh = ER(0x3810), tdt = ER(0x3818); if (k++ % 3 == 0) pch_dbg2(n); static int var; static u32 gtx; gtx += ER(0x4080); if (tdh != tdt && kicked && var < 5 && k > 4 && !gtx) { var++; ER(0x400) &= ~2u; udelay(100); for (int i = 0; i < NTX; i++) { e->tx[i].st = 1; e->tx[i].cmd = 0; } e->ti = 0; ER(0x3810) = 0; ER(0x3818) = 0;
        if (var == 1) { ER(0x3828) = 0x0101011Fu | 1 << 22; ER(0x3928) = ER(0x3828); ER(0x3820) = 8u | 1u << 31; ER(0x382C) = 32; }
        else if (var == 2) { u32 c = ER(0); ER(0) = (c & ~0x300u) | 1 << 8 | 1 << 11 | 1 << 12 | 1; }
        else if (var == 3) { u32 c = ER(0); ER(0) = c & ~(1u << 11 | 1u << 12); ER(0x400) |= 1u << 28; ER(0x3940) &= ~(1u << 28); }
        else if (var == 4) { ER(0x3828) = 1u << 22 | 1u << 24; ER(0x3928) = ER(0x3828); ER(0x3840) |= 1; ER(0x3940) |= 1; }
        else { ER(0x34) = 7u << 16 | 1u << 21; udelay(20); u32 kk = ER(0x34) & 0xFFFF; ER(0x34) = 7u << 16 | (kk | 2u); udelay(20); }
        mb(); ER(0x400) |= 2; kprintf("red: variante de transmision %d probada (CTRL=%x TXDCTL=%x TCTL=%x)\n", var, ER(0), ER(0x3828), ER(0x400)); }
    if (gtx && !var) {} if (gtx) kprintf("red: el transmisor funciona (paquetes enviados %u, variante %d)\n", gtx, var);
    if (tdh != tdt && !kicked && k > 2) { kicked = 1; kprintf("red: el transmisor no avanza; se reinicia la cola\n"); ER(0x400) &= ~2u; udelay(100); ER(0x3828) = PCH_TXDCTL; ER(0x3928) = ER(0x3828); ER(0x3810) = 0; ER(0x3818) = 0; for (int i = 0; i < NTX; i++) { e->tx[i].st = 1; e->tx[i].cmd = 0; } e->ti = 0; mb(); ER(0x400) |= 2; } kprintf("red: RCTL=%x TCTL=%x STATUS=%x RDH=%u RDT=%u TDH=%u TDT=%u CRC=%u MPC=%u GPRC=%u GPTC=%u TPR=%u RXERR=%u\n", ER(0x100), ER(0x400), ER(8), ER(0x2810), ER(0x2818), ER(0x3810), ER(0x3818), ER(0x4000), ER(0x4010), ER(0x4074), ER(0x4080), ER(0x40D0), ER(0x400C)); }
static void pch_dbg2(netdev_t *n) { e1k_t *e = n->priv; kprintf("red: TXDCTL0=%x TXDCTL1=%x TARC0=%x TARC1=%x PBA=%x TDLEN=%u TDBAL=%x CTRL=%x CTRL_EXT=%x FEXTNVM=%x FEXTNVM4=%x KUMCTRL=%x\n", ER(0x3828), ER(0x3928), ER(0x3840), ER(0x3940), ER(0x1000), ER(0x3808), ER(0x3800), ER(0), ER(0x18), ER(0x28), ER(0x24), ER(0x34)); }
static void pch_init(pci_t *p) {
    pci_enable(p, 1, 1, 1); e1k_t *e = kzalloc(sizeof *e); e->r = ioremap(pci_bar(p, 0), 0x20000); netdev_t *n = mk("82579", e);
    u32 lo = ER(0x5400), hi = ER(0x5404); memcpy(n->mac, &lo, 4); memcpy(n->mac + 4, &hi, 2);
    kprintf("red: PCH %04x CTRL=%x STATUS=%x FWSM=%x RAH=%x\n", p->device, ER(0), ER(8), ER(0x5B54), hi);
    if (!(hi & (1u << 31)) || !(lo | (hi & 0xFFFF))) { u32 t = rdtsc(); n->mac[0] = 2; n->mac[1] = 0x5A; memcpy(n->mac + 2, &t, 4); kprintf("red: MAC no valida, se usa una local\n"); }
    ER(0xD8) = ~0u; ER(0x100) = 0; ER(0x400) = 8; msleep(10); (void)ER(0xC0);
    /* reinicio completo del MAC como hace Linux (e1000_reset_hw_ich8lan): la BIOS o el ME pueden dejar el transmisor parado */
    if (sw_lock(e)) { ER(0) = ER(0) | 1u << 26; msleep(20); sw_unlock(e); } else { ER(0) = ER(0) | 1u << 26; msleep(20); }
    for (int t = 0; t < 30 && !(ER(8) & 1u << 9); t++) msleep(5); ER(8) = ER(8) & ~(1u << 9); ER(0xD8) = ~0u; (void)ER(0xC0);
    ER(0x3004) |= 0x50000; kprintf("red: 82579 reiniciado CTRL=%x STATUS=%x\n", ER(0), ER(8));
    ER(0xF10) &= ~(u32)(2 | 4 | 8 | 64); ER(0xF00) &= ~(1u << 7);
    ER(0x18) &= ~(1u << 11); msleep(5);
    if (sw_lock(e)) {
        int ok = phy_ok(e);
        if (!ok) { sw_unlock(e); u32 c = ER(0); ER(0) = (c | 1 << 16) & ~(1u << 17); udelay(50); ER(0) = c & ~(1u << 16); msleep(60); ER(0x18) &= ~(1u << 11); sw_lock(e); ok = phy_ok(e); kprintf("red: PHY reactivado: %d\n", ok); }
        if (ok) {
            PHYW(31, 0); int id = PHYR(2), id2 = PHYR(3), bm = PHYR(0); kprintf("red: PHY %x:%x BMCR=%x BMSR=%x\n", id, id2, bm, PHYR(1));
            if (bm >= 0) PHYW(0, (bm & ~(1 << 11) & ~(1 << 10)) | 1 << 12 | 1 << 9);
            ER(0x34) = 7u << 16 | 1u << 21; udelay(20); u32 k = ER(0x34) & 0xFFFF; ER(0x34) = 7u << 16 | (k | 2u); udelay(20); /* K1 activo: con K1 apagado el 82579 no transmite */
            u32 c = ER(0), x = ER(0x18); ER(0) = (c & ~(u32)0x300) | 1 << 11; ER(0x18) = x | 1 << 15; udelay(20); ER(0) = c; ER(0x18) = x; udelay(20);
        }
        sw_unlock(e);
    }
    ER(0) = (ER(0) | 1 << 6) & ~(u32)(1 << 3 | 1 << 11 | 1 << 12 | 1u << 31 | 1 << 7);
    ER(0x5400) = *(u32 *)n->mac; ER(0x5404) = *(u16 *)(n->mac + 4) | 1u << 31; for (int i = 0; i < 32; i++) ER(0x5200 + i * 4) = 0;
    e->rx = dma_alloc(NRX * 16); e->tx = dma_alloc(NTX * 16); e->rb = dma_alloc(NRX * BUFSZ); e->tb = dma_alloc(NTX * BUFSZ);
    for (int i = 0; i < NRX; i++) e->rx[i].addr = V2P(e->rb + i * BUFSZ); for (int i = 0; i < NTX; i++) { e->tx[i].addr = V2P(e->tb + i * BUFSZ); e->tx[i].st = 1; }
    ER(0x18) |= 1 << 28 | 1 << 22; ER(0x3828) = PCH_TXDCTL; ER(0x3840) |= 1 | 1 << 23 | 1 << 24 | 1 << 26 | 1 << 27; ER(0x5008) |= 1 << 6 | 1 << 7 | 1 << 16;
    /* errata 82579 (como Linux e1000e): la cola 1 debe configurarse igual que la 0 y TARC1 con sus bits fijos; si no, el transmisor no arranca */
    ER(0x3928) = ER(0x3828); { u32 t1 = ER(0x3940) & ~(1u << 28); ER(0x3940) = t1 | 1 | 1 << 24 | 1 << 26 | 1 << 30; } /* TCTL.MULR va activo: TARC1 bit 28 apagado */ ER(0x1000) = 26; ER(0x3830) = 0; ER(0x382C) = 0;
    u64 a = V2P(e->rx); ER(0x2800) = a; ER(0x2804) = a >> 32; ER(0x2808) = NRX * 16; ER(0x2810) = 0; ER(0x2818) = NRX - 1;
    a = V2P(e->tx); ER(0x3800) = a; ER(0x3804) = a >> 32; ER(0x3808) = NTX * 16; ER(0x3810) = 0; ER(0x3818) = 0;
    ER(0x100) = 2 | 1 << 15 | 1 << 26; ER(0x400) = 2 | 8 | 15 << 4 | 0x3F << 12 | 1 << 24 | 1u << 28; ER(0x410) = 8 | 8 << 10 | 7 << 20;
    n->tx = e_tx; n->rx = e_rx; n->link = e_link; n->dbg = pch_dbg; net_register(n);
}
/* ===== RTL8139 ===== */
typedef struct { u16 io; u8 *rb, *tb; u32 off; int tx; } r39_t;
#define RB 8192
static int r_tx(netdev_t *n, const void *p, u32 l) {
    r39_t *r = n->priv; int i = r->tx; u32 st = inl(r->io + 0x10 + i * 4);
    if (!(st & (1 << 13))) return -1;
    memcpy(r->tb + i * BUFSZ, p, l); if (l < 60) { memset(r->tb + i * BUFSZ + l, 0, 60 - l); l = 60; }
    outl(r->io + 0x10 + i * 4, l); r->tx = (i + 1) & 3; return 0;
}
static int r_rx(netdev_t *n, void *o) {
    r39_t *r = n->priv; if (inb(r->io + 0x37) & 1) return 0;
    u32 h = *(u32 *)(r->rb + r->off); u32 st = h & 0xFFFF, len = h >> 16;
    if (!(st & 1) || len < 8 || len > 1800) { outb(r->io + 0x37, 4); outb(r->io + 0x37, 0xC); r->off = 0; outw(r->io + 0x38, 0xFFF0); return -1; }
    u32 pl = len - 4; memcpy(o, r->rb + r->off + 4, pl); r->off = (r->off + len + 4 + 3) & ~3u; r->off %= RB; outw(r->io + 0x38, r->off - 16); outw(r->io + 0x3E, 0xFFFF); return pl;
}
static int r_link(netdev_t *n) { r39_t *r = n->priv; return !(inb(r->io + 0x58) & 4); }
static void r39_init(pci_t *p) {
    pci_enable(p, 0, 1, 1); r39_t *r = kzalloc(sizeof *r); r->io = pci_bar(p, 0); u16 io = r->io;
    outb(io + 0x52, 0); outb(io + 0x37, 0x10); for (int i = 0; i < 100 && (inb(io + 0x37) & 0x10); i++) msleep(1);
    r->rb = dma_alloc(RB + 16 + 2048); r->tb = dma_alloc(4 * BUFSZ); outl(io + 0x30, V2P(r->rb)); outw(io + 0x3C, 0);
    for (int i = 0; i < 4; i++) outl(io + 0x20 + i * 4, V2P(r->tb + i * BUFSZ));
    outb(io + 0x37, 0xC); outl(io + 0x44, 0xF | 1 << 7); outl(io + 0x40, 3 << 24 | 6 << 8);
    netdev_t *n = mk("rtl8139", r); for (int i = 0; i < 6; i++) n->mac[i] = inb(io + i); n->tx = r_tx; n->rx = r_rx; n->link = r_link; net_register(n);
}
/* ===== RTL8168/8169 ===== */
typedef struct { volatile u32 o1, o2; volatile u64 addr; } r69d_t;
typedef struct { volatile u8 *r; r69d_t *rx, *tx; u8 *rb, *tb; u32 ri, ti; u8 *cnt; } r69_t;
#define OWN (1u << 31)
#define EOR (1u << 30)
static int s_tx(netdev_t *n, const void *p, u32 l) {
    r69_t *r = n->priv; r69d_t *d = &r->tx[r->ti]; if (d->o1 & OWN) return -1;
    memcpy(r->tb + r->ti * BUFSZ, p, l); if (l < 60) { memset(r->tb + r->ti * BUFSZ + l, 0, 60 - l); l = 60; }
    d->o2 = 0; mb(); d->o1 = OWN | 3u << 28 | (r->ti == NTX - 1 ? EOR : 0) | l; r->ti = (r->ti + 1) % NTX; mb(); R8(r->r + 0x38) = 0x40; return 0;
}
static int s_rx(netdev_t *n, void *o) {
    r69_t *r = n->priv; r69d_t *d = &r->rx[r->ri]; u32 v = d->o1; if (v & OWN) return 0;
    int l = (v & 0x3FFF) - 4; if (l > 0 && l <= 1600 && (v & (3u << 28)) == (3u << 28)) memcpy(o, r->rb + r->ri * BUFSZ, l); else l = -1;
    d->o2 = 0; mb(); d->o1 = OWN | (r->ri == NRX - 1 ? EOR : 0) | BUFSZ; r->ri = (r->ri + 1) % NRX; return l;
}
static int s_link(netdev_t *n) { r69_t *r = n->priv; return R8(r->r + 0x6C) & 2 ? 1 : 0; }
/* Diagnostico para DIAGNOST.TXT: estado del chip, descriptores que el chip aun no ha tomado y contadores internos (tally) */
static void s_dbg(netdev_t *n) {
    r69_t *r = n->priv; volatile u8 *m = r->r; int own = 0; for (int i = 0; i < NTX; i++) if (r->tx[i].o1 & OWN) own++;
    u16 is = R16(m + 0x3E); R16(m + 0x3E) = is; u64 c = V2P(r->cnt); R32(m + 0x14) = c >> 32; R32(m + 0x10) = (u32)c | 8; for (int i = 0; i < 1000 && (R32(m + 0x10) & 8); i++) udelay(10);
    volatile u8 *t = r->cnt; kprintf("red: RTL CMD=%x ISR=%x PHY=%x TCR=%x RCR=%x TX_PEND=%d TXP=%lu RXP=%lu TXERR=%lu RXERR=%u MISS=%u BCAST=%lu ABORT=%u UNDERRUN=%u\n",
        R8(m + 0x37), is, R8(m + 0x6C), R32(m + 0x40), R32(m + 0x44), own, R64(t), R64(t + 8), R64(t + 16), R32(t + 24), R16(t + 28), R64(t + 48), R16(t + 60), R16(t + 62));
}
static void r69_init(pci_t *p) {
    pci_enable(p, 1, 1, 1); r69_t *r = kzalloc(sizeof *r); int b = (p->bar[2] & 1) || !p->bar[2] ? 1 : 2; r->r = ioremap(pci_bar(p, b), 256); volatile u8 *m = r->r;
    kprintf("red: RTL816x version %x (TCR=%x)\n", R32(m + 0x40) & 0x7CF00000u, R32(m + 0x40));
    R8(m + 0x37) = 0x10; for (int i = 0; i < 100 && (R8(m + 0x37) & 0x10); i++) msleep(1);
    r->cnt = dma_alloc(4096); r->rx = dma_alloc(NRX * 16); r->tx = dma_alloc(NTX * 16); r->rb = dma_alloc(NRX * BUFSZ); r->tb = dma_alloc(NTX * BUFSZ);
    for (int i = 0; i < NRX; i++) { r->rx[i].addr = V2P(r->rb + i * BUFSZ); r->rx[i].o1 = OWN | (i == NRX - 1 ? EOR : 0) | BUFSZ; }
    for (int i = 0; i < NTX; i++) r->tx[i].addr = V2P(r->tb + i * BUFSZ);
    R8(m + 0x50) = 0xC0; R16(m + 0xE0) |= 8; R16(m + 0xDA) = BUFSZ; R8(m + 0xEC) = 0x3F;
    u64 ta = V2P(r->tx), ra = V2P(r->rx); R32(m + 0x24) = ta >> 32; R32(m + 0x20) = (u32)ta; R32(m + 0xE8) = ra >> 32; R32(m + 0xE4) = (u32)ra; /* parte alta primero, como Linux */
    R32(m + 0x08) = ~0u; R32(m + 0x0C) = ~0u; R16(m + 0x3E) = 0xFFFF; R8(m + 0x37) = 0xC; R32(m + 0x40) = 3 << 24 | 7 << 8; R32(m + 0x44) = 0xF | 7 << 13 | 7 << 8; R8(m + 0x50) = 0; R16(m + 0x3C) = 0;
    netdev_t *n = mk("rtl8168", r); for (int i = 0; i < 6; i++) n->mac[i] = R8(m + i); n->tx = s_tx; n->rx = s_rx; n->link = s_link; n->dbg = s_dbg; net_register(n);
}
/* ===== virtio-net (interfaz legacy) ===== */
typedef struct { u64 addr; u32 len; u16 fl, next; } PACKED vd_t;
typedef struct { u16 sz; vd_t *d; volatile u16 *av; volatile u8 *us; u16 last, idx; u8 *b; } vq_t;
typedef struct { u16 io; vq_t q[2]; } vn_t;
static void vq_setup(vn_t *v, int i) {
    u16 io = v->io; outw(io + 14, i); u16 sz = inw(io + 12); vq_t *q = &v->q[i]; q->sz = sz;
    u64 a = ALIGN(16UL * sz + 6 + 2UL * sz, 4096), tot = a + ALIGN(6 + 8UL * sz, 4096); u8 *m = dma_alloc(tot);
    q->d = (vd_t *)m; q->av = (u16 *)(m + 16UL * sz); q->us = m + a; q->b = dma_alloc(sz * BUFSZ); outl(io + 8, V2P(m) >> 12);
}
static int v_tx(netdev_t *n, const void *p, u32 l) {
    vn_t *v = n->priv; vq_t *q = &v->q[1]; u16 i = q->idx % q->sz; u8 *b = q->b + i * BUFSZ; memset(b, 0, 10); memcpy(b + 10, p, l);
    q->d[i] = (vd_t){V2P(b), l + 10, 0, 0}; q->av[2 + i] = i; mb(); q->idx++; q->av[1] = q->idx; mb(); outw(v->io + 16, 1); return 0;
}
static int v_rx(netdev_t *n, void *o) {
    vn_t *v = n->priv; vq_t *q = &v->q[0]; u16 used = R16(q->us + 2); if (used == q->last) return 0;
    u32 *e = (u32 *)(q->us + 4 + 8 * (q->last % q->sz)); u32 id = e[0], len = e[1]; q->last++;
    int l = (int)len - 10; if (l > 0 && l <= 1600) memcpy(o, q->b + id * BUFSZ + 10, l); else l = -1;
    q->av[2 + q->idx % q->sz] = id; mb(); q->idx++; q->av[1] = q->idx; mb(); outw(v->io + 16, 0); return l;
}
static int v_link(netdev_t *n) { return 1; }
static void vn_init(pci_t *p) {
    pci_enable(p, 0, 1, 1); vn_t *v = kzalloc(sizeof *v); v->io = pci_bar(p, 0); u16 io = v->io; outb(io + 18, 0); outb(io + 18, 1); outb(io + 18, 3);
    u32 fe = inl(io); outl(io + 4, fe & (1 << 5)); vq_setup(v, 0); vq_setup(v, 1);
    vq_t *q = &v->q[0]; for (u16 i = 0; i < q->sz; i++) { q->d[i] = (vd_t){V2P(q->b + i * BUFSZ), BUFSZ, 2, 0}; q->av[2 + i] = i; } q->idx = q->sz; mb(); q->av[1] = q->idx;
    q = &v->q[1]; q->av[0] = 1; v->q[0].av[0] = 1;
    outb(io + 18, 7); outw(io + 16, 0);
    netdev_t *n = mk("virtio", v); for (int i = 0; i < 6; i++) n->mac[i] = inb(io + 20 + i); n->tx = v_tx; n->rx = v_rx; n->link = v_link; net_register(n);
}
void netdrv_init(void) {
    for (int i = 0; i < pci_count; i++) {
        pci_t *p = &pci_devs[i];
        if (p->vendor == 0x8086 && p->cls == 2 && is_pch(p->device)) pch_init(p);
        else if (p->vendor == 0x8086 && p->cls == 2 && is_e1k(p->device)) e1k_init(p);
        else if (p->vendor == 0x10EC && p->device == 0x8139) r39_init(p);
        else if (p->vendor == 0x10EC && (p->device == 0x8168 || p->device == 0x8169 || p->device == 0x8136)) r69_init(p);
        else if (p->vendor == 0x1AF4 && p->device == 0x1000) vn_init(p);
    }
}
