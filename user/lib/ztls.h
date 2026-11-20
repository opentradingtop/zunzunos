/* Cliente TLS 1.3 */
#pragma once
#include "zcrypto.h"
enum { TLS_OK = 0, TLS_EUNTRUSTED = 1, TLS_EHOST = 2, TLS_EEXPIRED = 3 };
typedef struct { int type; const u8 *a, *b; long al, bl; } zkey; /* type: 1 RSA (a=n, b=e), 256/384 ECDSA (a = punto 04|X|Y) */
typedef struct { const u8 *tbs, *issuer, *subject, *sig, *san; long tbsl, il, sl, sigl, sanl; int halg, sigrsa, ca; long nb, na; zkey key; } zcert;
typedef struct ztls {
    void *io; long (*snd)(void *, const void *, long); long (*rcv)(void *, void *, long, int); void *(*alloc)(long); void (*free_)(void *);
    int alg, keylen, enc, renc, closed, cert_status, v12; zgcm wk, rk; u8 wiv[12], riv[12], sap[64]; u64 wseq, rseq;
    u8 *tr; long trl, trcap; u8 rec[16384 + 512]; long recl, aoff, alen; char err[96];
} ztls;
void tls_set_roots(const u8 *data, long n);
int tls_handshake(ztls *t, const char *host, long now_yyyymmddhhmmss); /* 0 bien; cert_status dice si el certificado es de confianza */
long tls_write(ztls *t, const void *d, long n); long tls_read(ztls *t, void *d, long n);
int x509_parse(const u8 *c, long cl, zcert *x); int x509_chain(zcert *cs, int n, const char *host, long now);
