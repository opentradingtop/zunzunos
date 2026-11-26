/* HTTP y decodificadores de imagen (BMP y PNG) */
#include "z.h"
int z_bmp_decode(const u8 *d, long n, u32 **out, int *w, int *h) {
    if (n < 54 || d[0] != 'B' || d[1] != 'M') return -1;
    u32 off = *(u32 *)(d + 10); int W = *(int *)(d + 18), H = *(int *)(d + 22), bpp = *(u16 *)(d + 28), comp = *(u32 *)(d + 30), top = H < 0; if (top) H = -H;
    if (W <= 0 || H <= 0 || W > 8192 || H > 8192 || (comp && comp != 3) || off >= (u32)n) return -1;
    if (bpp != 24 && bpp != 32 && bpp != 8) return -1;
    const u8 *pal = d + 14 + *(u32 *)(d + 14); u32 *p = malloc(W * H * 4); if (!p) return -1; int st = ((W * bpp + 31) / 32) * 4;
    for (int y = 0; y < H; y++) { long o = off + (long)(top ? y : H - 1 - y) * st; if (o + st > n) break; const u8 *s = d + o; u32 *t = p + y * W;
        for (int x = 0; x < W; x++) { if (bpp == 8) { const u8 *c = pal + s[x] * 4; t[x] = c[2] << 16 | c[1] << 8 | c[0]; } else { const u8 *c = s + x * (bpp / 8); t[x] = c[2] << 16 | c[1] << 8 | c[0]; } } }
    *out = p; *w = W; *h = H; return 0;
}
/* inflate */
typedef struct { const u8 *s; long n, p; u32 b; int c; u8 *o; long on, oc; } inf_t;
static int bits(inf_t *z, int k) { while (z->c < k) { if (z->p >= z->n) return -1; z->b |= (u32)z->s[z->p++] << z->c; z->c += 8; } int v = z->b & ((1u << k) - 1); z->b >>= k; z->c -= k; return k ? v : 0; }
typedef struct { u16 cnt[16], sym[320]; } huf_t;
static void huf_build(huf_t *h, const u8 *l, int n) { memset(h->cnt, 0, sizeof h->cnt); for (int i = 0; i < n; i++) h->cnt[l[i]]++; h->cnt[0] = 0; u16 of[16]; of[1] = 0; for (int i = 1; i < 15; i++) of[i + 1] = of[i] + h->cnt[i]; for (int i = 0; i < n; i++) if (l[i]) h->sym[of[l[i]]++] = i; }
static int huf_dec(inf_t *z, huf_t *h) { int code = 0, first = 0, idx = 0; for (int l = 1; l <= 15; l++) { int b = bits(z, 1); if (b < 0) return -1; code |= b; int c = h->cnt[l]; if (code - c < first) return h->sym[idx + code - first]; idx += c; first = (first + c) << 1; code <<= 1; } return -1; }
static int inflate(inf_t *z) {
    static const u16 lb[] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258}; static const u8 le[] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const u16 db[] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577}; static const u8 de[] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    huf_t *L = malloc(sizeof *L), *D = malloc(sizeof *D); int last, r = -1;
    do {
        last = bits(z, 1); int t = bits(z, 2); if (last < 0 || t < 0) goto out;
        if (t == 0) { z->b = 0; z->c = 0; if (z->p + 4 > z->n) goto out; int l = z->s[z->p] | z->s[z->p + 1] << 8; z->p += 4; if (z->p + l > z->n || z->on + l > z->oc) goto out; memcpy(z->o + z->on, z->s + z->p, l); z->on += l; z->p += l; continue; }
        if (t == 3) goto out;
        u8 ls[320];
        if (t == 1) { int i = 0; for (; i < 144; i++) ls[i] = 8; for (; i < 256; i++) ls[i] = 9; for (; i < 280; i++) ls[i] = 7; for (; i < 288; i++) ls[i] = 8; huf_build(L, ls, 288); for (i = 0; i < 30; i++) ls[i] = 5; huf_build(D, ls, 30); }
        else {
            int nl = bits(z, 5) + 257, nd = bits(z, 5) + 1, nc = bits(z, 4) + 4; static const u8 ord[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15}; memset(ls, 0, 19); for (int i = 0; i < nc; i++) ls[ord[i]] = bits(z, 3); huf_build(L, ls, 19);
            int i = 0; u8 cl[320]; while (i < nl + nd) { int s = huf_dec(z, L); if (s < 0) goto out; if (s < 16) cl[i++] = s; else { int pv = 0, rp; if (s == 16) { if (!i) goto out; pv = cl[i - 1]; rp = 3 + bits(z, 2); } else if (s == 17) rp = 3 + bits(z, 3); else rp = 11 + bits(z, 7); if (i + rp > nl + nd) goto out; while (rp--) cl[i++] = pv; } }
            huf_build(L, cl, nl); huf_build(D, cl + nl, nd);
        }
        for (;;) { int s = huf_dec(z, L); if (s < 0) goto out; if (s < 256) { if (z->on >= z->oc) goto out; z->o[z->on++] = s; } else if (s == 256) break; else { s -= 257; if (s >= 29) goto out; int l = lb[s] + bits(z, le[s]), ds = huf_dec(z, D); if (ds < 0 || ds >= 30) goto out; long dist = db[ds] + bits(z, de[ds]); if (dist > z->on || z->on + l > z->oc) goto out; for (int i = 0; i < l; i++, z->on++) z->o[z->on] = z->o[z->on - dist]; } }
    } while (!last);
    r = 0;
