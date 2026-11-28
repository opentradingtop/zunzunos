/* Criptografia para TLS escrita desde cero: SHA-256/384/512, HMAC, HKDF, AES-GCM, X25519,
   aritmetica de Montgomery, RSA (PKCS#1 v1.5 y PSS) y ECDSA/ECDH en P-256 y P-384. */
#include "zcrypto.h"
static void zcpy(void *d, const void *s, long n) { u8 *a = d; const u8 *b = s; while (n--) *a++ = *b++; }
static void zset(void *d, int v, long n) { u8 *a = d; while (n--) *a++ = v; }
static int zcmp(const void *x, const void *y, long n) { const u8 *a = x, *b = y; int r = 0; while (n--) r |= *a++ ^ *b++; return r; }
/* ===================== SHA-256 ===================== */
static const u32 K256[64] = {0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
#define ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha256_blk(zsha256 *c, const u8 *p) { u32 w[64], a, b, cc, d, e, f, g, h; for (int i = 0; i < 16; i++) w[i] = (u32)p[i * 4] << 24 | p[i * 4 + 1] << 16 | p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (int i = 16; i < 64; i++) { u32 s0 = ROR32(w[i - 15], 7) ^ ROR32(w[i - 15], 18) ^ (w[i - 15] >> 3), s1 = ROR32(w[i - 2], 17) ^ ROR32(w[i - 2], 19) ^ (w[i - 2] >> 10); w[i] = w[i - 16] + s0 + w[i - 7] + s1; }
    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3]; e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];
    for (int i = 0; i < 64; i++) { u32 t1 = h + (ROR32(e, 6) ^ ROR32(e, 11) ^ ROR32(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i], t2 = (ROR32(a, 2) ^ ROR32(a, 13) ^ ROR32(a, 22)) + ((a & b) ^ (a & cc) ^ (b & cc)); h = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2; }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h; }
void sha256_init(zsha256 *c) { static const u32 iv[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}; zcpy(c->h, iv, 32); c->len = 0; c->n = 0; }
void sha256_up(zsha256 *c, const void *d, long n) { const u8 *p = d; c->len += n; while (n > 0) { int k = 64 - c->n; if (k > n) k = n; zcpy(c->buf + c->n, p, k); c->n += k; p += k; n -= k; if (c->n == 64) { sha256_blk(c, c->buf); c->n = 0; } } }
void sha256_end(zsha256 *c, u8 *out) { u64 bits = c->len * 8; u8 pad = 0x80; sha256_up(c, &pad, 1); u8 z = 0; while (c->n != 56) sha256_up(c, &z, 1); u8 l[8]; for (int i = 0; i < 8; i++) l[i] = bits >> (56 - 8 * i); sha256_up(c, l, 8); for (int i = 0; i < 8; i++) { out[i * 4] = c->h[i] >> 24; out[i * 4 + 1] = c->h[i] >> 16; out[i * 4 + 2] = c->h[i] >> 8; out[i * 4 + 3] = c->h[i]; } }
/* ===================== SHA-512 / SHA-384 ===================== */
static const u64 K512[80] = {0x428a2f98d728ae22ULL,0x7137449123ef65cdULL,0xb5c0fbcfec4d3b2fULL,0xe9b5dba58189dbbcULL,0x3956c25bf348b538ULL,0x59f111f1b605d019ULL,0x923f82a4af194f9bULL,0xab1c5ed5da6d8118ULL,0xd807aa98a3030242ULL,0x12835b0145706fbeULL,0x243185be4ee4b28cULL,0x550c7dc3d5ffb4e2ULL,0x72be5d74f27b896fULL,0x80deb1fe3b1696b1ULL,0x9bdc06a725c71235ULL,0xc19bf174cf692694ULL,0xe49b69c19ef14ad2ULL,0xefbe4786384f25e3ULL,0x0fc19dc68b8cd5b5ULL,0x240ca1cc77ac9c65ULL,0x2de92c6f592b0275ULL,0x4a7484aa6ea6e483ULL,0x5cb0a9dcbd41fbd4ULL,0x76f988da831153b5ULL,0x983e5152ee66dfabULL,0xa831c66d2db43210ULL,0xb00327c898fb213fULL,0xbf597fc7beef0ee4ULL,0xc6e00bf33da88fc2ULL,0xd5a79147930aa725ULL,0x06ca6351e003826fULL,0x142929670a0e6e70ULL,0x27b70a8546d22ffcULL,0x2e1b21385c26c926ULL,0x4d2c6dfc5ac42aedULL,0x53380d139d95b3dfULL,0x650a73548baf63deULL,0x766a0abb3c77b2a8ULL,0x81c2c92e47edaee6ULL,0x92722c851482353bULL,0xa2bfe8a14cf10364ULL,0xa81a664bbc423001ULL,0xc24b8b70d0f89791ULL,0xc76c51a30654be30ULL,0xd192e819d6ef5218ULL,0xd69906245565a910ULL,0xf40e35855771202aULL,0x106aa07032bbd1b8ULL,0x19a4c116b8d2d0c8ULL,0x1e376c085141ab53ULL,0x2748774cdf8eeb99ULL,0x34b0bcb5e19b48a8ULL,0x391c0cb3c5c95a63ULL,0x4ed8aa4ae3418acbULL,0x5b9cca4f7763e373ULL,0x682e6ff3d6b2b8a3ULL,0x748f82ee5defb2fcULL,0x78a5636f43172f60ULL,0x84c87814a1f0ab72ULL,0x8cc702081a6439ecULL,0x90befffa23631e28ULL,0xa4506cebde82bde9ULL,0xbef9a3f7b2c67915ULL,0xc67178f2e372532bULL,0xca273eceea26619cULL,0xd186b8c721c0c207ULL,0xeada7dd6cde0eb1eULL,0xf57d4f7fee6ed178ULL,0x06f067aa72176fbaULL,0x0a637dc5a2c898a6ULL,0x113f9804bef90daeULL,0x1b710b35131c471bULL,0x28db77f523047d84ULL,0x32caab7b40c72493ULL,0x3c9ebe0a15c9bebcULL,0x431d67c49c100d4cULL,0x4cc5d4becb3e42b6ULL,0x597f299cfc657e2aULL,0x5fcb6fab3ad6faecULL,0x6c44198c4a475817ULL};
#define ROR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))
static void sha512_blk(zsha512 *c, const u8 *p) { u64 w[80], v[8]; for (int i = 0; i < 16; i++) { w[i] = 0; for (int j = 0; j < 8; j++) w[i] = w[i] << 8 | p[i * 8 + j]; }
    for (int i = 16; i < 80; i++) { u64 s0 = ROR64(w[i - 15], 1) ^ ROR64(w[i - 15], 8) ^ (w[i - 15] >> 7), s1 = ROR64(w[i - 2], 19) ^ ROR64(w[i - 2], 61) ^ (w[i - 2] >> 6); w[i] = w[i - 16] + s0 + w[i - 7] + s1; }
    for (int i = 0; i < 8; i++) v[i] = c->h[i];
    for (int i = 0; i < 80; i++) { u64 t1 = v[7] + (ROR64(v[4], 14) ^ ROR64(v[4], 18) ^ ROR64(v[4], 41)) + ((v[4] & v[5]) ^ (~v[4] & v[6])) + K512[i] + w[i], t2 = (ROR64(v[0], 28) ^ ROR64(v[0], 34) ^ ROR64(v[0], 39)) + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2])); v[7] = v[6]; v[6] = v[5]; v[5] = v[4]; v[4] = v[3] + t1; v[3] = v[2]; v[2] = v[1]; v[1] = v[0]; v[0] = t1 + t2; }
    for (int i = 0; i < 8; i++) c->h[i] += v[i]; }
