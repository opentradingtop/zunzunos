/* Codificador MP3 propio: MPEG-1 Layer III de velocidad constante (32, 44,1 o 48 kHz; mono o estereo).
   Camino de cada granulo de 576 muestras por canal:
     banco de filtros polifasico (32 subbandas) -> MDCT de 18 lineas por subbanda (bloques largos, ventana seno)
     -> reduccion de alias -> cuantizacion con un paso comun (global_gain) buscado por biseccion para que quepa
     en los bits del cuadro -> Huffman con las tablas del estandar (tres regiones + cuadruplas).
   No usa modelo psicoacustico ni factores de escala (el ruido queda repartido por igual hasta el corte de agudos),
   ni deposito de bits entre cuadros: cada cuadro lleva sus propios datos. Es sencillo y rapido y suena bien a partir
   de 128 kbps en estereo. Las tablas de la norma estan en mp3tab.h. */
#include "z.h"
#include "mp3tab.h"

#if defined(__x86_64__)
static inline float fsq(float x) { float r; __asm__("sqrtss %1, %0" : "=x"(r) : "x"(x)); return r; }
#else
#define fsq(x) ((float)z_sqrt(x))
#endif

static const u8 SFW[3][22] = {   /* anchos de las bandas de factores de escala largas: 44,1 / 48 / 32 kHz */
    { 4,4,4,4,4,4,6,6,8,8,10,12,16,20,24,28,34,42,50,54,76,158 },
    { 4,4,4,4,4,4,6,6,6,8,10,12,16,18,22,28,34,40,46,54,54,192 },
    { 4,4,4,4,4,4,6,6,8,10,12,16,20,24,30,38,46,56,68,84,102,26 } };
static const u8 SUBDV[23][2] = { {0,0},{0,0},{0,0},{0,0},{0,0},{0,1},{1,1},{1,1},{1,2},{2,2},{2,3},{2,3},{3,4},{3,4},{3,4},{4,5},{4,5},{4,6},{5,6},{5,6},{5,7},{6,7},{6,7} };
static const short KBPS[15] = { 0,32,40,48,56,64,80,96,112,128,160,192,224,256,320 };

struct zmp3 { int rate, ch, bri, sfi, cut, n; long rem; int sfb[23]; float X[2][512], prev[2][32][18]; short in[2][1152]; };
typedef struct { int p23, bv, gg, tab[3], r0, r1, c1t; } gi_t;

static float Cw[512], Mc[32][64], Tm[18][36], CS[8], CA[8]; static int ready;
#define MSCALE (2.0f / 9.0f)   /* ajusta la escala de la MDCT a la de la IMDCT del estandar (prueba en tools/pruebas/mp3) */
static void tables(void) {
    if (ready) return; ready = 1; const double PI = 3.14159265358979323846;
    for (int n = 0; n < 512; n++) Cw[n] = ENWIN[n] / (65536.0 * 64) * ((n >> 6) & 1 ? -1 : 1);
    for (int k = 0; k < 32; k++) for (int i = 0; i < 64; i++) Mc[k][i] = z_cos((2 * k + 1) * (i - 16) * PI / 64);
    for (int m = 0; m < 18; m++) for (int i = 0; i < 36; i++) Tm[m][i] = z_sin(PI / 36 * (i + 0.5)) * z_cos(PI / 72 * (2 * i + 19) * (2 * m + 1)) * MSCALE;
    static const double c[8] = { -0.6, -0.535, -0.33, -0.185, -0.095, -0.041, -0.0142, -0.0037 };
    for (int i = 0; i < 8; i++) { double s = z_sqrt(1 + c[i] * c[i]); CS[i] = 1 / s; CA[i] = c[i] / s; } }

