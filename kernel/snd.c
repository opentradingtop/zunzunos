/* Sonido: AC'97 e Intel HD Audio. PCM 48 kHz, 16 bits, estereo */
#include "kernel.h"
#define RINGSZ (1 << 19)
#define PERIOD 4096
#define NP 32
static u8 *ring; static volatile u32 rh, rl; static u8 *dma; static int vol = 85; static char nm[48]; static mutex_t sl;
static int ac_rcur(void); static void ac_rkick(void);
#define ac_rcur_ptr ac_rcur
#define ac_rkick_ptr ac_rkick
static u8 *rdma, *rring; static volatile u32 rrh, rrl; static int rec_on, rlast, rec_ok, rgain = 80; static int (*rcur)(void); static void (*rrun)(int);
#define RRING (1 << 19)
static int (*cur_period)(void); static void (*kick)(int); static int last_fill;
static void fill(int p) {
    u8 *d = dma + p * PERIOD; u64 f = irq_save(); u32 n = MIN(rl, (u32)PERIOD); irq_restore(f);
    for (u32 i = 0; i < n; i++) d[i] = ring[(rh + i) % RINGSZ];
    if (n < PERIOD) memset(d + n, 0, PERIOD - n);
    i16 *s = (i16 *)d; for (u32 i = 0; i < n / 2; i++) s[i] = s[i] * vol / 100;
    dma_sync(d, PERIOD); f = irq_save(); rh = (rh + n) % RINGSZ; rl -= n; irq_restore(f);
}
static void snd_thread(void *a) {
    for (;;) {
        int c = cur_period(); int target = (c + NP - 2) % NP;
        while (last_fill != target) { last_fill = (last_fill + 1) % NP; fill(last_fill); }
        if (kick) kick(c);
        if (rec_on && rcur == ac_rcur_ptr) ac_rkick_ptr();
        if (rec_on) { int rc = rcur(); while (rlast != rc) { u8 *d = rdma + rlast * PERIOD; dma_sync(d, PERIOD); u64 f = irq_save(); for (u32 i = 0; i < PERIOD; i++) { if (rrl < RRING) { rring[(rrh + rrl) % RRING] = d[i]; rrl++; } } irq_restore(f); rlast = (rlast + 1) % NP; } }
        msleep(5);
    }
}
i64 snd_write(const void *b, u64 n) {
    if (!ring) return E_NOSYS; const u8 *p = b; u64 d = 0; mutex_lock(&sl);
    while (d < n) {
        u32 fr = RINGSZ - rl, c = MIN(fr, n - d);
        if (!c) { msleep(5); if (cur->proc && cur->proc->killed) break; continue; }
        u32 t = (rh + rl) % RINGSZ; for (u32 i = 0; i < c; i++) ring[(t + i) % RINGSZ] = p[d + i];
        u64 f = irq_save(); rl += c; irq_restore(f); d += c;
    }
    mutex_unlock(&sl); return d;
}
int snd_info(char *o, int n) { strncpy0(o, nm[0] ? nm : "sin tarjeta de sonido", n); return ring != 0; }
int snd_volume(int v) { if (v >= 0) vol = MIN(v, 100); return vol; }
int snd_space(void) { return ring ? RINGSZ - rl : 0; }
int rec_start(void) { if (!rec_ok) return E_NOSYS; if (rec_on) return 0; if (!rring) rring = kmalloc(RRING); rrh = rrl = 0; rrun(1); rlast = rcur(); rec_on = 1; return 0; }
void rec_stop(void) { if (rec_on) { rec_on = 0; rrun(0); } }
i64 rec_read(void *b, u64 n) { if (!rec_on && !rrl) return 0; u8 *p = b; u64 f = irq_save(); u32 c = MIN(n, (u64)rrl); irq_restore(f); for (u32 i = 0; i < c; i++) p[i] = rring[(rrh + i) % RRING]; f = irq_save(); rrh = (rrh + c) % RRING; rrl -= c; irq_restore(f); return c; }
void rec_gain(int g) { rgain = MAX(0, MIN(g, 100)); }
static void common_start(void) { ring = kmalloc(RINGSZ); last_fill = NP - 1; for (int i = 0; i < NP; i++) memset(dma + i * PERIOD, 0, PERIOD); kprintf("sonido: %s\n", nm); kthread("sonido", snd_thread, 0); }

