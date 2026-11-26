/* ZunzunOS - tactil resistivo XPT2046 (compatible ADS7846) en SPI0 CE1, con la linea de toque en GPIO17.
   Un toque es un clic del raton: se manda al gestor de ventanas como posicion absoluta con el boton 1.
   Calibracion de 3 puntos: la primera vez (o con la orden de serie "!C") se piden tres toques sobre unas cruces;
   con ellos se calcula una transformacion afin (sirve para cualquier giro o espejo) y se guarda en
   /sys/ZUNZUN/TACTIL.TXT. Para probarla sin la pantalla, "!R x y 1|0" del puerto serie entra como lectura cruda. */
#include "../kernel.h"
#include "pantalla.h"

extern int spi_present(void); extern void spi_begin(int cs, int div); extern void spi_end(void); extern void spi_xfer(const u8 *out, u8 *in, int n);
extern void wm_calib(int step, int x, int y);   /* wm.c: dibuja la cruz (step 0 = nada) */
/* sx = (a*rx + b*ry + c) >> 16, sy = (d*rx + e*ry + f) >> 16. Valores de partida: los de la tabla de calibracion
   de Waveshare para su pantalla (A) en apaisado (crudo X 3932..300 -> 0..479, crudo Y 294..3801 -> 0..319) */
static i64 K[6]; static int cal_step, have_hw, sys_cal;
static int cal_rx[3], cal_ry[3]; static const int CX[3] = {48, 432, 240}, CY[3] = {32, 160, 288};
static void cal_default(void) {
    K[0] = (i64)-479 * 65536 / (3932 - 300); K[1] = 0; K[2] = -K[0] * 3932;
    K[3] = 0; K[4] = (i64)319 * 65536 / (3801 - 294); K[5] = -K[4] * 294;
}
static void map(int rx, int ry, int *x, int *y) {
    i64 sx = (K[0] * rx + K[1] * ry + K[2]) >> 16, sy = (K[3] * rx + K[4] * ry + K[5]) >> 16;
    *x = MAX(0, MIN((int)sx, (int)scr_w - 1)); *y = MAX(0, MIN((int)sy, (int)scr_h - 1));
}
/* resuelve la afin con los 3 puntos (regla de Cramer, en coma fija 16.16) */
static int cal_solve(void) {
    i64 x0 = cal_rx[0], x1 = cal_rx[1], x2 = cal_rx[2], y0 = cal_ry[0], y1 = cal_ry[1], y2 = cal_ry[2];
    i64 det = x0 * (y1 - y2) - x1 * (y0 - y2) + x2 * (y0 - y1);
    if (det == 0 || (det < 0 ? -det : det) < 1000) return -1;
    for (int k = 0; k < 2; k++) {
        i64 s0 = k ? CY[0] : CX[0], s1 = k ? CY[1] : CX[1], s2 = k ? CY[2] : CX[2];
        i64 a = s0 * (y1 - y2) - s1 * (y0 - y2) + s2 * (y0 - y1);
        i64 b = x0 * (s1 - s2) - x1 * (s0 - s2) + x2 * (s0 - s1);
        i64 c = x0 * (y1 * s2 - y2 * s1) - x1 * (y0 * s2 - y2 * s0) + x2 * (y0 * s1 - y1 * s0);
        K[k * 3] = a * 65536 / det; K[k * 3 + 1] = b * 65536 / det; K[k * 3 + 2] = c * 65536 / det;
    }
    return 0;
}
static void cal_save(void) {
    char b[160]; int n = snprintf(b, sizeof b, "# calibracion del tactil (ZunzunOS)\n%lld %lld %lld %lld %lld %lld\n", K[0], K[1], K[2], K[3], K[4], K[5]);
    file_t *f; if (!vfs_open("/sys/ZUNZUN/TACTIL.TXT", O_WRITE | O_CREATE | O_TRUNC, &f)) { vfs_write(f, b, n); vfs_close(f); kprintf("tactil: calibracion guardada en /sys/ZUNZUN/TACTIL.TXT\n"); }
}
static int cal_load(void) {
    u64 n; char *s = vfs_slurp("/sys/ZUNZUN/TACTIL.TXT", &n); if (!s) return 0;
    i64 v[6]; int k = 0; char *p = s, *e = s + n;
    while (p < e && k < 6) { if (*p == '#') { while (p < e && *p != '\n') p++; continue; } if (*p == '-' || (*p >= '0' && *p <= '9')) { int neg = *p == '-'; if (neg) p++; i64 x = 0; while (p < e && *p >= '0' && *p <= '9') x = x * 10 + *p++ - '0'; v[k++] = neg ? -x : x; } else p++; }
    kfree(s); if (k < 6) return 0;
    for (int i = 0; i < 6; i++) K[i] = v[i];
    kprintf("tactil: calibracion leida de /sys\n"); return 1;
}
void touch_calibrate(void) { cal_step = 1; wm_calib(1, CX[0], CY[0]); kprintf("tactil: calibrando (toca el centro de las cruces)\n"); }