void sha512_init(zsha512 *c, int is384) { static const u64 i5[8] = {0x6a09e667f3bcc908ULL,0xbb67ae8584caa73bULL,0x3c6ef372fe94f82bULL,0xa54ff53a5f1d36f1ULL,0x510e527fade682d1ULL,0x9b05688c2b3e6c1fULL,0x1f83d9abfb41bd6bULL,0x5be0cd19137e2179ULL}, i3[8] = {0xcbbb9d5dc1059ed8ULL,0x629a292a367cd507ULL,0x9159015a3070dd17ULL,0x152fecd8f70e5939ULL,0x67332667ffc00b31ULL,0x8eb44a8768581511ULL,0xdb0c2e0d64f98fa7ULL,0x47b5481dbefa4fa4ULL}; zcpy(c->h, is384 ? i3 : i5, 64); c->len = 0; c->n = 0; c->out = is384 ? 48 : 64; }
void sha512_up(zsha512 *c, const void *d, long n) { const u8 *p = d; c->len += n; while (n > 0) { int k = 128 - c->n; if (k > n) k = n; zcpy(c->buf + c->n, p, k); c->n += k; p += k; n -= k; if (c->n == 128) { sha512_blk(c, c->buf); c->n = 0; } } }
void sha512_end(zsha512 *c, u8 *out) { u64 bits = c->len * 8; u8 pad = 0x80; sha512_up(c, &pad, 1); u8 z = 0; while (c->n != 112) sha512_up(c, &z, 1); u8 l[16]; zset(l, 0, 8); for (int i = 0; i < 8; i++) l[8 + i] = bits >> (56 - 8 * i); sha512_up(c, l, 16); for (int i = 0; i < c->out / 8; i++) for (int j = 0; j < 8; j++) out[i * 8 + j] = c->h[i] >> (56 - 8 * j); }
int zc_hlen(int alg) { return alg == 384 ? 48 : alg == 512 ? 64 : 32; }
void zh_init(zhash *h, int alg) { h->alg = alg; if (alg == 256) sha256_init(&h->u.a); else sha512_init(&h->u.b, alg == 384); }
void zh_up(zhash *h, const void *d, long n) { if (h->alg == 256) sha256_up(&h->u.a, d, n); else sha512_up(&h->u.b, d, n); }
void zh_end(zhash *h, u8 *out) { if (h->alg == 256) sha256_end(&h->u.a, out); else sha512_end(&h->u.b, out); }
void zc_hash(int alg, const void *d, long n, u8 *out) { zhash h; zh_init(&h, alg); zh_up(&h, d, n); zh_end(&h, out); }
void zc_hmac(int alg, const u8 *key, int kl, const void *d, long n, u8 *out) {
    int bs = alg == 256 ? 64 : 128, hl = zc_hlen(alg); u8 k[128], t[64]; zset(k, 0, bs); if (kl > bs) zc_hash(alg, key, kl, k); else zcpy(k, key, kl);
    zhash h; for (int i = 0; i < bs; i++) k[i] ^= 0x36; zh_init(&h, alg); zh_up(&h, k, bs); zh_up(&h, d, n); zh_end(&h, t);
    for (int i = 0; i < bs; i++) k[i] ^= 0x36 ^ 0x5c; zh_init(&h, alg); zh_up(&h, k, bs); zh_up(&h, t, hl); zh_end(&h, out); }