/* ===== AC97 ===== */
static u16 nam, nabm;
static int ac_cur(void) { return inb(nabm + 0x14) & 31; }
static void ac_kick(int c) { outb(nabm + 0x15, (c + NP - 1) % NP); }
static int ac_rcur(void) { return inb(nabm + 4) & 31; }
static void ac_rrun(int on) { if (on) { outb(nabm + 0xB, 2); for (int i = 0; i < 50 && (inb(nabm + 0xB) & 2); i++) msleep(1); outl(nabm, V2P(rdma + NP * PERIOD)); outb(nabm + 5, NP - 1); outb(nabm + 0xB, 1); } else outb(nabm + 0xB, 0); }
static void ac_rkick(void) { outb(nabm + 5, (ac_rcur() + NP - 1) % NP); }
static void ac97_init(pci_t *p) {
    pci_enable(p, 0, 1, 1); nam = pci_bar(p, 0); nabm = pci_bar(p, 1);
    outl(nabm + 0x2C, 2); msleep(20); outw(nam, 1); msleep(20);
    outw(nam + 2, 0); outw(nam + 0x18, 0); outw(nam + 4, 0);
    u16 ext = inw(nam + 0x28); if (ext & 1) { outw(nam + 0x2A, inw(nam + 0x2A) | 1); outw(nam + 0x2C, 48000); }
    outb(nabm + 0x1B, 2); for (int i = 0; i < 50 && (inb(nabm + 0x1B) & 2); i++) msleep(1);
    dma = dma_alloc(NP * PERIOD); u32 *bd = dma_alloc(4096);
    for (int i = 0; i < NP; i++) { bd[i * 2] = V2P(dma + i * PERIOD); bd[i * 2 + 1] = PERIOD / 2; }
    outl(nabm + 0x10, V2P(bd)); outb(nabm + 0x15, NP - 1); rdma = dma_alloc(NP * PERIOD + 4096); u32 *rb = (u32 *)(rdma + NP * PERIOD); for (int i = 0; i < NP; i++) { rb[i * 2] = V2P(rdma + i * PERIOD); rb[i * 2 + 1] = PERIOD / 2; } dma_sync(rb, NP * 8); outw(nam + 0x1A, 0); outw(nam + 0x1C, 0x0808); outw(nam + 0x0E, 0x0040); if (ext & 1) outw(nam + 0x32, 48000); rcur = ac_rcur; rrun = ac_rrun; rec_ok = 1;
    strncpy0(nm, "AC'97", 48); cur_period = ac_cur; kick = ac_kick; common_start(); outb(nabm + 0x1B, 1);
}
/* ===== Intel HDA ===== */
static volatile u8 *H; static u32 *corb; static u64 *rirb; static int corbn, ri, rbn; static volatile u8 *sd;
#define HR32(o) R32(H + (o))
#define HR16(o) R16(H + (o))
#define HR8(o) R8(H + (o))
static u32 verb(int c, int n, u32 v) {
    u32 w = (u32)c << 28 | (u32)n << 20 | v; corbn = (corbn + 1) % 256; corb[corbn] = w; dma_sync(&corb[corbn], 4); HR16(0x48) = corbn;
    for (int t = 0; t < 4000; t++) { int wp = HR16(0x58) & 255;
        while (wp != ri) { ri = (ri + 1) % 256; dma_sync(&rirb[ri], 8); u64 e = rirb[ri]; HR8(0x5D) = 5; if (!(e >> 36 & 1)) return (u32)e; }
        udelay(20); }
    kprintf("hda: sin respuesta al verbo %x nodo %d\n", v, n); return 0;
}
#define PARAM(c, n, p) verb(c, n, 0xF0000 | (p))
static int conn_list(int c, int n, int *o, int max) {
    u32 l = PARAM(c, n, 0xE); int cnt = l & 0x7F, k = 0; if (l & 0x80) return 0;
    for (int i = 0; i < cnt && k < max; i += 4) { u32 v = verb(c, n, 0xF0200 | i); for (int j = 0; j < 4 && i + j < cnt && k < max; j++) { int e = (v >> (j * 8)) & 255; if ((e & 0x80) && k) { for (int q = o[k - 1] + 1; q <= (e & 0x7F) && k < max; q++) o[k++] = q; } else o[k++] = e & 0x7F; } }
    return k;
}
static int path[8], pl;
static int find_dac(int c, int n, int d) {
    u32 w = PARAM(c, n, 9), t = (w >> 20) & 15; path[d] = n;
    if (t == 0) { if (w & (1 << 9)) return 0; pl = d + 1; return 1; }
    if (d >= 6 || (t != 2 && t != 3 && t != 4)) return 0;
    int l[16], k = conn_list(c, n, l, 16); for (int i = 0; i < k; i++) if (find_dac(c, l[i], d + 1)) { if (t != 2) verb(c, n, 0x70100 | i); return 1; }
    return 0;
}
static void unmute(int c, int n) {
    u32 w = PARAM(c, n, 9); verb(c, n, 0x70500);
    if (w & 4) { u32 a = (w & 8) ? PARAM(c, n, 0x12) : 0; int g = (a >> 8) & 0x7F; if (!(w & 8) || !g) g = 0x7F; verb(c, n, 0x3B000 | g); }
    if (w & 2) { u32 a = (w & 8) ? PARAM(c, n, 0xD) : 0; int g = a & 0x7F; for (int i = 0; i < 8; i++) verb(c, n, 0x37000 | i << 8 | g); }
}
static volatile u8 *isd;
static int find_src(int c, int n, int d, int want_fixed) { u32 w = PARAM(c, n, 9), t = (w >> 20) & 15; path[d] = n; if (t == 4) { u32 cfg = verb(c, n, 0xF1C00), pc = PARAM(c, n, 0xC); int dev = cfg >> 20 & 15, con = cfg >> 30; if (!(pc & 0x20) || (dev != 0xA && dev != 8) || con == 1) return 0; if (want_fixed && con != 2) return 0; pl = d + 1; return 1; } if (d >= 6 || (t != 1 && t != 2 && t != 3)) return 0; int l[16], k = conn_list(c, n, l, 16); for (int i = 0; i < k; i++) if (find_src(c, l[i], d + 1, want_fixed)) { if (t != 2) verb(c, n, 0x70100 | i); return 1; } return 0; }
static int hda_rcur(void) { return (R32(isd + 4) / PERIOD) % NP; }
static void hda_rrun(int on) { if (on) { R8(isd) = 1; for (int i = 0; i < 100 && !(R8(isd) & 1); i++) udelay(50); R8(isd) = 0; for (int i = 0; i < 100 && (R8(isd) & 1); i++) udelay(50); u64 a = V2P(rdma + NP * PERIOD); R32(isd + 0x18) = a; R32(isd + 0x1C) = a >> 32; R32(isd + 8) = NP * PERIOD; R16(isd + 0xC) = NP - 1; R16(isd + 0x12) = 0x0011; R8(isd + 2) = 2 << 4; R8(isd) = 2; } else R8(isd) = 0; }
static void hda_input(int c, int ws, int nw, int iss) { if (!iss) return; for (int pass = 0; pass < 2; pass++) for (int n = ws; n < ws + nw; n++) { u32 w = PARAM(c, n, 9); if (((w >> 20) & 15) != 1 || (w & (1 << 9))) continue; pl = 0; if (!find_src(c, n, 0, !pass)) continue; int pin = path[pl - 1]; u32 pc = PARAM(c, pin, 0xC); verb(c, pin, 0x70700 | 0x20 | (pc & (1 << 10) ? 4 : 0)); for (int i = 0; i < pl; i++) { int x = path[i]; u32 wc = PARAM(c, x, 9); verb(c, x, 0x70500); if (wc & 2) { u32 a = (wc & 8) ? PARAM(c, x, 0xD) : 0; int mx = (a >> 8) & 0x7F, g = mx ? mx * rgain / 100 : 0x7F; for (int k = 0; k < 8; k++) verb(c, x, 0x37000 | k << 8 | g); } if (wc & 4) { u32 a = (wc & 8) ? PARAM(c, x, 0x12) : 0; verb(c, x, 0x3B000 | (a & 0x7F)); } } verb(c, n, 0x70620); verb(c, n, 0x20000 | 0x0011); rdma = dma_alloc(NP * PERIOD + 4096); u32 *bd = (u32 *)(rdma + NP * PERIOD); for (int i = 0; i < NP; i++) { u64 a = V2P(rdma + i * PERIOD); bd[i * 4] = a; bd[i * 4 + 1] = a >> 32; bd[i * 4 + 2] = PERIOD; bd[i * 4 + 3] = 0; } dma_sync(bd, NP * 16); isd = H + 0x80; rcur = hda_rcur; rrun = hda_rrun; rec_ok = 1; kprintf("hda: entrada adc %d <- pin %d\n", n, pin); return; } kprintf("hda: sin entrada de audio\n"); }
static int hda_cur(void) { return (R32(sd + 4) / PERIOD) % NP; }
static void hda_init(pci_t *p) {
    pci_enable(p, 1, 0, 1);
    if (p->vendor == 0x8086) { pci_write(p, 0x44, pci_read(p, 0x44) & ~7u); pci_write16(p, 0x78, pci_read16(p, 0x78) & ~(1u << 11)); }
    H = ioremap(pci_bar(p, 0), 0x4000);
    HR8(0x4C) = 0; HR8(0x5C) = 0; HR32(8) = 0; for (int i = 0; i < 100 && (HR32(8) & 1); i++) msleep(1); msleep(1); HR32(8) = 1; for (int i = 0; i < 200 && !(HR32(8) & 1); i++) msleep(1); msleep(10);
    if (!(HR32(8) & 1)) { kprintf("hda: no sale de reset\n"); return; }
    u16 st = HR16(0xE); for (int i = 0; i < 20 && !st; i++) { msleep(10); st = HR16(0xE); }
    u16 gcap = HR16(0); int iss = gcap >> 8 & 15; kprintf("hda: %04x:%04x codecs=%x entradas=%d\n", p->vendor, p->device, st, iss);
    corb = dma_alloc(4096); rirb = dma_alloc(4096);
    HR32(0x40) = V2P(corb); HR32(0x44) = 0; HR8(0x4E) = 2; HR16(0x4A) = 0x8000; for (int i = 0; i < 100 && !(HR16(0x4A) & 0x8000); i++) udelay(50); HR16(0x4A) = 0; for (int i = 0; i < 100 && (HR16(0x4A) & 0x8000); i++) udelay(50); HR16(0x48) = 0; corbn = 0;
    HR32(0x50) = V2P(rirb); HR32(0x54) = 0; HR8(0x5E) = 2; HR16(0x58) = 0x8000; HR16(0x5A) = 0xFF; ri = 0; HR8(0x5D) = 7; HR8(0x4C) = 2; HR8(0x5C) = 2; msleep(2);
    for (int c = 0; c < 15; c++) {
        if (!(st & (1 << c))) continue;
        u32 id = PARAM(c, 0, 0), r = PARAM(c, 0, 4); int fs = r >> 16 & 255, nf = r & 255; kprintf("hda: codec %d id %x\n", c, id);
        for (int g = fs; g < fs + nf; g++) {
            if ((PARAM(c, g, 5) & 255) != 1) continue;
            verb(c, g, 0x70500); msleep(20); u32 w = PARAM(c, g, 4); int ws = w >> 16 & 255, nw = w & 255, here = 0;
            for (int n = ws; n < ws + nw; n++) {
                u32 wc = PARAM(c, n, 9); if (((wc >> 20) & 15) != 4 || (wc & (1 << 9))) continue;
                u32 pc = PARAM(c, n, 0xC), cfg = verb(c, n, 0xF1C00); int dev = cfg >> 20 & 15;
                if (pc & (1 << 16)) verb(c, n, 0x70C02);
                if (!(pc & 0x10) || dev > 2) continue;
                int l[16], k = conn_list(c, n, l, 16), ok = 0; pl = 0;
                for (int i = 0; i < k && !ok; i++) if (find_dac(c, l[i], 0)) { verb(c, n, 0x70100 | i); ok = 1; }
                if (!ok) continue;
                verb(c, n, 0x707C0); unmute(c, n); for (int i = 0; i < pl; i++) unmute(c, path[i]);
                int dac = path[pl - 1]; verb(c, dac, 0x70610); verb(c, dac, 0x20000 | 0x0011); here++; kprintf("hda: pin %d (tipo %d, cfg %x) -> dac %d\n", n, dev, cfg, dac);
            }
            if (here) { rbn = 1; hda_input(c, ws, nw, iss); }
        }
        if (rbn) break;
    }
    if (!rbn) { kprintf("hda: sin salida utilizable\n"); return; }
    sd = H + 0x80 + iss * 0x20; R8(sd) = 0; msleep(1); R8(sd) = 1; for (int i = 0; i < 100 && !(R8(sd) & 1); i++) udelay(50); R8(sd) = 0; for (int i = 0; i < 100 && (R8(sd) & 1); i++) udelay(50);
    dma = dma_alloc(NP * PERIOD); u32 *bd = dma_alloc(4096);
    for (int i = 0; i < NP; i++) { u64 a = V2P(dma + i * PERIOD); bd[i * 4] = a; bd[i * 4 + 1] = a >> 32; bd[i * 4 + 2] = PERIOD; bd[i * 4 + 3] = 0; }
    dma_sync(bd, NP * 16); dma_sync(dma, NP * PERIOD);
    u64 a = V2P(bd); R32(sd + 0x18) = a; R32(sd + 0x1C) = a >> 32; R32(sd + 8) = NP * PERIOD; R16(sd + 0xC) = NP - 1; R16(sd + 0x12) = 0x0011; R8(sd + 2) = 1 << 4;
    strncpy0(nm, "Intel HD Audio", 48); cur_period = hda_cur; kick = 0; common_start(); R8(sd) = 2;
}
void snd_init(void) {
    for (int i = 0; i < pci_count; i++) { pci_t *p = &pci_devs[i]; if (p->cls != 4) continue;
        if (p->sub == 3) { hda_init(p); if (ring) return; } else if (p->sub == 1) { ac97_init(p); return; } }
}
