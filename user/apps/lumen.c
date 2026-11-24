/* LUMEN: chat con IA para ZunzunOS. Version nativa (sin JavaScript) del cliente web LUMEN de tonyfluxa.top.
   Habla con OpenRouter (o con cualquier servidor compatible con la API de OpenAI, tambien en la red local)
   por HTTPS con certificados verificados. Respuestas en vivo (SSE), modelos gratuitos, razonamiento plegable,
   Markdown basico (titulos, listas, tablas, citas y codigo), conversaciones guardadas, instrucciones de sistema,
   exportar a Markdown, copiar e imprimir.
   La clave y los ajustes van en /sys/ZUNZUN/LUMEN.TXT; los chats, en /sys/ZUNZUN/LUMEN/ (uno por archivo). */
#include "z.h"
#define TB 40
#define SBH 22
#define LH 18
#define SW 220
#define DEF_API "https://openrouter.ai/api/v1"
#define DEF_MODEL "openrouter/free"
#define LYC_API "https://api.lyceum.technology/openai/v1"   /* Lyceum: servidores en la UE (Paris y Finlandia) */
#define LYC_MODEL "lyceum/router"
static zwin *W; static char msg[300], base[48];
static long atol(const char *s) { while (*s == ' ') s++; int g = *s == '-'; if (g) s++; long v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + *s++ - '0'; return g ? -v : v; }
static int isalnum(int c) { return isalpha(c) || isdigit(c); }
static long now_num(void) { u32 t[6]; z_time(t); return (long)t[5] * 10000000000L + (long)t[4] * 100000000L + (long)t[3] * 1000000L + t[2] * 10000 + t[1] * 100 + t[0]; }
static char *memchr_(const char *s, int c, long n) { for (long i = 0; i < n; i++) if (s[i] == (char)c) return (char *)s + i; return 0; }
static int u8len(u8 c) { return c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4; }
static int put_utf8(u32 c, char *o) { if (c < 0x10000) return utf8_put(c, o); o[0] = 0xF0 | c >> 18; o[1] = 0x80 | (c >> 12 & 63); o[2] = 0x80 | (c >> 6 & 63); o[3] = 0x80 | (c & 63); return 4; }
/* numero con decimales y exponente ("5e-07") */
static double num(const char *s, const char **end) { while (*s == ' ') s++; double v = 0, f = 1; int g = *s == '-'; if (*s == '-' || *s == '+') s++; while (isdigit(*s)) v = v * 10 + *s++ - '0';
    if (*s == '.') { s++; while (isdigit(*s)) { f /= 10; v += (*s++ - '0') * f; } }
    if (*s == 'e' || *s == 'E') { s++; int eg = *s == '-'; if (*s == '-' || *s == '+') s++; int x = 0; while (isdigit(*s)) x = x * 10 + *s++ - '0'; while (x--) v = eg ? v / 10 : v * 10; }
    if (end) *end = s; return g ? -v : v; }

/* ---------- JSON: lectura en el propio texto (las cadenas se desescapan en su sitio) ---------- */
typedef struct { u8 t; int ch, nx; char *k, *s; double n; } jn_t; /* t: 1 objeto, 2 lista, 3 texto, 4 numero, 5 si, 6 no, 7 nulo */
typedef struct { jn_t *n; int nn, cap; char *p; } jd_t;
static int jnew(jd_t *d, int t) { if (d->nn == d->cap) { d->cap = d->cap * 2 + 256; d->n = realloc(d->n, d->cap * sizeof *d->n); } jn_t *x = &d->n[d->nn]; memset(x, 0, sizeof *x); x->t = t; x->ch = x->nx = -1; return d->nn++; }
static void jws(jd_t *d) { while (*d->p == ' ' || *d->p == '\t' || *d->p == '\n' || *d->p == '\r') d->p++; }
static int hexv(int c) { return c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1; }
static u32 hex4(const char *p) { u32 v = 0; for (int i = 0; i < 4; i++) { int h = hexv(p[i]); if (h < 0) return 0xFFFFFFFF; v = v * 16 + h; } return v; }
static char *jstr(jd_t *d) { char *o = ++d->p, *w = o;
    while (*d->p && *d->p != '"') { if (*d->p != '\\') { *w++ = *d->p++; continue; } d->p++; char c = *d->p; if (!c) break; d->p++;
        if (c == 'n') *w++ = '\n'; else if (c == 't') *w++ = '\t'; else if (c == 'r') *w++ = '\r'; else if (c == 'b' || c == 'f') {} else if (c != 'u') *w++ = c;
        else { u32 u = hex4(d->p); if (u == 0xFFFFFFFF) continue; d->p += 4; if (u >= 0xD800 && u < 0xDC00 && d->p[0] == '\\' && d->p[1] == 'u') { u32 l = hex4(d->p + 2); if (l >= 0xDC00 && l < 0xE000) { u = 0x10000 + ((u - 0xD800) << 10) + (l - 0xDC00); d->p += 6; } } if (u) w += put_utf8(u, w); } }
    if (*d->p) d->p++; *w = 0; return o; }
static int jval(jd_t *d, int depth) { jws(d); char c = *d->p; if (depth > 60) return -1;
    if (c == '{' || c == '[') { int i = jnew(d, c == '{' ? 1 : 2), last = -1; char close = c == '{' ? '}' : ']'; d->p++;
        for (;;) { jws(d); if (*d->p == close) { d->p++; break; } if (!*d->p) return -1; char *k = 0;
            if (c == '{') { if (*d->p != '"') return -1; k = jstr(d); jws(d); if (*d->p != ':') return -1; d->p++; }
            int v = jval(d, depth + 1); if (v < 0) return -1; d->n[v].k = k; if (last < 0) d->n[i].ch = v; else d->n[last].nx = v; last = v; jws(d); if (*d->p == ',') d->p++; }
        return i; }
    if (c == '"') { int i = jnew(d, 3); d->n[i].s = jstr(d); return i; }
    if (c == '-' || isdigit(c)) { int i = jnew(d, 4); const char *e; d->n[i].n = num(d->p, &e); d->p = (char *)e; return i; }
    if (!strncmp(d->p, "true", 4)) { d->p += 4; return jnew(d, 5); } if (!strncmp(d->p, "false", 5)) { d->p += 5; return jnew(d, 6); } if (!strncmp(d->p, "null", 4)) { d->p += 4; return jnew(d, 7); }
    return -1; }
static int jparse(jd_t *d, char *text) { memset(d, 0, sizeof *d); d->p = text; return jval(d, 0); }
static void jfree(jd_t *d) { free(d->n); d->n = 0; d->nn = d->cap = 0; }
static int jget(jd_t *d, int o, const char *k) { if (o < 0 || d->n[o].t != 1) return -1; for (int c = d->n[o].ch; c >= 0; c = d->n[c].nx) if (!strcmp(d->n[c].k, k)) return c; return -1; }
static int jat(jd_t *d, int a, int i) { if (a < 0 || d->n[a].t != 2) return -1; int c = d->n[a].ch; while (c >= 0 && i--) c = d->n[c].nx; return c; }
static const char *js(jd_t *d, int i) { return i >= 0 && d->n[i].t == 3 ? d->n[i].s : 0; }
static double jn(jd_t *d, int i) { return i < 0 ? 0 : d->n[i].t == 4 ? d->n[i].n : d->n[i].t == 3 ? num(d->n[i].s, 0) : 0; }
static void jesc(sb_t *o, const char *s) { sb_s(o, "\""); for (; *s; s++) { u8 c = *s; if (c == '"' || c == '\\') { char e[2] = {'\\', c}; sb_add(o, e, 2); } else if (c == '\n') sb_s(o, "\\n"); else if (c == '\r') sb_s(o, "\\r"); else if (c == '\t') sb_s(o, "\\t"); else if (c < 32) sb_f(o, "\\u%04x", c); else sb_add(o, &c, 1); } sb_s(o, "\""); }

/* ---------- texto para la pantalla: la tipografia es CP437; se quitan emojis y se cambian simbolos sin dibujo ---------- */
static int keep_sym(u32 c) { return c == 0x2665 || c == 0x2666 || c == 0x2663 || c == 0x2660 || c == 0x263A || c == 0x263B || c == 0x263C || c == 0x266A || c == 0x266B; }
static void dnorm(sb_t *o, const char *s, long n) { const char *e = s + n; int skip_sp = 0;
    while (s < e) { const char *q = s; u32 c = utf8_next(&q); if (!c) break; if (q > e) q = e; const char *rep = 0; int drop = 0;
        if (c == '\r') drop = 1; else if (c == '\t') rep = "    "; else if (c < 32 && c != '\n') drop = 1;
        else if (c == 0x2026) rep = "..."; else if (c == 0x2260) rep = "!="; else if (c == 0x21D2) rep = "=>"; else if (c == 0x21D4) rep = "<=>"; else if (c == 0x2194) rep = "<->"; else if (c == 0x2122) rep = "(TM)"; else if (c == 0xAE) rep = "(R)"; else if (c == 0xA9) rep = "(c)";
        else if (c == 0x2212 || (c >= 0x2010 && c <= 0x2012) || c == 0x2015) rep = "-"; else if (c == 0x201A || c == 0x2032) rep = "'"; else if (c == 0x201E || c == 0x2033) rep = "\""; else if (c == 0xD7) rep = "x"; else if (c == 0x2705 || c == 0x2714 || c == 0x2611) rep = "\xE2\x88\x9A"; /* raiz como marca */
        else if (c == 0x274C || c == 0x2716 || c == 0x2717 || c == 0x2718 || c == 0x274E) rep = "x"; else if (c == 0x26A0 || c == 0x2757 || c == 0x2755) rep = "!"; else if (c == 0x2753 || c == 0x2754) rep = "?"; else if (c == 0x2B50 || c == 0x2728 || c == 0x2605 || c == 0x2606) rep = "*"; else if (c == 0x27A1 || c == 0x2794 || c == 0x279C) rep = "\xE2\x86\x92";
        else if ((c >= 0x200B && c <= 0x200F) || c == 0x2060 || c == 0xFEFF || (c >= 0xFE00 && c <= 0xFE0F) || c == 0x20E3 || (c >= 0xE0000 && c <= 0xE007F)) drop = 1;
        else if (c >= 0x1F000 || ((c >= 0x2600 && c <= 0x27BF) && !keep_sym(c)) || (c >= 0x2B00 && c <= 0x2BFF) || (c >= 0x2300 && c <= 0x23FF && c != 0x2302)) { drop = 1; skip_sp = 1; }
        if (drop) { s = q; continue; }
        if (skip_sp && c == ' ' && (!o->n || o->p[o->n - 1] == ' ' || o->p[o->n - 1] == '\n')) { s = q; continue; } skip_sp = 0;
        if (rep) sb_s(o, rep); else sb_add(o, s, q - s); s = q; }
    if (!o->p) sb_add(o, "", 0); }
/* copia hasta cap-1 bytes sin partir un caracter UTF-8 */
static int cpy_utf8(char *o, int cap, const char *s, long n) { int k = 0; for (long i = 0; i < n; ) { int l = u8len(s[i]); if (i + l > n || k + l > cap - 1) break; for (int j = 0; j < l; j++) o[k++] = s[i + j] == '\n' || s[i + j] == '\t' ? ' ' : s[i + j]; i += l; } o[k] = 0; return k; }
static void dnorm_str(const char *s, char *o, int cap) { sb_t b = {0}; dnorm(&b, s, strlen(s)); cpy_utf8(o, cap, b.p, b.n); free(b.p); }

/* ---------- ajustes ---------- */
typedef struct { char key[256], model[128], api[200], sys[1024]; int temp, maxtok, stream, side; int prov; char k2[3][256], m2[3][128], custom[200]; } cfg_t;   /* prov: 0 OpenRouter, 1 Lyceum, 2 otro servidor */
static cfg_t C; static int kst; static char kdet[300]; /* kst: 0 sin clave, 1 comprobando, 2 valida, 3 no valida, 4 sin verificar */
static void path_of(char *o, int cap, const char *f) { snprintf(o, cap, "%s/%s", base, f); }
static void kv(const char *t, const char *k, char *o, int cap) { int l = strlen(k); for (const char *p = t; p && *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : 0) if (!strncmp(p, k, l) && p[l] == '=') { int i = 0; p += l + 1; while (p[i] && p[i] != '\n' && i < cap - 1) { o[i] = p[i]; i++; } o[i] = 0; return; } }
static void unesc_nl(char *s) { char *w = s; for (char *r = s; *r; r++) { if (r[0] == '\\' && r[1] == 'n') { *w++ = '\n'; r++; } else if (r[0] == '\\' && r[1] == '\\') { *w++ = '\\'; r++; } else *w++ = *r; } *w = 0; }
static void esc_nl(sb_t *b, const char *s) { for (; *s; s++) { if (*s == '\n') sb_s(b, "\\n"); else if (*s == '\\') sb_s(b, "\\\\"); else if (*s != '\r') sb_add(b, s, 1); } }
/* el proveedor activo usa C.key / C.model / C.api; los otros guardan los suyos en k2 / m2 */
static void prov_keep(void) { strcpy(C.k2[C.prov], C.key); strcpy(C.m2[C.prov], C.model); if (C.prov == 2) strncpy0(C.custom, C.api, 200); }
static void prov_use(int p) { C.prov = p; strcpy(C.key, C.k2[p]); strcpy(C.model, C.m2[p][0] ? C.m2[p] : p == 0 ? DEF_MODEL : p == 1 ? LYC_MODEL : ""); strncpy0(C.api, p == 0 ? DEF_API : p == 1 ? LYC_API : C.custom[0] ? C.custom : "http://", 200); }
static void cfg_load(void) { memset(&C, 0, sizeof C); strcpy(C.model, DEF_MODEL); strcpy(C.api, DEF_API); C.temp = 7; C.stream = 1; C.side = 1; char p[96]; path_of(p, 96, "LUMEN.TXT"); char *t = z_readfile(p, 0); if (!t) return; char v[32];
    kv(t, "clave", C.key, 256); kv(t, "modelo", C.model, 128); kv(t, "servidor", C.api, 200); kv(t, "sistema", C.sys, 1024); unesc_nl(C.sys);
    { char pv[8] = ""; kv(t, "proveedor", pv, 8); strcpy(C.m2[1], LYC_MODEL);
      if (pv[0]) { C.prov = atoi(pv) % 3; strcpy(C.k2[0], C.key); strcpy(C.m2[0], C.model); kv(t, "clave_lyceum", C.k2[1], 256); kv(t, "clave_otro", C.k2[2], 256); kv(t, "modelo_lyceum", C.m2[1], 128); kv(t, "modelo_otro", C.m2[2], 128); if (!C.m2[1][0]) strcpy(C.m2[1], LYC_MODEL); }
      else { C.prov = strstr(C.api, "openrouter.ai") ? 0 : strstr(C.api, "lyceum.technology") ? 1 : 2; strcpy(C.k2[C.prov], C.key); strcpy(C.m2[C.prov], C.model); if (!C.m2[0][0]) strcpy(C.m2[0], DEF_MODEL); }   /* formato anterior */
      if (C.prov == 2) strncpy0(C.custom, C.api, 200); prov_use(C.prov); }
    v[0] = 0; kv(t, "temperatura", v, 32); if (v[0]) C.temp = atoi(v); v[0] = 0; kv(t, "max_tokens", v, 32); if (v[0]) C.maxtok = atoi(v); v[0] = 0; kv(t, "en_vivo", v, 32); if (v[0]) C.stream = atoi(v); v[0] = 0; kv(t, "lateral", v, 32); if (v[0]) C.side = atoi(v);
    if (!C.model[0]) strcpy(C.model, DEF_MODEL); if (!C.api[0]) strcpy(C.api, DEF_API); free(t); }
static int cfg_save(void) { prov_keep(); sb_t b = {0}; sb_f(&b, "proveedor=%d\nclave=%s\nclave_lyceum=%s\nclave_otro=%s\nmodelo=%s\nmodelo_lyceum=%s\nmodelo_otro=%s\nservidor=%s\ntemperatura=%d\nmax_tokens=%d\nen_vivo=%d\nlateral=%d\nsistema=", C.prov, C.k2[0], C.k2[1], C.k2[2], C.m2[0], C.m2[1], C.m2[2], C.custom, C.temp, C.maxtok, C.stream, C.side); esc_nl(&b, C.sys); sb_s(&b, "\n");
    char p[96]; path_of(p, 96, "LUMEN.TXT"); int e = z_writefile(p, b.p, b.n); free(b.p); z_sync(); return e; }