void zc_hkdf_extract(int alg, const u8 *salt, int sl, const u8 *ikm, int il, u8 *out) { u8 z[64]; if (!salt) { zset(z, 0, 64); salt = z; sl = zc_hlen(alg); } zc_hmac(alg, salt, sl, ikm, il, out); }
void zc_hkdf_expand(int alg, const u8 *prk, const u8 *info, int il, u8 *out, int ol) { int hl = zc_hlen(alg); u8 t[64], buf[64 + 300]; int tl = 0, done = 0; for (u8 c = 1; done < ol; c++) { zcpy(buf, t, tl); zcpy(buf + tl, info, il); buf[tl + il] = c; zc_hmac(alg, prk, hl, buf, tl + il + 1, t); tl = hl; int k = ol - done < hl ? ol - done : hl; zcpy(out + done, t, k); done += k; } }
/* ===================== AES y GCM ===================== */
static u8 SB[256]; static int sb_ok;
static u8 xt(u8 x) { return (u8)(x << 1) ^ (x & 0x80 ? 0x1b : 0); }
static void sb_init(void) { u8 p = 1, q = 1; do { p = p ^ (p << 1) ^ (p & 0x80 ? 0x1b : 0); q ^= q << 1; q ^= q << 2; q ^= q << 4; if (q & 0x80) q ^= 0x09; u8 x = q ^ (q << 1 | q >> 7) ^ (q << 2 | q >> 6) ^ (q << 3 | q >> 5) ^ (q << 4 | q >> 4); SB[p] = x ^ 0x63; } while (p != 1); SB[0] = 0x63; sb_ok = 1; }
static void aes_key(zgcm *g, const u8 *key, int kl) { if (!sb_ok) sb_init(); int nk = kl / 4; g->nr = nk + 6; u32 *w = g->rk; for (int i = 0; i < nk; i++) w[i] = (u32)key[4 * i] << 24 | key[4 * i + 1] << 16 | key[4 * i + 2] << 8 | key[4 * i + 3]; u8 rc = 1;
    for (int i = nk; i < 4 * (g->nr + 1); i++) { u32 t = w[i - 1]; if (i % nk == 0) { t = (u32)SB[(t >> 16) & 255] << 24 | SB[(t >> 8) & 255] << 16 | SB[t & 255] << 8 | SB[t >> 24]; t ^= (u32)rc << 24; rc = xt(rc); } else if (nk > 6 && i % nk == 4) t = (u32)SB[t >> 24] << 24 | SB[(t >> 16) & 255] << 16 | SB[(t >> 8) & 255] << 8 | SB[t & 255]; w[i] = w[i - nk] ^ t; } }
static void aes_enc(const zgcm *g, const u8 *in, u8 *out) { u8 s[16]; for (int i = 0; i < 16; i++) s[i] = in[i] ^ (u8)(g->rk[i / 4] >> (24 - 8 * (i % 4)));
    for (int r = 1; r <= g->nr; r++) { u8 t[16]; for (int i = 0; i < 16; i++) t[i] = SB[s[(i + 4 * (i % 4)) % 16]]; /* SubBytes + ShiftRows */
        if (r < g->nr) for (int c = 0; c < 4; c++) { u8 *a = t + 4 * c, a0 = a[0], a1 = a[1], a2 = a[2], a3 = a[3], x = a0 ^ a1 ^ a2 ^ a3; a[0] ^= x ^ xt(a0 ^ a1); a[1] ^= x ^ xt(a1 ^ a2); a[2] ^= x ^ xt(a2 ^ a3); a[3] ^= x ^ xt(a3 ^ a0); }
        for (int i = 0; i < 16; i++) s[i] = t[i] ^ (u8)(g->rk[r * 4 + i / 4] >> (24 - 8 * (i % 4))); }
    zcpy(out, s, 16); }
