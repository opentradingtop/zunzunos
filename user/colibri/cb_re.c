/* Colibri: expresiones regulares propias (vuelta atras) y los metodos de String que las usan.
   El patron se compila a un arbol de nodos dentro de un solo bloque de memoria; la coincidencia recorre el arbol con
   "continuaciones" en la pila de C. Las repeticiones de un solo caracter se resuelven sin recursion (.* sobre textos
   largos no agota la pila) y hay un limite de pasos contra los patrones explosivos. */
#include "cbi.h"
u32 cb_case(u32 c, int upper);
long cb_str_find(cb_str *s, cb_str *p, long from);
#define NF(name) static cbv name(cb_rt *R, cbv t, int argc, cbv *argv, cb_obj *fn)
#define ARG(i) ((i) < argc ? argv[i] : CB_UNDEF)
#define TRY(x) do { if ((x) == CB_EMPTY) return CB_EMPTY; } while (0)

enum { R_CHAR, R_ANY, R_CLASS, R_BOL, R_EOL, R_WB, R_NWB, R_GROUP, R_ALT, R_REP, R_BREF, R_LOOK, R_EMPTY };
typedef struct rnode rnode;
struct rnode { u8 type, neg, greedy, behind; int c; int idx; u32 min, max; rnode *body, *next; rnode **alts; int nalt; u32 *rg; int nrg; };
typedef struct { u8 *mem; u32 used, cap; int ncap; cbv names; int flags; rnode *root; int err; const char *emsg; } rprog;   /* flags: 1 g, 2 i, 4 m, 8 s, 16 u, 32 y, 64 d */
enum { F_G = 1, F_I = 2, F_M = 4, F_S = 8, F_U = 16, F_Y = 32, F_D = 64 };

/* ---------------- compilacion ---------------- */
typedef struct { rprog *p; cb_str *s; u32 i; cb_rt *R; } rc;
static void *ral(rc *c, u32 n) { n = (n + 7) & ~7u; if (c->p->used + n > c->p->cap) { c->p->err = 1; c->p->emsg = "patron demasiado grande"; return 0; } void *r = c->p->mem + c->p->used; c->p->used += n; memset(r, 0, n); return r; }
static rnode *rn(rc *c, int type) { rnode *n = ral(c, sizeof(rnode)); if (n) { n->type = (u8)type; n->max = 0xFFFFFFFF; n->greedy = 1; } return n; }
static int peek(rc *c) { return c->i < c->s->len ? c->s->c[c->i] : -1; }
static int hexd(int c) { return c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1; }
static void fail(rc *c, const char *m) { if (!c->p->err) { c->p->err = 1; c->p->emsg = m; } }
static rnode *alternation(rc *c);
/* clases: lista de rangos [a, b] */
typedef struct { u32 r[512]; int n; } rset;
static void radd(rset *s, u32 a, u32 b) { if (s->n < 510) { s->r[s->n++] = a; s->r[s->n++] = b; } }
static void add_class(rset *s, int k) {   /* d w s y sus negativas (D W S) */
    switch (k) {
    case 'd': radd(s, '0', '9'); break;
    case 'w': radd(s, '0', '9'); radd(s, 'A', 'Z'); radd(s, '_', '_'); radd(s, 'a', 'z'); break;
    case 's': radd(s, 9, 13); radd(s, 32, 32); radd(s, 0xA0, 0xA0); radd(s, 0x1680, 0x1680); radd(s, 0x2000, 0x200A); radd(s, 0x2028, 0x2029); radd(s, 0x202F, 0x202F); radd(s, 0x205F, 0x205F); radd(s, 0x3000, 0x3000); radd(s, 0xFEFF, 0xFEFF); break;
    case 'D': radd(s, 0, '0' - 1); radd(s, '9' + 1, 0x10FFFF); break;
    case 'W': radd(s, 0, '0' - 1); radd(s, '9' + 1, 'A' - 1); radd(s, 'Z' + 1, '_' - 1); radd(s, '_' + 1, 'a' - 1); radd(s, 'z' + 1, 0x10FFFF); break;
    case 'S': radd(s, 0, 8); radd(s, 14, 31); radd(s, 33, 0x9F); radd(s, 0xA1, 0x167F); radd(s, 0x1681, 0x1FFF); radd(s, 0x200B, 0x2027); radd(s, 0x202A, 0x202E); radd(s, 0x2030, 0x205E); radd(s, 0x2060, 0x2FFF); radd(s, 0x3001, 0xFEFE); radd(s, 0xFF00, 0x10FFFF); break; } }
/* \p{...}: aproximacion por bloques */
static void add_prop(rset *s, const char *name, int neg) {
    rset t; t.n = 0;
    if (!strcmp(name, "L") || !strcmp(name, "Letter") || !strcmp(name, "Alphabetic") || !strcmp(name, "Alpha")) { radd(&t, 'A', 'Z'); radd(&t, 'a', 'z'); radd(&t, 0xAA, 0xAA); radd(&t, 0xB5, 0xB5); radd(&t, 0xBA, 0xBA); radd(&t, 0xC0, 0xD6); radd(&t, 0xD8, 0xF6); radd(&t, 0xF8, 0x2C1); radd(&t, 0x370, 0x3FF); radd(&t, 0x400, 0x52F); radd(&t, 0x531, 0x587); radd(&t, 0x5D0, 0x5EA); radd(&t, 0x620, 0x64A); radd(&t, 0x904, 0x939); radd(&t, 0xE01, 0xE30); radd(&t, 0x1E00, 0x1FFF); radd(&t, 0x3040, 0x30FF); radd(&t, 0x3400, 0x4DBF); radd(&t, 0x4E00, 0x9FFF); radd(&t, 0xAC00, 0xD7A3); }
    else if (!strcmp(name, "Lu") || !strcmp(name, "Uppercase_Letter") || !strcmp(name, "Uppercase")) { radd(&t, 'A', 'Z'); radd(&t, 0xC0, 0xD6); radd(&t, 0xD8, 0xDE); radd(&t, 0x391, 0x3A9); radd(&t, 0x400, 0x42F); }
    else if (!strcmp(name, "Ll") || !strcmp(name, "Lowercase_Letter") || !strcmp(name, "Lowercase")) { radd(&t, 'a', 'z'); radd(&t, 0xDF, 0xF6); radd(&t, 0xF8, 0xFF); radd(&t, 0x3B1, 0x3C9); radd(&t, 0x430, 0x45F); }
    else if (!strcmp(name, "N") || !strcmp(name, "Nd") || !strcmp(name, "Number") || !strcmp(name, "Decimal_Number")) { radd(&t, '0', '9'); radd(&t, 0x660, 0x669); radd(&t, 0x6F0, 0x6F9); radd(&t, 0x966, 0x96F); radd(&t, 0xFF10, 0xFF19); }
    else if (!strcmp(name, "P") || !strcmp(name, "Punctuation")) { radd(&t, '!', '#'); radd(&t, '%', '*'); radd(&t, ',', '/'); radd(&t, ':', ';'); radd(&t, '?', '@'); radd(&t, '[', ']'); radd(&t, '_', '_'); radd(&t, '{', '{'); radd(&t, '}', '}'); radd(&t, 0xA1, 0xA1); radd(&t, 0xA7, 0xA7); radd(&t, 0xAB, 0xAB); radd(&t, 0xB6, 0xB7); radd(&t, 0xBB, 0xBB); radd(&t, 0xBF, 0xBF); radd(&t, 0x2010, 0x2027); radd(&t, 0x3001, 0x3003); }
    else if (!strcmp(name, "S") || !strcmp(name, "Symbol")) { radd(&t, '$', '$'); radd(&t, '+', '+'); radd(&t, '<', '>'); radd(&t, '^', '^'); radd(&t, '`', '`'); radd(&t, '|', '|'); radd(&t, '~', '~'); radd(&t, 0xA2, 0xA6); radd(&t, 0x20A0, 0x20CF); radd(&t, 0x2100, 0x2BFF); radd(&t, 0x1F300, 0x1FAFF); }
    else if (!strcmp(name, "Emoji") || !strcmp(name, "Extended_Pictographic") || !strcmp(name, "Emoji_Presentation")) { radd(&t, 0x2600, 0x27BF); radd(&t, 0x1F000, 0x1FAFF); }
    else if (!strncmp(name, "Script=Latin", 12) || !strncmp(name, "sc=Latin", 8)) { radd(&t, 'A', 'Z'); radd(&t, 'a', 'z'); radd(&t, 0xC0, 0x24F); radd(&t, 0x1E00, 0x1EFF); }
    else if (!strncmp(name, "Script=Greek", 12) || !strncmp(name, "sc=Greek", 8)) radd(&t, 0x370, 0x3FF);
    else if (!strncmp(name, "Script=Cyrillic", 15) || !strncmp(name, "sc=Cyrillic", 11)) radd(&t, 0x400, 0x52F);
    else if (!strncmp(name, "Script=Han", 10) || !strncmp(name, "sc=Han", 6)) { radd(&t, 0x3400, 0x4DBF); radd(&t, 0x4E00, 0x9FFF); }
    else radd(&t, 0, 0x10FFFF);
    if (!neg) { for (int i = 0; i < t.n; i += 2) radd(s, t.r[i], t.r[i + 1]); return; }
    u32 last = 0; for (int i = 0; i < t.n; i += 2) { if (t.r[i] > last) radd(s, last, t.r[i] - 1); last = t.r[i + 1] + 1; } if (last <= 0x10FFFF) radd(s, last, 0x10FFFF); }
