/* Alquimia: compresor deflate (RFC 1951) por flujo, con LZ77 de cadenas hash, emparejamiento perezoso y bloques
   Huffman dinamicos, fijos o guardados (se elige el mas corto en cada bloque). Sirve a ZIP, GZ y TAR.GZ.
   Uso: dfl_new(nivel 1-3); dfl_put(d, datos, n) tantas veces como haga falta; dfl_end(d). Lo comprimido se va
   dejando en d->out (d->on bytes): quien llama lo escribe y pone d->on = 0. */
#define DW 32768
#define DWM (DW - 1)
#define DMIN 3
#define DMAX 258
#define DMAXD (DW - DMAX - DMIN - 1)
#define DHB 15
#define DHS (1 << DHB)
#define DCAP (4 * DW)
#define DSYM 16384
#define DSPAN 60000

typedef struct {
    u8 *out; long on, ocap; u32 bb; int bc;
    u8 *buf; long bst; long blen;          /* buf[0] es la posicion absoluta bst; hay blen bytes */
    u32 *head, *prev; long pos, blk;       /* pos: siguiente byte por tratar; blk: donde empieza el bloque */
    u16 *sl, *sd; int ns;
    int chain, lazy, nice, mav, plen, pdist; int ended;
} dfl_t;

static void dfo_byte(dfl_t *d, u8 b) { if (d->on == d->ocap) { d->ocap = d->ocap * 2 + 65536; d->out = realloc(d->out, d->ocap); } d->out[d->on++] = b; }
static void dfo_bits(dfl_t *d, u32 v, int n) { d->bb |= v << d->bc; d->bc += n; while (d->bc >= 8) { dfo_byte(d, d->bb); d->bb >>= 8; d->bc -= 8; } }
static void dfo_align(dfl_t *d) { if (d->bc) dfo_bits(d, 0, 8 - d->bc); }
static u32 df_rev(u32 c, int n) { u32 r = 0; for (int i = 0; i < n; i++) r |= (c >> i & 1) << (n - 1 - i); return r; }