static int is_or(void) { return strstr(C.api, "openrouter.ai") != 0; }
static int is_ly(void) { return strstr(C.api, "lyceum.technology") != 0; }
/* la clave tal como se pego: sin espacios, comillas, caracteres invisibles ni "Bearer" delante */
static void clean_key(char *k) { char o[256]; int n = 0; for (const char *p = k; *p && n < 255; ) { const char *q = p; u32 c = utf8_next(&q); if (c > ' ' && c < 127 && c != '"' && c != '\'' && c != '`') o[n++] = c; p = q; } o[n] = 0;
    char *s = o; if (!strncasecmp(s, "bearer", 6)) s += 6; if (!strncasecmp(s, "authorization:", 14)) s += 14; if (!strncasecmp(s, "bearer", 6)) s += 6; if (!strncasecmp(s, "api_key=", 8) || !strncasecmp(s, "api-key=", 8) || !strncasecmp(s, "api_key:", 8)) s += 8; strcpy(k, s); }

/* ---------- conversaciones ---------- */
typedef struct { char *s; short kind, blk; u8 st; } dl_t;
enum { K_TXT, K_H, K_CODE, K_REAS, K_META, K_USER, K_ERR, K_GAP, K_TOGGLE, K_RETRY, K_CODEH, K_HR, K_QUOTE, K_TABLE, K_WHO, K_THINK };
typedef struct { int role; sb_t t, r; char model[128]; int tin, tout, stopped, open; long ms; dl_t *L; int nL, cols, dirty; char *img; } msg_t;   /* img: rutas de imagenes (una por linea) */ /* role: 0 tu, 1 IA, 2 error */
#define MAXATT 8
typedef struct { char file[32], title[96], model[128]; char *sys; long upd; msg_t **m; int n, cap; char att[MAXATT][200]; int natt; } conv_t;
static conv_t **CV; static int ncv, cur = -1;
static conv_t *CC(void) { return cur >= 0 && cur < ncv ? CV[cur] : 0; }
static void lines_free(msg_t *m) { for (int i = 0; i < m->nL; i++) free(m->L[i].s); free(m->L); m->L = 0; m->nL = 0; }
static void msg_free(msg_t *m) { lines_free(m); free(m->t.p); free(m->r.p); free(m->img); free(m); }
static msg_t *msg_add(conv_t *c, int role) { if (c->n == c->cap) { c->cap = c->cap * 2 + 16; c->m = realloc(c->m, c->cap * sizeof *c->m); } msg_t *m = calloc(1, sizeof *m); m->role = role; m->dirty = 1; sb_add(&m->t, "", 0); sb_add(&m->r, "", 0); c->m[c->n++] = m; return m; }
static void msg_del(conv_t *c, int i) { if (i < 0 || i >= c->n) return; msg_free(c->m[i]); memmove(c->m + i, c->m + i + 1, (c->n - i - 1) * sizeof *c->m); c->n--; }
static void conv_free(conv_t *c) { for (int i = 0; i < c->n; i++) msg_free(c->m[i]); free(c->m); free(c->sys); free(c); }
static void put_block(sb_t *b, const char *s) { for (const char *p = s; *p; ) { const char *e = strchr(p, '\n'); long n = e ? e - p : (long)strlen(p); if (n >= 2 && p[0] == '@' && p[1] == '@') sb_s(b, "@"); sb_add(b, p, n); sb_s(b, "\n"); if (!e) break; p = e + 1; } }
static int conv_save(conv_t *c) { if (!c->file[0]) { long t = now_num(); for (int k = 0; k < 100; k++) { snprintf(c->file, 32, "%ld%02d.TXT", t % 10000000000L, k); char p[128]; snprintf(p, 128, "%s/LUMEN/%s", base, c->file); zdirent d; if (z_stat(p, &d)) break; } }
    sb_t b = {0}; sb_f(&b, "LUMEN1\ntitulo=%s\nmodelo=%s\nfecha=%ld\nsistema=", c->title, c->model, c->upd); esc_nl(&b, c->sys ? c->sys : ""); sb_s(&b, "\n");
    if (c->natt) { sb_s(&b, "adjuntos="); for (int i = 0; i < c->natt; i++) { if (i) sb_s(&b, "|"); sb_s(&b, c->att[i]); } sb_s(&b, "\n"); }
    for (int i = 0; i < c->n; i++) { msg_t *m = c->m[i]; if (m->role == 0) { sb_s(&b, "@@tu\n"); if (m->img) { sb_s(&b, "@@imagenes "); for (const char *q = m->img; *q; q++) sb_add(&b, *q == '\n' ? "|" : q, 1); sb_s(&b, "\n"); } } else if (m->role == 1) sb_f(&b, "@@ia %s|%d|%d|%ld|%d\n", m->model, m->tin, m->tout, m->ms, m->stopped); else sb_s(&b, "@@error\n"); put_block(&b, m->t.p);
        if (m->role == 1 && m->r.n) { sb_s(&b, "@@razonamiento\n"); put_block(&b, m->r.p); } }
    char d[96]; snprintf(d, 96, "%s/LUMEN", base); z_mkdir(base); z_mkdir(d); char p[128]; snprintf(p, 128, "%s/%s", d, c->file); int e = z_writefile(p, b.p, b.n); free(b.p); z_sync(); return e; }
static void trim_nl(sb_t *s) { if (s->n && s->p[s->n - 1] == '\n') s->p[--s->n] = 0; }
static conv_t *conv_load(const char *file) { char p[128]; snprintf(p, 128, "%s/LUMEN/%s", base, file); char *t = z_readfile(p, 0); if (!t) return 0; if (strncmp(t, "LUMEN1\n", 7)) { free(t); return 0; }
    conv_t *c = calloc(1, sizeof *c); strncpy0(c->file, file, 32); char v[1100]; kv(t, "titulo", c->title, 96); kv(t, "modelo", c->model, 128); v[0] = 0; kv(t, "fecha", v, 32); c->upd = atol(v); v[0] = 0; kv(t, "sistema", v, 1100); unesc_nl(v); c->sys = strdup(v);
    { char a[MAXATT * 201]; a[0] = 0; kv(t, "adjuntos", a, sizeof a); for (char *q = a; *q && c->natt < MAXATT; ) { char *e = strchr(q, '|'); if (e) *e = 0; strncpy0(c->att[c->natt++], q, 200); if (!e) break; q = e + 1; } }
    msg_t *m = 0; sb_t *dst = 0; for (char *l = t; l && *l; ) { char *e = strchr(l, '\n'); if (e) *e = 0;
        if (l[0] == '@' && l[1] == '@' && l[2] != '@') { if (dst) trim_nl(dst); dst = 0;
            if (!strcmp(l, "@@tu")) { m = msg_add(c, 0); dst = &m->t; } else if (!strncmp(l, "@@imagenes ", 11) && m && m->role == 0) { m->img = strdup(l + 11); for (char *q = m->img; *q; q++) if (*q == '|') *q = '\n'; dst = &m->t; } else if (!strcmp(l, "@@error")) { m = msg_add(c, 2); dst = &m->t; } else if (!strcmp(l, "@@razonamiento") && m && m->role == 1) dst = &m->r;
            else if (!strncmp(l, "@@ia", 4)) { m = msg_add(c, 1); dst = &m->t; char *f = l + 4; while (*f == ' ') f++; char *b = strchr(f, '|'); if (b) *b = 0; strncpy0(m->model, f, 128); if (b) { m->tin = atoi(b + 1); char *q = strchr(b + 1, '|'); if (q) { m->tout = atoi(q + 1); q = strchr(q + 1, '|'); if (q) { m->ms = atol(q + 1); q = strchr(q + 1, '|'); if (q) m->stopped = atoi(q + 1); } } } } }
        else if (dst) { const char *s = l[0] == '@' && l[1] == '@' ? l + 1 : l; sb_s(dst, s); sb_s(dst, "\n"); }
        l = e ? e + 1 : 0; }
    if (dst) trim_nl(dst); free(t); return c; }
static int conv_cmp(const void *a, const void *b) { long x = (*(conv_t **)a)->upd, y = (*(conv_t **)b)->upd; return x < y ? 1 : x > y ? -1 : 0; }
static void conv_sort(void) { conv_t *c = CC(); qsort(CV, ncv, sizeof *CV, conv_cmp); for (int i = 0; i < ncv; i++) if (CV[i] == c) cur = i; }
static void conv_load_all(void) { char d[96]; snprintf(d, 96, "%s/LUMEN", base); zdirent e; for (int i = 0; !z_readdir(d, i, &e) && ncv < 500; i++) { if (e.is_dir || !z_ends(e.name, ".TXT")) continue; conv_t *c = conv_load(e.name); if (!c) continue; CV = realloc(CV, (ncv + 1) * sizeof *CV); CV[ncv++] = c; } conv_sort(); }
static conv_t *conv_new(void) { conv_t *c = calloc(1, sizeof *c); strncpy0(c->model, C.model, 128); c->sys = strdup(""); c->upd = now_num(); CV = realloc(CV, (ncv + 1) * sizeof *CV); memmove(CV + 1, CV, ncv * sizeof *CV); CV[0] = c; ncv++; cur = 0; return c; }
static void conv_title(conv_t *c) { if (c->title[0]) return; for (int i = 0; i < c->n; i++) if (c->m[i]->role == 0) { char t[96]; const char *s = c->m[i]->t.p; while (*s == ' ' || *s == '\n') s++; int k = cpy_utf8(t, 61, s, strlen(s)); if ((long)strlen(s) > k) strcat(t, "..."); strncpy0(c->title, t, 96); return; } }
static void conv_remove(int i) { if (i < 0 || i >= ncv) return; conv_t *c = CV[i]; if (c->file[0]) { char p[128]; snprintf(p, 128, "%s/LUMEN/%s", base, c->file); z_unlink(p); z_sync(); } conv_free(c); memmove(CV + i, CV + i + 1, (ncv - i - 1) * sizeof *CV); ncv--; if (cur == i) cur = -1; else if (cur > i) cur--; }

/* ---------- modelos ---------- */
typedef struct { char id[112], name[80]; long ctx; double pin, pout; int fr; } model_t;
static model_t *MD; static int nmd; static long md_at; static int md_tab, md_top, md_cur, md_loading; static char md_q[80];
static const char *RANK[] = {"deepseek/deepseek-v4.1-flash:free", "deepseek/deepseek-v4.1-flash", "openrouter/free", "deepseek/", "google/gemini", "google/gemma", "anthropic/", "openai/", "meta-llama/", "qwen/", "mistralai/", "nvidia/", "inclusionai/", "baidu/", "liquid/", "particle-ai/"};
static int rank_of(const char *id) { for (unsigned i = 0; i < sizeof RANK / sizeof *RANK; i++) if (!strcmp(id, RANK[i]) || (RANK[i][strlen(RANK[i]) - 1] == '/' && !strncmp(id, RANK[i], strlen(RANK[i])))) return i; return 99; }
static int md_cmp(const void *a, const void *b) { const model_t *x = a, *y = b; int r = rank_of(x->id) - rank_of(y->id); return r ? r : strcasecmp(x->name, y->name); }
static void clean_name(const char *n, char *o, int cap) { const char *c = strstr(n, ": "); if (c && c - n >= 2 && c - n <= 24) n = c + 2; char t[160]; dnorm_str(n, t, 160); int l = strlen(t); if (l > 7 && !strcasecmp(t + l - 7, " (free)")) t[l - 7] = 0; strncpy0(o, t, cap); }
static void md_add(const char *id, const char *name, long ctx, double pin, double pout, int fr) { MD = realloc(MD, (nmd + 1) * sizeof *MD); model_t *m = &MD[nmd++]; strncpy0(m->id, id, 112); clean_name(name && *name ? name : id, m->name, 80); m->ctx = ctx; m->pin = pin; m->pout = pout; m->fr = fr; }
/* modelos que OpenRouter no publica en /models pero existen (se piden por su nombre): siempre en la lista */
static void md_pinned(void) { if (!is_or()) return; static const struct { const char *id, *name; long ctx; } P[] = {{"deepseek/deepseek-v4.1-flash:free", "DeepSeek: DeepSeek V4.1 Flash (free)", 1048576}};
    for (unsigned i = 0; i < sizeof P / sizeof *P; i++) { int have = 0; for (int k = 0; k < nmd; k++) if (!strcmp(MD[k].id, P[i].id)) have = 1; if (!have) md_add(P[i].id, P[i].name, P[i].ctx, 0, 0, 1); } }
static void md_fallback_ly(void) { static const char *L[] = {"lyceum/router", "lyceum/simple", "lyceum/complex", "lyceum/reasoning", "deepseek/deepseek-v4.1-flash", "deepseek/deepseek-v4-flash-0731", "z-ai/glm-5.3", "z-ai/glm-5.3-instant", "z-ai/glm-5.3-flash", "z-ai/glm-5.3-flash-instant", "moonshotai/kimi-k3.1-pro", "moonshotai/kimi-k2.7-code", "qwen/qwen3.8-2.4t-a95b", "qwen/qwen3.8-27b", "qwen/qwen3.8-27b-instant", "minimax/minimax-m3"};   /* los 16 de chat de su documentacion (noviembre de 2026) */ nmd = 0; for (unsigned i = 0; i < sizeof L / sizeof *L; i++) md_add(L[i], L[i], 131072, 0, 0, 0); }
static void md_fallback_or(void);
static void md_fallback(void) { if (is_ly()) { md_fallback_ly(); return; } md_fallback_or(); md_pinned(); }
static void md_fallback_or(void) { static const struct { const char *id, *name; long ctx; } F[] = {{"openrouter/free", "Free Models Router", 200000}, {"deepseek/deepseek-v4.1-flash", "DeepSeek: DeepSeek V4.1 Flash", 1048576}, {"deepseek/deepseek-r1:free", "DeepSeek: R1 (free)", 163840}, {"deepseek/deepseek-chat-v3-0324:free", "DeepSeek: DeepSeek V3 0324 (free)", 163840}, {"google/gemma-4-31b-it:free", "Google: Gemma 4 31B (free)", 262144}, {"meta-llama/llama-3.3-70b-instruct:free", "Meta: Llama 3.3 70B Instruct (free)", 131072}, {"qwen/qwen3-235b-a22b:free", "Qwen: Qwen3 235B A22B (free)", 131072}, {"mistralai/mistral-small-3.2-24b-instruct:free", "Mistral: Mistral Small 3.2 24B (free)", 131072}};
    nmd = 0; for (unsigned i = 0; i < sizeof F / sizeof *F; i++) md_add(F[i].id, F[i].name, F[i].ctx, i == 1 ? 0.00000005 : 0, i == 1 ? 0.0000002 : 0, i != 1); qsort(MD, nmd, sizeof *MD, md_cmp); }
static void md_save(void) { sb_t b = {0}; sb_f(&b, "LUMENM1\n%ld\n%s\n", md_at, C.api); for (int i = 0; i < nmd; i++) sb_f(&b, "%s\t%s\t%ld\t%.12f\t%.12f\t%d\n", MD[i].id, MD[i].name, MD[i].ctx, MD[i].pin, MD[i].pout, MD[i].fr); char p[96]; path_of(p, 96, "LUMEN-MODELOS.TXT"); z_writefile(p, b.p, b.n); free(b.p); }
static void md_load(void) { char p[96]; path_of(p, 96, "LUMEN-MODELOS.TXT"); char *t = z_readfile(p, 0); md_fallback(); md_at = 0; if (!t) return; if (strncmp(t, "LUMENM1\n", 8)) { free(t); return; }
    char *l = t + 8, *e = strchr(l, '\n'); if (!e) { free(t); return; } long at = atol(l); l = e + 1; e = strchr(l, '\n'); if (!e) { free(t); return; } *e = 0; if (strcmp(l, C.api)) { free(t); return; } l = e + 1; int n0 = nmd; nmd = 0;
    for (; l && *l; l = e ? e + 1 : 0) { e = strchr(l, '\n'); if (e) *e = 0; char *f[6]; int k = 0; f[k++] = l; for (char *q = l; *q && k < 6; q++) if (*q == '\t') { *q = 0; f[k++] = q + 1; } if (k == 6) md_add(f[0], f[1], atol(f[2]), num(f[3], 0), num(f[4], 0), atoi(f[5])); }
    if (!nmd) { nmd = n0; md_fallback(); } else { md_at = at; md_pinned(); } free(t); qsort(MD, nmd, sizeof *MD, md_cmp); }