static int cp_next(rc *c) {   /* lee un caracter del patron (pares sustitutos en modo u) */
    int ch = c->s->c[c->i++];
    if ((c->p->flags & F_U) && ch >= 0xD800 && ch < 0xDC00 && c->i < c->s->len && c->s->c[c->i] >= 0xDC00 && c->s->c[c->i] < 0xE000) ch = 0x10000 + ((ch - 0xD800) << 10) + (c->s->c[c->i++] - 0xDC00);
    return ch; }
/* escape: devuelve el caracter o -('d'...) para clases, -1000-n para \p */
static int escape(rc *c, rset *set) {
    if (c->i >= c->s->len) { fail(c, "\\ al final del patron"); return 0; }
    int e = c->s->c[c->i++];
    switch (e) {
    case 'd': case 'D': case 'w': case 'W': case 's': case 'S': if (set) add_class(set, e); return -e;
    case 'n': return '\n'; case 'r': return '\r'; case 't': return '\t'; case 'v': return '\v'; case 'f': return '\f';
    case '0': if (peek(c) >= '0' && peek(c) <= '9' && !(c->p->flags & F_U)) { int v = 0; c->i--; for (int k = 0; k < 3 && peek(c) >= '0' && peek(c) <= '7'; k++) v = v * 8 + (c->s->c[c->i++] - '0'); return v; } return 0;
    case 'c': { int l = peek(c); if ((l | 32) >= 'a' && (l | 32) <= 'z') { c->i++; return l % 32; } c->i--; return '\\'; }
    case 'x': { int a = c->i + 1 < c->s->len ? hexd(c->s->c[c->i]) : -1, b = a >= 0 ? hexd(c->s->c[c->i + 1]) : -1; if (a < 0 || b < 0) return 'x'; c->i += 2; return a * 16 + b; }
    case 'u': {
        if (peek(c) == '{' && (c->p->flags & F_U)) { c->i++; int v = 0; while (peek(c) >= 0 && peek(c) != '}') { int h = hexd(c->s->c[c->i++]); if (h < 0) { fail(c, "escape \\u{} no valido"); return 0; } v = v * 16 + h; } c->i++; return v; }
        int v = 0; for (int k = 0; k < 4; k++) { int h = c->i < c->s->len ? hexd(c->s->c[c->i]) : -1; if (h < 0) { c->i -= k; return 'u'; } v = v * 16 + h; c->i++; }
        if ((c->p->flags & F_U) && v >= 0xD800 && v < 0xDC00 && c->i + 5 < c->s->len && c->s->c[c->i] == '\\' && c->s->c[c->i + 1] == 'u') { int w = 0, ok = 1; for (int k = 0; k < 4; k++) { int h = hexd(c->s->c[c->i + 2 + k]); if (h < 0) ok = 0; w = w * 16 + (h < 0 ? 0 : h); } if (ok && w >= 0xDC00 && w < 0xE000) { c->i += 6; v = 0x10000 + ((v - 0xD800) << 10) + (w - 0xDC00); } }
        return v; }
    case 'p': case 'P': if (c->p->flags & F_U) { if (peek(c) != '{') { fail(c, "\\p sin {"); return 0; } c->i++; char nm[48]; int k = 0; while (peek(c) >= 0 && peek(c) != '}' && k < 47) nm[k++] = (char)c->s->c[c->i++]; nm[k] = 0; c->i++;
            char *eq = strchr(nm, '='); if (eq && (!strncmp(nm, "General_Category", 16) || !strncmp(nm, "gc", 2))) memmove(nm, eq + 1, strlen(eq)); if (set) add_prop(set, nm, e == 'P'); return -2000; } return e;
    default: if (e >= 0xD800 && e < 0xDC00) { c->i--; return cp_next(c); } return e; } }
static rnode *parse_class(rc *c) {   /* despues de [ */
    rnode *n = rn(c, R_CLASS); if (!n) return 0; rset *s = malloc(sizeof *s); s->n = 0;
    if (peek(c) == '^') { n->neg = 1; c->i++; }
    int first = 1;
    while (c->i < c->s->len && (peek(c) != ']' || (first && 0))) {
        first = 0; int a;
        if (peek(c) == '\\') { c->i++; a = escape(c, s); if (a < 0) continue; if (a == 'b' && c->s->c[c->i - 1] == 'b') a = 8; }
        else a = cp_next(c);
        if (peek(c) == '-' && c->i + 1 < c->s->len && c->s->c[c->i + 1] != ']') {
            c->i++; int b; if (peek(c) == '\\') { c->i++; b = escape(c, s); if (b < 0) { radd(s, (u32)a, (u32)a); radd(s, '-', '-'); continue; } } else b = cp_next(c);
            if (b < a) { fail(c, "Range out of order in character class"); free(s); return n; }
            radd(s, (u32)a, (u32)b); }
        else radd(s, (u32)a, (u32)a); }
    if (peek(c) != ']') fail(c, "Unterminated character class"); else c->i++;
    n->rg = ral(c, (u32)s->n * 4); if (n->rg) memcpy(n->rg, s->r, (u32)s->n * 4); n->nrg = s->n; free(s); return n; }
