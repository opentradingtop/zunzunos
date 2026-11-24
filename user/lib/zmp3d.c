/* Decodificador MP3 propio de ZunzunOS: MPEG-1, MPEG-2 y MPEG-2.5, capas I, II y III (la III es la de casi todos los
   .mp3; la II, la de los .mp2 de radio y television). Escrito a partir de las normas ISO 11172-3 e ISO 13818-3; las
   tablas de Huffman y la ventana de sintesis son las de mp3tab.h, compartidas con el codificador (zmp3.c).
   Capas I y II: asignacion de bits -> factores de escala -> muestras de cada subbanda -> banco de sintesis.
   Capa III, camino de cada cuadro:
     cabecera -> informacion lateral -> deposito de bits (main_data_begin) -> por granulo y canal: factores de escala
     -> Huffman (pares y cuadruplas) -> recuantizacion |x|^(4/3) * 2^(g/4) -> estereo M/S e intensidad
     -> reduccion de alias -> IMDCT (36 lineas o 3 x 12) con solape -> inversion de frecuencias
     -> banco de filtros de sintesis de 32 subbandas.
   La prueba en el Mac (tools/pruebas/mp3) lo compara con el decodificador de macOS y con flujos generados al azar. */
#include "z.h"
#include "mp3tab.h"

/* bordes de las bandas de factores de escala (largas: 22 bandas, cortas: 13) por frecuencia:
   44,1 48 32 | 22,05 24 16 | 11,025 12 8 kHz */
static const short SFL[9][23] = {
    { 0,4,8,12,16,20,24,30,36,44,52,62,74,90,110,134,162,196,238,288,342,418,576 },
    { 0,4,8,12,16,20,24,30,36,42,50,60,72,88,106,128,156,190,230,276,330,384,576 },
    { 0,4,8,12,16,20,24,30,36,44,54,66,82,102,126,156,194,240,296,364,448,550,576 },
    { 0,6,12,18,24,30,36,44,54,66,80,96,116,140,168,200,238,284,336,396,464,522,576 },
    { 0,6,12,18,24,30,36,44,54,66,80,96,114,136,162,194,232,278,332,394,464,540,576 },
    { 0,6,12,18,24,30,36,44,54,66,80,96,116,140,168,200,238,284,336,396,464,522,576 },
    { 0,6,12,18,24,30,36,44,54,66,80,96,116,140,168,200,238,284,336,396,464,522,576 },
    { 0,6,12,18,24,30,36,44,54,66,80,96,116,140,168,200,238,284,336,396,464,522,576 },
    { 0,12,24,36,48,60,72,88,108,132,160,192,232,280,336,400,476,566,568,570,572,574,576 } };
static const short SFS[9][14] = {
    { 0,4,8,12,16,22,30,40,52,66,84,106,136,192 },
    { 0,4,8,12,16,22,28,38,50,64,80,100,126,192 },
    { 0,4,8,12,16,22,30,42,58,78,104,138,180,192 },
    { 0,4,8,12,18,24,32,42,56,74,100,132,174,192 },
    { 0,4,8,12,18,26,36,48,62,80,104,136,180,192 },
    { 0,4,8,12,18,26,36,48,62,80,104,134,174,192 },
    { 0,4,8,12,18,26,36,48,62,80,104,134,174,192 },
    { 0,4,8,12,18,26,36,48,62,80,104,134,174,192 },
    { 0,8,16,24,36,52,72,96,124,160,162,164,166,192 } };
static const int RATES[9] = { 44100, 48000, 32000, 22050, 24000, 16000, 11025, 12000, 8000 };
static const short KBPS[5][15] = {
    { 0,32,64,96,128,160,192,224,256,288,320,352,384,416,448 },   /* MPEG-1 capa I */
    { 0,32,48,56,64,80,96,112,128,160,192,224,256,320,384 },      /* MPEG-1 capa II */
    { 0,32,40,48,56,64,80,96,112,128,160,192,224,256,320 },       /* MPEG-1 capa III */
    { 0,32,48,56,64,80,96,112,128,144,160,176,192,224,256 },      /* MPEG-2/2.5 capa I */
    { 0,8,16,24,32,40,48,56,64,80,96,112,128,144,160 } };         /* MPEG-2/2.5 capas II y III */
