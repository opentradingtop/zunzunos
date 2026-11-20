/* Prueba del cliente TLS 1.3 desde el Mac contra webs reales: GET / y estado del certificado */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include "../../user/lib/ztls.h"
static long snd(void *io, const void *d, long n) { return send(*(int *)io, d, n, 0); }
static long rcv(void *io, void *d, long n, int to) { struct timeval tv = {to / 1000, 0}; setsockopt(*(int *)io, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv); return recv(*(int *)io, d, n, 0); }
static void *al(long n) { return malloc(n); }
int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb"); static u8 roots[200000]; long rl = fread(roots, 1, sizeof roots, f); fclose(f); tls_set_roots(roots, rl);
    time_t tt = time(0); struct tm *g = gmtime(&tt); long now = (long)(g->tm_year + 1900) * 10000000000L + (g->tm_mon + 1) * 100000000L + g->tm_mday * 1000000L + g->tm_hour * 10000 + g->tm_min * 100 + g->tm_sec;
    int bad = 0;
    for (int i = 2; i < argc; i++) { const char *host = argv[i]; struct addrinfo hints = {0}, *res; hints.ai_socktype = SOCK_STREAM; if (getaddrinfo(host, "443", &hints, &res)) { printf("%-28s sin DNS\n", host); bad++; continue; }
        int s = socket(res->ai_family, SOCK_STREAM, 0); if (connect(s, res->ai_addr, res->ai_addrlen)) { printf("%-28s sin conexion\n", host); bad++; continue; }
        static ztls t; memset(&t, 0, sizeof t); t.io = &s; t.snd = snd; t.rcv = rcv; t.alloc = al; t.free_ = free;
        int r = tls_handshake(&t, host, now); if (r) { printf("%-28s FALLO saludo: %s\n", host, t.err); bad++; close(s); continue; }
        char req[300]; int n = snprintf(req, 300, "GET / HTTP/1.1\r\nHost: %s\r\nUser-Agent: ZunzunOS\r\nConnection: close\r\n\r\n", host); tls_write(&t, req, n);
        char buf[200]; long got = tls_read(&t, buf, 199); buf[got > 0 ? got : 0] = 0; char *e = strchr(buf, '\r'); if (e) *e = 0;
        long total = got; char big[16384]; long k; while ((k = tls_read(&t, big, sizeof big)) > 0) total += k;
        printf("%-28s %s  cert=%s  cifrado=%s  %ld bytes  [%s]\n", host, got > 0 ? "ok" : "FALLO", t.cert_status == 0 ? "valido" : t.cert_status == 1 ? "NO FIABLE" : t.cert_status == 2 ? "OTRO NOMBRE" : "CADUCADO", t.alg == 256 ? "AES128" : "AES256", total, buf);
        if (got <= 0 || t.cert_status) bad++; close(s); }
    printf("RESULTADO: %d con problemas\n", bad); return bad != 0;
}