static int parse_int(rc *c, u32 *v) { if (!(peek(c) >= '0' && peek(c) <= '9')) return 0; u64 x = 0; while (peek(c) >= '0' && peek(c) <= '9') { x = x * 10 + (c->s->c[c->i++] - '0'); if (x > 0xFFFFFFF0ULL) x = 0xFFFFFFF0ULL; } *v = (u32)x; return 1; }
static rnode *atom(rc *c) {
    int ch = c->s->c[c->i];
    switch (ch) {
    case '^': c->i++; return rn(c, R_BOL);
    case '$': c->i++; return rn(c, R_EOL);
    case '.': c->i++; return rn(c, R_ANY);
    case '[': c->i++; return parse_class(c);
    case '(': {
        c->i++; rnode *g = rn(c, R_GROUP); if (!g) return 0; g->idx = -1;
        if (peek(c) == '?') { c->i++; int k = peek(c);
            if (k == ':') { c->i++; }
            else if (k == '=' || k == '!') { c->i++; g->type = R_LOOK; g->neg = k == '!'; }
            else if (k == '<' && c->i + 1 < c->s->len && (c->s->c[c->i + 1] == '=' || c->s->c[c->i + 1] == '!')) { g->type = R_LOOK; g->behind = 1; g->neg = c->s->c[c->i + 1] == '!'; c->i += 2; }
            else if (k == '<') { c->i++; cb_str *nm = cb_str_new(0); u32 st = c->i; while (peek(c) >= 0 && peek(c) != '>') c->i++; nm = cb_substr(c->s, st, c->i); c->i++;
                g->idx = ++c->p->ncap; if (!cb_isobj(c->p->names)) c->p->names = cb_objv(cb_new_array(c->R, 0)); cb_arr_set(c->R, cb_o(c->p->names), (u32)g->idx, cb_strv(nm)); }
            else { fail(c, "Invalid group"); return g; } }
        else g->idx = ++c->p->ncap;
        g->body = alternation(c); if (peek(c) != ')') fail(c, "Unterminated group"); else c->i++; return g; }
    case '\\': {
        c->i++; if (c->i >= c->s->len) { fail(c, "\\ at end of pattern"); return 0; }
        int e = c->s->c[c->i];
        if (e == 'b') { c->i++; return rn(c, R_WB); } if (e == 'B') { c->i++; return rn(c, R_NWB); }
        if (e >= '1' && e <= '9') { u32 v; u32 save = c->i; parse_int(c, &v); rnode *b = rn(c, R_BREF); if (b) b->idx = (int)v; (void)save; return b; }
        if (e == 'k' && c->i + 1 < c->s->len && c->s->c[c->i + 1] == '<') { c->i += 2; u32 st = c->i; while (peek(c) >= 0 && peek(c) != '>') c->i++; cb_str *nm = cb_substr(c->s, st, c->i); c->i++; rnode *b = rn(c, R_BREF); if (b) { b->idx = -1; b->c = 0; b->rg = (u32 *)(void *)nm; } return b; }
        rset *s = malloc(sizeof *s); s->n = 0; int v = escape(c, s);
        if (v < 0) { rnode *n = rn(c, R_CLASS); if (n) { n->rg = ral(c, (u32)s->n * 4); if (n->rg) memcpy(n->rg, s->r, (u32)s->n * 4); n->nrg = s->n; } free(s); return n; }
        free(s); rnode *n = rn(c, R_CHAR); if (n) n->c = v; return n; }
    case '*': case '+': case '?': fail(c, "Nothing to repeat"); c->i++; return 0;
    case '{': { u32 save = c->i; c->i++; u32 v; if (parse_int(c, &v) && (peek(c) == '}' || peek(c) == ',')) { if (c->p->flags & F_U) { fail(c, "Nothing to repeat"); return 0; } } c->i = save + 1; rnode *n = rn(c, R_CHAR); if (n) n->c = '{'; return n; }
    default: { rnode *n = rn(c, R_CHAR); if (n) n->c = cp_next(c); return n; } } }
static rnode *sequence(rc *c) {
    rnode *head = 0, **tail = &head;
    while (c->i < c->s->len && !c->p->err) {
        int ch = peek(c); if (ch == '|' || ch == ')') break;
        rnode *a = atom(c); if (!a) { if (c->p->err) break; continue; }
        /* cuantificador */
        int q = peek(c); u32 mn = 1, mx = 1; int isq = 0;
        if (q == '*') { mn = 0; mx = 0xFFFFFFFF; isq = 1; c->i++; }
        else if (q == '+') { mn = 1; mx = 0xFFFFFFFF; isq = 1; c->i++; }
        else if (q == '?') { mn = 0; mx = 1; isq = 1; c->i++; }
        else if (q == '{') { u32 save = c->i; c->i++; u32 v; if (parse_int(c, &v)) { mn = mx = v; if (peek(c) == ',') { c->i++; if (!parse_int(c, &mx)) mx = 0xFFFFFFFF; } if (peek(c) == '}') { c->i++; isq = 1; if (mx < mn) fail(c, "numbers out of order in {} quantifier"); } else c->i = save; } else c->i = save; }
        if (isq) { if (a->type == R_BOL || a->type == R_EOL || a->type == R_WB || a->type == R_NWB || (a->type == R_LOOK && (c->p->flags & F_U))) { fail(c, "Nothing to repeat"); break; }
            rnode *r = rn(c, R_REP); if (!r) break; r->min = mn; r->max = mx; r->body = a; if (peek(c) == '?') { r->greedy = 0; c->i++; } a = r; }
        *tail = a; tail = &a->next; }
    if (!head) head = rn(c, R_EMPTY);
    return head; }
static rnode *alternation(rc *c) {
    rnode *first = sequence(c); if (peek(c) != '|') return first;
    rnode *alt = rn(c, R_ALT); if (!alt) return first; rnode *list[256]; int n = 0; list[n++] = first;
    while (peek(c) == '|' && !c->p->err) { c->i++; if (n < 256) list[n++] = sequence(c); }
    alt->alts = ral(c, (u32)n * sizeof(rnode *)); if (alt->alts) memcpy(alt->alts, list, n * sizeof(rnode *)); alt->nalt = n; return alt; }
static rprog *compile(cb_rt *R, cb_str *pat, int flags) {
    pat = cb_flat(pat); rprog *p = cb_blob(sizeof(rprog)); p->flags = flags; p->names = CB_UNDEF; p->cap = pat->len * (sizeof(rnode) + 64) + 4096; p->mem = cb_raw(p->cap);
    rc c = { p, pat, 0, R }; p->root = alternation(&c);
    if (!p->err && c.i < pat->len) fail(&c, peek(&c) == ')' ? "Unmatched ')'" : "Invalid regular expression");
    return p; }

