/* Alquimia: imagenes. Lee PNG, JPEG, BMP y GIF (decodificadores del sistema) y ademas PPM/PGM, TGA e ICO.
   Escribe PNG, JPEG, BMP, GIF (corte por la mediana a 256 colores, difusion de error y LZW propio), ICO (varios
   tamanos), TGA, PPM, PDF y SVG. Los pixeles van como 0x00RRGGBB (las transparencias del PNG llegan ya sobre blanco). */
typedef struct { u32 *px; int w, h; } zimg;
static void img_free(zimg *m) { free(m->px); m->px = 0; m->w = m->h = 0; }
static int ppm_tok(const u8 *d, long n, long *p) { while (*p < n) { if (d[*p] == '#') { while (*p < n && d[*p] != '\n') (*p)++; } else if (isspace(d[*p])) (*p)++; else break; } long v = 0; int any = 0; while (*p < n && isdigit(d[*p])) { v = v * 10 + d[(*p)++] - '0'; any = 1; } return any ? (int)MIN(v, 100000L) : -1; }
static int ppm_decode(const u8 *d, long n, zimg *m) {
    if (n < 3 || d[0] != 'P' || d[1] < '1' || d[1] > '6') return -1; int t = d[1] - '0'; long p = 2; int W = ppm_tok(d, n, &p), H = ppm_tok(d, n, &p), mx = t == 1 || t == 4 ? 1 : ppm_tok(d, n, &p);
    if (W <= 0 || H <= 0 || mx <= 0 || (long)W * H > 64L << 20) return -1; p++; m->px = malloc((long)W * H * 4); if (!m->px) return -1; m->w = W; m->h = H;
    for (long i = 0; i < (long)W * H; i++) { int r, g, b;
        if (t == 6 || t == 5) { int bp = mx > 255 ? 2 : 1, c = t == 6 ? 3 : 1; if (p + bp * c > n) { r = g = b = 0; } else { int v[3]; for (int k = 0; k < c; k++) { v[k] = bp == 2 ? d[p] << 8 | d[p + 1] : d[p]; p += bp; } r = v[0]; g = c == 3 ? v[1] : r; b = c == 3 ? v[2] : r; } }
        else if (t == 4) { long y = i / W, x = i % W, rb = (W + 7) / 8; long o = p + y * rb + x / 8; int bit = o < n ? d[o] >> (7 - x % 8) & 1 : 0; r = g = b = bit ? 0 : mx; }
        else if (t == 3) { r = ppm_tok(d, n, &p); g = ppm_tok(d, n, &p); b = ppm_tok(d, n, &p); }
        else { r = ppm_tok(d, n, &p); if (t == 1) r = r == 1 ? 0 : mx; g = b = r; }
        r = MAX(r, 0) * 255 / mx; g = MAX(g, 0) * 255 / mx; b = MAX(b, 0) * 255 / mx; m->px[i] = (u32)MIN(r, 255) << 16 | MIN(g, 255) << 8 | MIN(b, 255); }
    return 0; }
static u32 over_white(u32 r, u32 g, u32 b, u32 a) { return ((r * a + 255 * (255 - a)) / 255) << 16 | ((g * a + 255 * (255 - a)) / 255) << 8 | ((b * a + 255 * (255 - a)) / 255); }
static int tga_decode(const u8 *d, long n, zimg *m) {
    if (n < 18) return -1; int idl = d[0], cmt = d[1], t = d[2], W = d[12] | d[13] << 8, H = d[14] | d[15] << 8, bpp = d[16], top = d[17] & 0x20;
    if (cmt || (t != 2 && t != 3 && t != 10 && t != 11) || W <= 0 || H <= 0 || (bpp != 8 && bpp != 24 && bpp != 32 && bpp != 16)) return -1;
    int bp = bpp / 8; long p = 18 + idl, np = (long)W * H; u32 *o = malloc(np * 4); if (!o) return -1;
    for (long i = 0; i < np;) { int cnt = 1, rle = 0; if (t >= 9) { if (p >= n) break; u8 c = d[p++]; cnt = (c & 127) + 1; rle = c & 128; }
        for (int k = 0; k < cnt && i < np; k++, i++) { if (p + bp > n) { o[i] = 0xFFFFFF; continue; } const u8 *q = d + p; u32 c;
            if (bp == 1) c = q[0] * 0x010101u; else if (bp == 2) { u32 v = q[0] | q[1] << 8; c = (v >> 10 & 31) * 255 / 31 << 16 | (v >> 5 & 31) * 255 / 31 << 8 | (v & 31) * 255 / 31; }
            else c = bp == 4 ? over_white(q[2], q[1], q[0], q[3]) : (u32)q[2] << 16 | q[1] << 8 | q[0];
            long y = i / W, x = i % W; o[(top ? y : H - 1 - y) * W + x] = c; if (!rle || k == cnt - 1) p += bp; } }
    m->px = o; m->w = W; m->h = H; return 0; }