static const u8 PRETAB[22] = { 0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,2,2,3,3,3,2,0 };
static const u8 SLEN[16][2] = { {0,0},{0,1},{0,2},{0,3},{3,0},{1,1},{1,2},{1,3},{2,1},{2,2},{2,3},{3,1},{3,2},{3,3},{4,2},{4,3} };
/* MPEG-2: cuantos factores de escala lleva cada uno de los 4 grupos (fila de scalefac_compress; largo, corto, mixto) */
static const u8 NRSFB[6][3][4] = {
    { {6,5,5,5},{9,9,9,9},{6,9,9,9} }, { {6,5,7,3},{9,9,12,6},{6,9,12,6} }, { {11,10,0,0},{18,18,0,0},{15,18,0,0} },
    { {7,7,7,0},{12,12,12,0},{6,15,12,0} }, { {6,6,6,3},{12,9,9,6},{6,12,9,6} }, { {8,8,5,0},{15,12,9,0},{6,18,9,0} } };

/* capa II: niveles de cada indice de asignacion (0 = subbanda sin muestras) segun la clase de subbanda (anexo B) */
static const unsigned short L2Q[8][16] = {
    { 0,3,7,15,31,63,127,255,511,1023,2047,4095,8191,16383,32767,65535 }, { 0,3,5,7,9,15,31,63,127,255,511,1023,2047,4095,8191,65535 },
    { 0,3,5,7,9,15,31,65535 }, { 0,3,5,65535 }, { 0,3,5,9,15,31,63,127,255,511,1023,2047,4095,8191,16383,32767 },
    { 0,3,5,9,15,31,63,127 }, { 0,3,5,7,9,15,31,63,127,255,511,1023,2047,4095,8191,16383 }, { 0,3,5,9 } };
static const u8 L2NB[8] = { 4, 4, 3, 2, 4, 3, 4, 2 };
/* clase de cada subbanda en las tablas B.2a, B.2b, B.2c, B.2d y la de MPEG-2 (B.1); 255 = fin */
static const u8 L2T[5][31] = {
    { 0,0,0,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,3,3,3,3,255 },
    { 0,0,0,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,3,3,3,3,3,3,3,255 },
    { 4,4,5,5,5,5,5,5,255 }, { 4,4,5,5,5,5,5,5,5,5,5,5,255 },
    { 6,6,6,6,5,5,5,5,5,5,5,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,255 } };

typedef struct { int ver, lay, prot, br, sri, pad, mode, mext, size, nsmp, nch, lsf; } hdr_t;
typedef struct { int p23, bv, gg, sfc, wsf, bt, mixed, tab[3], sbg[3], r0, r1, pre, ss, c1t; } gr_t;
typedef struct { int n; short st[40], w[40], f[40]; signed char win[40], band[40]; } lay_t;   /* f: linea dentro de la ventana corta */   /* bandas de un granulo en orden */
struct zmp3d { hdr_t h; int locked, nres; u8 res[512]; float ov[2][32][18], syn[2][16][64]; int sp[2]; };

/* ---- tablas que se calculan una vez ---- */
static short HT[16][512][2], QT[32][2]; static signed char HTI[32];
static float SCF[64], P43[8207], IML[36][18], IMS[12][6], WIN[4][36], WS[12], SYC[64][32], SYW[16][32], CS[8], CA[8], ISK[7][2];
static int ready;
static void trie_add(short (*T)[2], int *nn, u32 code, int len, int val) {
    int nd = 0; for (int b = len - 1; b >= 0; b--) { int bit = code >> b & 1;
        if (!b) T[nd][bit] = (short)(-1 - val); else { if (T[nd][bit] <= 0) T[nd][bit] = (short)(*nn)++; nd = T[nd][bit]; } } }