/* ---------------- coincidencia ---------------- */
typedef struct cont { int kind; rnode *n; int a; u32 b; struct cont *up; } cont;
enum { K_SEQ, K_CLOSE, K_REP, K_ENDAT };
typedef struct { rprog *p; const u16 *s; u32 len; int *caps; u32 end; long steps; int bail; } mctx;
static int m_seq(mctx *m, rnode *n, u32 pos, cont *k);
static int m_cont(mctx *m, cont *k, u32 pos);
static u32 fold(u32 c) { return c < 128 ? (c >= 'A' && c <= 'Z' ? c + 32 : c) : cb_case(c, 0); }
static int cp_at(mctx *m, u32 pos, u32 *len) { u32 c = m->s[pos]; *len = 1; if ((m->p->flags & F_U) && c >= 0xD800 && c < 0xDC00 && pos + 1 < m->len && m->s[pos + 1] >= 0xDC00 && m->s[pos + 1] < 0xE000) { *len = 2; return (int)(0x10000 + ((c - 0xD800) << 10) + (m->s[pos + 1] - 0xDC00)); } return (int)c; }
static int in_class(mctx *m, rnode *n, u32 c) {
    int in = 0; for (int i = 0; i < n->nrg; i += 2) if (c >= n->rg[i] && c <= n->rg[i + 1]) { in = 1; break; }
    if (!in && (m->p->flags & F_I)) { u32 l = fold(c), u = c < 128 ? (c >= 'a' && c <= 'z' ? c - 32 : c) : cb_case(c, 1); for (int i = 0; i < n->nrg; i += 2) if ((l >= n->rg[i] && l <= n->rg[i + 1]) || (u >= n->rg[i] && u <= n->rg[i + 1])) { in = 1; break; } }
    return in != n->neg; }
static int isword(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }
/* un nodo simple en pos: longitud consumida o -1 */
static int simple(mctx *m, rnode *n, u32 pos) {
    switch (n->type) {
    case R_CHAR: { if (pos >= m->len) return -1; u32 l; u32 c = (u32)cp_at(m, pos, &l); if (c == (u32)n->c || ((m->p->flags & F_I) && fold(c) == fold((u32)n->c))) return (int)l; return -1; }
    case R_ANY: { if (pos >= m->len) return -1; u32 l; int c = cp_at(m, pos, &l); if (!(m->p->flags & F_S) && (c == '\n' || c == '\r' || c == 0x2028 || c == 0x2029)) return -1; return (int)l; }
    case R_CLASS: { if (pos >= m->len) return -1; u32 l; int c = cp_at(m, pos, &l); return in_class(m, n, (u32)c) ? (int)l : -1; }
    case R_BOL: return pos == 0 || ((m->p->flags & F_M) && (m->s[pos - 1] == '\n' || m->s[pos - 1] == '\r' || m->s[pos - 1] == 0x2028 || m->s[pos - 1] == 0x2029)) ? 0 : -1;
    case R_EOL: return pos == m->len || ((m->p->flags & F_M) && (m->s[pos] == '\n' || m->s[pos] == '\r' || m->s[pos] == 0x2028 || m->s[pos] == 0x2029)) ? 0 : -1;
    case R_WB: case R_NWB: { int a = pos > 0 && isword(m->s[pos - 1]), b = pos < m->len && isword(m->s[pos]); return ((a != b) == (n->type == R_WB)) ? 0 : -1; }
    case R_EMPTY: return 0; }
    return -2; }
static int is_simple_atom(rnode *n) { return n->type == R_CHAR || n->type == R_ANY || n->type == R_CLASS; }
static int m_rep(mctx *m, rnode *n, u32 count, u32 start, u32 pos, cont *k) {
    if (count > n->min && pos == start) return 0;   /* repeticion vacia: no seguir */
    if (n->greedy) {
        if (count < n->max) { cont c = { K_REP, n, (int)count + 1, pos, k }; int sv[2 * 64]; int nc = m->p->ncap < 63 ? m->p->ncap : 63; memcpy(sv, m->caps + 2, nc * 2 * sizeof(int));
            if (m_seq(m, n->body, pos, &c)) return 1; memcpy(m->caps + 2, sv, nc * 2 * sizeof(int)); if (m->bail) return 0; }
        if (count >= n->min) return m_seq(m, n->next, pos, k);
        return 0; }
    if (count >= n->min && m_seq(m, n->next, pos, k)) return 1;
    if (m->bail) return 0;
    if (count < n->max) { cont c = { K_REP, n, (int)count + 1, pos, k }; return m_seq(m, n->body, pos, &c); }
    return 0; }
static int m_seq(mctx *m, rnode *n, u32 pos, cont *k) {
    for (;;) {
        if (++m->steps > 30000000) { m->bail = 1; return 0; }
        if (!n) return m_cont(m, k, pos);
        int r = simple(m, n, pos);
        if (r >= 0) { pos += (u32)r; n = n->next; continue; }
        if (r == -1) return 0;
        switch (n->type) {
        case R_GROUP: {
            if (n->idx < 0) { cont c = { K_SEQ, n->next, 0, 0, k }; return m_seq(m, n->body, pos, &c); }
            int os = m->caps[2 * n->idx], oe = m->caps[2 * n->idx + 1]; m->caps[2 * n->idx] = (int)pos;
            cont after = { K_SEQ, n->next, 0, 0, k }, close = { K_CLOSE, 0, n->idx, 0, &after };
            if (m_seq(m, n->body, pos, &close)) return 1; m->caps[2 * n->idx] = os; m->caps[2 * n->idx + 1] = oe; return 0; }
        case R_ALT: { for (int i = 0; i < n->nalt; i++) { cont c = { K_SEQ, n->next, 0, 0, k }; if (m_seq(m, n->alts[i], pos, &c)) return 1; if (m->bail) return 0; } return 0; }
        case R_REP:
            if (is_simple_atom(n->body)) {   /* sin recursion por caracter */
                u32 cnt = 0, p = pos; u32 lens[1]; (void)lens;
                if (n->greedy) {
                    u32 positions_cap = 64; u32 stackpos[64]; u32 *pp = stackpos; int heap = 0;
                    while (cnt < n->max) { int l = simple(m, n->body, p); if (l < 0) break; if (cnt + 1 >= positions_cap) { u32 nc = positions_cap * 4; u32 *np = malloc(nc * 4); memcpy(np, pp, positions_cap * 4); if (heap) free(pp); pp = np; heap = 1; positions_cap = nc; } pp[cnt] = p; p += (u32)l; cnt++; if (l == 0) break; }
                    pp[cnt] = p;
                    for (long i = (long)cnt; i >= (long)n->min; i--) { if (m_seq(m, n->next, pp[i], k)) { if (heap) free(pp); return 1; } if (m->bail) break; }
                    if (heap) free(pp); return 0; }
                for (;;) { if (cnt >= n->min && m_seq(m, n->next, p, k)) return 1; if (m->bail || cnt >= n->max) return 0; int l = simple(m, n->body, p); if (l < 0) return 0; p += (u32)l; cnt++; } }
            return m_rep(m, n, 0, 0xFFFFFFFF, pos, k);
        case R_BREF: {
            int idx = n->idx;
            if (idx < 0) { cb_str *nm = (cb_str *)(void *)n->rg; idx = 0; if (cb_isobj(m->p->names)) { cb_obj *na = cb_o(m->p->names); for (u32 i = 0; i < na->n1; i++) if (na->e[i] != CB_EMPTY && cb_isstr(na->e[i]) && cb_str_eq(cb_s(na->e[i]), nm)) idx = (int)i; } }
            if (idx > m->p->ncap) return 0;
            int s = m->caps[2 * idx], e = m->caps[2 * idx + 1]; if (s < 0 || e < 0) { n = n->next; continue; }
            u32 l = (u32)(e - s); if (pos + l > m->len) return 0;
            for (u32 i = 0; i < l; i++) { u32 a = m->s[s + i], b = m->s[pos + i]; if (a != b && !((m->p->flags & F_I) && fold(a) == fold(b))) return 0; }
            pos += l; n = n->next; continue; }
        case R_LOOK: {
            int sv[2 * 64]; int nc = m->p->ncap < 63 ? m->p->ncap : 63; memcpy(sv, m->caps + 2, nc * 2 * sizeof(int));
            int ok = 0;
            if (!n->behind) { ok = m_seq(m, n->body, pos, 0); }
            else { for (long j = (long)pos; j >= 0 && !ok; j--) { cont e = { K_ENDAT, 0, 0, pos, 0 }; ok = m_seq(m, n->body, (u32)j, &e); } }
            if (ok == n->neg) { memcpy(m->caps + 2, sv, nc * 2 * sizeof(int)); return 0; }
            if (n->neg) memcpy(m->caps + 2, sv, nc * 2 * sizeof(int));
            n = n->next; continue; }
        default: return 0; } } }