/* ICO: la entrada mas grande; PNG dentro o mapa DIB de 32/24 bits */
static int ico_decode(const u8 *d, long n, zimg *m) {
    if (n < 22 || d[0] || d[1] || d[2] != 1 || d[3]) return -1; int cnt = d[4] | d[5] << 8, best = -1, bw = 0;
    for (int i = 0; i < cnt && 6 + 16 * i + 16 <= n; i++) { const u8 *e = d + 6 + 16 * i; int w = e[0] ? e[0] : 256; if (w > bw) { bw = w; best = i; } } if (best < 0) return -1;
    const u8 *e = d + 6 + 16 * best; u32 sz = G32(e + 8), off = G32(e + 12); if ((long)off + sz > n) return -1; const u8 *q = d + off;
    if (sz > 8 && q[0] == 0x89 && q[1] == 'P') return z_png_decode(q, sz, &m->px, &m->w, &m->h);
    int W = G32(q + 4), H = (int)G32(q + 8) / 2, bpp = q[14] | q[15] << 8; if (W <= 0 || H <= 0 || W > 1024 || (bpp != 32 && bpp != 24)) return -1;
    m->px = malloc((long)W * H * 4); m->w = W; m->h = H; int rb = (W * bpp / 8 + 3) & ~3; const u8 *pix = q + G32(q);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { const u8 *s = pix + (long)(H - 1 - y) * rb + x * (bpp / 8); if (s + 3 > d + n) { m->px[y * W + x] = 0xFFFFFF; continue; } m->px[y * W + x] = bpp == 32 ? over_white(s[2], s[1], s[0], s[3]) : (u32)s[2] << 16 | s[1] << 8 | s[0]; }
    return 0; }
static int img_decode(const u8 *d, long n, zimg *m) {
    memset(m, 0, sizeof *m); if (n < 4) return -1; int r = -1;
    if (d[0] == 0x89 && d[1] == 'P') r = z_png_decode(d, n, &m->px, &m->w, &m->h); else if (d[0] == 0xFF && d[1] == 0xD8) r = z_jpeg_decode(d, n, &m->px, &m->w, &m->h);
    else if (d[0] == 'B' && d[1] == 'M') r = z_bmp_decode(d, n, &m->px, &m->w, &m->h); else if (!memcmp(d, "GIF8", 4)) r = z_gif_decode(d, n, &m->px, &m->w, &m->h);
    else if (d[0] == 'P' && d[1] >= '1' && d[1] <= '6') r = ppm_decode(d, n, m); else if (!d[0] && !d[1] && d[2] == 1 && !d[3]) r = ico_decode(d, n, m); else r = tga_decode(d, n, m);
    if (r || !m->px || m->w <= 0 || m->h <= 0) { img_free(m); return -1; } return 0; }