/* rate 32000/44100/48000, ch 1 o 2, kbps de la lista del estandar (32..320); 0 si no se puede */
zmp3 *mp3e_new(int rate, int ch, int kbps) {
    int sfi = rate == 44100 ? 0 : rate == 48000 ? 1 : rate == 32000 ? 2 : -1, bri = 0;
    for (int i = 1; i < 15; i++) if (KBPS[i] == kbps) bri = i;
    if (sfi < 0 || !bri || ch < 1 || ch > 2) return 0;
    zmp3 *e = calloc(1, sizeof *e); if (!e) return 0; tables(); e->rate = rate; e->ch = ch; e->bri = bri; e->sfi = sfi;
    for (int i = 0; i < 22; i++) e->sfb[i + 1] = e->sfb[i] + SFW[sfi][i];
    int per = kbps / ch, hz = per >= 96 ? 19500 : per >= 64 ? 16500 : per >= 48 ? 14000 : per >= 32 ? 11000 : 8000;
    if (hz > rate / 2) hz = rate / 2; e->cut = hz * 1152 / rate; return e; }
void mp3e_free(zmp3 *e) { free(e); }

/* ---- escritura de bits ---- */
typedef struct { u8 *p; long n; } bw_t;
static void put(bw_t *b, u32 v, int n) { while (n > 0) { int k = n > 8 ? 8 : n; n -= k; u32 bits = (v >> n) & ((1u << k) - 1); for (int i = k - 1; i >= 0; i--) { if (bits >> i & 1) b->p[b->n >> 3] |= 0x80 >> (b->n & 7); b->n++; } } }

/* ---- filtros: un granulo de un canal -> xr[576] (subbanda*18 + linea) ---- */
static void analyze(zmp3 *e, int c, const short *s, float *xr) {
    float sb[32][18], *X = e->X[c];
    for (int t = 0; t < 18; t++) {
        memmove(X + 32, X, 480 * sizeof(float)); for (int j = 0; j < 32; j++) X[31 - j] = s[t * 32 + j] * (1.0f / 32768);
        float Y[64]; for (int i = 0; i < 64; i++) { float y = 0; for (int j = 0; j < 8; j++) y += Cw[i + 64 * j] * X[i + 64 * j]; Y[i] = y; }
        for (int k = 0; k < 32; k++) { float v = 0; for (int i = 0; i < 64; i++) v += Mc[k][i] * Y[i]; sb[k][t] = (k & t & 1) ? -v : v; } }
    for (int k = 0; k < 32; k++) { float z[36]; memcpy(z, e->prev[c][k], 18 * sizeof(float)); memcpy(z + 18, sb[k], 18 * sizeof(float)); memcpy(e->prev[c][k], sb[k], 18 * sizeof(float));
        for (int m = 0; m < 18; m++) { float v = 0; for (int i = 0; i < 36; i++) v += Tm[m][i] * z[i]; xr[k * 18 + m] = v; } }
    for (int k = 1; k < 32; k++) for (int i = 0; i < 8; i++) { float a = xr[k * 18 - 1 - i], b = xr[k * 18 + i]; xr[k * 18 - 1 - i] = a * CS[i] + b * CA[i]; xr[k * 18 + i] = b * CS[i] - a * CA[i]; } }

/* ---- Huffman ---- */
static int tab_bits(int t, const int *ix, int a, int b) {
    int lin = HT_LIN[t], n = 0; const u8 *L = HLEN[t];
    for (int i = a; i < b; i += 2) { int x = ix[i], y = ix[i + 1]; if (lin) { int xx = x > 15 ? 15 : x, yy = y > 15 ? 15 : y; n += L[xx * 16 + yy] + (xx == 15 ? lin : 0) + (yy == 15 ? lin : 0); } else n += L[x * 16 + y]; }
    return n; }
static int best_tab(const int *ix, int a, int b, int *bits) {
    int mx = 0; for (int i = a; i < b; i++) if (ix[i] > mx) mx = ix[i];
    *bits = 0; if (!mx) return 0; int bt = -1, bb = 1 << 30;
    if (mx <= 15) { for (int t = 1; t < 16; t++) if (HLEN[t] && HT_X[t] > mx) { int n = tab_bits(t, ix, a, b); if (n < bb) { bb = n; bt = t; } } }
    else for (int f = 16; f < 32; f += 8) for (int t = f; t < f + 8; t++) if (15 + (1 << HT_LIN[t]) - 1 >= mx) { int n = tab_bits(t, ix, a, b); if (n < bb) { bb = n; bt = t; } break; }
    *bits = bb; return bt; }