static model_t *md_find(const char *id) { for (int i = 0; i < nmd; i++) if (!strcmp(MD[i].id, id)) return &MD[i]; return 0; }
static const char *model_name(const char *id) { model_t *m = md_find(id); if (m) return m->name; const char *s = strrchr(id, '/'); return s ? s + 1 : id; }
static void fmt_ctx(long c, char *o, int cap) { if (c >= 1000000) snprintf(o, cap, "%ldM", (c + 500000) / 1000000); else if (c >= 1000) snprintf(o, cap, "%ldK", (c + 500) / 1000); else snprintf(o, cap, "%ld", c); }
static void fmt_price(const model_t *m, char *o, int cap) { if (m->fr) { strncpy0(o, tr("Gratis"), cap); return; } if (m->pin < 0 || m->pout < 0) { strncpy0(o, tr("precio variable"), cap); return; } snprintf(o, cap, "$%.2f / $%.2f M", m->pin * 1e6, m->pout * 1e6); }

/* ---------- red ---------- */
static int busy, quit, stop_req; static u64 last_draw;
void draw(void); static void handle(zevent *e);
static int pump(void *ctx) { zevent e; while (z_event(&e, 0)) handle(&e); if (z_ms() - last_draw > 300) draw(); return stop_req || quit; }
static void hdrs(sb_t *h) { if (C.key[0]) sb_f(h, "Authorization: Bearer %s\r\n", C.key); if (is_or()) sb_s(h, "HTTP-Referer: https://tonyfluxa.top\r\nX-Title: LUMEN Chat (ZunzunOS)\r\n"); }
static void api_url(char *o, int cap, const char *path) { snprintf(o, cap, "%s", C.api); int l = strlen(o); while (l && o[l - 1] == '/') o[--l] = 0; strncpy0(o + l, path, cap - l); }
static void net_err(int e, zhttp_info *inf, char *o, int cap) { if (e == -125) strncpy0(o, tr("Cancelado"), cap); else if (e == -110) strncpy0(o, tr("El servidor tardó demasiado en responder"), cap); else if (e == -1004 && inf->err[0]) snprintf(o, cap, "%s (%s)", z_http_err(e), inf->err); else if (e == -111 || e == -2 || e == -5) snprintf(o, cap, "%s (%s)", tr("No hay conexión con el servidor de la API: revisa la red"), z_http_err(e)); else snprintf(o, cap, "%s", z_http_err(e)); }
static void api_msg(char *body, char *o, int cap) { o[0] = 0; if (!body) return; jd_t d; int r = jparse(&d, body); int er = jget(&d, r, "error"); const char *m = js(&d, jget(&d, er, "message")); if (!m) m = js(&d, er); if (!m) m = js(&d, jget(&d, r, "message")); if (m) dnorm_str(m, o, cap); jfree(&d); }
static void err_text(int st, const char *am, char *o, int cap) { const char *t;
    if (st == 401 || st == 403) t = strcasestr(am, "management") ? "Esa es una clave de gestión: crea una clave normal en openrouter.ai/keys" : "OpenRouter no acepta esta API key para chatear. En Ajustes pulsa Probar de nuevo para ver la causa."; else if (st == 402) t = "La cuenta no tiene saldo para este modelo. Elige un modelo gratuito o añade crédito en OpenRouter.";
    else if (st == 429) t = "Demasiadas peticiones o el modelo gratuito está saturado. Espera un momento o cambia de modelo."; else if (st == 404) t = "Este modelo no está disponible ahora. Elige otro en Modelo.";
    else if (st == 400 && (strcasestr(am, "model") || strcasestr(am, "context") || strcasestr(am, "token"))) t = "El modelo no acepta esta petición (modelo o tamaño del contexto)."; else if (st >= 500) t = "El servidor de la API tiene un problema. Prueba de nuevo en un momento."; else t = "No se pudo obtener respuesta.";
    if (am[0]) snprintf(o, cap, "%s (HTTP %d: %s)", tr(t), st, am); else snprintf(o, cap, "%s (HTTP %d)", tr(t), st); }
static void fetch_models(void) { if (busy) return; busy = 1; md_loading = 1; strcpy(msg, tr("Descargando la lista de modelos...")); draw(); char u[260]; api_url(u, 260, "/models"); sb_t h = {0}; sb_s(&h, ""); hdrs(&h);
    zhttp_opt o = {h.p, 0, pump, 0, 60000}; zhttp_info inf; char *body = 0; long bl = 0; int e = z_http_ex("GET", u, 0, 0, 0, &o, &body, &bl, &inf); free(h.p); busy = 0; md_loading = 0; stop_req = 0;
    if (e) { char t[200]; net_err(e, &inf, t, 200); snprintf(msg, 300, "%s: %s", tr("No se pudo descargar la lista de modelos"), t); free(body); return; }
    if (inf.status != 200) { char am[160]; api_msg(body, am, 160); snprintf(msg, 300, "%s: HTTP %d %s", tr("No se pudo descargar la lista de modelos"), inf.status, am); free(body); return; }
    jd_t d; int r = jparse(&d, body); int a = jget(&d, r, "data"); if (a < 0 && r >= 0 && d.n[r].t == 2) a = r; int n = 0;
    if (a >= 0) { nmd = 0; for (int c = d.n[a].ch; c >= 0; c = d.n[c].nx) { const char *id = js(&d, jget(&d, c, "id")); if (!id || z_ends(id, ":batch")) continue; int arch = jget(&d, c, "architecture"), om = jget(&d, arch, "output_modalities");
            if (om >= 0) { int text = 0; for (int k = d.n[om].ch; k >= 0; k = d.n[k].nx) if (js(&d, k) && !strcmp(js(&d, k), "text")) text = 1; if (!text) continue; }
            int pr = jget(&d, c, "pricing"); double pin = jn(&d, jget(&d, pr, "prompt")), pout = jn(&d, jget(&d, pr, "completion")); long ctx = (long)jn(&d, jget(&d, c, "context_length")); if (!ctx) ctx = (long)jn(&d, jget(&d, jget(&d, c, "top_provider"), "context_length"));
            if (strstr(id, "embedding")) continue; md_add(id, js(&d, jget(&d, c, "name")), ctx, pin, pout, !is_ly() && (pr < 0 || (pin == 0 && pout == 0))); n++; } }
    jfree(&d); free(body); if (!n) { md_fallback(); snprintf(msg, 300, "%s", tr("El servidor no devolvió modelos; se muestra la lista básica")); return; } md_pinned();
    qsort(MD, nmd, sizeof *MD, md_cmp); md_at = now_num(); md_save(); int fr = 0; for (int i = 0; i < nmd; i++) fr += MD[i].fr; snprintf(msg, 300, tr("%d modelos, %d gratuitos"), nmd, fr); char lg[80]; snprintf(lg, 80, "[prueba] lumen modelos %d gratis %d", nmd, fr); z_log(lg); }

/* comprobar la clave: /key dice si OpenRouter la conoce, pero eso no basta (las claves de gestion, desactivadas o
   caducadas pasan /key y el chat responde 401 "User not found"); por eso siempre se hace ademas una peticion
   real de 1 token, con el modelo gratuito en OpenRouter para no gastar saldo */
static void probe(int *st, char *am, int cap) { char u[260]; api_url(u, 260, "/chat/completions"); sb_t b = {0}; sb_s(&b, "{\"model\":"); jesc(&b, is_or() ? DEF_MODEL : C.model); sb_s(&b, ",\"messages\":[{\"role\":\"user\",\"content\":\"ping\"}],\"max_tokens\":1,\"stream\":false}");
    sb_t h = {0}; sb_s(&h, ""); hdrs(&h); zhttp_opt o = {h.p, 0, pump, 0, 40000}; zhttp_info inf; char *body = 0; long bl; int e = z_http_ex("POST", u, b.p, b.n, "application/json", &o, &body, &bl, &inf); free(h.p); free(b.p); *st = e ? 0 : inf.status; am[0] = 0; if (e) net_err(e, &inf, am, cap); else api_msg(body, am, cap); free(body); }
static int jtrue(jd_t *d, int o, const char *k) { int i = jget(d, o, k); return i >= 0 && d->n[i].t == 5; }
static void check_key(void) { if (busy) return; clean_key(C.key); if (!C.key[0]) { kst = 0; kdet[0] = 0; return; } busy = 1; kst = 1; kdet[0] = 0; strcpy(msg, tr("Comprobando la clave...")); draw();
    char u[260]; api_url(u, 260, "/key"); sb_t h = {0}; sb_s(&h, ""); hdrs(&h); zhttp_opt o = {h.p, 0, pump, 0, 30000}; zhttp_info inf; char *body = 0; long bl; int e = z_http_ex("GET", u, 0, 0, 0, &o, &body, &bl, &inf); free(h.p);
    const char *shape = is_or() && strncmp(C.key, "sk-or-", 6) ? tr("Las claves de OpenRouter empiezan por sk-or-v1-.") : is_ly() && strncmp(C.key, "lk_", 3) ? tr("Las claves de Lyceum empiezan por lk_.") : "";
    char l[64]; snprintf(l, 64, "sk-or-...%s", C.key + MAX(0, (int)strlen(C.key) - 4)); int ps = -1, mg = 0;
    if (e) { char t[200]; net_err(e, &inf, t, 200); kst = e == -125 ? 0 : 4; snprintf(kdet, 300, "%s %s", t, shape); }
    else if (inf.status == 200) { jd_t d; int r = jparse(&d, body); int dd = jget(&d, r, "data"); const char *lb = js(&d, jget(&d, dd, "label")); double us = jn(&d, jget(&d, dd, "usage")); int lim = jget(&d, dd, "limit"); kst = 2;
        if (lb) dnorm_str(lb, l, 40); mg = jtrue(&d, dd, "is_management_key") || jtrue(&d, dd, "is_provisioning_key");
        if (lim >= 0 && d.n[lim].t == 4) snprintf(kdet, 300, tr("Clave %s. Gastado: $%.4f de $%.2f."), l, us, d.n[lim].n); else snprintf(kdet, 300, tr("Clave %s. Gastado: $%.4f (sin límite)."), l, us); jfree(&d);
        if (mg) { kst = 3; snprintf(kdet, 300, tr("La clave %s es de gestión: sirve para administrar claves, no para chatear. Crea una clave normal en openrouter.ai/keys."), l); }
        else { char pm[160]; probe(&ps, pm, 160);
            if (ps == 401 || ps == 403) { kst = 3; snprintf(kdet, 300, tr("OpenRouter reconoce la clave %s pero no deja chatear con ella (HTTP %d: %s). Mira en openrouter.ai/keys que esté activa y sin caducar, o crea otra."), l, ps, pm); } } }
    else { char am[160]; api_msg(body, am, 160); int st = inf.status; char pm[160]; probe(&ps, pm, 160);
        if (ps && ps != 401 && ps != 403) { kst = 2; snprintf(kdet, 300, "%s", tr("La clave funciona (comprobada con una petición real).")); }
        else if (!ps) { kst = 4; snprintf(kdet, 300, "%s %s", pm, shape); } else { kst = 3; char t[260]; err_text(ps ? ps : st, pm[0] ? pm : am, t, 260); snprintf(kdet, 300, "%s %s", t, shape); } }
    free(body); busy = 0; stop_req = 0; strcpy(msg, kst == 2 ? tr("Clave válida") : kst == 3 ? tr("Clave no válida") : kst == 4 ? tr("Clave sin verificar: no se pudo contactar con el servidor") : "");
    char lg[160]; snprintf(lg, 160, "[prueba] lumen clave %d: /key %d, chat %d, gestion %d, %s (largo %d)", kst, e ? e : inf.status, ps, mg, l, (int)strlen(C.key)); z_log(lg); }

/* ---------- enviar y recibir ---------- */
typedef struct { msg_t *m; sb_t line, raw; int done, got, sse; char err[240]; } sse_t;
static msg_t *SM; static u64 t_send;
static void sse_json(sse_t *x, char *p) { jd_t d; int r = jparse(&d, p); if (r < 0) { jfree(&d); return; } msg_t *m = x->m;
    int er = jget(&d, r, "error"); if (er >= 0) { const char *em = js(&d, jget(&d, er, "message")); int code = (int)jn(&d, jget(&d, er, "code")); char am[160] = ""; if (em) dnorm_str(em, am, 160); err_text(code ? code : 500, am, x->err, 240); }
    const char *mo = js(&d, jget(&d, r, "model")); if (mo && !m->model[0]) strncpy0(m->model, mo, 128); else if (mo && strcmp(m->model, mo) && !strcmp(m->model, "openrouter/free")) strncpy0(m->model, mo, 128);
    int ch = jat(&d, jget(&d, r, "choices"), 0); int dl = jget(&d, ch, "delta"); if (dl < 0) dl = jget(&d, ch, "message");
    const char *c = js(&d, jget(&d, dl, "content")); if (c && *c) { sb_s(&m->t, c); x->got = 1; }
    const char *rs = js(&d, jget(&d, dl, "reasoning")); if (!rs) rs = js(&d, jget(&d, dl, "reasoning_content")); if (rs && *rs) { sb_s(&m->r, rs); x->got = 1; }
    int us = jget(&d, r, "usage"); if (us >= 0) { m->tin = (int)jn(&d, jget(&d, us, "prompt_tokens")); m->tout = (int)jn(&d, jget(&d, us, "completion_tokens")); }
    m->dirty = 1; jfree(&d); }
static int sse_cb(const char *d, long n, void *ctx) { sse_t *x = ctx; sb_add(&x->line, d, n); if (!x->sse && x->raw.n < (4L << 20)) sb_add(&x->raw, d, n);
    for (;;) { char *e = memchr_(x->line.p, '\n', x->line.n); if (!e) break; *e = 0; char *l = x->line.p; long ll = e - l; if (ll && l[ll - 1] == '\r') l[--ll] = 0;
        if (l[0] == ':' || !strncmp(l, "data:", 5) || !strncmp(l, "event:", 6)) x->sse = 1;
        if (!strncmp(l, "data:", 5)) { char *p = l + 5; while (*p == ' ') p++; if (!strcmp(p, "[DONE]")) x->done = 1; else if (*p) sse_json(x, p); }
        long rest = x->line.n - (e + 1 - x->line.p); memmove(x->line.p, e + 1, rest); x->line.n = rest; x->line.p[rest] = 0; }
    if (z_ms() - last_draw > 60) draw(); return stop_req || quit; }
/* ---------- archivos adjuntos (RAG): se leen una vez, se trocean y en cada pregunta van los fragmentos que
   mas tienen que ver con ella, delante de la pregunta ---------- */
static zrdoc RD[24]; static int nrd; static char rd_err[24][80];
static char pimg[1000]; static int npimg;   /* imagenes para el proximo mensaje */
static int IMG_OK(const char *n) { return z_ends(n, ".png") || z_ends(n, ".jpg") || z_ends(n, ".jpeg") || z_ends(n, ".gif") || z_ends(n, ".bmp"); }
static void img_add(const char *path) { if (npimg >= 4) { strcpy(msg, tr("Como mucho 4 imágenes por mensaje")); return; } u32 *px; int w, h; if (z_img_load(path, &px, &w, &h)) { snprintf(msg, 300, "%s: %s", tr("No se pudo abrir la imagen"), path); return; } free(px);
    if (strlen(pimg) + strlen(path) + 2 >= sizeof pimg) return; strcat(pimg, path); strcat(pimg, "\n"); npimg++; const char *b = strrchr(path, '/'); snprintf(msg, 300, tr("Imagen %s (%dx%d) lista para el próximo mensaje"), b ? b + 1 : path, w, h); char lg[300]; snprintf(lg, 300, "[prueba] lumen imagen %s %dx%d", b ? b + 1 : path, w, h); z_log(lg); }