/* cambio de tamano: promedio por area al reducir, bilineal al ampliar */
static int img_resize(zimg *m, int W, int H) {
    if (W == m->w && H == m->h) return 0; if (W <= 0 || H <= 0 || (long)W * H > 64L << 20) return -1; u32 *o = malloc((long)W * H * 4); if (!o) return -1;
    if (W <= m->w && H <= m->h) { for (int y = 0; y < H; y++) { int y0 = (int)((long)y * m->h / H), y1 = MAX(y0 + 1, (int)((long)(y + 1) * m->h / H));
            for (int x = 0; x < W; x++) { int x0 = (int)((long)x * m->w / W), x1 = MAX(x0 + 1, (int)((long)(x + 1) * m->w / W)); u32 r = 0, g = 0, b = 0, c = 0;
                for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++) { u32 p = m->px[(long)yy * m->w + xx]; r += p >> 16 & 255; g += p >> 8 & 255; b += p & 255; c++; } o[(long)y * W + x] = (r / c) << 16 | (g / c) << 8 | b / c; } } }
    else for (int y = 0; y < H; y++) { long fy = MAX(0L, ((long)y * 2 + 1) * m->h * 128 / H - 128); int y0 = (int)MIN(fy >> 8, (long)m->h - 1), y1 = MIN(y0 + 1, m->h - 1), wy = fy & 255;
        for (int x = 0; x < W; x++) { long fx = MAX(0L, ((long)x * 2 + 1) * m->w * 128 / W - 128); int x0 = (int)MIN(fx >> 8, (long)m->w - 1), x1 = MIN(x0 + 1, m->w - 1), wx = fx & 255; u32 a = m->px[(long)y0 * m->w + x0], b = m->px[(long)y0 * m->w + x1], c = m->px[(long)y1 * m->w + x0], dd = m->px[(long)y1 * m->w + x1], v = 0;
            for (int s = 0; s < 24; s += 8) { int t = ((a >> s & 255) * (256 - wx) + (b >> s & 255) * wx) * (256 - wy) + ((c >> s & 255) * (256 - wx) + (dd >> s & 255) * wx) * wy; v |= (u32)(t >> 16) << s; } o[(long)y * W + x] = v; } }
    free(m->px); m->px = o; m->w = W; m->h = H; return 0; }
/* tamano final segun las opciones: porcentaje y/o caja maxima, conservando la proporcion */
static void img_fit(zimg *m, int pct, int mw, int mh) { long W = m->w, H = m->h; if (pct > 0 && pct != 100) { W = MAX(1L, W * pct / 100); H = MAX(1L, H * pct / 100); }
    if (mw > 0 && W > mw) { H = MAX(1L, H * mw / W); W = mw; } if (mh > 0 && H > mh) { W = MAX(1L, W * mh / H); H = mh; } img_resize(m, (int)W, (int)H); }

static void bput16(sb_t *b, u32 v) { s16(b, v); } static void bput32(sb_t *b, u32 v) { s32(b, v); }
static void enc_bmp(const zimg *m, sb_t *b) { int rb = (m->w * 3 + 3) & ~3; u32 sz = 54 + rb * m->h; sb_add(b, "BM", 2); bput32(b, sz); bput32(b, 0); bput32(b, 54); bput32(b, 40); bput32(b, m->w); bput32(b, m->h); bput16(b, 1); bput16(b, 24); bput32(b, 0); bput32(b, rb * m->h); bput32(b, 2835); bput32(b, 2835); bput32(b, 0); bput32(b, 0);
    u8 *row = calloc(1, rb); for (int y = m->h - 1; y >= 0; y--) { for (int x = 0; x < m->w; x++) { u32 p = m->px[(long)y * m->w + x]; row[x * 3] = p; row[x * 3 + 1] = p >> 8; row[x * 3 + 2] = p >> 16; } sb_add(b, row, rb); } free(row); }
static void enc_tga(const zimg *m, sb_t *b) { u8 h[18] = {0, 0, 10}; h[12] = m->w; h[13] = m->w >> 8; h[14] = m->h; h[15] = m->h >> 8; h[16] = 24; h[17] = 0x20; sb_add(b, h, 18);
    for (int y = 0; y < m->h; y++) { const u32 *r = m->px + (long)y * m->w; for (int x = 0; x < m->w;) { int run = 1; while (x + run < m->w && run < 128 && r[x + run] == r[x]) run++;
            if (run > 1) { u8 c[4] = {0x80 | (run - 1), r[x], r[x] >> 8, r[x] >> 16}; sb_add(b, c, 4); x += run; }
            else { int k = 1; while (x + k < m->w && k < 128 && (x + k + 1 >= m->w || r[x + k] != r[x + k + 1])) k++; u8 c = k - 1; sb_add(b, &c, 1); for (int i = 0; i < k; i++) { u8 q[3] = {r[x + i], r[x + i] >> 8, r[x + i] >> 16}; sb_add(b, q, 3); } x += k; } } }
    sb_add(b, "\0\0\0\0\0\0\0\0TRUEVISION-XFILE.\0", 26); }
