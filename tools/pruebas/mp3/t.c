/* Prueba en el Mac del codificador MP3: codifica senales de prueba con zmp3.c, las decodifica con el decodificador propio
   (zmp3d.c) y mide retardo, ganancia y relacion senal/ruido. Escribe los .mp3 en DIR para que correr.sh los pase
   tambien por macOS. */
#include <stdio.h>
#include "z.h"
zmp3 *mp3e_new(int, int, int); long mp3e_put(zmp3 *, const short *, long, u8 *); long mp3e_end(zmp3 *, u8 *); void mp3e_free(zmp3 *);
zmp3d *mp3d_new(void); int mp3d_frame(zmp3d *d, const u8 *b, long n, short *pcm, int *used, int *hz, int *ch);
static int fails;
static void caso(const char *dir, const char *nom, int rate, int ch, int kb, double minsnr) {
    long fr = rate * 3; short *x = malloc(fr * ch * 2); unsigned r = 1;
    for (long i = 0; i < fr; i++) { double t = (double)i / rate, env = 0.5 + 0.5 * sin(t * 3); r = r * 1103515245 + 12345;
        double l = 0.25 * sin(2 * M_PI * 220 * t) + 0.15 * sin(2 * M_PI * 660 * t) * env + 0.05 * sin(2 * M_PI * 3000 * t) + 0.004 * ((r >> 16 & 1023) / 512.0 - 1);
        double rr = 0.25 * sin(2 * M_PI * 330 * t) + 0.1 * sin(2 * M_PI * 1500 * t) * env + ((i / (rate / 4)) & 1 ? 0.2 * sin(2 * M_PI * 5000 * t) : 0);
        x[i * ch] = l * 32767; if (ch == 2) x[i * ch + 1] = rr * 32767; }
    zmp3 *e = mp3e_new(rate, ch, kb); u8 *o = malloc(fr / 1152 * 1500 + 6000); long w = 0;
    for (long i = 0; i < fr; i += 3000) w += mp3e_put(e, x + i * ch, fr - i < 3000 ? fr - i : 3000, o + w);
    w += mp3e_end(e, o + w); mp3e_free(e); char p[300]; snprintf(p, 300, "%s/%s.mp3", dir, nom); FILE *g = fopen(p, "wb"); fwrite(o, 1, w, g); fclose(g);
    zmp3d *d = mp3d_new(); short *y = calloc(fr + 8000, ch * 2), pc[2304]; long ny = 0, q = 0, nf = 0, badf = 0;
    while (q < w) { int used, hz, c, s = mp3d_frame(d, o + q, w - q, pc, &used, &hz, &c); if (!used) break; int fb = 144000 * kb / rate; q += used; nf++; if (hz != rate || c != ch || (used != fb && used != fb + 1)) badf++; if (ny + s <= fr + 8000) memcpy(y + ny * ch, pc, s * ch * 2), ny += s; }
    free(d);
    int bd = 0; double best = 0;
    for (int dl = 0; dl < 2400; dl++) { double c = 0, a = 0, b = 0; for (long i = 0; i + dl < ny && i < fr; i += 5) for (int k = 0; k < ch; k++) { double u = x[i * ch + k], v = y[(i + dl) * ch + k]; c += u * v; a += u * u; b += v * v; } if (c / sqrt(a * b + 1) > best) best = c / sqrt(a * b + 1), bd = dl; }
    double c = 0, a = 0, er = 0; long st = rate / 10;
    for (long i = st; i + bd < ny && i < fr - st; i++) for (int k = 0; k < ch; k++) { double u = x[i * ch + k], v = y[(i + bd) * ch + k]; c += u * v; a += u * u; er += (v - u) * (v - u); }
    double snr = 10 * log10(a / er), gn = c / a, kbps = w * 8.0 * rate / fr / 1000;
    int ok = q == w && !badf && bd == 1057 && fabs(gn - 1) < 0.01 && snr >= minsnr && fabs(kbps - kb) < kb * 0.05;
    printf("%-10s %5d Hz %d can %3d kbps: %ld bytes (%.1f kbps), %ld cuadros, retardo %d, ganancia %.4f, snr %.1f dB  %s\n", nom, rate, ch, kb, w, kbps, nf, bd, gn, snr, ok ? "ok" : "FALLO");
    if (!ok) fails++; free(x); free(y); free(o); }
int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    caso(dir, "s48-128", 48000, 2, 128, 33); caso(dir, "s48-320", 48000, 2, 320, 38); caso(dir, "s48-64m", 48000, 1, 64, 30);
    caso(dir, "s44-128", 44100, 2, 128, 33); caso(dir, "s44-96m", 44100, 1, 96, 33); caso(dir, "s32-96", 32000, 2, 96, 30);
    caso(dir, "s48-32m", 48000, 1, 32, 15);
    return fails != 0; }