/* imagen como JPEG en base64 (como mucho 1280 px de lado), con una pequena memoria para no repetir el trabajo */
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static struct { char path[200]; char *b64; } IC[6]; static int icn;
static const char *img_b64(const char *path) { for (int i = 0; i < 6; i++) if (IC[i].b64 && !strcmp(IC[i].path, path)) return IC[i].b64;
    u32 *px; int w, h; int le = z_img_load(path, &px, &w, &h); if (le) { char lg[300]; snprintf(lg, 300, "[prueba] lumen imagen no se pudo leer %s: %d", path, le); z_log(lg); return 0; } int nw = w, nh = h; if (w > 1280 || h > 1280) { if (w >= h) { nw = 1280; nh = (int)((long)h * 1280 / w); } else { nh = 1280; nw = (int)((long)w * 1280 / h); } }
    if (nw != w || nh != h) { u32 *q = malloc((long)nw * nh * 4); for (int y = 0; y < nh; y++) for (int x = 0; x < nw; x++) q[(long)y * nw + x] = px[(long)(y * h / nh) * w + x * w / nw]; free(px); px = q; }
    u8 *j; long n = z_jpeg_encode(px, nw, nh, 85, &j); free(px); if (n <= 0) { z_log("[prueba] lumen imagen: fallo el JPEG"); return 0; } char *o = malloc(n * 4 / 3 + 40), *w2 = o; strcpy(w2, "data:image/jpeg;base64,"); w2 += strlen(w2);
    for (long i = 0; i < n; i += 3) { u32 v = j[i] << 16 | (i + 1 < n ? j[i + 1] << 8 : 0) | (i + 2 < n ? j[i + 2] : 0); *w2++ = B64[v >> 18 & 63]; *w2++ = B64[v >> 12 & 63]; *w2++ = i + 1 < n ? B64[v >> 6 & 63] : '='; *w2++ = i + 2 < n ? B64[v & 63] : '='; } *w2 = 0; free(j);
    free(IC[icn].b64); strncpy0(IC[icn].path, path, 200); IC[icn].b64 = o; icn = (icn + 1) % 6; return o; }
static int RAG_OK(const char *n) { static const char *E[] = {".txt", ".md", ".csv", ".html", ".htm", ".rtf", ".docx", ".odt", ".xlsx", ".pdf", ".json", ".xml", ".log", ".ini", ".c", ".h", ".py", ".js", ".css", ".tsv"}; for (unsigned i = 0; i < sizeof E / sizeof *E; i++) if (z_ends(n, E[i])) return 1; return 0; }
static zrdoc *rag_doc(const char *path, char *err, int ecap) { err[0] = 0; for (int i = 0; i < nrd; i++) if (!strcmp(RD[i].path, path)) { strncpy0(err, rd_err[i], ecap); return RD[i].txt ? &RD[i] : 0; }
    if (nrd == 24) { zr_free(&RD[0]); memmove(RD, RD + 1, 23 * sizeof *RD); memmove(rd_err, rd_err + 1, 23 * sizeof *rd_err); nrd--; }
    zrdoc *d = &RD[nrd]; memset(d, 0, sizeof *d); strncpy0(d->path, path, 200); const char *b = strrchr(path, '/'); strncpy0(d->name, b ? b + 1 : path, 64);
    snprintf(msg, 300, "%s %s...", tr("Leyendo"), d->name); draw(); d->txt = zr_read(path, &d->n, rd_err[nrd], 80, pump, 0); stop_req = 0; if (d->txt) zr_chunk(d); strncpy0(err, rd_err[nrd], ecap); nrd++;
    char lg[300]; snprintf(lg, 300, "[prueba] lumen adjunto %s: %ld letras, %d fragmentos%s%s", d->name, d->n, d->nch, err[0] ? ", error: " : "", err); z_log(lg); return d->txt ? d : 0; }
static void rag_add(const char *path) { conv_t *c = CC(); if (!c) c = conv_new(); for (int i = 0; i < c->natt; i++) if (!strcmp(c->att[i], path)) { strcpy(msg, tr("Ese archivo ya está adjunto")); return; }
    if (c->natt == MAXATT) { snprintf(msg, 300, tr("Como mucho %d archivos por chat"), MAXATT); return; } char er[80]; zrdoc *d = rag_doc(path, er, 80);
    if (!d) { snprintf(msg, 300, "%s: %s", tr("No se pudo usar el archivo"), er); return; } if (!d->n) { strcpy(msg, tr("El archivo está vacío")); return; }
    strncpy0(c->att[c->natt++], path, 200); if (c->n) conv_save(c); snprintf(msg, 300, tr("Adjunto: %s (%d fragmentos)"), d->name, d->nch); }
static void rag_del(int i) { conv_t *c = CC(); if (!c || i < 0 || i >= c->natt) return; memmove(c->att[i], c->att[i + 1], (c->natt - i - 1) * sizeof c->att[0]); c->natt--; if (c->n) conv_save(c); strcpy(msg, tr("Archivo quitado del chat")); }
/* texto que va delante de la ultima pregunta; budget en letras segun el contexto del modelo */
static sb_t RAGT; static int rag_n, rag_docs;
static void rag_build(conv_t *c, const char *q) { free(RAGT.p); RAGT = (sb_t){0}; rag_n = rag_docs = 0; if (!c->natt) return; zrdoc tmp[MAXATT]; int nd = 0;
    for (int i = 0; i < c->natt; i++) { char er[80]; zrdoc *d = rag_doc(c->att[i], er, 80); if (d) tmp[nd++] = *d; }
    if (!nd) return; model_t *md = md_find(C.model); long budget = md && md->ctx ? MIN(24000L, md->ctx * 4 / 3) : 16000; budget = MAX(budget, 3000L);
    sb_t f = {0}; sb_s(&f, ""); rag_n = zr_search(tmp, nd, q, budget, &f, &rag_docs);
    for (int i = 0; i < nd; i++) for (int k = 0; k < nrd; k++) if (!strcmp(RD[k].path, tmp[i].path)) { RD[k].co = tmp[i].co; RD[k].cl = tmp[i].cl; RD[k].nch = tmp[i].nch; }
    sb_s(&RAGT, tr("Fragmentos de archivos que he adjuntado. Úsalos para responder y cita el archivo entre corchetes; si la respuesta no está en ellos, dilo.")); sb_s(&RAGT, "\n"); sb_add(&RAGT, f.p, f.n); free(f.p);
    char lg[160]; snprintf(lg, 160, "[prueba] lumen rag %d fragmentos de %d archivos, %ld letras", rag_n, rag_docs, RAGT.n); z_log(lg); }
static void build_req(conv_t *c, sb_t *b) { sb_s(b, "{\"model\":"); jesc(b, C.model); sb_s(b, ",\"messages\":["); const char *sys = c->sys && c->sys[0] ? c->sys : C.sys; int first = 1;
    if (sys[0]) { sb_s(b, "{\"role\":\"system\",\"content\":"); jesc(b, sys); sb_s(b, "}"); first = 0; }
    model_t *md = md_find(C.model); long budget = md && md->ctx ? md->ctx * 6 / 10 * 4 : 480000, sz = 0; int start = 0; /* ~4 letras por token: se quitan los mensajes mas antiguos si no caben */
    for (int i = c->n - 1; i >= 0; i--) { if (c->m[i]->role == 2 || c->m[i] == SM) continue; sz += c->m[i]->t.n; if (sz > budget && c->n - i > 4) { start = i + 1; break; } }
    int lastu = -1; for (int i = c->n - 1; i >= 0; i--) if (c->m[i]->role == 0) { lastu = i; break; }
    for (int i = start; i < c->n; i++) { msg_t *m = c->m[i]; if (m->role == 2 || m == SM || !m->t.n) continue; if (!first) sb_s(b, ","); first = 0; sb_s(b, m->role ? "{\"role\":\"assistant\",\"content\":" : "{\"role\":\"user\",\"content\":");
        sb_t q = {0}; if (i == lastu && RAGT.n) { sb_add(&q, RAGT.p, RAGT.n); sb_s(&q, "\n---\n"); } sb_s(&q, m->t.p);
        if (m->role == 0 && m->img && m->img[0]) { sb_s(b, "[{\"type\":\"text\",\"text\":"); jesc(b, q.p); sb_s(b, "}"); int ni = 0;
            for (const char *z = m->img; *z; ) { const char *e = strchr(z, '\n'); char pth[200]; int n = e ? e - z : (int)strlen(z); snprintf(pth, 200, "%.*s", n, z); const char *d = img_b64(pth); if (d) { sb_s(b, ",{\"type\":\"image_url\",\"image_url\":{\"url\":\""); sb_s(b, d); sb_s(b, "\"}}"); ni++; } z = e ? e + 1 : z + n; }
            sb_s(b, "]"); if (i == lastu) { char lg[80]; snprintf(lg, 80, "[prueba] lumen enviadas %d imagenes", ni); z_log(lg); } }
        else jesc(b, q.p); free(q.p); sb_s(b, "}"); }
    sb_f(b, "],\"temperature\":%d.%d,\"stream\":%s", C.temp / 10, C.temp % 10, C.stream ? "true" : "false"); if (C.maxtok > 0) sb_f(b, ",\"max_tokens\":%d", C.maxtok);
    if (C.stream) sb_s(b, ",\"stream_options\":{\"include_usage\":true}"); if (is_or()) sb_s(b, ",\"usage\":{\"include\":true}"); sb_s(b, "}"); }
static int mode, stick = 1, scy, tot_h, view_h; static te_t CT;
static void ask(void) { conv_t *c = CC(); if (!c || busy) return; while (c->n && c->m[c->n - 1]->role == 2) msg_del(c, c->n - 1); if (!c->n || c->m[c->n - 1]->role != 0) return;
    msg_t *m = msg_add(c, 1); strncpy0(m->model, C.model, 128); strncpy0(c->model, C.model, 128); SM = m; busy = 1; stop_req = 0; stick = 1; t_send = z_ms();
    { free(RAGT.p); RAGT = (sb_t){0}; rag_n = 0; int lu = -1; for (int i = c->n - 1; i >= 0; i--) if (c->m[i]->role == 0) { lu = i; break; } if (c->natt && lu >= 0) rag_build(c, c->m[lu]->t.p); }
    char u[260]; api_url(u, 260, "/chat/completions"); sb_t b = {0}; build_req(c, &b); sb_t h = {0}; sb_s(&h, ""); hdrs(&h);
    sse_t x = {0}; x.m = m; zhttp_opt o = {h.p, C.stream ? sse_cb : 0, pump, &x, 120000}; zhttp_info inf; char *body = 0; long bl = 0;
    if (rag_n) snprintf(msg, 300, tr("Preguntando a %s con %d fragmentos de %d archivos...  (Esc para detener)"), model_name(C.model), rag_n, rag_docs); else snprintf(msg, 300, "%s %s...  (%s)", tr("Preguntando a"), model_name(C.model), tr("Esc para detener")); draw();
    int e = z_http_ex("POST", u, b.p, b.n, "application/json", &o, &body, &bl, &inf); m->ms = z_ms() - t_send; char er[300] = "";
    if (e == -125 || stop_req) m->stopped = 1; else if (e) net_err(e, &inf, er, 300); else if (inf.status < 200 || inf.status > 299) { char am[160]; api_msg(body, am, 160); err_text(inf.status, am, er, 300); if (inf.status == 401 || inf.status == 403) { kst = 3; snprintf(kdet, 300, "%s", er); } char lg[200]; snprintf(lg, 200, "[prueba] lumen chat HTTP %d: %s (clave de %d letras)", inf.status, am, (int)strlen(C.key)); z_log(lg); }
    else if (!C.stream && body) { sse_json(&x, body); }
    if (x.err[0] && !er[0]) strcpy(er, x.err); free(body); free(b.p); free(h.p); free(x.line.p); SM = 0;
    if (C.stream && !x.sse && x.raw.n && !er[0]) sse_json(&x, x.raw.p); /* servidores que ignoran "stream" y responden de una vez */
    free(x.raw.p); int empty = !m->t.n && !m->r.n, stopped = m->stopped; if (er[0] || empty) { if (empty) msg_del(c, c->n - 1); if (er[0] || !stopped) { msg_t *x2 = msg_add(c, 2); sb_s(&x2->t, er[0] ? er : tr("El modelo no devolvió texto. Prueba de nuevo o elige otro modelo.")); } }
    m = c->n && c->m[c->n - 1]->role == 1 ? c->m[c->n - 1] : 0; if (m) m->dirty = 1; busy = 0; stop_req = 0; c->upd = now_num(); conv_title(c); conv_sort(); int se = conv_save(c);
    if (er[0]) snprintf(msg, 300, "%s", er); else if (!m) strcpy(msg, tr("Detenido")); else snprintf(msg, 300, "%s %s · %ld.%ld s%s", tr("Respuesta de"), model_name(m->model), m->ms / 1000, m->ms % 1000 / 100, m->stopped ? tr(" (detenida)") : "");
    if (se) snprintf(msg + strlen(msg), 300 - strlen(msg), "  ·  %s: %s", tr("no se pudo guardar el chat"), z_errstr(se));
    char lg[400]; snprintf(lg, 400, "[prueba] lumen respuesta %ld letras, razonamiento %ld, error: %s", m ? m->t.n : 0L, m ? m->r.n : 0L, er[0] ? er : "no"); z_log(lg); }

/* ---------- Markdown a lineas de pantalla. \1 cambia negrita y \2 codigo en linea ---------- */
static void addl(msg_t *m, const char *pre, const char *s, long n, int kind, int st, int blk) { if (m->nL % 64 == 0) m->L = realloc(m->L, (m->nL + 64) * sizeof *m->L); int pl = pre ? strlen(pre) : 0; char *l = malloc(pl + n + 1); if (pl) memcpy(l, pre, pl); if (n) memcpy(l + pl, s, n); l[pl + n] = 0; m->L[m->nL++] = (dl_t){l, kind, blk, st}; }
static int vlen(const char *s, long n) { int c = 0; for (long i = 0; i < n && s[i]; ) { u8 ch = s[i]; if (ch == 1 || ch == 2) { i++; continue; } i += u8len(ch); c++; } return c; }
static void wrapadd(msg_t *m, const char *s, int cols, int kind, const char *pre1, int ind, int blk) { char sp[48]; ind = MIN(ind, 40); memset(sp, ' ', ind); sp[ind] = 0; int st = 0, first = 1; const char *p = s;
    if (!*p) { addl(m, pre1, "", 0, kind, 0, blk); return; }
    while (*p) { int avail = MAX(8, cols - (first ? vlen(pre1 ? pre1 : "", 999) : ind)); const char *q = p, *brk = 0; int c = 0;
        while (*q && c < avail) { if (*q == 1 || *q == 2) { q++; continue; } if (*q == ' ') brk = q; q += u8len(*q); c++; }
        const char *end, *nx; if (!*q) { end = q; nx = q; } else if (*q == ' ') { end = q; nx = q + 1; } else if (brk && brk > p) { end = brk; nx = brk + 1; } else { end = q; nx = q; }
        addl(m, first ? pre1 : sp, p, end - p, kind, st, blk); for (const char *t = p; t < nx; t++) if (*t == 1) st ^= 1; else if (*t == 2) st ^= 2; p = nx; first = 0; while (*p == ' ' && p[1] == ' ') p++; } }