static void enc_ppm(const zimg *m, sb_t *b) { sb_f(b, "P6\n# Alquimia - ZunzunOS\n%d %d\n255\n", m->w, m->h); for (long i = 0; i < (long)m->w * m->h; i++) { u8 q[3] = {m->px[i] >> 16, m->px[i] >> 8, m->px[i]}; sb_add(b, q, 3); } }
static int enc_png(const zimg *m, sb_t *b) { u8 *o; long n; if (z_png_encode(m->px, m->w, m->h, &o, &n) || n <= 0) return -1; sb_add(b, o, n); free(o); return 0; }
static int enc_jpg(const zimg *m, int q, sb_t *b) { u8 *o; long n = z_jpeg_encode(m->px, m->w, m->h, q, &o); if (n <= 0) return -1; sb_add(b, o, n); free(o); return 0; }

/* ---------- GIF ---------- */
typedef struct { int lo[3], hi[3]; long n; int a, b; } qbox;   /* a..b: tramo del vector de colores */
static int qcmp_ch; static int qcmp(const void *x, const void *y) { u32 a = *(const u32 *)x, b = *(const u32 *)y; int s = 10 - 5 * qcmp_ch; return (int)((a >> s) & 31) - (int)((b >> s) & 31); }
/* paleta de como mucho 256 colores por corte por la mediana sobre un histograma de 15 bits */
static int gif_palette(const u32 *px, long np, u32 *pal, int *exact_) {
    *exact_ = 0;
    u32 *hist = calloc(32768, 4); if (!hist) return 0; for (long i = 0; i < np; i++) { u32 p = px[i]; hist[(p >> 19 & 31) << 10 | (p >> 11 & 31) << 5 | (p >> 3 & 31)]++; }
    u32 *v = malloc(32768 * 4); int nv = 0; for (int i = 0; i < 32768; i++) if (hist[i]) v[nv++] = i;
    if (nv <= 256) { /* pocos colores: exactos */ u32 *exact = 0; int ne = 0; exact = malloc(257 * 4);
        for (long i = 0; i < np && ne <= 256; i++) { u32 p = px[i] & 0xFFFFFF; int f = 0; for (int k = ne - 1; k >= 0 && k >= ne - 8; k--) if (exact[k] == p) { f = 1; break; } if (!f) { for (int k = 0; k < ne; k++) if (exact[k] == p) { f = 1; break; } } if (!f) exact[ne++] = p; }
        if (ne <= 256) { memcpy(pal, exact, ne * 4); free(exact); free(v); free(hist); *exact_ = 1; return ne; } free(exact); }
    qbox bx[256]; int nb = 1; bx[0].a = 0; bx[0].b = nv;
    for (;;) { int best = -1; long bs = -1;
        for (int i = 0; i < nb; i++) { qbox *q = &bx[i]; for (int c = 0; c < 3; c++) { q->lo[c] = 31; q->hi[c] = 0; } q->n = 0;
            for (int k = q->a; k < q->b; k++) { u32 c = v[k]; int ch[3] = {c >> 10 & 31, c >> 5 & 31, c & 31}; for (int t = 0; t < 3; t++) { q->lo[t] = MIN(q->lo[t], ch[t]); q->hi[t] = MAX(q->hi[t], ch[t]); } q->n += hist[c]; }
            int rg = MAX(q->hi[0] - q->lo[0], MAX(q->hi[1] - q->lo[1], q->hi[2] - q->lo[2])); long sc = (long)rg * (q->n > 0 ? 1 + (long)z_sqrt((double)q->n) : 0); if (q->b - q->a > 1 && sc > bs) { bs = sc; best = i; } }
        if (best < 0 || nb == 256) break;
        qbox *q = &bx[best]; int ch = 0, r0 = q->hi[0] - q->lo[0], r1 = q->hi[1] - q->lo[1], r2 = q->hi[2] - q->lo[2]; if (r1 * 6 >= r0 * 5 && r1 >= r2) ch = 1; else if (r2 > r0 && r2 > r1) ch = 2;   /* el verde pesa algo mas */
        qcmp_ch = ch; qsort(v + q->a, q->b - q->a, 4, qcmp); long half = q->n / 2, acc = 0; int cut = q->a;
        for (int k = q->a; k < q->b - 1; k++) { acc += hist[v[k]]; cut = k + 1; if (acc >= half) break; }
        bx[nb].a = cut; bx[nb].b = q->b; q->b = cut; nb++; }
    for (int i = 0; i < nb; i++) { u64 r = 0, g = 0, b = 0, n = 0; for (int k = bx[i].a; k < bx[i].b; k++) { u32 c = v[k], w = hist[c]; r += (u64)((c >> 10 & 31) * 8 + 4) * w; g += (u64)((c >> 5 & 31) * 8 + 4) * w; b += (u64)((c & 31) * 8 + 4) * w; n += w; }
        if (!n) n = 1; pal[i] = (u32)(r / n) << 16 | (u32)(g / n) << 8 | (u32)(b / n); }
    free(v); free(hist); return nb; }