static void init(void) {
    if (ready) return; ready = 1; const double PI = 3.14159265358979323846;
    static const u8 tl[15] = { 1,2,3,5,6,7,8,9,10,11,12,13,15,16,24 };
    for (int i = 0; i < 32; i++) HTI[i] = -1;
    for (int k = 0; k < 15; k++) { int t = tl[k], nn = 1; for (int v = 0; v < 256; v++) if (HLEN[t][v]) trie_add(HT[k], &nn, HCODE[t][v], HLEN[t][v], v); HTI[t] = (signed char)k; }
    for (int t = 17; t < 24; t++) HTI[t] = HTI[16]; for (int t = 25; t < 32; t++) HTI[t] = HTI[24];
    { int nn = 1; for (int v = 0; v < 16; v++) trie_add(QT, &nn, QA_C[v], QA_L[v], v); }
    for (int i = 0; i < 8207; i++) P43[i] = (float)z_pow(i, 4.0 / 3);
    for (int i = 0; i < 63; i++) SCF[i] = (float)z_pow(2, 1 - i / 3.0);   /* factores de escala de las capas I y II */
    for (int i = 0; i < 36; i++) for (int k = 0; k < 18; k++) IML[i][k] = (float)z_cos(PI / 72 * (2 * i + 19) * (2 * k + 1));
    for (int i = 0; i < 12; i++) for (int k = 0; k < 6; k++) IMS[i][k] = (float)z_cos(PI / 24 * (2 * i + 7) * (2 * k + 1));
    for (int i = 0; i < 36; i++) {
        double s36 = z_sin(PI / 36 * (i + 0.5)); WIN[0][i] = (float)s36;
        WIN[1][i] = (float)(i < 18 ? s36 : i < 24 ? 1 : i < 30 ? z_sin(PI / 12 * (i - 18 + 0.5)) : 0);
        WIN[3][i] = (float)(i < 6 ? 0 : i < 12 ? z_sin(PI / 12 * (i - 6 + 0.5)) : i < 18 ? 1 : s36); }
    for (int i = 0; i < 12; i++) WS[i] = (float)z_sin(PI / 12 * (i + 0.5));
    for (int n = 0; n < 64; n++) for (int k = 0; k < 32; k++) SYC[n][k] = (float)z_cos((2 * k + 1) * (n + 16) * PI / 64);
    for (int i = 0; i < 16; i++) for (int j = 0; j < 32; j++) SYW[i][j] = (float)(ENWIN[32 * i + j] / 65536.0 * ((i >> 1) & 1 ? -1 : 1));
    static const double c[8] = { -0.6, -0.535, -0.33, -0.185, -0.095, -0.041, -0.0142, -0.0037 };
    for (int i = 0; i < 8; i++) { double s = z_sqrt(1 + c[i] * c[i]); CS[i] = (float)(1 / s); CA[i] = (float)(c[i] / s); }
    for (int p = 0; p < 7; p++) { if (p == 6) { ISK[p][0] = 1; ISK[p][1] = 0; continue; }
        double t = z_tan(p * PI / 12); ISK[p][0] = (float)(t / (1 + t)); ISK[p][1] = (float)(1 / (1 + t)); } }

/* 2^(q/4) */
static float p2q(int q) {
    static const float F[4] = { 1.0f, 1.18920712f, 1.41421356f, 1.68179283f };
    int e = q >> 2; if (e < -126) return 0; if (e > 127) e = 127;
    union { u32 u; float f; } x; x.u = (u32)(e + 127) << 23; return x.f * F[q & 3]; }

/* ---- cabecera ---- */
static int parse_hdr(const u8 *b, hdr_t *h) {
    if (b[0] != 0xFF || (b[1] & 0xE0) != 0xE0) return 0;
    int v = b[1] >> 3 & 3, l = 4 - (b[1] >> 1 & 3), bi = b[2] >> 4, si = b[2] >> 2 & 3;
    if (v == 1 || l == 4 || bi == 0 || bi == 15 || si == 3) return 0;
    h->ver = v; h->lay = l; h->prot = !(b[1] & 1); h->lsf = v != 3; h->sri = (v == 3 ? 0 : v == 2 ? 3 : 6) + si;
    h->br = KBPS[h->lsf ? (l == 1 ? 3 : 4) : l - 1][bi]; h->pad = b[2] >> 1 & 1; h->mode = b[3] >> 6; h->mext = b[3] >> 4 & 3;
    h->nch = h->mode == 3 ? 1 : 2; int sr = RATES[h->sri];
    if (l == 1) { h->size = (12000 * h->br / sr + h->pad) * 4; h->nsmp = 384; }
    else if (l == 2) { h->size = 144000 * h->br / sr + h->pad; h->nsmp = 1152; }
    else { h->size = (h->lsf ? 72000 : 144000) * h->br / sr + h->pad; h->nsmp = h->lsf ? 576 : 1152; }
    return h->size >= 8; }
static int same(const hdr_t *a, const hdr_t *b) { return a->ver == b->ver && a->lay == b->lay && a->sri == b->sri; }

/* ---- lectura de bits ---- */
typedef struct { const u8 *p; long pos, end; } bits_t;
static u32 gb(bits_t *b, int n) {
    u32 v = 0; while (n-- > 0) { v <<= 1; if (b->pos < b->end) v |= b->p[b->pos >> 3] >> (7 - (b->pos & 7)) & 1; b->pos++; } return v; }
static int g1(bits_t *b) { int v = b->pos < b->end ? b->p[b->pos >> 3] >> (7 - (b->pos & 7)) & 1 : 0; b->pos++; return v; }

