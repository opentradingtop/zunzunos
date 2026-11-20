/* Lector de tarjetas SD (SDHCI por PIO), incluido el Ricoh e823 del ThinkPad X220 */
#include "kernel.h"
typedef struct { volatile u8 *r; u32 base_mhz, rca; int sdhc, present; blkdev_t *blk; int id; } sd_t;
#define R8_(o) R8(s->r + (o))
#define R16_(o) R16(s->r + (o))
#define R32_(o) R32(s->r + (o))
#define RSP_NONE 0
#define RSP_136 0x09
#define RSP_48 0x1A
#define RSP_48B 0x1B
#define RSP_R3 0x02
static void rst(sd_t *s, u8 m) { R8_(0x2F) = m; for (int i = 0; i < 1000 && (R8_(0x2F) & m); i++) udelay(100); }
static void set_clock(sd_t *s, u32 khz) {
    R16_(0x2C) = 0; u32 mx = (R16_(0xFE) & 255) >= 2 ? 1023 : 128, d = 1; while (d < mx && s->base_mhz * 1000 / (d * 2) > khz) d *= 2; if (d > mx) d = mx; u32 sel = s->base_mhz * 1000 <= khz ? 0 : d;
    R16_(0x2C) = (sel & 0xFF) << 8 | ((sel >> 8) & 3) << 6 | 1; for (int i = 0; i < 2000 && !(R16_(0x2C) & 2); i++) udelay(50); R16_(0x2C) |= 4; udelay(200);
}
static int cmd(sd_t *s, int idx, u32 arg, int rsp, u16 mode, int data) {
    u32 mask = data || rsp == RSP_48B ? 3 : 1; for (int i = 0; i < 20000 && (R32_(0x24) & mask); i++) udelay(50);
    if (R32_(0x24) & mask) { rst(s, 6); return -1; }
    R16_(0x30) = 0xFFFF; R16_(0x32) = 0xFFFF; R32_(8) = arg; R16_(0xC) = mode; R16_(0xE) = (u16)(idx << 8 | rsp | (data ? 0x20 : 0));
    u64 end = ticks + 1000;
    for (;;) { u16 st = R16_(0x30); if (st & 0x8000) { u16 e = R16_(0x32); R16_(0x32) = 0xFFFF; R16_(0x30) = 0xFFFF; rst(s, 6); return e & 1 ? -2 : -3; } if (st & 1) { R16_(0x30) = 1; break; } if (ticks > end) { rst(s, 6); return -4; } yield(); }
    if (rsp == RSP_48B) { end = ticks + 2000; while (!(R16_(0x30) & 2) && ticks < end) { if (R16_(0x30) & 0x8000) break; yield(); } R16_(0x30) = 2; }
    return 0;
}
static int acmd(sd_t *s, int idx, u32 arg, int rsp) { int r = cmd(s, 55, s->rca << 16, RSP_48, 0, 0); return r ? r : cmd(s, idx, arg, rsp, 0, 0); }
static int xfer(blkdev_t *b, u64 lba, u32 n, void *buf, int wr) {
    sd_t *s = b->drv; u32 *p = buf; if (!(R32_(0x24) & (1 << 16))) return -1; R16_(4) = 512; R16_(6) = n; R8_(0x2E) = 0xE; u16 m = (wr ? 0 : 1 << 4) | (n > 1 ? 1 << 5 | 1 << 1 | 1 << 2 : 0);
    int c = wr ? (n > 1 ? 25 : 24) : (n > 1 ? 18 : 17), r = cmd(s, c, s->sdhc ? (u32)lba : (u32)lba * 512, RSP_48, m, 1); if (r) return r; u16 want = wr ? 1 << 4 : 1 << 5;
    for (u32 k = 0; k < n; k++) { u64 end = ticks + 3000; for (;;) { u16 st = R16_(0x30); if (st & 0x8000) { R16_(0x32) = 0xFFFF; R16_(0x30) = 0xFFFF; rst(s, 6); return -5; } if (st & want) { R16_(0x30) = want; break; } if (ticks > end) { rst(s, 6); return -6; } yield(); }
        for (int i = 0; i < 128; i++) { if (wr) R32_(0x20) = *p++; else *p++ = R32_(0x20); } }
    u64 end = ticks + 3000; for (;;) { u16 st = R16_(0x30); if (st & 0x8000) { R16_(0x32) = 0xFFFF; R16_(0x30) = 0xFFFF; rst(s, 6); return -7; } if (st & 2) { R16_(0x30) = 2; return 0; } if (ticks > end) { rst(s, 6); return -8; } yield(); }
}
static int sd_rd(blkdev_t *b, u64 l, u32 n, void *p) { return xfer(b, l, n, p, 0); }
static int sd_wr(blkdev_t *b, u64 l, u32 n, const void *p) { return xfer(b, l, n, (void *)p, 1); }
static int card_init(sd_t *s) {
    rst(s, 1); R16_(0x34) = 0xFFFF; R16_(0x36) = 0xFFFF; R16_(0x38) = 0; R16_(0x3A) = 0; R8_(0x29) = 0x0F; msleep(20); set_clock(s, 400); R8_(0x28) = 0; R8_(0x2E) = 0xE; msleep(5); s->rca = 0; s->sdhc = 0;
    cmd(s, 0, 0, RSP_NONE, 0, 0); msleep(2); int v2 = !cmd(s, 8, 0x1AA, RSP_48, 0, 0) && (R32_(0x10) & 0xFFF) == 0x1AA; u32 ocr = 0; int ok = 0;
    for (int i = 0; i < 100; i++) { if (acmd(s, 41, 0x00FF8000 | (v2 ? 1u << 30 : 0), RSP_R3)) { msleep(10); continue; } ocr = R32_(0x10); if (ocr >> 31) { ok = 1; break; } msleep(10); } if (!ok) { kprintf("sd: la tarjeta no responde\n"); return -1; }
    s->sdhc = !!(ocr & (1u << 30)); if (cmd(s, 2, 0, RSP_136, 0, 0) || cmd(s, 3, 0, RSP_48, 0, 0)) return -1; s->rca = R32_(0x10) >> 16; if (cmd(s, 9, s->rca << 16, RSP_136, 0, 0)) return -1; u32 c[4]; for (int i = 0; i < 4; i++) c[i] = R32_(0x10 + i * 4); u64 sec;
    if (c[3] >> 22 & 3) sec = (((u64)((c[1] >> 8) & 0x3FFFFF)) + 1) * 1024; else { u32 bl = (c[2] >> 8) & 15, cs = ((c[2] & 3) << 10) | (c[1] >> 22), mu = (c[1] >> 7) & 7; sec = (u64)(cs + 1) << (mu + 2 + bl - 9); }
    if (cmd(s, 7, s->rca << 16, RSP_48B, 0, 0)) return -1; if (!s->sdhc) cmd(s, 16, 512, RSP_48, 0, 0); if (!acmd(s, 6, 2, RSP_48)) R8_(0x28) |= 2; set_clock(s, 25000);
    blkdev_t *b = blk_new("sd"); b->drv = s; b->ssize = 512; b->sectors = sec; strncpy0(b->model, s->sdhc ? "Tarjeta SDHC/SDXC" : "Tarjeta SD", 41); b->read = sd_rd; b->write = sd_wr; b->removable = 1; b->maxsect = 64; s->blk = b; blk_register(b); return 0;
}
static void th(void *a) { sd_t *s = a; for (;;) { int in = !!(R32_(0x24) & (1 << 16)); if (in && !s->present) { msleep(200); s->present = 1; kprintf("sd: tarjeta insertada\n"); if (card_init(s)) kprintf("sd: no se pudo iniciar la tarjeta\n"); } else if (!in && s->present) { s->present = 0; kprintf("sd: tarjeta retirada\n"); if (s->blk) { blk_remove(s->blk); s->blk = 0; } R8_(0x29) = 0; } msleep(500); } }
void sdhci_init(void) {
    int id = 0; for (int i = 0; i < pci_count; i++) { pci_t *p = &pci_devs[i]; if (!((p->cls == 8 && p->sub == 5) || (p->vendor == 0x1180 && (p->device == 0xE822 || p->device == 0xE823)))) continue;
        pci_enable(p, 1, 0, 1); sd_t *s = kzalloc(sizeof *s); s->id = id++; s->r = ioremap(pci_bar(p, 0), 256); rst(s, 1); u32 cap = R32_(0x40); s->base_mhz = (cap >> 8) & 255; if (!s->base_mhz) s->base_mhz = 50; kprintf("sd: %04x:%04x base %u MHz version %x\n", p->vendor, p->device, s->base_mhz, R16_(0xFE)); kthread("sd", th, s); }
}
