/* Teclado y raton: PS/2 + cola comun (tambien la usa USB). Teclas = codigos HID */
#include "kernel.h"
#define Q 512
static inev_t q[Q]; static volatile u32 qh, qt; task_t *wm_task; int key_layout = 1; /* 0 US, 1 ES */
static int ps2_ok, has_wheel; static u8 mpkt[4]; static int mi, e0;

volatile u64 last_input; /* ticks del ultimo evento de teclado, raton o mando */
static void push(int k, int a, int b, int c, int d) {
    last_input = ticks;
    u64 f = irq_save(); u32 n = (qt + 1) % Q;
    if (n != qh) { q[qt] = (inev_t){k, a, b, c, d}; qt = n; }
    irq_restore(f);
    if (wm_task) task_wake(wm_task);
}
int input_pop(inev_t *e) { u64 f = irq_save(); int r = qh != qt; if (r) { *e = q[qh]; qh = (qh + 1) % Q; } irq_restore(f); return r; }
void input_key(u8 u, int down) { push(1, u, down, 0, 0); }
void input_joy(u32 b, int dx, int dy, u32 id) { push(4, b, dx, dy, id); }
volatile u8 kbd_leds = 2; volatile u32 kbd_ledgen = 1;
static void ps2_leds(void);
void kbd_set_leds(u8 l) { kbd_leds = l; kbd_ledgen++; ps2_leds(); }
void input_mouse(int dx, int dy, int b, int w) { push(2, dx, dy, b, w); }
void input_abs(int x, int y, int mx, int my, int b, int w) {
    push(3, (u64)x * scr_w / (mx + 1), (u64)y * scr_h / (my + 1), b, w);
}

static const u8 set1[0x59] = {0,0x29,0x1E,0x1F,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x2D,0x2E,0x2A,0x2B,
 0x14,0x1A,0x08,0x15,0x17,0x1C,0x18,0x0C,0x12,0x13,0x2F,0x30,0x28,0xE0,0x04,0x16,
 0x07,0x09,0x0A,0x0B,0x0D,0x0E,0x0F,0x33,0x34,0x35,0xE1,0x31,0x1D,0x1B,0x06,0x19,
 0x05,0x11,0x10,0x36,0x37,0x38,0xE5,0x55,0xE2,0x2C,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,
 0x3F,0x40,0x41,0x42,0x43,0x53,0x47,0x5F,0x60,0x61,0x56,0x5C,0x5D,0x5E,0x57,0x59,
 0x5A,0x5B,0x62,0x63,0x46,0,0x64,0x44,0x45}; /* 0x54 = Alt+Impr Pant (PetSis) */
