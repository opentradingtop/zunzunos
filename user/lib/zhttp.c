/* Cliente HTTP/1.1 con HTTPS (TLS 1.3 y 1.2): GET y POST, redirecciones, trozos y gzip */
#include "z.h"
#include "ztls.h"
int z_https_allow_bad; static char allow_host[128];
void z_https_allow(const char *host) { strncpy0(allow_host, host, 128); }
static u8 *roots; static long nroots;
static long atol_(const char *s) { while (*s == ' ') s++; int neg = *s == '-'; if (neg) s++; long v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + *s++ - '0'; return neg ? -v : v; }
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
/* ---------- cookies: se aceptan solas; las que caducan se guardan en /sys/ZUNZUN/COOKIES.TXT
   (el invitado tiene su propia copia en memoria). Formato: dominio, solo_host, ruta, segura, caduca, nombre, valor ---------- */
#define CKF "/sys/ZUNZUN/COOKIES.TXT"
typedef struct { char dom[96], path[96], name[96]; char *val; long exp; u8 host_only, secure; } ck_t;
static ck_t *CK; static int nck, capck, ck_loaded, ck_dirty;
static long days_civ(long y, int m, int d) { y -= m <= 2; long era = (y >= 0 ? y : y - 399) / 400; long yoe = y - era * 400; long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy; return era * 146097 + doe - 719468; }
static long now_epoch(void) { u32 t[6]; z_time(t); return days_civ(t[5], t[4], t[3]) * 86400 + t[2] * 3600 + t[1] * 60 + t[0]; }
static long parse_date(const char *s) { static const char *MO = "janfebmaraprmayjunjulaugsepoctnovdec"; int d = 0, mo = 0, y = 0, hh = 0, mm = 0, ss = 0; for (const char *p = s; *p; ) { if (isdigit(*p)) { int v = 0, k = 0; while (isdigit(p[k])) v = v * 10 + p[k++] - '0'; if (p[k] == ':') { hh = v; mm = atoi(p + k + 1); const char *q = strchr(p + k + 1, ':'); ss = q ? atoi(q + 1) : 0; p += k; while (*p && *p != ' ') p++; continue; } if (k >= 4) y = v; else if (!d) d = v; else if (!y) y = v < 70 ? 2000 + v : v < 100 ? 1900 + v : v; p += k; continue; } if (isalpha(*p)) { char m3[4] = {tolower(p[0]), p[1] ? tolower(p[1]) : 0, p[2] ? tolower(p[2]) : 0, 0}; const char *f = strstr(MO, m3); if (f && strlen(m3) == 3 && (f - MO) % 3 == 0) mo = (f - MO) / 3 + 1; while (isalpha(*p)) p++; continue; } p++; } if (!d || !mo || !y) return 0; return days_civ(y, mo, d) * 86400 + hh * 3600 + mm * 60 + ss; }
static void ck_add(const char *dom, int ho, const char *path, int sec, long exp, const char *name, const char *val) { for (int i = 0; i < nck; i++) if (!strcasecmp(CK[i].dom, dom) && !strcmp(CK[i].path, path) && !strcmp(CK[i].name, name)) { free(CK[i].val); CK[i] = CK[--nck]; break; } if (exp < 0) return; if (nck >= 800) { free(CK[0].val); CK[0] = CK[--nck]; } if (nck == capck) { capck = capck * 2 + 64; CK = realloc(CK, capck * sizeof *CK); } ck_t *c = &CK[nck++]; strncpy0(c->dom, dom, 96); strncpy0(c->path, path, 96); strncpy0(c->name, name, 96); c->val = strdup(val); c->exp = exp; c->host_only = ho; c->secure = sec; }
static void ck_load(void) { if (ck_loaded) return; ck_loaded = 1; char *t = z_readfile(CKF, 0); if (!t) return; long now = now_epoch(); for (char *p = t; p && *p; ) { char *e = strchr(p, '\n'); if (e) *e = 0; char *f[7]; int k = 0; f[k++] = p; for (char *q = p; *q && k < 7; q++) if (*q == '\t') { *q = 0; f[k++] = q + 1; } if (k == 7) { long ex = atol_(f[4]); if (ex > now) ck_add(f[0], atoi(f[1]), f[2], atoi(f[3]), ex, f[5], f[6]); } p = e ? e + 1 : 0; } free(t); }
static void ck_save(void) { if (!ck_dirty) return; ck_dirty = 0; sb_t b = {0}; sb_s(&b, ""); long now = now_epoch(); for (int i = 0; i < nck; i++) if (CK[i].exp > now) sb_f(&b, "%s\t%d\t%s\t%d\t%ld\t%s\t%s\n", CK[i].dom, CK[i].host_only, CK[i].path, CK[i].secure, CK[i].exp, CK[i].name, CK[i].val); z_mkdir("/sys/ZUNZUN"); z_writefile(CKF, b.p, b.n); free(b.p); }
static void ck_set(const char *host, const char *rpath, const char *v) { char line[4200]; int i = 0; while (v[i] && v[i] != '\r' && v[i] != '\n' && i < 4199) { line[i] = v[i]; i++; } line[i] = 0; char *semi = strchr(line, ';'); if (semi) *semi = 0; char *eq = strchr(line, '='); if (!eq || eq == line) return; *eq = 0; char name[96], *val = eq + 1; strncpy0(name, line, 96); { char *t = name + strlen(name); while (t > name && t[-1] == ' ') *--t = 0; } while (*val == ' ') val++;
    char dom[96]; strncpy0(dom, host, 96); int ho = 1, sec = 0; long exp = 0, now = now_epoch(); int has_ma = 0; char path[96]; strncpy0(path, rpath, 96); { char *q = strchr(path, '?'); if (q) *q = 0; char *sl = strrchr(path, '/'); if (sl && sl != path) *sl = 0; else strcpy(path, "/"); }
    for (char *a = semi ? semi + 1 : 0; a && *a; ) { char *nx = strchr(a, ';'); if (nx) *nx++ = 0; while (*a == ' ') a++; char *ae = strchr(a, '='); char *av = ae ? ae + 1 : (char *)""; if (ae) *ae = 0;
        if (!strcasecmp(a, "domain") && *av) { while (*av == '.') av++; int hl = strlen(host), dl = strlen(av); if (dl <= hl && !strcasecmp(host + hl - dl, av) && (dl == hl || host[hl - dl - 1] == '.') && strchr(av, '.')) { strncpy0(dom, av, 96); ho = 0; } else return; }
        else if (!strcasecmp(a, "path") && *av == '/') strncpy0(path, av, 96); else if (!strcasecmp(a, "secure")) sec = 1; else if (!strcasecmp(a, "max-age")) { has_ma = 1; long m = atol_(av); exp = *av == '-' || m <= 0 ? -1 : now + m; } else if (!strcasecmp(a, "expires") && !has_ma) { long t = parse_date(av); if (t) exp = t <= now ? -1 : t; }
        a = nx; }
    ck_add(dom, ho, path, sec, exp, name, val); ck_dirty = 1; }
