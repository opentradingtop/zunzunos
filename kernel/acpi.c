#include "kernel.h"
typedef struct PACKED { char sig[4]; u32 len; u8 rev, sum; char oem[6], tid[8]; u32 orev, cid, crev; } sdt_t;
static u32 pm1a, pm1b; static u16 slp_a, slp_b; static int have_s5; static u8 reset_val; static u64 reset_addr; static int reset_space = -1;
static u32 smi_cmd; static u8 acpi_en; u32 acpi_pm_tmr;

static sdt_t *map_table(u64 pa) {
    sdt_t *h = ioremap(pa, 4096);
    if (h->len > 4096 && h->len < (16u << 20)) h = ioremap(pa, h->len);
    return h;
}
static u64 find_rsdp(void) {
    if (bi.rsdp) return bi.rsdp;
    u16 ebda = *(u16 *)P2V(0x40E);
    u64 ranges[2][2] = {{(u64)ebda << 4, ((u64)ebda << 4) + 1024}, {0xE0000, 0x100000}};
    for (int r = 0; r < 2; r++) for (u64 a = ranges[r][0]; a + 16 < ranges[r][1]; a += 16) if (!memcmp(P2V(a), "RSD PTR ", 8)) return a;
    return 0;
}
static void parse_fadt(u8 *f, u32 len) {
    acpi_pm_tmr = *(u32 *)(f + 76); pm1a = *(u32 *)(f + 64); pm1b = *(u32 *)(f + 68); smi_cmd = *(u32 *)(f + 48); acpi_en = f[52];
    u64 dsdt = *(u32 *)(f + 40);
    if (len >= 148 && *(u64 *)(f + 140)) dsdt = *(u64 *)(f + 140);
    if (len >= 129 && (*(u32 *)(f + 112) & (1 << 10))) { reset_space = f[116]; reset_addr = *(u64 *)(f + 120); reset_val = f[128]; }
    if (!dsdt) return;
    sdt_t *d = map_table(dsdt); u8 *p = (u8 *)d;
    for (u32 i = 36; i + 16 < d->len; i++) {
        if (memcmp(p + i, "_S5_", 4)) continue;
        u8 *q = p + i + 4;
        if (*q != 0x12) continue;
        q++; q += ((*q >> 6) & 3) + 1; q++;
        if (*q == 0x0A) q++;
        slp_a = *q++ ; if (*q == 0x0A) q++;
        slp_b = *q; have_s5 = 1;
        break;
    }
}
void acpi_init(void) {
    u64 ra = find_rsdp();
    if (!ra) { kprintf("acpi: no encontrado\n"); return; }
    u8 *r = ioremap(ra, 64); u64 x = 0; int w = 4; u64 root = *(u32 *)(r + 16);
    if (r[15] >= 2 && (x = *(u64 *)(r + 24))) { root = x; w = 8; }
    sdt_t *t = map_table(root); int n = (t->len - 36) / w;
    for (int i = 0; i < n; i++) {
        u8 *e = (u8 *)t + 36 + i * w;
        u64 pa = w == 8 ? *(u64 *)e : *(u32 *)e;
        sdt_t *s = map_table(pa);
        if (!memcmp(s->sig, "FACP", 4)) parse_fadt((u8 *)s, s->len);
    }
    kprintf("acpi: PM1a=%x S5=%d reset=%d\n", pm1a, have_s5, reset_space);
}
void acpi_poweroff(void) {
    vfs_sync(); blk_sync();
    __asm__("cli");
    if (have_s5 && pm1a) {
        if (smi_cmd && acpi_en && !(inw(pm1a) & 1)) { outb(smi_cmd, acpi_en); for (int i = 0; i < 300 && !(inw(pm1a) & 1); i++) mdelay_busy(10); }
        outw(pm1a, slp_a << 10 | 1 << 13);
        if (pm1b) outw(pm1b, slp_b << 10 | 1 << 13);
        mdelay_busy(2000);
    }
    outw(0x604, 0x2000); outw(0xB004, 0x2000);
    kprintf("no se pudo apagar: ya puedes cortar la corriente\n");
    for (;;) __asm__("hlt");
}
void acpi_reboot(void) {
    vfs_sync(); blk_sync();
    __asm__("cli");
    if (reset_space == 1) outb(reset_addr, reset_val);
    else if (reset_space == 0) { volatile u8 *p = ioremap(reset_addr, 1); *p = reset_val; }
    mdelay_busy(50);
    outb(0xCF9, 2); outb(0xCF9, 6); mdelay_busy(50);
    for (int i = 0; i < 1000 && (inb(0x64) & 2); i++) udelay(10);
    outb(0x64, 0xFE); mdelay_busy(50);
    struct PACKED { u16 l; u64 b; } z = {0, 0};
    __asm__("lidt %0; int3" : : "m"(z));
    for (;;) __asm__("hlt");
}
