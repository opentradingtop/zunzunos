/* Pruebas de user/lib/zcrypto.c en el Mac: vectores oficiales y firmas reales de OpenSSL */
#include <stdio.h>
#include <stdlib.h>
#include "../../user/lib/zcrypto.h"
static int fails;
static int hx(const char *h, u8 *o) { int n = 0; while (h[0] && h[1] && h[0] != ' ' && h[0] != '\n') { unsigned v; sscanf(h, "%2x", &v); o[n++] = v; h += 2; } return n; }
static void chk(const char *name, const u8 *got, const char *want) { u8 w[512]; int n = hx(want, w); int ok = !memcmp(got, w, n); printf("%-28s %s\n", name, ok ? "ok" : "FALLO"); if (!ok) { fails++; for (int i = 0; i < n; i++) printf("%02x", got[i]); printf("\n"); } }
static void res(const char *name, int ok) { printf("%-28s %s\n", name, ok ? "ok" : "FALLO"); if (!ok) fails++; }
int main(int argc, char **argv) {
    u8 o[128];
    zc_hash(256, "abc", 3, o); chk("SHA-256", o, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    zc_hash(384, "abc", 3, o); chk("SHA-384", o, "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7");
    zc_hash(512, "abc", 3, o); chk("SHA-512", o, "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
    { char big[1000]; memset(big, 'a', 1000); zsha256 c; sha256_init(&c); for (int i = 0; i < 1000; i++) sha256_up(&c, big, 1000); sha256_end(&c, o); chk("SHA-256 millon de a", o, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"); }
    zc_hmac(256, (const u8 *)"Jefe", 4, "what do ya want for nothing?", 28, o); chk("HMAC-SHA256 RFC4231", o, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    zc_hmac(384, (const u8 *)"Jefe", 4, "what do ya want for nothing?", 28, o); chk("HMAC-SHA384 RFC4231", o, "af45d2e376484031617f78d2b58a6b1b9c7ef464f5a01b47e42ec3736322445e8e2240ca5e69e2c78b3239ecfab21649");
    { u8 ikm[22], salt[13], info[10], prk[32]; memset(ikm, 0x0b, 22); for (int i = 0; i < 13; i++) salt[i] = i; for (int i = 0; i < 10; i++) info[i] = 0xf0 + i; zc_hkdf_extract(256, salt, 13, ikm, 22, prk); chk("HKDF extract RFC5869", prk, "077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5"); zc_hkdf_expand(256, prk, info, 10, o, 42); chk("HKDF expand RFC5869", o, "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865"); }
    { zgcm g; u8 k[32] = {0}, iv[12] = {0}, p[16] = {0}, c[16], t[16]; gcm_init(&g, k, 16); gcm_seal(&g, iv, 0, 0, p, 16, c, t); chk("AES-128-GCM cifrado", c, "0388dace60b6a392f328c2b971b2fe78"); chk("AES-128-GCM etiqueta", t, "ab6e47d42cec13bdf53a67b21257bddf");
      u8 d[16]; res("AES-128-GCM descifrado", !gcm_open(&g, iv, 0, 0, c, 16, d, t) && !memcmp(d, p, 16)); t[0] ^= 1; res("AES-GCM rechaza alterado", gcm_open(&g, iv, 0, 0, c, 16, d, t) != 0);
      gcm_init(&g, k, 32); gcm_seal(&g, iv, 0, 0, p, 16, c, t); chk("AES-256-GCM cifrado", c, "cea7403d4d606b6e074ec5d3baf39d18"); chk("AES-256-GCM etiqueta", t, "d0d1c8a799996bf0265b98b5d48ab919"); }
    { /* GCM caso 4 (con AAD y longitud no multiplo de 16) */ zgcm g; u8 k[16], iv[12], p[60], a[20], c[60], t[16]; hx("feffe9928665731c6d6a8f9467308308", k); hx("cafebabefacedbaddecaf888", iv); hx("d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39", p); hx("feedfacedeadbeeffeedfacedeadbeefabaddad2", a);
      gcm_init(&g, k, 16); gcm_seal(&g, iv, a, 20, p, 60, c, t); chk("AES-GCM caso 4 cifrado", c, "42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091"); chk("AES-GCM caso 4 etiqueta", t, "5bc94fbc3221a5db94fae95ae7121a47"); }
    { u8 sc[32], u[32]; hx("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4", sc); hx("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c", u); x25519(o, sc, u); chk("X25519 RFC7748", o, "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552");
      u8 a[32], b[32], pa[32], pb[32], s1[32], s2[32]; hx("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", a); hx("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", b); x25519_base(pa, a); chk("X25519 clave publica", pa, "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a"); x25519_base(pb, b); x25519(s1, a, pb); x25519(s2, b, pa); chk("X25519 secreto comun", s1, "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742"); res("X25519 simetrico", !memcmp(s1, s2, 32)); }
    { u8 k[32], P[65], P2[65], s1[32], s2[32], k2[32]; for (int i = 0; i < 32; i++) { k[i] = i + 1; k2[i] = 200 - i; } ecdh_p256_pub(P, k); ecdh_p256_pub(P2, k2); ecdh_p256(s1, k, P2); ecdh_p256(s2, k2, P); res("ECDH P-256 simetrico", !memcmp(s1, s2, 32)); }
    if (argc > 1) { FILE *f = fopen(argv[1], "r"); char ln[4096]; while (fgets(ln, sizeof ln, f)) { char t[8], a[2048], b[2048], c[256], d[256], e[256]; int n = sscanf(ln, "%7s %2047s %2047s %255s %255s %255s", t, a, b, c, d, e);
        static u8 A[1024], B[1024], C[128], D[128], E[128]; int al = hx(a, A), bl = hx(b, B), cl = hx(c, C); u8 ex[3] = {1, 0, 1};
        if (!strcmp(t, "rsa")) { res("RSA PKCS#1 firma real", !rsa_verify_pkcs1(A, al, ex, 3, B, bl, 256, C)); C[0] ^= 1; res("RSA PKCS#1 rechaza falsa", rsa_verify_pkcs1(A, al, ex, 3, B, bl, 256, C) != 0); }
        else if (!strcmp(t, "pss")) { res("RSA-PSS firma real", !rsa_verify_pss(A, al, ex, 3, B, bl, 256, C)); C[0] ^= 1; res("RSA-PSS rechaza falsa", rsa_verify_pss(A, al, ex, 3, B, bl, 256, C) != 0); }
        else if (!strncmp(t, "ec", 2) && n == 5) { int cv = atoi(t + 2); int dl = hx(d, D), el = hx(e, E); /* a=pub b=hash c=r d=s */ u8 *pub = A; int pl = al; u8 *hash = B; int hl = bl; (void)cl; res(cv == 256 ? "ECDSA P-256 firma real" : "ECDSA P-384 firma real", !ecdsa_verify(cv, pub, pl, hash, hl, C, cl, D, dl)); hash[3] ^= 1; res("ECDSA rechaza falsa", ecdsa_verify(cv, pub, pl, hash, hl, C, cl, D, dl) != 0); (void)el; } } }
    printf("%s: %d fallos\n", fails ? "RESULTADO MAL" : "RESULTADO BIEN", fails); return fails != 0;
}
