/* Alquimia: cifrado de ZIP. WinZip AES (AE-1 y AE-2: PBKDF2-HMAC-SHA1 de 1000 vueltas, AES en modo CTR con el
   contador en little-endian y HMAC-SHA1 de 10 bytes), compatible con 7-Zip, WinRAR y WinZip; y el ZipCrypto
   tradicional, solo para abrir archivos antiguos. El AES por bloques es el del nucleo criptografico (zcrypto.c). */
typedef struct { u32 h[5]; u64 len; u8 b[64]; int n; } sha1_t;
static u32 rol(u32 x, int n) { return x << n | x >> (32 - n); }
static void sha1_blk(sha1_t *c, const u8 *p) {
    u32 w[80]; for (int i = 0; i < 16; i++) w[i] = (u32)p[4 * i] << 24 | (u32)p[4 * i + 1] << 16 | (u32)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    u32 a = c->h[0], b = c->h[1], d = c->h[2], e = c->h[3], f = c->h[4];
    for (int i = 0; i < 80; i++) { u32 k, t; if (i < 20) { t = (b & d) | (~b & e); k = 0x5A827999; } else if (i < 40) { t = b ^ d ^ e; k = 0x6ED9EBA1; } else if (i < 60) { t = (b & d) | (b & e) | (d & e); k = 0x8F1BBCDC; } else { t = b ^ d ^ e; k = 0xCA62C1D6; }
        u32 x = rol(a, 5) + t + f + k + w[i]; f = e; e = d; d = rol(b, 30); b = a; a = x; }
    c->h[0] += a; c->h[1] += b; c->h[2] += d; c->h[3] += e; c->h[4] += f; }
static void sha1_init(sha1_t *c) { static const u32 H[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0}; memcpy(c->h, H, 20); c->len = 0; c->n = 0; }
static void sha1_up(sha1_t *c, const void *d, long n) { const u8 *p = d; c->len += n; while (n > 0) { if (!c->n && n >= 64) { sha1_blk(c, p); p += 64; n -= 64; continue; } int k = (int)MIN((long)(64 - c->n), n); memcpy(c->b + c->n, p, k); c->n += k; p += k; n -= k; if (c->n == 64) { sha1_blk(c, c->b); c->n = 0; } } }
static void sha1_end(sha1_t *c, u8 *o) { u64 bits = c->len * 8; u8 z = 0x80; sha1_up(c, &z, 1); z = 0; while (c->n != 56) sha1_up(c, &z, 1); u8 l[8]; for (int i = 0; i < 8; i++) l[i] = bits >> (56 - 8 * i); sha1_up(c, l, 8); for (int i = 0; i < 20; i++) o[i] = c->h[i / 4] >> (24 - 8 * (i % 4)); }
/* HMAC-SHA1 incremental */
typedef struct { sha1_t in, out; } hmac1_t;
static void hmac1_init(hmac1_t *m, const u8 *k, int kl) { u8 kb[64] = {0}, t[64]; if (kl > 64) { sha1_t c; sha1_init(&c); sha1_up(&c, k, kl); sha1_end(&c, kb); } else memcpy(kb, k, kl);
    for (int i = 0; i < 64; i++) t[i] = kb[i] ^ 0x36; sha1_init(&m->in); sha1_up(&m->in, t, 64); for (int i = 0; i < 64; i++) t[i] = kb[i] ^ 0x5C; sha1_init(&m->out); sha1_up(&m->out, t, 64); }
static void hmac1_end(hmac1_t *m, u8 *o) { u8 t[20]; sha1_end(&m->in, t); sha1_up(&m->out, t, 20); sha1_end(&m->out, o); }
static void pbkdf2_sha1(const char *pw, const u8 *salt, int sl, int iters, u8 *out, int ol) {
    int pl = strlen(pw); hmac1_t base; hmac1_init(&base, (const u8 *)pw, pl);   /* la clave es siempre la misma: se precalcula */
    for (int blk = 1, o = 0; o < ol; blk++) { u8 u[20], t[20], c[4] = {blk >> 24, blk >> 16, blk >> 8, blk};
        hmac1_t m = base; sha1_up(&m.in, salt, sl); sha1_up(&m.in, c, 4); hmac1_end(&m, u); memcpy(t, u, 20);
        for (int i = 1; i < iters; i++) { m = base; sha1_up(&m.in, u, 20); hmac1_end(&m, u); for (int j = 0; j < 20; j++) t[j] ^= u[j]; }
        int k = MIN(20, ol - o); memcpy(out + o, t, k); o += k; }
}
/* WinZip AES: fuerza 1, 2, 3 = AES-128, 192, 256 */
static int aes_klen(int st) { return st == 1 ? 16 : st == 2 ? 24 : 32; }
typedef struct { zgcm k; u8 ctr[16], ks[16]; int ki; hmac1_t mac; } wza_t;
/* deriva las claves; pv recibe los 2 bytes de comprobacion */
static void wza_init(wza_t *a, const char *pw, const u8 *salt, int st, u8 *pv) {
    int kl = aes_klen(st); u8 dk[66]; pbkdf2_sha1(pw, salt, kl / 2, 1000, dk, 2 * kl + 2);
    zc_aes_key(&a->k, dk, kl); hmac1_init(&a->mac, dk + kl, kl); memcpy(pv, dk + 2 * kl, 2); memset(a->ctr, 0, 16); a->ki = 16; }
static void wza_xor(wza_t *a, u8 *p, long n) { for (long i = 0; i < n; i++) { if (a->ki == 16) { for (int j = 0; j < 16; j++) if (++a->ctr[j]) break; zc_aes_enc(&a->k, a->ctr, a->ks); a->ki = 0; } p[i] ^= a->ks[a->ki++]; } }
static void wza_enc(wza_t *a, u8 *p, long n) { wza_xor(a, p, n); sha1_up(&a->mac.in, p, n); }
static void wza_dec(wza_t *a, u8 *p, long n) { sha1_up(&a->mac.in, p, n); wza_xor(a, p, n); }
static void wza_tag(wza_t *a, u8 *tag10) { u8 t[20]; hmac1_end(&a->mac, t); memcpy(tag10, t, 10); }
/* ZipCrypto (PKWARE tradicional): solo descifrar */
typedef struct { u32 k0, k1, k2; } zc_t;
static void zcr_up(zc_t *z, u8 c) { z->k0 = ~z_crc_add(~z->k0, &c, 1); z->k1 = (z->k1 + (z->k0 & 255)) * 134775813 + 1; u8 b = z->k1 >> 24; z->k2 = ~z_crc_add(~z->k2, &b, 1); }
static u8 zcr_byte(zc_t *z) { u32 t = (z->k2 & 0xFFFF) | 2; return (t * (t ^ 1)) >> 8; }
static void zcr_init(zc_t *z, const char *pw) { z->k0 = 0x12345678; z->k1 = 0x23456789; z->k2 = 0x34567890; for (; *pw; pw++) zcr_up(z, (u8)*pw); }
static void zcr_dec(zc_t *z, u8 *p, long n) { for (long i = 0; i < n; i++) { u8 c = p[i] ^ zcr_byte(z); zcr_up(z, c); p[i] = c; } }