/* ---- capa III ---- */
static void layout(const gr_t *g, int sri, int lsf, lay_t *L) {
    int n = 0;
    if (!(g->wsf && g->bt == 2)) { for (int b = 0; b < 22; b++) { L->st[n] = SFL[sri][b]; L->w[n] = SFL[sri][b + 1] - SFL[sri][b]; L->win[n] = -1; L->band[n++] = b; } }
    else { int nl = g->mixed ? (lsf ? 6 : 8) : 0, p = SFL[sri][nl];
        for (int b = 0; b < nl; b++) { L->st[n] = SFL[sri][b]; L->w[n] = SFL[sri][b + 1] - SFL[sri][b]; L->win[n] = -1; L->band[n++] = b; }
        for (int b = g->mixed ? 3 : 0; b < 13; b++) { int w = SFS[sri][b + 1] - SFS[sri][b];
            for (int k = 0; k < 3; k++) { L->st[n] = p; L->w[n] = w; L->f[n] = SFS[sri][b]; L->win[n] = k; L->band[n++] = b; p += w; } } }
    L->n = n; }

static int huff(bits_t *b, int k) {   /* un simbolo de la tabla k (indice de HT); -1 si el codigo no existe */
    int nd = 0; for (int i = 0; i < 24; i++) { int c = HT[k][nd][g1(b)]; if (c < 0) return -1 - c; if (!c) return -1; nd = c; } return -1; }

/* Huffman de un granulo: valores enteros con signo en q[576]; devuelve hasta donde hay datos */
static int read_huff(bits_t *b, const gr_t *g, const lay_t *L, long end, int *q) {
    int r1 = 576, r2 = 576, n = g->bv * 2, i = 0, acc = 0;
    if (g->wsf) { int c = g->bt == 2 && !g->mixed ? 9 : 8; for (int e = 0; e < c && e < L->n; e++) acc += L->w[e]; r1 = acc; }
    else { for (int e = 0; e < L->n; e++) { if (e == g->r0 + 1) r1 = acc; if (e == g->r0 + g->r1 + 2) r2 = acc; acc += L->w[e]; } if (g->r0 + 1 >= L->n) r1 = 576; }
    if (n > 576) n = 576; if (r1 > n) r1 = n; if (r2 > n) r2 = n; if (r2 < r1) r2 = r1;
    for (; i < n && b->pos <= end; i += 2) {
        int t = g->tab[i < r1 ? 0 : i < r2 ? 1 : 2], k = HTI[t];
        if (k < 0) { q[i] = q[i + 1] = 0; continue; }
        int v = huff(b, k); if (v < 0) break; int x = v >> 4, y = v & 15, lb = HT_LIN[t];
        if (lb && x == 15) x += gb(b, lb); if (x && g1(b)) x = -x;
        if (lb && y == 15) y += gb(b, lb); if (y && g1(b)) y = -y;
        q[i] = x; q[i + 1] = y; }
    if (i > n) i = n;
    while (i + 4 <= 576 && b->pos < end) {
        int v; if (g->c1t) v = 15 - (int)gb(b, 4);
        else { int nd = 0; v = -1; for (int j = 0; j < 7; j++) { int c = QT[nd][g1(b)]; if (c < 0) { v = -1 - c; break; } nd = c; } if (v < 0) break; }
        int t[4] = { v >> 3 & 1, v >> 2 & 1, v >> 1 & 1, v & 1 };
        for (int j = 0; j < 4; j++) if (t[j] && g1(b)) t[j] = -1;
        if (b->pos > end) break;   /* la ultima cuadrupla se pasa del final: no cuenta */
        for (int j = 0; j < 4; j++) q[i + j] = t[j]; i += 4; }
    for (int j = i; j < 576; j++) q[j] = 0;
    return i; }

/* factores de escala de un granulo y canal (sf y su maximo para la intensidad de MPEG-2) */
static void read_sf(bits_t *b, const hdr_t *h, gr_t *g, const lay_t *L, int gr, int ch, const int *scfsi, int *sf, int *smax, int prev[2][22]) {
    for (int e = 0; e < 40; e++) { sf[e] = 0; smax[e] = 0; }
    if (!h->lsf) {
        int s1 = SLEN[g->sfc][0], s2 = SLEN[g->sfc][1];
        if (g->wsf && g->bt == 2) { for (int e = 0; e < L->n; e++) { int bd = L->band[e], sl = L->win[e] < 0 ? s1 : bd < 6 ? s1 : bd < 12 ? s2 : 0; sf[e] = gb(b, sl); smax[e] = (1 << sl) - 1; } }
        else { for (int bd = 0; bd < 21; bd++) { int grp = bd < 6 ? 0 : bd < 11 ? 1 : bd < 16 ? 2 : 3, sl = bd < 11 ? s1 : s2;
                if (gr && scfsi[grp]) sf[bd] = prev[ch][bd]; else sf[bd] = gb(b, sl); smax[bd] = (1 << sl) - 1; prev[ch][bd] = sf[bd]; } }
        return; }
    int sfc = g->sfc, sl[4] = { 0, 0, 0, 0 }, row;
    if ((h->mext & 1) && h->mode == 1 && ch == 1) { int c = sfc >> 1;
        if (c < 180) { sl[0] = c / 36; sl[1] = c % 36 / 6; sl[2] = c % 6; row = 3; }
        else if (c < 244) { c -= 180; sl[0] = (c & 63) >> 4; sl[1] = (c & 15) >> 2; sl[2] = c & 3; row = 4; }
        else { c -= 244; sl[0] = c / 3; sl[1] = c % 3; row = 5; }
        g->pre = 0; }
    else if (sfc < 400) { sl[0] = (sfc >> 4) / 5; sl[1] = (sfc >> 4) % 5; sl[2] = (sfc & 15) >> 2; sl[3] = sfc & 3; row = 0; g->pre = 0; }
    else if (sfc < 500) { sfc -= 400; sl[0] = (sfc >> 2) / 5; sl[1] = (sfc >> 2) % 5; sl[2] = sfc & 3; row = 1; g->pre = 0; }
    else { sfc -= 500; sl[0] = sfc / 3; sl[1] = sfc % 3; row = 2; g->pre = 1; }
    int bi = g->wsf && g->bt == 2 ? (g->mixed ? 2 : 1) : 0, e = 0;
    for (int k = 0; k < 4; k++) for (int j = 0; j < NRSFB[row][bi][k] && e < 40; j++, e++) { sf[e] = gb(b, sl[k]); smax[e] = (1 << sl[k]) - 1; } }

