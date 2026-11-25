/* Emision de prueba para la Radio: 30 s de tono estereo (440 Hz a la izquierda, 660 Hz a la derecha), 44,1 kHz,
   128 kbps, codificado con el MP3 propio (user/lib/zmp3.c). Lo emite tools/fake_radio.py.
   Uso: mktono SALIDA.mp3 */
#include <stdio.h>
#include "z.h"
zmp3 *mp3e_new(int, int, int); long mp3e_put(zmp3 *, const short *, long, u8 *); long mp3e_end(zmp3 *, u8 *); void mp3e_free(zmp3 *);
int main(int argc, char **argv) {
    int rate = 44100; long fr = rate * 30; short *x = malloc(fr * 4);
    for (long i = 0; i < fr; i++) { double t = (double)i / rate; x[i * 2] = 0.3 * sin(2 * M_PI * 440 * t) * 32767; x[i * 2 + 1] = 0.3 * sin(2 * M_PI * 660 * t) * 32767; }
    zmp3 *e = mp3e_new(rate, 2, 128); u8 *o = malloc(fr / 1152 * 1500 + 6000); long w = 0;
    for (long i = 0; i < fr; i += 4096) w += mp3e_put(e, x + i * 2, fr - i < 4096 ? fr - i : 4096, o + w);
    w += mp3e_end(e, o + w); mp3e_free(e); FILE *g = fopen(argv[1], "wb"); if (!g) return 1; fwrite(o, 1, w, g); fclose(g);
    printf("%s: %ld bytes\n", argv[1], w); return 0; }
