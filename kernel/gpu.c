/* Brillo: registros PWM de las graficas Intel; si no, atenuacion por software */
#include "kernel.h"
static volatile u8 *mm; static u32 maxv; static int kind, cur_pct = 100; /* 1 = SNB/IVB, 2 = HSW */
#define RD(o) R32(mm + (o))
extern void wm_set_dim(int);
void backlight_init(void) {
    for (int i = 0; i < pci_count; i++) {
        pci_t *p = &pci_devs[i]; if (p->cls != 3 || p->vendor != 0x8086) continue; u16 d = p->device, f = d & 0xFF00;
        if (f == 0x0100) kind = 1; else if (f == 0x0400 || f == 0x0A00 || f == 0x0C00 || f == 0x0D00) kind = 2; else continue;
        pci_enable(p, 1, 0, 1); mm = ioremap(pci_bar(p, 0), 0x100000); u32 c2 = RD(0xC8254); maxv = c2 >> 16;
        u32 cur = kind == 1 ? RD(0x48254) & 0xFFFF : c2 & 0xFFFF; kprintf("brillo: Intel %04x max=%u actual=%u\n", d, maxv, cur);
        if (maxv < 16 || cur > maxv) { maxv = 0; kind = 0; mm = 0; } else cur_pct = MAX(5, MIN(100, cur * 100 / maxv));
        return;
    }
}
int backlight_get(void) { return cur_pct | (kind ? 256 : 0); }
extern void wm_blank(int);
/* apaga la pantalla: luz de fondo a cero (Intel) y imagen en negro; al encender recupera el brillo */
int screen_power(int on) {
    if (!on) { if (kind) { u32 o = kind == 1 ? 0x48254 : 0xC8254; R32(mm + o) = RD(o) & 0xFFFF0000u; } wm_blank(1); }
    else { wm_blank(0); backlight_set(cur_pct); }
    return 0;
}
int backlight_set(int p) {
    p = MAX(5, MIN(p, 100)); cur_pct = p;
    if (kind) {
        u32 lv = MAX(maxv * p / 100, maxv / 20), o = kind == 1 ? 0x48254 : 0xC8254, v = (RD(o) & 0xFFFF0000u) | lv; R32(mm + o) = v;
        if ((RD(o) & 0xFFFF) == lv) { wm_set_dim(100); return 0; }
        kprintf("brillo: el registro no acepta el valor, se usa software\n"); kind = 0;
    }
    wm_set_dim(p); return 0;
}