static void requant(const gr_t *g, const lay_t *L, const int *sf, const int *q, float *xr) {
    for (int e = 0; e < L->n; e++) {
        int qq = g->gg - 210, s = sf[e] + (L->win[e] < 0 && g->pre ? PRETAB[L->band[e]] : 0);
        if (L->win[e] >= 0) qq -= 8 * g->sbg[L->win[e]];
        qq -= s << (1 + g->ss); float m = p2q(qq);
        for (int i = L->st[e]; i < L->st[e] + L->w[e]; i++) { int v = q[i]; xr[i] = v > 0 ? P43[v > 8206 ? 8206 : v] * m : v < 0 ? -P43[-v > 8206 ? 8206 : -v] * m : 0; } } }

static void stereo(const hdr_t *h, const gr_t *g1r, const lay_t *L, const int *sf, const int *smax, float *x0, float *x1) {
    int ms = h->mext & 2, is = h->mext & 1; const float R2 = 0.70710678f;
    signed char isr[40] = { 0 };
    if (is) {
        int lastl = -1, lasts[3] = { -1, -1, -1 }, anys = 0;
        for (int e = 0; e < L->n; e++) { int nz = 0; for (int i = L->st[e]; i < L->st[e] + L->w[e]; i++) if (x1[i] != 0) { nz = 1; break; }
            if (L->win[e] < 0) { if (nz) lastl = e; } else { anys |= nz; if (nz) lasts[L->win[e]] = e; } }
        for (int e = 0; e < L->n; e++) isr[e] = L->win[e] < 0 ? (!anys && e > lastl) : e > lasts[L->win[e]];
        float io = g1r->sfc & 1 ? R2 : 0.84089642f;
        for (int e = 0; e < L->n; e++) { if (!isr[e]) continue;
            /* la banda mas alta no lleva factor: usa la posicion de la de debajo si esa tambien va por intensidad (como el
               decodificador de referencia de la norma) */
            int last = L->win[e] < 0 ? L->band[e] == 21 : L->band[e] == 12, src = last ? (L->win[e] < 0 ? e - 1 : e - 3) : e;
            if (last && (src < 0 || !isr[src])) { isr[e] = 0; continue; }
            int p = sf[src]; float kl, kr;
            if (!h->lsf) { if (p >= 7) { isr[e] = 0; continue; } kl = ISK[p][0]; kr = ISK[p][1]; }
            else { if (p == smax[src]) { isr[e] = 0; continue; }
                kl = kr = 1; if (p & 1) { for (int j = 0; j < (p + 1) / 2; j++) kl *= io; } else for (int j = 0; j < p / 2; j++) kr *= io; }
            for (int i = L->st[e]; i < L->st[e] + L->w[e]; i++) { float m = x0[i]; x0[i] = m * kl; x1[i] = m * kr; } } }
    if (ms) for (int e = 0; e < L->n; e++) if (!isr[e])
        for (int i = L->st[e]; i < L->st[e] + L->w[e]; i++) { float m = x0[i], s = x1[i]; x0[i] = (m + s) * R2; x1[i] = (m - s) * R2; } }

