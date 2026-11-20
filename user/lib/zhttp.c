/* Cliente HTTP/1.1 con HTTPS (TLS 1.3 y 1.2): GET y POST, redirecciones, trozos y gzip */
#include "z.h"
#include "ztls.h"
int z_https_allow_bad; static char allow_host[128];
void z_https_allow(const char *host) { strncpy0(allow_host, host, 128); }
static u8 *roots; static long nroots;
static long io_snd(void *io, const void *d, long n) { return z_send(*(int *)io, d, n); }
static long io_rcv(void *io, void *d, long n, int to) { return z_recv(*(int *)io, d, n, to); }
static void *io_alloc(long n) { return malloc(n); }
typedef struct { int s; ztls *t; } conn_t;
static long c_send(conn_t *c, const void *d, long n) { if (c->t) return tls_write(c->t, d, n); long o = 0; while (o < n) { long r = z_send(c->s, (const u8 *)d + o, n - o); if (r <= 0) return -1; o += r; } return n; }
static long c_recv(conn_t *c, void *d, long n) { return c->t ? tls_read(c->t, d, n) : z_recv(c->s, d, n, 20000); }
static int split_url(const char *u, int *tls, char *host, int *port, char *path) {
    *tls = 0; if (!strncasecmp(u, "https://", 8)) { *tls = 1; u += 8; } else if (!strncasecmp(u, "http://", 7)) u += 7; else return -22;
    int i = 0; *port = *tls ? 443 : 80; while (*u && *u != '/' && *u != ':' && *u != '?' && i < 127) host[i++] = *u++; host[i] = 0;
    if (*u == ':') { *port = atoi(u + 1); while (*u && *u != '/' && *u != '?') u++; }
    if (*u == '?') { path[0] = '/'; strncpy0(path + 1, u, 1023); } else strncpy0(path, *u ? u : "/", 1024); char *h = strchr(path, '#'); if (h) *h = 0; return host[0] ? 0 : -22; }
static int isxd(int c) { return isdigit(c) || (c | 32) >= 'a' && (c | 32) <= 'f'; }
static long now_num(void) { u32 t[6]; z_time(t); return (long)t[5] * 10000000000L + (long)t[4] * 100000000L + (long)t[3] * 1000000L + t[2] * 10000 + t[1] * 100 + t[0]; }
static const char *hdr(const char *h, const char *name) { int l = strlen(name); for (const char *p = h; p && *p; ) { if (!strncasecmp(p, name, l) && p[l] == ':') { p += l + 1; while (*p == ' ') p++; return p; } p = strchr(p, '\n'); if (p) p++; } return 0; }
static long gunzip(const u8 *d, long n, u8 **out) {
    if (n < 18 || d[0] != 0x1f || d[1] != 0x8b || d[2] != 8) return -1; int fl = d[3]; long p = 10; if (fl & 4) p += 2 + (d[p] | d[p + 1] << 8); if (fl & 8) { while (p < n && d[p]) p++; p++; } if (fl & 16) { while (p < n && d[p]) p++; p++; } if (fl & 2) p += 2; if (p >= n) return -1;
    u32 isz = d[n - 4] | d[n - 3] << 8 | d[n - 2] << 16 | (u32)d[n - 1] << 24; long cap = isz && isz < (64u << 20) ? isz + 16 : n * 8 + 65536; *out = malloc(cap + 1); long r = z_inflate(d + p, n - p, *out, cap); if (r < 0) { free(*out); return -1; } (*out)[r] = 0; return r; }