static void codeadd(msg_t *m, const char *s, long n, int cols, int blk) { if (!n) { addl(m, 0, "", 0, K_CODE, 0, blk); return; } const char *p = s, *e = s + n; while (p < e) { const char *q = p; int c = 0; while (q < e && c < cols) { q += u8len(*q); c++; } if (q > e) q = e; addl(m, 0, p, q - p, K_CODE, 0, blk); p = q; } }
/* negrita, codigo, cursiva, tachado, enlaces e imagenes dentro de una linea */
static void inl(sb_t *o, const char *s) { int bold = 0, code = 0;
    for (const char *p = s; *p; ) {
        if (!code && *p == '\\' && p[1] && strchr("\\`*_{}[]()#+-.!|>~", p[1])) { sb_add(o, p + 1, 1); p += 2; continue; }
        if (*p == '`') { const char *e = strchr(p + 1, '`'); if (code || e) { sb_s(o, "\2"); code ^= 1; p++; continue; } }
        if (code) { sb_add(o, p, 1); p++; continue; }
        if ((p[0] == '*' && p[1] == '*') || (p[0] == '_' && p[1] == '_' && (p == s || !isalnum(p[-1])))) { char mk[3] = {p[0], p[1], 0}; if (bold || strstr(p + 2, mk)) { sb_s(o, "\1"); bold ^= 1; p += 2; continue; } }
        if (p[0] == '~' && p[1] == '~') { p += 2; continue; }
        if (*p == '*' && p[1] != '*' && ((p > s && p[-1] != ' ' && p[-1] != '(' && (p[1] == 0 || p[1] == ' ' || strchr(".,;:!?)", p[1]))) || ((p == s || p[-1] == ' ' || p[-1] == '(') && p[1] && p[1] != ' ' && strchr(p + 1, '*')))) { p++; continue; }
        if (*p == '!' && p[1] == '[') { const char *e = strstr(p, "]("), *f = e ? strchr(e, ')') : 0; if (e && f) { sb_s(o, "["); sb_s(o, tr("imagen")); if (e > p + 2) { sb_s(o, ": "); sb_add(o, p + 2, e - p - 2); } sb_s(o, "]"); p = f + 1; continue; } }
        if (*p == '[') { const char *e = strstr(p, "]("), *f = e ? strchr(e + 2, ')') : 0, *nl = strchr(p + 1, '['); if (e && f && (!nl || nl > e)) { sb_add(o, p + 1, e - p - 1); if (f - e - 2 > 0 && strncmp(e + 2, "#", 1)) { sb_s(o, " ("); sb_add(o, e + 2, f - e - 2); sb_s(o, ")"); } p = f + 1; continue; } }
        sb_add(o, p, 1); p++; }
    if (bold) sb_s(o, "\1"); if (code) sb_s(o, "\2"); if (!o->p) sb_add(o, "", 0); }
static int is_sep_row(const char *s) { int dash = 0; for (; *s; s++) { if (*s == '-') dash = 1; else if (*s != '|' && *s != ':' && *s != ' ') return 0; } return dash; }
static const char *trim_l(const char *s) { while (*s == ' ') s++; return s; }
static int next_is_sep(const char *p) { const char *e = strchr(p, '\n'); long n = e ? e - p : (long)strlen(p); char t[400]; if (n >= 400) return 0; memcpy(t, p, n); t[n] = 0; return *trim_l(t) == '|' && is_sep_row(t); }
#define TC 12
static int split_row(const char *l, char **cell) { int n = 0; const char *p = trim_l(l); if (*p == '|') p++; while (*p && n < TC) { const char *e = p; while (*e && *e != '|') { if (*e == '\\' && e[1]) e++; e++; } const char *a = p, *b = e; while (a < b && *a == ' ') a++; while (b > a && b[-1] == ' ') b--; if (!*e && a == b) break; sb_t c = {0}; char *raw = malloc(b - a + 1); memcpy(raw, a, b - a); raw[b - a] = 0; inl(&c, raw); free(raw); long k = 0; for (long i = 0; i < c.n; i++) if (c.p[i] != 1 && c.p[i] != 2) c.p[k++] = c.p[i]; c.p[k] = 0; cell[n++] = c.p; if (!*e) break; p = e + 1; } return n; }
static void boxline(sb_t *o, const int *w, int nc, const char *l, const char *mid, const char *r) { sb_s(o, l); for (int c = 0; c < nc; c++) { for (int k = 0; k < w[c] + 2; k++) sb_s(o, "\xE2\x94\x80"); sb_s(o, c + 1 < nc ? mid : r); } }
/* tabla: si cabe se dibuja con lineas; si no, cada fila en texto separado por | */
static const char *table(msg_t *m, const char *p, int cols) { char *rows[80][TC]; int nr = 0, nc = 0, rc[80], li = 0; const char *q = p;
    while (*q && nr < 80) { const char *e = strchr(q, '\n'); long n = e ? e - q : (long)strlen(q); char *ln = malloc(n + 1); memcpy(ln, q, n); ln[n] = 0; if (*trim_l(ln) != '|') { free(ln); break; }
        if (li != 1 || !is_sep_row(ln)) { rc[nr] = split_row(ln, rows[nr]); nc = MAX(nc, rc[nr]); nr++; } li++; free(ln); q = e ? e + 1 : q + n; }
    int w[TC] = {0}; for (int r = 0; r < nr; r++) for (int c = 0; c < rc[r]; c++) w[c] = MAX(w[c], vlen(rows[r][c], 9999)); int tot = 1; for (int c = 0; c < nc; c++) tot += w[c] + 3;
    if (tot <= cols && nc) { sb_t o = {0}; boxline(&o, w, nc, "\xE2\x94\x8C", "\xE2\x94\xAC", "\xE2\x94\x90"); addl(m, 0, o.p, o.n, K_TABLE, 0, 0); o.n = 0;
        for (int r = 0; r < nr; r++) { sb_s(&o, "\xE2\x94\x82"); for (int c = 0; c < nc; c++) { const char *t = c < rc[r] ? rows[r][c] : ""; sb_s(&o, " "); sb_s(&o, t); for (int k = vlen(t, 9999); k < w[c]; k++) sb_s(&o, " "); sb_s(&o, " \xE2\x94\x82"); } addl(m, 0, o.p, o.n, K_TABLE, r == 0, 0); o.n = 0;
            if (r == 0 && nr > 1) { boxline(&o, w, nc, "\xE2\x94\x9C", "\xE2\x94\xBC", "\xE2\x94\xA4"); addl(m, 0, o.p, o.n, K_TABLE, 0, 0); o.n = 0; } }
        boxline(&o, w, nc, "\xE2\x94\x94", "\xE2\x94\xB4", "\xE2\x94\x98"); addl(m, 0, o.p, o.n, K_TABLE, 0, 0); free(o.p); }
    else for (int r = 0; r < nr; r++) { sb_t o = {0}; if (!r) sb_s(&o, "\1"); for (int c = 0; c < rc[r]; c++) { if (c) sb_s(&o, " | "); sb_s(&o, rows[r][c]); } if (!r) sb_s(&o, "\1"); if (!o.p) sb_add(&o, "", 0); wrapadd(m, o.p, cols, K_TXT, 0, 2, 0); free(o.p); }
    for (int r = 0; r < nr; r++) for (int c = 0; c < rc[r]; c++) free(rows[r][c]); return q; }
static void md_build(msg_t *m, const char *t, int cols) { int code = 0, blk = 0;
    for (const char *p = t; *p; ) { const char *e = strchr(p, '\n'); long n = e ? e - p : (long)strlen(p); char *ln = malloc(n + 1); memcpy(ln, p, n); ln[n] = 0; const char *nx = e ? e + 1 : p + n; const char *s = trim_l(ln); int ind = s - ln;
        if (ind < 4 && (!strncmp(s, "```", 3) || !strncmp(s, "~~~", 3))) { if (!code) { code = 1; blk++; const char *lang = trim_l(s + 3); addl(m, 0, lang, strlen(lang), K_CODEH, 0, blk); } else { code = 0; addl(m, 0, "", 0, K_GAP, 0, 0); } }
        else if (code) codeadd(m, ln, n, cols - 2, blk);
        else if (*s == '|' && e && next_is_sep(e + 1)) { free(ln); p = table(m, p, cols); continue; }
        else if (!*s) { if (m->nL && m->L[m->nL - 1].s[0] && m->L[m->nL - 1].kind != K_WHO) addl(m, 0, "", 0, K_GAP, 0, 0); }
        else { int h = 0; while (s[h] == '#' && h < 7) h++; sb_t o = {0};
            if (h && h < 7 && s[h] == ' ') { inl(&o, trim_l(s + h)); wrapadd(m, o.p, cols, K_H, 0, 0, 0); }
            else if ((s[0] == '-' || s[0] == '*' || s[0] == '_') && is_sep_row(s) && strlen(s) >= 3 && (s[0] != '-' || !strchr(s, '|'))) addl(m, 0, "", 0, K_HR, 0, 0);
            else if ((s[0] == '-' || s[0] == '*' || s[0] == '+') && s[1] == ' ') { char pre[48]; int k = MIN(ind, 30); memset(pre, ' ', k); strcpy(pre + k, "\xE2\x80\xA2 "); inl(&o, trim_l(s + 2)); wrapadd(m, o.p, cols, K_TXT, pre, k + 2, 0); }
            else if (s[0] == '>') { const char *q = s; while (*q == '>' || *q == ' ') q++; inl(&o, q); wrapadd(m, o.p, cols - 2, K_QUOTE, 0, 0, 0); }
            else { int d = 0; while (isdigit(s[d])) d++; if (d && d < 4 && (s[d] == '.' || s[d] == ')') && s[d + 1] == ' ') { char pre[48]; int k = MIN(ind, 30); memset(pre, ' ', k); memcpy(pre + k, s, d + 2); pre[k + d + 2] = 0; inl(&o, trim_l(s + d + 2)); wrapadd(m, o.p, cols, K_TXT, pre, k + d + 2, 0); }
                else { inl(&o, s); char pre[48]; int k = MIN(ind, 30); memset(pre, ' ', k); pre[k] = 0; wrapadd(m, o.p, cols, K_TXT, pre, k, 0); } }
            free(o.p); }
        free(ln); p = nx; }
    while (m->nL && m->L[m->nL - 1].kind == K_GAP) { free(m->L[--m->nL].s); } }
static void plainadd(msg_t *m, const char *t, int cols, int kind) { for (const char *p = t; ; ) { const char *e = strchr(p, '\n'); long n = e ? e - p : (long)strlen(p); char *ln = malloc(n + 1); for (long i = 0; i < n; i++) ln[i] = p[i] == 1 || p[i] == 2 ? ' ' : p[i]; ln[n] = 0; wrapadd(m, ln, cols, kind, 0, 0, 0); free(ln); if (!e) break; p = e + 1; } }
static int words(const char *s) { int n = 0, in = 0; for (; *s; s++) { int w = *s != ' ' && *s != '\n'; if (w && !in) n++; in = w; } return n; }
static void build(msg_t *m, int cols, int last) { lines_free(m); m->cols = cols; m->dirty = 0; char h[200];
    if (m->role == 0) snprintf(h, 200, "%s", tr("Tú")); else if (m->role == 1) snprintf(h, 200, "LUMEN  \xC2\xB7  %s", model_name(m->model)); else snprintf(h, 200, "%s", tr("Error")); addl(m, 0, h, strlen(h), K_WHO, 0, 0);
    sb_t t = {0}; dnorm(&t, m->t.p, m->t.n);
    if (m->role == 1 && m->r.n) { int open = m->open || (m == SM && !m->t.n); snprintf(h, 200, "%s %s (%d %s)", open ? "\xE2\x96\xBC" : "\xE2\x96\xBA", tr("Razonamiento"), words(m->r.p), tr("palabras")); addl(m, 0, h, strlen(h), K_TOGGLE, 0, 0); if (open) { sb_t r = {0}; dnorm(&r, m->r.p, m->r.n); plainadd(m, r.p, cols - 2, K_REAS); free(r.p); } }
    if (m == SM && !m->t.n) addl(m, 0, "", 0, K_THINK, 0, 0);
    else if (m->role == 0) { plainadd(m, t.p, cols - 2, K_USER); for (const char *q = m->img; q && *q; ) { const char *e = strchr(q, '\n'); long n = e ? e - q : (long)strlen(q); const char *b = q; for (const char *z = q; z < q + n; z++) if (*z == '/') b = z + 1; char ln[200]; snprintf(ln, 200, "[%s] %.*s", tr("imagen"), (int)(q + n - b), b); addl(m, 0, ln, strlen(ln), K_USER, 0, 0); q = e ? e + 1 : q + n; } if (last && !busy) { addl(m, 0, "", 0, K_GAP, 0, 0); addl(m, 0, "", 0, K_RETRY, 0, 0); } } else if (m->role == 2) { plainadd(m, t.p, cols, K_ERR); if (last) addl(m, 0, "", 0, K_RETRY, 0, 0); } else md_build(m, t.p, cols);
    if (m->role == 1 && m != SM) { if (m->tin || m->tout) snprintf(h, 200, "\xE2\x86\x91 %d  \xE2\x86\x93 %d tokens  \xC2\xB7  %ld.%ld s%s", m->tin, m->tout, m->ms / 1000, m->ms % 1000 / 100, m->stopped ? tr("  \xC2\xB7  detenida") : ""); else snprintf(h, 200, "%ld.%ld s%s", m->ms / 1000, m->ms % 1000 / 100, m->stopped ? tr("  \xC2\xB7  detenida") : ""); addl(m, 0, h, strlen(h), K_META, 0, 0); }
    addl(m, 0, "", 0, K_GAP, 0, 0); free(t.p); }

/* ---------- pantalla ---------- */
#define CH_H (4 * LH + 22)
enum { B_NEW = 1, B_SIDE, B_MODEL, B_SET, B_EXP, B_PRINT, B_SEND, B_CLOSE, B_KPASTE, B_KSHOW, B_KSAVE, B_KTEST, B_KDEL, B_TMINUS, B_TPLUS, B_MMINUS, B_MPLUS, B_LIVE1, B_LIVE0, B_APIRESET, B_WIPE, B_ADDKEY, B_TABFREE, B_TABALL, B_REFRESH,
    F_KEY = 50, F_SYS, F_CSYS, F_API, F_SEARCH, B_USEID = 55, B_PROV = 56, B_FILES = 60, B_FUP, B_FPATH, B_FDOCS, B_FDESK, B_FSYS, B_FROOT, B_SUG = 70, B_MROW = 100, B_FDEL = 4000, B_FROW = 4100 };
typedef struct { int x, y, w, h, id; } hz_t; static hz_t HZ[200]; static int nhz, need, sb_top, sf = -1, scur, show_key, qcur; static char s_key[256], s_sys[1024], s_csys[1024], s_api[200];
static void hz(int x, int y, int w, int h, int id) { if (nhz < 200) HZ[nhz++] = (hz_t){x, y, w, h, id}; }
static void btn(int x, int y, int w, int h, const char *s, int st, int id) { ui_button(W, x, y, w, h, s, st); hz(x, y, w, h, id); }
static int bw(const char *s) { return g_textw(tr(s)) + 22; }
static void ctext(int cx, int y, const char *s, u32 c) { g_text(W, cx - g_textw(s) / 2, y, s, c); }
static int side_on(void) { return C.side && W->w >= 640; }
static int CX0(void) { return side_on() ? SW : 0; }
static const char *SUG[] = {"¿Cómo recupero archivos borrados de una memoria USB?", "Escribe un poema corto sobre el colibrí", "Dame ideas para organizar mi semana", "Explícame qué es un sistema operativo"};
static const char *KS[] = {"Sin clave", "Comprobando...", "Clave válida", "Clave no válida", "Sin verificar", "Clave guardada"};
static const u32 KC[] = {0x64748B, 0x64748B, 0x16A34A, 0xDC2626, 0xD97706, 0x0284C7};
static const int MAXT[] = {0, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768};
static void dirty_all(void) { for (int i = 0; i < ncv; i++) for (int k = 0; k < CV[i]->n; k++) CV[i]->m[k]->dirty = 1; }
/* parrafo con saltos de linea por anchura (para los paneles) */
static int para(int x, int y, int w, const char *s, u32 col) { int cols = MAX(10, w / 8), n = 0; const char *p = s; char ln[300];
    while (*p) { const char *q = p, *brk = 0; int c = 0; while (*q && *q != '\n' && c < cols) { if (*q == ' ') brk = q; q += u8len(*q); c++; } const char *end = q, *nx = q; if (*q == '\n') nx = q + 1; else if (*q && brk && brk > p) { end = brk; nx = brk + 1; }
        int k = MIN((int)(end - p), 299); memcpy(ln, p, k); ln[k] = 0; g_text(W, x, y + n * 18, ln, col); n++; p = nx; } return n; }
static void dtext(int x, int y, const char *s, int st, u32 fg) { char seg[512]; int k = 0, b = st & 1, cd = st & 2;
    for (const char *p = s; ; p++) { if (!*p || *p == 1 || *p == 2 || k >= 500) { seg[k] = 0; if (k) { int w = g_textw(seg); if (cd) g_rect(W, x - 1, y - 1, w + 2, 17, UI_PANEL); (b ? g_textb : g_text)(W, x, y, seg, cd ? UI_ACC : fg); x += w; } k = 0; if (!*p) break; if (*p == 1) b ^= 1; else if (*p == 2) cd ^= 1; else seg[k++] = *p; continue; } seg[k++] = *p; } }
