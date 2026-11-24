/* Prueba del decodificador MP3 (user/lib/zmp3d.c): genera flujos MPEG validos con contenido al azar que recorren casi
   todo lo que la norma permite (capas I, II y III; MPEG-1, 2 y 2.5; mono, estereo, M/S e intensidad; bloques largos,
   de inicio, cortos y de fin en secuencias validas; todas las tablas de Huffman, cuadruplas, scfsi, preflag, deposito de
   bits con main_data_begin, CRC). correr.sh los decodifica con zmp3d.c y con macOS y compara.
   No genera bloques mixtos: ahi los decodificadores difieren (macOS y otros aplican la ventana de forma distinta en las
   dos subbandas largas); zmp3d.c sigue al decodificador de referencia de la norma.
   Uso: flujos DIR [semilla] */
#include <stdio.h>
#include "z.h"
#include "mp3tab.h"
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
    { 0,4,8,12,16,22,30,40,52,66,84,106,136,192 }, { 0,4,8,12,16,22,28,38,50,64,80,100,126,192 }, { 0,4,8,12,16,22,30,42,58,78,104,138,180,192 },
    { 0,4,8,12,18,24,32,42,56,74,100,132,174,192 }, { 0,4,8,12,18,26,36,48,62,80,104,136,180,192 }, { 0,4,8,12,18,26,36,48,62,80,104,134,174,192 },
    { 0,4,8,12,18,26,36,48,62,80,104,134,174,192 }, { 0,4,8,12,18,26,36,48,62,80,104,134,174,192 }, { 0,8,16,24,36,52,72,96,124,160,162,164,166,192 } };
static const int RATES[9] = { 44100, 48000, 32000, 22050, 24000, 16000, 11025, 12000, 8000 };
static const short K1[15] = { 0,32,40,48,56,64,80,96,112,128,160,192,224,256,320 }, K2[15] = { 0,8,16,24,32,40,48,56,64,80,96,112,128,144,160 };
static const u8 SLEN[16][2] = { {0,0},{0,1},{0,2},{0,3},{3,0},{1,1},{1,2},{1,3},{2,1},{2,2},{2,3},{3,1},{3,2},{3,3},{4,2},{4,3} };
static const u8 NRSFB[6][3][4] = {
    { {6,5,5,5},{9,9,9,9},{6,9,9,9} }, { {6,5,7,3},{9,9,12,6},{6,9,12,6} }, { {11,10,0,0},{18,18,0,0},{15,18,0,0} },
    { {7,7,7,0},{12,12,12,0},{6,15,12,0} }, { {6,6,6,3},{12,9,9,6},{6,12,9,6} }, { {8,8,5,0},{15,12,9,0},{6,18,9,0} } };
