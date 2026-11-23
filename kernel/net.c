/* Pila de red: Ethernet, ARP, IPv4, ICMP, UDP, TCP, DHCP y DNS */
#include "kernel.h"
#define HS16(x) __builtin_bswap16(x)
#define HL32(x) __builtin_bswap32(x)
static netdev_t *nic; static u32 my_ip, my_mask, my_gw, my_dns; static int dhcp_ok; static mutex_t nl;
static u8 BCAST[6] = {255, 255, 255, 255, 255, 255};
typedef struct { u32 ip; u8 mac[6]; u64 t; } arp_t; static arp_t arpt[32];
#define RXS 65536
#define TXS 65536
enum { C, LISTEN, SYN_S, SYN_R, EST, FIN1, FIN2, CLOSE_W, LAST_A, TIME_W };
typedef struct dg { struct dg *next; u32 len, src; u16 sp; u8 d[]; } dg_t;
typedef struct sock {
    struct sock *next, *parent; int type, st, orphan, accepted, peer_fin, fin_sent, want_fin, err; u16 lport, rport; u32 rip;
    u32 una, nxt, rnxt, wnd; u8 *rx, *tx; u32 rh, rl, th, tl; u64 rto_at; u32 rto; u64 die_at; int retries;
    dg_t *q, *qe; int qn; u32 from_ip; u16 from_port;
} sock_t;
static sock_t *socks; static u16 next_port = 49200;