/* AES por bloques para quien lo necesite fuera de GCM (Alquimia: ZIP cifrado WinZip AES, CTR propio) */
void zc_aes_key(zgcm *g, const u8 *key, int klen) { aes_key(g, key, klen); }
void zc_aes_enc(const zgcm *g, const u8 *in, u8 *out) { aes_enc(g, in, out); }
static void gmul(u8 *x, const u8 *h) { u8 z[16], v[16]; zset(z, 0, 16); zcpy(v, h, 16); for (int i = 0; i < 128; i++) { if (x[i / 8] >> (7 - i % 8) & 1) for (int j = 0; j < 16; j++) z[j] ^= v[j]; int lsb = v[15] & 1; for (int j = 15; j > 0; j--) v[j] = (v[j] >> 1) | (v[j - 1] << 7); v[0] >>= 1; if (lsb) v[0] ^= 0xe1; } zcpy(x, z, 16); }
void gcm_init(zgcm *g, const u8 *key, int klen) { aes_key(g, key, klen); u8 z[16]; zset(z, 0, 16); aes_enc(g, z, g->H); }
static void ghash(const zgcm *g, u8 *y, const u8 *d, long n) { while (n > 0) { int k = n < 16 ? n : 16; for (int i = 0; i < k; i++) y[i] ^= d[i]; gmul(y, g->H); d += k; n -= k; } }
static void gcm_core(zgcm *g, const u8 *iv, const u8 *aad, int al, const u8 *in, long n, u8 *out, u8 *tag, int dec) {
    u8 j0[16], ctr[16], ks[16], y[16]; zcpy(j0, iv, 12); j0[12] = j0[13] = j0[14] = 0; j0[15] = 1; zcpy(ctr, j0, 16); zset(y, 0, 16); ghash(g, y, aad, al);
    if (dec) ghash(g, y, in, n);
    for (long o = 0; o < n; o += 16) { for (int i = 15; i >= 12; i--) if (++ctr[i]) break; aes_enc(g, ctr, ks); long k = n - o < 16 ? n - o : 16; for (long i = 0; i < k; i++) out[o + i] = in[o + i] ^ ks[i]; }
    if (!dec) ghash(g, y, out, n);
    u8 l[16]; u64 ab = (u64)al * 8, cb = (u64)n * 8; for (int i = 0; i < 8; i++) { l[i] = ab >> (56 - 8 * i); l[8 + i] = cb >> (56 - 8 * i); } ghash(g, y, l, 16); aes_enc(g, j0, ks); for (int i = 0; i < 16; i++) tag[i] = y[i] ^ ks[i]; }
void gcm_seal(zgcm *g, const u8 *iv, const u8 *aad, int al, const u8 *in, long n, u8 *out, u8 *tag) { gcm_core(g, iv, aad, al, in, n, out, tag, 0); }
int gcm_open(zgcm *g, const u8 *iv, const u8 *aad, int al, const u8 *in, long n, u8 *out, const u8 *tag) { u8 t[16]; gcm_core(g, iv, aad, al, in, n, out, t, 1); return zcmp(t, tag, 16) ? -1 : 0; }
/* ===================== X25519 (formulas de TweetNaCl, dominio publico) ===================== */
typedef i64 gf[16];
static void car(gf o) { for (int i = 0; i < 16; i++) { o[i] += (1LL << 16); i64 c = o[i] >> 16; o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15); o[i] -= c << 16; } }
static void sel(gf p, gf q, int b) { i64 c = ~(b - 1); for (int i = 0; i < 16; i++) { i64 t = c & (p[i] ^ q[i]); p[i] ^= t; q[i] ^= t; } }
static void pack(u8 *o, const gf n) { gf m, t; for (int i = 0; i < 16; i++) t[i] = n[i]; car(t); car(t); car(t);
    for (int j = 0; j < 2; j++) { m[0] = t[0] - 0xffed; for (int i = 1; i < 15; i++) { m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1); m[i - 1] &= 0xffff; } m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1); int b = (m[15] >> 16) & 1; m[14] &= 0xffff; sel(t, m, 1 - b); }
    for (int i = 0; i < 16; i++) { o[2 * i] = t[i] & 0xff; o[2 * i + 1] = t[i] >> 8; } }
static void unpack(gf o, const u8 *n) { for (int i = 0; i < 16; i++) o[i] = n[2 * i] + ((i64)n[2 * i + 1] << 8); o[15] &= 0x7fff; }
static void fA(gf o, const gf a, const gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] + b[i]; }
static void fZ(gf o, const gf a, const gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] - b[i]; }
static void fM(gf o, const gf a, const gf b) { i64 t[31]; for (int i = 0; i < 31; i++) t[i] = 0; for (int i = 0; i < 16; i++) for (int j = 0; j < 16; j++) t[i + j] += a[i] * b[j]; for (int i = 0; i < 15; i++) t[i] += 38 * t[i + 16]; for (int i = 0; i < 16; i++) o[i] = t[i]; car(o); car(o); }
static void finv(gf o, const gf in) { gf c; for (int a = 0; a < 16; a++) c[a] = in[a]; for (int a = 253; a >= 0; a--) { fM(c, c, c); if (a != 2 && a != 4) fM(c, c, in); } for (int a = 0; a < 16; a++) o[a] = c[a]; }
void x25519(u8 *q, const u8 *n, const u8 *p) { u8 z[32]; i64 x[80]; gf a, b, c, d, e, f; static const gf k121665 = {0xDB41, 1};
    for (int i = 0; i < 31; i++) z[i] = n[i]; z[31] = (n[31] & 127) | 64; z[0] &= 248; unpack(x, p);
    for (int i = 0; i < 16; i++) { b[i] = x[i]; d[i] = a[i] = c[i] = 0; } a[0] = d[0] = 1;
    for (int i = 254; i >= 0; --i) { int r = (z[i >> 3] >> (i & 7)) & 1; sel(a, b, r); sel(c, d, r); fA(e, a, c); fZ(a, a, c); fA(c, b, d); fZ(b, b, d); fM(d, e, e); fM(f, a, a); fM(a, c, a); fM(c, b, e); fA(e, a, c); fZ(a, a, c); fM(b, a, a); fZ(c, d, f); fM(a, c, k121665); fA(a, a, d); fM(c, c, a); fM(a, d, f); fM(d, b, x); fM(b, e, e); sel(a, b, r); sel(c, d, r); }
    for (int i = 0; i < 16; i++) { x[i + 16] = a[i]; x[i + 32] = c[i]; x[i + 48] = b[i]; x[i + 64] = d[i]; } finv(x + 32, x + 32); fM(x + 16, x + 16, x + 32); pack(q, x + 16); }