/* reduccion de alias, IMDCT, solape e inversion de frecuencias: xr (576) -> out[18][32] (tiempo x subbanda) */
static void hybrid(zmp3d *d, int ch, const gr_t *g, const lay_t *L, float *xr, float out[18][32]) {
    int shortb = g->wsf && g->bt == 2, nlong = shortb ? (g->mixed ? 2 : 0) : 32, bt = g->wsf && !shortb ? g->bt : 0;
    float xs[3][192];
    if (shortb) { for (int w = 0; w < 3; w++) for (int i = 0; i < 192; i++) xs[w][i] = 0;
        for (int e = 0; e < L->n; e++) if (L->win[e] >= 0) for (int j = 0; j < L->w[e]; j++) xs[L->win[e]][L->f[e] + j] = xr[L->st[e] + j]; }
    for (int sb = 0; sb + 1 < nlong; sb++) for (int i = 0; i < 8; i++) {
        float lo = xr[18 * sb + 17 - i], hi = xr[18 * sb + 18 + i];
        xr[18 * sb + 17 - i] = lo * CS[i] - hi * CA[i]; xr[18 * sb + 18 + i] = hi * CS[i] + lo * CA[i]; }
    for (int sb = 0; sb < 32; sb++) {
        float z[36], *ov = d->ov[ch][sb];
        if (sb < nlong) { const float *X = xr + 18 * sb; int nz = 0; for (int k = 0; k < 18; k++) if (X[k] != 0) { nz = 1; break; }
            if (!nz) for (int i = 0; i < 36; i++) z[i] = 0;
            else for (int i = 0; i < 36; i++) { float s = 0; for (int k = 0; k < 18; k++) s += X[k] * IML[i][k]; z[i] = s * WIN[bt][i]; } }
        else { for (int i = 0; i < 36; i++) z[i] = 0;
            for (int w = 0; w < 3; w++) { const float *X = xs[w] + 6 * sb; int nz = 0; for (int k = 0; k < 6; k++) if (X[k] != 0) { nz = 1; break; } if (!nz) continue;
                for (int i = 0; i < 12; i++) { float s = 0; for (int k = 0; k < 6; k++) s += X[k] * IMS[i][k]; z[6 + 6 * w + i] += s * WS[i]; } } }
        for (int t = 0; t < 18; t++) { float v = z[t] + ov[t]; ov[t] = z[18 + t]; out[t][sb] = (sb & t & 1) ? -v : v; } } }

/* banco de sintesis: 32 muestras de subbanda -> 32 muestras de sonido (cada canal guarda los 16 ultimos vectores) */
static void synth(zmp3d *d, int ch, const float *S, float *o) {
    int p = d->sp[ch] = (d->sp[ch] + 15) & 15; float *C = d->syn[ch][p];
    for (int n = 0; n < 64; n++) { float s = 0; for (int k = 0; k < 32; k++) s += S[k] * SYC[n][k]; C[n] = s; }
    for (int j = 0; j < 32; j++) { float s = 0; for (int i = 0; i < 16; i++) s += SYW[i][j] * d->syn[ch][(p + i) & 15][32 * (i & 1) + j]; o[j] = s; } }

static short clip16(float v) { v *= 32768.0f; if (v >= 32767) return 32767; if (v <= -32768) return -32768; return (short)(v < 0 ? v - 0.5f : v + 0.5f); }

