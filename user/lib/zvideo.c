/* Video: codificador JPEG (baseline, 4:2:0, tablas estandar del anexo K) y archivos AVI con
   MJPEG y sonido PCM opcional (grabar la pantalla y reproducir esas grabaciones). */
#include "z.h"
/* ---------- JPEG ---------- */
static const u8 ZZ[64] = {0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63};
static const u8 QL[64] = {16,11,10,16,24,40,51,61,12,12,14,19,26,58,60,55,14,13,16,24,40,57,69,56,14,17,22,29,51,87,80,62,18,22,37,56,68,109,103,77,24,35,55,64,81,104,113,92,49,64,78,87,103,121,120,101,72,92,95,98,112,100,103,99};
static const u8 QC[64] = {17,18,24,47,99,99,99,99,18,21,26,66,99,99,99,99,24,26,56,99,99,99,99,99,47,66,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99};
static const u8 DCLB[16] = {0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0}, DCCB[16] = {0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0}, DCV[12] = {0,1,2,3,4,5,6,7,8,9,10,11};
static const u8 ACLB[16] = {0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d}, ACCB[16] = {0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77};
static const u8 ACLV[162] = {0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa};
static const u8 ACCV[162] = {0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa};
typedef struct { u16 code[256]; u8 len[256]; } huf_t;
static void huf_build(huf_t *h, const u8 *bits, const u8 *vals) { int k = 0, code = 0; for (int l = 1; l <= 16; l++) { for (int i = 0; i < bits[l - 1]; i++) { h->code[vals[k]] = code; h->len[vals[k]] = l; k++; code++; } code <<= 1; } }
typedef struct { u8 *p; long n, cap; u32 acc; int bits; } jw_t;
static void jb(jw_t *w, u8 b) { if (w->n == w->cap) { w->cap = w->cap * 2 + 65536; w->p = realloc(w->p, w->cap); } w->p[w->n++] = b; }
static void jbits(jw_t *w, u32 v, int n) { w->acc = w->acc << n | (v & ((1u << n) - 1)); w->bits += n; while (w->bits >= 8) { u8 b = w->acc >> (w->bits - 8); jb(w, b); if (b == 0xFF) jb(w, 0); w->bits -= 8; } }
static void j16(jw_t *w, int v) { jb(w, v >> 8); jb(w, v); }
/* DCT directa en coma flotante (algoritmo AAN); la escala final va en la cuantizacion */
static void fdct(float *d) {
    for (int p = 0; p < 2; p++) for (int i = 0; i < 8; i++) { float *r = p ? d + i : d + i * 8; int s = p ? 8 : 1;
        float t0 = r[0] + r[7 * s], t7 = r[0] - r[7 * s], t1 = r[s] + r[6 * s], t6 = r[s] - r[6 * s], t2 = r[2 * s] + r[5 * s], t5 = r[2 * s] - r[5 * s], t3 = r[3 * s] + r[4 * s], t4 = r[3 * s] - r[4 * s];
        float t10 = t0 + t3, t13 = t0 - t3, t11 = t1 + t2, t12 = t1 - t2; r[0] = t10 + t11; r[4 * s] = t10 - t11; float z1 = (t12 + t13) * 0.707106781f; r[2 * s] = t13 + z1; r[6 * s] = t13 - z1;
        t10 = t4 + t5; t11 = t5 + t6; t12 = t6 + t7; float z5 = (t10 - t12) * 0.382683433f, z2 = 0.541196100f * t10 + z5, z4 = 1.306562965f * t12 + z5, z3 = t11 * 0.707106781f, z11 = t7 + z3, z13 = t7 - z3;
        r[5 * s] = z13 + z2; r[3 * s] = z13 - z2; r[s] = z11 + z4; r[7 * s] = z11 - z4; } }