static unsigned R = 1; static int PB[2], PB0[2];   /* tipo de bloque anterior de cada canal */ static unsigned rnd(void) { R = R * 1103515245 + 12345; return R >> 8; }
typedef struct { u8 *b; long n; } bw;
static void put(bw *w, unsigned v, int n) { for (int i = n - 1; i >= 0; i--) { if (v >> i & 1) w->b[w->n >> 3] |= 0x80 >> (w->n & 7); w->n++; } }
typedef struct { int p23, bv, gg, sfc, wsf, bt, mixed, tab[3], sbg[3], r0, r1, pre, ss, c1t; } gi;
static int okt(int t) { return t != 4 && t != 14; }
static int pick_tab(void) { int t; do t = rnd() % 32; while (!okt(t)); return t; }
/* escribe factores de escala, Huffman y cuadruplas; devuelve bits */
static void gen_gr(bw *w, gi *g, int lsf, int sri, int isr, int gr, int *scfsi, int sparse) {
    long st = w->n;
    if (!lsf) { int s1 = SLEN[g->sfc][0], s2 = SLEN[g->sfc][1];
        if (g->wsf && g->bt == 2) { int nl = g->mixed ? 8 : 0;
            for (int b = 0; b < nl; b++) put(w, rnd() % (1 << s1), s1);
            for (int b = g->mixed ? 3 : 0; b < 12; b++) for (int k = 0; k < 3; k++) { int s = b < 6 ? s1 : s2; put(w, rnd() % (1 << s), s); } }
        else for (int b = 0; b < 21; b++) { int grp = b < 6 ? 0 : b < 11 ? 1 : b < 16 ? 2 : 3, s = b < 11 ? s1 : s2; if (gr && scfsi[grp]) continue; put(w, rnd() % (1 << s), s); } }
    else { int sfc = g->sfc, sl[4] = { 0 }, row;
        if (isr) { int c = sfc >> 1; if (c < 180) { sl[0] = c / 36; sl[1] = c % 36 / 6; sl[2] = c % 6; row = 3; } else if (c < 244) { c -= 180; sl[0] = (c & 63) >> 4; sl[1] = (c & 15) >> 2; sl[2] = c & 3; row = 4; } else { c -= 244; sl[0] = c / 3; sl[1] = c % 3; row = 5; } }
        else if (sfc < 400) { sl[0] = (sfc >> 4) / 5; sl[1] = (sfc >> 4) % 5; sl[2] = (sfc & 15) >> 2; sl[3] = sfc & 3; row = 0; }
        else if (sfc < 500) { sfc -= 400; sl[0] = (sfc >> 2) / 5; sl[1] = (sfc >> 2) % 5; sl[2] = sfc & 3; row = 1; }
        else { sfc -= 500; sl[0] = sfc / 3; sl[1] = sfc % 3; row = 2; }
        int bi = g->wsf && g->bt == 2 ? (g->mixed ? 2 : 1) : 0;
        for (int k = 0; k < 4; k++) for (int j = 0; j < NRSFB[row][bi][k]; j++) put(w, rnd() % (1 << sl[k]), sl[k]); }
    /* regiones segun la norma */
    int r1, r2, n = g->bv * 2;
    if (g->wsf) { r1 = g->bt == 2 && !g->mixed ? SFS[sri][3] * 3 : SFL[sri][8]; r2 = 576; }
    else { r1 = SFL[sri][g->r0 + 1]; r2 = SFL[sri][g->r0 + g->r1 + 2]; }
    for (int i = 0; i < n; i += 2) {
        int t = g->tab[i < r1 ? 0 : i < r2 ? 1 : 2]; if (!t) continue;
        int xl = HT_X[t], lb = HT_LIN[t], v[2];
        for (int k = 0; k < 2; k++) v[k] = sparse || rnd() % 100 < 50 ? (rnd() % 3 == 0) : rnd() % xl;
        put(w, HCODE[t][v[0] * 16 + v[1]], HLEN[t][v[0] * 16 + v[1]]);
        for (int k = 0; k < 2; k++) { if (lb && v[k] == 15) { int m = (1 << lb) - 1; put(w, rnd() % (m < 300 ? m + 1 : 300), lb); } if (v[k]) put(w, rnd() & 1, 1); } }
    int maxq = (576 - n) / 4, nq = maxq ? rnd() % (sparse ? 3 : maxq + 1) : 0; if (rnd() % 4 == 0) nq = 0;
    for (int k = 0; k < nq; k++) { int v = rnd() % 3 ? 0 : rnd() % 16; if (g->c1t) put(w, 15 - v, 4); else put(w, QA_C[v], QA_L[v]); for (int j = 3; j >= 0; j--) if (v >> j & 1) put(w, rnd() & 1, 1); }
    g->p23 = (int)(w->n - st); }
