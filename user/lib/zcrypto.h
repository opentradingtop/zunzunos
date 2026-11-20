/* Criptografia para TLS: SHA-2, HMAC/HKDF, AES-GCM, X25519, RSA y ECDSA (P-256/P-384) */
#pragma once
#ifdef ZC_HOST
#include <stdint.h>
#include <string.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64; typedef int64_t i64;
#else
#include "z.h"
#endif
typedef struct { u32 h[8]; u64 len; u8 buf[64]; int n; } zsha256;
typedef struct { u64 h[8]; u64 len; u8 buf[128]; int n; int out; } zsha512;
void sha256_init(zsha256 *c); void sha256_up(zsha256 *c, const void *d, long n); void sha256_end(zsha256 *c, u8 *out);
void sha512_init(zsha512 *c, int is384); void sha512_up(zsha512 *c, const void *d, long n); void sha512_end(zsha512 *c, u8 *out);
void zc_hash(int alg, const void *d, long n, u8 *out); /* alg: 256, 384, 512 */
int zc_hlen(int alg);
/* hash incremental generico */
typedef struct { int alg; union { zsha256 a; zsha512 b; } u; } zhash;
void zh_init(zhash *h, int alg); void zh_up(zhash *h, const void *d, long n); void zh_end(zhash *h, u8 *out);
void zc_hmac(int alg, const u8 *key, int kl, const void *d, long n, u8 *out);
void zc_hkdf_extract(int alg, const u8 *salt, int sl, const u8 *ikm, int il, u8 *out);
void zc_hkdf_expand(int alg, const u8 *prk, const u8 *info, int il, u8 *out, int ol);
/* AES-GCM */
typedef struct { u32 rk[60]; int nr; u8 H[16]; } zgcm;
void gcm_init(zgcm *g, const u8 *key, int klen);
void gcm_seal(zgcm *g, const u8 *iv, const u8 *aad, int al, const u8 *in, long n, u8 *out, u8 *tag);
int gcm_open(zgcm *g, const u8 *iv, const u8 *aad, int al, const u8 *in, long n, u8 *out, const u8 *tag);
/* X25519 */
void x25519(u8 *out, const u8 *scalar, const u8 *point); void x25519_base(u8 *out, const u8 *scalar);
/* firmas */
int rsa_verify_pkcs1(const u8 *n, int nl, const u8 *e, int el, const u8 *sig, int sl, int alg, const u8 *hash);
int rsa_verify_pss(const u8 *n, int nl, const u8 *e, int el, const u8 *sig, int sl, int alg, const u8 *hash);
int ecdsa_verify(int curve, const u8 *pub, int pl, const u8 *hash, int hl, const u8 *r, int rl, const u8 *s, int sl); /* curve 256/384, pub = 04|X|Y */
int ecdh_pub(int curve, u8 *pub, const u8 *priv); int ecdh(int curve, u8 *shared, const u8 *priv, const u8 *peer_pub); int ecdh_p256(u8 *shared, const u8 *priv, const u8 *peer_pub); int ecdh_p256_pub(u8 *pub, const u8 *priv);
void zc_random(u8 *out, int n);