static const float AAN[8] = {1.0f, 1.387039845f, 1.306562965f, 1.175875602f, 1.0f, 0.785694958f, 0.541196100f, 0.275899379f};
static void put_val(jw_t *w, const huf_t *h, int sym) { jbits(w, h->code[sym], h->len[sym]); }
static int nbits(int v) { if (v < 0) v = -v; int n = 0; while (v) { n++; v >>= 1; } return n; }
static void block(jw_t *w, float *blk, const float *qd, int *pdc, const huf_t *dc, const huf_t *ac) {
    fdct(blk); int q[64]; for (int i = 0; i < 64; i++) { float v = blk[ZZ[i]] * qd[i]; q[i] = (int)(v < 0 ? v - 0.5f : v + 0.5f); }
    int diff = q[0] - *pdc; *pdc = q[0]; int nb = nbits(diff); put_val(w, dc, nb); if (nb) jbits(w, diff < 0 ? diff - 1 : diff, nb);
    int run = 0, last = 63; while (last > 0 && !q[last]) last--; for (int i = 1; i <= last; i++) { if (!q[i]) { if (++run == 16) { put_val(w, ac, 0xF0); run = 0; } continue; } int n2 = nbits(q[i]); put_val(w, ac, run << 4 | n2); jbits(w, q[i] < 0 ? q[i] - 1 : q[i], n2); run = 0; }
    if (last < 63) put_val(w, ac, 0); }