void x25519_base(u8 *out, const u8 *s) { u8 nine[32]; zset(nine, 0, 32); nine[0] = 9; x25519(out, s, nine); }
/* ===================== Montgomery ===================== */
#define ML 130
typedef struct { int n; u32 m[ML], rr[ML], one[ML]; u32 minv; } zmont;
static void be2l(u32 *r, int n, const u8 *b, int bl) { zset(r, 0, n * 4); for (int i = 0; i < bl && i < n * 4; i++) r[i / 4] |= (u32)b[bl - 1 - i] << (8 * (i % 4)); }
static void l2be(u8 *b, int bl, const u32 *r, int n) { for (int i = 0; i < bl; i++) b[bl - 1 - i] = i / 4 < n ? (u8)(r[i / 4] >> (8 * (i % 4))) : 0; }
static int lcmp(const u32 *a, const u32 *b, int n) { for (int i = n - 1; i >= 0; i--) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1; return 0; }
static u32 lsub(u32 *r, const u32 *a, const u32 *b, int n) { u64 br = 0; for (int i = 0; i < n; i++) { u64 t = (u64)a[i] - b[i] - br; r[i] = (u32)t; br = (t >> 32) & 1; } return (u32)br; }
static u32 ladd(u32 *r, const u32 *a, const u32 *b, int n) { u64 c = 0; for (int i = 0; i < n; i++) { c += (u64)a[i] + b[i]; r[i] = (u32)c; c >>= 32; } return (u32)c; }
static int liszero(const u32 *a, int n) { u32 z = 0; for (int i = 0; i < n; i++) z |= a[i]; return !z; }
static void mmul(const zmont *c, u32 *r, const u32 *a, const u32 *b) {
    int n = c->n; u32 t[ML + 2]; zset(t, 0, (n + 2) * 4);
    for (int i = 0; i < n; i++) { u64 C = 0; for (int j = 0; j < n; j++) { C = (u64)t[j] + (u64)a[j] * b[i] + C; t[j] = (u32)C; C >>= 32; } u64 s = (u64)t[n] + C; t[n] = (u32)s; t[n + 1] = (u32)(s >> 32);
        u32 m = t[0] * c->minv; C = ((u64)t[0] + (u64)m * c->m[0]) >> 32; for (int j = 1; j < n; j++) { C = (u64)t[j] + (u64)m * c->m[j] + C; t[j - 1] = (u32)C; C >>= 32; } s = (u64)t[n] + C; t[n - 1] = (u32)s; t[n] = t[n + 1] + (u32)(s >> 32); }
    if (t[n] || lcmp(t, c->m, n) >= 0) lsub(t, t, c->m, n); zcpy(r, t, n * 4); }
static void mont_init(zmont *c, const u8 *m, int ml) { while (ml > 1 && !*m) { m++; ml--; } c->n = (ml + 3) / 4; be2l(c->m, c->n, m, ml); u32 x = c->m[0]; for (int i = 0; i < 5; i++) x *= 2 - c->m[0] * x; c->minv = -x;
    u32 *r = c->rr; zset(r, 0, c->n * 4); r[0] = 1; for (int i = 0; i < 64 * c->n; i++) { u32 top = r[c->n - 1] >> 31; ladd(r, r, r, c->n); if (top || lcmp(r, c->m, c->n) >= 0) lsub(r, r, c->m, c->n); }
    u32 o[ML]; zset(o, 0, c->n * 4); o[0] = 1; mmul(c, c->one, o, c->rr); }
static void to_m(const zmont *c, u32 *r, const u32 *a) { mmul(c, r, a, c->rr); }
static void from_m(const zmont *c, u32 *r, const u32 *a) { u32 o[ML]; zset(o, 0, c->n * 4); o[0] = 1; mmul(c, r, a, o); }
static void mexp(const zmont *c, u32 *r, const u32 *bm, const u8 *e, int el) { u32 x[ML]; zcpy(x, c->one, c->n * 4); for (int i = 0; i < el; i++) for (int b = 7; b >= 0; b--) { mmul(c, x, x, x); if (e[i] >> b & 1) mmul(c, x, x, bm); } zcpy(r, x, c->n * 4); }
static void fadd(const zmont *c, u32 *r, const u32 *a, const u32 *b) { u32 cy = ladd(r, a, b, c->n); if (cy || lcmp(r, c->m, c->n) >= 0) lsub(r, r, c->m, c->n); }
static void fsub(const zmont *c, u32 *r, const u32 *a, const u32 *b) { if (lsub(r, a, b, c->n)) ladd(r, r, c->m, c->n); }
/* ===================== RSA ===================== */
static int rsa_raw(const u8 *n, int nl, const u8 *e, int el, const u8 *sig, int sl, u8 *em) {
    while (nl > 1 && !*n) { n++; nl--; } if (nl > 512 || sl > nl + 1) return -1; zmont *c = (zmont *)__builtin_alloca(sizeof(zmont)); mont_init(c, n, nl);
    u32 s[ML], sm[ML], r[ML]; be2l(s, c->n, sig, sl); if (lcmp(s, c->m, c->n) >= 0) return -1; to_m(c, sm, s); mexp(c, r, sm, e, el); from_m(c, r, r); l2be(em, nl, r, c->n); return nl; }
