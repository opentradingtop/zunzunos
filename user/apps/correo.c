/* Correo: Gmail, iCloud, Yahoo u otro servidor por IMAP (recibir) y SMTP (enviar), siempre con TLS
   y certificados verificados. Se entra con una "contrasena de aplicacion" del proveedor.
   Lee la bandeja de entrada, muestra texto (los correos en HTML se pasan a texto), escribe,
   responde, reenvia, borra e imprime. La cuenta se guarda en /sys/ZUNZUN/CORREO.TXT. */
#include "z.h"
#include "ztls.h"
#define CFG "/sys/ZUNZUN/CORREO.TXT"
#define TB 40
#define NMAX 60
static zwin *W, *CW; static char msg[200];
void draw(void);
static long atol(const char *s) { while (*s == ' ') s++; long v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + *s++ - '0'; return v; }
static int isalnum(int c) { return isalpha(c) || isdigit(c); }
static char *sep_(char *s) { for (; *s; s++) if (*s == ',' || *s == ';') return s; return 0; }
static int acct_ok_cache = -1; static int acct_load_ok(void) { return acct_ok_cache > 0; }
/* ---------- cuenta ---------- */
typedef struct { char prov[12], name[64], user[128], pass[128], imap[96], smtp[96]; int iport, sport, starttls; } acct_t;
static acct_t A;
static const struct { const char *id, *imap, *smtp; int sport, starttls; const char *help; } PROV[] = {
    {"gmail", "imap.gmail.com", "smtp.gmail.com", 465, 0, "Gmail: activa la verificación en dos pasos y crea una contraseña de aplicación en myaccount.google.com/apppasswords"},
    {"icloud", "imap.mail.me.com", "smtp.mail.me.com", 587, 1, "iCloud: crea una contraseña específica para apps en account.apple.com (Inicio de sesión y seguridad)"},
    {"yahoo", "imap.mail.yahoo.com", "smtp.mail.yahoo.com", 465, 0, "Yahoo: crea una contraseña de aplicación en login.yahoo.com/account/security"},
    {"otro", "", "", 465, 0, "Otro proveedor: escribe sus servidores IMAP (puerto 993) y SMTP (465 con TLS o 587 con STARTTLS)"}};
static int prov_i(const char *p) { for (int i = 0; i < 4; i++) if (!strcmp(p, PROV[i].id)) return i; return 3; }
static void kv(char *t, const char *k, char *o, int cap) { int l = strlen(k); for (char *p = t; p && *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : 0) if (!strncmp(p, k, l) && p[l] == '=') { int i = 0; p += l + 1; while (p[i] && p[i] != '\n' && i < cap - 1) { o[i] = p[i]; i++; } o[i] = 0; return; } }
static int acct_load(void) { memset(&A, 0, sizeof A); char *t = z_readfile(CFG, 0); if (!t) return 0; char n[16]; kv(t, "proveedor", A.prov, 12); kv(t, "nombre", A.name, 64); kv(t, "usuario", A.user, 128); kv(t, "clave", A.pass, 128); kv(t, "imap", A.imap, 96); kv(t, "smtp", A.smtp, 96);
    kv(t, "puerto_imap", n, 16); A.iport = atoi(n) ? atoi(n) : 993; kv(t, "puerto_smtp", n, 16); A.sport = atoi(n) ? atoi(n) : 465; kv(t, "starttls", n, 16); A.starttls = atoi(n); free(t); return A.user[0] && A.pass[0] && A.imap[0]; }
static int acct_save(void) { sb_t b = {0}; sb_f(&b, "proveedor=%s\nnombre=%s\nusuario=%s\nclave=%s\nimap=%s\npuerto_imap=%d\nsmtp=%s\npuerto_smtp=%d\nstarttls=%d\n", A.prov, A.name, A.user, A.pass, A.imap, A.iport, A.smtp, A.sport, A.starttls); z_mkdir("/sys/ZUNZUN"); int e = z_writefile(CFG, b.p, b.n); free(b.p); z_sync(); return e; }
/* ---------- conexion (TCP + TLS) ---------- */
typedef struct { int s; ztls *t; u8 b[8192]; int bn, bp; char err[120]; } cn_t;
static u8 *roots; static long nroots;
static long io_snd(void *io, const void *d, long n) { return z_send(*(int *)io, d, n); }
static long io_rcv(void *io, void *d, long n, int to) { return z_recv(*(int *)io, d, n, to); }
static void *io_alloc(long n) { return malloc(n); }
static long now_num(void) { u32 t[6]; z_time(t); return (long)t[5] * 10000000000LL + (long)t[4] * 100000000LL + (long)t[3] * 1000000LL + t[2] * 10000 + t[1] * 100 + t[0]; }
static int cn_tls(cn_t *c, const char *host) {
    if (!roots) { roots = (u8 *)z_readfile("/rd/res/raices.bin", &nroots); if (roots) tls_set_roots(roots, nroots); }
    ztls *t = calloc(1, sizeof(ztls)); t->io = &c->s; t->snd = io_snd; t->rcv = io_rcv; t->alloc = io_alloc; t->free_ = free;
    if (tls_handshake(t, host, now_num())) { snprintf(c->err, 120, "TLS: %s", t->err); free(t); return -1; }
    if (t->cert_status) { snprintf(c->err, 120, "%s", _("El certificado del servidor no es de confianza")); free(t); return -1; }
    c->t = t; c->bn = c->bp = 0; return 0; }
static void cn_close(cn_t *c) { if (c->s > 0) z_close(c->s); free(c->t); c->t = 0; c->s = 0; }
static int cn_open(cn_t *c, const char *host, int port, int tls) { memset(c, 0, sizeof *c); u32 ip; if (z_dns(host, &ip)) { snprintf(c->err, 120, "%s %s", _("No se encuentra el servidor"), host); return -1; }
    c->s = z_socket(1); if (c->s < 0) { strcpy(c->err, z_errstr(c->s)); return -1; } int e = z_connect(c->s, ip, port, 12000); if (e) { snprintf(c->err, 120, "%s %s:%d: %s", _("No se pudo conectar con"), host, port, z_errstr(e)); z_close(c->s); c->s = 0; return -1; }
    if (tls && cn_tls(c, host)) { cn_close(c); return -1; } return 0; }
/* "servidor", "servidor:puerto" o, solo para pruebas, "sin-tls:servidor:puerto" (sin cifrar) */
static int cn_open_srv(cn_t *c, const char *srv, int defport, int tls) { char h[96]; int plain = !strncmp(srv, "sin-tls:", 8); strncpy0(h, plain ? srv + 8 : srv, 96); int port = defport; char *k = strchr(h, ':'); if (k) { *k = 0; port = atoi(k + 1); } return cn_open(c, h, port, tls && !plain); }
static int cn_write(cn_t *c, const void *d, long n) { if (c->t) return tls_write(c->t, d, n) == n ? 0 : -1; long o = 0; while (o < n) { long r = z_send(c->s, (const u8 *)d + o, n - o); if (r <= 0) return -1; o += r; } return 0; }
static int cn_puts(cn_t *c, const char *s) { return cn_write(c, s, strlen(s)); }
static int cn_fill(cn_t *c) { if (c->bp < c->bn) return 1; long r = c->t ? tls_read(c->t, c->b, sizeof c->b) : z_recv(c->s, c->b, sizeof c->b, 30000); if (r <= 0) { strcpy(c->err, _("El servidor cerró la conexión")); return 0; } c->bn = r; c->bp = 0; return 1; }
static int cn_line(cn_t *c, char *o, int cap) { int n = 0; for (;;) { if (!cn_fill(c)) return -1; u8 ch = c->b[c->bp++]; if (ch == '\n') break; if (ch != '\r' && n < cap - 1) o[n++] = ch; } o[n] = 0; return n; }
static int cn_read(cn_t *c, sb_t *o, long n) { while (n > 0) { if (!cn_fill(c)) return -1; long k = MIN(n, (long)(c->bn - c->bp)); sb_add(o, c->b + c->bp, k); c->bp += k; n -= k; } return 0; }
/* ---------- base64, quoted-printable y juegos de caracteres ---------- */
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static void b64e(sb_t *o, const u8 *d, long n) { for (long i = 0; i < n; i += 3) { u32 v = d[i] << 16 | (i + 1 < n ? d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0); char q[4] = {B64[v >> 18 & 63], B64[v >> 12 & 63], i + 1 < n ? B64[v >> 6 & 63] : '=', i + 2 < n ? B64[v & 63] : '='}; sb_add(o, q, 4); } }
static int b64v(int c) { const char *p = c ? strchr(B64, c) : 0; return p ? p - B64 : -1; }
static long b64d(const char *s, long n, u8 *o) { long k = 0; u32 v = 0; int bits = 0; for (long i = 0; i < n; i++) { int x = b64v(s[i]); if (x < 0) continue; v = v << 6 | x; bits += 6; if (bits >= 8) { bits -= 8; o[k++] = v >> bits; } } return k; }
static int hexv(int c) { return c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1; }
static long qpd(const char *s, long n, u8 *o, int hdr) { long k = 0; for (long i = 0; i < n; i++) { if (s[i] == '=' && i + 2 < n && hexv(s[i + 1]) >= 0 && hexv(s[i + 2]) >= 0) { o[k++] = hexv(s[i + 1]) * 16 + hexv(s[i + 2]); i += 2; } else if (s[i] == '=' && i + 1 < n && (s[i + 1] == '\r' || s[i + 1] == '\n')) { i++; if (s[i] == '\r' && i + 1 < n && s[i + 1] == '\n') i++; } else if (hdr && s[i] == '_') o[k++] = ' '; else o[k++] = s[i]; } return k; }
static const u16 W1252[32] = {0x20AC, 0x81, 0x201A, 0x192, 0x201E, 0x2026, 0x2020, 0x2021, 0x2C6, 0x2030, 0x160, 0x2039, 0x152, 0x8D, 0x17D, 0x8F, 0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x2DC, 0x2122, 0x161, 0x203A, 0x153, 0x9D, 0x17E, 0x178};
static void put_u(sb_t *o, u32 c) { char u[4]; int k = 0; if (c < 0x80) u[k++] = c; else if (c < 0x800) { u[k++] = 0xC0 | c >> 6; u[k++] = 0x80 | (c & 63); } else { u[k++] = 0xE0 | c >> 12; u[k++] = 0x80 | (c >> 6 & 63); u[k++] = 0x80 | (c & 63); } sb_add(o, u, k); }
/* pasa a UTF-8 desde utf-8, us-ascii, iso-8859-1/15 o windows-1252 */
static void to_utf8(sb_t *o, const u8 *d, long n, const char *cs) { int latin = cs && (strstr(cs, "8859") || strstr(cs, "1252") || strstr(cs, "latin")); if (!latin) { sb_add(o, d, n); return; } for (long i = 0; i < n; i++) put_u(o, d[i] >= 0x80 && d[i] < 0xA0 ? W1252[d[i] - 0x80] : d[i] == 0xA4 && strstr(cs, "15") ? 0x20AC : d[i]); }
static void lower(char *s) { for (; *s; s++) *s = tolower(*s); }
/* cabecera con =?utf-8?B?...?= o =?iso-8859-1?Q?...?= */
static void hdr_dec(const char *s, char *out, int cap) { sb_t o = {0}; sb_s(&o, ""); while (*s) { const char *a = strstr(s, "=?"); if (!a) { sb_s(&o, s); break; } const char *q1 = strchr(a + 2, '?'), *q2 = q1 ? strchr(q1 + 1, '?') : 0, *e = q2 ? strstr(q2 + 1, "?=") : 0; if (!e) { sb_s(&o, s); break; }
        { const char *g = s; int ws = 1; for (; g < a; g++) if (*g != ' ' && *g != '\t' && *g != '\r' && *g != '\n') ws = 0; if (!ws || o.n == 0 || o.p[o.n - 1] != 1) sb_add(&o, s, a - s); else o.n--; }
        char cs[40]; int cl = MIN((int)(q1 - a - 2), 39); memcpy(cs, a + 2, cl); cs[cl] = 0; lower(cs); u8 *t = malloc(e - q2 + 4); long n = (q1[1] | 32) == 'b' ? b64d(q2 + 1, e - q2 - 1, t) : qpd(q2 + 1, e - q2 - 1, t, 1); to_utf8(&o, t, n, cs); free(t); sb_add(&o, "\1", 1); s = e + 2; }
    long k = 0; for (long i = 0; i < o.n && k < cap - 1; i++) if (o.p[i] != 1) out[k++] = o.p[i] == '\t' || o.p[i] == '\r' || o.p[i] == '\n' ? ' ' : o.p[i]; out[k] = 0; free(o.p); }