static int capa3(const char *dir, unsigned seed) {
    int nf = 0;
    for (int ver = 0; ver < 3; ver++) for (int si = 0; si < 3; si++) for (int md = 0; md < 4; md++) for (int rep = 0; rep < 2; rep++) {
        R = (unsigned)(ver * 1000 + si * 100 + md * 10 + rep + 7 + seed); int lsf = ver > 0, sri = ver * 3 + si, nch = md == 3 ? 1 : 2, ngr = lsf ? 1 : 2, sil = lsf ? (nch == 1 ? 9 : 17) : (nch == 1 ? 17 : 32);
        int vb = ver == 0 ? 3 : ver == 1 ? 2 : 0, maxback = lsf ? 255 : 511;
        static u8 ms[1 << 22], out[1 << 22], fh[400][64]; static int slot[400], fsz[400], hl[400]; memset(ms, 0, sizeof ms);
        for (long i = 0; i < (1 << 22); i++) ms[i] = rnd();
        long S = 0, prevEnd = 0; int NF = 40;
        for (int f = 0; f < NF; f++) {
            int bi = lsf ? 8 + rnd() % 7 : 9 + rnd() % 6, pad = rnd() & 1, prot = rnd() % 4 == 0, mext = md == 1 ? rnd() % 4 : 0; if (lsf) mext &= 2;   /* sin intensidad en MPEG-2: macOS la decodifica distinto (ver correr.sh) */
            int br = lsf ? K2[bi] : K1[bi], size = (lsf ? 72000 : 144000) * br / RATES[sri] + pad, Ls = size - 4 - (prot ? 2 : 0) - sil;
            gi G[2][2]; int scfsi[2][4] = { { 0 } }; static u8 mdb_[1 << 16]; bw w;
            PB0[0] = PB[0]; PB0[1] = PB[1];
            for (int tries = 0;; tries++) { PB[0] = PB0[0]; PB[1] = PB0[1];
                memset(mdb_, 0, sizeof mdb_); w.b = mdb_; w.n = 0; int big = tries < 20;
                for (int c = 0; c < nch; c++) for (int k = 0; k < 4; k++) scfsi[c][k] = !lsf && rnd() % 3 == 0;
                for (int gr = 0; gr < ngr; gr++) for (int c = 0; c < nch; c++) { gi *g = &G[gr][c]; memset(g, 0, sizeof *g);
                    if (c == 1 && md == 1) { *g = G[gr][0]; } else { int pb = PB[c], nb; if (pb == 1) nb = 2; else if (pb == 2) nb = rnd() % 2 ? 2 : 3;
                        else nb = rnd() % 6 == 0 ? 1 : 0;
                        g->wsf = nb != 0; g->bt = nb; }
                    if (g->wsf) { g->tab[0] = pick_tab(); g->tab[1] = pick_tab(); g->tab[2] = 0; for (int k = 0; k < 3; k++) g->sbg[k] = rnd() % 4; g->r0 = g->r1 = 0; }
                    else { for (int k = 0; k < 3; k++) g->tab[k] = pick_tab(); g->r0 = rnd() % 16; g->r1 = rnd() % 8; if (g->r0 + g->r1 + 2 > 22) g->r1 = 20 - g->r0; }
                    g->gg = 140 + rnd() % 30; g->sfc = lsf ? rnd() % 512 : rnd() % 16; if (lsf && c == 1 && md == 1 && (mext & 1)) { int cc; do cc = rnd() % 180; while (!(cc / 36) || !(cc % 36 / 6) || !(cc % 6)); g->sfc = cc * 2 + (rnd() & 1); } g->pre = lsf ? 0 : rnd() & 1; g->ss = rnd() & 1; g->c1t = rnd() & 1;
                    if (c == 0 || md != 1) PB[c] = g->wsf ? g->bt : 0; if (md == 1 && c == 1) PB[1] = PB[0]; int sparse = c == 1 && (mext & 1); g->bv = big ? rnd() % (sparse ? 40 : 289) : rnd() % 30; }
                for (int c = 0; c < nch; c++) { int ls = 0; for (int gr = 0; gr < ngr; gr++) ls |= G[gr][c].wsf && G[gr][c].bt == 2; if (ls) for (int k = 0; k < 4; k++) scfsi[c][k] = 0; }
                int fits = 1;
                for (int gr = 0; gr < ngr; gr++) for (int c = 0; c < nch; c++) { int sparse = c == 1 && (mext & 1) && G[gr][c].bv < 40;
                    gen_gr(&w, &G[gr][c], lsf, sri, lsf && (mext & 1) && md == 1 && c == 1, gr, scfsi[c], sparse); if (G[gr][c].p23 > 4095) fits = 0; }
                long len = (w.n + 7) / 8, lo = prevEnd > S - maxback ? prevEnd : S - maxback;
                if (fits && lo + len <= S + Ls) { long P = lo + (S + Ls - lo - len > 0 ? rnd() % (S - lo + 1) : 0); if (P + len > S + Ls) P = lo; if (f == 0) P = 0;
                    if (P + len > S + Ls) continue; memcpy(ms + P, mdb_, len); prevEnd = P + len;
                    /* cabecera e informacion lateral */
                    bw h = { fh[f], 0 }; memset(fh[f], 0, 64);
                    put(&h, 0x7FF, 11); put(&h, vb, 2); put(&h, 1, 2); put(&h, !prot, 1); put(&h, bi, 4); put(&h, si, 2); put(&h, pad, 1); put(&h, 0, 1); put(&h, md, 2); put(&h, mext, 2); put(&h, 0, 4);
                    if (prot) put(&h, rnd() & 0xFFFF, 16);
                    put(&h, (unsigned)(S - P), lsf ? 8 : 9); put(&h, 0, lsf ? nch : (nch == 1 ? 5 : 3));
                    if (!lsf) for (int c = 0; c < nch; c++) for (int k = 0; k < 4; k++) put(&h, scfsi[c][k], 1);
                    for (int gr = 0; gr < ngr; gr++) for (int c = 0; c < nch; c++) { gi *g = &G[gr][c];
                        put(&h, g->p23, 12); put(&h, g->bv, 9); put(&h, g->gg, 8); put(&h, g->sfc, lsf ? 9 : 4); put(&h, g->wsf, 1);
                        if (g->wsf) { put(&h, g->bt, 2); put(&h, g->mixed, 1); put(&h, g->tab[0], 5); put(&h, g->tab[1], 5); for (int k = 0; k < 3; k++) put(&h, g->sbg[k], 3); }
                        else { for (int k = 0; k < 3; k++) put(&h, g->tab[k], 5); put(&h, g->r0, 4); put(&h, g->r1, 3); }
                        if (!lsf) put(&h, g->pre, 1); put(&h, g->ss, 1); put(&h, g->c1t, 1); }
                    hl[f] = (int)(h.n / 8); slot[f] = Ls; fsz[f] = size; break; }
                if (tries > 200) { fprintf(stderr, "no cabe\n"); return 1; } }
            S += Ls; }
        long o = 0, s = 0; for (int f = 0; f < NF; f++) { memcpy(out + o, fh[f], hl[f]); o += hl[f]; memcpy(out + o, ms + s, slot[f]); o += slot[f]; s += slot[f]; }
        char p[300]; snprintf(p, 300, "%s/f%d%d%d%d.mp3", dir, ver, si, md, rep); FILE *fo = fopen(p, "wb"); fwrite(out, 1, o, fo); fclose(fo); nf++; }
    return nf; }