static netdev_t *devs[4]; static int ndevs; static netdev_t *want;
void net_register(netdev_t *n) { kprintf("red: %s (%s) %02x:%02x:%02x:%02x:%02x:%02x\n", n->name, n->kind, n->mac[0], n->mac[1], n->mac[2], n->mac[3], n->mac[4], n->mac[5]); if (ndevs < 4) devs[ndevs++] = n; if (!nic) nic = n; else if (n->gone) want = n; }
static u16 csum(const void *d, u32 n, u32 s) { const u8 *p = d; while (n > 1) { s += p[0] << 8 | p[1]; p += 2; n -= 2; } if (n) s += p[0] << 8; while (s >> 16) s = (s & 0xFFFF) + (s >> 16); return ~s; }
static u32 sum_add(const void *d, u32 n, u32 s) { const u8 *p = d; while (n > 1) { s += p[0] << 8 | p[1]; p += 2; n -= 2; } if (n) s += p[0] << 8; return s; }
static void eth_tx(const u8 *dst, u16 type, const void *pl, u32 n) {
    u8 f[1600]; if (n > 1514 || !nic) return; memcpy(f, dst, 6); memcpy(f + 6, nic->mac, 6); *(u16 *)(f + 12) = HS16(type); memcpy(f + 14, pl, n);
    if (!nic->tx(nic, f, n + 14)) { nic->txp++; nic->txb += n + 14; }
}
static void arp_store(u32 ip, const u8 *m) { arp_t *f = &arpt[0]; for (int i = 0; i < 32; i++) { if (arpt[i].ip == ip) { f = &arpt[i]; break; } if (arpt[i].t < f->t) f = &arpt[i]; } f->ip = ip; memcpy(f->mac, m, 6); f->t = ticks + 1; }
static void arp_send(int op, const u8 *tm, const u8 *dm, u32 tip) { if (!nic) return; u8 a[28]; *(u16 *)a = HS16(1); *(u16 *)(a + 2) = HS16(0x800); a[4] = 6; a[5] = 4; *(u16 *)(a + 6) = HS16(op); memcpy(a + 8, nic->mac, 6); memcpy(a + 14, &my_ip, 4); memcpy(a + 18, tm, 6); memcpy(a + 24, &tip, 4); eth_tx(dm, 0x806, a, 28); }
static int ip_tx(u32 dst, u8 proto, const void *seg, u32 n) {
    u8 p[1520]; u8 mac[6]; if (n > 1480) return E_INVAL; static u16 id;
    if (dst == 0xFFFFFFFF) memcpy(mac, BCAST, 6);
    else {
        u32 hop = (dst & my_mask) == (my_ip & my_mask) || (dst & 0xFFFF) == (169 | 254 << 8) || (my_ip & 0xFFFF) == (169 | 254 << 8) || !my_gw ? dst : my_gw;   /* 169.254 y sin router: directo por el cable */ int ok = 0; if (!hop) return E_CONN;
        for (int i = 0; i < 32; i++) if (arpt[i].ip == hop && arpt[i].t) { memcpy(mac, arpt[i].mac, 6); ok = 1; break; }
        if (!ok) { static u8 z[6]; arp_send(1, z, BCAST, hop); return E_TIMEOUT; }
    }
    p[0] = 0x45; p[1] = 0; *(u16 *)(p + 2) = HS16(n + 20); *(u16 *)(p + 4) = HS16(++id); *(u16 *)(p + 6) = HS16(0x4000); p[8] = 64; p[9] = proto; *(u16 *)(p + 10) = 0; memcpy(p + 12, &my_ip, 4); memcpy(p + 16, &dst, 4);
    *(u16 *)(p + 10) = HS16(csum(p, 20, 0)); memcpy(p + 20, seg, n); eth_tx(mac, 0x800, p, n + 20); return 0;
}
static u16 l4sum(u32 src, u32 dst, u8 proto, const void *d, u32 n) { u32 s = sum_add(&src, 4, 0); s = sum_add(&dst, 4, s); s += proto + n; s = sum_add(d, n, s); while (s >> 16) s = (s & 0xFFFF) + (s >> 16); return ~s; }
static int udp_tx(u32 dst, u16 sp, u16 dp, const void *d, u32 n) {
    u8 u[1480]; if (n > 1472) return E_INVAL; *(u16 *)u = HS16(sp); *(u16 *)(u + 2) = HS16(dp); *(u16 *)(u + 4) = HS16(n + 8); *(u16 *)(u + 6) = 0; memcpy(u + 8, d, n);
    u16 c = l4sum(my_ip, dst, 17, u, n + 8); *(u16 *)(u + 6) = HS16(c ? c : 0xFFFF); return ip_tx(dst, 17, u, n + 8);
}
/* --- DHCP --- */
static u32 xid, offer_ip, offer_srv; static int dh_state, ll_on; static u64 dh_next;
static void dhcp_send(int type) {
    u8 b[320]; memset(b, 0, sizeof b); b[0] = 1; b[1] = 1; b[2] = 6; memcpy(b + 4, &xid, 4); *(u16 *)(b + 10) = HS16(0x8000); memcpy(b + 28, nic->mac, 6); *(u32 *)(b + 236) = HL32(0x63825363);
    u8 *o = b + 240; *o++ = 53; *o++ = 1; *o++ = type;
    if (type == 3) { *o++ = 50; *o++ = 4; memcpy(o, &offer_ip, 4); o += 4; *o++ = 54; *o++ = 4; memcpy(o, &offer_srv, 4); o += 4; }
    *o++ = 12; *o++ = 6; memcpy(o, "zunzun", 6); o += 6; *o++ = 55; *o++ = 3; *o++ = 1; *o++ = 3; *o++ = 6; *o++ = 255;
    u32 sv = my_ip; my_ip = 0; udp_tx(0xFFFFFFFF, 68, 67, b, o - b); my_ip = sv;
}
static void dhcp_rx(const u8 *b, u32 n) {
    if (n < 240 || b[0] != 2 || memcmp(b + 4, &xid, 4)) return;
    int type = 0; u32 mask = 0, gw = 0, dns = 0, srv = 0, yi; memcpy(&yi, b + 16, 4);
    for (u32 i = 240; i + 1 < n && b[i] != 255;) { u8 c = b[i], l = b[i + 1]; if (!c) { i++; continue; } const u8 *v = b + i + 2; if (c == 53) type = v[0]; else if (c == 1) memcpy(&mask, v, 4); else if (c == 3) memcpy(&gw, v, 4); else if (c == 6) memcpy(&dns, v, 4); else if (c == 54) memcpy(&srv, v, 4); i += l + 2; }
    if (type == 2 && dh_state == 1) { offer_ip = yi; offer_srv = srv; dh_state = 2; dhcp_send(3); dh_next = ticks + 2000; }
    else if (type == 5 && dh_state == 2) { ll_on = 0; my_ip = yi; my_mask = mask ? mask : 0xFFFFFF; my_gw = gw; my_dns = dns ? dns : gw; dh_state = 3; dhcp_ok = 1;
        kprintf("red: IP %d.%d.%d.%d puerta %d.%d.%d.%d dns %d.%d.%d.%d\n", yi & 255, yi >> 8 & 255, yi >> 16 & 255, yi >> 24, gw & 255, gw >> 8 & 255, gw >> 16 & 255, gw >> 24, my_dns & 255, my_dns >> 8 & 255, my_dns >> 16 & 255, my_dns >> 24); }
    else if (type == 6) { dh_state = 0; dh_next = ticks + 1000; }
}
/* --- TCP --- */
#define F_FIN 1
#define F_SYN 2
#define F_RST 4
#define F_PSH 8
#define F_ACK 16
#define MSS 1460
static int seg_tx(sock_t *s, u32 seq, u8 fl, const void *d, u32 n) {
    u8 t[1500]; int hl = 20 + (fl & F_SYN ? 4 : 0); *(u16 *)t = HS16(s->lport); *(u16 *)(t + 2) = HS16(s->rport); *(u32 *)(t + 4) = HL32(seq); *(u32 *)(t + 8) = HL32(s->rnxt);
    t[12] = hl << 2; t[13] = fl; u32 fr = s->rx ? RXS - s->rl : 0; *(u16 *)(t + 14) = HS16(MIN(fr, 65535U)); *(u16 *)(t + 16) = 0; *(u16 *)(t + 18) = 0;
    if (fl & F_SYN) { t[20] = 2; t[21] = 4; *(u16 *)(t + 22) = HS16(MSS); }
    if (n) memcpy(t + hl, d, n); *(u16 *)(t + 16) = HS16(l4sum(my_ip, s->rip, 6, t, hl + n)); return ip_tx(s->rip, 6, t, hl + n);
}
static void rst_reply(u32 ip, u16 sp, u16 dp, u32 seq, u32 ack, int has_ack) { sock_t t; memset(&t, 0, sizeof t); t.lport = dp; t.rport = sp; t.rip = ip; t.rnxt = ack; seg_tx(&t, has_ack ? seq : 0, F_RST | (has_ack ? 0 : F_ACK), 0, 0); }
static void tcp_out(sock_t *s) {
    if (s->st != EST && s->st != CLOSE_W && s->st != FIN1 && s->st != LAST_A) return;
    u8 d[MSS]; u32 win = MAX(s->wnd, 1U); win = MIN(win, 32U * MSS);
    for (;;) {
        u32 sent = s->nxt - s->una; if (s->fin_sent && sent) sent = MIN(sent, s->tl);
        if (sent >= s->tl) break;
        if (sent >= win) break;
        u32 n = MIN(MIN(s->tl - sent, MSS), win - sent); for (u32 i = 0; i < n; i++) d[i] = s->tx[(s->th + sent + i) % TXS];
        if (seg_tx(s, s->una + sent, F_ACK | F_PSH, d, n)) break;
        s->nxt = s->una + sent + n; if (!s->rto_at) s->rto_at = ticks + s->rto;
    }
    if (s->want_fin && !s->fin_sent && s->nxt - s->una == s->tl) { if (!seg_tx(s, s->nxt, F_ACK | F_FIN, 0, 0)) { s->nxt++; s->fin_sent = 1; s->st = s->st == CLOSE_W ? LAST_A : FIN1; s->rto_at = ticks + s->rto; } }
}
static void sock_free(sock_t *s) {
    sock_t **p = &socks; while (*p && *p != s) p = &(*p)->next; if (*p) *p = s->next;
    while (s->q) { dg_t *n = s->q->next; kfree(s->q); s->q = n; }
    kfree(s->rx); kfree(s->tx); kfree(s);
}
static sock_t *sock_alloc(int type) { sock_t *s = kzalloc(sizeof *s); if (!s) return 0; s->type = type; s->rto = 400; if (type == 1) { s->rx = kmalloc(RXS); s->tx = kmalloc(TXS); } s->next = socks; socks = s; return s; }
static void tcp_rx(u32 src, const u8 *t, u32 n) {
    if (n < 20) return; u16 sp = HS16(*(u16 *)t), dp = HS16(*(u16 *)(t + 2)); u32 seq = HL32(*(u32 *)(t + 4)), ack = HL32(*(u32 *)(t + 8)); u32 hl = (t[12] >> 4) * 4; u8 fl = t[13]; u32 wnd = HS16(*(u16 *)(t + 14));
    if (hl > n) return; const u8 *d = t + hl; u32 len = n - hl; sock_t *s, *ls = 0;
    for (s = socks; s; s = s->next) { if (s->type != 1 || s->lport != dp) continue; if (s->st == LISTEN) ls = s; else if (s->rport == sp && s->rip == src && s->st != C) break; }
    if (!s && ls && (fl & F_SYN) && !(fl & F_ACK)) {
        s = sock_alloc(1); if (!s) return; s->parent = ls; s->lport = dp; s->rport = sp; s->rip = src; s->rnxt = seq + 1; s->una = s->nxt = (u32)rdtsc(); s->wnd = wnd; s->st = SYN_R; seg_tx(s, s->nxt, F_SYN | F_ACK, 0, 0); s->nxt++; s->rto_at = ticks + s->rto; return;
    }
    if (!s) { if (!(fl & F_RST)) rst_reply(src, sp, dp, ack, seq + len + ((fl & F_SYN) ? 1 : 0), fl & F_ACK); return; }
    if (fl & F_RST) { s->err = E_CONN; s->st = C; s->peer_fin = 1; if (s->orphan) sock_free(s); return; }
    if (s->st == SYN_S) {
        if ((fl & (F_SYN | F_ACK)) == (F_SYN | F_ACK) && ack == s->nxt) { s->rnxt = seq + 1; s->una = ack; s->wnd = wnd; s->st = EST; s->rto_at = 0; s->retries = 0; seg_tx(s, s->nxt, F_ACK, 0, 0); }
        return;
    }
    if (fl & F_ACK) {
        i32 ad = ack - s->una, ah = ack - s->nxt;
        if (ad > 0 && ah <= 0) {
            u32 data = ad; if (s->st == SYN_R) { data = 0; s->st = EST; }
            else { if (data > s->tl) data = s->tl; s->th = (s->th + data) % TXS; s->tl -= data; }
            s->una = ack; s->retries = 0; s->rto = 400; s->rto_at = s->una != s->nxt ? ticks + s->rto : 0;
            if (s->fin_sent && s->una == s->nxt) { if (s->st == FIN1) s->st = FIN2; else if (s->st == LAST_A) { s->st = C; if (s->orphan) { sock_free(s); return; } } }
        }
        s->wnd = wnd;
    }
    int need_ack = 0;
    if (len && s->st >= EST) {
        need_ack = 1;
        if (seq == s->rnxt) { u32 fr = RXS - s->rl, c = MIN(len, fr); for (u32 i = 0; i < c; i++) s->rx[(s->rh + s->rl + i) % RXS] = d[i]; s->rl += c; s->rnxt += c; if (c < len) fl &= ~F_FIN; }
    }
    if ((fl & F_FIN) && seq + len == s->rnxt && !s->peer_fin) {
        s->rnxt++; s->peer_fin = 1; need_ack = 1;
        if (s->st == EST) s->st = CLOSE_W; else if (s->st == FIN2 || s->st == FIN1) { s->st = TIME_W; s->die_at = ticks + 2000; }
    }
    if (need_ack) seg_tx(s, s->nxt, F_ACK, 0, 0);
    tcp_out(s);
}
static void tcp_timers(void) {
    for (sock_t *s = socks, *n; s; s = n) {
        n = s->next;
        if (s->type != 1) continue;
        if (s->st == TIME_W && ticks >= s->die_at) { s->st = C; if (s->orphan) { sock_free(s); continue; } }
        if (s->orphan && s->die_at && ticks >= s->die_at) { sock_free(s); continue; }
        if (s->rto_at && ticks >= s->rto_at) {
            if (++s->retries > 10) { s->err = E_TIMEOUT; s->st = C; s->rto_at = 0; s->peer_fin = 1; if (s->orphan) sock_free(s); continue; }
            s->rto = MIN(s->rto * 2, 8000U); s->rto_at = ticks + s->rto;
            if (s->st == SYN_S) seg_tx(s, s->una, F_SYN, 0, 0);
            else if (s->st == SYN_R) seg_tx(s, s->una, F_SYN | F_ACK, 0, 0);
            else { int f = s->fin_sent && s->nxt - s->una == 1 && !s->tl; if (f) seg_tx(s, s->una, F_ACK | F_FIN, 0, 0); else { if (s->fin_sent) { s->fin_sent = 0; if (s->st == FIN1) s->st = EST; else if (s->st == LAST_A) s->st = CLOSE_W; } s->nxt = s->una; } }
        }
        tcp_out(s);
    }
}
/* --- recepcion --- */
static volatile int ping_seq, ping_ok;
static void udp_rx(u32 src, const u8 *u, u32 n) {
    if (n < 8) return; u16 sp = HS16(*(u16 *)u), dp = HS16(*(u16 *)(u + 2)); u32 l = HS16(*(u16 *)(u + 4)); if (l > n || l < 8) return; l -= 8;
    if (dp == 68 && sp == 67) { dhcp_rx(u + 8, l); return; }
    for (sock_t *s = socks; s; s = s->next) if (s->type == 2 && s->lport == dp && s->qn < 32) { dg_t *g = kmalloc(sizeof *g + l); if (!g) return; g->next = 0; g->len = l; g->src = src; g->sp = sp; memcpy(g->d, u + 8, l); if (s->qe) s->qe->next = g; else s->q = g; s->qe = g; s->qn++; s->from_ip = src; s->from_port = sp; return; }
}
static void rx_frame(u8 *f, int n) {
    if (n < 14) return; u16 t = HS16(*(u16 *)(f + 12)); u8 *p = f + 14; n -= 14;
    if (t == 0x806 && n >= 28) {
        u32 sip, tip; memcpy(&sip, p + 14, 4); memcpy(&tip, p + 24, 4); u16 op = HS16(*(u16 *)(p + 6)); if (sip) arp_store(sip, p + 8);
        if (op == 1 && tip == my_ip && my_ip) arp_send(2, p + 8, p + 8, sip);
    } else if (t == 0x800 && n >= 20) {
        u32 hl = (p[0] & 15) * 4, tl = HS16(*(u16 *)(p + 2)); if (tl > (u32)n || hl > tl) return; u32 src, dst; memcpy(&src, p + 12, 4); memcpy(&dst, p + 16, 4);
        if (dst != my_ip && dst != 0xFFFFFFFF && my_ip && dst != (my_ip | ~my_mask)) return; if (HS16(*(u16 *)(p + 6)) & 0x3FFF) return;
        u8 *l = p + hl; u32 ln = tl - hl;
        if (p[9] == 1 && ln >= 8) { if (l[0] == 8) { u8 r[1480]; if (ln > 1480) return; memcpy(r, l, ln); r[0] = 0; *(u16 *)(r + 2) = 0; *(u16 *)(r + 2) = HS16(csum(r, ln, 0)); ip_tx(src, 1, r, ln); } else if (l[0] == 0 && HS16(*(u16 *)(l + 6)) == ping_seq) ping_ok = 1; }
        else if (p[9] == 17) udp_rx(src, l, ln); else if (p[9] == 6) tcp_rx(src, l, ln);
    }
}
static void net_switch(netdev_t *n) { nic = n; my_ip = my_gw = my_mask = my_dns = 0; dhcp_ok = 0; ll_on = 0; dh_state = 0; dh_next = ticks; memset(arpt, 0, sizeof arpt); kprintf("red: ahora se usa %s (%s)\n", n->name, n->kind); }
static void net_thread(void *a) {
    static u8 f[2048]; u64 next_tcp = 0, next_log = 0; int last_link = -1; xid = (u32)rdtsc();
    for (;;) {
        if (!nic) { msleep(200); continue; }
        int busy = 0; mutex_lock(&nl);
        if (want && !*want->gone) { net_switch(want); want = 0; }
        if (nic->gone && *nic->gone) { netdev_t *b = 0; for (int i = 0; i < ndevs; i++) if (devs[i] != nic && !(devs[i]->gone && *devs[i]->gone)) b = devs[i]; for (int i = 0; i < ndevs; i++) if (devs[i] == nic) { devs[i] = devs[--ndevs]; break; } if (b) net_switch(b); else { nic = 0; mutex_unlock(&nl); continue; } }
        for (int i = 0; i < 32; i++) { int n = nic->rx(nic, f); if (n == 0) break; busy = 1; if (n > 0) { nic->rxp++; nic->rxb += n; rx_frame(f, n); } }
        /* sin servidor DHCP (dos equipos unidos por un cable): a los 12 s con enlace, direccion local 169.254.x.y sacada de la MAC.
           Se sigue pidiendo DHCP; si llega, manda la direccion del DHCP. */
        { static u64 link_at; static int was_down; int l = nic->link(nic); if (!l) { link_at = 0; was_down = 1; } else if (!link_at) { link_at = ticks;
            if (was_down && (dhcp_ok || ll_on)) { dhcp_ok = 0; ll_on = 0; dh_state = 0; dh_next = ticks; xid++; kprintf("red: cable conectado de nuevo, se pide direccion otra vez\n"); } was_down = 0; }
          if (l && link_at && !dhcp_ok && !ll_on && ticks - link_at > 12000) { u8 a = 1 + nic->mac[4] % 254, b = nic->mac[5] ? nic->mac[5] : 1; my_ip = 169 | 254 << 8 | (u32)a << 16 | (u32)b << 24; my_mask = 0xFFFF; my_gw = 0; my_dns = 0; ll_on = 1;
            kprintf("red: sin DHCP, direccion local 169.254.%d.%d\n", a, b); } }
        if (ticks >= dh_next && dh_state < 3) { if (dh_state == 0 || dh_state == 2) dh_state = 1; dhcp_send(1); dh_next = ticks + 2000; }
        if (ticks >= next_log) { next_log = ticks + 5000; int l = nic->link(nic); if (l != last_link || (!dhcp_ok && ticks < 90000)) kprintf("red: enlace=%d dhcp=%d estado=%d rx=%lu tx=%lu t=%lu\n", l, dhcp_ok, dh_state, nic->rxp, nic->txp, ticks); if (!dhcp_ok && nic->dbg) nic->dbg(nic); last_link = l; }
        if (ticks >= next_tcp) { next_tcp = ticks + 5; tcp_timers(); }
        mutex_unlock(&nl);
        if (!busy) msleep(1);
    }
}
void net_init(void) { kthread("red", net_thread, 0); }
int net_info(znet_t *o) {
    memset(o, 0, sizeof *o); if (!nic) return E_NOENT; memcpy(o->mac, nic->mac, 6); o->up = nic->link(nic); o->dhcp = dhcp_ok; o->local = ll_on; o->ip = my_ip; o->mask = my_mask; o->gw = my_gw; o->dns = my_dns; o->rxp = nic->rxp; o->txp = nic->txp; o->rxb = nic->rxb; o->txb = nic->txb; strncpy0(o->name, nic->name, 8); strncpy0(o->kind, nic->kind, 16); return 0;
}
int net_ping(u32 ip, int to, int seq) {
    if (!my_ip) return E_CONN; u8 b[64]; memset(b, 0x5A, 64); b[0] = 8; b[1] = 0; *(u16 *)(b + 2) = 0; *(u16 *)(b + 4) = HS16(0x5A5A); *(u16 *)(b + 6) = HS16(seq); *(u16 *)(b + 2) = HS16(csum(b, 64, 0));
    ping_seq = seq; ping_ok = 0; u64 st = ticks, end = ticks + to; int sent = 0; u64 retry = 0;
    while (ticks < end) { if (!sent && ticks >= retry) { mutex_lock(&nl); sent = !ip_tx(ip, 1, b, 64); mutex_unlock(&nl); retry = ticks + 100; } if (ping_ok) return ticks - st; msleep(1); }
    return E_TIMEOUT;
}
static const fsops_t nofs;
file_t *sock_file(int type) {
    if (type != 1 && type != 2) return 0; mutex_lock(&nl); sock_t *s = sock_alloc(type); mutex_unlock(&nl); if (!s) return 0;
    file_t *f = kzalloc(sizeof *f); f->is_sock = 1; f->priv = s; f->refs = 1; f->flags = O_READ | O_WRITE; (void)nofs; return f;
}
static u16 alloc_port(void) { if (++next_port > 65000) next_port = 49200; return next_port; }
int sock_connect(file_t *f, u32 ip, int port, int to) {
    sock_t *s = f->priv; if (!my_ip) return E_CONN; if (s->st != C) return E_INVAL;
    mutex_lock(&nl); s->rip = ip; s->rport = port; if (!s->lport) s->lport = alloc_port();
    if (s->type == 2) { mutex_unlock(&nl); return 0; }
    s->una = s->nxt = (u32)rdtsc(); s->st = SYN_S; s->rto = 300; s->err = 0; s->wnd = 1460; seg_tx(s, s->nxt, F_SYN, 0, 0); s->nxt++; s->rto_at = ticks + 200; mutex_unlock(&nl);
    u64 end = ticks + (to > 0 ? to : 15000);
    while (s->st == SYN_S && ticks < end && !(cur->proc && cur->proc->killed)) msleep(2);
    if (s->st == EST) return 0;
    mutex_lock(&nl); int e = s->err ? s->err : E_TIMEOUT; s->st = C; s->rto_at = 0; mutex_unlock(&nl); return e;
}
i64 sock_send(file_t *f, const void *b, u64 n) {
    sock_t *s = f->priv; const u8 *p = b; u64 done = 0;
    if (s->type == 2) return sock_sendto(f, s->rip, s->rport, b, n);
    while (done < n) {
        if (s->st != EST && s->st != CLOSE_W) return done ? (i64)done : (s->err ? s->err : E_CONN);
        if (cur->proc && cur->proc->killed) return E_CONN;
        mutex_lock(&nl); u32 fr = TXS - s->tl, c = MIN(fr, n - done); for (u32 i = 0; i < c; i++) s->tx[(s->th + s->tl + i) % TXS] = p[done + i]; s->tl += c; done += c; tcp_out(s); mutex_unlock(&nl);
        if (!c) msleep(1);
    }
    return done;
}
i64 sock_recv(file_t *f, void *b, u64 n, int to) {
    sock_t *s = f->priv; u64 end = to > 0 ? ticks + to : 0; u8 *p = b;
    for (;;) {
        mutex_lock(&nl);
        if (s->type == 2 && s->q) { dg_t *g = s->q; s->from_ip = g->src; s->from_port = g->sp; s->q = g->next; if (!s->q) s->qe = 0; s->qn--; u32 c = MIN(n, g->len); memcpy(b, g->d, c); kfree(g); mutex_unlock(&nl); return c; }
        if (s->type == 1 && s->rl) { int was_zero = s->rl == RXS; u32 c = MIN(n, s->rl); for (u32 i = 0; i < c; i++) p[i] = s->rx[(s->rh + i) % RXS]; s->rh = (s->rh + c) % RXS; s->rl -= c; if (was_zero || c > 8192) seg_tx(s, s->nxt, F_ACK, 0, 0); mutex_unlock(&nl); return c; }
        int eof = s->type == 1 && (s->peer_fin || s->st == C), err = s->err; mutex_unlock(&nl);
        if (eof) return err && err != E_TIMEOUT ? err : 0;
        if (to == 0) return E_TIMEOUT;
        if (to > 0 && ticks >= end) return E_TIMEOUT;
        if (cur->proc && cur->proc->killed) return E_CONN;
        msleep(1);
    }
}
i64 sock_sendto(file_t *f, u32 ip, int port, const void *b, u64 n) {
    sock_t *s = f->priv; if (s->type != 2) return E_INVAL; if (n > 1472) return E_INVAL; if (!s->lport) s->lport = alloc_port();
    for (int t = 0; t < 15; t++) { mutex_lock(&nl); int r = udp_tx(ip, s->lport, port, b, n); mutex_unlock(&nl); if (!r) return n; if (r != E_TIMEOUT) return r; msleep(100); }
    return E_TIMEOUT;
}
int sock_from(file_t *f, u32 *ip, u16 *port) { sock_t *s = f->priv; *ip = s->from_ip; *port = s->from_port; return 0; }
int sock_bind(file_t *f, int port) { sock_t *s = f->priv; s->lport = port; if (s->type == 1) s->st = LISTEN; return 0; }
int sock_accept(file_t *f, int to, file_t **o) {
    sock_t *l = f->priv; u64 end = to > 0 ? ticks + to : 0; if (l->st != LISTEN) return E_INVAL;
    for (;;) {
        mutex_lock(&nl);
        for (sock_t *s = socks; s; s = s->next) if (s->parent == l && !s->accepted && s->st >= EST) { s->accepted = 1; file_t *n = kzalloc(sizeof *n); n->is_sock = 1; n->priv = s; n->refs = 1; n->flags = 3; *o = n; mutex_unlock(&nl); return 0; }
        mutex_unlock(&nl); if (to > 0 && ticks > end) return E_TIMEOUT; if (cur->proc && cur->proc->killed) return E_CONN; msleep(2);
    }
}
void sock_close(file_t *f) {
    sock_t *s = f->priv; mutex_lock(&nl);
    if (s->type == 1 && (s->st == EST || s->st == CLOSE_W)) { s->want_fin = 1; s->orphan = 1; s->die_at = ticks + 20000; tcp_out(s); }
    else if (s->type == 1 && s->st >= SYN_R && s->st != TIME_W) { s->orphan = 1; s->die_at = ticks + 20000; }
    else if (s->st == TIME_W) s->orphan = 1;
    else { if (s->st == LISTEN) for (sock_t *c = socks; c; c = c->next) if (c->parent == s) { c->parent = 0; c->orphan = 1; c->die_at = ticks + 1000; } sock_free(s); }
    mutex_unlock(&nl); kfree(f);
}
int net_dns(const char *h, u32 *ip) {
    if (!dhcp_ok) return E_CONN; int dots = 0, num = 1; u32 v = 0, cur_o = 0; for (const char *p = h; *p; p++) { if (*p == '.') { v |= cur_o << (dots * 8); cur_o = 0; dots++; } else if (*p >= '0' && *p <= '9') cur_o = cur_o * 10 + *p - '0'; else num = 0; }
    if (num && dots == 3) { *ip = v | cur_o << 24; return 0; }
    file_t *f = sock_file(2); if (!f) return E_NOMEM; u8 q[300], r[600]; u16 id = rdtsc(); memset(q, 0, 12); *(u16 *)q = id; q[2] = 1; q[5] = 1; int n = 12;
    for (const char *p = h; *p;) { const char *e = strchr(p, '.'); int l = e ? e - p : (int)strlen(p); if (l > 63 || n + l + 6 > 290) { sock_close(f); return E_INVAL; } q[n++] = l; memcpy(q + n, p, l); n += l; p += l; if (*p) p++; }
    q[n++] = 0; q[n++] = 0; q[n++] = 1; q[n++] = 0; q[n++] = 1; int res = E_TIMEOUT;
    for (int t = 0; t < 3 && res == E_TIMEOUT; t++) {
        if (sock_sendto(f, my_dns, 53, q, n) < 0) continue; i64 g = sock_recv(f, r, sizeof r, 1500); if (g < 12 || *(u16 *)r != id) continue;
        int an = r[6] << 8 | r[7], qd = r[4] << 8 | r[5], o = 12; res = E_NOENT;
        for (int i = 0; i < qd; i++) { while (o < g && r[o]) { if (r[o] >= 192) { o++; break; } o += r[o] + 1; } o += 5; }
        for (int i = 0; i < an && o + 12 <= g; i++) { while (o < g && r[o]) { if (r[o] >= 192) { o++; break; } o += r[o] + 1; } o++; int ty = r[o] << 8 | r[o + 1], rl = r[o + 8] << 8 | r[o + 9]; o += 10; if (ty == 1 && rl == 4) { memcpy(ip, r + o, 4); res = 0; break; } o += rl; }
    }
    sock_close(f); return res;
}