/* valor de una cabecera (con lineas plegadas) dentro de un bloque de cabeceras */
static int hget(const char *h, long hl, const char *name, char *o, int cap) { int l = strlen(name); o[0] = 0; for (long i = 0; i < hl; ) { if ((i == 0 || h[i - 1] == '\n') && i + l < hl && !strncasecmp(h + i, name, l) && h[i + l] == ':') { long j = i + l + 1; int k = 0; while (j < hl && (h[j] == ' ' || h[j] == '\t')) j++; while (j < hl && k < cap - 1) { if (h[j] == '\r') { j++; continue; } if (h[j] == '\n') { if (j + 1 < hl && (h[j + 1] == ' ' || h[j + 1] == '\t')) { j++; continue; } break; } o[k++] = h[j++]; } o[k] = 0; return 1; } i++; } return 0; }
static void param(const char *v, const char *p, char *o, int cap) { o[0] = 0; char k[40]; snprintf(k, 40, "%s=", p); const char *s = v; while ((s = strcasestr(s, k))) { if (s == v || s[-1] == ';' || s[-1] == ' ' || s[-1] == '\t') break; s++; } if (!s) return; s += strlen(k); int q = *s == '"'; if (q) s++; int n = 0; while (*s && n < cap - 1 && (q ? *s != '"' : *s != ';' && *s != ' ' && *s != '\r')) o[n++] = *s++; o[n] = 0; }
/* HTML a texto: quita etiquetas, estilos y guiones; saltos de linea en bloques; entidades basicas */
static void html_text(sb_t *o, const char *s, long n) { int sp = 0; for (long i = 0; i < n; i++) { char c = s[i];
        if (c == '<') { long j = i + 1; char tag[12]; int k = 0, close = s[j] == '/'; if (close) j++; while (j < n && k < 11 && isalnum(s[j])) tag[k++] = tolower(s[j++]); tag[k] = 0;
            if (!close && (!strcmp(tag, "style") || !strcmp(tag, "script") || !strcmp(tag, "head") || !strcmp(tag, "title"))) { char end[20]; snprintf(end, 20, "</%s", tag); const char *e = strcasestr(s + j, end); if (!e || e >= s + n) break; i = e - s; }
            while (i < n && s[i] != '>') i++;
            if (!strcmp(tag, "br") || !strcmp(tag, "p") || !strcmp(tag, "div") || !strcmp(tag, "tr") || !strcmp(tag, "h1") || !strcmp(tag, "h2") || !strcmp(tag, "h3") || !strcmp(tag, "table") || !strcmp(tag, "blockquote")) { if (o->n && o->p[o->n - 1] != '\n') sb_s(o, "\n"); if (!close && (!strcmp(tag, "p") || tag[0] == 'h')) sb_s(o, "\n"); sp = 0; }
            else if (!strcmp(tag, "li") && !close) { sb_s(o, "\n  • "); sp = 0; } else if (!strcmp(tag, "td") && close) { sb_s(o, "  "); }
            continue; }
        if (c == '&') { static const struct { const char *e; u32 u; } EN[] = {{"amp;", '&'}, {"lt;", '<'}, {"gt;", '>'}, {"quot;", '"'}, {"apos;", '\''}, {"#39;", '\''}, {"nbsp;", ' '}, {"euro;", 0x20AC}, {"copy;", 0xA9}, {"aacute;", 0xE1}, {"eacute;", 0xE9}, {"iacute;", 0xED}, {"oacute;", 0xF3}, {"uacute;", 0xFA}, {"ntilde;", 0xF1}, {"Ntilde;", 0xD1}, {"uuml;", 0xFC}, {"iexcl;", 0xA1}, {"iquest;", 0xBF}, {"mdash;", 0x2014}, {"ndash;", 0x2013}, {"hellip;", 0x2026}, {"laquo;", 0xAB}, {"raquo;", 0xBB}};
            int done = 0; for (unsigned k = 0; k < sizeof EN / sizeof EN[0]; k++) { int l = strlen(EN[k].e); if (i + l < n && !strncmp(s + i + 1, EN[k].e, l)) { put_u(o, EN[k].u); i += l; done = 1; break; } }
            if (!done && i + 2 < n && s[i + 1] == '#') { long j = i + 2; u32 v = 0; int hx = s[j] == 'x' || s[j] == 'X'; if (hx) j++; while (j < n && (hx ? hexv(s[j]) >= 0 : isdigit(s[j]))) v = v * (hx ? 16 : 10) + (hx ? hexv(s[j]) : s[j] - '0'), j++; if (j < n && s[j] == ';' && v) { put_u(o, v < 0x10000 ? v : '?'); i = j; done = 1; } }
            if (!done) sb_s(o, "&"); sp = 0; continue; }
        if (c == '\r' || c == '\n' || c == '\t' || c == ' ') { if (!sp && o->n && o->p[o->n - 1] != '\n') sb_s(o, " "); sp = 1; continue; } sp = 0; sb_add(o, &c, 1); } }