int z_http(const char *method, const char *url, const char *body, long bl, const char *ctype, char **out, long *olen, zhttp_info *inf) {
    char cur[1100]; strncpy0(cur, url, sizeof cur); zhttp_info dummy; if (!inf) inf = &dummy; memset(inf, 0, sizeof *inf); const char *meth = method ? method : "GET";
    if (!roots) { roots = (u8 *)z_readfile("/rd/res/raices.bin", &nroots); if (roots) tls_set_roots(roots, nroots); }
    for (int hop = 0; hop < 8; hop++) {
        char host[128], path[1024]; int port, tls; int e = split_url(cur, &tls, host, &port, path); if (e) return e; inf->tls = tls;
        u32 ip; e = z_dns(host, &ip); if (e) return e; conn_t c = {z_socket(1), 0}; if (c.s < 0) return c.s; e = z_connect(c.s, ip, port, 10000); if (e) { z_close(c.s); return e; }
        if (tls) { ztls *t = calloc(1, sizeof(ztls)); t->io = &c.s; t->snd = io_snd; t->rcv = io_rcv; t->alloc = io_alloc; t->free_ = free;
            if (tls_handshake(t, host, now_num())) { strncpy0(inf->err, t->err, 96); free(t); z_close(c.s); return -1004; }
            inf->cert = t->cert_status; if (t->cert_status && !z_https_allow_bad && strcmp(allow_host, host)) { free(t); z_close(c.s); return -1000 - t->cert_status; } c.t = t; }
        char req[2600]; int n = snprintf(req, sizeof req, "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: Mozilla/5.0 (ZunzunOS) ZunzunNavegador/2.2\r\nAccept: text/html,application/xhtml+xml,image/png,image/jpeg,image/gif,*/*;q=0.5\r\nAccept-Encoding: gzip\r\nAccept-Language: es-ES,es;q=0.9,en;q=0.7\r\nConnection: close\r\n", meth, path, host);
        if (body) n += snprintf(req + n, sizeof req - n, "Content-Type: %s\r\nContent-Length: %ld\r\n", ctype ? ctype : "application/x-www-form-urlencoded", bl);
        n += snprintf(req + n, sizeof req - n, "\r\n"); if (c_send(&c, req, n) < 0 || (body && bl && c_send(&c, body, bl) < 0)) { if (c.t) free(c.t); z_close(c.s); return -5; }
        long cap = 65536, got = 0; char *b = malloc(cap + 1);
        for (;;) { if (got == cap) { if (cap >= (48L << 20)) break; cap *= 2; char *nb = realloc(b, cap + 1); if (!nb) break; b = nb; } long r = c_recv(&c, b + got, cap - got); if (r <= 0) break; got += r; }
        if (c.t) free(c.t); z_close(c.s); b[got] = 0;
        char *he = strstr(b, "\r\n\r\n"); if (!he) { free(b); return -5; } *he = 0; char *data = he + 4; long dl = got - (data - b); char *sp = strchr(b, ' '); inf->status = sp ? atoi(sp + 1) : 0;
        const char *ct = hdr(b, "content-type"); inf->ctype[0] = 0; if (ct) { int i = 0; while (ct[i] && ct[i] != '\r' && i < 95) { inf->ctype[i] = tolower(ct[i]); i++; } inf->ctype[i] = 0; }
        if (inf->status >= 300 && inf->status < 400) { const char *l = hdr(b, "location"); if (l) { char loc[1100]; int i = 0; while (l[i] && l[i] != '\r' && l[i] != '\n' && i < 1099) { loc[i] = l[i]; i++; } loc[i] = 0;
                if (!strncasecmp(loc, "http", 4)) strcpy(cur, loc); else if (loc[0] == '/' && loc[1] == '/') snprintf(cur, sizeof cur, "%s:%s", tls ? "https" : "http", loc); else if (loc[0] == '/') { if (port != (tls ? 443 : 80)) snprintf(cur, sizeof cur, "%s://%s:%d%s", tls ? "https" : "http", host, port, loc); else snprintf(cur, sizeof cur, "%s://%s%s", tls ? "https" : "http", host, loc); } else { char *s = strrchr(cur, '/'); if (s) strncpy0(s + 1, loc, sizeof cur - (s + 1 - cur)); }
                if (inf->status != 307 && inf->status != 308) { meth = "GET"; body = 0; } free(b); continue; } }
        const char *te = hdr(b, "transfer-encoding"); if (te && !strncasecmp(te, "chunked", 7)) { char *r = data, *w = data, *end = data + dl;
            while (r < end) { long cl = 0; while (r < end && isxd(*r)) { cl = cl * 16 + (isdigit(*r) ? *r - '0' : (tolower(*r) - 'a' + 10)); r++; } while (r < end && *r != '\n') r++; r++; if (!cl || r + cl > end) { if (cl && r < end) { long k = end - r; memmove(w, r, k); w += k; } break; } memmove(w, r, cl); w += cl; r += cl + 2; } dl = w - data; }
        const char *ce = hdr(b, "content-encoding"); u8 *dec = 0; if (ce && !strncasecmp(ce, "gzip", 4)) { long r = gunzip((u8 *)data, dl, &dec); if (r >= 0) { data = (char *)dec; dl = r; } }
        char *o = malloc(dl + 1); memcpy(o, data, dl); o[dl] = 0; free(dec); free(b); *out = o; *olen = dl; strncpy0(inf->final, cur, sizeof inf->final); return 0; }
    return -40; }
int z_http_get(const char *url, char **body, long *len, char *fin, int fcap, int *status) { zhttp_info i; int e = z_http("GET", url, 0, 0, 0, body, len, &i); if (status) *status = i.status; if (fin && !e) strncpy0(fin, i.final, fcap); return e; }
const char *z_http_err(int e) { return e == -1001 ? tr("certificado no fiable") : e == -1002 ? tr("el certificado es de otro sitio") : e == -1003 ? tr("certificado caducado") : e == -1004 ? tr("fallo en la conexion segura") : z_errstr(e); }
