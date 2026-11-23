/* Prueba de la bateria: un DSDT con las regiones del EC como las de un ThinkPad y un EC simulado. */
#include "kernel.h"
bi_t bi; volatile u64 ticks = 100000;
static u8 ecmem[256], dsdt[4096]; static int dn;
void *ioremap(u64 pa, u64 n) { (void)pa; (void)n; return dsdt; }
/* EC simulado: comando 0x80 leer, 0x81 escribir */
static int st_cmd, st_addr = -1, obf; static u8 outv;
u8 inb(u16 p) { if (p == 0x66) return obf ? 1 : 0; if (p == 0x62) { obf = 0; return outv; } return 0xFF; }
u16 inw(u16 p) { (void)p; return 0; } void outw(u16 p, u16 v) { (void)p; (void)v; }
void outb(u16 p, u8 v) { if (p == 0x66) { st_cmd = v; st_addr = -1; return; } if (p == 0x62) { if (st_addr < 0) { st_addr = v; if (st_cmd == 0x80) { outv = ecmem[v]; obf = 1; } } else if (st_cmd == 0x81) ecmem[st_addr] = v; } }
#include "acpi.c"
static void b(int v) { dsdt[dn++] = v; } static void seg(const char *s) { memcpy(dsdt + dn, s, 4); dn += 4; }
static void plen(int l) { if (l < 64) b(l); else { b(0x40 | (l & 15)); b(l >> 4); } }
static int fstart; static void field_begin(const char *reg) { b(0x5B); b(0x81); fstart = dn; b(0); b(0); seg(reg); b(0x01); }
static void field_end(void) { int l = dn - fstart; dsdt[fstart] = 0x40 | (l & 15); dsdt[fstart + 1] = l >> 4; }
static void res(int bits) { b(0); plen(bits); } static void nf(const char *n, int bits) { seg(n); plen(bits); }
int main(void) {
    dn = 36; b(0x5B); b(0x80); b('\\'); seg("ECOR"); b(0x03); b(0x0A); b(0); b(0x0B); b(0); b(1);   /* OperationRegion (\ECOR, EmbeddedControl, 0, 0x100) */
    b(0x5B); b(0x80); seg("GNVS"); b(0x00); b(0x0A); b(0); b(0x0A); b(16);                          /* otra region que no es del EC */
    field_begin("GNVS"); nf("SBRC", 16); field_end();                                                 /* no debe confundir */
    field_begin("ECOR"); res(0x38 * 8); nf("HB0S", 7); nf("HB0A", 1); res((0x46 - 0x39) * 8 + 4); nf("HPAC", 1); res(3); res((0x81 - 0x47) * 8); nf("HIID", 8); field_end();
    field_begin("ECOR"); res(0xA0 * 8); nf("SBRC", 16); nf("SBFC", 16); nf("SBAE", 16); nf("SBRS", 16); nf("SBAC", 16); field_end();
    dsdt_p = dsdt; dsdt_len = dn; int o[5], ok = 1;
    ecmem[0x38] = 0x80 | 0x40; ecmem[0x46] = 0x00; ecmem[0xA0] = 0x34; ecmem[0xA1] = 0x12; ecmem[0xA2] = 0x50; ecmem[0xA3] = 0x15; ecmem[0x81] = 0x55;  /* 0x1234 de 0x1550: 85 %, descargando, sin cargador */
    int r = acpi_battery(o); printf("r=%d hay=%d %d%% cargando=%d cargador=%d descargando=%d HIID=%x\n", r, o[0], o[1], o[2], o[3], o[4], ecmem[0x81]);
    if (r || !o[0] || o[1] != 85 || o[2] || o[3] || !o[4] || ecmem[0x81] != 0) { printf("FALLO 1\n"); ok = 0; }
    ticks += 10 * HZ; ecmem[0x38] = 0x80 | 0x20; ecmem[0x46] = 0x10; ecmem[0xA0] = 0x50; ecmem[0xA1] = 0x15;   /* lleno y cargando con cargador */
    r = acpi_battery(o); printf("r=%d hay=%d %d%% cargando=%d cargador=%d descargando=%d\n", r, o[0], o[1], o[2], o[3], o[4]);
    if (r || o[1] != 100 || !o[2] || !o[3] || o[4]) { printf("FALLO 2\n"); ok = 0; }
    ticks += 10 * HZ; ecmem[0x38] = 0x00; r = acpi_battery(o); printf("sin bateria: hay=%d\n", o[0]); if (o[0]) { printf("FALLO 3\n"); ok = 0; }
    printf("bateria: %s\n", ok ? "OK" : "FALLO"); return !ok; }