/* ---- capas I y II ---- */
static const unsigned short L2Q[8][16] = {
    { 0,3,7,15,31,63,127,255,511,1023,2047,4095,8191,16383,32767,65535 }, { 0,3,5,7,9,15,31,63,127,255,511,1023,2047,4095,8191,65535 },
    { 0,3,5,7,9,15,31,65535 }, { 0,3,5,65535 }, { 0,3,5,9,15,31,63,127,255,511,1023,2047,4095,8191,16383,32767 },
    { 0,3,5,9,15,31,63,127 }, { 0,3,5,7,9,15,31,63,127,255,511,1023,2047,4095,8191,16383 }, { 0,3,5,9 } };
static const u8 L2NB[8] = { 4, 4, 3, 2, 4, 3, 4, 2 };
static const u8 L2T[5][31] = {
    { 0,0,0,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,3,3,3,3,255 }, { 0,0,0,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,3,3,3,3,3,3,3,255 },
    { 4,4,5,5,5,5,5,5,255 }, { 4,4,5,5,5,5,5,5,5,5,5,5,255 }, { 6,6,6,6,5,5,5,5,5,5,5,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,255 } };
static const short KB12[5][15] = { { 0,32,64,96,128,160,192,224,256,288,320,352,384,416,448 }, { 0,32,48,56,64,80,96,112,128,160,192,224,256,320,384 },
    { 0 }, { 0,32,48,56,64,80,96,112,128,144,160,176,192,224,256 }, { 0,8,16,24,32,40,48,56,64,80,96,112,128,144,160 } };