static const u8 DI256[] = {0x30,0x31,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x01,0x05,0x00,0x04,0x20}, DI384[] = {0x30,0x41,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x02,0x05,0x00,0x04,0x30}, DI512[] = {0x30,0x51,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x03,0x05,0x00,0x04,0x40};
int rsa_verify_pkcs1(const u8 *n, int nl, const u8 *e, int el, const u8 *sig, int sl, int alg, const u8 *hash) {
    u8 em[512]; int k = rsa_raw(n, nl, e, el, sig, sl, em); if (k < 0) return -1; const u8 *di = alg == 256 ? DI256 : alg == 384 ? DI384 : DI512; int hl = zc_hlen(alg), tl = 19 + hl; if (k < tl + 11) return -1;
    if (em[0] != 0 || em[1] != 1) return -1; for (int i = 2; i < k - tl - 1; i++) if (em[i] != 0xFF) return -1; if (em[k - tl - 1] != 0) return -1; if (zcmp(em + k - tl, di, 19) || zcmp(em + k - hl, hash, hl)) return -1; return 0; }
int rsa_verify_pss(const u8 *n, int nl, const u8 *e, int el, const u8 *sig, int sl, int alg, const u8 *hash) {
    u8 em[512]; while (nl > 1 && !*n) { n++; nl--; } int k = rsa_raw(n, nl, e, el, sig, sl, em); if (k < 0) return -1; int hl = zc_hlen(alg);
    int bits = 8 * nl; for (u8 t = n[0]; !(t & 0x80) && bits; t <<= 1) bits--; int embits = bits - 1, emlen = (embits + 7) / 8; u8 *E = em + (k - emlen); if (emlen < 2 * hl + 2 || E[emlen - 1] != 0xbc) return -1;
    int dbl = emlen - hl - 1; u8 *db = E, *H = E + dbl, mask[512]; for (int off = 0, cnt = 0; off < dbl; cnt++) { u8 b[68], t[64]; zcpy(b, H, hl); b[hl] = cnt >> 24; b[hl + 1] = cnt >> 16; b[hl + 2] = cnt >> 8; b[hl + 3] = cnt; zc_hash(alg, b, hl + 4, t); for (int i = 0; i < hl && off < dbl; i++) mask[off++] = t[i]; }
    for (int i = 0; i < dbl; i++) db[i] ^= mask[i]; db[0] &= 0xFF >> (8 * emlen - embits); int i = 0; while (i < dbl && !db[i]) i++; if (i >= dbl || db[i] != 1) return -1; i++; int saltl = dbl - i;
    u8 mp[8 + 64 + 512], h2[64]; zset(mp, 0, 8); zcpy(mp + 8, hash, hl); zcpy(mp + 8 + hl, db + i, saltl); zc_hash(alg, mp, 8 + hl + saltl, h2); return zcmp(h2, H, hl) ? -1 : 0; }
/* ===================== curvas P-256 y P-384 (a = -3) ===================== */
static const u8 P256p[32] = {0xFF,0xFF,0xFF,0xFF,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
static const u8 P256n[32] = {0xFF,0xFF,0xFF,0xFF,0,0,0,0,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xBC,0xE6,0xFA,0xAD,0xA7,0x17,0x9E,0x84,0xF3,0xB9,0xCA,0xC2,0xFC,0x63,0x25,0x51};
static const u8 P256g[64] = {0x6B,0x17,0xD1,0xF2,0xE1,0x2C,0x42,0x47,0xF8,0xBC,0xE6,0xE5,0x63,0xA4,0x40,0xF2,0x77,0x03,0x7D,0x81,0x2D,0xEB,0x33,0xA0,0xF4,0xA1,0x39,0x45,0xD8,0x98,0xC2,0x96,0x4F,0xE3,0x42,0xE2,0xFE,0x1A,0x7F,0x9B,0x8E,0xE7,0xEB,0x4A,0x7C,0x0F,0x9E,0x16,0x2B,0xCE,0x33,0x57,0x6B,0x31,0x5E,0xCE,0xCB,0xB6,0x40,0x68,0x37,0xBF,0x51,0xF5};
static const u8 P384p[48] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,0xFF,0xFF,0xFF,0xFF,0,0,0,0,0,0,0,0,0xFF,0xFF,0xFF,0xFF};
static const u8 P384n[48] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xC7,0x63,0x4D,0x81,0xF4,0x37,0x2D,0xDF,0x58,0x1A,0x0D,0xB2,0x48,0xB0,0xA7,0x7A,0xEC,0xEC,0x19,0x6A,0xCC,0xC5,0x29,0x73};
static const u8 P384g[96] = {0xAA,0x87,0xCA,0x22,0xBE,0x8B,0x05,0x37,0x8E,0xB1,0xC7,0x1E,0xF3,0x20,0xAD,0x74,0x6E,0x1D,0x3B,0x62,0x8B,0xA7,0x9B,0x98,0x59,0xF7,0x41,0xE0,0x82,0x54,0x2A,0x38,0x55,0x02,0xF2,0x5D,0xBF,0x55,0x29,0x6C,0x3A,0x54,0x5E,0x38,0x72,0x76,0x0A,0xB7,0x36,0x17,0xDE,0x4A,0x96,0x26,0x2C,0x6F,0x5D,0x9E,0x98,0xBF,0x92,0x92,0xDC,0x29,0xF8,0xF4,0x1D,0xBD,0x28,0x9A,0x14,0x7C,0xE9,0xDA,0x31,0x13,0xB5,0xF0,0xB8,0xC0,0x0A,0x60,0xB1,0xCE,0x1D,0x7E,0x81,0x9D,0x7A,0x43,0x1D,0x7C,0x90,0xEA,0x0E,0x5F};
typedef struct { u32 x[ML], y[ML], z[ML]; } jpt;
static void pdbl(const zmont *f, jpt *r, const jpt *p) { if (liszero(p->z, f->n)) { *r = *p; return; } int n = f->n; u32 d[ML], g[ML], b[ML], a[ML], t[ML], u[ML];
    mmul(f, d, p->z, p->z); mmul(f, g, p->y, p->y); mmul(f, b, p->x, g); fsub(f, t, p->x, d); fadd(f, u, p->x, d); mmul(f, a, t, u); fadd(f, t, a, a); fadd(f, a, t, a);
    jpt o; mmul(f, o.x, a, a); fadd(f, t, b, b); fadd(f, t, t, t); fadd(f, u, t, t); fsub(f, o.x, o.x, u); fadd(f, u, p->y, p->z); mmul(f, o.z, u, u); fsub(f, o.z, o.z, g); fsub(f, o.z, o.z, d);
    fsub(f, u, t, o.x); mmul(f, o.y, a, u); mmul(f, g, g, g); fadd(f, g, g, g); fadd(f, g, g, g); fadd(f, g, g, g); fsub(f, o.y, o.y, g); (void)n; *r = o; }