/* elige regiones y tablas; devuelve los bits de la parte 3 (sin contar signos de las regiones: van aparte) */
static int count(const int *ix, gi_t *g, const int *sfb, int *c1end) {
    int i = 576; while (i > 1 && !ix[i - 1] && !ix[i - 2]) i -= 2;
    int c1 = i; while (c1 > 3 && ix[c1 - 1] <= 1 && ix[c1 - 2] <= 1 && ix[c1 - 3] <= 1 && ix[c1 - 4] <= 1) c1 -= 4;
    *c1end = i; g->bv = c1 / 2; int bits = 0, ba = 0, bb = 0;
    for (int k = c1; k < i; k += 4) { int q = ix[k] * 8 + ix[k + 1] * 4 + ix[k + 2] * 2 + ix[k + 3]; ba += QA_L[q]; bb += 4; }
    g->c1t = bb < ba; bits += bb < ba ? bb : ba;
    for (int k = 0; k < i; k++) bits += ix[k] != 0;   /* signos */
    g->tab[0] = g->tab[1] = g->tab[2] = 0; g->r0 = g->r1 = 0;
    if (c1) { int a = 0; while (a < 22 && sfb[a] < c1) a++;
        int r0 = SUBDV[a][0]; while (r0 && sfb[r0 + 1] > c1) r0--;
        int r1 = SUBDV[a][1]; while (r1 && sfb[r0 + r1 + 2] > c1) r1--;
        int s1 = sfb[r0 + 1] < c1 ? sfb[r0 + 1] : c1, s2 = sfb[r0 + r1 + 2] < c1 ? sfb[r0 + r1 + 2] : c1, n;
        g->r0 = r0; g->r1 = r1; g->tab[0] = best_tab(ix, 0, s1, &n); bits += n; g->tab[1] = best_tab(ix, s1, s2, &n); bits += n; g->tab[2] = best_tab(ix, s2, c1, &n); bits += n; }
    return bits; }
static void emit(bw_t *b, const int *ix, const float *xr, const gi_t *g, const int *sfb, int c1end) {
    int c1 = g->bv * 2, s1 = sfb[g->r0 + 1] < c1 ? sfb[g->r0 + 1] : c1, s2 = sfb[g->r0 + g->r1 + 2] < c1 ? sfb[g->r0 + g->r1 + 2] : c1;
    for (int i = 0; i < c1; i += 2) {
        int t = g->tab[i < s1 ? 0 : i < s2 ? 1 : 2]; if (!t) continue;   /* tabla 0: todo ceros, no se escribe nada */
        int x = ix[i], y = ix[i + 1], lin = HT_LIN[t];
        if (lin) { int xx = x > 15 ? 15 : x, yy = y > 15 ? 15 : y; put(b, HCODE[t][xx * 16 + yy], HLEN[t][xx * 16 + yy]);
            if (xx == 15) put(b, x - 15, lin); if (x) put(b, xr[i] < 0, 1); if (yy == 15) put(b, y - 15, lin); if (y) put(b, xr[i + 1] < 0, 1); }
        else { put(b, HCODE[t][x * 16 + y], HLEN[t][x * 16 + y]); if (x) put(b, xr[i] < 0, 1); if (y) put(b, xr[i + 1] < 0, 1); } }
    for (int k = c1; k < c1end; k += 4) { int q = ix[k] * 8 + ix[k + 1] * 4 + ix[k + 2] * 2 + ix[k + 3];
        if (g->c1t) put(b, 15 - q, 4); else put(b, QA_C[q], QA_L[q]);
        for (int j = 0; j < 4; j++) if (ix[k + j]) put(b, xr[k + j] < 0, 1); } }

/* cuantiza con el paso de global_gain gg; 0 si algun valor no cabe */
static int quant(const float *a34, int gg, int *ix) {
    float st = (float)z_pow(2.0, -(gg - 210) * 3.0 / 16);
    for (int i = 0; i < 576; i++) { float v = a34[i] * st + 0.4054f; if (v > 8206) return 0; ix[i] = (int)v; }
    return 1; }