out: free(L); free(D); return r;
}
long z_inflate(const u8 *in, long n, u8 *out, long cap) { inf_t z = {in, n, 0, 0, 0, out, 0, cap}; return inflate(&z) ? -1 : z.on; }
int z_png_decode(const u8 *d, long n, u32 **out, int *w, int *h) {
    if (n < 33 || memcmp(d, "\x89PNG\r\n\x1a\n", 8)) return -1;
#define BE(p) ((u32)(p)[0] << 24 | (p)[1] << 16 | (p)[2] << 8 | (p)[3])
    long p = 8; int W = 0, H = 0, depth = 0, ct = 0, il = 0; u8 *idat = malloc(n); long il_n = 0; u8 pal[768]; u8 trns[256]; memset(trns, 255, 256);
    while (p + 12 <= n) { u32 l = BE(d + p); const u8 *t = d + p + 4, *c = d + p + 8; if (p + 12 + l > n) break;
        if (!memcmp(t, "IHDR", 4)) { W = BE(c); H = BE(c + 4); depth = c[8]; ct = c[9]; il = c[12]; } else if (!memcmp(t, "PLTE", 4)) memcpy(pal, c, MIN(l, 768U)); else if (!memcmp(t, "tRNS", 4) && ct == 3) memcpy(trns, c, MIN(l, 256U)); else if (!memcmp(t, "IDAT", 4)) { memcpy(idat + il_n, c, l); il_n += l; } else if (!memcmp(t, "IEND", 4)) break; p += 12 + l; }
    int ch = ct == 2 ? 3 : ct == 6 ? 4 : ct == 4 ? 2 : 1; if (W <= 0 || H <= 0 || W > 8192 || H > 8192 || il || (depth != 8 && !(ct == 3 && depth < 8) && !(ct == 0 && depth < 8))) { free(idat); return -1; }
    long st = (W * ch * depth + 7) / 8, tot = (st + 1) * H; u8 *raw = malloc(tot); inf_t z = {idat, il_n, 2, 0, 0, raw, 0, tot}; int e = inflate(&z); free(idat); if (e || z.on < tot) { free(raw); return -1; }
    int bpp = MAX(ch * depth / 8, 1); u8 *prev = calloc(1, st); u32 *o = malloc(W * H * 4);
    for (int y = 0; y < H; y++) { u8 *l = raw + y * (st + 1), f = *l++;
        for (long x = 0; x < st; x++) { int a = x >= bpp ? l[x - bpp] : 0, b = prev[x], c = x >= bpp ? prev[x - bpp] : 0, v = l[x], pr = 0;
            if (f == 1) pr = a; else if (f == 2) pr = b; else if (f == 3) pr = (a + b) / 2; else if (f == 4) { int q = a + b - c, pa = q > a ? q - a : a - q, pb = q > b ? q - b : b - q, pc = q > c ? q - c : c - q; pr = pa <= pb && pa <= pc ? a : pb <= pc ? b : c; } l[x] = v + pr; }
        memcpy(prev, l, st); u32 *t = o + y * W;
        for (int x = 0; x < W; x++) { int r, g, b2; if (depth < 8) { int bi = x * depth, v = (l[bi >> 3] >> (8 - depth - (bi & 7))) & ((1 << depth) - 1); if (ct == 3) { r = pal[v * 3]; g = pal[v * 3 + 1]; b2 = pal[v * 3 + 2]; } else r = g = b2 = v * 255 / ((1 << depth) - 1); } else if (ct == 3) { int v = l[x]; r = pal[v * 3]; g = pal[v * 3 + 1]; b2 = pal[v * 3 + 2]; } else if (ch >= 3) { r = l[x * ch]; g = l[x * ch + 1]; b2 = l[x * ch + 2]; if (ch == 4) { int al = l[x * 4 + 3]; r = (r * al + 255 * (255 - al)) / 255; g = (g * al + 255 * (255 - al)) / 255; b2 = (b2 * al + 255 * (255 - al)) / 255; } } else r = g = b2 = l[x * ch]; t[x] = r << 16 | g << 8 | b2; } }
    free(prev); free(raw); *out = o; *w = W; *h = H; return 0;
}
int z_jpeg_decode(const u8 *, long, u32 **, int *, int *); int z_gif_decode(const u8 *, long, u32 **, int *, int *);
int z_img_load(const char *p, u32 **o, int *w, int *h) { long n; u8 *d = z_readfile(p, &n); if (!d) return -2; int r = z_png_decode(d, n, o, w, h); if (r) r = z_bmp_decode(d, n, o, w, h); if (r) r = z_jpeg_decode(d, n, o, w, h); if (r == -1) r = z_gif_decode(d, n, o, w, h); free(d); return r; }
/* ===== JPEG basico (baseline, Huffman) ===== */
typedef struct { int minc[17], maxc[18], vp[17]; u8 val[256]; } jh_t;
typedef struct { const u8 *p, *e; u32 bits; int nb, mk; } jb_t;
static void jh_build(jh_t *h, const u8 *cnt, const u8 *v) { int code = 0, k = 0; for (int l = 1; l <= 16; l++) { h->vp[l] = k; h->minc[l] = code; code += cnt[l - 1]; k += cnt[l - 1]; h->maxc[l] = cnt[l - 1] ? code - 1 : -1; code <<= 1; } h->maxc[17] = 0x7FFFFFFF; memcpy(h->val, v, MIN(k, 256)); }
static int jbit(jb_t *b) { if (!b->nb) { if (b->p >= b->e || b->mk) return 0; u8 c = *b->p++; if (c == 0xFF) { if (*b->p == 0) b->p++; else { b->p--; b->mk = 1; return 0; } } b->bits = c; b->nb = 8; } b->nb--; return (b->bits >> b->nb) & 1; }
static int jdec(jb_t *b, jh_t *h) { int c = jbit(b), l = 1; while (c > h->maxc[l]) { c = (c << 1) | jbit(b); if (++l > 16) return 0; } return h->val[h->vp[l] + c - h->minc[l]]; }
static int jrecv(jb_t *b, int s) { int v = 0; for (int i = 0; i < s; i++) v = (v << 1) | jbit(b); if (s && v < (1 << (s - 1))) v -= (1 << s) - 1; return v; }
static const u8 ZZ[64] = {0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63};
static int jget(jb_t *b, int n) { int v = 0; while (n--) v = v << 1 | jbit(b); return v; }
/* JPEG baseline y progresivo: se guardan los coeficientes de todos los bloques y al final se hace la IDCT */
int z_jpeg_decode(const u8 *d, long n, u32 **out, int *ow, int *oh) {
    if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return -1;
    u16 qt[4][64]; jh_t *H = calloc(8, sizeof(jh_t)); int W = 0, Hh = 0, nc = 0, prog = 0, cid[3], ch_[3], cv[3], cq[3], cdc[3] = {0}, cac[3] = {0}, hmax = 1, vmax = 1, dri = 0, mx = 0, my = 0, bw[3] = {0}, bh[3] = {0}; long p = 2; int ok = -1; short *cf[3] = {0}; u8 *pl[3] = {0};
    while (p + 4 <= n) {
        if (d[p] != 0xFF) { p++; continue; } u8 m = d[p + 1]; p += 2; if (m == 0xD8 || (m >= 0xD0 && m <= 0xD7) || m == 0x01 || m == 0xFF) continue; if (m == 0xD9) break; int len = d[p] << 8 | d[p + 1]; const u8 *s = d + p + 2; if (p + len > n) break;
        if (m == 0xDB) { for (const u8 *q = s; q < d + p + len;) { int pq = *q >> 4, tq = *q & 3; q++; for (int i = 0; i < 64; i++) { qt[tq][ZZ[i]] = pq ? (q[0] << 8 | q[1]) : q[0]; q += pq ? 2 : 1; } } }
        else if (m == 0xC0 || m == 0xC1 || m == 0xC2) { prog = m == 0xC2; Hh = s[1] << 8 | s[2]; W = s[3] << 8 | s[4]; nc = s[5]; if ((nc != 1 && nc != 3) || !W || !Hh || W > 8192 || Hh > 8192) goto out;
            for (int i = 0; i < nc; i++) { cid[i] = s[6 + i * 3]; ch_[i] = s[7 + i * 3] >> 4; cv[i] = s[7 + i * 3] & 15; cq[i] = s[8 + i * 3] & 3; if (ch_[i] > hmax) hmax = ch_[i]; if (cv[i] > vmax) vmax = cv[i]; }
            mx = (W + 8 * hmax - 1) / (8 * hmax); my = (Hh + 8 * vmax - 1) / (8 * vmax); for (int c = 0; c < nc; c++) { bw[c] = mx * ch_[c]; bh[c] = my * cv[c]; cf[c] = calloc((long)bw[c] * bh[c] * 64, sizeof(short)); if (!cf[c]) goto out; } }
        else if (m == 0xC3 || (m >= 0xC5 && m <= 0xCF && m != 0xC8 && m != 0xCC)) { ok = -2; goto out; }
        else if (m == 0xC4) { for (const u8 *q = s; q < d + p + len;) { int tc = *q >> 4, th = *q & 3; int tot = 0; for (int i = 0; i < 16; i++) tot += q[1 + i]; jh_build(&H[tc * 4 + th], q + 1, q + 17); q += 17 + tot; } }
        else if (m == 0xDD) dri = s[0] << 8 | s[1];
        else if (m == 0xDA && cf[0]) {
            int ns = s[0], sc[3]; for (int i = 0; i < ns && i < 3; i++) for (int c = 0; c < nc; c++) if (cid[c] == s[1 + i * 2]) { sc[i] = c; cdc[c] = s[2 + i * 2] >> 4; cac[c] = s[2 + i * 2] & 15; }
            const u8 *t = s + 1 + 2 * ns; int Ss = prog ? t[0] : 0, Se = prog ? t[1] : 63, Ah = prog ? t[2] >> 4 : 0, Al = prog ? t[2] & 15 : 0;
            jb_t b = {t + 3, d + n, 0, 0, 0}; int pred[3] = {0}, cnt = 0, eob = 0;
            /* una unidad: bloque (c, bx, by) */
            #define BLK(c, bx, by) (cf[c] + ((long)(by) * bw[c] + (bx)) * 64)
            int units, ux = 1; if (ns == 1) { int c = sc[0], cw = ((W * ch_[c] + hmax - 1) / hmax + 7) / 8, chh = ((Hh * cv[c] + vmax - 1) / vmax + 7) / 8; ux = cw; units = cw * chh; } else units = mx * my;
            for (int u = 0; u < units; u++) {
                if (dri && cnt == dri) { b.nb = 0; b.mk = 0; while (b.p + 1 < b.e && !(b.p[0] == 0xFF && b.p[1] >= 0xD0 && b.p[1] <= 0xD7)) b.p++; if (b.p + 1 < b.e) b.p += 2; pred[0] = pred[1] = pred[2] = 0; eob = 0; cnt = 0; } cnt++;
                for (int k = 0; k < ns; k++) { int c = sc[k], nby = ns == 1 ? 1 : cv[c], nbx = ns == 1 ? 1 : ch_[c];
                    for (int by = 0; by < nby; by++) for (int bx = 0; bx < nbx; bx++) {
                        short *co = ns == 1 ? BLK(c, u % ux, u / ux) : BLK(c, (u % mx) * ch_[c] + bx, (u / mx) * cv[c] + by);
                        if (Ss == 0) { if (!Ah) { int tt = jdec(&b, &H[cdc[c]]); pred[c] += jrecv(&b, tt & 15); co[0] = pred[c] * (1 << Al); } else if (jbit(&b)) co[0] |= 1 << Al;
                            if (prog) continue;
                            for (int kk = 1; kk < 64;) { int rs = jdec(&b, &H[4 + cac[c]]), r = rs >> 4, sz = rs & 15; if (sz) { kk += r; if (kk > 63) break; co[ZZ[kk]] = jrecv(&b, sz); kk++; } else { if (r != 15) break; kk += 16; } }
                            continue; }
                        if (!Ah) { if (eob) { eob--; continue; } for (int kk = Ss; kk <= Se; kk++) { int rs = jdec(&b, &H[4 + cac[c]]), r = rs >> 4, sz = rs & 15; if (!sz) { if (r < 15) { eob = (1 << r) - 1; if (r) eob += jget(&b, r); break; } kk += 15; } else { kk += r; if (kk > 63) break; co[ZZ[kk]] = jrecv(&b, sz) * (1 << Al); } } continue; }
                        int p1 = 1 << Al, m1 = -1 * (1 << Al);
                        if (eob) { eob--; for (int kk = Ss; kk <= Se; kk++) { short *q = &co[ZZ[kk]]; if (*q && jbit(&b) && !(*q & p1)) *q += *q >= 0 ? p1 : m1; } continue; }
                        int kk = Ss; do { int rs = jdec(&b, &H[4 + cac[c]]), r = rs >> 4, sz = rs & 15, val = 0;
                            if (!sz) { if (r < 15) { eob = (1 << r) - 1; if (r) eob += jget(&b, r); r = 64; } } else val = jbit(&b) ? p1 : m1;
                            while (kk <= Se) { short *q = &co[ZZ[kk++]]; if (*q) { if (jbit(&b) && !(*q & p1)) *q += *q >= 0 ? p1 : m1; } else { if (r == 0) { *q = val; break; } r--; } }
                        } while (kk <= Se);
                    } }
            }
            #undef BLK
            if (!prog) break;
            p = b.p - d; continue;
        }
        p += len;
    }
    if (!cf[0]) goto out;
    { static float ct[8][8]; for (int u = 0; u < 8; u++) for (int x = 0; x < 8; x++) ct[u][x] = (u ? 0.5f : 0.35355339f) * z_cos((2 * x + 1) * u * 3.14159265358979 / 16);
      for (int c = 0; c < nc; c++) { pl[c] = malloc((long)bw[c] * 8 * bh[c] * 8); int pw = bw[c] * 8;
        for (int by = 0; by < bh[c]; by++) for (int bx = 0; bx < bw[c]; bx++) { short *q = cf[c] + ((long)by * bw[c] + bx) * 64; float co[64], tmp[64]; for (int i = 0; i < 64; i++) co[i] = q[i] * qt[cq[c]][i];
            for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) { float sm = 0; for (int u = 0; u < 8; u++) sm += ct[u][x] * co[y * 8 + u]; tmp[y * 8 + x] = sm; }
            u8 *dst = pl[c] + (long)by * 8 * pw + bx * 8; for (int x = 0; x < 8; x++) for (int y = 0; y < 8; y++) { float sm = 0; for (int v = 0; v < 8; v++) sm += ct[v][y] * tmp[v * 8 + x]; int vv = (int)(sm + 128.5f); dst[y * pw + x] = vv < 0 ? 0 : vv > 255 ? 255 : vv; } } }
      u32 *o = malloc((long)W * Hh * 4); int pw0 = bw[0] * 8, pw1 = nc > 1 ? bw[1] * 8 : 0, pw2 = nc > 2 ? bw[2] * 8 : 0;
      for (int y = 0; y < Hh; y++) for (int x = 0; x < W; x++) { int Y = pl[0][(long)(y * cv[0] / vmax) * pw0 + x * ch_[0] / hmax]; if (nc == 1) { o[(long)y * W + x] = Y * 0x010101; continue; } int cb = pl[1][(long)(y * cv[1] / vmax) * pw1 + x * ch_[1] / hmax] - 128, cr = pl[2][(long)(y * cv[2] / vmax) * pw2 + x * ch_[2] / hmax] - 128; int r = Y + (91881 * cr >> 16), g = Y - ((22554 * cb + 46802 * cr) >> 16), bl = Y + (116130 * cb >> 16); r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; bl = bl < 0 ? 0 : bl > 255 ? 255 : bl; o[(long)y * W + x] = r << 16 | g << 8 | bl; }
      *out = o; *ow = W; *oh = Hh; ok = 0; }