/* ---------- MIME: busca el texto (o el HTML) y la lista de adjuntos ---------- */
typedef struct { char name[160], ctype[80]; u8 *d; long n; } att_t;   /* adjunto ya decodificado */
typedef struct { sb_t text, html, att; att_t a[16]; int na; } mime_t;
static const char *ext_for(const char *ct) { return strstr(ct, "png") ? ".png" : strstr(ct, "jpeg") || strstr(ct, "jpg") ? ".jpg" : strstr(ct, "gif") ? ".gif" : strstr(ct, "pdf") ? ".pdf" : strstr(ct, "mpeg") ? ".mp3" : strstr(ct, "wav") ? ".wav" : strstr(ct, "plain") ? ".txt" : strstr(ct, "html") ? ".html" : strstr(ct, "wordprocessingml") ? ".docx" : strstr(ct, "spreadsheetml") ? ".xlsx" : strstr(ct, "zip") ? ".zip" : ".bin"; }
static void mime_part(mime_t *m, const char *p, long n, int depth) {
    long hl = 0; while (hl + 1 < n && !(p[hl] == '\n' && (p[hl + 1] == '\n' || (p[hl + 1] == '\r' && hl + 2 < n && p[hl + 2] == '\n')))) hl++; const char *body; if (hl + 1 >= n) { hl = n; body = p + n; } else body = p + MIN(n, hl + (p[hl + 1] == '\r' ? 3 : 2)); long bl = p + n - body; if (bl < 0) bl = 0;
    char ct[300] = "text/plain", te[40] = "", cd[300] = "", v[200]; hget(p, hl, "Content-Type", ct, 300); hget(p, hl, "Content-Transfer-Encoding", te, 40); hget(p, hl, "Content-Disposition", cd, 300); char lc[300]; strcpy(lc, ct); lower(lc); lower(te);
    if (!strncmp(lc, "multipart/", 10) && depth < 6) { char bd[100]; param(ct, "boundary", bd, 100); if (!bd[0]) return; char mk[110]; snprintf(mk, 110, "--%s", bd); int ml = strlen(mk); const char *s = body, *e = body + bl;
        const char *a = 0; while (s < e) { const char *f = 0; for (const char *q = s; q + ml <= e; q++) if ((q == body || q[-1] == '\n') && !memcmp(q, mk, ml)) { f = q; break; } if (!f) break; if (a) { const char *z = f; if (z > a && z[-1] == '\n') z--; if (z > a && z[-1] == '\r') z--; mime_part(m, a, z - a, depth + 1); } if (f + ml + 1 < e && f[ml] == '-' && f[ml + 1] == '-') break; a = strchr(f, '\n'); if (!a) break; a++; s = a; } return; }
    char fn[160] = ""; param(cd, "filename", fn, 160); if (!fn[0]) param(ct, "name", fn, 160); char lcd[300]; strcpy(lcd, cd); lower(lcd);
    if (fn[0] || !strncmp(lcd, "attachment", 10) || (strncmp(lc, "text/", 5) && strncmp(lc, "message/", 8))) { char d[200];
        if (fn[0]) hdr_dec(fn, d, 200); else snprintf(d, 200, "%s%d%s", _("adjunto"), m->na + 1, ext_for(lc));
        for (char *q = d; *q; q++) if (*q == '/' || *q == '\\' || *q == ':') *q = '_';   /* sin rutas en el nombre */
        if (m->att.n) sb_s(&m->att, ", "); sb_s(&m->att, d);
        if (m->na < 16) { att_t *a = &m->a[m->na++]; strncpy0(a->name, d, 160); strncpy0(a->ctype, lc, 80); a->d = malloc(bl + 1); a->n = strstr(te, "base64") ? b64d(body, bl, a->d) : strstr(te, "quoted") ? qpd(body, bl, a->d, 0) : (memcpy(a->d, body, bl), bl); }
        return; }
    if (!strncmp(lc, "message/rfc822", 14)) { mime_part(m, body, bl, depth + 1); return; }
    u8 *raw = malloc(bl + 1); long rn = strstr(te, "base64") ? b64d(body, bl, raw) : strstr(te, "quoted") ? qpd(body, bl, raw, 0) : (memcpy(raw, body, bl), bl); param(ct, "charset", v, 200); lower(v);
    sb_t u = {0}; sb_s(&u, ""); to_utf8(&u, raw, rn, v); free(raw); if (strstr(lc, "text/html")) { if (!m->html.n) sb_add(&m->html, u.p, u.n); } else if (!m->text.n) sb_add(&m->text, u.p, u.n); free(u.p); }
/* ---------- IMAP ---------- */
typedef struct { u32 uid; int seen; char from[120], subj[200], date[40]; } hdr_t;
static hdr_t H[NMAX]; static int nh, sel = -1, topl; static int tagn;
static cn_t IC; static int ic_ok;
/* envia una orden y lee hasta la respuesta con su etiqueta; los datos (con literales {n}) van a out */
static int imap(sb_t *out, const char *fmt, ...) { char cmd[600]; va_list a; va_start(a, fmt); vsnprintf(cmd, 560, fmt, a); va_end(a); char t[16]; snprintf(t, 16, "z%d", ++tagn); char line[700]; snprintf(line, 700, "%s %s\r\n", t, cmd); if (cn_puts(&IC, line)) return -1;
    char l[2048]; for (;;) { int n = cn_line(&IC, l, sizeof l); if (n < 0) return -1; if (out) { sb_add(out, l, n); sb_s(out, "\n"); }
        while (n > 2 && l[n - 1] == '}') { char *b = strrchr(l, '{'); if (!b) break; long k = atol(b + 1); sb_t tmp = {0}; if (cn_read(&IC, out ? out : &tmp, k)) { free(tmp.p); return -1; } free(tmp.p); if (out) sb_s(out, "\n"); n = cn_line(&IC, l, sizeof l); if (n < 0) return -1; if (out) { sb_add(out, l, n); sb_s(out, "\n"); } }
        if (!strncmp(l, t, strlen(t)) && l[strlen(t)] == ' ') { const char *r = l + strlen(t) + 1; if (!strncmp(r, "OK", 2)) return 0; snprintf(IC.err, 120, "%s", r); return -2; } } }
static void imap_q(sb_t *b, const char *s) { sb_s(b, "\""); for (; *s; s++) { if (*s == '"' || *s == '\\') sb_s(b, "\\"); sb_add(b, s, 1); } sb_s(b, "\""); }
static int imap_login(void) {
    if (ic_ok) { if (!imap(0, "NOOP")) return 0; cn_close(&IC); ic_ok = 0; }
    snprintf(msg, 200, "%s %s...", _("Conectando con"), A.imap); if (W) draw();
    if (cn_open_srv(&IC, A.imap, A.iport, 1)) { snprintf(msg, 200, "%s", IC.err); return -1; } char l[600]; if (cn_line(&IC, l, sizeof l) < 0 || strncmp(l, "* OK", 4)) { snprintf(msg, 200, "IMAP: %s", l); cn_close(&IC); return -1; }
    /* iCloud: el usuario de IMAP suele ser lo que va antes de la @ */
    for (int tryi = 0; tryi < 2; tryi++) { char u[128]; strcpy(u, A.user); if (!strcmp(A.prov, "icloud") && tryi == 0 && strchr(u, '@')) *strchr(u, '@') = 0; else if (tryi == 1 && strcmp(A.prov, "icloud")) break;
        sb_t c = {0}; sb_s(&c, "LOGIN "); imap_q(&c, u); sb_s(&c, " "); imap_q(&c, A.pass); int r = imap(0, "%s", c.p); free(c.p); if (!r) { ic_ok = 1; return 0; } }
    snprintf(msg, 200, "%s (%s)", _("El servidor no aceptó el usuario o la contraseña de aplicación"), IC.err); cn_close(&IC); return -1; }
static void parse_fetch(const char *d, long n) { nh = 0; const char *p = d, *e = d + n;
    while (p < e && nh < NMAX) { const char *f = strstr(p, " FETCH ("); if (!f || f >= e) break; const char *ls = f; while (ls > d && ls[-1] != '\n') ls--; const char *nx = strstr(f + 8, "\n* "); if (!nx) nx = e;
        hdr_t *h = &H[nh]; memset(h, 0, sizeof *h); const char *u = strstr(f, "UID "); if (u && u < nx) h->uid = atol(u + 4); const char *fl = strstr(f, "FLAGS ("); if (fl && fl < nx) { const char *fe = strchr(fl, ')'); h->seen = fe && strstr(fl, "\\Seen") && strstr(fl, "\\Seen") < fe; }
        const char *hb = strstr(f, "}\n"); if (hb && hb < nx) { hb += 2; long hl = nx - hb; char v[400]; if (hget(hb, hl, "From", v, 400)) hdr_dec(v, h->from, 120); if (hget(hb, hl, "Subject", v, 400)) hdr_dec(v, h->subj, 200); if (hget(hb, hl, "Date", v, 400)) { char *c = strchr(v, ','); strncpy0(h->date, c ? c + 2 : v, 22); } }
        if (h->uid) nh++; p = nx; }
    for (int i = 0; i < nh / 2; i++) { hdr_t t = H[i]; H[i] = H[nh - 1 - i]; H[nh - 1 - i] = t; } /* el mas nuevo arriba */ }
static void refresh(void) { if (imap_login()) return; sb_t o = {0}; if (imap(&o, "SELECT INBOX")) { snprintf(msg, 200, "IMAP: %s", IC.err); free(o.p); return; } long ex = 0; for (char *p = o.p; p && (p = strstr(p, " EXISTS")); p++) { char *b = p; while (b > o.p && b[-1] != ' ') b--; ex = atol(b); break; } free(o.p); o = (sb_t){0};
    nh = 0; sel = -1; topl = 0; if (ex > 0) { long a = MAX(1, ex - NMAX + 1); if (imap(&o, "FETCH %ld:%ld (UID FLAGS BODY.PEEK[HEADER.FIELDS (FROM SUBJECT DATE)])", a, ex)) { snprintf(msg, 200, "IMAP: %s", IC.err); free(o.p); return; } parse_fetch(o.p, o.n); free(o.p); }
    int unread = 0; for (int i = 0; i < nh; i++) unread += !H[i].seen; snprintf(msg, 200, _("%ld mensajes en la bandeja de entrada, %d sin leer"), ex, unread); char m[100]; snprintf(m, 100, "[prueba] correo bandeja %ld mensajes", ex); z_log(m); }