static u8 set1e0(u8 s) {
    switch (s) {
    case 0x1C: return 0x58; case 0x1D: return 0xE4; case 0x35: return 0x54; case 0x38: return 0xE6;
    case 0x47: return 0x4A; case 0x48: return 0x52; case 0x49: return 0x4B; case 0x4B: return 0x50;
    case 0x4D: return 0x4F; case 0x4F: return 0x4D; case 0x50: return 0x51; case 0x51: return 0x4E;
    case 0x37: return 0x46; /* Impr Pant */
    case 0x52: return 0x49; case 0x53: return 0x4C; case 0x5B: return 0xE3; case 0x5C: return 0xE7; case 0x5D: return 0x65;
    }
    return 0;
}
#ifdef __arm__
/* la Raspberry Pi no tiene PS/2: el teclado y el raton llegan por la pantalla tactil (y mas adelante por USB) */
void ps2_poll(void) {}
static void ps2_leds(void) {}
void input_init(void) { (void)set1; (void)set1e0; (void)mpkt; (void)mi; (void)e0; (void)has_wheel; (void)ps2_ok; }
#else
void ps2_poll(void) {
    if (!ps2_ok) return;
    for (int n = 0; n < 16; n++) {
        u8 st = inb(0x64);
        if (!(st & 1)) return;
        u8 d = inb(0x60);
        if (st & 0x20) {
            if (mi == 0 && !(d & 8)) continue;
            mpkt[mi++] = d;
            if (mi == (has_wheel ? 4 : 3)) {
                mi = 0;
                int dx = mpkt[1] - ((mpkt[0] & 0x10) ? 256 : 0), dy = mpkt[2] - ((mpkt[0] & 0x20) ? 256 : 0);
                int w = has_wheel ? (i8)mpkt[3] : 0;
                input_mouse(dx, -dy, mpkt[0] & 7, w);
            }
        } else {
            if (d == 0xE0) { e0 = 1; continue; }
            if (d == 0xE1 || d == 0xFA || d == 0xFE) continue;
            int up = d & 0x80; u8 s = d & 0x7F;
            u8 u = e0 ? set1e0(s) : (s < sizeof set1 ? set1[s] : 0);
            e0 = 0;
            if (u) input_key(u, !up);
        }
    }
}
static int wait_w(void) { for (int i = 0; i < 20000; i++) { if (!(inb(0x64) & 2)) return 1; udelay(5); } return 0; }
static int wait_r(void) { for (int i = 0; i < 20000; i++) { if (inb(0x64) & 1) return 1; udelay(5); } return 0; }
static void cmd(u8 c) { wait_w(); outb(0x64, c); }
static void dat(u8 d) { wait_w(); outb(0x60, d); }
static int rd(void) { return wait_r() ? inb(0x60) : -1; }
static int mouse_cmd(u8 c) { cmd(0xD4); dat(c); return rd(); }
static void ps2_leds(void) { if (!ps2_ok) return; dat(0xED); udelay(300); dat(kbd_leds & 7); }
void input_init(void) {
    if (inb(0x64) == 0xFF) { kprintf("ps2: no hay controlador\n"); return; }
    u64 f = irq_save();
    cmd(0xAD); cmd(0xA7);
    for (int i = 0; i < 64 && (inb(0x64) & 1); i++) inb(0x60);
    cmd(0x20); int c = rd();
    if (c < 0) { irq_restore(f); kprintf("ps2: sin respuesta\n"); return; }
    c = (c & ~3) | 0x40; c &= ~0x30; cmd(0x60); dat(c);
    cmd(0xAE); cmd(0xA8);
    if (mouse_cmd(0xF6) >= 0) {
        static const u8 mg[] = {200, 100, 80};
        for (int i = 0; i < 3; i++) { mouse_cmd(0xF3); mouse_cmd(mg[i]); }
        mouse_cmd(0xF2); int id = rd(); has_wheel = id == 3;
        mouse_cmd(0xF4);
    }
    ps2_ok = 1;
    irq_restore(f); ps2_leds();
    kprintf("ps2: listo, rueda=%d\n", has_wheel);
}