static int m_cont(mctx *m, cont *k, u32 pos) {
    if (!k) { m->end = pos; return 1; }
    switch (k->kind) {
    case K_SEQ: return m_seq(m, k->n, pos, k->up);
    case K_CLOSE: { int oe = m->caps[2 * k->a + 1]; m->caps[2 * k->a + 1] = (int)pos; if (m_cont(m, k->up, pos)) return 1; m->caps[2 * k->a + 1] = oe; return 0; }
    case K_REP: return m_rep(m, k->n, (u32)k->a, k->b, pos, k->up);
    case K_ENDAT: if (pos != k->b) return 0; m->end = pos; return 1; }
    return 0; }
/* busca desde 'from'; devuelve 1 y caps[] llenos (caps[0..1] la coincidencia entera) */
static int re_exec(rprog *p, cb_str *s, u32 from, int *caps, int sticky) {
    s = cb_flat(s); mctx m = { p, s->c, s->len, caps, 0, 0, 0 };
    for (u32 st = from; st <= s->len; st++) {
        for (int i = 0; i < 2 * (p->ncap + 1); i++) caps[i] = -1;
        if (m_seq(&m, p->root, st, 0)) { caps[0] = (int)st; caps[1] = (int)m.end; return 1; }
        if (sticky || m.bail) return 0;
        if ((p->flags & F_U) && st < s->len && s->c[st] >= 0xD800 && s->c[st] < 0xDC00) st++; }
    return 0; }

/* ---------------- RegExp ---------------- */
static cb_obj *recache;
void cb_re_reset(void) { recache = 0; }   /* al cerrar el motor: la cache vivia en su memoria */
static int parse_flags(cb_str *f) { int fl = 0; f = cb_flat(f); for (u32 i = 0; i < f->len; i++) { int b = 0; switch (f->c[i]) { case 'g': b = F_G; break; case 'i': b = F_I; break; case 'm': b = F_M; break; case 's': b = F_S; break; case 'u': case 'v': b = F_U; break; case 'y': b = F_Y; break; case 'd': b = F_D; break; default: return -1; } if (fl & b) return -1; fl |= b; } return fl; }
cbv cb_regexp_new(cb_rt *R, cbv pattern, cbv flags) {
    cb_str *pat = cb_isstr(pattern) ? cb_s(pattern) : cb_s(cb_mkstr(R, "(?:)")); if (!pat->len) pat = cb_s(cb_mkstr(R, "(?:)"));
    cb_str *fs = cb_isstr(flags) ? cb_s(flags) : cb_s(cb_mkstr(R, ""));
    int fl = parse_flags(fs); if (fl < 0) { char *u = cb_str_utf8z(fs, 0); cbv e = cb_throw(R, 3, "Invalid flags supplied to RegExp constructor '%s'", u); free(u); return e; }
    cbv key = cb_intern(R, cb_concat(cb_concat(pat, cb_s(cb_mkstr(R, "/"))), fs)); rprog *p = 0;
    if (!recache) { recache = cb_new_obj(R, 0); static cbv keep; keep = cb_objv(recache); cb_root(&keep); }
    cb_prop *pc = cb_own(recache, key); if (pc) p = (rprog *)(u64)pc->val;
    if (!p) { p = compile(R, pat, fl); if (p->err) { char *u = cb_str_utf8z(pat, 0); cbv e = cb_throw(R, 3, "Invalid regular expression: /%s/: %s", u, p->emsg); free(u); return e; } if (recache->np < 2000) cb_add_prop(R, recache, key, (cbv)(u64)p, PF_W); }
    cb_obj *o = cb_new_obj(R, R->RegExpP); o->cls = OC_REGEXP; o->p1 = p; o->v1 = cb_strv(pat); o->v2 = cb_strv(fs);
    cb_add_prop(R, o, A_lastIndex, cb_num(0), PF_W); return cb_objv(o); }
static cb_obj *re_this(cb_rt *R, cbv t) { if (!cb_isobj(t) || cb_o(t)->cls != OC_REGEXP) { cb_throw(R, 1, "RegExp method called on incompatible receiver"); return 0; } return cb_o(t); }
NF(RegExp_ctor) {
    cbv pat = ARG(0), fl = ARG(1);
    if (cb_isobj(pat) && cb_o(pat)->cls == OC_REGEXP) { if (cb_isundef(R->nt) && cb_isundef(fl)) return pat; if (cb_isundef(fl)) fl = cb_o(pat)->v2; pat = cb_o(pat)->v1; }
    else { if (cb_isundef(pat)) pat = cb_mkstr(R, ""); else { pat = cb_to_str(R, pat); TRY(pat); } }
    if (!cb_isundef(fl)) { fl = cb_to_str(R, fl); TRY(fl); }
    cbv r = cb_regexp_new(R, pat, fl); TRY(r);
    if (!cb_isundef(R->nt) && R->nt != cb_objv(fn)) { cbv p = cb_get(R, R->nt, A_prototype); TRY(p); if (cb_isobj(p)) cb_o(r)->proto = cb_o(p); }
    return r; }