static int pal_near(const u32 *pal, int np, int r, int g, int b) { int best = 0; i64 bd = (i64)1 << 40; for (int i = 0; i < np; i++) { int dr = (int)(pal[i] >> 16 & 255) - r, dg = (int)(pal[i] >> 8 & 255) - g, db = (int)(pal[i] & 255) - b; long d = 3L * dr * dr + 4L * dg * dg + 2L * db * db; if (d < bd) { bd = d; best = i; } } return best; }
/* pixeles a indices de la paleta, con difusion de error Floyd-Steinberg si dith */
static u8 *gif_index(const zimg *m, const u32 *pal, int np, int dith, int exact) {
    long N = (long)m->w * m->h; u8 *o = malloc(N); short *cache = malloc(32768 * 2); for (int i = 0; i < 32768; i++) cache[i] = -1;
    if (exact) { for (long i = 0; i < N; i++) { u32 p = m->px[i] & 0xFFFFFF; int k = 0; while (k < np - 1 && pal[k] != p) k++; o[i] = k; } free(cache); return o; }
    int *err = dith ? calloc((m->w + 2) * 2 * 3, sizeof(int)) : 0;
    for (int y = 0; y < m->h; y++) { int *cur = err ? err + (y & 1) * (m->w + 2) * 3 : 0, *nx = err ? err + ((y + 1) & 1) * (m->w + 2) * 3 : 0; if (nx) memset(nx, 0, (m->w + 2) * 3 * sizeof(int));
        for (int x = 0; x < m->w; x++) { u32 p = m->px[(long)y * m->w + x]; int c[3] = {p >> 16 & 255, p >> 8 & 255, p & 255};
            if (cur) for (int t = 0; t < 3; t++) { c[t] += cur[(x + 1) * 3 + t] / 16; c[t] = MAX(0, MIN(255, c[t])); }
            int key = (c[0] >> 3) << 10 | (c[1] >> 3) << 5 | c[2] >> 3; int k = cache[key]; if (k < 0) k = cache[key] = pal_near(pal, np, c[0], c[1], c[2]); o[(long)y * m->w + x] = k;
            if (cur) { int e[3] = {c[0] - (int)(pal[k] >> 16 & 255), c[1] - (int)(pal[k] >> 8 & 255), c[2] - (int)(pal[k] & 255)};
                for (int t = 0; t < 3; t++) { cur[(x + 2) * 3 + t] += e[t] * 7; nx[x * 3 + t] += e[t] * 3; nx[(x + 1) * 3 + t] += e[t] * 5; nx[(x + 2) * 3 + t] += e[t]; } } } }
    free(err); free(cache); return o; }