/* un cuadro de la capa III ya completo en f[0..size): 0 si falta el deposito de bits del cuadro anterior */
static int layer3(zmp3d *d, const hdr_t *h, const u8 *f, short *pcm) {
    int nch = h->nch, ngr = h->lsf ? 1 : 2, si = h->lsf ? (nch == 1 ? 9 : 17) : (nch == 1 ? 17 : 32), off = 4 + (h->prot ? 2 : 0);
    if (off + si > h->size) return 0;
    bits_t b = { f + off, 0, si * 8 }; gr_t G[2][2]; int scfsi[2][4] = { { 0 } };
    int mdb = gb(&b, h->lsf ? 8 : 9); gb(&b, h->lsf ? nch : (nch == 1 ? 5 : 3));
    if (!h->lsf) for (int c = 0; c < nch; c++) for (int k = 0; k < 4; k++) scfsi[c][k] = gb(&b, 1);
    for (int gr = 0; gr < ngr; gr++) for (int c = 0; c < nch; c++) { gr_t *g = &G[gr][c];
        g->p23 = gb(&b, 12); g->bv = gb(&b, 9); g->gg = gb(&b, 8); g->sfc = gb(&b, h->lsf ? 9 : 4); g->wsf = gb(&b, 1);
        g->bt = g->mixed = 0; g->r0 = g->r1 = 0; g->sbg[0] = g->sbg[1] = g->sbg[2] = 0;
        if (g->wsf) { g->bt = gb(&b, 2); g->mixed = gb(&b, 1); g->tab[0] = gb(&b, 5); g->tab[1] = gb(&b, 5); g->tab[2] = 0;
            for (int k = 0; k < 3; k++) g->sbg[k] = gb(&b, 3); if (!g->bt) return 0; }
        else { for (int k = 0; k < 3; k++) g->tab[k] = gb(&b, 5); g->r0 = gb(&b, 4); g->r1 = gb(&b, 3); }
        g->pre = h->lsf ? 0 : gb(&b, 1); g->ss = gb(&b, 1); g->c1t = gb(&b, 1); }
    /* datos principales: los mdb ultimos bytes del deposito y despues los de este cuadro */
    int mlen = h->size - off - si; u8 md[512 + 2048];
    int ok = mdb <= d->nres && mlen >= 0 && mlen <= 2048;
    if (ok) { memcpy(md, d->res + d->nres - mdb, mdb); memcpy(md + mdb, f + off + si, mlen); }
    if (mlen > 0 && mlen <= 2048) {   /* el deposito se queda con los ultimos 511 bytes */
        if (mlen >= 511) { memcpy(d->res, f + h->size - 511, 511); d->nres = 511; }
        else { int keep = d->nres + mlen > 511 ? 511 - mlen : d->nres; memmove(d->res, d->res + d->nres - keep, keep); memcpy(d->res + keep, f + off + si, mlen); d->nres = keep + mlen; } }
    if (!ok) return 0;
    bits_t m = { md, 0, (long)(mdb + mlen) * 8 }; int prev[2][22] = { { 0 } };
    static float xr[2][576], out[2][18][32]; static int q[576];
    for (int gr = 0; gr < ngr; gr++) {
        lay_t L[2]; int sf[2][40], smax[2][40];
        for (int c = 0; c < nch; c++) { gr_t *g = &G[gr][c]; layout(g, h->sri, h->lsf, &L[c]);
            long st = m.pos, end = st + g->p23; if (end > m.end) end = m.end;
            read_sf(&m, h, g, &L[c], gr, c, scfsi[c], sf[c], smax[c], prev);
            read_huff(&m, g, &L[c], end, q); requant(g, &L[c], sf[c], q, xr[c]); m.pos = st + g->p23; }
        if (nch == 2 && h->mode == 1 && h->mext) stereo(h, &G[gr][1], &L[1], sf[1], smax[1], xr[0], xr[1]);
        for (int c = 0; c < nch; c++) hybrid(d, c, &G[gr][c], &L[c], xr[c], out[c]);
        for (int t = 0; t < 18; t++) for (int c = 0; c < nch; c++) { float o[32]; synth(d, c, out[c][t], o);
            short *p = pcm + (gr * 576 + t * 32) * nch + c; for (int j = 0; j < 32; j++) p[j * nch] = clip16(o[j]); } }
    return ngr * 576; }