/* ---------- lectura ---------- */
static att_t RA[16]; static int nra, att_y[16];   /* adjuntos del mensaje abierto y donde se dibujan */
static sb_t body; static char rd_from[200], rd_to[300], rd_subj[300], rd_date[80], rd_att[300], rd_reply[200], rd_msgid[200]; static int rtop; static char **wl; static int nwl;
static void wrap(int cols) { for (int i = 0; i < nwl; i++) free(wl[i]); free(wl); wl = 0; nwl = 0; int cap = 64; wl = malloc(cap * sizeof *wl); const char *p = body.p ? body.p : "";
    while (*p) { const char *e = strchr(p, '\n'); long n = e ? e - p : (long)strlen(p); const char *q = p; do { long k = 0, last = -1, ch = 0; while (k < n - (q - p) && ch < cols) { if (q[k] == ' ') last = k; u8 c = q[k]; k += c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4; ch++; } if (k < n - (q - p) && last > 0) k = last + 1; if (nwl == cap) { cap *= 2; wl = realloc(wl, cap * sizeof *wl); } char *l = malloc(k + 1); memcpy(l, q, k); l[k] = 0; if (k && l[k - 1] == '\r') l[k - 1] = 0; wl[nwl++] = l; q += k; } while (q < p + n); if (!n) { if (nwl == cap) { cap *= 2; wl = realloc(wl, cap * sizeof *wl); } wl[nwl++] = strdup(""); } p = e ? e + 1 : p + n; } }
static void open_msg(int i) { if (i < 0 || i >= nh) return; sel = i; if (imap_login()) return; if (imap(0, "SELECT INBOX")) return; sb_t o = {0}; snprintf(msg, 200, "%s", _("Descargando el mensaje...")); draw();
    if (imap(&o, "UID FETCH %u (BODY.PEEK[])", H[i].uid)) { snprintf(msg, 200, "IMAP: %s", IC.err); free(o.p); return; } char *lit = strstr(o.p, "}\n"); if (!lit) { free(o.p); return; } char *br = lit; while (br > o.p && *br != '{') br--; lit += 2; long ml = MIN(atol(br + 1), o.n - (lit - o.p)); /* solo el literal {n}, sin el cierre de IMAP */
    long hl = 0; while (hl + 1 < ml && !(lit[hl] == '\n' && (lit[hl + 1] == '\n' || (lit[hl + 1] == '\r' && lit[hl + 2] == '\n')))) hl++; char v[600];
    hget(lit, hl, "From", v, 600); hdr_dec(v, rd_from, 200); hget(lit, hl, "To", v, 600); hdr_dec(v, rd_to, 300); hget(lit, hl, "Subject", v, 600); hdr_dec(v, rd_subj, 300); hget(lit, hl, "Date", v, 600); strncpy0(rd_date, v, 80); if (!hget(lit, hl, "Reply-To", v, 600)) hget(lit, hl, "From", v, 600); strncpy0(rd_reply, v, 200); hget(lit, hl, "Message-ID", v, 600); strncpy0(rd_msgid, v, 200);
    mime_t m = {0}; mime_part(&m, lit, ml, 0); free(body.p); body = (sb_t){0}; sb_s(&body, ""); if (m.text.n) sb_add(&body, m.text.p, m.text.n); else if (m.html.n) { sb_t h = {0}; sb_s(&h, ""); html_text(&h, m.html.p, m.html.n); long k = 0; int nl = 2; for (long j = 0; j < h.n; j++) { char ch = h.p[j]; if (ch == '\n') { if (nl >= 2) continue; nl++; } else if (ch == ' ' && nl) continue; else nl = 0; h.p[k++] = ch; } sb_add(&body, h.p, k); free(h.p); } /* sin lineas en blanco de mas */ rd_att[0] = 0; if (m.att.n) strncpy0(rd_att, m.att.p, 300); free(m.text.p); free(m.html.p); free(m.att.p); free(o.p);
    for (int k = 0; k < nra; k++) free(RA[k].d); nra = m.na; for (int k = 0; k < m.na; k++) RA[k] = m.a[k];
    if (!H[i].seen) { imap(0, "UID STORE %u +FLAGS (\\Seen)", H[i].uid); H[i].seen = 1; } rtop = 0; wrap((W->w - 32) / 8); msg[0] = 0; char lg[300]; snprintf(lg, 300, "[prueba] correo leido: %s", rd_subj); z_log(lg); }
/* ---------- adjuntos: guardar en Documentos/Correo o abrir con su programa ---------- */
static int att_save(int k, int open) {
    char dir[200], p[400]; if (open) strcpy(dir, "/rd/tmp/correo"); else { char d[160]; z_docdir(d); snprintf(dir, 200, "%s/Correo", d); }
    z_mkdir(dir); snprintf(p, 400, "%s/%s", dir, RA[k].name);
    zdirent st; if (!open) for (int n = 2; !z_stat(p, &st) && n < 100; n++) { const char *dot = strrchr(RA[k].name, '.'); int bl = dot ? (int)(dot - RA[k].name) : (int)strlen(RA[k].name); snprintf(p, 400, "%s/%.*s (%d)%s", dir, bl, RA[k].name, n, dot ? dot : ""); }
    int e = z_writefile(p, RA[k].d, RA[k].n); z_sync();
    char lg[500]; snprintf(lg, 500, "[prueba] correo adjunto %s %s %ld bytes: %d", open ? "abierto" : "guardado", p, RA[k].n, e); z_log(lg);
    if (e) { snprintf(msg, 200, "%s: %s", _("No se pudo guardar el adjunto"), z_errstr(e)); return -1; }
    if (open) { if (z_open_path(p) < 0) snprintf(msg, 200, "%s", _("No hay ningún programa para abrir ese archivo")); else msg[0] = 0; }
    else snprintf(msg, 200, "%s %s", _("Guardado en"), p);
    return 0; }

