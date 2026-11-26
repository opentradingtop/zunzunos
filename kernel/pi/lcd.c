/* ZunzunOS - SPI0 de la Raspberry Pi y pantalla ILI9486 de 480x320 (3,5" en el conector GPIO).
   El gestor de ventanas compone en el framebuffer (32 bits) y avisa con lcd_dirty(); un hilo copia las zonas
   cambiadas a la pantalla por SPI en RGB565. La pantalla de 3,5" (A) lleva registros de desplazamiento delante del
   ILI9486: cada orden, dato o pixel va en una palabra de 16 bits (el byte de orden o dato en la parte baja). */
#include "../kernel.h"
#include "pantalla.h"

#define SPI(o) R32(PERI(0x204000 + (o)))
#define CS_TA (1u << 7)
#define CS_DONE (1u << 16)
#define CS_RXD (1u << 17)
#define CS_TXD (1u << 18)
static mutex_t spi_m; static int spi_ok;

/* --- SPI0 por sondeo --- */
int spi_present(void) { return spi_ok; }
void spi_begin(int cs, int div) { mutex_lock(&spi_m); SPI(0x08) = div; SPI(0x00) = (cs & 3) | 0x30; SPI(0x00) = (cs & 3) | CS_TA; }
void spi_end(void) { for (int t = 0; t < 200000 && !(SPI(0x00) & CS_DONE); t++) continue; while (SPI(0x00) & CS_RXD) (void)SPI(0x04); SPI(0x00) = 0x30; mutex_unlock(&spi_m); }
/* envia n bytes y guarda los recibidos en in (si no es nulo) */
void spi_xfer(const u8 *out, u8 *in, int n) {
    int tx = 0, rx = 0;
    for (int guard = 0; (tx < n || rx < n) && guard < 4000000; guard++) {
        u32 cs = SPI(0x00);
        if (tx < n && (cs & CS_TXD)) SPI(0x04) = out ? out[tx++] : (tx++, 0);
        if (rx < n && (cs & CS_RXD)) { u8 v = SPI(0x04); if (in) in[rx] = v; rx++; }
    }
}
static int spi_probe(void) {
    for (int p = 7; p <= 11; p++) gpio_func(p, 4);   /* CE1, CE0, MISO, MOSI, SCLK en ALT0 */
    SPI(0x00) = 0x30;
    u32 cs = SPI(0x00);
    return (cs & CS_TXD) != 0;   /* sin controlador (QEMU) se lee 0 */
}