/* ---- capas I y II ---- */
static int layer12(zmp3d *d, const hdr_t *h, const u8 *f, short *pcm) {
    int nch = h->nch, off = 4 + (h->prot ? 2 : 0), sbl = 32, cls[32] = { 0 };
    int bound = h->mode == 1 ? (h->mext + 1) * 4 : 32;
    bits_t b = { f + off, 0, (long)(h->size - off) * 8 };
    static u8 al[2][32], sc[2][32][3]; static float smp[2][3][32];   /* asignacion, factores, 3 muestras por subbanda */
    if (h->lay == 2) {   /* tabla de asignacion segun frecuencia y bits por canal */
        int t, cbr = h->br / nch, sr = RATES[h->sri];
        if (h->lsf) t = 4; else if ((sr == 48000 && cbr >= 56) || (cbr >= 56 && cbr <= 80)) t = 0;
        else if (sr != 48000 && cbr >= 96) t = 1; else if (sr != 32000 && cbr <= 48) t = 2; else t = 3;
        for (sbl = 0; L2T[t][sbl] != 255; sbl++) cls[sbl] = L2T[t][sbl]; }
    if (bound > sbl) bound = sbl;
    for (int sb = 0; sb < 32; sb++) for (int c = 0; c < 2; c++) al[c][sb] = 0;
    for (int sb = 0; sb < sbl; sb++) { int nb = h->lay == 1 ? 4 : L2NB[cls[sb]];
        if (sb < bound) for (int c = 0; c < nch; c++) al[c][sb] = gb(&b, nb); else al[0][sb] = al[1][sb] = gb(&b, nb);
        if (h->lay == 1) for (int c = 0; c < 2; c++) if (al[c][sb] == 15) al[c][sb] = 0; }
    if (h->lay == 1) { for (int sb = 0; sb < 32; sb++) for (int c = 0; c < nch; c++) if (al[c][sb]) sc[c][sb][0] = sc[c][sb][1] = sc[c][sb][2] = gb(&b, 6); }
    else { u8 si[2][32];
        for (int sb = 0; sb < sbl; sb++) for (int c = 0; c < nch; c++) if (al[c][sb]) si[c][sb] = gb(&b, 2);
        for (int sb = 0; sb < sbl; sb++) for (int c = 0; c < nch; c++) if (al[c][sb]) { u8 *s = sc[c][sb];
            switch (si[c][sb]) { case 0: s[0] = gb(&b, 6); s[1] = gb(&b, 6); s[2] = gb(&b, 6); break;
                case 1: s[0] = s[1] = gb(&b, 6); s[2] = gb(&b, 6); break; case 2: s[0] = s[1] = s[2] = gb(&b, 6); break;
                default: s[0] = gb(&b, 6); s[1] = s[2] = gb(&b, 6); } } }
    int ngr = h->lay == 1 ? 12 : 12, nsm = h->lay == 1 ? 1 : 3;   /* capa I: 12 x 1 muestra; capa II: 12 x 3 */
    for (int gr = 0; gr < ngr; gr++) {
        for (int c = 0; c < 2; c++) for (int k = 0; k < 3; k++) for (int sb = 0; sb < 32; sb++) smp[c][k][sb] = 0;
        for (int sb = 0; sb < sbl; sb++) for (int c = 0; c < (sb < bound ? nch : 1); c++) { int a = al[c][sb]; if (!a) continue;
            int L, cw[3];
            if (h->lay == 1) { int nb = a + 1; L = (1 << nb) - 1; cw[0] = gb(&b, nb); }
            else { L = L2Q[cls[sb]][a];
                if (L == 3 || L == 5 || L == 9) { int v = gb(&b, L == 3 ? 5 : L == 5 ? 7 : 10); for (int k = 0; k < 3; k++) { cw[k] = v % L; v /= L; } }
                else { int nb = 0; while ((1 << nb) - 1 < L) nb++; for (int k = 0; k < 3; k++) cw[k] = gb(&b, nb); } }
            int part = h->lay == 1 ? 0 : gr >> 2;
            for (int k = 0; k < nsm; k++) { float v = (float)(2 * cw[k] + 1 - L) / L;
                for (int cc = sb < bound ? c : 0; cc < (sb < bound ? c + 1 : nch); cc++) { int s = sc[cc][sb][part]; smp[cc][k][sb] = s < 63 ? v * SCF[s] : 0; } } }
        for (int k = 0; k < nsm; k++) for (int c = 0; c < nch; c++) { float o[32]; synth(d, c, smp[c][k], o);
            short *p = pcm + ((gr * nsm + k) * 32) * nch + c; for (int j = 0; j < 32; j++) p[j * nch] = clip16(o[j]); } }
    return ngr * nsm * 32; }

/* ---- interfaz ---- */
zmp3d *mp3d_new(void) { init(); return calloc(1, sizeof(zmp3d)); }
/* decodifica el siguiente cuadro de b[0..n): devuelve muestras por canal (0 si era un trozo sin sonido) y en *used
   los bytes consumidos (0: no queda ningun cuadro). pcm puede ser 0 para solo recorrer el archivo (duracion). */
int mp3d_frame(zmp3d *d, const u8 *b, long n, short *pcm, int *used, int *hz, int *ch) {
    long i = 0; hdr_t h; *used = 0;
    if (n >= 10 && !memcmp(b, "ID3", 3)) { long t = 10 + ((b[6] & 127L) << 21 | (b[7] & 127L) << 14 | (b[8] & 127L) << 7 | (b[9] & 127L)) + (b[5] & 0x10 ? 10 : 0);
        if (t <= n) { *used = (int)t; return 0; } return 0; }
    for (; i + 4 <= n; i++) {
        if (!parse_hdr(b + i, &h)) continue;
        if (d->locked && same(&h, &d->h)) break;
        /* sin sincronia: tienen que seguir dos cuadros iguales (o acabarse el archivo justo despues) */
        long j = i + h.size; int good = 1; hdr_t h2;
        for (int k = 0; k < 2 && good; k++) { if (j == n) break; if (j + 4 > n || !parse_hdr(b + j, &h2) || !same(&h, &h2)) good = 0; else j += h2.size; }
        if (good) break; }
    if (i + 4 > n || i + h.size > n) { *used = 0; return 0; }
    if (!d->locked || !same(&h, &d->h)) { d->nres = 0; } d->locked = 1; d->h = h;
    *used = (int)(i + h.size); if (hz) *hz = RATES[h.sri]; if (ch) *ch = h.nch;
    if (!pcm) return h.nsmp;
    return h.lay == 3 ? layer3(d, &h, b + i, pcm) : layer12(d, &h, b + i, pcm); }