/* LZW de GIF: el ancho del codigo sube cuando el siguiente codigo libre llega a 2^ancho (como gif.h de C. Tangora) */
typedef struct { sb_t *b; u8 blk[256]; int bn; u32 acc; int nb; } gbits;
static void gb_flushblk(gbits *g) { if (!g->bn) return; u8 c = g->bn; sb_add(g->b, &c, 1); sb_add(g->b, g->blk, g->bn); g->bn = 0; }
static void gb_put(gbits *g, u32 code, int len) { g->acc |= code << g->nb; g->nb += len; while (g->nb >= 8) { g->blk[g->bn++] = g->acc; g->acc >>= 8; g->nb -= 8; if (g->bn == 255) gb_flushblk(g); } }
static void gif_lzw(sb_t *b, const u8 *ix, long n, int mcs) {
    u8 c = mcs; sb_add(b, &c, 1); gbits g = {b, {0}, 0, 0, 0}; int clr = 1 << mcs, eoi = clr + 1, size = mcs + 1, maxc = eoi;
    u16 *tree = calloc(4096 * 256, 2); gb_put(&g, clr, size); int cur = -1;
    for (long i = 0; i < n; i++) { int v = ix[i]; if (cur < 0) { cur = v; continue; } if (tree[cur * 256 + v]) { cur = tree[cur * 256 + v]; continue; }
        gb_put(&g, cur, size); tree[cur * 256 + v] = ++maxc; if (maxc >= (1 << size)) size++;
        if (maxc == 4095) { gb_put(&g, clr, size); memset(tree, 0, 4096 * 256 * 2); size = mcs + 1; maxc = eoi; } cur = v; }
    if (cur >= 0) gb_put(&g, cur, size); gb_put(&g, eoi, size); if (g.nb) { g.blk[g.bn++] = g.acc; if (g.bn == 255) gb_flushblk(&g); } gb_flushblk(&g); c = 0; sb_add(b, &c, 1); free(tree); }
static void gif_begin(sb_t *b, int w, int h, int loop) { sb_add(b, "GIF89a", 6); s16(b, w); s16(b, h); u8 f[3] = {0, 0, 0}; sb_add(b, f, 3);
    if (loop) { sb_add(b, "\x21\xFF\x0BNETSCAPE2.0\x03\x01\x00\x00\x00", 19); } }
static void gif_frame(sb_t *b, const zimg *m, int delay_cs, int dith) {
    u32 pal[256]; int exact = 0, np = gif_palette(m->px, (long)m->w * m->h, pal, &exact); if (np < 2) { pal[np] = np ? 0xFFFFFF ^ pal[0] : 0; np = 2; } int bits = 1; while ((1 << bits) < np) bits++;
    u8 gce[8] = {0x21, 0xF9, 4, 0, delay_cs, delay_cs >> 8, 0, 0}; sb_add(b, gce, 8);
    u8 id[10] = {0x2C, 0, 0, 0, 0, m->w, m->w >> 8, m->h, m->h >> 8, 0x80 | (bits - 1)}; sb_add(b, id, 10);
    for (int i = 0; i < (1 << bits); i++) { u32 c = i < np ? pal[i] : 0; u8 q[3] = {c >> 16, c >> 8, c}; sb_add(b, q, 3); }
    u8 *ix = gif_index(m, pal, np, dith && !exact, exact); gif_lzw(b, ix, (long)m->w * m->h, MAX(2, bits)); free(ix); }