static void draw_tb(void) { g_rect(W, 0, 0, W->w, TB, UI_PANEL); g_rect(W, 0, TB - 1, W->w, 1, UI_LINE); int x = 8, w;
    w = bw("Nuevo chat"); btn(x, 7, w, 26, "Nuevo chat", 2, B_NEW); x += w + 6; w = bw("Chats"); btn(x, 7, w, 26, "Chats", side_on() ? 1 : 0, B_SIDE); x += w + 6;
    char t[160], nm[60]; const char *mn = model_name(C.model); if (cpy_utf8(nm, 34, mn, strlen(mn)) < (int)strlen(mn)) strcat(nm, "..."); snprintf(t, 160, "%s: %s \xE2\x96\xBC", tr("Modelo"), nm); w = g_textw(t) + 22; btn(x, 7, w, 26, t, mode == 2 ? 1 : 0, B_MODEL); x += w + 6;
    w = bw("Ajustes"); btn(x, 7, w, 26, "Ajustes", mode == 1 ? 1 : 0, B_SET); if (!C.key[0] && is_or()) g_circle(W, x + w - 5, 10, 4, 0xDC2626, 1); x += w + 6;
    w = bw("Exportar"); if (x + w + bw("Imprimir") < W->w) { btn(x, 7, w, 26, "Exportar", 0, B_EXP); x += w + 6; w = bw("Imprimir"); btn(x, 7, w, 26, "Imprimir", 0, B_PRINT); } }
static void fmt_upd(long u, char *o, int cap) { u32 t[6] = {u % 100, u / 100 % 100, u / 10000 % 100, u / 1000000 % 100, u / 100000000 % 100, u / 10000000000L}; char a[24]; z_fmt_time(t, 1, 0, a, 24); if (u / 1000000 == now_num() / 1000000) snprintf(o, cap, "%s %s", tr("Hoy"), a); else { char d[40]; z_fmt_date(t, 1, d, 40); snprintf(o, cap, "%s %s", d, a); } }
static void draw_side(void) { int h = W->h - TB - SBH; g_rect(W, 0, TB, SW, h, UI_PANEL); g_rect(W, SW - 1, TB, 1, h, UI_LINE); g_text(W, 12, TB + 10, "Conversaciones", UI_MUTED);
    int y = TB + 34, rows = MAX(1, (W->h - SBH - y) / 40); sb_top = MAX(0, MIN(sb_top, ncv - rows)); if (!ncv) para(12, y + 4, SW - 24, tr("Aún no hay chats guardados."), UI_MUTED);
    for (int i = sb_top; i < ncv && i < sb_top + rows; i++, y += 40) { conv_t *c = CV[i]; if (i == cur) g_round(W, 4, y, SW - 9, 38, 4, UI_ACC2); char t[100], d[60]; dnorm_str(c->title[0] ? c->title : tr("Chat nuevo"), t, 100);
        zw_clip(W, 12, y, SW - 22, 40); (i == cur ? g_textb : g_text)(W, 12, y + 3, t, UI_TEXT); fmt_upd(c->upd, d, 60); g_text(W, 12, y + 20, d, UI_MUTED); zw_noclip(W); } }
static void ensure(conv_t *c, int cols) { for (int i = 0; i < c->n; i++) { msg_t *m = c->m[i]; if (m->dirty || m->cols != cols || m == SM || m->role == 2 || (m->role == 0 && i >= c->n - 2)) build(m, cols, i == c->n - 1); } }
static void dline(msg_t *m, dl_t *l, int x, int y, int rw) {
    if (m->role == 0 && (l->kind == K_WHO || l->kind == K_USER || l->kind == K_GAP)) { if (l->kind != K_GAP) g_rect(W, x - 12, y, rw + 24, LH, UI_PANEL); }
    switch (l->kind) {
    case K_WHO: { u32 c = m->role == 0 ? UI_TEXT : m->role == 2 ? 0xDC2626 : UI_ACC; if (m->role == 1) g_circle(W, x + 4, y + 8, 4, UI_ACC, 1); g_textb(W, x + (m->role == 1 ? 14 : 0), y + 1, l->s, c); break; }
    case K_USER: dtext(x, y + 1, l->s, 0, UI_TEXT); break;
    case K_TXT: case K_TABLE: dtext(x, y + 1, l->s, l->st, UI_TEXT); break;
    case K_H: dtext(x, y + 1, l->s, l->st | 1, UI_ACC); break;
    case K_CODEH: g_rect(W, x - 8, y, rw + 16, LH, 0x0F172A); g_text(W, x, y + 1, l->s[0] ? l->s : tr("código"), 0x94A3B8); g_text(W, x + rw - g_textw(tr("Copiar")), y + 1, tr("Copiar"), 0x93C5FD); break;
    case K_CODE: g_rect(W, x - 8, y, rw + 16, LH, 0x1E293B); g_text(W, x + 2, y + 1, l->s, 0xE2E8F0); break;
    case K_REAS: g_rect(W, x + 3, y, 2, LH, UI_LINE); g_text(W, x + 14, y + 1, l->s, UI_MUTED); break;
    case K_META: g_text(W, x, y + 1, l->s, UI_MUTED); break;
    case K_ERR: g_text(W, x, y + 1, l->s, 0xDC2626); break;
    case K_TOGGLE: g_text(W, x, y + 1, l->s, UI_ACC); break;
    case K_RETRY: { int w = bw("Reintentar"); g_round(W, x, y, w, LH, 3, UI_ACC); ctext(x + w / 2, y + 1, "Reintentar", UI_WHITE); break; }
    case K_HR: g_rect(W, x, y + LH / 2, rw, 1, UI_LINE); break;
    case K_QUOTE: g_rect(W, x, y, 3, LH, UI_ACC2); dtext(x + 12, y + 1, l->s, l->st, UI_MUTED); break;
    case K_THINK: { char t[60]; int d = z_ms() / 400 % 4; snprintf(t, 60, "%s%s", tr(m->r.n ? "Razonando" : "Pensando"), d == 3 ? "..." : d == 2 ? ".." : d == 1 ? "." : ""); g_text(W, x, y + 1, t, UI_MUTED); break; } } }
static void draw_empty(int x0, int y0, int w, int h) { int cx = x0 + w / 2, y = y0 + MAX(16, h / 2 - 160);
    g_text2(W, cx - 60, y, "LUMEN", UI_ACC, 3); y += 58; ctext(cx, y, "Chat con IA para ZunzúnOS", UI_MUTED); y += 30;
    if ((is_or() || is_ly()) && !C.key[0]) { ctext(cx, y, is_ly() ? "Añade tu API key de Lyceum para empezar." : "Añade tu API key gratuita de OpenRouter para empezar.", UI_TEXT); y += 26; int b = bw("Añadir API key") + 20; btn(cx - b / 2, y, b, 30, "Añadir API key", 2, B_ADDKEY); y += 46; }
    else { char t[200]; snprintf(t, 200, "%s: %s", tr("Modelo"), model_name(C.model)); ctext(cx, y, t, UI_TEXT); y += 34; }
    for (int i = 0; i < 4 && y + 34 < y0 + h; i++, y += 38) { int b = MIN(w - 40, g_textw(tr(SUG[i])) + 30); btn(cx - b / 2, y, b, 30, SUG[i], 0, B_SUG + i); } }
static void draw_chat(void) { int x0 = CX0(), cw = W->w - x0, y0 = TB, y1 = W->h - SBH - CH_H; conv_t *c = CC(); view_h = y1 - y0;
    if (!c || !c->n) { tot_h = 0; scy = 0; draw_empty(x0, y0, cw, y1 - y0); return; }
    int cols = MAX(20, (cw - 56) / 8), tot = 20; ensure(c, cols); for (int i = 0; i < c->n; i++) tot += c->m[i]->nL * LH; tot_h = tot; if (stick) scy = tot - view_h; scy = MAX(0, MIN(scy, tot - view_h));
    zw_clip(W, x0, y0, cw, view_h); zs_area(W, x0, y0, cw, view_h, scy); int y = y0 + 12 - scy, lx = x0 + 28, rw = cw - 56;
    for (int i = 0; i < c->n; i++) { msg_t *m = c->m[i]; if (y + m->nL * LH < y0 || y > y1) { y += m->nL * LH; continue; } for (int k = 0; k < m->nL; k++, y += LH) if (y + LH > y0 && y < y1) dline(m, &m->L[k], lx, y, rw); }
    zs_stop(W); zw_noclip(W); if (tot > view_h) { int bh = MAX(24, view_h * view_h / tot); g_round(W, W->w - 7, y0 + 2 + (view_h - bh - 4) * scy / MAX(1, tot - view_h), 4, bh, 2, UI_LINE); } }
static void draw_comp(void) { int x0 = CX0(), cw = W->w - x0, y = W->h - SBH - CH_H; g_rect(W, x0, y, cw, CH_H, UI_BG); g_rect(W, x0, y, cw, 1, UI_LINE);
    int bx = x0 + 12, by = y + 10, bww = cw - 24 - 106, bh = CH_H - 20; g_rect(W, bx, by, bww, bh, UI_WHITE); g_frame(W, bx, by, bww, bh, UI_ACC); hz(bx, by, bww, bh, 0);
    CT.cols = MAX(10, (bww - 16) / 8); if (!CT.len) { g_text(W, bx + 9, by + 7, "Escribe un mensaje...", UI_MUTED); g_text(W, bx + 9, by + 7 + LH * 2, "Intro envía  \xC2\xB7  Mayús+Intro, nueva línea", UI_LINE); }
    zw_clip(W, bx + 1, by + 1, bww - 2, bh - 2); te_draw(W, &CT, bx + 8, by + 7, bh - 12, 1, UI_TEXT, UI_ACC2, UI_ACC, 0); zw_noclip(W);
    btn(bx + bww + 10, by, 96, 34, busy && SM ? "Detener" : "Enviar", 2, B_SEND); if (!busy) { conv_t *c = CC(); char t[40]; if ((c && c->natt) || npimg) snprintf(t, 40, "%s (%d)", tr("Archivos"), (c ? c->natt : 0) + npimg); else strcpy(t, tr("Archivos")); btn(bx + bww + 10, by + 40, 96, 28, t, (c && c->natt) || npimg ? 1 : 0, B_FILES); } if (busy && SM) { char t[40]; snprintf(t, 40, "%ld s", (long)((z_ms() - t_send) / 1000)); ctext(bx + bww + 58, by + 44, t, UI_MUTED); } }
static void fld(int x, int y, int w, int id, const char *v, int foc) { ui_input(W, x, y, w, 26, v, foc ? scur : 0, foc); hz(x, y, w, 26, id); }
static void spin(int x, int y, const char *val, int idm, int idp) { btn(x, y, 28, 26, "-", 0, idm); g_rect(W, x + 32, y, 74, 26, UI_WHITE); g_frame(W, x + 32, y, 74, 26, UI_LINE); ctext(x + 69, y + 5, val, UI_TEXT); btn(x + 110, y, 28, 26, "+", 0, idp); }
static void draw_settings(void) { int x0 = 28, w = MIN(W->w - 56, 760), y = TB + 14; g_rect(W, 0, TB, W->w, W->h - TB - SBH, UI_BG);
    g_text2(W, x0, y, "Ajustes", UI_TEXT, 2); btn(W->w - 28 - bw("Cerrar"), y, bw("Cerrar"), 28, "Cerrar", 0, B_CLOSE);
    { static const char *PV[] = {"OpenRouter", "Lyceum (UE)", "Otro servidor"}; int px = x0 + 150; g_text(W, px, y + 6, "Proveedor:", UI_MUTED); px += g_textw(tr("Proveedor:")) + 10; for (int i = 0; i < 3; i++) { btn(px, y, bw(PV[i]), 28, PV[i], C.prov == i ? 2 : 0, B_PROV + i); px += bw(PV[i]) + 6; } } y += 42;
    g_textb(W, x0, y, is_or() ? "API key de OpenRouter" : is_ly() ? "API key de Lyceum" : "API key (opcional en servidores locales)", UI_TEXT); y += 22; char mk[256]; const char *kv_ = s_key; if (!show_key) { int n = MIN((int)strlen(s_key), 255); memset(mk, '*', n); mk[n] = 0; if (n > 12) memcpy(mk, s_key, 6); kv_ = mk; }
    int iw = w - bw("Pegar") - bw("Mostrar") - 16; ui_input(W, x0, y, iw, 26, kv_, sf == 0 ? (show_key ? scur : (int)strlen(kv_)) : 0, sf == 0); hz(x0, y, iw, 26, F_KEY); btn(x0 + iw + 8, y, bw("Pegar"), 26, "Pegar", 0, B_KPASTE); btn(x0 + iw + 16 + bw("Pegar"), y, bw("Mostrar"), 26, show_key ? "Ocultar" : "Mostrar", 0, B_KSHOW); y += 34;
    g_circle(W, x0 + 6, y + 8, 5, KC[kst], 1); g_textb(W, x0 + 18, y, KS[kst], KC[kst]); y += 20; if (kdet[0]) y += para(x0 + 18, y, w - 18, kdet, UI_MUTED) * 18; y += 6;
    int x = x0; btn(x, y, bw("Guardar y comprobar"), 28, "Guardar y comprobar", 2, B_KSAVE); x += bw("Guardar y comprobar") + 8; btn(x, y, bw("Probar de nuevo"), 28, "Probar de nuevo", 0, B_KTEST); x += bw("Probar de nuevo") + 8; btn(x, y, bw("Borrar clave"), 28, "Borrar clave", 0, B_KDEL); y += 38;
    y += para(x0, y, w, tr(is_or() ? "Crea una clave gratuita en openrouter.ai/keys (empieza por sk-or-v1-). Se guarda solo en esta memoria (ZUNZUN/LUMEN.TXT) y solo se envía al servidor de la API."
        : is_ly() ? "Crea la clave en dashboard.lyceum.technology (empieza por lk_). Lyceum funciona en servidores de la UE (París y Finlandia) y dice que no guarda los mensajes ni los usa para entrenar. Se paga por uso."
        : "Servidor compatible con OpenAI, por ejemplo Ollama o LM Studio en otro equipo de la red local. La clave solo hace falta si el servidor la pide."), UI_MUTED) * 18 + 12;
    g_textb(W, x0, y, "Instrucciones de sistema para todos los chats", UI_TEXT); y += 22; fld(x0, y, w, F_SYS, s_sys, sf == 1); y += 36;
    if (CC()) { g_textb(W, x0, y, "Instrucciones solo para este chat (tienen prioridad)", UI_TEXT); y += 22; fld(x0, y, w, F_CSYS, s_csys, sf == 2); y += 36; }
    char v[32]; g_textb(W, x0, y, "Temperatura", UI_TEXT); g_textb(W, x0 + 190, y, "Máximo de tokens", UI_TEXT); g_textb(W, x0 + 380, y, "Respuesta en vivo", UI_TEXT); y += 22;
    snprintf(v, 32, "%d.%d", C.temp / 10, C.temp % 10); spin(x0, y, v, B_TMINUS, B_TPLUS); if (C.maxtok) snprintf(v, 32, "%d", C.maxtok); else strcpy(v, tr("auto")); spin(x0 + 190, y, v, B_MMINUS, B_MPLUS);
    btn(x0 + 380, y, 60, 26, "Sí", C.stream ? 2 : 0, B_LIVE1); btn(x0 + 444, y, 60, 26, "No", C.stream ? 0 : 2, B_LIVE0); y += 40;
    g_textb(W, x0, y, "Servidor de la API (compatible con OpenAI; también en la red local)", UI_TEXT); y += 22; int aw = w - bw("Restablecer") - 8; fld(x0, y, aw, F_API, s_api, sf == 3); btn(x0 + aw + 8, y, bw("Restablecer"), 26, "Restablecer", 0, B_APIRESET); y += 40;
    g_textb(W, x0, y, "Datos", UI_TEXT); y += 22; btn(x0, y, bw("Borrar todos los chats"), 28, "Borrar todos los chats", 0, B_WIPE); char t[120]; snprintf(t, 120, tr("%d chats en %s/LUMEN"), ncv, base); g_text(W, x0 + bw("Borrar todos los chats") + 12, y + 6, t, UI_MUTED); }
