/* Compara el decodificador propio (zmp3d.c) con un WAV de referencia decodificado por macOS: busca el desfase (macOS
   quita el retardo del codificador si el archivo lo indica) y mide la relacion senal/ruido. Uso: compara X.mp3 REF.wav */
#include <stdio.h>
#include "z.h"
zmp3d *mp3d_new(void); int mp3d_frame(zmp3d *d, const u8 *b, long n, short *pcm, int *used, int *hz, int *ch);
static u8 *rd(const char *p, long *n) { FILE *f = fopen(p, "rb"); if (!f) return 0; fseek(f, 0, 2); *n = ftell(f); fseek(f, 0, 0); u8 *b = malloc(*n); fread(b, 1, *n, f); fclose(f); return b; }
int main(int c, char **v) { long n, nw; u8 *b = rd(v[1], &n), *w = rd(v[2], &nw); if (!b || !w) return 2;
  long o = 12; short *W = 0; long nW = 0; int wch = 0;
  while (o + 8 <= nw) { u32 l = *(u32 *)(w + o + 4); if (!memcmp(w + o, "fmt ", 4)) wch = *(u16 *)(w + o + 10); if (!memcmp(w + o, "data", 4)) { W = (short *)(w + o + 8); nW = l / 2; break; } o += 8 + l; }
  short *B = malloc(n * 40 * 2 + 100000), pc[2304]; long nb = 0, q = 0; int ch = 0; zmp3d *d = mp3d_new();
  while (q < n) { int u, hz, cc, s = mp3d_frame(d, b + q, n - q, pc, &u, &hz, &cc); if (!u) break; q += u; if (s) { ch = cc; memcpy(B + nb, pc, s * cc * 2); nb += s * cc; } }
  if (ch != wch) { printf("%s: canales %d/%d DIFIERE\n", v[1], ch, wch); return 1; }
  long fa = nb / ch, fw = nW / ch; int best = 0; double bs = -1e300;
  for (int dl = -3000; dl <= 3000; dl++) { double e = 0, p = 0; for (long i = 3000; i < fa - 3000 && i < 200000; i += 7) { long j = i + dl; if (j < 0 || j >= fw) continue; for (int k = 0; k < ch; k++) { double x = B[i * ch + k], y = W[j * ch + k]; e += (x - y) * (x - y); p += x * x; } } double s = p / (e + 1e-9); if (s > bs) { bs = s; best = dl; } }
  double e = 0, p = 0; int mx = 0; for (long i = 0; i < fa; i++) { long j = i + best; if (j < 0 || j >= fw) continue; for (int k = 0; k < ch; k++) { int x = B[i * ch + k], y = W[j * ch + k]; e += (double)(x - y) * (x - y); p += (double)x * x; if (abs(x - y) > mx) mx = abs(x - y); } }
  double snr = p ? 10 * log10(p / (e + 1e-9)) : 0; printf("%-40.40s desfase %5d  snr %6.1f dB  dif max %5d  %s\n", strrchr(v[1], '/') ? strrchr(v[1], '/') + 1 : v[1], best, snr, mx, snr >= 60 ? "ok" : "DIFIERE"); return snr < 60; }