out: for (int c = 0; c < 3; c++) { free(pl[c]); free(cf[c]); } free(H); return ok;
}
/* ===== GIF (primer fotograma) ===== */
int z_gif_decode(const u8 *d, long n, u32 **out, int *ow, int *oh) {
    if (n < 13 || memcmp(d, "GIF8", 4)) return -1; int W = d[6] | d[7] << 8, H = d[8] | d[9] << 8, f = d[10]; if (!W || !H || W > 8192 || H > 8192) return -1; long p = 13; const u8 *gct = f & 0x80 ? d + 13 : 0; if (gct) p += 3LL << ((f & 7) + 1); int tr_ = -1;
    u32 *o = malloc(W * H * 4); for (int i = 0; i < W * H; i++) o[i] = 0xFFFFFF;
    while (p < n) { u8 b = d[p++];
        if (b == 0x21) { u8 l = d[p++]; if (l == 0xF9 && d[p] >= 4 && (d[p + 1] & 1)) tr_ = d[p + 4]; while (p < n && d[p]) p += d[p] + 1; p++; continue; }
        if (b != 0x2C) break; int x0 = d[p] | d[p + 1] << 8, y0 = d[p + 2] | d[p + 3] << 8, iw = d[p + 4] | d[p + 5] << 8, ih = d[p + 6] | d[p + 7] << 8, lf = d[p + 8]; p += 9; const u8 *pal = gct; if (lf & 0x80) { pal = d + p; p += 3LL << ((lf & 7) + 1); } if (!pal) break;
        int mcs = d[p++]; u8 *data = malloc(n); long dl = 0; while (p < n && d[p]) { memcpy(data + dl, d + p + 1, d[p]); dl += d[p]; p += d[p] + 1; } p++;
        u16 *pre = malloc(4096 * 2); u8 *suf = malloc(4096), *stk = malloc(4097); int clr = 1 << mcs, eoi = clr + 1, cs = mcs + 1, nx = clr + 2, old = -1, first = 0; long bp = 0, px = 0, tot = (long)iw * ih;
        for (int i = 0; i < clr; i++) { pre[i] = 0xFFFF; suf[i] = i; }
        int pass = 0, row = 0, col = 0; static const int ps[4] = {0, 4, 2, 1}, pi[4] = {8, 8, 4, 2};
        #define PUT(ix) do { if (px < tot) { int yy = (lf & 0x40) ? row : (int)(px / iw), xx = (lf & 0x40) ? col : (int)(px % iw); if ((ix) != tr_ && x0 + xx < W && y0 + yy < H) o[(y0 + yy) * W + x0 + xx] = pal[(ix) * 3] << 16 | pal[(ix) * 3 + 1] << 8 | pal[(ix) * 3 + 2]; px++; if (lf & 0x40) { if (++col == iw) { col = 0; row += pi[pass]; while (row >= ih && pass < 3) { pass++; row = ps[pass]; } } } } } while (0)
        while (bp + cs <= dl * 8 && px < tot) { int c = 0; for (int i = 0; i < cs; i++, bp++) c |= ((data[bp >> 3] >> (bp & 7)) & 1) << i;
            if (c == clr) { cs = mcs + 1; nx = clr + 2; old = -1; continue; } if (c == eoi) break;
            if (old < 0) { PUT(c); old = first = c; continue; } int in = c, sp = 0; if (c >= nx) { stk[sp++] = first; c = old; } while (c >= clr && sp < 4096) { stk[sp++] = suf[c]; c = pre[c]; } stk[sp++] = c; first = c; while (sp) { int v_ = stk[--sp]; PUT(v_); }
            if (nx < 4096) { pre[nx] = old; suf[nx] = first; nx++; if (nx == (1 << cs) && cs < 12) cs++; } old = in; }
        free(pre); free(suf); free(stk); free(data); break; }
    *out = o; *ow = W; *oh = H; return 0;
}
/* ===== PNG sin compresion (para guardar) ===== */
static void be32(u8 *p, u32 v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
int z_png_encode(const u32 *px, int w, int h, u8 **out, long *n) {
    long raw = (long)(w * 3 + 1) * h, blocks = (raw + 65534) / 65535, zl = 2 + raw + blocks * 5 + 4, tot = 8 + 25 + 12 + zl + 12; u8 *o = malloc(tot), *q = o; memcpy(q, "\x89PNG\r\n\x1a\n", 8); q += 8;
    be32(q, 13); memcpy(q + 4, "IHDR", 4); be32(q + 8, w); be32(q + 12, h); q[16] = 8; q[17] = 2; q[18] = q[19] = q[20] = 0; be32(q + 21, z_crc32(q + 4, 17)); q += 25;
    be32(q, zl); memcpy(q + 4, "IDAT", 4); u8 *z = q + 8, *zp = z; *zp++ = 0x78; *zp++ = 1; u32 a = 1, b = 0; long left = raw, row = 0, col = 0;
    while (left) { long c = MIN(left, 65535LL); *zp++ = left == c; *zp++ = c; *zp++ = c >> 8; *zp++ = ~c; *zp++ = ~c >> 8; for (long i = 0; i < c; i++) { u8 v; if (col == 0) v = 0; else { u32 pxl = px[row * w + (col - 1) / 3]; int k = (col - 1) % 3; v = pxl >> (16 - 8 * k); } *zp++ = v; a = (a + v) % 65521; b = (b + a) % 65521; if (++col == w * 3 + 1) { col = 0; row++; } } left -= c; }
    be32(zp, b << 16 | a); zp += 4; be32(zp, z_crc32(q + 4, 4 + zl)); q = zp + 4; be32(q, 0); memcpy(q + 4, "IEND", 4); be32(q + 8, z_crc32(q + 4, 4)); q += 12; *out = o; *n = q - o; return 0;
}