static int md_filter(int *o) { int n = 0; for (int i = 0; i < nmd; i++) { if (md_tab == 0 && !MD[i].fr) continue; if (md_q[0] && !strcasestr(MD[i].name, md_q) && !strcasestr(MD[i].id, md_q)) continue; o[n++] = i; } return n; }
static int md_rows(void) { return MAX(1, (W->h - SBH - (TB + 112)) / 40); }
static void draw_models(void) { int x0 = 28, w = W->w - 56, y = TB + 14; g_rect(W, 0, TB, W->w, W->h - TB - SBH, UI_BG); g_text2(W, x0, y, "Elegir modelo", UI_TEXT, 2); btn(W->w - 28 - bw("Cerrar"), y, bw("Cerrar"), 28, "Cerrar", 0, B_CLOSE); y += 42;
    int nf = 0, na = nmd; for (int i = 0; i < nmd; i++) nf += MD[i].fr; char t[200]; snprintf(t, 200, "%s (%d)", tr("Gratis"), nf); btn(x0, y, g_textw(t) + 24, 28, t, md_tab == 0 ? 2 : 0, B_TABFREE); int x = x0 + g_textw(t) + 32; snprintf(t, 200, "%s (%d)", tr("Todos"), na); btn(x, y, g_textw(t) + 24, 28, t, md_tab == 1 ? 2 : 0, B_TABALL); x += g_textw(t) + 40;
    int rw = bw(md_loading ? "Descargando..." : "Actualizar lista"); int sw_ = MAX(120, x0 + w - rw - 8 - x); g_text(W, x, y + 6, "Buscar", UI_MUTED); x += g_textw(tr("Buscar")) + 8; sw_ -= g_textw(tr("Buscar")) + 8; ui_input(W, x, y + 1, sw_, 26, md_q, qcur, 1); hz(x, y + 1, sw_, 26, F_SEARCH); btn(x0 + w - rw, y, rw, 28, md_loading ? "Descargando..." : "Actualizar lista", 0, B_REFRESH); y += 36;
    if (md_at) { char d[60]; fmt_upd(md_at, d, 60); snprintf(t, 200, "%s %s  \xC2\xB7  %s", tr("Lista actualizada:"), d, tr("los modelos gratuitos tienen límites de uso y cambian a menudo")); } else snprintf(t, 200, "%s", tr("Lista básica sin conexión: pulsa Actualizar lista para ver todos los modelos")); g_text(W, x0, y, t, UI_MUTED); y += 24;
    int *fl = malloc((nmd + 1) * sizeof(int)), n = md_filter(fl), rows = md_rows(); md_cur = MAX(0, MIN(md_cur, n - 1)); if (md_cur < md_top) md_top = md_cur; if (md_cur >= md_top + rows) md_top = md_cur - rows + 1; md_top = MAX(0, MIN(md_top, n - rows));
    if (!n) { g_text(W, x0, y + 8, "Ningún modelo coincide con la búsqueda.", UI_MUTED); if (strchr(md_q, '/')) { char u2[160]; snprintf(u2, 160, "%s %s", tr("Usar el modelo"), md_q); btn(x0, y + 34, g_textw(u2) + 24, 30, u2, 2, B_USEID); } }
    for (int r = md_top; r < n && r < md_top + rows; r++, y += 40) { model_t *m = &MD[fl[r]]; int sel = !strcmp(m->id, C.model); g_round(W, x0, y, w, 37, 4, r == md_cur ? UI_ACC2 : UI_WHITE); if (sel) g_rect(W, x0, y + 4, 4, 29, UI_ACC);
        char pr[80], cx[24], rt[120]; fmt_price(m, pr, 80); fmt_ctx(m->ctx, cx, 24); if (m->ctx) snprintf(rt, 120, "%s  \xC2\xB7  %s %s", pr, cx, tr("contexto")); else strcpy(rt, pr); int rtw = g_textw(rt);
        zw_clip(W, x0 + 12, y, w - rtw - 36, 38); g_textb(W, x0 + 12, y + 2, m->name, UI_TEXT); g_text(W, x0 + 12, y + 19, m->id, UI_MUTED); zw_noclip(W); g_text(W, x0 + w - rtw - 12, y + 11, rt, m->fr ? 0x16A34A : UI_MUTED); hz(x0, y, w, 37, B_MROW + fl[r]); }
    free(fl); }
/* panel Archivos: los adjuntos del chat y un explorador sencillo para anadir mas */
static char fb_dir[200]; static int fb_top; static zdirent FBE[300]; static int nfbe;
static int fb_cmp(const void *a, const void *b) { const zdirent *x = a, *y = b; if (x->is_dir != y->is_dir) return y->is_dir - x->is_dir; return strcasecmp(x->name, y->name); }
static void fb_load(const char *d) { if (d != fb_dir) strncpy0(fb_dir, d, 200); nfbe = 0; fb_top = 0; zdirent e; for (int i = 0; nfbe < 300 && !z_readdir(fb_dir, i, &e); i++) { if (e.name[0] == '.' || (!e.is_dir && !RAG_OK(e.name) && !IMG_OK(e.name))) continue; FBE[nfbe++] = e; } qsort(FBE, nfbe, sizeof *FBE, fb_cmp); }
static void fb_go(const char *d) { zdirent e; if (d[0] && (!strcmp(d, "/") || !z_stat(d, &e))) fb_load(d); else snprintf(msg, 300, "%s: %s", tr("No existe"), d); }
static int fb_rows(void) { return MAX(3, (W->h - SBH - (TB + 14 + 300)) / 26); }
static void draw_files(void) { int x0 = 28, w = MIN(W->w - 56, 860), y = TB + 14; conv_t *c = CC(); g_rect(W, 0, TB, W->w, W->h - TB - SBH, UI_BG);
    g_text2(W, x0, y, "Archivos del chat", UI_TEXT, 2); btn(W->w - 28 - bw("Cerrar"), y, bw("Cerrar"), 28, "Cerrar", 0, B_CLOSE); y += 42;
    y += para(x0, y, w, tr("LUMEN busca en estos archivos los fragmentos que tienen que ver con cada pregunta y se los manda al modelo junto con ella. Sirven TXT, MD, CSV, HTML, RTF, DOCX, ODT, XLSX y PDF con texto."), UI_MUTED) * 18 + 10;
    if (!c || !c->natt) { g_text(W, x0, y, "Este chat no tiene archivos todavía.", UI_MUTED); y += 26; }
    else for (int i = 0; i < c->natt; i++) { const char *b = strrchr(c->att[i], '/'); b = b ? b + 1 : c->att[i]; char t[200]; int nf = 0; long nl = 0; for (int k = 0; k < nrd; k++) if (!strcmp(RD[k].path, c->att[i])) { nf = RD[k].nch; nl = RD[k].n; }
        g_textb(W, x0, y + 5, b, UI_TEXT); if (nl) { snprintf(t, 200, tr("%ld letras, %d fragmentos"), nl, nf); g_text(W, x0 + g_textw(b) + 16, y + 5, t, UI_MUTED); } btn(x0 + w - bw("Quitar"), y, bw("Quitar"), 26, "Quitar", 0, B_FDEL + i); y += 30; }
    y += 10; g_rect(W, x0, y, w, 1, UI_LINE); y += 12; g_textb(W, x0, y, "Añadir un archivo", UI_TEXT); y += 24;
    int x = x0; static const char *PL[] = {"Documentos", "Escritorio", "Memoria", "Discos", "Escribir ruta..."}; static const int PI[] = {B_FDOCS, B_FDESK, B_FSYS, B_FROOT, B_FPATH};
    for (int i = 0; i < 5; i++) { btn(x, y, bw(PL[i]), 26, PL[i], 0, PI[i]); x += bw(PL[i]) + 6; } y += 34;
    if (!fb_dir[0]) { char d[120]; z_docdir(d); fb_load(d); }
    btn(x0, y, 30, 24, "..", 0, B_FUP); g_text(W, x0 + 40, y + 4, fb_dir, UI_TEXT); y += 30; int rows = fb_rows(); fb_top = MAX(0, MIN(fb_top, nfbe - rows));
    g_rect(W, x0, y, w, rows * 26 + 4, UI_WHITE); g_frame(W, x0, y, w, rows * 26 + 4, UI_LINE);
    if (!nfbe) g_text(W, x0 + 10, y + 8, "Aquí no hay carpetas ni documentos que se puedan usar.", UI_MUTED);
    for (int i = 0; i < rows && fb_top + i < nfbe; i++) { zdirent *e = &FBE[fb_top + i]; int ry = y + 2 + i * 26; char t[160]; if (e->is_dir) snprintf(t, 160, "[%s]  %s", tr("carpeta"), e->name); else snprintf(t, 160, "%s   (%lu KB)", e->name, (unsigned long)((e->size + 1023) / 1024));
        g_text(W, x0 + 10, ry + 5, t, e->is_dir ? UI_ACC : UI_TEXT); hz(x0 + 1, ry, w - 2, 26, B_FROW + fb_top + i); }
    if (nfbe > rows) { char t[60]; snprintf(t, 60, tr("%d de %d (rueda para ver más)"), MIN(rows, nfbe - fb_top), nfbe); g_text(W, x0, y + rows * 26 + 10, t, UI_MUTED); } }
static void fb_pick(int i) { if (i < 0 || i >= nfbe) return; char p[260]; snprintf(p, 260, "%s%s%s", fb_dir, strcmp(fb_dir, "/") ? "/" : "", FBE[i].name); if (FBE[i].is_dir) { fb_go(p); return; } if (IMG_OK(p)) { img_add(p); return; } rag_add(p); }
void draw(void) { nhz = 0; need = 0; last_draw = z_ms(); g_clear(W, UI_WHITE); zs_area(W, 0, 0, 0, 0, 0); zs_stop(W); draw_tb(); if (mode == 1) draw_settings(); else if (mode == 2) draw_models(); else if (mode == 3) draw_files(); else { if (side_on()) draw_side(); draw_chat(); draw_comp(); }
    g_rect(W, 0, W->h - SBH, W->w, SBH, UI_PANEL); g_rect(W, 0, W->h - SBH, W->w, 1, UI_LINE); zw_clip(W, 0, W->h - SBH, W->w - 200, SBH); g_text(W, 8, W->h - 19, msg, UI_TEXT); zw_noclip(W);
    conv_t *c = CC(); if (c && c->n) { char t[60]; snprintf(t, 60, tr("%d mensajes"), c->n); g_text(W, W->w - 8 - g_textw(t), W->h - 19, t, UI_MUTED); } if (mode == 0) zs_show(W); zw_present(W); }

/* ---------- acciones ---------- */
static int wait_busy(void) { if (!busy) return 0; strcpy(msg, tr("Espera a que termine (Esc para detener)")); return 1; }
static void conv_md(conv_t *c, sb_t *b) { sb_s(b, "# "); sb_s(b, c->title[0] ? c->title : tr("Chat")); sb_s(b, "\n\n"); const char *sys = c->sys && c->sys[0] ? c->sys : C.sys; if (sys[0]) { sb_s(b, "> "); sb_s(b, tr("Instrucciones de sistema")); sb_s(b, ": "); sb_s(b, sys); sb_s(b, "\n\n"); }
    for (int i = 0; i < c->n; i++) { msg_t *m = c->m[i]; if (m->role == 0) { sb_s(b, "**"); sb_s(b, tr("Tú")); sb_s(b, ":**\n\n"); } else if (m->role == 1) { sb_s(b, "**LUMEN ("); sb_s(b, m->model); sb_s(b, "):**\n\n"); } else { sb_s(b, "> **"); sb_s(b, tr("Error")); sb_s(b, ":** "); } sb_s(b, m->t.p); sb_s(b, "\n\n"); }
    sb_s(b, "---\n"); sb_s(b, tr("Exportado desde LUMEN en ZunzúnOS")); sb_s(b, "\n"); }
static void export_md(conv_t *c) { if (!c || !c->n) { strcpy(msg, tr("No hay nada que exportar")); return; } if (wait_busy()) return; char d[120], nm[64], p[200]; z_docdir(d); dnorm_str(c->title[0] ? c->title : "LUMEN", nm, 50); for (char *q = nm; *q; q++) if (strchr("/\\:*?\"<>|.", *q)) *q = '_';
    snprintf(p, 200, "%s/%s.md", d, nm); if (!ui_prompt("Exportar", "Guardar el chat como Markdown en:", p, 190) || !p[0]) return; if (!z_ends(p, ".md")) strcat(p, ".md"); sb_t b = {0}; conv_md(c, &b); int e = z_writefile(p, b.p, b.n); free(b.p); z_sync();
    if (e) snprintf(msg, 300, "%s: %s", tr("No se pudo guardar"), z_errstr(e)); else snprintf(msg, 300, "%s %s", tr("Guardado en"), p); char lg[260]; snprintf(lg, 260, "[prueba] lumen exportado %s: %d", p, e); z_log(lg); }
static void print_conv(conv_t *c) { if (!c || !c->n) { strcpy(msg, tr("No hay nada que imprimir")); return; } if (wait_busy()) return; sb_t b = {0}; conv_md(c, &b); char sub[120]; char d[60]; fmt_upd(c->upd, d, 60); snprintf(sub, 120, "LUMEN  \xC2\xB7  %s", d); zp_print_text(c->title[0] ? c->title : "LUMEN", sub, b.p, 0); free(b.p); }
static void open_settings(void) { strncpy0(s_key, C.key, 256); strncpy0(s_sys, C.sys, 1024); strncpy0(s_csys, CC() && CC()->sys ? CC()->sys : "", 1024); strncpy0(s_api, C.api, 200); mode = 1; sf = C.key[0] ? -1 : 0; scur = strlen(s_key); show_key = 0; if (C.key[0] && !kst) kst = 5; }
static void apply_settings(void) { clean_key(s_key); int changed = strcmp(s_key, C.key) != 0; strcpy(C.key, s_key); strcpy(C.sys, s_sys); char *a = s_api; while (*a == ' ') a++; int l = strlen(a); while (l && (a[l - 1] == ' ' || a[l - 1] == '/')) a[--l] = 0; if (*a && strcmp(a, C.api) && C.prov != 2 && strcmp(a, DEF_API) && strcmp(a, LYC_API)) { char pm[128]; strcpy(pm, C.model); prov_keep(); strncpy0(C.custom, a, 200); prov_use(2); strcpy(C.key, s_key); if (!C.model[0]) strcpy(C.model, pm); } else if (C.prov == 2) strncpy0(C.api, *a ? a : C.custom, 200); else strncpy0(C.api, C.prov == 1 ? LYC_API : DEF_API, 200);
    if (CC()) { free(CC()->sys); CC()->sys = strdup(s_csys); if (CC()->n) conv_save(CC()); } if (changed) { kst = C.key[0] ? 5 : 0; kdet[0] = 0; } int e = cfg_save(); if (e) snprintf(msg, 300, "%s: %s", tr("No se pudieron guardar los ajustes"), z_errstr(e)); }
static void close_panel(void) { if (mode == 1) apply_settings(); mode = 0; sf = -1; }
static void retry(void) { conv_t *c = CC(); if (!c || wait_busy()) return; while (c->n && c->m[c->n - 1]->role != 0) msg_del(c, c->n - 1); if (c->n) ask(); }
static void send_text(void) { if (wait_busy()) return; char *t = CT.t ? CT.t : (char *)""; while (*t == ' ' || *t == '\n') t++; long n = strlen(t); while (n && (t[n - 1] == ' ' || t[n - 1] == '\n')) n--; if (!n) return;
    if ((is_or() || is_ly()) && !C.key[0]) { open_settings(); strcpy(msg, tr("Primero añade tu API key de OpenRouter (gratis en openrouter.ai/keys)")); return; }
    conv_t *c = CC(); if (!c) c = conv_new(); msg_t *m = msg_add(c, 0); sb_add(&m->t, t, n); if (npimg) { m->img = strdup(pimg); pimg[0] = 0; npimg = 0; } te_set(&CT, "", 0); conv_title(c); c->upd = now_num(); stick = 1; mode = 0; ask(); }
static void new_chat(void) { if (wait_busy()) return; mode = 0; if (CC() && !CC()->n) return; for (int i = 0; i < ncv; i++) if (!CV[i]->n) { cur = i; return; } cur = -1; stick = 1; te_set(&CT, "", 0); }
static void code_block(const char *t, int k, sb_t *o) { int blk = 0, in = 0; for (const char *p = t; *p; ) { const char *e = strchr(p, '\n'); long n = e ? e - p : (long)strlen(p); const char *s = p; while (s < p + n && *s == ' ') s++;
        if (s - p < 4 && (!strncmp(s, "```", 3) || !strncmp(s, "~~~", 3))) { if (!in) { in = 1; blk++; } else { in = 0; if (blk == k) break; } } else if (in && blk == k) { sb_add(o, p, n); sb_s(o, "\n"); } if (!e) break; p = e + 1; } if (!o->p) sb_add(o, "", 0); }