/* un toque crudo (del XPT2046 o del puerto serie) */
static int down, last_x, last_y;
void touch_raw(int rx, int ry, int press) {
    if (cal_step) {
        if (!press) { if (down) { down = 0; if (cal_step > 3) { cal_step = 0; } } return; }
        if (down) return;   /* cada punto cuenta al apoyar el dedo */
        down = 1; cal_rx[cal_step - 1] = rx; cal_ry[cal_step - 1] = ry; kprintf("tactil: punto %d crudo %d,%d\n", cal_step, rx, ry);
        if (cal_step < 3) { cal_step++; wm_calib(cal_step, CX[cal_step - 1], CY[cal_step - 1]); return; }
        if (cal_solve()) { kprintf("tactil: los puntos no sirven, otra vez\n"); cal_step = 1; wm_calib(1, CX[0], CY[0]); return; }
        cal_step = 4; wm_calib(0, 0, 0); kprintf("tactil: calibrado\n"); cal_save(); return;
    }
    int x, y; map(rx, ry, &x, &y);
    if (press) { down = 1; last_x = x; last_y = y; input_abs(x, y, scr_w - 1, scr_h - 1, 1, 0); }
    else if (down) { down = 0; input_abs(last_x, last_y, scr_w - 1, scr_h - 1, 0, 0); }
}

/* --- XPT2046: 12 bits, diferencial; se promedian varias lecturas descartando las extremas --- */
static int rd(u8 ch) { u8 o[3] = {ch, 0, 0}, i[3]; spi_xfer(o, i, 3); return ((i[1] << 8) | i[2]) >> 3; }
static int sample(int *rx, int *ry) {
    int xs[7], ys[7], z1, z2;
    spi_begin(TP_CS, TP_SPI_DIV);
    z1 = rd(0xB1); z2 = rd(0xC1);
    for (int i = 0; i < 7; i++) { xs[i] = rd(0xD1); ys[i] = rd(0x91); }
    rd(0x90);   /* deja la linea de toque activa (PD = 00) */
    spi_end();
    for (int i = 0; i < 7; i++) for (int j = i + 1; j < 7; j++) { if (xs[j] < xs[i]) { int t = xs[i]; xs[i] = xs[j]; xs[j] = t; } if (ys[j] < ys[i]) { int t = ys[i]; ys[i] = ys[j]; ys[j] = t; } }
    *rx = (xs[2] + xs[3] + xs[4]) / 3; *ry = (ys[2] + ys[3] + ys[4]) / 3;
    (void)z2;
    return z1 > 8 && xs[5] - xs[1] < 150 && ys[5] - ys[1] < 150;   /* hay contacto (TP_IRQ ya lo dice) y la lectura es estable */
}
static void touch_th(void *a) {
    int up = 0;
    for (;;) {
        if (!sys_cal) { zdirent_t d; if (!vfs_stat("/sys", &d)) { sys_cal = 1; if (!cal_load() && have_hw) touch_calibrate(); } }
        if (!gpio_get(TP_PIN_IRQ)) { int rx, ry; if (sample(&rx, &ry)) { touch_raw(rx, ry, 1); up = 0; } msleep(15); continue; }
        if (down && ++up >= 2) touch_raw(0, 0, 0);
        msleep(15);
    }
}
static void sys_th(void *a) {   /* sin tactil (QEMU): solo leer la calibracion cuando aparezca /sys */
    for (int i = 0; i < 200 && !sys_cal; i++) { zdirent_t d; if (!vfs_stat("/sys", &d)) { sys_cal = 1; cal_load(); } msleep(100); }
}
/* hay XPT2046 si su sensor de temperatura (canal 0, referencia interna) da una lectura plausible; sin chip
   (QEMU, o la Pi sin la pantalla) MISO se queda a 0 */
int touch_probe(void) {
    if (!spi_present()) return 0;
    gpio_func(TP_PIN_IRQ, 0); gpio_pull(TP_PIN_IRQ, 2);
    int v[4]; spi_begin(TP_CS, TP_SPI_DIV); rd(0x87); udelay(200); for (int i = 0; i < 4; i++) v[i] = rd(0x87); rd(0x90); spi_end();
    int ok = 0; for (int i = 0; i < 4; i++) if (v[i] > 20 && v[i] < 4075) ok++;
    kprintf("tactil: sonda XPT2046 (temperatura) %d %d %d %d\n", v[0], v[1], v[2], v[3]);
    return ok >= 3;
}
void touch_init(void) {
    cal_default();
    if (!touch_probe()) { kprintf("tactil: no hay XPT2046\n"); kthread("tactil", sys_th, 0); return; }
    have_hw = 1;
    kprintf("tactil: XPT2046 en SPI0 CE%d, linea de toque GPIO%d\n", TP_CS, TP_PIN_IRQ);
    kthread("tactil", touch_th, 0);
}