static void granule(zmp3 *e, const float *xr, int budget, gi_t *g, int *ix, int *c1end) {
    float a34[576]; for (int i = 0; i < 576; i++) { float a = xr[i] < 0 ? -xr[i] : xr[i]; a34[i] = i < e->cut ? fsq(a * fsq(a)) : 0; }
    if (budget > 4095) budget = 4095;
    int lo = 0, hi = 255; gi_t t;   /* menor gg (mas fino) que quepa; con gg=255 todo es cero */
    while (lo < hi) { int m = (lo + hi) / 2; if (quant(a34, m, ix) && count(ix, &t, e->sfb, c1end) <= budget) hi = m; else lo = m + 1; }
    quant(a34, lo, ix); g->p23 = count(ix, g, e->sfb, c1end); g->gg = lo; }

static int frame(zmp3 *e, u8 *out) {
    int br = KBPS[e->bri] * 1000, fb = 144 * br / e->rate, pad = 0; e->rem += 144LL * br % e->rate; if (e->rem >= e->rate) { e->rem -= e->rate; pad = 1; } fb += pad;
    int side = e->ch == 1 ? 17 : 32; static float xr[2][2][576]; static int ix[2][2][576]; gi_t g[2][2]; int c1e[2][2];
    for (int gr = 0; gr < 2; gr++) for (int c = 0; c < e->ch; c++) analyze(e, c, e->in[c] + gr * 576, xr[gr][c]);
    int left = fb * 8 - 32 - side * 8, parts = 2 * e->ch;
    for (int gr = 0; gr < 2; gr++) for (int c = 0; c < e->ch; c++) { granule(e, xr[gr][c], left / parts, &g[gr][c], ix[gr][c], &c1e[gr][c]); left -= g[gr][c].p23; parts--; }
    memset(out, 0, fb); bw_t b = { out, 0 };
    put(&b, 0xFFFB, 16); put(&b, e->bri, 4); put(&b, e->sfi, 2); put(&b, pad, 1); put(&b, 0, 1); put(&b, e->ch == 1 ? 3 : 0, 2); put(&b, 0, 2); put(&b, 0, 1); put(&b, 1, 1); put(&b, 0, 2);
    put(&b, 0, 9); put(&b, 0, e->ch == 1 ? 5 : 3); put(&b, 0, 4 * e->ch);
    for (int gr = 0; gr < 2; gr++) for (int c = 0; c < e->ch; c++) { gi_t *q = &g[gr][c];
        put(&b, q->p23, 12); put(&b, q->bv, 9); put(&b, q->gg, 8); put(&b, 0, 4); put(&b, 0, 1);
        for (int r = 0; r < 3; r++) put(&b, q->tab[r], 5);
        put(&b, q->r0, 4); put(&b, q->r1, 3); put(&b, 0, 1); put(&b, 0, 1); put(&b, q->c1t, 1); }
    for (int gr = 0; gr < 2; gr++) for (int c = 0; c < e->ch; c++) emit(&b, ix[gr][c], xr[gr][c], &g[gr][c], e->sfb, c1e[gr][c]);
    return fb; }

/* muestras de 16 bits entrelazadas (frames = muestras por canal). out necesita sitio para 1441 bytes por cada 1152 frames
   (mas uno). Devuelve los bytes escritos en out. */
long mp3e_put(zmp3 *e, const short *pcm, long frames, u8 *out) {
    long w = 0;
    for (long i = 0; i < frames; i++) { for (int c = 0; c < e->ch; c++) e->in[c][e->n] = pcm[i * e->ch + c]; if (++e->n == 1152) { w += frame(e, out + w); e->n = 0; } }
    return w; }
/* vacia lo que queda (con silencio detras para que salga el final del sonido); out: 3 cuadros */
long mp3e_end(zmp3 *e, u8 *out) {
    long w = 0; int need = 1152 - e->n + 1152;
    while (need-- > 0) { for (int c = 0; c < e->ch; c++) e->in[c][e->n] = 0; if (++e->n == 1152) { w += frame(e, out + w); e->n = 0; } }
    return w; }