static cbv match_obj(cb_rt *R, rprog *p, cb_str *s, int *caps) {
    cb_obj *a = cb_new_array(R, 0);
    for (int i = 0; i <= p->ncap; i++) cb_arr_push(R, a, caps[2 * i] >= 0 && caps[2 * i + 1] >= 0 ? cb_strv(cb_substr(s, (u32)caps[2 * i], (u32)caps[2 * i + 1])) : CB_UNDEF);
    cb_add_prop(R, a, A_index, cb_num(caps[0]), PF_WEC); cb_add_prop(R, a, A_input, cb_strv(s), PF_WEC);
    cbv groups = CB_UNDEF;
    if (cb_isobj(p->names)) { cb_obj *g = cb_new_obj(R, 0); cb_obj *na = cb_o(p->names); for (u32 i = 1; i < na->n1 && (int)i <= p->ncap; i++) if (na->e[i] != CB_EMPTY && cb_isstr(na->e[i])) cb_add_prop(R, g, cb_intern(R, cb_s(na->e[i])), a->e[i], PF_WEC); groups = cb_objv(g); }
    cb_add_prop(R, a, A_groups, groups, PF_WEC);
    if (p->flags & F_D) { cb_obj *ind = cb_new_array(R, 0); for (int i = 0; i <= p->ncap; i++) { if (caps[2 * i] >= 0) { cbv pr[2] = { cb_num(caps[2 * i]), cb_num(caps[2 * i + 1]) }; cb_arr_push(R, ind, cb_list_to_array(R, 2, pr)); } else cb_arr_push(R, ind, CB_UNDEF); } cb_add_prop(R, a, cb_atom(R, "indices"), cb_objv(ind), PF_WEC); }
    return cb_objv(a); }
/* exec con lastIndex: devuelve el array o null; caps opcional para quien llama desde C */
static cbv re_exec_js(cb_rt *R, cb_obj *re, cb_str *s, int *capsout) {
    rprog *p = re->p1; int glob = p->flags & (F_G | F_Y); double li = 0;
    if (glob) { cbv l = cb_get(R, cb_objv(re), A_lastIndex); TRY(l); li = cb_to_int(R, l); if (R->has_exc) return CB_EMPTY; if (li > s->len || li < 0) { cb_set(R, cb_objv(re), A_lastIndex, cb_num(0)); return CB_NULL; } }
    int capbuf[2 * 64 + 2]; int *caps = capsout ? capsout : capbuf; int ncap = p->ncap; int *big = 0; if (ncap > 63) { big = malloc(sizeof(int) * 2 * (ncap + 1)); caps = big; }
    int ok = re_exec(p, s, (u32)li, caps, (p->flags & F_Y) != 0);
    if (!ok) { if (glob) cb_set(R, cb_objv(re), A_lastIndex, cb_num(0)); free(big); return CB_NULL; }
    if (glob) cb_set(R, cb_objv(re), A_lastIndex, cb_num(caps[1]));
    cbv r = match_obj(R, p, cb_flat(s), caps); if (capsout && big) memcpy(capsout, big, sizeof(int) * 2 * 64); free(big); return r; }
NF(RP_exec) { cb_obj *re = re_this(R, t); if (!re) return CB_EMPTY; cbv s = cb_to_str(R, ARG(0)); TRY(s); return re_exec_js(R, re, cb_s(s), 0); }
NF(RP_test) { cb_obj *re = re_this(R, t); if (!re) { /* objetos con exec propio */ return CB_EMPTY; } cbv s = cb_to_str(R, ARG(0)); TRY(s); cbv r = re_exec_js(R, re, cb_s(s), 0); TRY(r); return cb_bool(!cb_isnull(r)); }
NF(RP_toString) { if (!cb_isobj(t)) return cb_throw(R, 1, "RegExp.prototype.toString called on non-object"); cbv src = cb_getp(R, t, "source"); TRY(src); cbv fl = cb_getp(R, t, "flags"); TRY(fl);
    src = cb_to_str(R, src); TRY(src); fl = cb_to_str(R, fl); TRY(fl); return cb_strv(cb_concat(cb_concat(cb_concat(cb_s(cb_mkstr(R, "/")), cb_s(src)), cb_s(cb_mkstr(R, "/"))), cb_s(fl))); }
NF(RP_source) { if (cb_isobj(t) && cb_o(t) == R->RegExpP) return cb_mkstr(R, "(?:)"); cb_obj *re = re_this(R, t); if (!re) return CB_EMPTY;
    cb_str *s = cb_flat(cb_s(re->v1)); cb_str *o = cb_str_new(s->len * 2 + 1); u32 k = 0; int cls = 0; for (u32 i = 0; i < s->len; i++) { u16 c = s->c[i]; if (c == '\\' && i + 1 < s->len) { o->c[k++] = c; o->c[k++] = s->c[++i]; continue; } if (c == '[') cls = 1; else if (c == ']') cls = 0; if (c == '/' && !cls) { o->c[k++] = '\\'; o->c[k++] = '/'; } else if (c == '\n') { o->c[k++] = '\\'; o->c[k++] = 'n'; } else o->c[k++] = c; } o->len = k; return cb_strv(o); }
NF(RP_flags) { if (!cb_isobj(t)) return cb_throw(R, 1, "RegExp.prototype.flags getter called on non-object"); char b[10]; int k = 0; const char *n[] = { "hasIndices", "global", "ignoreCase", "multiline", "dotAll", "unicode", "sticky" }; const char *c = "dgimsuy";
    for (int i = 0; i < 7; i++) { cbv v = cb_getp(R, t, n[i]); TRY(v); if (cb_truthy(v)) b[k++] = c[i]; } b[k] = 0; return cb_mkstr(R, b); }
static cbv flagget(cb_rt *R, cbv t, int f) { if (cb_isobj(t) && cb_o(t) == R->RegExpP) return CB_UNDEF; cb_obj *re = re_this(R, t); if (!re) return CB_EMPTY; return cb_bool(((rprog *)re->p1)->flags & f); }
NF(RP_global) { return flagget(R, t, F_G); } NF(RP_ignoreCase) { return flagget(R, t, F_I); } NF(RP_multiline) { return flagget(R, t, F_M); } NF(RP_dotAll) { return flagget(R, t, F_S); }
NF(RP_unicode) { return flagget(R, t, F_U); } NF(RP_sticky) { return flagget(R, t, F_Y); } NF(RP_hasIndices) { return flagget(R, t, F_D); }
NF(RegExp_escape) { cbv sv = cb_to_str(R, ARG(0)); TRY(sv); cb_str *s = cb_flat(cb_s(sv)); cb_str *o = cb_str_new(s->len * 2); u32 k = 0; for (u32 i = 0; i < s->len; i++) { u16 c = s->c[i]; if (c < 128 && strchr("^$\\.*+?()[]{}|/", c)) o->c[k++] = '\\'; o->c[k++] = c; } o->len = k; return cb_strv(o); }