/* ---------- envio (SMTP) ---------- */
static int smtp_resp(cn_t *c, char *l, int cap) { int code; do { if (cn_line(c, l, cap) < 0) return -1; code = atoi(l); } while (strlen(l) > 3 && l[3] == '-'); return code; }
static int smtp_cmd(cn_t *c, const char *s, int want, char *l) { if (s && cn_puts(c, s)) return -1; int r = smtp_resp(c, l, 600); if (r / 100 != want / 100) { snprintf(c->err, 120, "SMTP: %s", l); return -1; } return 0; }
static void addr_of(const char *s, char *o, int cap) { const char *a = strchr(s, '<'); if (a) { const char *b = strchr(a, '>'); int n = MIN(b ? (int)(b - a - 1) : (int)strlen(a + 1), cap - 1); memcpy(o, a + 1, n); o[n] = 0; } else { while (*s == ' ') s++; strncpy0(o, s, cap); char *e = o + strlen(o); while (e > o && e[-1] == ' ') *--e = 0; } }
static void enc_hdr(sb_t *b, const char *s) { int ascii = 1; for (const char *p = s; *p; p++) if ((u8)*p >= 0x80) ascii = 0; if (ascii) { sb_s(b, s); return; } sb_s(b, "=?UTF-8?B?"); b64e(b, (const u8 *)s, strlen(s)); sb_s(b, "?="); }
static char c_att[8][260]; static int nca;   /* archivos adjuntos del mensaje que se escribe */
static const char *mime_of(const char *n) { const char *d = strrchr(n, '.'); if (!d) return "application/octet-stream"; static const char *T[][2] = { {".pdf", "application/pdf"}, {".png", "image/png"}, {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"}, {".gif", "image/gif"}, {".txt", "text/plain; charset=UTF-8"}, {".html", "text/html; charset=UTF-8"}, {".htm", "text/html; charset=UTF-8"}, {".mp3", "audio/mpeg"}, {".wav", "audio/wav"}, {".zip", "application/zip"}, {".csv", "text/csv"}, {".docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"}, {".xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"}, {".pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"}, {".odt", "application/vnd.oasis.opendocument.text"}, {".doc", "application/msword"}, {".avi", "video/x-msvideo"} };
    for (u32 i = 0; i < sizeof T / sizeof *T; i++) if (!strcasecmp(d, T[i][0])) return T[i][1]; return "application/octet-stream"; }
static void b64_lines(sb_t *m, const u8 *d, long n) { sb_t b6 = {0}; b64e(&b6, d, n); for (long i = 0; i < b6.n; i += 76) { sb_add(m, b6.p + i, MIN(76, b6.n - i)); sb_s(m, "\r\n"); } free(b6.p); }
static void fname_param(sb_t *m, const char *key, const char *nm) {   /* nombre de archivo: tal cual si es ASCII; si no, RFC 2231 y palabra codificada */
    int ascii = 1; for (const char *p = nm; *p; p++) if ((u8)*p >= 0x80 || *p == '"') ascii = 0;
    if (ascii) { sb_f(m, "; %s=\"%s\"", key, nm); return; }
    sb_f(m, "; %s*=UTF-8''", key); for (const u8 *p = (const u8 *)nm; *p; p++) { if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || strchr(".-_", *p)) sb_add(m, (const char *)p, 1); else sb_f(m, "%%%02X", *p); } }
static int smtp_send(const char *to, const char *subj, const char *text, const char *inrep) {
    cn_t c; char l[600]; snprintf(msg, 200, "%s %s...", _("Enviando por"), A.smtp); draw();
    if (cn_open_srv(&c, A.smtp, A.sport, !A.starttls)) { snprintf(msg, 200, "%s", c.err); return -1; }
    int e = smtp_cmd(&c, 0, 220, l); if (!e) e = smtp_cmd(&c, "EHLO zunzunos\r\n", 250, l);
    if (!e && A.starttls && strncmp(A.smtp, "sin-tls:", 8)) { e = smtp_cmd(&c, "STARTTLS\r\n", 220, l); char hh[96]; strncpy0(hh, A.smtp, 96); if (strchr(hh, ':')) *strchr(hh, ':') = 0; if (!e) e = cn_tls(&c, hh); if (!e) e = smtp_cmd(&c, "EHLO zunzunos\r\n", 250, l); }
    if (!e) { sb_t a = {0}, x = {0}; sb_add(&x, "\0", 1); sb_s(&x, A.user); sb_add(&x, "\0", 1); sb_s(&x, A.pass); sb_s(&a, "AUTH PLAIN "); b64e(&a, (u8 *)x.p, x.n); sb_s(&a, "\r\n"); e = smtp_cmd(&c, a.p, 235, l); free(a.p); free(x.p); if (e) snprintf(c.err, 120, "%s", _("El servidor no aceptó el usuario o la contraseña de aplicación")); }
    if (!e) { snprintf(l, 600, "MAIL FROM:<%s>\r\n", A.user); e = smtp_cmd(&c, l, 250, l); }
    char list[600]; strncpy0(list, to, 600); int nr = 0; for (char *p = list, *nx; p && *p && !e; p = nx) { nx = sep_(p); if (nx) *nx++ = 0; char ad[200]; addr_of(p, ad, 200); if (!ad[0]) continue; char cmd[260]; snprintf(cmd, 260, "RCPT TO:<%s>\r\n", ad); e = smtp_cmd(&c, cmd, 250, l); nr++; }
    if (!e && !nr) { e = -1; strcpy(c.err, _("Falta el destinatario")); }
    if (!e) e = smtp_cmd(&c, "DATA\r\n", 354, l);
    if (!e) { sb_t m = {0}; u32 t[6]; z_time(t); sb_s(&m, "From: "); if (A.name[0]) { enc_hdr(&m, A.name); sb_s(&m, " "); } sb_f(&m, "<%s>\r\nTo: %s\r\nSubject: ", A.user, to); enc_hdr(&m, subj); sb_f(&m, "\r\nMessage-ID: <%lu.%u@zunzunos>\r\n", (unsigned long)z_ms(), t[0] * 7919 + t[1]);
        if (inrep && inrep[0]) sb_f(&m, "In-Reply-To: %s\r\nReferences: %s\r\n", inrep, inrep); sb_s(&m, "MIME-Version: 1.0\r\nX-Mailer: ZunzunOS Correo\r\n");
        char bnd[64]; snprintf(bnd, 64, "=_zunzun_%lu_%u", (unsigned long)z_ms(), t[0] * 31 + t[1]);
        if (nca) sb_f(&m, "Content-Type: multipart/mixed; boundary=\"%s\"\r\n\r\nMensaje en varias partes (MIME).\r\n--%s\r\n", bnd, bnd);
        sb_s(&m, "Content-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: base64\r\n\r\n");
        sb_t nb = {0}; for (const char *p = text; *p; p++) { if (*p == '\n' && (p == text || p[-1] != '\r')) sb_s(&nb, "\r"); sb_add(&nb, p, 1); } b64_lines(&m, (u8 *)(nb.p ? nb.p : ""), nb.n); free(nb.p);
        for (int k = 0; k < nca && !e; k++) { long fl; u8 *fd = z_readfile(c_att[k], &fl); if (!fd) { e = -1; snprintf(c.err, 120, "%s %s", _("No se puede abrir"), c_att[k]); break; }
            const char *nm = strrchr(c_att[k], '/'); nm = nm ? nm + 1 : c_att[k];
            sb_f(&m, "--%s\r\nContent-Type: %s", bnd, mime_of(nm)); fname_param(&m, "name", nm); sb_s(&m, "\r\nContent-Disposition: attachment"); fname_param(&m, "filename", nm); sb_s(&m, "\r\nContent-Transfer-Encoding: base64\r\n\r\n");
            b64_lines(&m, fd, fl); free(fd); }
        if (nca) sb_f(&m, "--%s--\r\n", bnd);
        if (!e) { sb_s(&m, ".\r\n"); e = cn_write(&c, m.p, m.n); } free(m.p); if (!e) e = smtp_cmd(&c, 0, 250, l); }
    if (!e) cn_puts(&c, "QUIT\r\n"); if (e) snprintf(msg, 200, "%s: %s", _("No se pudo enviar"), c.err[0] ? c.err : l); else snprintf(msg, 200, "%s", _("Mensaje enviado")); cn_close(&c);
    char lg[300]; snprintf(lg, 300, "[prueba] correo enviado a %s: %d", to, e); z_log(lg); return e; }
/* ---------- borradores y enviados: en la memoria, un archivo de texto por mensaje ----------
   para=, asunto=, fecha=, inrep= y una linea adjunto= por archivo; despues una linea en blanco y el texto.
   A Enviados solo va lo que el servidor SMTP acepto (respuesta 250 al terminar el mensaje). */
#define CDIR "/sys/ZUNZUN/CORREO"
static const char *FDIR[] = {"", CDIR "/BORRADORES", CDIR "/ENVIADOS"};
typedef struct { char file[40], to[200], subj[200], date[24]; } loc_t;
static loc_t LF[NMAX]; static int nlf, folder;   /* carpeta: 0 Entrada (IMAP), 1 Borradores, 2 Enviados */
static void lkv(const char *t, const char *k, char *o, int cap) { int l = strlen(k); o[0] = 0; for (const char *p = t; p && *p && *p != '\n'; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : 0) if (!strncmp(p, k, l) && p[l] == '=') { int i = 0; p += l + 1; while (p[i] && p[i] != '\n' && i < cap - 1) { o[i] = p[i]; i++; } o[i] = 0; return; } }
static int loc_save(int f, char *file, const char *to, const char *subj, const char *inrep, char att[][260], int na, const char *text) {
    z_mkdir("/sys/ZUNZUN"); z_mkdir(CDIR); z_mkdir(FDIR[f]); u32 t[6]; z_time(t); char p[200];
    if (!file[0]) for (int n = 0; n < 100; n++) { zdirent st; if (n) snprintf(file, 40, "%04u%02u%02u-%02u%02u%02u-%d.txt", t[5], t[4], t[3], t[2], t[1], t[0], n); else snprintf(file, 40, "%04u%02u%02u-%02u%02u%02u.txt", t[5], t[4], t[3], t[2], t[1], t[0]); snprintf(p, 200, "%s/%s", FDIR[f], file); if (z_stat(p, &st)) break; }
    snprintf(p, 200, "%s/%s", FDIR[f], file); sb_t b = {0}; sb_f(&b, "para=%s\nasunto=%s\nfecha=%04u-%02u-%02u %02u:%02u\ninrep=%s\n", to, subj, t[5], t[4], t[3], t[2], t[1], inrep ? inrep : "");
    for (int k = 0; k < na; k++) sb_f(&b, "adjunto=%s\n", att[k]); sb_s(&b, "\n"); sb_s(&b, text); int e = z_writefile(p, b.p, b.n); free(b.p); z_sync(); return e; }
static void loc_list(int f) { nlf = 0; zdirent d; static char names[NMAX][40]; int nn = 0;
    for (int i = 0; !z_readdir(FDIR[f], i, &d); i++) { if (d.is_dir || d.name[0] == '.' || !z_ends(d.name, ".txt")) continue; if (nn < NMAX) strncpy0(names[nn++], d.name, 40); else { int m = 0; for (int k = 1; k < nn; k++) if (strcmp(names[k], names[m]) < 0) m = k; if (strcmp(d.name, names[m]) > 0) strncpy0(names[m], d.name, 40); } }
    for (int i = 1; i < nn; i++) for (int j = i; j > 0 && strcmp(names[j - 1], names[j]) < 0; j--) { char x[40]; strcpy(x, names[j]); strcpy(names[j], names[j - 1]); strcpy(names[j - 1], x); }   /* el mas nuevo arriba */
    for (int i = 0; i < nn; i++) { char p[200]; snprintf(p, 200, "%s/%s", FDIR[f], names[i]); char *t = z_readfile(p, 0); if (!t) continue; loc_t *l = &LF[nlf++]; strcpy(l->file, names[i]); lkv(t, "para", l->to, 200); lkv(t, "asunto", l->subj, 200); lkv(t, "fecha", l->date, 24); free(t); }
    char lg[80]; snprintf(lg, 80, "[prueba] correo carpeta %s %d mensajes", f == 1 ? "borradores" : "enviados", nlf); z_log(lg); }
/* ---------- ventana Escribir ---------- */
static te_t CT; static char c_to[600], c_subj[300], c_inrep[200], c_draft[40]; static int c_foc, c_cur, c_dirty;
static int c_top(void) { return nca ? 114 + 24 * ((nca + 2) / 3) : 114; }   /* donde empieza el texto */
static void draw_compose(void) { zwin *w = CW; g_clear(w, UI_WHITE); g_rect(w, 0, 0, w->w, 40, UI_PANEL); ui_button(w, 8, 7, 90, 26, "Enviar", 2); ui_button(w, 104, 7, 90, 26, "Cancelar", 0); ui_button(w, 200, 7, 110, 26, "Adjuntar...", 0); ui_button(w, 316, 7, g_textw(_("Guardar borrador")) + 14, 26, "Guardar borrador", 0);
    for (int k = 0; k < nca; k++) { const char *nm = strrchr(c_att[k], '/'); nm = nm ? nm + 1 : c_att[k]; int cw = (w->w - 24) / 3, x = 12 + (k % 3) * cw, y = 112 + (k / 3) * 24; char b[80]; snprintf(b, 80, "%.26s", nm); g_round(w, x, y, cw - 8, 20, 3, 0xFEF3C7); g_text(w, x + 6, y + 2, b, 0x92400E); g_text(w, x + cw - 26, y + 2, "x", 0xB91C1C); }
    g_text(w, 12, 54, _("Para"), UI_TEXT); ui_input(w, 90, 48, w->w - 102, 26, c_to, c_foc == 0 ? c_cur : 0, c_foc == 0); g_text(w, 12, 86, _("Asunto"), UI_TEXT); ui_input(w, 90, 80, w->w - 102, 26, c_subj, c_foc == 1 ? c_cur : 0, c_foc == 1);
    int T0 = c_top(); g_rect(w, 8, T0, w->w - 16, w->h - T0 - 8, 0xFFFFFF); g_frame(w, 8, T0, w->w - 16, w->h - T0 - 8, c_foc == 2 ? UI_ACC : UI_LINE); CT.cols = (w->w - 32) / 8; te_draw(w, &CT, 14, T0 + 4, w->h - T0 - 14, 1, UI_TEXT, UI_ACC2, UI_ACC, 0); zw_present(w); }
static void compose(const char *to, const char *subj, const char *text, const char *inrep) { if (CW) zw_close(CW); nca = 0; c_draft[0] = 0; c_dirty = 0; strncpy0(c_to, to, 600); strncpy0(c_subj, subj, 300); strncpy0(c_inrep, inrep ? inrep : "", 200); te_init(&CT); te_set(&CT, text, strlen(text)); CT.cur = 0; c_foc = to[0] ? 2 : 0; c_cur = strlen(c_to);
    zsys s; z_sysinfo(&s); CW = zw_new("Mensaje nuevo", -1, 0, MIN(760, (int)s.scr_w - 60), MIN(520, (int)s.scr_h - 100), WF_DECOR | WF_RESIZE); draw_compose();
    { int sx, sy; zw_screen(CW, 0, 0, &sx, &sy); char lg[160]; snprintf(lg, 160, "[prueba] correo escribir: para %d,%d asunto %d,%d texto %d,%d adjuntar %d,%d enviar %d,%d borrador %d,%d", sx + 200, sy + 60, sx + 200, sy + 92, sx + 200, sy + 200, sx + 255, sy + 20, sx + 50, sy + 20, sx + 380, sy + 20); z_log(lg); } }
static void draft_save(void) { int e = loc_save(1, c_draft, c_to, c_subj, c_inrep, c_att, nca, CT.t ? CT.t : ""); if (e) { snprintf(msg, 200, "%s: %s", _("No se pudo guardar el borrador"), z_errstr(e)); return; }
    c_dirty = 0; strcpy(msg, _("Borrador guardado en Borradores")); char lg[120]; snprintf(lg, 120, "[prueba] correo borrador guardado %s", c_draft); z_log(lg); if (folder == 1) loc_list(1); }
/* cerrar la ventana Escribir: si hay algo sin guardar, ofrece guardarlo en Borradores */
static void compose_close(void) { if (c_dirty && (c_to[0] || c_subj[0] || (CT.t && CT.t[0])) && ui_confirm("Borrador", "¿Guardar este mensaje en Borradores?")) draft_save(); zw_close(CW); CW = 0; }
static int c_drag;
static void add_attach(void) {
    if (nca >= 8) { strcpy(msg, _("Como mucho 8 adjuntos por mensaje")); return; }
    char p[260]; if (!z_pick_file(_("Adjuntar un archivo"), 0, p, 260)) return;
    zdirent st; if (z_stat(p, &st)) return; long tot = (long)st.size; for (int k = 0; k < nca; k++) { zdirent s2; if (!z_stat(c_att[k], &s2)) tot += (long)s2.size; }
    if (tot > 20LL * 1024 * 1024) { strcpy(msg, _("Los adjuntos pasan de 20 MB: muchos servidores no los aceptan")); draw(); return; }
    strncpy0(c_att[nca++], p, 260); char lg[300]; snprintf(lg, 300, "[prueba] correo adjuntado %s", p); z_log(lg); }
static void compose_ev(zevent *e) { if (e->type == EV_CLOSE) { compose_close(); return; } if (e->type == EV_RESIZE) { zw_resized(CW, e->x, e->y); draw_compose(); return; } if (e->type == EV_REDRAW) { draw_compose(); return; }
    if (e->type == EV_DOWN) { if (e->y < 40) { if (e->x < 100) { if (!smtp_send(c_to, c_subj, CT.t ? CT.t : "", c_inrep)) { char f[40] = ""; loc_save(2, f, c_to, c_subj, c_inrep, c_att, nca, CT.t ? CT.t : "");
                    if (c_draft[0]) { char p[200]; snprintf(p, 200, "%s/%s", FDIR[1], c_draft); z_unlink(p); z_sync(); } if (folder) loc_list(folder); zw_close(CW); CW = 0; draw(); return; } draw(); }
            else if (e->x < 196) { compose_close(); return; } else if (e->x >= 200 && e->x < 310) { add_attach(); c_dirty = 1; draw_compose(); return; }
            else if (e->x >= 316 && e->x < 316 + g_textw(_("Guardar borrador")) + 14) { draft_save(); draw(); draw_compose(); return; } }
        else if (nca && e->y >= 112 && e->y < c_top() - 2) { int cw = (CW->w - 24) / 3, k = (e->y - 112) / 24 * 3 + (e->x - 12) / cw; if (k >= 0 && k < nca && (e->x - 12) % cw >= cw - 34) { memmove(c_att[k], c_att[k + 1], (size_t)(nca - k - 1) * sizeof c_att[0]); nca--; } }
        else if (e->y < 76) { c_foc = 0; if (e->btn & 2) ui_input_menu(CW, c_to, 590, &c_cur, e); else ui_input_click(CW, e, c_to, &c_cur); } else if (e->y < 108) { c_foc = 1; if (e->btn & 2) ui_input_menu(CW, c_subj, 290, &c_cur, e); else ui_input_click(CW, e, c_subj, &c_cur); }
        else { c_foc = 2; if (e->btn & 2) te_menu(CW, &CT, e); else { te_click(&CT, e->x - 14, e->y - c_top() - 4, 1, e->mods & M_SHIFT); c_drag = 1; } } draw_compose(); return; }
    if (e->type == EV_MOVE && c_drag) { if (!e->btn) c_drag = 0; else { te_click(&CT, e->x - 14, e->y - c_top() - 4, 1, 1); draw_compose(); } return; } if (e->type == EV_UP) { c_drag = 0; return; }
    if (e->type == EV_KEY) { if (e->key != K_TAB) c_dirty = 1; if (e->key == K_TAB) c_foc = (c_foc + 1) % 3; else if (c_foc == 0) ui_input_key(c_to, 590, &c_cur, e); else if (c_foc == 1) ui_input_key(c_subj, 290, &c_cur, e); else te_key(&CT, e, 10); if (c_foc < 2 && e->key == K_TAB) c_cur = strlen(c_foc ? c_subj : c_to); draw_compose(); } }
static void open_local(int i) { if (i < 0 || i >= nlf) return; char p[200]; snprintf(p, 200, "%s/%s", FDIR[folder], LF[i].file); long n; char *t = z_readfile(p, &n); if (!t) { strcpy(msg, _("No se pudo leer")); return; }
    char *bd = strstr(t, "\n\n"); const char *text = bd ? bd + 2 : ""; if (bd) bd[1] = 0; char to[600], subj[300], inrep[200], date[24]; lkv(t, "para", to, 600); lkv(t, "asunto", subj, 300); lkv(t, "inrep", inrep, 200); lkv(t, "fecha", date, 24);
    char att[8][260]; int na = 0; for (char *q = t; q && *q && na < 8; q = strchr(q, '\n') ? strchr(q, '\n') + 1 : 0) if (!strncmp(q, "adjunto=", 8)) { int k = 0; q += 8; while (q[k] && q[k] != '\n' && k < 259) { att[na][k] = q[k]; k++; } att[na++][k] = 0; }
    if (folder == 1) { compose(to, subj, text, inrep[0] ? inrep : 0); strcpy(c_draft, LF[i].file); for (int k = 0; k < na; k++) { zdirent st; if (!z_stat(att[k], &st)) strcpy(c_att[nca++], att[k]); } if (CW) draw_compose(); free(t); char lg[120]; snprintf(lg, 120, "[prueba] correo borrador abierto %s", c_draft); z_log(lg); return; }
    sel = i; strncpy0(rd_from, A.user, 200); strncpy0(rd_to, to, 300); strncpy0(rd_subj, subj, 300); strncpy0(rd_date, date, 80); strncpy0(rd_reply, to, 200); rd_msgid[0] = 0; rd_att[0] = 0;
    for (int k = 0; k < na; k++) { const char *nm = strrchr(att[k], '/'); nm = nm ? nm + 1 : att[k]; int l = strlen(rd_att); snprintf(rd_att + l, 300 - l, "%s%s", k ? ", " : "", nm); }
    for (int k = 0; k < nra; k++) free(RA[k].d); nra = 0; free(body.p); body = (sb_t){0}; sb_s(&body, text); free(t); rtop = 0; wrap((W->w - 32) / 8); msg[0] = 0; char lg[300]; snprintf(lg, 300, "[prueba] correo enviado leido: %s", rd_subj); z_log(lg); }
static void open_item(int i) { if (folder) open_local(i); else open_msg(i); }
static int nitems(void) { return folder ? nlf : nh; }
static void set_folder(int f) { folder = f; sel = -1; topl = 0; free(body.p); body = (sb_t){0}; for (int k = 0; k < nra; k++) free(RA[k].d); nra = 0; if (f) { loc_list(f); snprintf(msg, 200, "%s: %d", _(f == 1 ? "Borradores" : "Enviados (confirmados por el servidor)"), nlf); } else msg[0] = 0; }
/* ---------- formulario de la cuenta (dentro de la ventana principal) ---------- */
static int setup, sf, scur; static char s_imap[96], s_smtp[96], s_sport[8];
static char *sfield(int i) { return i == 0 ? A.name : i == 1 ? A.user : i == 2 ? A.pass : i == 3 ? s_imap : i == 4 ? s_smtp : s_sport; }
static void draw_setup(void) { zwin *w = W; int p = prov_i(A.prov), y = 56; g_textb(w, 16, y, _("Cuenta de correo"), UI_TEXT); y += 28; static const char *PN[] = {"Gmail", "iCloud", "Yahoo", "Otro"};
    for (int i = 0; i < 4; i++) ui_button(w, 16 + i * 110, y, 100, 28, PN[i], i == p ? 2 : 0); y += 44; static const char *FN[] = {"Tu nombre", "Correo", "Contraseña de aplicación", "Servidor IMAP", "Servidor SMTP", "Puerto SMTP"};
    for (int i = 0; i < (p == 3 ? 6 : 3); i++) { g_text(w, 16, y + 6, _(FN[i]), UI_TEXT); char sh[130]; const char *v = sfield(i); if (i == 2) { int n = strlen(v); memset(sh, '*', MIN(n, 129)); sh[MIN(n, 129)] = 0; v = sh; } ui_input(w, 230, y, 380, 26, v, sf == i ? (i == 2 ? (int)strlen(v) : scur) : 0, sf == i); y += 36; }
    y += 6; zw_clip(w, 16, y, w->w - 32, 60); g_text(w, 16, y, _(PROV[p].help), UI_MUTED); zw_noclip(w); y += 26; g_text(w, 16, y, _("La contraseña se guarda en esta memoria (ZUNZUN/CORREO.TXT); el invitado no puede verla."), UI_MUTED); y += 34;
    ui_button(w, 16, y, 160, 30, "Guardar y entrar", 2); if (acct_load_ok()) ui_button(w, 186, y, 110, 30, "Cancelar", 0); }
static void setup_ev(zevent *e) { int p = prov_i(A.prov), y = 84;
    if (e->type == EV_KEY) { if (e->key == K_TAB) { sf = (sf + 1) % (p == 3 ? 6 : 3); scur = strlen(sfield(sf)); } else if (e->ch == '\n') goto save; else { int cap = sf == 5 ? 6 : sf >= 3 ? 90 : 120; ui_input_key(sfield(sf), cap, &scur, e); } return; }
    if (e->type != EV_DOWN) return; if (e->y >= y && e->y < y + 28) { int i = (e->x - 16) / 110; if (i >= 0 && i < 4) { strcpy(A.prov, PROV[i].id); if (i < 3) { strcpy(s_imap, PROV[i].imap); strcpy(s_smtp, PROV[i].smtp); snprintf(s_sport, 8, "%d", PROV[i].sport); } } return; } y += 44;
    for (int i = 0; i < (p == 3 ? 6 : 3); i++, y += 36) if (e->y >= y && e->y < y + 26) { sf = i; ui_input_click(W, e, sfield(i), &scur); return; }
    y += 6 + 26 + 34; if (e->y >= y && e->y < y + 30) { if (e->x >= 16 && e->x < 176) goto save; if (e->x >= 186 && e->x < 296 && acct_load_ok()) { acct_load(); setup = 0; } } return;
save: { int i = prov_i(A.prov); if (!A.user[0] || !A.pass[0]) { strcpy(msg, _("Escribe el correo y la contraseña de aplicación")); return; } strncpy0(A.imap, i < 3 ? PROV[i].imap : s_imap, 96); strncpy0(A.smtp, i < 3 ? PROV[i].smtp : s_smtp, 96); A.iport = 993; A.sport = i < 3 ? PROV[i].sport : atoi(s_sport) ? atoi(s_sport) : 465; A.starttls = i < 3 ? PROV[i].starttls : A.sport == 587;
        if (!A.imap[0] || !A.smtp[0]) { strcpy(msg, _("Faltan los servidores")); return; } char *sp = A.pass; for (char *q = A.pass; *q; q++) if (*q != ' ') *sp++ = *q; *sp = 0; /* Google muestra la clave en grupos con espacios */
        int er = acct_save(); if (er) { snprintf(msg, 200, "%s: %s", _("No se pudo guardar la cuenta"), z_errstr(er)); return; } acct_ok_cache = 1; setup = 0; ic_ok = 0; cn_close(&IC); refresh(); } }
/* ---------- ventana principal ---------- */
static const char *BT[] = {"Recibir", "Escribir", "Responder", "Reenviar", "Borrar", "Imprimir", "Cuenta"};
static int bx(int i) { int x = 8; for (int k = 0; k < i; k++) x += g_textw(_(BT[k])) + 20; return x; }
static const char *FN_[] = {"Entrada", "Borradores", "Enviados"};
static int fw_(int j) { return g_textw(_(FN_[j])) + 14; }
static int fx_(int j) { int x = W->w - 8; for (int k = 2; k >= j; k--) x -= fw_(k) + (k < 2 ? 6 : 0); return x; }
#define LH 20
static int list_h(void) { return MIN(nitems(), 12) * LH + 24; }
/* los errores de conexion tambien van al registro (DIAGNOST.TXT), sin la contrasena */
static void log_err(void) { static char last[200]; if (!msg[0] || !strcmp(msg, last)) return; strcpy(last, msg); if (strstr(msg, "IMAP") || strstr(msg, "SMTP") || strstr(msg, "TLS") || strstr(msg, _("No se")) || strstr(msg, _("El servidor")) || strstr(msg, _("El certificado"))) { char m[260]; snprintf(m, 260, "[prueba] correo error: %s", msg); z_log(m); } }
void draw(void) { log_err(); zwin *w = W; g_clear(w, UI_WHITE); zs_area(w, 0, 0, 0, 0, 0); g_rect(w, 0, 0, w->w, TB, UI_PANEL); for (int i = 0; i < 7; i++) ui_button(w, bx(i), 7, g_textw(_(BT[i])) + 14, 26, BT[i], i == 1 ? 2 : 0);
    if (fx_(0) > bx(7)) for (int j = 0; j < 3; j++) ui_button(w, fx_(j), 7, fw_(j), 26, FN_[j], folder == j ? 2 : 0);
    { static int logged; if (!logged) { logged = 1; char lg[160]; int p[6]; for (int j = 0; j < 3; j++) zw_screen(w, fx_(j) + fw_(j) / 2, 20, &p[2 * j], &p[2 * j + 1]); snprintf(lg, 160, "[prueba] correo carpetas: entrada %d,%d borradores %d,%d enviados %d,%d", p[0], p[1], p[2], p[3], p[4], p[5]); z_log(lg); } }
    if (setup) { draw_setup(); } else { int y = TB + 4, lh = list_h(); g_rect(w, 0, y, w->w, 20, UI_PANEL); g_text(w, 26, y + 2, _(folder ? "Para" : "De"), UI_TEXT); g_text(w, 260, y + 2, _("Asunto"), UI_TEXT); g_text(w, w->w - 190, y + 2, _("Fecha"), UI_TEXT); y += 22;
        for (int i = topl; folder && i < nlf && i < topl + 12; i++) { loc_t *l = &LF[i]; if (i == sel) g_rect(w, 0, y - 1, w->w, LH, UI_ACC2); zw_clip(w, 26, y, 226, LH); g_text(w, 26, y + 1, l->to[0] ? l->to : _("(sin destinatario)"), UI_TEXT); zw_clip(w, 260, y, w->w - 460, LH); g_text(w, 260, y + 1, l->subj[0] ? l->subj : _("(sin asunto)"), UI_TEXT); zw_noclip(w); g_text(w, w->w - 190, y + 1, l->date, UI_MUTED); y += LH; }
        if (folder && !nlf) g_text(w, 26, y + 4, _(folder == 1 ? "No hay borradores." : "Todavía no has enviado mensajes desde este equipo."), UI_MUTED);
        for (int i = topl; !folder && i < nh && i < topl + 12; i++) { hdr_t *h = &H[i]; if (i == sel) g_rect(w, 0, y - 1, w->w, LH, UI_ACC2); if (!h->seen) g_circle(w, 12, y + 8, 4, UI_ACC, 1); zw_clip(w, 26, y, 226, LH); (h->seen ? g_text : g_textb)(w, 26, y + 1, h->from, UI_TEXT); zw_clip(w, 260, y, w->w - 460, LH); (h->seen ? g_text : g_textb)(w, 260, y + 1, h->subj[0] ? h->subj : _("(sin asunto)"), UI_TEXT); zw_noclip(w); g_text(w, w->w - 190, y + 1, h->date, UI_MUTED); y += LH; }
        if (!folder && !nh) g_text(w, 26, y + 4, _(acct_load_ok() ? "No hay mensajes. Pulsa Recibir." : "Pulsa Cuenta para configurar tu correo."), UI_MUTED);
        y = TB + 4 + lh + 4; g_rect(w, 0, y, w->w, 1, UI_LINE); y += 6; zs_area(w, 0, y, w->w, w->h - 22 - y, 0);
        if (sel >= 0 && body.p) { g_textb(w, 16, y, rd_subj, UI_TEXT); y += 20; char b[400]; snprintf(b, 400, "%s: %s", _("De"), rd_from); g_text(w, 16, y, b, UI_MUTED); y += 18; snprintf(b, 400, "%s: %s", _("Para"), rd_to); zw_clip(w, 16, y, w->w - 32, 18); g_text(w, 16, y, b, UI_MUTED); zw_noclip(w); y += 18; snprintf(b, 400, "%s: %s", _("Fecha"), rd_date); g_text(w, 16, y, b, UI_MUTED); y += 18;
            for (int k = 0; k < nra; k++) { char sz[24]; if (RA[k].n < 1024) snprintf(sz, 24, "%ld B", (long)RA[k].n); else if (RA[k].n < 1048576) snprintf(sz, 24, "%ld KB", (long)((RA[k].n + 1023) / 1024)); else snprintf(sz, 24, "%ld.%ld MB", (long)(RA[k].n / 1048576), (long)(RA[k].n % 1048576 * 10 / 1048576)); snprintf(b, 400, "%s: %s (%s)", _("Adjunto"), RA[k].name, sz); zw_clip(w, 16, y, w->w - 230, 24); g_text(w, 16, y + 4, b, 0xB45309); zw_noclip(w); ui_button(w, w->w - 206, y, 90, 24, "Abrir", 0); ui_button(w, w->w - 110, y, 94, 24, "Guardar", 0); att_y[k] = y; y += 28; }
            { static u32 logged; if (nra && logged != H[sel].uid) { logged = H[sel].uid; int sx, sy; zw_screen(w, w->w - 63, att_y[0] + 12, &sx, &sy); char lg[120]; snprintf(lg, 120, "[prueba] correo adjunto boton %d,%d", sx, sy); z_log(lg); } }
            if (!nra && rd_att[0]) { snprintf(b, 400, "%s: %s", _("Adjuntos"), rd_att); zw_clip(w, 16, y, w->w - 32, 18); g_text(w, 16, y, b, 0xB45309); zw_noclip(w); y += 18; } y += 6; int vis = (w->h - 24 - y) / 18; rtop = MAX(0, MIN(rtop, nwl - vis)); for (int i = rtop; i < nwl && i < rtop + vis; i++, y += 18) g_text(w, 16, y, wl[i], UI_TEXT); } }
    zs_stop(w); zs_show(w); g_rect(w, 0, w->h - 22, w->w, 22, UI_PANEL); zw_clip(w, 0, w->h - 22, w->w, 22); g_text(w, 8, w->h - 19, msg, UI_TEXT); zw_noclip(w); zw_present(w); }
static void quote(sb_t *o) { sb_s(o, "\n\n"); sb_f(o, _("El %s, %s escribió:"), rd_date, rd_from); sb_s(o, "\n"); for (int i = 0; i < nwl; i++) sb_f(o, "> %s\n", wl[i]); }
static void act(int b) { char t[400];
    if (b == 0) { set_folder(0); if (acct_load_ok()) refresh(); else setup = 1; }
    else if (b == 1) compose("", "", "", 0);
    else if ((b == 2 || b == 3) && sel >= 0 && body.p) { sb_t q = {0}; sb_s(&q, ""); quote(&q); int fw = b == 3; const char *s = rd_subj; if (!strncasecmp(s, fw ? "Fwd: " : "Re: ", fw ? 5 : 4)) s += fw ? 5 : 4; snprintf(t, 400, "%s%s", fw ? "Fwd: " : "Re: ", s); compose(fw ? "" : rd_reply, t, q.p, fw ? 0 : rd_msgid); free(q.p);
        if (fw) for (int k = 0; k < nra && nca < 8; k++) { char p[260]; snprintf(p, 260, "/rd/tmp/correo/%s", RA[k].name); z_mkdir("/rd/tmp/correo"); if (!z_writefile(p, RA[k].d, RA[k].n)) strncpy0(c_att[nca++], p, 260); } if (fw && CW) draw_compose(); }
    else if (b == 4 && sel >= 0 && folder) { if (!ui_confirm("Borrar", folder == 1 ? "¿Borrar este borrador?" : "¿Borrar este mensaje de Enviados? Solo se borra la copia de este equipo.")) return;
        char p[200]; snprintf(p, 200, "%s/%s", FDIR[folder], LF[sel].file); z_unlink(p); z_sync(); set_folder(folder); strcpy(msg, _("Mensaje borrado")); }
    else if (b == 4 && sel >= 0) { if (!ui_confirm("Borrar", "¿Borrar este mensaje del servidor?")) return; if (imap_login() || imap(0, "SELECT INBOX") || imap(0, "UID STORE %u +FLAGS (\\Deleted)", H[sel].uid) || imap(0, "EXPUNGE")) { snprintf(msg, 200, "IMAP: %s", IC.err); return; } memmove(&H[sel], &H[sel + 1], (nh - sel - 1) * sizeof *H); nh--; sel = -1; free(body.p); body = (sb_t){0}; strcpy(msg, _("Mensaje borrado")); }
    else if (b == 5 && sel >= 0 && body.p) { char sub[600]; snprintf(sub, 600, "%s: %s   %s: %s", _("De"), rd_from, _("Fecha"), rd_date); sb_t tx = {0}; sb_s(&tx, ""); if (rd_att[0]) sb_f(&tx, "%s: %s\n\n", _("Adjuntos"), rd_att); sb_s(&tx, body.p); zp_print_text(rd_subj[0] ? rd_subj : _("(sin asunto)"), sub, tx.p, 0); free(tx.p); }
    else if (b == 5) strcpy(msg, _("Abre un mensaje para imprimirlo"));
    else if (b == 6) { setup = 1; sf = 1; scur = strlen(A.user); int i = prov_i(A.prov); strcpy(s_imap, A.imap); strcpy(s_smtp, A.smtp); snprintf(s_sport, 8, "%d", A.sport ? A.sport : PROV[i].sport); if (!A.prov[0]) strcpy(A.prov, "gmail"); } }
int main(const char *a) { zsys s; z_sysinfo(&s); W = zw_new("Correo", -1, 0, MIN(980, (int)s.scr_w - 40), MIN(660, (int)s.scr_h - 80), WF_DECOR | WF_RESIZE); acct_ok_cache = acct_load();
    if (!acct_ok_cache) { strcpy(A.prov, "gmail"); act(6); } draw(); if (acct_ok_cache) { refresh(); draw(); } zevent e;
    for (;;) { if (!z_event(&e, 60000)) { if (ic_ok && !setup) { imap(0, "NOOP"); } continue; } if (e.type == EV_REDRAW) { draw(); if (CW) draw_compose(); continue; }
        if (CW && e.win == (u32)CW->id) { compose_ev(&e); continue; } if (e.win != (u32)W->id) continue; if (e.type == EV_CLOSE) { if (ic_ok) { imap(0, "LOGOUT"); cn_close(&IC); } return 0; } if (e.type == EV_RESIZE) { zw_resized(W, e.x, e.y); wrap((W->w - 32) / 8); draw(); continue; }
        if (e.type == EV_KEY && e.ch == 16) { act(5); draw(); continue; } if (e.type == EV_KEY && e.ch == 14) { act(1); draw(); continue; }
        if (e.type == EV_DOWN && e.y < TB && fx_(0) > bx(7) && e.x >= fx_(0)) { for (int j = 2; j >= 0; j--) if (e.x >= fx_(j)) { if (e.x < fx_(j) + fw_(j)) { set_folder(j); if (!j && acct_load_ok() && !nh) refresh(); } break; } draw(); continue; }
        if (e.type == EV_DOWN && e.y < TB) { for (int i = 6; i >= 0; i--) if (e.x >= bx(i)) { if (e.x < bx(i) + g_textw(_(BT[i])) + 14) act(i); break; } draw(); continue; }
        if (setup) { setup_ev(&e); draw(); continue; }
        if (zs_event(W, &e)) { draw(); continue; } if (e.type == EV_WHEEL) zs_clear(W);
        if (e.type == EV_WHEEL) { int lh = TB + 4 + list_h(); if (e.y < lh) topl = MAX(0, MIN(topl - (int)e.key, nitems() - 12)); else rtop -= (int)e.key * 3; draw(); continue; }
        if (e.type == EV_KEY) { if (e.key == K_DOWN && sel + 1 < nitems() && folder != 1) { open_item(sel + 1); if (sel >= topl + 12) topl = sel - 11; } else if (e.key == K_UP && sel > 0 && folder != 1) { open_item(sel - 1); if (sel < topl) topl = sel; } else if (e.key == K_PGDN || e.ch == ' ') rtop += 20; else if (e.key == K_PGUP) rtop -= 20; else if (e.key == K_DEL) act(4); draw(); continue; }
        if (e.type == EV_DOWN && (e.btn & 2) && sel >= 0 && body.p && e.y > TB + 4 + list_h()) { int sx, sy; zw_screen(W, e.x, e.y, &sx, &sy); static const char *it[] = {"Copiar el mensaje", "Responder", "Reenviar", "Imprimir"}; int r = ui_menu(sx, sy, it, 4); if (r == 0) { sb_t c = {0}; sb_f(&c, "%s: %s\n%s: %s\n%s: %s\n\n%s", _("De"), rd_from, _("Fecha"), rd_date, _("Asunto"), rd_subj, body.p); z_clip_set(c.p, c.n); free(c.p); strcpy(msg, _("Mensaje copiado")); } else if (r > 0) act(r == 1 ? 2 : r == 2 ? 3 : 5); draw(); continue; }
        if (e.type == EV_DOWN) { for (int k = 0; k < nra; k++) if (e.y >= att_y[k] && e.y < att_y[k] + 24 && e.x >= W->w - 206) { att_save(k, e.x < W->w - 112); draw(); goto next_ev; }
            int y0 = TB + 4 + 22; if (e.y >= y0 && e.y < y0 + MIN(nitems(), 12) * LH) { open_item(topl + (e.y - y0) / LH); draw(); } }
        next_ev:; }
}