static const u16 LBASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const u8 LEXT[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const u16 DBASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const u8 DEXT[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
static const u8 CLORD[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
static int lcode(int l) { int c = 0; while (c < 28 && LBASE[c + 1] <= l) c++; return c; }
static int dcode(int x) { int c = 0; while (c < 29 && DBASE[c + 1] <= x) c++; return c; }
static u8 LC[259]; static u8 DCt[512]; static int df_tabs;
static void df_init_tabs(void) { if (df_tabs) return; df_tabs = 1; for (int l = 3; l <= 258; l++) LC[l] = lcode(l); for (int x = 1; x <= 256; x++) DCt[x - 1] = dcode(x); for (int x = 257; x <= 32768; x += 128) DCt[256 + ((x - 1) >> 7)] = dcode(x); }
static int dcf(int x) { return x <= 256 ? DCt[x - 1] : DCt[256 + ((x - 1) >> 7)]; }

/* longitudes de codigo Huffman de como mucho lim bits: si el arbol sale mas hondo se reducen las frecuencias */
static void df_lens(const u32 *f0, int n, int lim, u8 *len) {
    u32 f[300]; int nz = 0; for (int i = 0; i < n; i++) { f[i] = f0[i]; nz += f[i] != 0; len[i] = 0; }
    if (nz == 0) { f[0] = f[1] = 1; nz = 2; } else if (nz == 1) { for (int i = 0; i < n; i++) if (!f[i]) { f[i] = 1; break; } nz = 2; }
    for (;;) {
        /* arbol por el metodo de las dos colas (hojas ordenadas y nodos internos en orden de creacion) */
        int leaf[300], nl = 0; for (int i = 0; i < n; i++) if (f[i]) leaf[nl++] = i;
        for (int i = 1; i < nl; i++) { int v = leaf[i], j = i; while (j > 0 && f[leaf[j - 1]] > f[v]) { leaf[j] = leaf[j - 1]; j--; } leaf[j] = v; }
        u32 w[600]; int par[600], k = nl; for (int i = 0; i < nl; i++) w[i] = f[leaf[i]];
        int a = 0, b = nl; /* a: siguiente hoja; b: siguiente nodo interno sin usar */
        for (int m = 0; m < nl - 1; m++) { int x[2]; for (int t = 0; t < 2; t++) { if (a < nl && (b >= k || w[a] <= w[b])) x[t] = a++; else x[t] = b++; } w[k] = w[x[0]] + w[x[1]]; par[x[0]] = par[x[1]] = k; k++; }
        int dep[600]; dep[k - 1] = 0; int mx = 0; for (int i = k - 2; i >= 0; i--) { dep[i] = dep[par[i]] + 1; if (i < nl && dep[i] > mx) mx = dep[i]; }
        if (mx <= lim) { for (int i = 0; i < nl; i++) len[leaf[i]] = dep[i]; return; }
        for (int i = 0; i < n; i++) if (f[i]) f[i] = (f[i] >> 1) | 1;
    }
}
static void df_codes(const u8 *len, int n, u16 *code) {
    int cnt[16] = {0}, nx[16]; for (int i = 0; i < n; i++) cnt[len[i]]++; cnt[0] = 0; int c = 0; for (int b = 1; b < 16; b++) { c = (c + cnt[b - 1]) << 1; nx[b] = c; }
    for (int i = 0; i < n; i++) if (len[i]) code[i] = df_rev(nx[len[i]]++, len[i]); }

/* longitudes de los dos arboles en forma RLE (16 repetir, 17 y 18 ceros) */
static int df_rle(const u8 *l, int n, u8 *sym, u8 *ext) {
    int k = 0; for (int i = 0; i < n;) { int v = l[i], r = 1; while (i + r < n && l[i + r] == v) r++;
        if (v == 0 && r >= 3) { int t = MIN(r, 138); if (t >= 11) { sym[k] = 18; ext[k++] = t - 11; } else { sym[k] = 17; ext[k++] = t - 3; } i += t; }
        else if (v != 0 && r >= 4) { sym[k] = v; ext[k++] = 0; int t = MIN(r - 1, 6); sym[k] = 16; ext[k++] = t - 3; i += 1 + t; }
        else { sym[k] = v; ext[k++] = 0; i++; } }
    return k; }

static void df_block(dfl_t *d, int last) {
    long span = d->pos - d->blk; u32 lf[286] = {0}, df[30] = {0};
    for (int i = 0; i < d->ns; i++) { if (d->sd[i]) { lf[257 + LC[d->sl[i]]]++; df[dcf(d->sd[i])]++; } else lf[d->sl[i]]++; } lf[256] = 1;
    u8 ll[286], dl[30]; df_lens(lf, 286, 15, ll); df_lens(df, 30, 15, dl);
    int nll = 286; while (nll > 257 && !ll[nll - 1]) nll--; int ndl = 30; while (ndl > 1 && !dl[ndl - 1]) ndl--;
    u8 all[316], rs[316], rx[316]; memcpy(all, ll, nll); memcpy(all + nll, dl, ndl); int nr = df_rle(all, nll + ndl, rs, rx);
    u32 cf[19] = {0}; for (int i = 0; i < nr; i++) cf[rs[i]]++; u8 cl[19]; df_lens(cf, 19, 7, cl); int ncl = 19; while (ncl > 4 && !cl[CLORD[ncl - 1]]) ncl--;
    /* coste en bits de cada clase de bloque */
    u64 dyn = 3 + 14 + 3 * ncl, fix = 3; for (int i = 0; i < nr; i++) dyn += cl[rs[i]] + (rs[i] == 16 ? 2 : rs[i] == 17 ? 3 : rs[i] == 18 ? 7 : 0);
    for (int s = 0; s < 286; s++) if (lf[s]) { int e = s > 256 ? LEXT[s - 257] : 0; dyn += (u64)lf[s] * (ll[s] + e); fix += (u64)lf[s] * ((s < 144 ? 8 : s < 256 ? 9 : s < 280 ? 7 : 8) + e); }
    for (int s = 0; s < 30; s++) if (df[s]) { dyn += (u64)df[s] * (dl[s] + DEXT[s]); fix += (u64)df[s] * (5 + DEXT[s]); }
    u64 sto = span ? 0 : ~0ULL; if (span) { sto = 3 + ((d->bc + 3) % 8 ? 8 - (d->bc + 3) % 8 : 0) + 32 + 8 * (u64)span; }
    if (span && sto <= dyn && sto <= fix) {   /* guardado: los bytes tal cual (span < 65536) */
        dfo_bits(d, last, 1); dfo_bits(d, 0, 2); dfo_align(d); dfo_bits(d, span & 0xFFFF, 16); dfo_bits(d, ~span & 0xFFFF, 16);
        const u8 *p = d->buf + (d->blk - d->bst); for (long i = 0; i < span; i++) dfo_byte(d, p[i]); }
    else {
        u8 L[288], D[30]; u16 LCd[288], DCd[30];
        if (fix <= dyn) { for (int i = 0; i < 288; i++) L[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8; for (int i = 0; i < 30; i++) D[i] = 5; df_codes(L, 288, LCd); df_codes(D, 30, DCd); dfo_bits(d, last, 1); dfo_bits(d, 1, 2); }
        else { memcpy(L, ll, 286); memcpy(D, dl, 30); df_codes(L, 286, LCd); df_codes(D, 30, DCd); u16 cc[19]; df_codes(cl, 19, cc);
            dfo_bits(d, last, 1); dfo_bits(d, 2, 2); dfo_bits(d, nll - 257, 5); dfo_bits(d, ndl - 1, 5); dfo_bits(d, ncl - 4, 4);
            for (int i = 0; i < ncl; i++) dfo_bits(d, cl[CLORD[i]], 3);
            for (int i = 0; i < nr; i++) { dfo_bits(d, cc[rs[i]], cl[rs[i]]); if (rs[i] == 16) dfo_bits(d, rx[i], 2); else if (rs[i] == 17) dfo_bits(d, rx[i], 3); else if (rs[i] == 18) dfo_bits(d, rx[i], 7); } }
        for (int i = 0; i < d->ns; i++) {
            if (!d->sd[i]) { dfo_bits(d, LCd[d->sl[i]], L[d->sl[i]]); continue; }
            int l = d->sl[i], c = LC[l], x = d->sd[i], e = dcf(x);
            dfo_bits(d, LCd[257 + c], L[257 + c]); if (LEXT[c]) dfo_bits(d, l - LBASE[c], LEXT[c]);
            dfo_bits(d, DCd[e], D[e]); if (DEXT[e]) dfo_bits(d, x - DBASE[e], DEXT[e]); }
        dfo_bits(d, LCd[256], L[256]); }
    d->ns = 0; d->blk = d->pos;
    /* deslizar la ventana: se conservan los ultimos DW bytes ya tratados y lo que queda por tratar */
    if (d->pos - d->bst > 2 * DW) { long keep = d->pos - DW, cut = keep - d->bst; memmove(d->buf, d->buf + cut, d->blen - cut); d->blen -= cut; d->bst = keep; }
}

static dfl_t *dfl_new(int level) {
    df_init_tabs(); dfl_t *d = calloc(1, sizeof *d); if (!d) return 0;
    d->buf = malloc(DCAP); d->head = calloc(DHS, 4); d->prev = calloc(DW, 4); d->sl = malloc(DSYM * 2); d->sd = malloc(DSYM * 2);
    if (!d->buf || !d->head || !d->prev || !d->sl || !d->sd) { free(d->buf); free(d->head); free(d->prev); free(d->sl); free(d->sd); free(d); return 0; }
    if (level <= 1) { d->chain = 8; d->lazy = 4; d->nice = 16; } else if (level == 2) { d->chain = 128; d->lazy = 32; d->nice = 128; } else { d->chain = 2048; d->lazy = 258; d->nice = 258; }
    d->plen = DMIN - 1; return d;
}
static void dfl_free(dfl_t *d) { if (!d) return; free(d->buf); free(d->head); free(d->prev); free(d->sl); free(d->sd); free(d->out); free(d); }
static inline u32 df_hash(const u8 *p) { return ((u32)p[0] << 16 | (u32)p[1] << 8 | p[2]) * 2654435761u >> (32 - DHB); }
static inline u32 df_ins(dfl_t *d, long at) { const u8 *p = d->buf + (at - d->bst); u32 h = df_hash(p), o = d->head[h]; d->prev[at & DWM] = o; d->head[h] = (u32)(at + 1); return o; }
static void df_sym(dfl_t *d, int l, int x) { d->sl[d->ns] = l; d->sd[d->ns] = x; d->ns++; }
/* la coincidencia mas larga en pos partiendo de la cadena de hash cand (posicion + 1) */
static int df_match(dfl_t *d, u32 cand, int prevlen, int *dist) {
    long la = d->bst + d->blen - d->pos; int maxl = (int)MIN((long)DMAX, la), best = prevlen, ch = d->chain; if (prevlen >= d->lazy) ch >>= 2;
    const u8 *s = d->buf + (d->pos - d->bst);
    while (cand && ch--) { u32 x = (u32)(d->pos + 1) - cand; if (x == 0 || x > DMAXD) break; const u8 *m = s - x;
        if (m[best] == s[best] && m[0] == s[0] && m[1] == s[1]) { int l = 2; while (l < maxl && m[l] == s[l]) l++; if (l > best) { best = l; *dist = x; if (l >= d->nice || l >= maxl) break; } }
        cand = d->prev[(cand - 1) & DWM]; }
    return best; }
/* trata lo que hay en el bufer; con fin == 0 se para cuando quedan menos de DMAX + DMIN bytes por mirar */
static void df_run(dfl_t *d, int fin) {
    for (;;) {
        long la = d->bst + d->blen - d->pos; if (la <= 0 || (!fin && la < DMAX + DMIN)) break;
        u32 hh = la >= DMIN ? df_ins(d, d->pos) : 0; int pl = d->plen, pd = d->pdist, ml = DMIN - 1, md = 0;
        if (hh && pl < d->lazy && la >= DMIN) ml = df_match(d, hh, DMIN - 1, &md);
        if (ml == DMIN && md > 4096) ml = DMIN - 1;   /* una de 3 tan lejos sale mas cara que tres literales */
        if (pl >= DMIN && ml <= pl) {   /* la del byte anterior era mejor: se emite y se saltan sus bytes */
            df_sym(d, pl, pd); long end = d->pos - 1 + pl; d->pos++;
            while (d->pos < end) { if (d->bst + d->blen - d->pos >= DMIN) df_ins(d, d->pos); d->pos++; }
            d->mav = 0; d->plen = DMIN - 1; }
        else { if (d->mav) df_sym(d, d->buf[d->pos - 1 - d->bst], 0); d->mav = 1; d->plen = ml; d->pdist = md; d->pos++; }
        if (d->ns >= DSYM - 2 || d->pos - d->blk >= DSPAN - DMAX) {   /* el literal pendiente es del bloque siguiente */
            if (d->mav) { d->pos--; df_block(d, 0); d->pos++; } else df_block(d, 0); }
    }
}
static void dfl_put(dfl_t *d, const u8 *p, long n) {
    while (n > 0) { long room = DCAP - d->blen; if (room <= 0) { df_run(d, 0); room = DCAP - d->blen; if (room <= 0) { df_block(d, 0); continue; } }
        long k = MIN(room, n); memcpy(d->buf + d->blen, p, k); d->blen += k; p += k; n -= k; df_run(d, 0); }
}
static void dfl_end(dfl_t *d) {
    df_run(d, 1); if (d->mav) { df_sym(d, d->buf[d->pos - 1 - d->bst], 0); d->mav = 0; } df_block(d, 1); dfo_align(d); d->ended = 1;
}
/* comodidad: comprimir un bufer entero */
static u8 *dfl_all(const u8 *p, long n, int level, long *on) {
    dfl_t *d = dfl_new(level); if (!d) return 0; dfl_put(d, p, n); dfl_end(d); u8 *o = d->out; *on = d->on; d->out = 0; dfl_free(d); return o; }