/* ---------------- String: match, matchAll, replace, replaceAll, search, split ---------------- */
static cb_str *sthis(cb_rt *R, cbv t) { if (cb_isnullish(t)) { cb_throw(R, 1, "String.prototype method called on null or undefined"); return 0; } cbv s = cb_to_str(R, t); return s == CB_EMPTY ? 0 : cb_flat(cb_s(s)); }
static int isre(cbv v) { return cb_isobj(v) && cb_o(v)->cls == OC_REGEXP; }
NF(SP_match) { cb_str *s = sthis(R, t); if (!s) return CB_EMPTY; cbv rx = ARG(0);
    if (!isre(rx)) { rx = cb_regexp_new(R, cb_isundef(rx) ? cb_mkstr(R, "") : cb_to_str(R, rx), CB_UNDEF); TRY(rx); }
    cb_obj *re = cb_o(rx); rprog *p = re->p1;
    if (!(p->flags & F_G)) return re_exec_js(R, re, s, 0);
    cb_obj *out = cb_new_array(R, 0); int caps[130]; u32 pos = 0; int *cp = p->ncap > 63 ? malloc(sizeof(int) * 2 * (p->ncap + 1)) : caps;
    while (pos <= s->len && re_exec(p, s, pos, cp, (p->flags & F_Y) != 0)) { cb_arr_push(R, out, cb_strv(cb_substr(s, (u32)cp[0], (u32)cp[1]))); pos = cp[1] > cp[0] ? (u32)cp[1] : (u32)cp[1] + 1; }
    if (cp != caps) free(cp); cb_set(R, rx, A_lastIndex, cb_num(0)); return out->n1 ? cb_objv(out) : CB_NULL; }
NF(SP_matchAll) { cb_str *s = sthis(R, t); if (!s) return CB_EMPTY; cbv rx = ARG(0);
    if (isre(rx)) { if (!(((rprog *)cb_o(rx)->p1)->flags & F_G)) return cb_throw(R, 1, "String.prototype.matchAll called with a non-global RegExp argument"); }
    else { rx = cb_regexp_new(R, cb_isundef(rx) ? cb_mkstr(R, "") : cb_to_str(R, rx), cb_mkstr(R, "g")); TRY(rx); }
    rprog *p = cb_o(rx)->p1; cb_obj *out = cb_new_array(R, 0); int caps[130]; u32 pos = 0; cbv li = cb_get(R, rx, A_lastIndex); if (cb_isnum(li)) pos = (u32)cb_to_int(R, li);
    int *cp = p->ncap > 63 ? malloc(sizeof(int) * 2 * (p->ncap + 1)) : caps;
    while (pos <= s->len && re_exec(p, s, pos, cp, 0)) { cb_arr_push(R, out, match_obj(R, p, s, cp)); pos = cp[1] > cp[0] ? (u32)cp[1] : (u32)cp[1] + 1; }
    if (cp != caps) free(cp); cbv vals = cb_getp(R, cb_objv(out), "values"); TRY(vals); return cb_call_v(R, vals, cb_objv(out), 0, 0); }
/* $-sustituciones */
static cb_str *expand(cb_rt *R, cb_str *rep, cb_str *s, int *caps, int ncap, cbv groups) {
    rep = cb_flat(rep); cb_str *acc = cb_str_ascii("", 0); u32 last = 0;
    for (u32 i = 0; i < rep->len; i++) {
        if (rep->c[i] != '$' || i + 1 >= rep->len) continue;
        u16 c = rep->c[i + 1]; cb_str *ins = 0; u32 skip = 2;
        if (c == '$') ins = cb_str_ascii("$", 1);
        else if (c == '&') ins = cb_substr(s, (u32)caps[0], (u32)caps[1]);
        else if (c == '`') ins = cb_substr(s, 0, (u32)caps[0]);
        else if (c == '\'') ins = cb_substr(s, (u32)caps[1], s->len);
        else if (c >= '0' && c <= '9') { int n = c - '0'; if (i + 2 < rep->len && rep->c[i + 2] >= '0' && rep->c[i + 2] <= '9' && n * 10 + (rep->c[i + 2] - '0') <= ncap) { n = n * 10 + (rep->c[i + 2] - '0'); skip = 3; }
            if (n >= 1 && n <= ncap) { ins = caps[2 * n] >= 0 ? cb_substr(s, (u32)caps[2 * n], (u32)caps[2 * n + 1]) : cb_str_ascii("", 0); } else continue; }
        else if (c == '<' && cb_isobj(groups)) { u32 j = i + 2; while (j < rep->len && rep->c[j] != '>') j++; if (j >= rep->len) continue; cbv nm = cb_intern(R, cb_substr(rep, i + 2, j)); cbv v = cb_get(R, groups, nm); ins = cb_isundef(v) || v == CB_EMPTY ? cb_str_ascii("", 0) : cb_s(cb_to_str(R, v)); skip = j - i + 1; }
        else continue;
        acc = cb_concat(cb_concat(acc, cb_substr(rep, last, i)), ins); last = i + skip; i += skip - 1; }
    return cb_concat(acc, cb_substr(rep, last, rep->len)); }
static cbv replace_impl(cb_rt *R, cbv t, int argc, cbv *argv, int all) {
    cb_str *s = sthis(R, t); if (!s) return CB_EMPTY; cbv pat = ARG(0), rep = ARG(1); int fnrep = cb_isfunc(rep); cb_str *reps = 0;
    if (!fnrep) { cbv r = cb_to_str(R, rep); TRY(r); reps = cb_s(r); }
    cb_str *acc = cb_str_ascii("", 0); u32 last = 0;
    if (isre(pat)) {
        rprog *p = cb_o(pat)->p1; int glob = (p->flags & F_G) != 0;
        if (all && !glob) return cb_throw(R, 1, "replaceAll must be called with a global RegExp");
        int caps[130]; int *cp = p->ncap > 63 ? malloc(sizeof(int) * 2 * (p->ncap + 1)) : caps; u32 pos = 0;
        if (!glob && (p->flags & F_Y)) { cbv li = cb_get(R, pat, A_lastIndex); pos = (u32)cb_to_int(R, li); }
        while (pos <= s->len && re_exec(p, s, pos, cp, (p->flags & F_Y) != 0)) {
            cb_str *ins;
            cbv groups = CB_UNDEF; if (cb_isobj(p->names)) { cbv mo = match_obj(R, p, s, cp); groups = cb_get(R, mo, A_groups); }
            if (fnrep) { cbv av[70]; int ac = 0; for (int i = 0; i <= p->ncap && ac < 66; i++) av[ac++] = cp[2 * i] >= 0 ? cb_strv(cb_substr(s, (u32)cp[2 * i], (u32)cp[2 * i + 1])) : CB_UNDEF; av[ac++] = cb_num(cp[0]); av[ac++] = cb_strv(s); if (cb_isobj(groups)) av[ac++] = groups;
                cbv r = cb_call_v(R, rep, CB_UNDEF, ac, av); if (r == CB_EMPTY) { if (cp != caps) free(cp); return r; } r = cb_to_str(R, r); if (r == CB_EMPTY) { if (cp != caps) free(cp); return r; } ins = cb_s(r); }
            else ins = expand(R, reps, s, cp, p->ncap, groups);
            acc = cb_concat(cb_concat(acc, cb_substr(s, last, (u32)cp[0])), ins); last = (u32)cp[1];
            if (!glob) { if (p->flags & F_Y) cb_set(R, pat, A_lastIndex, cb_num(cp[1])); break; }
            pos = cp[1] > cp[0] ? (u32)cp[1] : (u32)cp[1] + 1; }
        if (glob) cb_set(R, pat, A_lastIndex, cb_num(0));
        if (cp != caps) free(cp);
        return cb_strv(cb_concat(acc, cb_substr(s, last, s->len))); }
    cbv ps = cb_to_str(R, pat); TRY(ps); cb_str *needle = cb_flat(cb_s(ps)); long pos = 0;
    for (;;) {
        long at = cb_str_find(s, needle, pos); if (at < 0) break;
        int caps[2] = { (int)at, (int)(at + needle->len) }; cb_str *ins;
        if (fnrep) { cbv av[3] = { cb_strv(needle), cb_num((double)at), cb_strv(s) }; cbv r = cb_call_v(R, rep, CB_UNDEF, 3, av); TRY(r); r = cb_to_str(R, r); TRY(r); ins = cb_s(r); }
        else ins = expand(R, reps, s, caps, 0, CB_UNDEF);
        acc = cb_concat(cb_concat(acc, cb_substr(s, (u32)last, (u32)at)), ins); last = (u32)(at + needle->len);
        if (!all) break; pos = needle->len ? at + needle->len : at + 1; if (pos > (long)s->len) break; }
    return cb_strv(cb_concat(acc, cb_substr(s, last, s->len))); }