#endif
/* --- distribuciones --- */
#define D_ACUTE 0xE001
#define D_GRAVE 0xE002
#define D_CIRC 0xE003
#define D_DIAER 0xE004
static const char us_n[] = "abcdefghijklmnopqrstuvwxyz1234567890", us_s[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()";
static u32 deadk;
static u32 combine(u32 d, u32 c) {
    static const char *v = "aeiouAEIOU";
    static const u16 ac[] = {0xE1,0xE9,0xED,0xF3,0xFA,0xC1,0xC9,0xCD,0xD3,0xDA}, gr[] = {0xE0,0xE8,0xEC,0xF2,0xF9,0xC0,0xC8,0xCC,0xD2,0xD9};
    static const u16 ci[] = {0xE2,0xEA,0xEE,0xF4,0xFB,0xC2,0xCA,0xCE,0xD4,0xDB}, di[] = {0xE4,0xEB,0xEF,0xF6,0xFC,0xC4,0xCB,0xCF,0xD6,0xDC};
    if (c == ' ') return d == D_ACUTE ? 0xB4 : d == D_GRAVE ? '`' : d == D_CIRC ? '^' : 0xA8;
    const u16 *t = d == D_ACUTE ? ac : d == D_GRAVE ? gr : d == D_CIRC ? ci : di;
    for (int i = 0; i < 10; i++) if ((u32)(u8)v[i] == c) return t[i];
    return c;
}
/* caracter de la tecla segun la distribucion, sin tocar el estado de las teclas muertas (0xE001-0xE004 = muerta) */
u32 key_char(u8 u, int m) {
    int s = !!(m & M_SHIFT), ag = !!(m & M_ALTGR); u32 c = 0;
    if ((m & M_CAPS) && ((u >= 4 && u <= 0x1D) || u == 0x33 || u == 0x31 || u == 0x32)) s = !s;
    if (u == 0x28 || u == 0x58) return '\n';
    if (u == 0x2A) return 8;
    if (u == 0x2B) return '\t';
    if (u == 0x29) return 27;
    if (u == 0x2C) c = ' ';
    else if (u == 0x4C) return 127;
    else if (u >= 0x54 && u <= 0x57) c = "/*-+"[u - 0x54];
    else if (u >= 0x59 && u <= 0x62) c = "1234567890"[u - 0x59];
    else if (u == 0x63) c = '.';
    else if (key_layout == 0) {
        if (u >= 4 && u <= 0x27) c = (u8)(s ? us_s : us_n)[u - 4];
        else switch (u) {
            case 0x2D: c = s ? '_' : '-'; break; case 0x2E: c = s ? '+' : '='; break; case 0x2F: c = s ? '{' : '['; break;
            case 0x30: c = s ? '}' : ']'; break; case 0x31: case 0x32: c = s ? '|' : '\\'; break; case 0x33: c = s ? ':' : ';'; break;
            case 0x34: c = s ? '"' : '\''; break; case 0x35: c = s ? '~' : '`'; break; case 0x36: c = s ? '<' : ','; break;
            case 0x37: c = s ? '>' : '.'; break; case 0x38: c = s ? '?' : '/'; break; case 0x64: c = s ? '>' : '<'; break;
        }
    } else {
        if (u >= 4 && u <= 0x1D) { c = (u8)(s ? us_s : us_n)[u - 4]; if (ag && u == 8) c = 0x20AC; }
        else if (u >= 0x1E && u <= 0x27) {
            static const u16 sh[] = {'!','"',0xB7,'$','%','&','/','(',')','='};
            c = s ? sh[u - 0x1E] : (u8)us_n[u - 4];
            if (ag) switch (u) { case 0x1E: c = '|'; break; case 0x1F: c = '@'; break; case 0x20: c = '#'; break; case 0x21: c = '~'; break; case 0x23: c = 0xAC; break; }
        } else switch (u) {
            case 0x2D: c = s ? '?' : '\''; break; case 0x2E: c = s ? 0xBF : 0xA1; break;
            case 0x2F: c = ag ? '[' : s ? D_CIRC : D_GRAVE; break; case 0x30: c = ag ? ']' : s ? '*' : '+'; break;
            case 0x31: case 0x32: c = ag ? '}' : s ? 0xC7 : 0xE7; break; case 0x33: c = s ? 0xD1 : 0xF1; break;
            case 0x34: c = ag ? '{' : s ? D_DIAER : D_ACUTE; break; case 0x35: c = ag ? '\\' : s ? 0xAA : 0xBA; break;
            case 0x36: c = s ? ';' : ','; break; case 0x37: c = s ? ':' : '.'; break; case 0x38: c = s ? '_' : '-'; break;
            case 0x64: c = s ? '>' : '<'; break;
        }
    }
    return c;
}
u32 key_translate(u8 u, int m) {
    u32 c = key_char(u, m);
    if (c == '\n' || c == 8 || c == '\t' || c == 27 || c == 127) return c;
    if (!c) return 0;
    if (c >= 0xE001 && c <= 0xE004) { deadk = c; return 0; }
    if (deadk) { u32 d = deadk; deadk = 0; c = combine(d, c); }
    if ((m & M_CTRL) && c < 128) { if (c >= 'a' && c <= 'z') return c - 96; if (c >= 'A' && c <= 'Z') return c - 64; }
    return c;
}
