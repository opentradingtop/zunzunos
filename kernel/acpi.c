#include "kernel.h"
typedef struct PACKED { char sig[4]; u32 len; u8 rev, sum; char oem[6], tid[8]; u32 orev, cid, crev; } sdt_t;
static u32 pm1a, pm1b; static u16 slp_a, slp_b; static int have_s5; static u8 reset_val; static u64 reset_addr; static int reset_space = -1;
static u32 smi_cmd; static u8 acpi_en; u32 acpi_pm_tmr;
static u8 *dsdt_p; static u32 dsdt_len; static u16 ec_cmd = 0x66, ec_dat = 0x62;

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
    sdt_t *d = map_table(dsdt); u8 *p = (u8 *)d; dsdt_p = p; dsdt_len = d->len;
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
        if (!memcmp(s->sig, "ECDT", 4) && s->len >= 60 && ((u8 *)s)[36] == 1 && ((u8 *)s)[48] == 1) { ec_cmd = *(u64 *)((u8 *)s + 40); ec_dat = *(u64 *)((u8 *)s + 52); }
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

/* ===== Bateria =====
   Sin interprete de AML, la bateria se lee como hace el propio AML de los ThinkPad: del controlador empotrado (EC).
   Las posiciones no se escriben a mano: se sacan de las definiciones Field de la region EmbeddedControl del DSDT
   (nombres HB0S/HB0A estado, HIID pagina, SBRC/SBFC capacidad restante y total, HPAC cargador). Si el DSDT no
   tiene esos nombres (equipo que no es ThinkPad), no se muestra nada. */
typedef struct { char n[5]; u16 bit, bits; } ecf_t;
static ecf_t ECF[] = {{"HB0S"}, {"HB0A"}, {"HIID"}, {"SBRC"}, {"SBFC"}, {"HPAC"}, {"SBAC"}};
enum { F_HB0S, F_HB0A, F_HIID, F_SBRC, F_SBFC, F_HPAC, F_SBAC, NF };
static int bat_state = -1;   /* -1 sin mirar, 0 no hay, 1 hay */
static u32 pkglen(u8 **pp) { u8 *p = *pp; u8 b = *p++; int n = b >> 6; u32 l = n ? b & 15 : b & 63; for (int i = 0; i < n; i++) l |= (u32)*p++ << (4 + 8 * i); *pp = p; return l; }
static void nameseg_last(u8 **pp, char *o) {   /* NameString: deja en o el ultimo segmento de 4 letras */
    u8 *p = *pp; while (*p == '\\' || *p == '^') p++; int n = 1; if (*p == 0x2E) { n = 2; p++; } else if (*p == 0x2F) { p++; n = *p++; } else if (*p == 0) { o[0] = 0; *pp = p + 1; return; }
    for (int i = 0; i < n; i++) { memcpy(o, p, 4); p += 4; } o[4] = 0; *pp = p; }