NF(SP_replace) { return replace_impl(R, t, argc, argv, 0); }
NF(SP_replaceAll) { return replace_impl(R, t, argc, argv, 1); }
NF(SP_search) { cb_str *s = sthis(R, t); if (!s) return CB_EMPTY; cbv rx = ARG(0); if (!isre(rx)) { rx = cb_regexp_new(R, cb_isundef(rx) ? cb_mkstr(R, "") : cb_to_str(R, rx), CB_UNDEF); TRY(rx); }
    rprog *p = cb_o(rx)->p1; int caps[130]; int *cp = p->ncap > 63 ? malloc(sizeof(int) * 2 * (p->ncap + 1)) : caps; int r = re_exec(p, s, 0, cp, (p->flags & F_Y) != 0) ? cp[0] : -1; if (cp != caps) free(cp); return cb_num(r); }
NF(SP_split) {
    cb_str *s = sthis(R, t); if (!s) return CB_EMPTY; cbv sep = ARG(0); double lim = cb_isundef(ARG(1)) ? 4294967295.0 : (double)cb_to_u32(cb_to_num(R, ARG(1)));
    cb_obj *out = cb_new_array(R, 0); if (lim == 0) return cb_objv(out);
    if (cb_isundef(sep)) { cb_arr_push(R, out, cb_strv(s)); return cb_objv(out); }
    if (isre(sep)) {
        rprog *p = cb_o(sep)->p1; int caps[130]; int *cp = p->ncap > 63 ? malloc(sizeof(int) * 2 * (p->ncap + 1)) : caps;
        if (!s->len) { if (!re_exec(p, s, 0, cp, 1)) cb_arr_push(R, out, cb_strv(s)); if (cp != caps) free(cp); return cb_objv(out); }
        u32 last = 0, q = 0;
        while (q < s->len) {
            if (!re_exec(p, s, q, cp, 1)) { q++; continue; }
            u32 e = (u32)cp[1]; if (e == last || (e == (u32)cp[0] && cp[0] == (int)q && e >= s->len)) { q++; continue; }
            if (e == q && cp[0] == (int)q && e == last) { q++; continue; }
            if ((u32)cp[0] >= s->len) break;
            if (cp[1] == cp[0] && (u32)cp[0] == last) { q++; continue; }
            cb_arr_push(R, out, cb_strv(cb_substr(s, last, (u32)cp[0]))); if (out->n1 >= lim) break;
            for (int i = 1; i <= p->ncap; i++) { cb_arr_push(R, out, cp[2 * i] >= 0 ? cb_strv(cb_substr(s, (u32)cp[2 * i], (u32)cp[2 * i + 1])) : CB_UNDEF); if (out->n1 >= lim) break; }
            if (out->n1 >= lim) break;
            last = e; q = e > cp[0] ? e : e + 1; }
        if (out->n1 < lim) cb_arr_push(R, out, cb_strv(cb_substr(s, last, s->len)));
        if (cp != caps) free(cp); return cb_objv(out); }
    cbv sv = cb_to_str(R, sep); TRY(sv); cb_str *ss = cb_flat(cb_s(sv));
    if (!ss->len) { for (u32 i = 0; i < s->len && out->n1 < lim; i++) cb_arr_push(R, out, cb_strv(cb_substr(s, i, i + 1))); return cb_objv(out); }
    long pos = 0;
    for (;;) { long at = cb_str_find(s, ss, pos); if (at < 0) break; cb_arr_push(R, out, cb_strv(cb_substr(s, (u32)pos, (u32)at))); if (out->n1 >= lim) return cb_objv(out); pos = at + ss->len; }
    cb_arr_push(R, out, cb_strv(cb_substr(s, (u32)pos, s->len))); return cb_objv(out); }

void cb_lib3_init(cb_rt *R) {
    R->RegExpP = cb_new_obj(R, R->ObjectP);
    cbv c = cb_new_native(R, "RegExp", RegExp_ctor, 2); cb_obj *co = cb_o(c); co->fk |= 2;
    cb_add_prop(R, co, A_prototype, cb_objv(R->RegExpP), 0); cb_add_prop(R, R->RegExpP, A_constructor, c, PF_W | PF_C); cb_add_prop(R, R->global, cb_atom(R, "RegExp"), c, PF_W | PF_C);
    cb_defn(R, co, "escape", RegExp_escape, 1);
    cb_defn(R, R->RegExpP, "exec", RP_exec, 1); cb_defn(R, R->RegExpP, "test", RP_test, 1); cb_defn(R, R->RegExpP, "toString", RP_toString, 0);
    cb_defgs(R, R->RegExpP, "source", RP_source, 0); cb_defgs(R, R->RegExpP, "flags", RP_flags, 0); cb_defgs(R, R->RegExpP, "global", RP_global, 0); cb_defgs(R, R->RegExpP, "ignoreCase", RP_ignoreCase, 0);
    cb_defgs(R, R->RegExpP, "multiline", RP_multiline, 0); cb_defgs(R, R->RegExpP, "dotAll", RP_dotAll, 0); cb_defgs(R, R->RegExpP, "unicode", RP_unicode, 0); cb_defgs(R, R->RegExpP, "sticky", RP_sticky, 0); cb_defgs(R, R->RegExpP, "hasIndices", RP_hasIndices, 0);
    cb_defn(R, R->StringP, "match", SP_match, 1); cb_defn(R, R->StringP, "matchAll", SP_matchAll, 1); cb_defn(R, R->StringP, "replace", SP_replace, 2); cb_defn(R, R->StringP, "replaceAll", SP_replaceAll, 2);
    cb_defn(R, R->StringP, "search", SP_search, 1); cb_defn(R, R->StringP, "split", SP_split, 2);
}
