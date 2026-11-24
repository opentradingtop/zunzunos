/* Vectores oficiales: AES-256 (FIPS-197, apendice C.3) y PBKDF2-HMAC-SHA256 (RFC 7914, seccion 11) */
#include "kernel.h"
volatile u64 ticks; u8 net_first_mac[6];
#include "sha.inc"
#include "cifrado.c"
static int hx(const char *s, u8 *o) { int n = 0; while (s[0] && s[1]) { unsigned v; sscanf(s, "%2x", &v); o[n++] = v; s += 2; while (*s == ' ') s++; } return n; }
int main(void) { u8 k[32], in[16], want[16], out[16]; int ok = 1;
    hx("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", k); hx("00112233445566778899aabbccddeeff", in); hx("8ea2b7ca516745bfeafc49904b496089", want);
    crypt_aes_test(k, in, out); if (memcmp(out, want, 16)) { printf("FALLO: AES-256\n"); ok = 0; } else printf("AES-256 FIPS-197: bien\n");
    u8 dk[64], w2[64]; hx("55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783", w2);
    crypt_pbkdf2((const u8 *)"passwd", 6, (const u8 *)"salt", 4, 1, dk, 64); if (memcmp(dk, w2, 64)) { printf("FALLO: PBKDF2\n"); ok = 0; } else printf("PBKDF2-HMAC-SHA256 RFC 7914: bien\n");
    printf("cifrado: %s\n", ok ? "OK" : "FALLO"); return !ok; }
