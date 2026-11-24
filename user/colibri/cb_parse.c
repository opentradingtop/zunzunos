/* Colibri: analizador lexico y sintactico (texto -> arbol) y resolucion de nombres.
   El arbol se reserva en bloques propios que viven lo que viva el motor (las funciones lo usan al llamarse).
   Despues de analizar, un recorrido asigna a cada variable su ambito: (saltos de entorno, posicion), o "global". */
#include "cbi.h"
double cb_parse_number(const char *s, int n);

/* ---------------- memoria del arbol ---------------- */
typedef struct achunk { struct achunk *next; u32 used, cap; } achunk;
static achunk *arena;
void cb_ast_free(void) { while (arena) { achunk *n = arena->next; free(arena); arena = n; } }
static void *aal(u32 n) {
    n = (n + 15) & ~15u;
    if (!arena || arena->used + n > arena->cap) { u32 cap = n > 65536 ? n : 65536; achunk *c = malloc(sizeof(achunk) + cap); if (!c) return 0; c->next = arena; c->used = 0; c->cap = cap; arena = c; }
    void *p = (u8 *)(arena + 1) + arena->used; arena->used += n; memset(p, 0, n); return p; }

/* ---------------- lexico ---------------- */
enum { T_EOF, T_NUM, T_STR, T_NAME, T_PRIV, T_PUNC, T_TMPL, T_REGEX, T_BIGINT };
#define P2(a, b) ((a) | ((b) << 8))
#define P3(a, b, c) ((a) | ((b) << 8) | ((c) << 16))
#define P4(a, b, c, d) ((a) | ((b) << 8) | ((c) << 16) | ((u32)(d) << 24))
typedef struct {
    cb_rt *R; const char *s; long n, p; u32 line; const char *file;
    int t; u32 pc; cbv v; long ts, te; int nl; u32 tline; int escaped;   /* token actual */
    int tail;                         /* plantilla: 1 si el trozo acaba en ` */
    cbv raw;                          /* plantilla: texto crudo */
    char *err; int ecap; int failed;
    int prev_expr;                    /* el token anterior termina una expresion (entonces / es division) */
    scope *sc; fninfo *fn;            /* ambito y funcion actuales */
    int in_class, strict, module;
    int no_in;                        /* dentro de la parte inicial de un for (in no es operador) */
} P;
static void perr(P *p, const char *m) { if (p->failed) return; p->failed = 1; cb_snprintf(p->err, p->ecap, "SyntaxError: %s (%s:%u)", m, p->file ? p->file : "?", p->tline ? p->tline : p->line); }
static int idstart(u32 c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '$' || c == '_' || c >= 0x80; }
static int idpart(u32 c) { return idstart(c) || (c >= '0' && c <= '9') || c == 0x200C || c == 0x200D; }
static u32 utf8at(P *p, long i, int *len) {
    u8 b = (u8)p->s[i]; if (b < 0x80) { *len = 1; return b; }
    int k = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1; u32 c = k == 4 ? b & 7 : k == 3 ? b & 15 : k == 2 ? b & 31 : 0xFFFD;
    for (int j = 1; j < k; j++) { if (i + j >= p->n) { *len = 1; return 0xFFFD; } c = c << 6 | ((u8)p->s[i + j] & 63); } *len = k; return c; }
static int hexv(int c) { return c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1; }
/* cadena de trabajo en UTF-16 para literales */
typedef struct { u16 *c; u32 n, cap; } wbuf;
static void wput(wbuf *w, u32 c) { if (w->n + 2 > w->cap) { w->cap = w->cap * 2 + 64; w->c = realloc(w->c, w->cap * 2); } if (c >= 0x10000) { c -= 0x10000; w->c[w->n++] = (u16)(0xD800 + (c >> 10)); w->c[w->n++] = (u16)(0xDC00 + (c & 1023)); } else w->c[w->n++] = (u16)c; }
static cbv wstr(wbuf *w, int atom) { cb_str *s = cb_str_new(w->n); if (w->n) memcpy(s->c, w->c, w->n * 2); int a = 1; for (u32 i = 0; i < w->n; i++) if (w->c[i] >= 128) a = 0; s->ascii = (u8)a; free(w->c); w->c = 0; return atom ? cb_strv(cb_atom_str(s)) : cb_strv(s); }
/* escape dentro de cadena o plantilla; devuelve el punto de codigo (o -1 continuacion de linea, -2 error) */
static long esc(P *p, int tmpl) {
    char c = p->s[p->p++];
    switch (c) {
    case 'n': return '\n'; case 't': return '\t'; case 'r': return '\r'; case 'b': return '\b'; case 'f': return '\f'; case 'v': return '\v';
    case '\r': if (p->p < p->n && p->s[p->p] == '\n') p->p++; p->line++; return -1;
    case '\n': p->line++; return -1;
    case 'x': { int a = p->p + 1 < p->n ? hexv(p->s[p->p]) : -1, b = a >= 0 ? hexv(p->s[p->p + 1]) : -1; if (a < 0 || b < 0) return -2; p->p += 2; return a * 16 + b; }
    case 'u': {
        if (p->s[p->p] == '{') { p->p++; long v = 0; int d = 0; while (p->p < p->n && p->s[p->p] != '}') { int h = hexv(p->s[p->p++]); if (h < 0) return -2; v = v * 16 + h; if (v > 0x10FFFF) return -2; d++; } p->p++; return d ? v : -2; }
        long v = 0; for (int i = 0; i < 4; i++) { int h = p->p < p->n ? hexv(p->s[p->p++]) : -1; if (h < 0) return -2; v = v * 16 + h; } return v; }
    default:
        if (c >= '0' && c <= '7') {   /* \0 y octales antiguos */
            if (c == '0' && !(p->p < p->n && p->s[p->p] >= '0' && p->s[p->p] <= '9')) return 0;
            if (tmpl) return -2; long v = c - '0'; for (int i = 0; i < 2 && p->p < p->n && p->s[p->p] >= '0' && p->s[p->p] <= '7' && v * 8 + p->s[p->p] - '0' < 256; i++) v = v * 8 + p->s[p->p++] - '0'; return v; }
        { p->p--; int l; u32 cp = utf8at(p, p->p, &l); p->p += l; if (cp == 0x2028 || cp == 0x2029) return -1; return cp; } } }
static void skip_ws(P *p) {
    p->nl = 0;
    for (;;) {
        if (p->p >= p->n) return;
        u8 c = (u8)p->s[p->p];
        if (c == '\n') { p->nl = 1; p->line++; p->p++; continue; }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f') { p->p++; continue; }
        if (c == '/' && p->p + 1 < p->n && p->s[p->p + 1] == '/') { while (p->p < p->n && p->s[p->p] != '\n') p->p++; continue; }
        if (c == '/' && p->p + 1 < p->n && p->s[p->p + 1] == '*') { p->p += 2; while (p->p + 1 < p->n && !(p->s[p->p] == '*' && p->s[p->p + 1] == '/')) { if (p->s[p->p] == '\n') { p->nl = 1; p->line++; } p->p++; } p->p += 2; continue; }
        if (c == '<' && p->p + 3 < p->n && !memcmp(p->s + p->p, "<!--", 4) && !p->module) { while (p->p < p->n && p->s[p->p] != '\n') p->p++; continue; }
        if (c == '-' && p->nl && p->p + 2 < p->n && !memcmp(p->s + p->p, "-->", 3)) { while (p->p < p->n && p->s[p->p] != '\n') p->p++; continue; }
        if (c == '#' && p->p == 0 && p->p + 1 < p->n && p->s[1] == '!') { while (p->p < p->n && p->s[p->p] != '\n') p->p++; continue; }
        if (c >= 0x80) { int l; u32 cp = utf8at(p, p->p, &l); if (cp == 0xA0 || cp == 0xFEFF || cp == 0x2028 || cp == 0x2029 || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x202F || cp == 0x205F || cp == 0x3000 || cp == 0x1680) { if (cp == 0x2028 || cp == 0x2029) { p->nl = 1; p->line++; } p->p += l; continue; } }
        return; } }
static void scan_template(P *p) {   /* desde despues de ` o de } hasta ${ o ` */
    wbuf w = { 0 }, raw = { 0 }; int bad = 0;
    for (;;) {
        if (p->p >= p->n) { perr(p, "plantilla sin cerrar"); break; }
        char c = p->s[p->p];
        if (c == '`') { p->p++; p->tail = 1; break; }
        if (c == '$' && p->p + 1 < p->n && p->s[p->p + 1] == '{') { p->p += 2; p->tail = 0; break; }
        if (c == '\\') { long st = p->p; p->p++; long v = esc(p, 1); for (long i = st; i < p->p; i++) wput(&raw, (u8)p->s[i]); if (v == -2) bad = 1; else if (v >= 0) wput(&w, (u32)v); continue; }
        if (c == '\r') { p->p++; if (p->p < p->n && p->s[p->p] == '\n') p->p++; p->line++; wput(&w, '\n'); wput(&raw, '\n'); continue; }
        if (c == '\n') p->line++;
        int l; u32 cp = utf8at(p, p->p, &l); p->p += l; wput(&w, cp); wput(&raw, cp); }
    p->t = T_TMPL; p->v = bad ? CB_UNDEF : wstr(&w, 1); p->raw = wstr(&raw, 1); }   /* internadas: el arbol no lo ve el recolector */