/* --- ILI9486 --- */
static int lcd_ok, madctl = LCD_MADCTL; static volatile int dx0 = 1 << 30, dy0 = 1 << 30, dx1 = -1, dy1 = -1;
static task_t *lcd_task; static u8 *line;
static void w16(int dc, const u8 *b, int n) {   /* n valores de 8 bits, cada uno en una palabra de 16 */
    u8 t[64]; gpio_set(LCD_PIN_DC, dc);
    while (n > 0) { int k = MIN(n, 32); for (int i = 0; i < k; i++) { t[2 * i] = 0; t[2 * i + 1] = b[i]; } spi_xfer(t, 0, 2 * k); b += k; n -= k; }
}
static void cmd(u8 c, const u8 *d, int n) {
    spi_begin(LCD_CS, LCD_SPI_DIV); w16(0, &c, 1); spi_end();
    if (n) { spi_begin(LCD_CS, LCD_SPI_DIV); w16(1, d, n); spi_end(); }
}
#define C(c, ...) do { static const u8 d_[] = {__VA_ARGS__}; cmd(c, d_, sizeof d_); } while (0)
static void lcd_reset(void) {
    gpio_set(LCD_PIN_RST, 1); msleep(5); gpio_set(LCD_PIN_RST, 0); msleep(20); gpio_set(LCD_PIN_RST, 1); msleep(120);
    cmd(0x01, 0, 0); msleep(120);           /* reinicio por programa */
    cmd(0x11, 0, 0); msleep(120);           /* salir del reposo */
    C(0x3A, 0x55);                          /* 16 bits por pixel */
    C(0xC2, 0x44);                          /* control de alimentacion 3 */
    C(0xC5, 0x00, 0x00, 0x00, 0x00);        /* VCOM */
    { u8 m = madctl; cmd(0x36, &m, 1); }    /* orientacion */
    cmd(0x20, 0, 0);                        /* sin invertir colores */
    cmd(0x29, 0, 0); msleep(20);            /* encender */
}
static void push(int x0, int y0, int x1, int y1) {   /* copia [x0,x1) x [y0,y1) del framebuffer */
    extern u32 *wm_fb_line(int y);
    u8 a[4] = {x0 >> 8, x0, (x1 - 1) >> 8, x1 - 1}, b[4] = {y0 >> 8, y0, (y1 - 1) >> 8, y1 - 1};
    cmd(0x2A, a, 4); cmd(0x2B, b, 4);
    spi_begin(LCD_CS, LCD_SPI_DIV); { u8 c = 0x2C; w16(0, &c, 1); }
    gpio_set(LCD_PIN_DC, 1);
    for (int y = y0; y < y1; y++) {
        u32 *s = wm_fb_line(y) + x0; int n = x1 - x0;
        for (int i = 0; i < n; i++) { u32 v = s[i]; u16 p = (v >> 8 & 0xF800) | (v >> 5 & 0x07E0) | (v >> 3 & 0x001F); line[2 * i] = p >> 8; line[2 * i + 1] = p; }
        spi_xfer(line, 0, 2 * n);
    }
    spi_end();
}
void lcd_dirty(int x0, int y0, int x1, int y1) {
    if (!lcd_ok) return;
    u64 f = irq_save(); dx0 = MIN(dx0, MAX(x0, 0)); dy0 = MIN(dy0, MAX(y0, 0)); dx1 = MAX(dx1, MIN(x1, LCD_W)); dy1 = MAX(dy1, MIN(y1, LCD_H)); irq_restore(f);
    if (lcd_task) task_wake(lcd_task);
}
void lcd_set_madctl(int m);
/* /sys/ZUNZUN/PANTALLA.TXT: "madctl=0xE8" gira la imagen sin recompilar */
static void cfg_load(void) {
    u64 n; char *s = vfs_slurp("/sys/ZUNZUN/PANTALLA.TXT", &n); if (!s) return;
    for (u64 i = 0; i + 7 < n; i++) if (!memcmp(s + i, "madctl=", 7)) {
        const char *p = s + i + 7; int v = 0, b = 10; if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { b = 16; p += 2; }
        for (;; p++) { int d = *p >= '0' && *p <= '9' ? *p - '0' : (*p | 32) >= 'a' && (*p | 32) <= 'f' && b == 16 ? (*p | 32) - 'a' + 10 : -1; if (d < 0) break; v = v * b + d; }
        kprintf("pantalla SPI: madctl=0x%x (PANTALLA.TXT)\n", v); lcd_set_madctl(v & 0xFF); break;
    }
    kfree(s);
}
static void lcd_th(void *a) {
    lcd_task = cur; lcd_reset(); lcd_dirty(0, 0, LCD_W, LCD_H); int tries = 0;
    for (;;) {
        if (tries >= 0) { zdirent_t d; if (!vfs_stat("/sys", &d)) { cfg_load(); tries = -1; } else if (++tries > 500) tries = -1; }
        u64 f = irq_save(); int x0 = dx0, y0 = dy0, x1 = dx1, y1 = dy1; dx0 = dy0 = 1 << 30; dx1 = dy1 = -1; irq_restore(f);
        if (x1 > x0 && y1 > y0) push(x0, y0, x1, y1);
        task_block(40);
    }
}
void lcd_set_madctl(int m) { madctl = m; if (lcd_ok) { u8 v = m; cmd(0x36, &v, 1); lcd_dirty(0, 0, LCD_W, LCD_H); } }
void lcd_off(void) { if (lcd_ok) cmd(0x28, 0, 0); }   /* apagar la imagen */
void lcd_init(void) {
    spi_ok = spi_probe();
    if (!spi_ok) { kprintf("pantalla SPI: no hay controlador SPI0: solo el framebuffer\n"); return; }
    extern int touch_probe(void);
    if (!touch_probe()) { kprintf("pantalla SPI: no se encuentra la pantalla de 3,5\" (su tactil no responde): solo el framebuffer\n"); return; }
    if (scr_w != LCD_W || scr_h != LCD_H) { kprintf("pantalla SPI: el escritorio mide %ux%u, no 480x320\n", scr_w, scr_h); return; }
    gpio_func(LCD_PIN_DC, 1); gpio_func(LCD_PIN_RST, 1);
    line = kmalloc(LCD_W * 2); lcd_ok = 1;
    kprintf("pantalla SPI: ILI9486 480x320 en SPI0 CE%d, DC GPIO%d, RST GPIO%d, %u MHz\n", LCD_CS, LCD_PIN_DC, LCD_PIN_RST, 250 / LCD_SPI_DIV);
    kthread("lcd", lcd_th, 0);
}