static void enc_gif(const zimg *m, sb_t *b) { gif_begin(b, m->w, m->h, 0); gif_frame(b, m, 0, 1); u8 t = 0x3B; sb_add(b, &t, 1); }
/* ICO con 16, 32, 48, 64, 128 y 256 (los que no pasen del original): mapas de 32 bits y 256 en PNG */
static int enc_ico(const zimg *m0, sb_t *b) {
    static const int S[] = {16, 32, 48, 64, 128, 256}; int ns = 0, use[6]; int side = MAX(m0->w, m0->h); for (int i = 0; i < 6; i++) if (S[i] <= MAX(side, 32)) use[ns++] = S[i];
    sb_t body[6]; memset(body, 0, sizeof body);
    for (int i = 0; i < ns; i++) { int s = use[i]; zimg t; t.w = m0->w; t.h = m0->h; t.px = malloc((long)t.w * t.h * 4); memcpy(t.px, m0->px, (long)t.w * t.h * 4);
        int W = m0->w >= m0->h ? s : MAX(1, m0->w * s / m0->h), H = m0->h >= m0->w ? s : MAX(1, m0->h * s / m0->w); img_resize(&t, W, H); int ox = (s - W) / 2, oy = (s - H) / 2;
        if (s == 256) { zimg q; q.w = q.h = s; q.px = malloc((long)s * s * 4); for (long k = 0; k < (long)s * s; k++) q.px[k] = 0xFFFFFF; for (int y = 0; y < H; y++) memcpy(q.px + (long)(y + oy) * s + ox, t.px + (long)y * W, W * 4); sb_s(&body[i], ""); enc_png(&q, &body[i]); img_free(&q); }
        else { sb_t *o = &body[i]; sb_s(o, ""); s32(o, 40); s32(o, s); s32(o, s * 2); s16(o, 1); s16(o, 32); s32(o, 0); s32(o, s * s * 4); s32(o, 0); s32(o, 0); s32(o, 0); s32(o, 0);
            for (int y = s - 1; y >= 0; y--) for (int x = 0; x < s; x++) { int in = x >= ox && x < ox + W && y >= oy && y < oy + H; u32 p = in ? t.px[(long)(y - oy) * W + (x - ox)] : 0; u8 q[4] = {p, p >> 8, p >> 16, in ? 255 : 0}; sb_add(o, q, 4); }
            int mr = ((s + 31) / 32) * 4; u8 *mask = malloc(mr); for (int y = s - 1; y >= 0; y--) { memset(mask, 0, mr); for (int x = 0; x < s; x++) { int in = x >= ox && x < ox + W && y >= oy && y < oy + H; if (!in) mask[x / 8] |= 0x80 >> (x % 8); } sb_add(o, mask, mr); } free(mask); }
        img_free(&t); }
    s16(b, 0); s16(b, 1); s16(b, ns); u32 off = 6 + 16 * ns;
    for (int i = 0; i < ns; i++) { u8 e[8] = {use[i] == 256 ? 0 : use[i], use[i] == 256 ? 0 : use[i], 0, 0, 1, 0, 32, 0}; sb_add(b, e, 8); s32(b, body[i].n); s32(b, off); off += body[i].n; }
    for (int i = 0; i < ns; i++) { sb_add(b, body[i].p, body[i].n); free(body[i].p); } return 0; }
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static void b64_enc(sb_t *b, const u8 *d, long n, int wrap) { char q[4]; int col = 0; for (long i = 0; i < n; i += 3) { u32 v = d[i] << 16 | (i + 1 < n ? d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0); q[0] = B64[v >> 18]; q[1] = B64[v >> 12 & 63]; q[2] = i + 1 < n ? B64[v >> 6 & 63] : '='; q[3] = i + 2 < n ? B64[v & 63] : '='; sb_add(b, q, 4); col += 4; if (wrap && col >= wrap) { sb_s(b, "\n"); col = 0; } } if (wrap && col) sb_s(b, "\n"); }
static void enc_svg(const zimg *m, sb_t *b) { sb_t p = {0}; enc_png(m, &p); sb_f(b, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"%d\" height=\"%d\" viewBox=\"0 0 %d %d\">\n<image width=\"%d\" height=\"%d\" xlink:href=\"data:image/png;base64,", m->w, m->h, m->w, m->h, m->w, m->h); b64_enc(b, (u8 *)p.p, p.n, 0); sb_s(b, "\"/>\n</svg>\n"); free(p.p); }
static zpset pdf_set(int paper, int land) { zpset s; memset(&s, 0, sizeof s); s.pw = paper ? 612 : 595.28; s.ph = paper ? 792 : 841.89; s.paper = paper; s.land = land; s.copies = 1; s.color = 1; s.dpi = 300; return s; }
static int enc_pdf_img(const zimg *m, int paper, sb_t *b) { int land = m->w > m->h; zpset s = pdf_set(paper, land); zpdoc *d = zp_new(&s); zp_page(d); double mg = 28, aw = zp_pw(d) - 2 * mg, ah = zp_ph(d) - 2 * mg, k = MIN(aw / m->w, ah / m->h); if (k > 1.5) k = 1.5;
    double w = m->w * k, h = m->h * k; zp_image(d, (zp_pw(d) - w) / 2, (zp_ph(d) - h) / 2, w, h, m->px, m->w, m->h); u8 *o; long n = zp_render(d, &s, "pdf", &o); zp_free(d); if (n <= 0) return -1; sb_add(b, o, n); free(o); return 0; }