static int ec_wait(int bit, int set) { for (int i = 0; i < 20000; i++) { u8 st = inb(ec_cmd); if (!!(st & bit) == set) return 0; udelay(5); } return -1; }
static int ec_rd(u8 a) { if (ec_wait(2, 0)) return -1; outb(ec_cmd, 0x80); if (ec_wait(2, 0)) return -1; outb(ec_dat, a); if (ec_wait(1, 1)) return -1; return inb(ec_dat); }
static int ec_wr(u8 a, u8 v) { if (ec_wait(2, 0)) return -1; outb(ec_cmd, 0x81); if (ec_wait(2, 0)) return -1; outb(ec_dat, a); if (ec_wait(2, 0)) return -1; outb(ec_dat, v); return ec_wait(2, 0); }
static void bat_scan(void) {
    bat_state = 0; if (!dsdt_p) return; char regs[16][5]; int nr = 0;
    for (u32 i = 36; i + 8 < dsdt_len && nr < 16; i++) if (dsdt_p[i] == 0x5B && dsdt_p[i + 1] == 0x80) {   /* OperationRegion */
        u8 *q = dsdt_p + i + 2; char n[5]; nameseg_last(&q, n); if (n[0] && *q == 0x03) memcpy(regs[nr++], n, 5); }
    if (!nr) return; int found = 0;
    for (u32 i = 36; i + 8 < dsdt_len; i++) if (dsdt_p[i] == 0x5B && dsdt_p[i + 1] == 0x81) {   /* Field */
        u8 *q = dsdt_p + i + 2, *st = q; u32 len = pkglen(&q); u8 *end = st + len; if (end > dsdt_p + dsdt_len) continue; char rn[5]; nameseg_last(&q, rn); int ec = 0;
        for (int r = 0; r < nr; r++) if (!memcmp(rn, regs[r], 4)) ec = 1; if (!ec) continue; q++;   /* flags */
        u32 bit = 0;
        while (q < end) { if (*q == 0x00) { q++; bit += pkglen(&q); } else if (*q == 0x01) q += 3; else if (*q == 0x03) q += 4; else if (*q == 0x02) break;
            else { char fn[5]; memcpy(fn, q, 4); fn[4] = 0; q += 4; u32 w = pkglen(&q);
                for (int f = 0; f < NF; f++) if (!ECF[f].bits && !memcmp(fn, ECF[f].n, 4)) { ECF[f].bit = bit; ECF[f].bits = w; found++; }
                bit += w; } } }
    if (ECF[F_HB0S].bits && ECF[F_HIID].bits && ECF[F_SBRC].bits && ECF[F_SBFC].bits) bat_state = 1;
    kprintf("bateria: %s (EC %x/%x, HB0S %d HIID %d SBRC %d SBFC %d HPAC %d)\n", bat_state ? "controlador ThinkPad" : "no se encontro", ec_cmd, ec_dat, ECF[F_HB0S].bit / 8, ECF[F_HIID].bit / 8, ECF[F_SBRC].bit / 8, ECF[F_SBFC].bit / 8, ECF[F_HPAC].bits ? ECF[F_HPAC].bit : -1); }
static int ec_field(int f) { ecf_t *e = &ECF[f]; if (!e->bits) return -1; u32 v = 0; int nb = (e->bit % 8 + e->bits + 7) / 8;
    for (int i = 0; i < nb && i < 4; i++) { int b = ec_rd(e->bit / 8 + i); if (b < 0) return -1; v |= (u32)b << (8 * i); } v >>= e->bit % 8; if (e->bits < 32) v &= (1u << e->bits) - 1; return (int)v; }
/* o[0] hay bateria, o[1] porcentaje, o[2] cargando, o[3] con cargador, o[4] descargando. Devuelve 0 o error */
int acpi_battery(int *o) {
    static u64 last; static int cache[5], ok;
    if (bat_state < 0) bat_scan(); if (bat_state != 1) return E_NOENT;
    if (ok && ticks - last < 5 * HZ) { memcpy(o, cache, sizeof cache); return 0; }
    u64 f = irq_save(); int st = ec_field(F_HB0S), at = ECF[F_HB0A].bits ? ec_field(F_HB0A) : 1, ac = ECF[F_HPAC].bits ? ec_field(F_HPAC) : -1, rc = -1, fc = -1;
    if (st >= 0 && at > 0 && ec_wr(ECF[F_HIID].bit / 8, 0) == 0) { rc = ec_field(F_SBRC); fc = ec_field(F_SBFC); }
    irq_restore(f);
    static int fails; if (st < 0) { if (++fails >= 3) { bat_state = 0; kprintf("bateria: el controlador no responde\n"); } return E_IO; } fails = 0;
    cache[0] = at > 0 && rc >= 0 && fc > 0; cache[1] = cache[0] ? MIN(100, (int)((u64)rc * 100 / fc)) : 0; cache[2] = !!(st & 0x20); cache[4] = !!(st & 0x40);
    cache[3] = ac >= 0 ? ac : !cache[4]; last = ticks; ok = 1;
    static int lp = -1; if (cache[1] / 10 != lp) { lp = cache[1] / 10; kprintf("bateria: %d%% %s\n", cache[1], cache[2] ? "cargando" : cache[4] ? "descargando" : cache[3] ? "con cargador" : ""); }
    memcpy(o, cache, sizeof cache); return 0; }