static void next(P *p) {
    int was_expr = p->prev_expr;
    skip_ws(p); p->ts = p->p; p->tline = p->line; p->escaped = 0;
    if (p->p >= p->n) { p->t = T_EOF; p->te = p->p; return; }
    u8 c = (u8)p->s[p->p];
    if (c >= '0' && c <= '9' || (c == '.' && p->p + 1 < p->n && p->s[p->p + 1] >= '0' && p->s[p->p + 1] <= '9')) {
        long st = p->p; char buf[400]; int k = 0; int big = 0;
        if (c == '0' && p->p + 1 < p->n && strchr("xXoObB", p->s[p->p + 1])) {
            int base = (p->s[p->p + 1] | 32) == 'x' ? 16 : (p->s[p->p + 1] | 32) == 'o' ? 8 : 2; p->p += 2; double v = 0; int any = 0;
            while (p->p < p->n) { char d = p->s[p->p]; if (d == '_') { p->p++; continue; } int h = hexv(d); if (h < 0 || h >= base) break; v = v * base + h; p->p++; any = 1; }
            if (p->p < p->n && p->s[p->p] == 'n') { p->p++; big = 1; }
            if (!any) perr(p, "numero no valido"); p->t = big ? T_BIGINT : T_NUM; p->v = big ? cb_intern(p->R, cb_s(cb_str_utf8(p->R, p->s + p->ts, p->p - 1 - p->ts))) : cb_num(v); p->te = p->p; p->prev_expr = 1; return; }
        int legacy = c == '0' && p->p + 1 < p->n && p->s[p->p + 1] >= '0' && p->s[p->p + 1] <= '9';
        if (legacy) { int oct = 1; long q = p->p + 1; while (q < p->n && p->s[q] >= '0' && p->s[q] <= '9') { if (p->s[q] > '7') oct = 0; q++; }
            if (oct) { double v = 0; for (long i = p->p + 1; i < q; i++) v = v * 8 + p->s[i] - '0'; p->p = q; p->t = T_NUM; p->v = cb_num(v); p->te = p->p; p->prev_expr = 1; return; } }
        while (p->p < p->n && k < 390) { char d = p->s[p->p]; if (d >= '0' && d <= '9') buf[k++] = d; else if (d == '_') {} else break; p->p++; }
        if (p->p < p->n && p->s[p->p] == 'n') { p->p++; p->t = T_BIGINT; p->v = cb_intern(p->R, cb_s(cb_str_utf8(p->R, p->s + p->ts, p->p - 1 - p->ts))); p->te = p->p; p->prev_expr = 1; return; }
        if (p->p < p->n && p->s[p->p] == '.') { buf[k++] = '.'; p->p++; while (p->p < p->n && k < 390) { char d = p->s[p->p]; if (d >= '0' && d <= '9') buf[k++] = d; else if (d == '_') {} else break; p->p++; } }
        if (p->p < p->n && (p->s[p->p] | 32) == 'e') { long sv = p->p; int kk = k; buf[k++] = 'e'; p->p++; if (p->p < p->n && (p->s[p->p] == '+' || p->s[p->p] == '-')) buf[k++] = p->s[p->p++]; int any = 0; while (p->p < p->n && p->s[p->p] >= '0' && p->s[p->p] <= '9' && k < 398) { buf[k++] = p->s[p->p++]; any = 1; } if (!any) { p->p = sv; k = kk; } }
        if (p->p < p->n && idstart((u8)p->s[p->p])) perr(p, "identificador justo despues de un numero");
        buf[k] = 0; (void)st; p->t = T_NUM; p->v = cb_num(cb_parse_number(buf, k)); p->te = p->p; p->prev_expr = 1; return; }
    if (c == '"' || c == '\'') {
        p->p++; wbuf w = { 0 };
        for (;;) { if (p->p >= p->n || p->s[p->p] == '\n') { perr(p, "cadena sin cerrar"); break; }
            char d = p->s[p->p]; if ((u8)d == c) { p->p++; break; }
            if (d == '\\') { p->p++; long v = esc(p, 0); if (v == -2) { perr(p, "escape no valido"); break; } if (v >= 0) wput(&w, (u32)v); continue; }
            int l; u32 cp = utf8at(p, p->p, &l); p->p += l; wput(&w, cp); }
        p->t = T_STR; p->v = wstr(&w, 1); p->te = p->p; p->prev_expr = 1; return; }
    if (c == '`') { p->p++; scan_template(p); p->te = p->p; p->prev_expr = 1; return; }
    if (idstart(c) || c == '\\' || c == '#') {
        int priv = c == '#'; if (priv) p->p++;
        wbuf w = { 0 }; if (priv) wput(&w, '#');
        while (p->p < p->n) { int l; u32 cp; if (p->s[p->p] == '\\') { if (p->p + 1 < p->n && p->s[p->p + 1] == 'u') { p->p += 2; p->escaped = 1; long v; if (p->s[p->p] == '{') { p->p++; v = 0; while (p->p < p->n && p->s[p->p] != '}') v = v * 16 + hexv(p->s[p->p++]); p->p++; } else { v = 0; for (int i = 0; i < 4 && p->p < p->n; i++) v = v * 16 + hexv(p->s[p->p++]); } wput(&w, (u32)v); continue; } break; }
            cp = utf8at(p, p->p, &l); if (!(w.n ? idpart(cp) : idstart(cp))) break; wput(&w, cp); p->p += l; }
        if (!w.n || (priv && w.n == 1)) { perr(p, "caracter no valido"); p->p++; }
        p->t = priv ? T_PRIV : T_NAME; p->v = wstr(&w, 1); p->te = p->p;
        cb_str *s = cb_s(p->v);
        p->prev_expr = priv || !(cb_str_eqa(s, "return") || cb_str_eqa(s, "typeof") || cb_str_eqa(s, "instanceof") || cb_str_eqa(s, "in") || cb_str_eqa(s, "of") || cb_str_eqa(s, "new") || cb_str_eqa(s, "delete") || cb_str_eqa(s, "void") || cb_str_eqa(s, "throw") || cb_str_eqa(s, "case") || cb_str_eqa(s, "do") || cb_str_eqa(s, "else") || cb_str_eqa(s, "yield") || cb_str_eqa(s, "await") || cb_str_eqa(s, "extends"));
        return; }
    if (c == '/' && !was_expr) {   /* expresion regular */
        long st = ++p->p; int cls = 0;
        for (;;) { if (p->p >= p->n || p->s[p->p] == '\n') { perr(p, "expresion regular sin cerrar"); break; }
            char d = p->s[p->p]; if (d == '\\') { p->p += 2; continue; } if (d == '[') cls = 1; else if (d == ']') cls = 0; else if (d == '/' && !cls) break; p->p++; }
        long en = p->p++; long fs = p->p; while (p->p < p->n && idpart((u8)p->s[p->p])) p->p++;
        p->t = T_REGEX; p->v = cb_intern(p->R, cb_s(cb_str_utf8(p->R, p->s + st, en - st))); p->raw = cb_intern(p->R, cb_s(cb_str_utf8(p->R, p->s + fs, p->p - fs))); p->te = p->p; p->prev_expr = 1; return; }
    /* signos: el mas largo posible */
    static const char *const ops[] = { ">>>=", "...", "===", "!==", "**=", "<<=", ">>=", ">>>", "&&=", "||=", "??=", "=>", "==", "!=", "<=", ">=", "&&", "||", "??", "?.", "++", "--", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<", ">>", "**", 0 };
    for (int i = 0; ops[i]; i++) { int l = (int)strlen(ops[i]); if (p->p + l <= p->n && !memcmp(p->s + p->p, ops[i], l)) {
            if (!strcmp(ops[i], "?.") && p->p + 2 < p->n && p->s[p->p + 2] >= '0' && p->s[p->p + 2] <= '9') continue;   /* a?.5:b */
            u32 v = 0; for (int j = 0; j < l; j++) v |= (u32)(u8)ops[i][j] << (8 * j); p->p += l; p->t = T_PUNC; p->pc = v; p->te = p->p; p->prev_expr = !strcmp(ops[i], "++") || !strcmp(ops[i], "--"); return; } }
    if (strchr("{}()[];,<>+-*/%&|^!~?:=.@", c)) { p->p++; p->t = T_PUNC; p->pc = c; p->te = p->p; p->prev_expr = c == ')' || c == ']' || c == '}'; return; }
    perr(p, "caracter no valido"); p->p++; p->t = T_EOF; }
/* estado para mirar por delante y volver */
typedef struct { long p; u32 line; int t; u32 pc; cbv v, raw; long ts, te; int nl, prev_expr, tail, escaped; u32 tline; } lexsave;
static void lsave(P *p, lexsave *s) { *s = (lexsave){ p->p, p->line, p->t, p->pc, p->v, p->raw, p->ts, p->te, p->nl, p->prev_expr, p->tail, p->escaped, p->tline }; }
static void lrest(P *p, lexsave *s) { p->p = s->p; p->line = s->line; p->t = s->t; p->pc = s->pc; p->v = s->v; p->raw = s->raw; p->ts = s->ts; p->te = s->te; p->nl = s->nl; p->prev_expr = s->prev_expr; p->tail = s->tail; p->escaped = s->escaped; p->tline = s->tline; }

static int is(P *p, u32 pc) { return p->t == T_PUNC && p->pc == pc; }
static int isw(P *p, const char *w) { return p->t == T_NAME && !p->escaped && cb_str_eqa(cb_s(p->v), w); }
static int eat(P *p, u32 pc) { if (is(p, pc)) { next(p); return 1; } return 0; }
static int eatw(P *p, const char *w) { if (isw(p, w)) { next(p); return 1; } return 0; }
static void expect(P *p, u32 pc) { if (!eat(p, pc)) { char m[64]; char t[5] = { 0 }; for (int i = 0; i < 4; i++) t[i] = (char)(pc >> (8 * i)); cb_snprintf(m, 64, "se esperaba '%s'", t); perr(p, m); } }
static void semi(P *p) { if (eat(p, ';')) return; if (is(p, '}') || p->t == T_EOF || p->nl) return; perr(p, "falta ';'"); }
static const char *const RESERVED[] = { "break", "case", "catch", "class", "const", "continue", "debugger", "default", "delete", "do", "else", "export", "extends", "false", "finally", "for", "function", "if", "import", "in", "instanceof", "new", "null", "return", "super", "switch", "this", "throw", "true", "try", "typeof", "var", "void", "while", "with", 0 };
static int reserved(cbv v) { cb_str *s = cb_s(v); for (int i = 0; RESERVED[i]; i++) if (cb_str_eqa(s, RESERVED[i])) return 1; return 0; }

/* ---------------- nodos y ambitos ---------------- */
static node *nn(P *p, int k) { node *n = aal(sizeof(node)); n->k = (u16)k; n->line = p->tline; n->hops = n->slot = -1; return n; }
static scope *push_scope(P *p, int kind) { scope *s = aal(sizeof(scope)); s->up = p->sc; s->fn = p->fn; s->kind = kind; p->sc = s; return s; }
static void pop_scope(P *p) { p->sc = p->sc->up; }
static int declare(P *p, scope *s, cbv name, int kind) {
    for (int i = 0; i < s->n; i++) if (s->names[i] == name) { if (kind != VK_VAR && s->nkind[i] != 0 && s->kind != 2) { if (s->nkind[i] != VK_VAR || kind != 4) {} } return i; }
    if (s->n == s->cap) { int nc = s->cap * 2 + 8; cbv *nm = aal(nc * sizeof(cbv)); u8 *nk = aal(nc); if (s->n) { memcpy(nm, s->names, s->n * sizeof(cbv)); memcpy(nk, s->nkind, s->n); } s->names = nm; s->nkind = nk; s->cap = nc; }
    s->names[s->n] = name; s->nkind[s->n] = (u8)kind; return s->n++; }
static scope *var_scope(P *p) { scope *s = p->sc; while (s && s->kind != 0 && s->kind != 2) s = s->up; return s; }
static void add_func(scope *s, node *f) { if (s->nf == s->capf) { int nc = s->capf * 2 + 4; node **nf = aal(nc * sizeof(node *)); if (s->nf) memcpy(nf, s->funcs, s->nf * sizeof(node *)); s->funcs = nf; s->capf = nc; } s->funcs[s->nf++] = f; }
static void declare_pattern(P *p, node *pat, int kind);
static void declare_name(P *p, cbv name, int kind) {
    scope *s = kind == VK_VAR ? var_scope(p) : p->sc;
    if (kind == VK_VAR) for (scope *b = p->sc; b && b != s; b = b->up) { (void)b; }
    declare(p, s, name, kind); }
static void declare_pattern(P *p, node *pat, int kind) {
    if (!pat) return;
    switch (pat->k) {
    case N_ID: declare_name(p, pat->v, kind); break;
    case N_DEFAULT: declare_pattern(p, pat->a, kind); break;
    case N_REST: declare_pattern(p, pat->a, kind); break;
    case N_ARRPAT: for (node *e = pat->a; e; e = e->next) declare_pattern(p, e, kind); break;
    case N_OBJPAT: for (node *e = pat->a; e; e = e->next) declare_pattern(p, e->k == N_PROPPAT ? e->b : e, kind); break;
    default: break; } }

/* ---------------- expresiones ---------------- */
static node *expr(P *p);
static node *assign(P *p);
static node *stmt(P *p);
static node *func_rest(P *p, u32 flags, cbv name, long start);
static node *class_expr(P *p, int decl);
static node *to_pattern(P *p, node *e);
static node *binding(P *p);
static node *str_node(P *p, cbv v) { node *n = nn(p, N_STR); n->v = v; return n; }
static node *ident(P *p) {
    if (p->t != T_NAME) { perr(p, "se esperaba un nombre"); next(p); return nn(p, N_ID); }
    node *n = nn(p, N_ID); n->v = p->v; next(p); return n; }
static node *args(P *p) {   /* ( ... ) ya consumido el ( */
    node *h = 0, **t = &h;
    while (!is(p, ')') && !p->failed) { node *a; if (eat(p, P3('.', '.', '.'))) { a = nn(p, N_SPREAD); a->a = assign(p); } else a = assign(p); *t = a; t = &a->next; if (!eat(p, ',')) break; }
    expect(p, ')'); return h; }
static node *template(P *p, node *tag) {
    node *n = nn(p, N_TMPL); node *h = 0, **t = &h; node *rh = 0, **rt = &rh;
    for (;;) { node *q = nn(p, N_STR); q->v = p->v; *t = q; t = &q->next; node *rq = nn(p, N_STR); rq->v = p->raw; *rt = rq; rt = &rq->next;
        if (p->v == CB_UNDEF && !tag) perr(p, "escape no valido en plantilla");
        if (p->tail) { p->prev_expr = 1; next(p); break; }
        next(p); node *e = expr(p); *t = e; t = &e->next; if (!is(p, '}')) { perr(p, "se esperaba '}' en la plantilla"); break; }
        scan_template(p); }
    n->a = h; n->c = rh; n->b = tag; if (tag) n->k = N_TAGGED; return n; }
static node *obj_lit(P *p) {
    node *o = nn(p, N_OBJ); node *h = 0, **t = &h;
    while (!is(p, '}') && !p->failed) {
        node *pr = nn(p, N_PROP); long start = p->ts;
        if (eat(p, P3('.', '.', '.'))) { pr->flags = 256; pr->b = assign(p); }
        else {
            u32 fl = 0; int isasync = 0, isgen = 0, get = 0, set = 0;
            if (isw(p, "async")) { lexsave s; lsave(p, &s); next(p); if (!is(p, ',') && !is(p, ':') && !is(p, '(') && !is(p, '}') && !is(p, '=') && !p->nl) isasync = 1; else lrest(p, &s); }
            if (eat(p, '*')) isgen = 1;
            if (!isasync && !isgen && (isw(p, "get") || isw(p, "set"))) { lexsave s; lsave(p, &s); int g = isw(p, "get"); next(p); if (!is(p, ',') && !is(p, ':') && !is(p, '(') && !is(p, '}') && !is(p, '=')) { if (g) get = 1; else set = 1; } else lrest(p, &s); }
            /* clave */
            node *key; int shorthand_ok = 0; cbv sname = 0;
            if (eat(p, '[')) { key = assign(p); expect(p, ']'); fl |= 1; }
            else if (p->t == T_NAME) { sname = p->v; shorthand_ok = 1; key = str_node(p, p->v); next(p); }
            else if (p->t == T_STR) { key = str_node(p, p->v); next(p); }
            else if (p->t == T_NUM) { key = nn(p, N_NUM); key->v = p->v; next(p); }
            else if (p->t == T_BIGINT) { key = str_node(p, cb_intern(p->R, cb_s(cb_big_to_str(p->R, cb_big_from_str(p->R, cb_s(p->v), 0), 10)))); next(p); }
            else { perr(p, "clave de objeto no valida"); break; }
            pr->a = key;
            if (get || set || isasync || isgen || is(p, '(')) {
                u32 ff = FK_METHOD | (get ? FK_GET : 0) | (set ? FK_SET : 0) | (isasync ? FK_ASYNC : 0) | (isgen ? FK_GEN : 0);
                pr->b = func_rest(p, ff, key->k == N_STR ? key->v : 0, start); fl |= get ? 16 : set ? 32 : 8; }
            else if (eat(p, ':')) pr->b = assign(p);
            else if (shorthand_ok) { node *id = nn(p, N_ID); id->v = sname; fl |= 2;
                if (is(p, '=')) { next(p); node *d = nn(p, N_DEFAULT); d->a = id; d->b = assign(p); pr->b = d; pr->flags |= 512; } else pr->b = id; }
            else perr(p, "se esperaba ':'");
            pr->flags |= fl; }
        *t = pr; t = &pr->next; if (!eat(p, ',')) break; }
    expect(p, '}'); o->a = h; return o; }
static int arrow_ahead(P *p) {   /* estamos en ( : mira si despues del ) emparejado viene => */
    lexsave s; lsave(p, &s); int depth = 0, ok = 0;
    for (;;) { if (p->t == T_EOF || p->failed) break; if (is(p, '(') || is(p, '[') || is(p, '{')) depth++; else if (is(p, ')') || is(p, ']') || is(p, '}')) { depth--; if (depth == 0) { next(p); ok = is(p, P2('=', '>')) && !p->nl; break; } }
        next(p); }
    int f = p->failed; lrest(p, &s); p->failed = f; return ok; }
static node *arrow(P *p, u32 flags, long start) {   /* en ( o en un nombre */
    fninfo *fi = aal(sizeof(fninfo)); fi->flags = FK_ARROW | flags; fi->src = p->s; fi->src_start = (u32)start; fi->file = p->file;
    node *f = nn(p, N_FUNC); f->fn = fi; fninfo *ofn = p->fn; p->fn = fi; fi->sc = push_scope(p, 0); fi->sc->fn = fi; fi->sc->env = 1;
    node *h = 0, **t = &h; int simple = 1, n = 0, len = -1;
    if (p->t == T_NAME) { node *id = ident(p); h = id; n = 1; }
    else { expect(p, '('); while (!is(p, ')') && !p->failed) { node *b; if (eat(p, P3('.', '.', '.'))) { b = nn(p, N_REST); b->a = binding(p); simple = 0; if (len < 0) len = n; } else { b = binding(p); if (b->k != N_ID) simple = 0; if (is(p, '=')) { next(p); node *d = nn(p, N_DEFAULT); d->a = b; d->b = assign(p); b = d; simple = 0; if (len < 0) len = n; } } *t = b; t = &b->next; n++; if (!eat(p, ',')) break; } expect(p, ')'); }
    for (node *q = h; q; q = q->next) declare_pattern(p, q, VK_VAR);
    fi->params = h; fi->nparams = n; fi->simple = simple; fi->length = len < 0 ? n : len;
    expect(p, P2('=', '>'));
    if (is(p, '{')) { next(p); node *bh = 0, **bt = &bh; while (!is(p, '}') && !p->failed) { node *s = stmt(p); if (s) { *bt = s; bt = &s->next; } } fi->src_end = (u32)p->te; next(p); node *b = nn(p, N_BLOCK); b->a = bh; fi->body = b; }
    else { int ni = p->no_in; p->no_in = 0; node *e = assign(p); p->no_in = ni; fi->flags |= FK_EXPR; node *r = nn(p, N_RETURN); r->a = e; fi->body = r; fi->src_end = (u32)p->ts; }
    pop_scope(p); p->fn = ofn; p->prev_expr = 1; return f; }
static node *primary(P *p) {
    node *n; long start = p->ts;
    switch (p->t) {
    case T_NUM: n = nn(p, N_NUM); n->v = p->v; next(p); return n;
    case T_BIGINT: n = nn(p, N_NUM); n->v = p->v; n->flags = 1; next(p); return n;
    case T_STR: n = str_node(p, p->v); next(p); return n;
    case T_TMPL: return template(p, 0);
    case T_REGEX: n = nn(p, N_REGEX); n->v = p->v; n->a = str_node(p, p->raw); next(p); return n;
    case T_PRIV: { n = nn(p, N_PRIVIN); n->v = p->v; next(p); if (!isw(p, "in")) perr(p, "#nombre fuera de su clase"); return n; }
    case T_NAME: {
        if (isw(p, "function")) { next(p); u32 fl = 0; if (eat(p, '*')) fl |= FK_GEN; cbv nm = 0; if (p->t == T_NAME) { nm = p->v; next(p); } node *f = func_rest(p, fl, nm, start); f->flags |= 1; return f; }
        if (isw(p, "async")) { lexsave s; lsave(p, &s); next(p);
            if (isw(p, "function") && !p->nl) { next(p); u32 fl = FK_ASYNC; if (eat(p, '*')) fl |= FK_GEN; cbv nm = 0; if (p->t == T_NAME) { nm = p->v; next(p); } node *f = func_rest(p, fl, nm, start); f->flags |= 1; return f; }
            if (p->t == T_NAME && !p->nl) { lexsave s2; lsave(p, &s2); next(p); int ar = is(p, P2('=', '>')); lrest(p, &s2); if (ar) return arrow(p, FK_ASYNC, start); }
            if (is(p, '(') && !p->nl && arrow_ahead(p)) return arrow(p, FK_ASYNC, start);
            lrest(p, &s); }
        if (isw(p, "class")) return class_expr(p, 0);
        if (isw(p, "this")) { next(p); return nn(p, N_THIS); }
        if (isw(p, "null")) { next(p); return nn(p, N_NULL); }
        if (isw(p, "true")) { next(p); return nn(p, N_TRUE); }
        if (isw(p, "false")) { next(p); return nn(p, N_FALSE); }
        if (isw(p, "super")) { next(p); return nn(p, N_SUPER); }
        if (isw(p, "new")) { next(p);
            if (eat(p, '.')) { if (!isw(p, "target")) perr(p, "se esperaba new.target"); next(p); return nn(p, N_NEWTARGET); }
            node *c = nn(p, N_NEW);
            /* el constructor: miembro sin llamadas */
            node *callee = primary(p);
            for (;;) { if (eat(p, '.')) { node *d; if (p->t == T_PRIV) { d = nn(p, N_PRIVDOT); d->v = p->v; next(p); } else { d = nn(p, N_DOT); if (p->t != T_NAME) perr(p, "se esperaba un nombre"); d->v = p->v; next(p); } d->a = callee; callee = d; }
                else if (eat(p, '[')) { node *d = nn(p, N_INDEX); d->a = callee; d->b = expr(p); expect(p, ']'); callee = d; }
                else if (p->t == T_TMPL) callee = template(p, callee);
                else break; }
            c->a = callee; if (eat(p, '(')) c->b = args(p); p->prev_expr = 1; return c; }
        if (isw(p, "import")) { next(p); if (eat(p, '.')) { next(p); return nn(p, N_IMPORTMETA); } expect(p, '('); n = nn(p, N_DYNIMPORT); n->a = assign(p); eat(p, ','); if (!is(p, ')')) assign(p); expect(p, ')'); return n; }
        { lexsave s; lsave(p, &s); cbv nm = p->v; next(p); if (is(p, P2('=', '>')) && !p->nl) { lrest(p, &s); return arrow(p, 0, start); } lrest(p, &s); (void)nm; }
        if (reserved(p->v) && !p->escaped) { perr(p, "palabra reservada inesperada"); next(p); return nn(p, N_NULL); }
        return ident(p); }
    case T_PUNC:
        if (is(p, '(')) {
            if (arrow_ahead(p)) return arrow(p, 0, start);
            next(p); int ni = p->no_in; p->no_in = 0; n = expr(p); p->no_in = ni; expect(p, ')'); if (n && n->k == N_ID) n->flags |= 1024; else if (n && (n->k == N_OBJ || n->k == N_ARR)) n->flags |= 2048; return n; }
        if (eat(p, '[')) {
            n = nn(p, N_ARR); node *h = 0, **t = &h;
            while (!is(p, ']') && !p->failed) { node *e; if (is(p, ',')) { e = nn(p, N_HOLE); } else if (eat(p, P3('.', '.', '.'))) { e = nn(p, N_SPREAD); e->a = assign(p); } else e = assign(p); *t = e; t = &e->next; if (!eat(p, ',')) break; }
            expect(p, ']'); n->a = h; return n; }
        if (eat(p, '{')) return obj_lit(p);
        if (is(p, '/') || is(p, P2('/', '='))) {   /* regex que el lexico tomo por division */
            p->p = p->ts; p->prev_expr = 0; next(p); return primary(p); }
        if (is(p, '#')) { next(p); }
        break;
    default: break; }
    perr(p, "expresion inesperada"); next(p); return nn(p, N_NULL); }
static node *call_member(P *p, node *e) {
    for (;;) {
        if (eat(p, '.')) { node *d; if (p->t == T_PRIV) { d = nn(p, N_PRIVDOT); d->v = p->v; next(p); } else { if (p->t != T_NAME) { perr(p, "se esperaba un nombre"); return e; } d = nn(p, N_DOT); d->v = p->v; next(p); p->prev_expr = 1; } d->a = e; e = d; continue; }
        if (eat(p, P2('?', '.'))) {
            node *oc = e->k == N_OPTCHAIN ? e : 0; node *base = oc ? oc->a : e; node *d;
            if (eat(p, '(')) { d = nn(p, N_CALL); d->a = base; d->b = args(p); }
            else if (eat(p, '[')) { d = nn(p, N_INDEX); d->a = base; d->b = expr(p); expect(p, ']'); }
            else if (p->t == T_PRIV) { d = nn(p, N_PRIVDOT); d->a = base; d->v = p->v; next(p); }
            else { d = nn(p, N_DOT); d->a = base; d->v = p->v; next(p); p->prev_expr = 1; }
            d->flags |= 1; if (!oc) { oc = nn(p, N_OPTCHAIN); } oc->a = d; e = oc; continue; }
        if (eat(p, '[')) { node *d = nn(p, N_INDEX); int ni = p->no_in; p->no_in = 0; d->b = expr(p); p->no_in = ni; expect(p, ']'); if (e->k == N_OPTCHAIN) { d->a = e->a; e->a = d; } else { d->a = e; e = d; } continue; }
        if (eat(p, '(')) { node *d = nn(p, N_CALL); d->b = args(p); if (e->k == N_OPTCHAIN) { d->a = e->a; e->a = d; } else { d->a = e; e = d; } p->prev_expr = 1; continue; }
        if (p->t == T_TMPL) { if (e->k == N_OPTCHAIN) perr(p, "plantilla en cadena opcional"); e = template(p, e); continue; }
        if (e->k == N_OPTCHAIN) { /* la cadena sigue mientras haya . [ ( */ }
        return e; } }
static node *postfix(P *p) {
    node *e = call_member(p, primary(p));
    if ((is(p, P2('+', '+')) || is(p, P2('-', '-'))) && !p->nl) { node *u = nn(p, N_UPDATE); u->op = is(p, P2('+', '+')) ? O_INC : O_DEC; u->a = e; u->flags = 0; next(p); p->prev_expr = 1; return u; }
    return e; }
static node *unary(P *p) {
    int op = 0;
    if (p->t == T_PUNC) switch (p->pc) { case '!': op = O_NOT; break; case '-': op = O_NEG; break; case '+': op = O_PLUS; break; case '~': op = O_BITNOT; break; case P2('+', '+'): op = O_INC; break; case P2('-', '-'): op = O_DEC; break; }
    else if (isw(p, "typeof")) op = O_TYPEOF; else if (isw(p, "void")) op = O_VOID; else if (isw(p, "delete")) op = O_DELETE;
    else if (isw(p, "await") && (p->fn ? (p->fn->flags & FK_ASYNC) : p->module)) { next(p); node *a = nn(p, N_AWAIT); a->a = unary(p); return a; }
    if (!op) return postfix(p);
    next(p);
    if (op == O_INC || op == O_DEC) { node *u = nn(p, N_UPDATE); u->op = (u16)op; u->flags = 1; u->a = unary(p); return u; }
    node *u = nn(p, N_UNARY); u->op = (u16)op; u->a = unary(p);
    if (op != O_DELETE && op != O_TYPEOF && op != O_VOID && op != O_NOT && is(p, P2('*', '*'))) perr(p, "** despues de un operador unario necesita parentesis");
    return u; }
static int binprec(P *p, int *op) {
    if (p->t == T_NAME) { if (isw(p, "instanceof")) { *op = O_INSTANCEOF; return 10; } if (isw(p, "in") && !p->no_in) { *op = O_IN; return 10; } return 0; }
    if (p->t != T_PUNC) return 0;
    switch (p->pc) {
    case P2('?', '?'): *op = O_NULLISH; return 1;
    case P2('|', '|'): *op = O_LOR; return 2; case P2('&', '&'): *op = O_LAND; return 3;
    case '|': *op = O_OR; return 4; case '^': *op = O_XOR; return 5; case '&': *op = O_AND; return 6;
    case P2('=', '='): *op = O_EQ; return 7; case P2('!', '='): *op = O_NE; return 7; case P3('=', '=', '='): *op = O_SEQ; return 7; case P3('!', '=', '='): *op = O_SNE; return 7;
    case '<': *op = O_LT; return 8; case '>': *op = O_GT; return 8; case P2('<', '='): *op = O_LE; return 8; case P2('>', '='): *op = O_GE; return 8;
    case P2('<', '<'): *op = O_SHL; return 9; case P2('>', '>'): *op = O_SAR; return 9; case P3('>', '>', '>'): *op = O_SHR; return 9;
    case '+': *op = O_ADD; return 11; case '-': *op = O_SUB; return 11;
    case '*': *op = O_MUL; return 12; case '/': *op = O_DIV; return 12; case '%': *op = O_MOD; return 12;
    case P2('*', '*'): *op = O_EXP; return 13; }
    return 0; }
static node *binary(P *p, int minp) {
    node *l = unary(p);
    for (;;) {
        int op, pr = binprec(p, &op); if (!pr || pr < minp) return l;
        next(p);
        node *r = op == O_EXP ? binary(p, pr) : binary(p, pr + 1);   /* ** asocia a la derecha */
        node *b = nn(p, op == O_LAND || op == O_LOR || op == O_NULLISH ? N_LOGIC : N_BIN); b->op = (u16)op; b->a = l; b->b = r; l = b; } }
static node *cond(P *p) {
    node *c = binary(p, 1);
    if (!eat(p, '?')) return c;
    node *n = nn(p, N_COND); n->a = c; int ni = p->no_in; p->no_in = 0; n->b = assign(p); p->no_in = ni; expect(p, ':'); n->c = assign(p); return n; }
static int assign_op(P *p, int *op) {
    if (p->t != T_PUNC) return 0;
    switch (p->pc) { case '=': *op = O_ASSIGN; return 1; case P2('+', '='): *op = O_ADD; return 1; case P2('-', '='): *op = O_SUB; return 1; case P2('*', '='): *op = O_MUL; return 1; case P2('/', '='): *op = O_DIV; return 1;
        case P2('%', '='): *op = O_MOD; return 1; case P3('*', '*', '='): *op = O_EXP; return 1; case P3('<', '<', '='): *op = O_SHL; return 1; case P3('>', '>', '='): *op = O_SAR; return 1; case P4('>', '>', '>', '='): *op = O_SHR; return 1;
        case P2('&', '='): *op = O_AND; return 1; case P2('|', '='): *op = O_OR; return 1; case P2('^', '='): *op = O_XOR; return 1;
        case P3('&', '&', '='): *op = O_LAND; return 1; case P3('|', '|', '='): *op = O_LOR; return 1; case P3('?', '?', '='): *op = O_NULLISH; return 1; }
    return 0; }
static node *assign(P *p) {
    if (isw(p, "yield") && p->fn && (p->fn->flags & FK_GEN)) {
        next(p); node *y = nn(p, N_YIELD);
        if (eat(p, '*')) { y->flags = 1; y->a = assign(p); }
        else if (!p->nl && !is(p, ')') && !is(p, ']') && !is(p, '}') && !is(p, ',') && !is(p, ';') && !is(p, ':') && p->t != T_EOF && !isw(p, "in")) y->a = assign(p);
        return y; }
    node *l = cond(p); int op;
    if (assign_op(p, &op)) {
        next(p); node *a = nn(p, N_ASSIGN); a->op = (u16)op;
        if (op == O_ASSIGN && (l->k == N_OBJ || l->k == N_ARR)) l = to_pattern(p, l);
        else if (l->k != N_ID && l->k != N_DOT && l->k != N_INDEX && l->k != N_PRIVDOT) perr(p, "asignacion a algo que no es una variable");
        a->a = l; a->b = assign(p); return a; }
    return l; }
static node *expr(P *p) {
    node *e = assign(p);
    if (!is(p, ',')) return e;
    node *s = nn(p, N_SEQ); node **t = &s->a; *t = e; t = &e->next;
    while (eat(p, ',')) { node *x = assign(p); *t = x; t = &x->next; }
    return s; }
/* expresion -> patron (en [a, b] = ... y en parametros de flecha) */
static node *to_pattern(P *p, node *e) {
    if (!e) return e;
    switch (e->k) {
    case N_ID: case N_DOT: case N_INDEX: case N_PRIVDOT: return e;
    case N_ASSIGN: if (e->op != O_ASSIGN) break; { node *d = nn(p, N_DEFAULT); d->a = to_pattern(p, e->a); d->b = e->b; d->next = e->next; return d; }
    case N_DEFAULT: return e;
    case N_ARR: { node *pt = nn(p, N_ARRPAT); node *h = 0, **t = &h;
        for (node *x = e->a; x; ) { node *nx = x->next; node *q; if (x->k == N_HOLE) q = x; else if (x->k == N_SPREAD) { q = nn(p, N_REST); q->a = to_pattern(p, x->a); } else q = to_pattern(p, x); q->next = 0; *t = q; t = &q->next; x = nx; }
        pt->a = h; pt->next = e->next; return pt; }
    case N_OBJ: { node *pt = nn(p, N_OBJPAT); node *h = 0, **t = &h;
        for (node *x = e->a; x; ) { node *nx = x->next; node *q;
            if (x->flags & 256) { q = nn(p, N_REST); q->a = to_pattern(p, x->b); }
            else { q = nn(p, N_PROPPAT); q->a = x->a; q->flags = x->flags & 1; q->b = to_pattern(p, x->b); }
            q->next = 0; *t = q; t = &q->next; x = nx; }
        pt->a = h; pt->next = e->next; return pt; }
    case N_ARRPAT: case N_OBJPAT: return e;
    default: break; }
    perr(p, "destino de asignacion no valido"); return e; }
static node *binding(P *p) {   /* patron de declaracion */
    if (eat(p, '[')) { node *pt = nn(p, N_ARRPAT); node *h = 0, **t = &h;
        while (!is(p, ']') && !p->failed) { node *e; if (is(p, ',')) e = nn(p, N_HOLE); else if (eat(p, P3('.', '.', '.'))) { e = nn(p, N_REST); e->a = binding(p); } else { e = binding(p); if (eat(p, '=')) { node *d = nn(p, N_DEFAULT); d->a = e; d->b = assign(p); e = d; } } *t = e; t = &e->next; if (!eat(p, ',')) break; }
        expect(p, ']'); pt->a = h; return pt; }
    if (eat(p, '{')) { node *pt = nn(p, N_OBJPAT); node *h = 0, **t = &h;
        while (!is(p, '}') && !p->failed) { node *e;
            if (eat(p, P3('.', '.', '.'))) { e = nn(p, N_REST); e->a = binding(p); }
            else { e = nn(p, N_PROPPAT); cbv nm = 0;
                if (eat(p, '[')) { e->a = assign(p); expect(p, ']'); e->flags = 1; }
                else if (p->t == T_NAME || p->t == T_STR) { nm = p->t == T_NAME ? p->v : 0; e->a = str_node(p, p->v); next(p); }
                else if (p->t == T_NUM) { e->a = nn(p, N_NUM); e->a->v = p->v; next(p); }
                else { perr(p, "patron no valido"); break; }
                if (eat(p, ':')) e->b = binding(p); else if (nm) { node *id = nn(p, N_ID); id->v = nm; e->b = id; } else perr(p, "se esperaba ':'");
                if (eat(p, '=')) { node *d = nn(p, N_DEFAULT); d->a = e->b; d->b = assign(p); e->b = d; } }
            *t = e; t = &e->next; if (!eat(p, ',')) break; }
        expect(p, '}'); pt->a = h; return pt; }
    return ident(p); }

/* ---------------- funciones y clases ---------------- */
static node *body_stmts(P *p) { node *h = 0, **t = &h; while (!is(p, '}') && !p->failed && p->t != T_EOF) { node *s = stmt(p); if (s) { *t = s; t = &s->next; } } return h; }
static node *func_rest(P *p, u32 flags, cbv name, long start) {   /* desde ( */
    fninfo *fi = aal(sizeof(fninfo)); fi->flags = flags; fi->name = name; fi->src = p->s; fi->src_start = (u32)start; fi->file = p->file;
    node *f = nn(p, N_FUNC); f->fn = fi; fninfo *ofn = p->fn; p->fn = fi; fi->sc = push_scope(p, 0); fi->sc->fn = fi; fi->sc->env = 1;
    int ostrict = p->strict;
    expect(p, '('); node *h = 0, **t = &h; int simple = 1, n = 0, len = -1;
    while (!is(p, ')') && !p->failed) { node *b; if (eat(p, P3('.', '.', '.'))) { b = nn(p, N_REST); b->a = binding(p); simple = 0; if (len < 0) len = n; } else { b = binding(p); if (b->k != N_ID) simple = 0; if (eat(p, '=')) { node *d = nn(p, N_DEFAULT); d->a = b; d->b = assign(p); b = d; simple = 0; if (len < 0) len = n; } } *t = b; t = &b->next; n++; if (!eat(p, ',')) break; }
    expect(p, ')');
    for (node *q = h; q; q = q->next) declare_pattern(p, q, VK_VAR);
    fi->params = h; fi->nparams = n; fi->simple = simple; fi->length = len < 0 ? n : len;
    if ((flags & FK_GET) && n) perr(p, "un get no lleva parametros");
    expect(p, '{');
    if (p->t == T_STR && cb_str_eqa(cb_s(p->v), "use strict")) { p->strict = 1; fi->flags |= FK_STRICT; }
    if (p->strict) fi->flags |= FK_STRICT;
    node *b = nn(p, N_BLOCK); b->a = body_stmts(p); fi->body = b; fi->src_end = (u32)p->te; expect(p, '}');
    p->strict = ostrict; pop_scope(p); p->fn = ofn; p->prev_expr = 1; return f; }
static node *class_expr(P *p, int decl) {
    long start = p->ts; next(p);   /* class */
    node *c = nn(p, N_CLASS); fninfo *ci = aal(sizeof(fninfo)); c->fn = ci; ci->src = p->s; ci->src_start = (u32)start; ci->file = p->file;
    if (p->t == T_NAME && !isw(p, "extends")) { ci->name = p->v; next(p); }
    else if (decl) perr(p, "la clase necesita nombre");
    int ostrict = p->strict; p->strict = 1;
    c->sc = push_scope(p, 4); if (ci->name) declare(p, c->sc, ci->name, VK_CONST);
    if (eatw(p, "extends")) c->b = call_member(p, primary(p));
    expect(p, '{'); node *h = 0, **t = &h; int oic = p->in_class; p->in_class = 1;
    while (!is(p, '}') && !p->failed) {
        if (eat(p, ';')) continue;
        node *m = nn(p, N_MEMBERDEF); long ms = p->ts; u32 fl = 0; int isasync = 0, isgen = 0, get = 0, set = 0;
        if (isw(p, "static")) { lexsave s; lsave(p, &s); next(p); if (is(p, '(') || is(p, '=') || is(p, ';') || is(p, '}')) lrest(p, &s); else { fl |= 4; if (is(p, '{')) { next(p);   /* bloque estatico */
                    fninfo *fi = aal(sizeof(fninfo)); fi->flags = FK_METHOD | FK_STATIC; fi->src = p->s; node *f = nn(p, N_FUNC); f->fn = fi; fninfo *ofn = p->fn; p->fn = fi; fi->sc = push_scope(p, 0); fi->sc->fn = fi; fi->sc->env = 1;
                    node *b = nn(p, N_BLOCK); b->a = body_stmts(p); fi->body = b; expect(p, '}'); pop_scope(p); p->fn = ofn; m->k = N_STATICBLOCK; m->b = f; m->flags = 4; *t = m; t = &m->next; continue; } } }
        if (isw(p, "async")) { lexsave s; lsave(p, &s); next(p); if (!is(p, '(') && !is(p, '=') && !is(p, ';') && !is(p, '}') && !p->nl) isasync = 1; else lrest(p, &s); }
        if (eat(p, '*')) isgen = 1;
        if (!isasync && !isgen && (isw(p, "get") || isw(p, "set"))) { lexsave s; lsave(p, &s); int g = isw(p, "get"); next(p); if (!is(p, '(') && !is(p, '=') && !is(p, ';') && !is(p, '}')) { if (g) get = 1; else set = 1; } else lrest(p, &s); }
        node *key;
        if (eat(p, '[')) { key = assign(p); expect(p, ']'); fl |= 1; }
        else if (p->t == T_PRIV) { key = str_node(p, p->v); fl |= 64; declare(p, c->sc, p->v, VK_CONST); next(p); }
        else if (p->t == T_NAME || p->t == T_STR) { key = str_node(p, p->v); next(p); }
        else if (p->t == T_NUM) { key = nn(p, N_NUM); key->v = p->v; next(p); }
        else { perr(p, "miembro de clase no valido"); break; }
        m->a = key;
        if (get || set || isasync || isgen || is(p, '(')) {
            int ctor = !(fl & 5) && key->k == N_STR && cb_str_eqa(cb_s(key->v), "constructor") && !get && !set;
            u32 ff = FK_METHOD | FK_STRICT | (get ? FK_GET : 0) | (set ? FK_SET : 0) | (isasync ? FK_ASYNC : 0) | (isgen ? FK_GEN : 0) | (fl & 4 ? FK_STATIC : 0) | (ctor ? FK_CTOR | (c->b ? FK_DERIVED : 0) : 0);
            m->b = func_rest(p, ff, key->k == N_STR ? key->v : 0, ms); fl |= get ? 16 : set ? 32 : 8; if (ctor) { fl |= 1024; } }
        else {   /* campo: su valor se evalua como un metodo (this = la instancia) */
            fl |= 128;
            if (eat(p, '=')) { fninfo *fi = aal(sizeof(fninfo)); fi->flags = FK_METHOD | FK_FIELDINIT | FK_STRICT | (fl & 4 ? FK_STATIC : 0); fi->src = p->s; node *f = nn(p, N_FUNC); f->fn = fi; fninfo *ofn = p->fn; p->fn = fi; fi->sc = push_scope(p, 0); fi->sc->fn = fi; fi->sc->env = 1;
                node *r = nn(p, N_RETURN); r->a = assign(p); fi->body = r; fi->flags |= FK_EXPR; pop_scope(p); p->fn = ofn; m->b = f; }
            semi(p); }
        m->flags = fl; *t = m; t = &m->next; }
    expect(p, '}'); p->in_class = oic; p->strict = ostrict; pop_scope(p);
    c->a = h; ci->src_end = (u32)p->ts; p->prev_expr = 1; return c; }

/* ---------------- sentencias ---------------- */
static node *var_decl(P *p, int kind, int in_for) {
    node *v = nn(p, N_VAR); v->op = (u16)kind; node *h = 0, **t = &h;
    do { node *d = nn(p, N_DECL); d->a = binding(p); declare_pattern(p, d->a, kind);
        if (eat(p, '=')) d->b = assign(p);
        else if (!in_for && (kind == VK_CONST || d->a->k != N_ID)) perr(p, "falta el valor inicial");
        *t = d; t = &d->next; } while (eat(p, ','));
    v->a = h; return v; }
static node *block(P *p) {   /* desde { */
    node *b = nn(p, N_BLOCK); expect(p, '{'); b->sc = push_scope(p, 1); b->a = body_stmts(p); expect(p, '}'); pop_scope(p); p->prev_expr = 0; return b; }
static int is_let_decl(P *p) { if (!isw(p, "let")) return 0; lexsave s; lsave(p, &s); next(p); int r = p->t == T_NAME || is(p, '[') || is(p, '{'); if (p->t == T_NAME && (isw(p, "in") || isw(p, "of") || isw(p, "instanceof"))) r = 0; lrest(p, &s); return r; }
static node *stmt(P *p) {
    if (p->failed) { p->t = T_EOF; return 0; }
    node *n;
    if (p->t == T_PUNC) {
        if (is(p, '{')) return block(p);
        if (eat(p, ';')) return nn(p, N_EMPTY); }
    if (p->t == T_NAME && !p->escaped) {
        if (isw(p, "var")) { next(p); n = var_decl(p, VK_VAR, 0); semi(p); return n; }
        if (isw(p, "const") || is_let_decl(p)) { int k = isw(p, "const") ? VK_CONST : VK_LET; next(p); n = var_decl(p, k, 0); semi(p); return n; }
        if (isw(p, "function") || (isw(p, "async") && ({ lexsave s; lsave(p, &s); next(p); int r = isw(p, "function") && !p->nl; lrest(p, &s); r; }))) {
            long start = p->ts; u32 fl = 0; if (eatw(p, "async")) fl |= FK_ASYNC; next(p); if (eat(p, '*')) fl |= FK_GEN;
            if (p->t != T_NAME) perr(p, "la funcion necesita nombre");
            cbv nm = p->v; next(p); node *f = func_rest(p, fl, nm, start);
            n = nn(p, N_FUNCDECL); n->a = f; n->v = nm;
            scope *s = p->sc->kind == 1 && p->fn ? p->sc : var_scope(p); if (p->sc->kind == 1 && !p->fn) s = p->sc;
            n->slot = declare(p, s, nm, s->kind == 1 ? VK_LET : VK_VAR); add_func(s, n); p->prev_expr = 0; return n; }
        if (isw(p, "class")) { node *c = class_expr(p, 1); n = nn(p, N_CLASSDECL); n->a = c; n->v = c->fn->name; declare(p, p->sc, c->fn->name, VK_LET); p->prev_expr = 0; return n; }
        if (isw(p, "if")) { next(p); n = nn(p, N_IF); expect(p, '('); n->a = expr(p); expect(p, ')'); p->prev_expr = 0; n->b = stmt(p); if (eatw(p, "else")) n->c = stmt(p); return n; }
        if (isw(p, "while")) { next(p); n = nn(p, N_WHILE); expect(p, '('); n->a = expr(p); expect(p, ')'); p->prev_expr = 0; n->b = stmt(p); return n; }
        if (isw(p, "do")) { next(p); n = nn(p, N_DOWHILE); n->a = stmt(p); if (!eatw(p, "while")) perr(p, "se esperaba while"); expect(p, '('); n->b = expr(p); expect(p, ')'); eat(p, ';'); p->prev_expr = 0; return n; }
        if (isw(p, "for")) {
            next(p); int isawait = eatw(p, "await"); expect(p, '('); scope *fs = push_scope(p, 1); node *init = 0;
            p->no_in = 1;
            if (is(p, ';')) {}
            else if (isw(p, "var")) { next(p); init = var_decl(p, VK_VAR, 1); }
            else if (isw(p, "const") || is_let_decl(p)) { int k = isw(p, "const") ? VK_CONST : VK_LET; next(p); init = var_decl(p, k, 1); }
            else init = expr(p);
            p->no_in = 0;
            if (isw(p, "of") || isw(p, "in")) {
                int of = isw(p, "of"); next(p); n = nn(p, of ? N_FOROF : N_FORIN); n->flags = (u32)isawait;
                if (init && init->k != N_VAR) init = to_pattern(p, init);
                n->a = init; n->b = of ? assign(p) : expr(p); expect(p, ')'); p->prev_expr = 0; n->c = stmt(p); n->sc = fs; pop_scope(p); return n; }
            n = nn(p, N_FOR); n->a = init; expect(p, ';'); if (!is(p, ';')) n->b = expr(p); expect(p, ';'); if (!is(p, ')')) n->c = expr(p); expect(p, ')'); p->prev_expr = 0; n->d = stmt(p); n->sc = fs; pop_scope(p); return n; }
        if (isw(p, "return")) { next(p); n = nn(p, N_RETURN); if (!p->fn) perr(p, "return fuera de una funcion"); if (!is(p, ';') && !is(p, '}') && !p->nl && p->t != T_EOF) n->a = expr(p); semi(p); return n; }
        if (isw(p, "break") || isw(p, "continue")) { int br = isw(p, "break"); next(p); n = nn(p, br ? N_BREAK : N_CONTINUE); if (p->t == T_NAME && !p->nl && !reserved(p->v)) { n->v = p->v; next(p); } semi(p); return n; }
        if (isw(p, "throw")) { next(p); n = nn(p, N_THROW); if (p->nl) perr(p, "salto de linea despues de throw"); n->a = expr(p); semi(p); return n; }
        if (isw(p, "try")) { next(p); n = nn(p, N_TRY); n->a = block(p);
            if (eatw(p, "catch")) { n->sc = push_scope(p, 3); if (eat(p, '(')) { n->b = binding(p); declare_pattern(p, n->b, VK_LET); expect(p, ')'); } n->c = block(p); pop_scope(p); }
            if (eatw(p, "finally")) n->d = block(p);
            if (!n->c && !n->d) perr(p, "try sin catch ni finally"); return n; }
        if (isw(p, "switch")) { next(p); n = nn(p, N_SWITCH); expect(p, '('); n->a = expr(p); expect(p, ')'); expect(p, '{'); n->sc = push_scope(p, 1); node *h = 0, **t = &h;
            while (!is(p, '}') && !p->failed) { node *c = nn(p, N_CASE); if (eatw(p, "default")) c->flags = 1; else { if (!eatw(p, "case")) { perr(p, "se esperaba case"); break; } c->a = expr(p); } expect(p, ':');
                node *bh = 0, **bt = &bh; while (!is(p, '}') && !isw(p, "case") && !isw(p, "default") && !p->failed) { node *s = stmt(p); if (s) { *bt = s; bt = &s->next; } } c->b = bh; *t = c; t = &c->next; }
            expect(p, '}'); pop_scope(p); n->b = h; p->prev_expr = 0; return n; }
        if (isw(p, "debugger")) { next(p); semi(p); return nn(p, N_EMPTY); }
        if (isw(p, "with")) { perr(p, "with no esta disponible"); return 0; }
        if (isw(p, "import") && p->module) { lexsave s; lsave(p, &s); next(p); if (!is(p, '(') && !is(p, '.')) {   /* import ... from "..." */
                n = nn(p, N_IMPORT); node *h = 0, **t = &h;
                if (p->t == T_STR) { n->v = p->v; next(p); semi(p); return n; }
                if (p->t == T_NAME && !isw(p, "from")) { node *sp = nn(p, N_PROP); sp->a = str_node(p, cb_atom(p->R, "default")); sp->b = ident(p); declare(p, p->sc, sp->b->v, VK_CONST); *t = sp; t = &sp->next; eat(p, ','); }
                if (eat(p, '*')) { eatw(p, "as"); node *sp = nn(p, N_PROP); sp->flags = 1; sp->b = ident(p); declare(p, p->sc, sp->b->v, VK_CONST); *t = sp; t = &sp->next; }
                else if (eat(p, '{')) { while (!is(p, '}') && !p->failed) { node *sp = nn(p, N_PROP); sp->a = str_node(p, p->v); next(p); if (eatw(p, "as")) sp->b = ident(p); else { sp->b = nn(p, N_ID); sp->b->v = sp->a->v; } declare(p, p->sc, sp->b->v, VK_CONST); *t = sp; t = &sp->next; if (!eat(p, ',')) break; } expect(p, '}'); }
                if (!eatw(p, "from")) perr(p, "se esperaba from"); n->v = p->v; next(p); semi(p); n->a = h; return n; }
            lrest(p, &s); }
        if (isw(p, "export") && p->module) { next(p); n = nn(p, N_EXPORT);
            if (eatw(p, "default")) { n->flags = 1; if (isw(p, "function") || isw(p, "class") || isw(p, "async")) { node *d = stmt(p); n->a = d; } else { n->b = assign(p); semi(p); } return n; }
            if (eat(p, '*')) { n->flags = 2; if (eatw(p, "as")) n->c = ident(p); eatw(p, "from"); n->v = p->v; next(p); semi(p); return n; }
            if (eat(p, '{')) { n->flags = 3; node *h = 0, **t = &h; while (!is(p, '}') && !p->failed) { node *sp = nn(p, N_PROP); sp->a = nn(p, N_ID); sp->a->v = p->v; next(p); if (eatw(p, "as")) { sp->b = str_node(p, p->v); next(p); } else sp->b = str_node(p, sp->a->v); *t = sp; t = &sp->next; if (!eat(p, ',')) break; } expect(p, '}'); n->c = h; if (eatw(p, "from")) { n->v = p->v; next(p); } semi(p); return n; }
            n->a = stmt(p); return n; }
        /* etiqueta */
        { lexsave s; lsave(p, &s); cbv nm = p->v; next(p); if (is(p, ':') && !reserved(nm)) { next(p); n = nn(p, N_LABEL); n->v = nm; p->prev_expr = 0; n->a = stmt(p); return n; } lrest(p, &s); }
    }
    n = nn(p, N_EXPR); n->a = expr(p); semi(p); p->prev_expr = 0; return n; }

/* ---------------- resolucion de nombres ---------------- */
typedef struct rctx { scope *sc; fninfo *fn; } rctx;
static cbv A_arguments_atom;
static void res(P *p, node *n, scope *sc);
static void res_list(P *p, node *n, scope *sc) { for (; n; n = n->next) res(p, n, sc); }
static fninfo *this_fn(scope *sc) { for (; sc; sc = sc->up) if (sc->kind == 0 && sc->fn && !(sc->fn->flags & FK_ARROW)) return sc->fn; return 0; }
/* busca un nombre: saltos (contando solo los ambitos que crean entorno) y posicion */
static void lookup(node *id, scope *sc) {
    int hops = 0;
    for (scope *s = sc; s; s = s->up) {
        if (s->kind == 2) { for (int i = 0; i < s->n; i++) if (s->names[i] == id->v && s->nkind[i] != VK_VAR) { id->hops = -2; return; } break; }
        if (s->env) { for (int i = 0; i < s->n; i++) if (s->names[i] == id->v) { id->hops = hops; id->slot = i; id->op = s->nkind[i]; return; } hops++; } }
    id->hops = -1; }
static void lookup_hidden(node *n, scope *sc, int which) {   /* this, new.target, arguments, objeto casa (super) */
    int hops = 0;
    for (scope *s = sc; s; s = s->up) {
        if (s->kind == 0 && s->fn && !(s->fn->flags & FK_ARROW)) { fninfo *f = s->fn;
            int *sl = which == 0 ? &f->this_slot : which == 1 ? &f->nt_slot : which == 2 ? &f->args_slot : &f->home_slot;
            if (*sl <= 0) *sl = declare(0, s, cb_num(-(double)(which + 1) - 100 * (double)(u64)(void *)f), 0) + 1;   /* nombre que nadie puede escribir */
            if (which == 0) f->uses_this = 1; if (which == 2) f->uses_args = 1;
            n->hops = hops; n->slot = *sl - 1; return; }
        if (s->env) hops++; }
    n->hops = -1; }
static void res(P *p, node *n, scope *sc) {
    if (!n) return;
    switch (n->k) {
    case N_ID:
        if (n->v == A_arguments_atom) { lookup(n, sc); if (n->hops < 0) { fninfo *f = this_fn(sc); if (f) lookup_hidden(n, sc, 2); } }
        else lookup(n, sc);
        return;
    case N_THIS: lookup_hidden(n, sc, 0); return;
    case N_NEWTARGET: lookup_hidden(n, sc, 1); return;
    case N_SUPER: { lookup_hidden(n, sc, 3); node t; memset(&t, 0, sizeof t); lookup_hidden(&t, sc, 0); n->flags = (u32)t.hops; n->op = (u16)t.slot; n->d = aal(sizeof(node)); lookup_hidden(n->d, sc, 1); return; }
    case N_FUNC: {
        fninfo *f = n->fn; scope *fs = f->sc;
        /* funciones con nombre propio en expresiones: el nombre se ve dentro */
        if ((n->flags & 1) && f->name) { int found = 0; for (int i = 0; i < fs->n; i++) if (fs->names[i] == f->name) found = 1; if (!found) declare(0, fs, f->name, 5); }
        for (node *q = f->params; q; q = q->next) res(p, q, fs);
        res(p, f->body, fs);
        for (int i = 0; i < fs->nf; i++) res(p, fs->funcs[i]->a, fs);
        return; }
    case N_CLASS: {
        scope *cs = n->sc; cs->env = cs->n > 0;
        res(p, n->b, cs);
        for (node *m = n->a; m; m = m->next) { if (m->flags & 1) res(p, m->a, cs); res(p, m->b, cs); }
        return; }
    case N_BLOCK: {
        scope *s = n->sc ? n->sc : sc; if (n->sc) { n->sc->env = n->sc->n > 0; }
        for (int i = 0; n->sc && i < n->sc->nf; i++) res(p, n->sc->funcs[i]->a, s);
        res_list(p, n->a, s); return; }
    case N_FUNCDECL: if (!n->a->fn->sc->up) {} return;   /* se resuelven con su ambito (lista funcs) */
    case N_CLASSDECL: res(p, n->a, sc); { node id; memset(&id, 0, sizeof id); id.v = n->v; lookup(&id, sc); n->hops = id.hops; n->slot = id.slot; } return;
    case N_FOR: case N_FORIN: case N_FOROF: { scope *s = n->sc; s->env = s->n > 0;
        if (n->k != N_FOR) res(p, n->b, sc);   /* el objeto se evalua fuera del ambito del for */
        res(p, n->a, s); if (n->k == N_FOR) { res(p, n->b, s); res(p, n->c, s); res(p, n->d, s); } else res(p, n->c, s); return; }
    case N_TRY: res(p, n->a, sc); if (n->sc) { n->sc->env = n->sc->n > 0; res(p, n->b, n->sc); res(p, n->c, n->sc); } res(p, n->d, sc); return;
    case N_SWITCH: res(p, n->a, sc); n->sc->env = n->sc->n > 0; for (int i = 0; i < n->sc->nf; i++) res(p, n->sc->funcs[i]->a, n->sc); for (node *c = n->b; c; c = c->next) { res(p, c->a, n->sc); res_list(p, c->b, n->sc); } return;
    case N_VAR: for (node *d = n->a; d; d = d->next) { res(p, d->a, sc); res(p, d->b, sc); } return;
    case N_PROP: case N_MEMBERDEF: if (n->flags & 1) res(p, n->a, sc); res(p, n->b, sc); return;
    case N_PROPPAT: if (n->flags & 1) res(p, n->a, sc); res(p, n->b, sc); return;
    case N_DOT: res(p, n->a, sc); return;
    case N_PRIVDOT: res(p, n->a, sc); lookup(n, sc); return;
    case N_PRIVIN: lookup(n, sc); return;
    case N_LABEL: res(p, n->a, sc); return;
    case N_IMPORT: for (node *q = n->a; q; q = q->next) res(p, q->b, sc); return;
    case N_EXPORT: res(p, n->a, sc); res(p, n->b, sc); for (node *q = n->c; q; q = q->next) res(p, q->a, sc); return;
    case N_TMPL: case N_TAGGED: res(p, n->b, sc); res_list(p, n->a, sc); return;
    case N_ARR: case N_ARRPAT: case N_OBJ: case N_OBJPAT: case N_SEQ: res_list(p, n->a, sc); return;
    case N_CALL: case N_NEW: res(p, n->a, sc); res_list(p, n->b, sc); return;
    case N_CASE: return;
    default: res(p, n->a, sc); res(p, n->b, sc); res(p, n->c, sc); res(p, n->d, sc); return; } }

node *cb_parse(cb_rt *R, const char *src, long len, const char *file, int module, char *err, int ecap) {
    P p; memset(&p, 0, sizeof p); p.R = R; p.s = src; p.n = len; p.line = 1; p.file = file; p.err = err; p.ecap = ecap; p.module = module; p.strict = module;
    A_arguments_atom = cb_atom(R, "arguments");
    node *prog = nn(&p, N_BLOCK);
    if (module) {   /* un modulo es como una funcion: su propio ambito */
        fninfo *fi = aal(sizeof(fninfo)); fi->flags = FK_ASYNC | FK_STRICT; fi->src = src; fi->file = file; p.fn = fi; fi->sc = push_scope(&p, 0); fi->sc->fn = fi; fi->sc->env = 1; prog->fn = fi; }
    else { prog->sc = push_scope(&p, 2); }
    next(&p);
    if (p.t == T_STR && cb_str_eqa(cb_s(p.v), "use strict")) p.strict = 1;
    node *h = 0, **t = &h;
    while (p.t != T_EOF && !p.failed) { node *s = stmt(&p); if (s) { *t = s; t = &s->next; } }
    if (p.failed) return 0;
    prog->a = h; prog->flags = (u32)p.strict;
    if (module) { scope *s = prog->fn->sc; for (int i = 0; i < s->nf; i++) res(&p, s->funcs[i]->a, s); res_list(&p, h, s); }
    else { scope *g = prog->sc; for (int i = 0; i < g->nf; i++) res(&p, g->funcs[i]->a, g); res_list(&p, h, g); }
    return prog; }
