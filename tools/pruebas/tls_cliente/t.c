/* Prueba en el Mac del TLS propio cuando el servidor pide certificado de cliente (CertificateRequest): ZunzunOS no
   tiene certificado y tiene que contestar con uno vacio y seguir, en TLS 1.3 y en TLS 1.2 (smtp.gmail.com lo pide).
   Uso: t RAICES HOST PUERTO MODO   (MODO www: GET / a openssl s_server -www; smtp: lee el saludo 220 y dice QUIT) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include "ztls.h"
static long snd(void *io, const void *d, long n) { return send(*(int *)io, d, n, 0); }
static long rcv(void *io, void *d, long n, int to) { struct timeval tv = {5, 0}; setsockopt(*(int *)io, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv); return recv(*(int *)io, d, n, 0); }
static void *al(long n) { return malloc(n); }
int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb"); static u8 roots[200000]; long rl = fread(roots, 1, sizeof roots, f); fclose(f); tls_set_roots(roots, rl);
    time_t tt = time(0); struct tm *g = gmtime(&tt); long now = (long)(g->tm_year + 1900) * 10000000000L + (g->tm_mon + 1) * 100000000L + g->tm_mday * 1000000L + g->tm_hour * 10000 + g->tm_min * 100 + g->tm_sec;
    struct addrinfo hints = {0}, *res; hints.ai_socktype = SOCK_STREAM; hints.ai_family = AF_INET; if (getaddrinfo(argv[2], argv[3], &hints, &res)) { printf("sin DNS\n"); return 1; }
    int s = socket(res->ai_family, SOCK_STREAM, 0); if (connect(s, res->ai_addr, res->ai_addrlen)) { printf("sin conexion\n"); return 1; }
    static ztls t; memset(&t, 0, sizeof t); t.io = &s; t.snd = snd; t.rcv = rcv; t.alloc = al; t.free_ = free;
    if (tls_handshake(&t, argv[2], now)) { printf("FALLO saludo: %s\n", t.err); return 1; }
    char buf[300]; long got;
    if (!strcmp(argv[4], "smtp")) { got = tls_read(&t, buf, 299); buf[got > 0 ? got : 0] = 0; char *e = strchr(buf, '\r'); if (e) *e = 0; tls_write(&t, "QUIT\r\n", 6);
        printf("%s TLS 1.%d cert=%d: %s\n", got > 0 && !strncmp(buf, "220", 3) ? "ok" : "FALLO", t.v12 ? 2 : 3, t.cert_status, buf); return !(got > 0 && !strncmp(buf, "220", 3)); }
    tls_write(&t, "GET / HTTP/1.0\r\n\r\n", 18); got = tls_read(&t, buf, 299); buf[got > 0 ? got : 0] = 0; char *e = strchr(buf, '\r'); if (e) *e = 0;
    int ok = !strncmp(buf, "HTTP/1.0 200", 12); printf("%s TLS 1.%d: %s\n", ok ? "ok" : "FALLO", t.v12 ? 2 : 3, buf); return !ok; }