static void wipe(void) { if (wait_busy() || !ncv || !ui_confirm("Borrar todos los chats", "¿Borrar todas las conversaciones guardadas?")) return; while (ncv) conv_remove(0); cur = -1; strcpy(msg, tr("Conversaciones borradas")); }
static void pick_model(int i) { if (i < 0 || i >= nmd) return; strncpy0(C.model, MD[i].id, 128); cfg_save(); mode = 0; snprintf(msg, 300, "%s: %s (%s)", tr("Modelo"), MD[i].name, MD[i].id); char lg[200]; snprintf(lg, 200, "[prueba] lumen modelo %s", MD[i].id); z_log(lg); }
static int models_stale(void) { return !md_at || now_num() - md_at > 60000; }
static void act(int id) {
    if (id >= B_FROW) { fb_pick(id - B_FROW); return; } if (id >= B_FDEL) { rag_del(id - B_FDEL); return; }
    if (id >= B_MROW) { pick_model(id - B_MROW); return; } if (id >= B_SUG && id < B_SUG + 4) { te_set(&CT, tr(SUG[id - B_SUG]), strlen(tr(SUG[id - B_SUG]))); send_text(); return; }
    switch (id) {
    case B_FILES: if (wait_busy()) break; if (mode == 1) apply_settings(); mode = mode == 3 ? 0 : 3; if (mode == 3 && !fb_dir[0]) { char d[120]; z_docdir(d); fb_load(d); } else if (mode == 3) fb_load(fb_dir); break;
    case B_FUP: { char d[200]; strcpy(d, fb_dir); char *q = strrchr(d, '/'); if (q && q > d) *q = 0; else strcpy(d, "/"); fb_go(d); break; }
    case B_FDOCS: { char d[120]; z_docdir(d); fb_go(d); break; } case B_FDESK: fb_go("/sys/ESCRITORIO"); break; case B_FSYS: fb_go("/sys"); break; case B_FROOT: fb_go("/"); break;
    case B_FPATH: { char p[200]; strcpy(p, fb_dir); if (ui_prompt("Añadir archivo", "Ruta del archivo o de la carpeta:", p, 190) && p[0]) { zdirent e; if (!z_stat(p, &e) && e.is_dir) fb_go(p); else if (IMG_OK(p)) img_add(p); else rag_add(p); } break; }
    case B_NEW: new_chat(); break; case B_SIDE: C.side = !side_on(); if (C.side && W->w < 640) C.side = 1; cfg_save(); break;
    case B_MODEL: if (mode == 1) apply_settings(); mode = mode == 2 ? 0 : 2; if (mode == 2) { md_q[0] = 0; qcur = 0; md_top = 0; model_t *m = md_find(C.model); md_tab = m && !m->fr ? 1 : 0; md_cur = 0; int fl[2048], n = nmd < 2048 ? md_filter(fl) : 0; for (int k = 0; k < n; k++) if (!strcmp(MD[fl[k]].id, C.model)) md_cur = k; if (models_stale() && !busy) { draw(); fetch_models(); } } break;
    case B_SET: if (mode == 1) close_panel(); else open_settings(); break; case B_ADDKEY: open_settings(); sf = 0; break;
    case B_EXP: { if (wait_busy()) break; static const char *it[] = {"Guardar como Markdown", "Copiar todo el chat"}; int x = 0; for (int i = 0; i < nhz; i++) if (HZ[i].id == B_EXP) x = HZ[i].x; int sx, sy; zw_screen(W, x, TB - 4, &sx, &sy); int r = ui_menu(sx, sy, it, 2);
        if (r == 0) export_md(CC()); else if (r == 1 && CC() && CC()->n) { sb_t b = {0}; conv_md(CC(), &b); z_clip_set(b.p, b.n); free(b.p); strcpy(msg, tr("Chat copiado al portapapeles")); } break; }
    case B_PRINT: print_conv(CC()); break; case B_SEND: if (busy && SM) { stop_req = 1; strcpy(msg, tr("Deteniendo...")); } else send_text(); break;
    case B_CLOSE: close_panel(); break; case B_KPASTE: { char t[256]; int n = z_clip_get(t, 255); if (n > 0) { t[n] = 0; clean_key(t); strncpy0(s_key, t, 256); scur = strlen(s_key); sf = 0; } break; }
    case B_KSHOW: show_key = !show_key; break; case B_KSAVE: if (wait_busy()) break; apply_settings(); check_key(); break; case B_KTEST: if (wait_busy()) break; apply_settings(); check_key(); break;
    case B_KDEL: s_key[0] = 0; scur = 0; apply_settings(); strcpy(msg, tr("Clave borrada")); break;
    case B_TMINUS: C.temp = MAX(0, C.temp - 1); cfg_save(); break; case B_TPLUS: C.temp = MIN(20, C.temp + 1); cfg_save(); break;
    case B_MMINUS: case B_MPLUS: { int k = 0; for (int i = 0; i < 9; i++) if (MAXT[i] == C.maxtok) k = i; k = id == B_MPLUS ? MIN(8, k + 1) : MAX(0, k - 1); C.maxtok = MAXT[k]; cfg_save(); break; }
    case B_PROV: case B_PROV + 1: case B_PROV + 2: { if (wait_busy()) break; apply_settings(); prov_keep(); prov_use(id - B_PROV); strncpy0(s_key, C.key, 256); scur = strlen(s_key); strncpy0(s_api, C.api, 200); kst = C.key[0] ? 5 : 0; kdet[0] = 0; md_load(); cfg_save(); snprintf(msg, 300, "%s: %s", tr("Proveedor"), id == B_PROV ? "OpenRouter" : id == B_PROV + 1 ? "Lyceum (UE)" : tr("Otro servidor")); char lg[80]; snprintf(lg, 80, "[prueba] lumen proveedor %d", C.prov); z_log(lg); break; }
    case B_LIVE1: C.stream = 1; cfg_save(); break; case B_LIVE0: C.stream = 0; cfg_save(); break; case B_APIRESET: strcpy(s_api, C.prov == 1 ? LYC_API : DEF_API); scur = strlen(s_api); break; case B_WIPE: wipe(); break;
    case B_USEID: { char id[112]; strncpy0(id, md_q, 112); char *t = id; while (*t == ' ') t++; md_add(t, t, 0, 0, 0, z_ends(t, ":free")); md_save(); pick_model(nmd - 1); break; }
    case B_TABFREE: md_tab = 0; md_top = md_cur = 0; break; case B_TABALL: md_tab = 1; md_top = md_cur = 0; break; case B_REFRESH: if (!wait_busy()) fetch_models(); break;
    case F_KEY: sf = 0; if (show_key) ui_input_click(W, 0, s_key, &scur); else scur = strlen(s_key); break; case F_SYS: sf = 1; ui_input_click(W, 0, s_sys, &scur); break; case F_CSYS: sf = 2; ui_input_click(W, 0, s_csys, &scur); break; case F_API: sf = 3; ui_input_click(W, 0, s_api, &scur); break; } }
static char *sfield(int i, int *cap) { *cap = i == 0 ? 250 : i == 3 ? 190 : 1000; return i == 0 ? s_key : i == 1 ? s_sys : i == 2 ? s_csys : s_api; }
static int locate(int y, int *mi, int *li) { conv_t *c = CC(); if (!c || y < TB || y >= W->h - SBH - CH_H) return 0; int yy = TB + 12 - scy; for (int i = 0; i < c->n; i++) { msg_t *m = c->m[i]; if (y >= yy + m->nL * LH) { yy += m->nL * LH; continue; } if (y < yy) return 0; *mi = i; *li = (y - yy) / LH; return 1; } return 0; }
static void msg_menu(zevent *e, int mi, int li) { conv_t *c = CC(); msg_t *m = c->m[mi]; int last = mi == c->n - 1, code = m->L[li].blk; const char *it[5]; int id[5], n = 0;
    if (m->role == 2) { it[n] = "Reintentar"; id[n++] = 3; } else { it[n] = "Copiar"; id[n++] = 0; } if (code) { it[n] = "Copiar el código"; id[n++] = 1; } if (m->role == 0) { it[n] = "Editar y reenviar"; id[n++] = 2; } if (m->role == 1 && last) { it[n] = "Regenerar"; id[n++] = 3; } it[n] = "Borrar"; id[n++] = 4;
    int sx, sy; zw_screen(W, e->x, e->y, &sx, &sy); int r = ui_menu(sx, sy, it, n); if (r < 0) return; r = id[r];
    if (r == 0) { z_clip_set(m->t.p, m->t.n); strcpy(msg, tr("Mensaje copiado")); } else if (r == 1) { sb_t o = {0}; code_block(m->t.p, code, &o); z_clip_set(o.p, o.n); free(o.p); strcpy(msg, tr("Código copiado")); }
    else if (r == 2) { te_set(&CT, m->t.p, m->t.n); CT.cur = CT.len; while (c->n > mi) msg_del(c, c->n - 1); conv_save(c); stick = 1; } else if (r == 3) retry(); else if (r == 4) { msg_del(c, mi); if (c->n) conv_save(c); else { conv_remove(cur); } } }
static void side_menu(zevent *e, int i) { static const char *it[] = {"Renombrar", "Guardar como Markdown", "Copiar todo el chat", "Borrar"}; int sx, sy; zw_screen(W, e->x, e->y, &sx, &sy); int r = ui_menu(sx, sy, it, 4); conv_t *c = CV[i];
    if (r == 0) { char t[96]; strncpy0(t, c->title, 96); if (ui_prompt("Renombrar", "Nuevo nombre del chat:", t, 90) && t[0]) { strncpy0(c->title, t, 96); conv_save(c); } } else if (r == 1) export_md(c); else if (r == 2) { sb_t b = {0}; conv_md(c, &b); z_clip_set(b.p, b.n); free(b.p); strcpy(msg, tr("Chat copiado al portapapeles")); }
    else if (r == 3) { char q[200]; snprintf(q, 200, tr("¿Borrar \"%.60s\"?"), c->title); if (ui_confirm("Borrar", q)) conv_remove(i); } }
static int comp_drag, comp_x, comp_y;
static void click(zevent *e) { int right = e->btn & 2;
    for (int i = nhz - 1; i >= 0; i--) if (ui_in(e->x, e->y, HZ[i].x, HZ[i].y, HZ[i].w, HZ[i].h)) { if (HZ[i].id == 0) { if (mode == 0) { if (right) te_menu(W, &CT, e); else { te_click(&CT, e->x - HZ[i].x - 8, e->y - HZ[i].y - 7, 1, e->mods & M_SHIFT); comp_drag = 1; comp_x = HZ[i].x; comp_y = HZ[i].y; } } return; } if (!right) act(HZ[i].id); return; }
    if (mode) { sf = -1; return; }
    if (side_on() && e->x < SW && e->y > TB + 34 && e->y < W->h - SBH) { int i = sb_top + (e->y - TB - 34) / 40; if (i >= ncv) return; if (busy) { wait_busy(); return; } if (right) side_menu(e, i); else { cur = i; stick = 1; } return; }
    int mi, li; if (!locate(e->y, &mi, &li)) return; msg_t *m = CC()->m[mi]; if (li >= m->nL) return; dl_t *l = &m->L[li];
    if (right) { if (!wait_busy()) msg_menu(e, mi, li); return; }
    if (l->kind == K_TOGGLE) { m->open = !m->open; m->dirty = 1; stick = 0; } else if (l->kind == K_RETRY) retry(); else if (l->kind == K_CODEH) { sb_t o = {0}; code_block(m->t.p, l->blk, &o); z_clip_set(o.p, o.n); free(o.p); strcpy(msg, tr("Código copiado")); } }
static void key(zevent *e) {
    if (e->ch == 27) { if (busy) { stop_req = 1; strcpy(msg, tr("Deteniendo...")); } else if (mode) close_panel(); return; }
    if (e->ch == 14) { act(B_NEW); return; } if (e->ch == 16 && !mode) { print_conv(CC()); return; } if (e->ch == 12) { act(B_MODEL); return; }
    if (mode == 1) { if (e->key == K_TAB) { int n = CC() ? 4 : 3; do sf = (sf + 1) % 4; while (sf == 2 && !CC()); (void)n; int cap; scur = strlen(sfield(sf, &cap)); return; } if (e->ch == '\n') { if (!wait_busy()) { apply_settings(); if (sf == 0) check_key(); else mode = 0; } return; } if (sf >= 0) { int cap; ui_input_key(sfield(sf, &cap), cap, &scur, e); } return; }
    if (mode == 2) { int fl[2048], n = nmd < 2048 ? md_filter(fl) : 0, rows = md_rows(); if (e->key == K_DOWN) md_cur++; else if (e->key == K_UP) md_cur--; else if (e->key == K_PGDN) md_cur += rows; else if (e->key == K_PGUP) md_cur -= rows; else if (e->ch == '\n') { if (n) pick_model(fl[MAX(0, MIN(md_cur, n - 1))]); } else if (ui_input_key(md_q, 70, &qcur, e)) { md_cur = md_top = 0; } md_cur = MAX(0, MIN(md_cur, n - 1)); return; }
    if (mode == 3) return;
    if (e->ch == 19) { export_md(CC()); return; } if (e->ch == 18) { retry(); return; }
    if (e->key == K_PGUP || e->key == K_PGDN) { scy += (e->key == K_PGUP ? -1 : 1) * MAX(LH, view_h - 3 * LH); stick = scy >= tot_h - view_h; return; }
    if (e->ch == '\n' && !(e->mods & M_SHIFT)) { send_text(); return; } te_key(&CT, e, 4); }
static void handle(zevent *e) {
    if (e->type == EV_REDRAW) { dirty_all(); need = 1; return; } if (e->win != (u32)W->id) return;
    if (e->type == EV_CLOSE) { quit = 1; stop_req = 1; return; } if (e->type == EV_RESIZE) { zw_resized(W, e->x, e->y); need = 1; return; }
    /* seleccionar con el raton: en la conversacion (zsel) y en el cuadro de escribir (arrastrando dentro) */
    if (mode == 0 && zs_event(W, e)) { need = 1; return; }
    if (e->type == EV_MOVE && comp_drag) { if (!e->btn) comp_drag = 0; else { te_click(&CT, e->x - comp_x - 8, e->y - comp_y - 7, 1, 1); need = 1; } return; }
    if (e->type == EV_UP) { comp_drag = 0; return; }
    if (e->type == EV_KEY) { key(e); need = 1; } else if (e->type == EV_DOWN) { click(e); need = 1; }
    else if (e->type == EV_WHEEL) { int d = (int)e->key; if (mode == 2) md_cur -= d * 3; else if (mode == 3) fb_top -= d * 3; else if (mode == 0 && side_on() && e->x < SW) sb_top -= d; else if (mode == 0) { scy -= d * 3 * LH; stick = scy >= tot_h - view_h; } need = 1; } }
int main(const char *a) { zdirent d; if (!z_stat("/sys", &d)) strcpy(base, "/sys/ZUNZUN"); else { z_mkdir("/rd/tmp"); strcpy(base, "/rd/tmp/ZUNZUN"); } z_mkdir(base);
    cfg_load(); md_load(); conv_load_all(); te_init(&CT); if (C.key[0]) kst = 5;
    zsys s; z_sysinfo(&s); W = zw_new("LUMEN", -1, 0, MIN(1000, (int)s.scr_w - 40), MIN(680, (int)s.scr_h - 80), WF_DECOR | WF_RESIZE); if (!W) return 1;
    if ((is_or() || is_ly()) && !C.key[0]) strcpy(msg, tr("Bienvenido a LUMEN. Añade tu API key de OpenRouter en Ajustes.")); else snprintf(msg, 300, "%s: %s", tr("Modelo"), model_name(C.model));
    draw(); char lg[80]; snprintf(lg, 80, "[prueba] lumen listo, %d chats", ncv); z_log(lg); zevent e;
    while (!quit) { if (!z_event(&e, busy ? 300 : -1)) { if (busy) draw(); continue; } handle(&e); if (need && !quit) draw(); }
    return 0; }