static int ck_header(char *o, int cap, const char *host, const char *rpath, int tls) { ck_load(); long now = now_epoch(); int n = 0; for (int i = 0; i < nck; i++) { ck_t *c = &CK[i]; if (c->exp && c->exp <= now) continue; if (c->secure && !tls) continue; int hl = strlen(host), dl = strlen(c->dom); if (c->host_only ? strcasecmp(host, c->dom) : !(dl <= hl && !strcasecmp(host + hl - dl, c->dom) && (dl == hl || host[hl - dl - 1] == '.'))) continue; int pl = strlen(c->path); if (strncmp(rpath, c->path, pl) || (pl > 1 && rpath[pl] && rpath[pl] != '/' && rpath[pl] != '?' && c->path[pl - 1] != '/')) continue;
        int need = strlen(c->name) + strlen(c->val) + 3; if (n + need + 20 >= cap) break; n += snprintf(o + n, cap - n, "%s%s=%s", n ? "; " : "Cookie: ", c->name, c->val); } if (n) n += snprintf(o + n, cap - n, "\r\n"); return n; }
int z_cookies_count(void) { ck_load(); return nck; }
int z_cookies_clear(void) { ck_load(); int n = nck; for (int i = 0; i < nck; i++) free(CK[i].val); nck = 0; ck_dirty = 0; z_unlink(CKF); z_sync(); return n; }
int z_http(const char *method, const char *url, const char *body, long bl, const char *ctype, char **out, long *olen, zhttp_info *inf) {
    char cur[1100]; strncpy0(cur, url, sizeof cur); zhttp_info dummy; if (!inf) inf = &dummy; memset(inf, 0, sizeof *inf); const char *meth = method ? method : "GET";
    if (!roots) { roots = (u8 *)z_readfile("/rd/res/raices.bin", &nroots); if (roots) tls_set_roots(roots, nroots); }
    for (int hop = 0; hop < 8; hop++) {
        char host[128], path[1024]; int port, tls; int e = split_url(cur, &tls, host, &port, path); if (e) return e; inf->tls = tls;
        u32 ip; e = z_dns(host, &ip); if (e) return e; conn_t c = {z_socket(1), 0}; if (c.s < 0) return c.s; e = z_connect(c.s, ip, port, 10000); if (e) { z_close(c.s); return e; }
        if (tls) { ztls *t = calloc(1, sizeof(ztls)); t->io = &c.s; t->snd = io_snd; t->rcv = io_rcv; t->alloc = io_alloc; t->free_ = free;
            if (tls_handshake(t, host, now_num())) { strncpy0(inf->err, t->err, 96); free(t); z_close(c.s); return -1004; }
            inf->cert = t->cert_status; if (t->cert_status && !z_https_allow_bad && strcmp(allow_host, host)) { free(t); z_close(c.s); return -1000 - t->cert_status; } c.t = t; }
        char req[9000]; int n = snprintf(req, sizeof req, "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: Mozilla/5.0 (ZunzunOS) ZunzunNavegador/2.2\r\nAccept: text/html,application/xhtml+xml,image/png,image/jpeg,image/gif,*/*;q=0.5\r\nAccept-Encoding: gzip\r\nAccept-Language: es-ES,es;q=0.9,en;q=0.7\r\nConnection: close\r\n", meth, path, host);
        n += ck_header(req + n, 6000, host, path, tls);
        if (body) n += snprintf(req + n, sizeof req - n, "Content-Type: %s\r\nContent-Length: %ld\r\n", ctype ? ctype : "application/x-www-form-urlencoded", bl);
        n += snprintf(req + n, sizeof req - n, "\r\n"); if (c_send(&c, req, n) < 0 || (body && bl && c_send(&c, body, bl) < 0)) { if (c.t) free(c.t); z_close(c.s); return -5; }
        long cap = 65536, got = 0; char *b = malloc(cap + 1);
        for (;;) { if (got == cap) { if (cap >= (48L << 20)) break; cap *= 2; char *nb = realloc(b, cap + 1); if (!nb) break; b = nb; } long r = c_recv(&c, b + got, cap - got); if (r <= 0) break; got += r; }
        if (c.t) free(c.t); z_close(c.s); b[got] = 0;
        char *he = strstr(b, "\r\n\r\n"); if (!he) { free(b); return -5; } *he = 0;
        for (char *hl = b; hl && *hl; ) { if (!strncasecmp(hl, "set-cookie:", 11)) { const char *cv = hl + 11; while (*cv == ' ') cv++; ck_set(host, path, cv); } hl = strchr(hl, '\n'); if (hl) hl++; } ck_save(); char *data = he + 4; long dl = got - (data - b); char *sp = strchr(b, ' '); inf->status = sp ? atoi(sp + 1) : 0;
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