static void padd(const zmont *f, jpt *r, const jpt *p, const jpt *q) { int n = f->n; if (liszero(p->z, n)) { *r = *q; return; } if (liszero(q->z, n)) { *r = *p; return; }
    u32 z1[ML], z2[ML], u1[ML], u2[ML], s1[ML], s2[ML], h[ML], rr[ML], hh[ML], hhh[ML], v[ML], t[ML];
    mmul(f, z1, p->z, p->z); mmul(f, z2, q->z, q->z); mmul(f, u1, p->x, z2); mmul(f, u2, q->x, z1); mmul(f, s1, p->y, q->z); mmul(f, s1, s1, z2); mmul(f, s2, q->y, p->z); mmul(f, s2, s2, z1);
    fsub(f, h, u2, u1); fsub(f, rr, s2, s1); if (liszero(h, n)) { if (liszero(rr, n)) { pdbl(f, r, p); return; } zset(r, 0, sizeof *r); return; }
    jpt o; mmul(f, hh, h, h); mmul(f, hhh, h, hh); mmul(f, v, u1, hh); mmul(f, o.x, rr, rr); fsub(f, o.x, o.x, hhh); fsub(f, o.x, o.x, v); fsub(f, o.x, o.x, v);
    fsub(f, t, v, o.x); mmul(f, o.y, rr, t); mmul(f, t, s1, hhh); fsub(f, o.y, o.y, t); mmul(f, o.z, p->z, q->z); mmul(f, o.z, o.z, h); *r = o; }
static int curve(int c, const u8 **p, const u8 **n, const u8 **g) { if (c == 256) { *p = P256p; *n = P256n; *g = P256g; return 32; } *p = P384p; *n = P384n; *g = P384g; return 48; }
static int pt_load(const zmont *f, jpt *o, const u8 *xy, int L) { u32 t[ML]; be2l(t, f->n, xy, L); to_m(f, o->x, t); be2l(t, f->n, xy + L, L); to_m(f, o->y, t); zcpy(o->z, f->one, f->n * 4); return 0; }
static void pt_affx(const zmont *f, u32 *x, const jpt *p, const u8 *pp, int L) { u32 zi[ML], z2[ML]; u8 e[64]; zcpy(e, pp, L); /* p - 2: el ultimo byte de p es >= 2 en ambas curvas */ e[L - 1] -= 2; mexp(f, zi, p->z, e, L); mmul(f, z2, zi, zi); mmul(f, x, p->x, z2); from_m(f, x, x); }
static void pt_affxy(const zmont *f, u32 *x, u32 *y, const jpt *p, const u8 *pp, int L) { u32 zi[ML], z2[ML], z3[ML]; u8 e[64]; zcpy(e, pp, L); e[L - 1] -= 2; mexp(f, zi, p->z, e, L); mmul(f, z2, zi, zi); mmul(f, z3, z2, zi); mmul(f, x, p->x, z2); from_m(f, x, x); mmul(f, y, p->y, z3); from_m(f, y, y); }
static void smul2(const zmont *f, jpt *r, const u8 *k1, const jpt *P, const u8 *k2, const jpt *Q, int L) { jpt PQ, R; padd(f, &PQ, P, Q); zset(&R, 0, sizeof R);
    for (int i = 0; i < L; i++) for (int b = 7; b >= 0; b--) { pdbl(f, &R, &R); int a = k1 ? k1[i] >> b & 1 : 0, c = k2 ? k2[i] >> b & 1 : 0; if (a && c) padd(f, &R, &R, &PQ); else if (a) padd(f, &R, &R, P); else if (c) padd(f, &R, &R, Q); } *r = R; }