static void marker_q(jw_t *w, int id, const u8 *t) { j16(w, 0xFFDB); j16(w, 67); jb(w, id); for (int i = 0; i < 64; i++) jb(w, t[ZZ[i]]); }
static void marker_h(jw_t *w, int cls, int id, const u8 *bits, const u8 *vals, int nv) { j16(w, 0xFFC4); j16(w, 3 + 16 + nv); jb(w, cls << 4 | id); for (int i = 0; i < 16; i++) jb(w, bits[i]); for (int i = 0; i < nv; i++) jb(w, vals[i]); }
/* px: 0xRRGGBB; calidad 1..100. Devuelve el tamano o <0 */
long z_jpeg_encode(const u32 *px, int W, int H, int quality, u8 **out) {
    static huf_t hdl, hdc, hal, hac; static int init; if (!init) { init = 1; huf_build(&hdl, DCLB, DCV); huf_build(&hdc, DCCB, DCV); huf_build(&hal, ACLB, ACLV); huf_build(&hac, ACCB, ACCV); }
    int sc = quality < 50 ? 5000 / MAX(1, quality) : 200 - quality * 2; u8 ql[64], qc[64]; float qdl[64], qdc[64];
    for (int i = 0; i < 64; i++) { ql[i] = MAX(1, MIN(255, (QL[i] * sc + 50) / 100)); qc[i] = MAX(1, MIN(255, (QC[i] * sc + 50) / 100)); }
    for (int i = 0; i < 64; i++) { int r = ZZ[i] / 8, c = ZZ[i] % 8; qdl[i] = 1.0f / (ql[ZZ[i]] * AAN[r] * AAN[c] * 8.0f); qdc[i] = 1.0f / (qc[ZZ[i]] * AAN[r] * AAN[c] * 8.0f); }
    jw_t w = {0}; j16(&w, 0xFFD8); static const u8 jfif[] = {0xFF, 0xE0, 0, 16, 'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0}; for (unsigned i = 0; i < sizeof jfif; i++) jb(&w, jfif[i]);
    marker_q(&w, 0, ql); marker_q(&w, 1, qc); j16(&w, 0xFFC0); j16(&w, 17); jb(&w, 8); j16(&w, H); j16(&w, W); jb(&w, 3); jb(&w, 1); jb(&w, 0x22); jb(&w, 0); jb(&w, 2); jb(&w, 0x11); jb(&w, 1); jb(&w, 3); jb(&w, 0x11); jb(&w, 1);
    marker_h(&w, 0, 0, DCLB, DCV, 12); marker_h(&w, 1, 0, ACLB, ACLV, 162); marker_h(&w, 0, 1, DCCB, DCV, 12); marker_h(&w, 1, 1, ACCB, ACCV, 162);
    j16(&w, 0xFFDA); j16(&w, 12); jb(&w, 3); jb(&w, 1); jb(&w, 0x00); jb(&w, 2); jb(&w, 0x11); jb(&w, 3); jb(&w, 0x11); jb(&w, 0); jb(&w, 63); jb(&w, 0);
    int dy = 0, du = 0, dv = 0; float Y[4][64], U[64], V[64];
    for (int my = 0; my < H; my += 16) for (int mx = 0; mx < W; mx += 16) {
        for (int i = 0; i < 64; i++) U[i] = V[i] = 0;
        for (int yy = 0; yy < 16; yy++) for (int xx = 0; xx < 16; xx++) { int x = MIN(mx + xx, W - 1), y = MIN(my + yy, H - 1); u32 c = px[(long)y * W + x]; float r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
            Y[(yy >> 3) * 2 + (xx >> 3)][(yy & 7) * 8 + (xx & 7)] = 0.299f * r + 0.587f * g + 0.114f * b - 128; int k = (yy >> 1) * 8 + (xx >> 1); U[k] += (-0.168736f * r - 0.331264f * g + 0.5f * b) * 0.25f; V[k] += (0.5f * r - 0.418688f * g - 0.081312f * b) * 0.25f; }
        for (int k = 0; k < 4; k++) block(&w, Y[k], qdl, &dy, &hdl, &hal); block(&w, U, qdc, &du, &hdc, &hac); block(&w, V, qdc, &dv, &hdc, &hac); }
    if (w.bits) jbits(&w, 0x7F, 8 - w.bits); j16(&w, 0xFFD9); *out = w.p; return w.n; }
/* ---------- AVI (RIFF) con MJPEG y PCM ---------- */
static void le32(u8 *p, u32 v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void le16(u8 *p, u32 v) { p[0] = v; p[1] = v >> 8; }
static u8 *wbuf; static long wbn;
#define AVI_HDR 324   /* cabecera fija: RIFF + hdrl (video y audio) + LIST movi */
int zavi_open(zavi_t *a, const char *path, int w, int h, int fps, int arate) {
    memset(a, 0, sizeof *a); a->fd = z_open(path, O_WRITE | O_CREATE | O_TRUNC); if (a->fd < 0) return a->fd; a->w = w; a->h = h; a->fps = fps; a->arate = arate;
    u8 hd[AVI_HDR]; memset(hd, 0, sizeof hd); int o = 0; memcpy(hd, "RIFF", 4); memcpy(hd + 8, "AVI LIST", 8); le32(hd + 16, AVI_HDR - 12 - 8 - 12); memcpy(hd + 20, "hdrlavih", 8); le32(hd + 28, 56);
    le32(hd + 32, 1000000 / fps); le32(hd + 36, w * h * 3 * fps / 10); le32(hd + 44, 0x10); le32(hd + 56, arate ? 2 : 1); le32(hd + 60, 1 << 20); le32(hd + 64, w); le32(hd + 68, h);
    o = 88; memcpy(hd + o, "LIST", 4); le32(hd + o + 4, 4 + 64 + 48); memcpy(hd + o + 8, "strlstrh", 8); le32(hd + o + 16, 56); memcpy(hd + o + 20, "vidsMJPG", 8); le32(hd + o + 40, 1); le32(hd + o + 44, fps); le32(hd + o + 56, 1 << 20); le32(hd + o + 60, 0xFFFFFFFF); le16(hd + o + 72, w); le16(hd + o + 74, h);
    o += 76; memcpy(hd + o, "strf", 4); le32(hd + o + 4, 40); le32(hd + o + 8, 40); le32(hd + o + 12, w); le32(hd + o + 16, h); le16(hd + o + 20, 1); le16(hd + o + 22, 24); memcpy(hd + o + 24, "MJPG", 4); le32(hd + o + 28, w * h * 3);
    o += 48; /* 212: audio (o relleno JUNK si no hay) */
    if (arate) { memcpy(hd + o, "LIST", 4); le32(hd + o + 4, 4 + 64 + 24); memcpy(hd + o + 8, "strlstrh", 8); le32(hd + o + 16, 56); memcpy(hd + o + 20, "auds", 4); le32(hd + o + 40, 2); le32(hd + o + 44, arate * 2); le32(hd + o + 56, arate * 2); le32(hd + o + 60, 0xFFFFFFFF); le32(hd + o + 64, 2);
        o += 76; memcpy(hd + o, "strf", 4); le32(hd + o + 4, 16); le16(hd + o + 8, 1); le16(hd + o + 10, 1); le32(hd + o + 12, arate); le32(hd + o + 16, arate * 2); le16(hd + o + 20, 2); le16(hd + o + 22, 16); o += 24; }
    else { memcpy(hd + o, "JUNK", 4); le32(hd + o + 4, 100 - 8); o += 100; }
    memcpy(hd + o, "LIST", 4); memcpy(hd + o + 8, "movi", 4); a->movi = o + 8; /* 304 + 12 = AVI_HDR? */
    if (o + 12 != AVI_HDR) return -22; le32(hd + 4, 0); wbn = 0; int r = z_write(a->fd, hd, AVI_HDR) == AVI_HDR ? 0 : -5; a->pos = AVI_HDR; return r; }
/* lo escrito se junta en bloques de 1 MB: muchas escrituras pequenas en una memoria USB son lentas */
static int wflush(zavi_t *a) { if (!wbn) return 0; long r = z_write(a->fd, wbuf, wbn); int e = r == wbn ? 0 : -5; wbn = 0; return e; }
static int wput(zavi_t *a, const void *d, long n) { if (!wbuf) wbuf = malloc(1 << 20); if (wbn + n > (1 << 20)) { int e = wflush(a); if (e) return e; } if (n > (1 << 20)) return z_write(a->fd, d, n) == n ? 0 : -5; memcpy(wbuf + wbn, d, n); wbn += n; return 0; }
static int chunk(zavi_t *a, const char *id, const void *d, long n, int vid) { u8 h[8]; memcpy(h, id, 4); le32(h + 4, n); if (wput(a, h, 8) || (n && wput(a, d, n))) return -5; u8 z = 0; if (n & 1) wput(a, &z, 1);
    if (a->nidx == a->cap) { a->cap = a->cap * 2 + 1024; a->idx = realloc(a->idx, a->cap * 16); } u8 *e = a->idx + (long)a->nidx * 16; memcpy(e, id, 4); le32(e + 4, vid ? 0x10 : 0); le32(e + 8, a->pos - a->movi); le32(e + 12, n); a->nidx++; a->pos += 8 + n + (n & 1); return 0; }
int zavi_frame(zavi_t *a, const u8 *jpg, long n) { int e = chunk(a, "00dc", jpg, n, 1); if (!e) a->frames++; return e; } /* n = 0: repetir el cuadro anterior */
int zavi_audio(zavi_t *a, const short *pcm, long samples) { int e = chunk(a, "01wb", pcm, samples * 2, 0); if (!e) a->asamples += samples; return e; }
int zavi_close(zavi_t *a) { if (a->fd < 0) return -9; wflush(a); u8 h[8]; memcpy(h, "idx1", 4); le32(h + 4, a->nidx * 16); z_write(a->fd, h, 8); z_write(a->fd, a->idx, (long)a->nidx * 16); long end = a->pos + 8 + (long)a->nidx * 16; u8 v[4];
    le32(v, end - 8); z_seek(a->fd, 4, 0); z_write(a->fd, v, 4); le32(v, a->frames); z_seek(a->fd, 48, 0); z_write(a->fd, v, 4); z_seek(a->fd, 88 + 8 + 4 + 8 + 32, 0); z_write(a->fd, v, 4);
    if (a->arate) { le32(v, a->asamples); z_seek(a->fd, 212 + 8 + 4 + 8 + 32, 0); z_write(a->fd, v, 4); } le32(v, a->pos - a->movi); z_seek(a->fd, a->movi - 4, 0); z_write(a->fd, v, 4); /* tamano de LIST movi: desde 'movi' hasta el final de los datos */
    z_close(a->fd); a->fd = -1; free(a->idx); a->idx = 0; return 0; }
/* ---------- lectura de AVI por partes (sirve para archivos grandes) ---------- */
static u32 rd32(const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
static int rdat(int fd, long off, void *b, long n) { if (z_seek(fd, off, 0) < 0) return -1; return z_read(fd, b, n) == n ? 0 : -1; }
static void addf(long **o, long **l, int *n, int *cap, long off, long len) { if (*n == *cap) { *cap = *cap * 2 + 256; *o = realloc(*o, *cap * sizeof(long)); *l = realloc(*l, *cap * sizeof(long)); } (*o)[*n] = off; (*l)[*n] = len; (*n)++; }
int zavi_load(zavi_r *r, const char *path) { memset(r, 0, sizeof *r); r->vstream = 0; r->astream = -1; zdirent st; if (z_stat(path, &st)) return -2; r->n = st.size; r->fd = z_open(path, O_READ); if (r->fd < 0) return r->fd;
    u8 h[64]; if (r->n < 64 || rdat(r->fd, 0, h, 12) || memcmp(h, "RIFF", 4) || memcmp(h + 8, "AVI ", 4)) { zavi_free(r); return -22; } long movi = -1; int strm = -1;
    for (long p = 12; p + 8 <= r->n; ) { if (rdat(r->fd, p, h, MIN(64L, r->n - p))) break; u32 sz = rd32(h + 4);
        if (!memcmp(h, "LIST", 4)) { if (!memcmp(h + 8, "movi", 4)) { movi = p + 8; p += 8 + sz + (sz & 1); continue; } p += 12; continue; }
        if (!memcmp(h, "avih", 4) && sz >= 40) { r->us_frame = rd32(h + 8); r->w = rd32(h + 40); r->h = rd32(h + 44); }
        else if (!memcmp(h, "strh", 4) && sz >= 48) { strm++; if (!memcmp(h + 8, "auds", 4)) r->astream = strm; else if (!memcmp(h + 8, "vids", 4)) { r->vstream = strm; u32 scale = rd32(h + 28), rate = rd32(h + 32); if (scale && rate) r->us_frame = (u32)(1000000ULL * scale / rate); } }
        else if (!memcmp(h, "strf", 4) && strm >= 0 && strm == r->astream && sz >= 16) { r->achan = h[10]; r->arate = rd32(h + 12); r->abits = h[22]; }
        p += 8 + sz + (sz & 1); }
    if (movi < 0) { zavi_free(r); return -22; } char vid[3], aud[3]; snprintf(vid, 3, "%02d", r->vstream); snprintf(aud, 3, "%02d", r->astream < 0 ? 99 : r->astream);
    for (long p = movi + 4; p + 8 <= r->n; ) { if (rdat(r->fd, p, h, 12)) break; u32 sz = rd32(h + 4); if (!memcmp(h, "LIST", 4)) { p += 12; continue; } if (!memcmp(h, "idx1", 4)) break;
        long len = MIN((long)sz, r->n - p - 8); if (!memcmp(h, vid, 2) && h[2] == 'd') addf(&r->fo, &r->fl, &r->nf, &r->capf, p + 8, len); else if (r->astream >= 0 && !memcmp(h, aud, 2) && h[2] == 'w') addf(&r->ao, &r->al, &r->na, &r->capa, p + 8, len);
        p += 8 + sz + (sz & 1); }
    if (!r->us_frame) r->us_frame = 100000; if (!r->nf) { zavi_free(r); return -22; } return 0; }
/* lee el cuadro i (JPEG) en un buffer nuevo */
u8 *zavi_read(zavi_r *r, int i, long *n) { if (i < 0 || i >= r->nf) return 0; u8 *b = malloc(r->fl[i] + 16); if (!b) return 0; if (rdat(r->fd, r->fo[i], b, r->fl[i])) { free(b); return 0; } *n = r->fl[i]; return b; }
u8 *zavi_read_audio(zavi_r *r, int i, long *n) { if (i < 0 || i >= r->na) return 0; u8 *b = malloc(r->al[i] + 16); if (!b) return 0; if (rdat(r->fd, r->ao[i], b, r->al[i])) { free(b); return 0; } *n = r->al[i]; return b; }
void zavi_free(zavi_r *r) { if (r->fd > 0) z_close(r->fd); free(r->fo); free(r->fl); free(r->ao); free(r->al); memset(r, 0, sizeof *r); r->fd = -1; }