static int capa12(const char *dir, unsigned seed) {
    int nf = 0;
    for (int lay = 1; lay <= 2; lay++) for (int ver = 0; ver < 2; ver++) for (int si = 0; si < 3; si++) for (int md = 0; md < 4; md++) {
        R = lay * 10000 + ver * 1000 + si * 100 + md * 10 + 3 + seed;
        int lsf = ver, sri = ver * 3 + si, nch = md == 3 ? 1 : 2; static u8 out[1 << 22]; long o = 0;
        for (int f = 0; f < 40; f++) {
            int bi = 5 + rnd() % 10, pad = rnd() & 1, prot = rnd() % 4 == 0, mext = md == 1 ? rnd() % 4 : 0;
            int br = KB12[lsf ? (lay == 1 ? 3 : 4) : lay - 1][bi], sr = RATES[sri];
            int size = lay == 1 ? (12000 * br / sr + pad) * 4 : 144000 * br / sr + pad;
            int sbl = 32, cls[32] = { 0 }, bound = md == 1 ? (mext + 1) * 4 : 32;
            if (lay == 2) { int t, cbr = br / nch; if (lsf) t = 4; else if ((sr == 48000 && cbr >= 56) || (cbr >= 56 && cbr <= 80)) t = 0; else if (sr != 48000 && cbr >= 96) t = 1; else if (sr != 32000 && cbr <= 48) t = 2; else t = 3;
                for (sbl = 0; L2T[t][sbl] != 255; sbl++) cls[sbl] = L2T[t][sbl]; }
            if (bound > sbl) bound = sbl;
            for (int tries = 0;; tries++) {
                memset(out + o, 0, size); bw w = { out + o, 0 };
                put(&w, 0x7FF, 11); put(&w, ver == 0 ? 3 : 2, 2); put(&w, 4 - lay, 2); put(&w, !prot, 1); put(&w, bi, 4); put(&w, si, 2); put(&w, pad, 1); put(&w, 0, 1); put(&w, md, 2); put(&w, mext, 2); put(&w, 0, 4);
                if (prot) put(&w, rnd() & 0xFFFF, 16);
                int al[2][32] = { { 0 } }; int dens = tries < 30 ? 3 : tries < 200 ? 8 : 1000000;
                for (int sb = 0; sb < sbl; sb++) { int nb = lay == 1 ? 4 : L2NB[cls[sb]], mx = (1 << nb) - (lay == 1 ? 2 : 1);
                    for (int c = 0; c < (sb < bound ? nch : 1); c++) { int a = rnd() % dens ? 0 : rnd() % (mx + 1); if (a > 4 && rnd() % 2) a = rnd() % 4; al[c][sb] = a; put(&w, a, nb); }
                    if (sb >= bound) al[1][sb] = al[0][sb]; }
                if (lay == 1) { for (int sb = 0; sb < 32; sb++) for (int c = 0; c < nch; c++) if (al[c][sb]) put(&w, rnd() % 63, 6); }
                else { int s2[2][32];
                    for (int sb = 0; sb < sbl; sb++) for (int c = 0; c < nch; c++) if (al[c][sb]) { s2[c][sb] = rnd() % 4; put(&w, s2[c][sb], 2); }
                    for (int sb = 0; sb < sbl; sb++) for (int c = 0; c < nch; c++) if (al[c][sb]) { int k = s2[c][sb] == 0 ? 3 : s2[c][sb] == 2 ? 1 : 2; for (int j = 0; j < k; j++) put(&w, 10 + rnd() % 53, 6); } }
                for (int gr = 0; gr < 12; gr++) for (int sb = 0; sb < sbl; sb++) for (int c = 0; c < (sb < bound ? nch : 1); c++) { int a = al[c][sb]; if (!a) continue;
                    if (lay == 1) { int nb = a + 1; put(&w, rnd() % ((1 << nb) - 1), nb); }
                    else { int L = L2Q[cls[sb]][a]; if (L == 3 || L == 5 || L == 9) { int v = rnd() % L + L * (rnd() % L) + L * L * (rnd() % L); put(&w, v, L == 3 ? 5 : L == 5 ? 7 : 10); }
                        else { int nb = 0; while ((1 << nb) - 1 < L) nb++; for (int k = 0; k < 3; k++) put(&w, rnd() % L, nb); } } }
                if (w.n <= size * 8L) break; if (tries > 500) { fprintf(stderr, "no cabe\n"); return 1; } }
            o += size; }
        char p[300]; snprintf(p, 300, "%s/l%d%d%d%d.mp3", dir, lay, ver, si, md); FILE *fo = fopen(p, "wb"); fwrite(out, 1, o, fo); fclose(fo); nf++; }
    return nf; }

int main(int argc, char **argv) {
    if (argc < 2) return 2; unsigned seed = argc > 2 ? (unsigned)atoi(argv[2]) * 100000u : 0;
    int n = capa3(argv[1], seed); n += capa12(argv[1], seed); printf("%d flujos generados\n", n); return 0; }