int ecdsa_verify(int cv, const u8 *pub, int pl, const u8 *hash, int hl, const u8 *r, int rl, const u8 *s, int sl) {
    const u8 *P, *N, *G; int L = curve(cv, &P, &N, &G); if (pl != 1 + 2 * L || pub[0] != 4) return -1; while (rl > L && !*r) { r++; rl--; } while (sl > L && !*s) { s++; sl--; } if (rl > L || sl > L) return -1;
    zmont *fp = __builtin_alloca(sizeof(zmont)), *fn = __builtin_alloca(sizeof(zmont)); mont_init(fp, P, L); mont_init(fn, N, L); int n = fn->n;
    u32 R[ML], S[ML], E[ML], w[ML], u1[ML], u2[ML], t[ML]; be2l(R, n, r, rl); be2l(S, n, s, sl); if (liszero(R, n) || liszero(S, n) || lcmp(R, fn->m, n) >= 0 || lcmp(S, fn->m, n) >= 0) return -1;
    be2l(E, n, hash, hl < L ? hl : L); if (hl < L) { u8 hb[64]; zset(hb, 0, L); zcpy(hb + L - hl, hash, hl); be2l(E, n, hb, L); } if (lcmp(E, fn->m, n) >= 0) lsub(E, E, fn->m, n);
    u8 e2[64]; zcpy(e2, N, L); e2[L - 1] -= 2; to_m(fn, t, S); mexp(fn, w, t, e2, L); to_m(fn, t, E); mmul(fn, u1, t, w); from_m(fn, u1, u1); to_m(fn, t, R); mmul(fn, u2, t, w); from_m(fn, u2, u2);
    u8 k1[64], k2[64]; l2be(k1, L, u1, n); l2be(k2, L, u2, n); jpt g, q, X; pt_load(fp, &g, G, L); pt_load(fp, &q, pub + 1, L); smul2(fp, &X, k1, &g, k2, &q, L);
    if (liszero(X.z, fp->n)) return -1; u32 x[ML]; pt_affx(fp, x, &X, P, L); if (lcmp(x, fn->m, n) >= 0) lsub(x, x, fn->m, n); return lcmp(x, R, n) ? -1 : 0; }
int ecdh_pub(int cv, u8 *pub, const u8 *priv) { const u8 *P, *N, *G; int L = curve(cv, &P, &N, &G); zmont *fp = __builtin_alloca(sizeof(zmont)); mont_init(fp, P, L); jpt g, X; pt_load(fp, &g, G, L); smul2(fp, &X, priv, &g, 0, &g, L); u32 x[ML], y[ML]; pt_affxy(fp, x, y, &X, P, L); pub[0] = 4; l2be(pub + 1, L, x, fp->n); l2be(pub + 1 + L, L, y, fp->n); return 0; }
int ecdh(int cv, u8 *sh, const u8 *priv, const u8 *peer) { const u8 *P, *N, *G; int L = curve(cv, &P, &N, &G); if (peer[0] != 4) return -1; zmont *fp = __builtin_alloca(sizeof(zmont)); mont_init(fp, P, L); jpt q, X; pt_load(fp, &q, peer + 1, L); smul2(fp, &X, priv, &q, 0, &q, L); if (liszero(X.z, fp->n)) return -1; u32 x[ML]; pt_affx(fp, x, &X, P, L); l2be(sh, L, x, fp->n); return 0; }
int ecdh_p256_pub(u8 *pub, const u8 *priv) { return ecdh_pub(256, pub, priv); }
int ecdh_p256(u8 *sh, const u8 *priv, const u8 *peer) { return ecdh(256, sh, priv, peer); }
/* ===================== aleatorio: SHA-256 sobre fluctuaciones del reloj del procesador ===================== */
static u8 pool[32]; static u64 rcnt; static int seeded;
static u64 tsc(void) {
#ifdef ZC_HOST
    static u64 x = 88172645463325252ULL; x ^= x << 13; x ^= x >> 7; x ^= x << 17; return x;
#else
    return z_cycles();
#endif
}
void zc_random(u8 *out, int n) {
    if (!seeded) { zsha256 c; sha256_init(&c); for (int i = 0; i < 4096; i++) { u64 a = tsc(); volatile u32 k = 0; for (int j = 0; j < (int)(a & 63); j++) k += j; u64 b = tsc(); u64 d = b - a; sha256_up(&c, &d, 8); sha256_up(&c, &a, 8); }
#ifndef ZC_HOST
        u64 ms = z_ms(); sha256_up(&c, &ms, 8);
        /* lo que cambia de un equipo a otro: la MAC de la tarjeta de red y la hora (dos equipos iguales arrancados a la vez daban lo mismo) */
        znet ni; if (!z_netinfo(&ni)) sha256_up(&c, &ni, sizeof ni); u32 t6[6]; z_time(t6); sha256_up(&c, t6, sizeof t6);
#endif
        sha256_end(&c, pool); seeded = 1; }
    while (n > 0) { u8 b[48], o[32]; zcpy(b, pool, 32); u64 t = tsc(); zcpy(b + 32, &t, 8); rcnt++; zcpy(b + 40, &rcnt, 8); zc_hash(256, b, 48, o); b[47] ^= 0x5a; zc_hash(256, b, 48, pool); int k = n < 32 ? n : 32; zcpy(out, o, k); out += k; n -= k; } }
