/* Colibri: memoria, recolector y cadenas.
   Las celdas viven en paginas de 256 KB, una pagina por tamano de celda; las mayores de 2 KB van sueltas.
   El recolector marca desde las raices (el estado del motor, la pila de C y las pilas de las corrutinas) mirando
   cada palabra de 64 bits: si es un puntero (o un valor etiquetado) a una celda viva, la marca y la recorre igual.
   Asi el codigo en C no registra nada. Las cadenas guardan caracteres y no se recorren por dentro. */
#include "cbi.h"

#define PAGE (256 * 1024)
static const u32 SCS[] = { 16, 24, 32, 48, 64, 80, 96, 128, 160, 192, 256, 320, 384, 512, 768, 1024, 1536, 2048 };
#define NSC (sizeof SCS / sizeof *SCS)
typedef struct { u8 *base, *end; u32 cs, sc; } page_t;
typedef struct { u8 *p; u64 n; } big_t;
static page_t *pages; static u32 npages, cappages;
static big_t *bigs; static u32 nbigs, capbigs;
static cb_cell *freel[NSC];
static u64 since_gc, next_gc = 16 << 20, live_bytes;
static void *stack_top;
static cbv **roots; static u32 nroots, caproots;
static cb_rt *RT;
int cb_gc_off;

void cb_set_stack_top(void *top) { stack_top = top; }
void *cb_stack_top(void) { return stack_top; }
void cb_root(cbv *p) { if (nroots == caproots) { caproots = caproots * 2 + 16; roots = realloc(roots, caproots * sizeof *roots); } roots[nroots++] = p; }
void cb_unroot(cbv *p) { for (u32 i = 0; i < nroots; i++) if (roots[i] == p) { roots[i] = roots[--nroots]; return; } }

static int sc_of(u64 n) { for (u32 i = 0; i < NSC; i++) if (SCS[i] >= n) return (int)i; return -1; }
static void add_page(int sc) {
    u8 *b = malloc(PAGE); if (!b) return;
    if (npages == cappages) { cappages = cappages * 2 + 64; pages = realloc(pages, cappages * sizeof *pages); }
    u32 i = npages; while (i > 0 && pages[i - 1].base > b) { pages[i] = pages[i - 1]; i--; }   /* ordenadas por direccion */
    u32 cs = SCS[sc], n = PAGE / cs; pages[i] = (page_t){ b, b + n * cs, cs, (u32)sc }; npages++;
    for (u32 k = n; k-- > 0;) { cb_cell *c = (cb_cell *)(b + k * cs); c->type = CT_FREE; c->mark = 0; *(cb_cell **)(c + 1) = freel[sc]; freel[sc] = c; } }
static void maybe_gc(void) { if (since_gc > next_gc && RT && !cb_gc_off) cb_gc(RT); }
void *cb_alloc(int type, u64 n) {
    if (n < 16) n = 16; maybe_gc(); since_gc += n;
    int sc = sc_of(n); cb_cell *c;
    if (sc < 0) {   /* celda grande */
        c = malloc(n); if (!c) return 0;
        if (nbigs == capbigs) { capbigs = capbigs * 2 + 64; bigs = realloc(bigs, capbigs * sizeof *bigs); }
        u32 i = nbigs; while (i > 0 && bigs[i - 1].p > (u8 *)c) { bigs[i] = bigs[i - 1]; i--; } bigs[i] = (big_t){ (u8 *)c, n }; nbigs++;
        memset(c, 0, n); c->type = (u8)type; c->sc = 255; return c; }
    if (!freel[sc]) add_page(sc); c = freel[sc]; if (!c) return 0; freel[sc] = *(cb_cell **)(c + 1);
    memset(c, 0, SCS[sc]); c->type = (u8)type; c->sc = (u8)sc; return c; }
/* bloque de valores para el codigo en C (lo recorre el recolector): matrices de argumentos, propiedades... */
void *cb_blob(u64 bytes) { cb_cell *c = cb_alloc(CT_BLOB, bytes + sizeof(cb_cell)); return c ? c + 1 : 0; }
void *cb_raw(u64 bytes) { cb_cell *c = cb_alloc(CT_BLOB, bytes + sizeof(cb_cell)); if (c) c->flags = 1; return c ? c + 1 : 0; }   /* no se recorre (bytes) */
u64 cb_blob_size(void *p) { cb_cell *c = (cb_cell *)p - 1; return (c->sc == 255 ? 0 : SCS[c->sc]) - sizeof(cb_cell) + (c->sc == 255 ? big_size(c) : 0); }
u64 big_size(cb_cell *c) { u32 lo = 0, hi = nbigs; while (lo < hi) { u32 m = (lo + hi) / 2; if (bigs[m].p == (u8 *)c) return bigs[m].n - sizeof(cb_cell); if (bigs[m].p < (u8 *)c) lo = m + 1; else hi = m; } return 0; }
void *cb_blob_grow(void *p, u64 old, u64 nw) { void *q = cb_blob(nw); if (q && p) memcpy(q, p, old < nw ? old : nw); return q; }

/* ---- busqueda de la celda que contiene un puntero ---- */
static cb_cell *find_cell(u64 a) {
    if (!npages && !nbigs) return 0;
    u32 lo = 0, hi = npages;
    while (lo < hi) { u32 m = (lo + hi) / 2; if ((u64)pages[m].base <= a) lo = m + 1; else hi = m; }
    if (lo > 0) { page_t *p = &pages[lo - 1]; if (a < (u64)p->end) { u64 off = (a - (u64)p->base) / p->cs * p->cs; cb_cell *c = (cb_cell *)(p->base + off); return c->type != CT_FREE ? c : 0; } }
    lo = 0; hi = nbigs;
    while (lo < hi) { u32 m = (lo + hi) / 2; if ((u64)bigs[m].p <= a) lo = m + 1; else hi = m; }
    if (lo > 0 && a < (u64)bigs[lo - 1].p + bigs[lo - 1].n) return (cb_cell *)bigs[lo - 1].p;
    return 0; }
static cb_cell **mstack; static u32 nm, capm;
static void push_cell(cb_cell *c) { if (!c || c->mark) return; c->mark = 1; if (nm == capm) { capm = capm * 2 + 1024; mstack = realloc(mstack, capm * sizeof *mstack); } mstack[nm++] = c; }
static void scan_word(u64 w) {
    u64 t = w >> 48; cb_cell *c;
    if (t == TAG_STR || t == TAG_SYM || t == TAG_OBJ || (t == TAG_SPEC && (w & 0xFFFFFFFFFFFFULL))) { if ((c = find_cell(w & 0xFFFFFFFFFFFFULL))) push_cell(c); return; }
    if (t == 0 && w >= 4096 && (c = find_cell(w))) push_cell(c); }
static void scan_range(const void *a, const void *b) { for (const u64 *p = (const u64 *)(((u64)a + 7) & ~7ULL); (const u8 *)(p + 1) <= (const u8 *)b; p++) scan_word(*p); }
static u64 cell_size(cb_cell *c) { return c->sc == 255 ? big_size(c) + sizeof(cb_cell) : SCS[c->sc]; }
static void trace(cb_cell *c) {
    u64 n = cell_size(c);
    if (c->type == CT_STR) { cb_str *s = (cb_str *)c; if (s->rope) { push_cell((cb_cell *)s->l); if (s->r) push_cell((cb_cell *)s->r); } return; }
    if ((c->type == CT_BLOB && c->flags == 1) || c->type == CT_BIG) return;
    scan_range(c + 1, (u8 *)c + n); }
void cb_coro_scan(void (*scan)(const void *, const void *));
static void scan_stack_regs(void) __attribute__((noinline));
static void scan_stack_regs(void) {
    volatile u64 regs[8]; u64 sp;
#if defined(__x86_64__)
    __asm__ volatile("mov %%rbx, 0(%0); mov %%rbp, 8(%0); mov %%r12, 16(%0); mov %%r13, 24(%0); mov %%r14, 32(%0); mov %%r15, 40(%0)" : : "r"(regs) : "memory");
#endif
    sp = (u64)&sp; scan_range((void *)regs, (void *)(regs + 8));
    cb_coro_scan(scan_range);   /* pilas de las corrutinas (y la principal si estamos en una) */
    if (!cb_in_coro()) scan_range((void *)sp, stack_top); }
void cb_gc(cb_rt *R) {
    if (cb_gc_off) return; nm = 0;
    scan_range(R, R + 1);
    for (u32 i = 0; i < nroots; i++) scan_word(*roots[i]);
    cb_atoms_mark(push_cell);
    scan_stack_regs();
    while (nm) { cb_cell *c = mstack[--nm]; trace(c); }
    u64 live = 0;
    for (u32 i = 0; i < npages; i++) { page_t *p = &pages[i]; for (u8 *a = p->base; a < p->end; a += p->cs) { cb_cell *c = (cb_cell *)a; if (c->type == CT_FREE) continue; if (c->mark) { c->mark = 0; live += p->cs; continue; } c->type = CT_FREE; *(cb_cell **)(c + 1) = freel[p->sc]; freel[p->sc] = c; } }
    u32 k = 0; for (u32 i = 0; i < nbigs; i++) { cb_cell *c = (cb_cell *)bigs[i].p; if (c->mark) { c->mark = 0; live += bigs[i].n; bigs[k++] = bigs[i]; } else free(c); } nbigs = k;
    live_bytes = live; since_gc = 0; next_gc = live * 2 > (16 << 20) ? live * 2 : (16 << 20); }
void cb_mem_init(cb_rt *R) { RT = R; }
void cb_mem_free(void) {
    for (u32 i = 0; i < npages; i++) free(pages[i].base); free(pages); pages = 0; npages = cappages = 0;
    for (u32 i = 0; i < nbigs; i++) free(bigs[i].p); free(bigs); bigs = 0; nbigs = capbigs = 0;
    for (u32 i = 0; i < NSC; i++) freel[i] = 0; free(mstack); mstack = 0; nm = capm = 0; free(roots); roots = 0; nroots = caproots = 0; RT = 0; since_gc = 0; }

/* ---------------- cadenas ---------------- */
cb_str *cb_str_new(u32 len) { cb_str *s = cb_alloc(CT_STR, sizeof(cb_str) + (u64)len * 2); if (!s) return 0; s->len = len; return s; }
cb_str *cb_flat(cb_str *s) {
    if (!s->rope) return s; if (s->rope == 2) return s->l;
    cb_str *f = cb_str_new(s->len); if (!f) return s;
    /* se rellena desde el final, con una pila propia: sirve para cuerdas muy profundas (s += x mil veces) */
    u32 cap = 64, sp = 0, pos = s->len; int asc = 1; cb_str **stk = malloc(cap * sizeof *stk); stk[sp++] = s;
    while (sp) { cb_str *n = stk[--sp]; if (n->rope == 2) n = n->l;
        if (n->rope == 1) { if (sp + 2 > cap) { cap *= 2; stk = realloc(stk, cap * sizeof *stk); } stk[sp++] = n->l; stk[sp++] = n->r; continue; }
        pos -= n->len; memcpy(f->c + pos, n->c, (u64)n->len * 2); asc &= n->ascii; }
    free(stk); f->ascii = (u8)asc; s->rope = 2; s->l = f; s->r = 0; return f; }
cb_str *cb_str_ascii(const char *a, u32 n) { cb_str *s = cb_str_new(n); if (!s) return 0; for (u32 i = 0; i < n; i++) s->c[i] = (u8)a[i]; s->ascii = 1; return s; }
cb_str *cb_str_from_utf8(const char *a, long n) {
    if (n < 0) n = (long)strlen(a);
    u32 cnt = 0; int asc = 1; for (long i = 0; i < n;) { u8 b = (u8)a[i]; if (b < 0x80) { cnt++; i++; continue; } asc = 0; int k = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1; cnt += k == 4 ? 2 : 1; i += k; }
    cb_str *s = cb_str_new(cnt); if (!s) return 0; u32 o = 0;
    for (long i = 0; i < n && o < cnt;) { u8 b = (u8)a[i]; u32 cp; int k;
        if (b < 0x80) { cp = b; k = 1; } else if (b >= 0xF0) { cp = b & 7; k = 4; } else if (b >= 0xE0) { cp = b & 15; k = 3; } else if (b >= 0xC0) { cp = b & 31; k = 2; } else { cp = 0xFFFD; k = 1; }
        int ok = 1; for (int j = 1; j < k; j++) { if (i + j >= n || ((u8)a[i + j] & 0xC0) != 0x80) { ok = 0; break; } cp = cp << 6 | ((u8)a[i + j] & 63); }
        if (!ok) { cp = 0xFFFD; k = 1; }
        if (cp >= 0x10000) { cp -= 0x10000; s->c[o++] = (u16)(0xD800 + (cp >> 10)); if (o < cnt) s->c[o++] = (u16)(0xDC00 + (cp & 1023)); } else s->c[o++] = (u16)cp;
        i += k; }
    s->len = o; s->ascii = (u8)asc; return s; }
char *cb_str_utf8z(cb_str *s, long *on) {
    s = cb_flat(s); char *o = malloc((u64)s->len * 3 + 1); if (!o) return 0; long k = 0;
    for (u32 i = 0; i < s->len; i++) { u32 c = s->c[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < s->len && s->c[i + 1] >= 0xDC00 && s->c[i + 1] < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (s->c[i + 1] - 0xDC00); i++; }
        else if (c >= 0xD800 && c < 0xE000) c = 0xFFFD;
        if (c < 0x80) o[k++] = (char)c; else if (c < 0x800) { o[k++] = (char)(0xC0 | c >> 6); o[k++] = (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { o[k++] = (char)(0xE0 | c >> 12); o[k++] = (char)(0x80 | ((c >> 6) & 63)); o[k++] = (char)(0x80 | (c & 63)); }
        else { o[k++] = (char)(0xF0 | c >> 18); o[k++] = (char)(0x80 | ((c >> 12) & 63)); o[k++] = (char)(0x80 | ((c >> 6) & 63)); o[k++] = (char)(0x80 | (c & 63)); } }
    o[k] = 0; if (on) *on = k; return o; }
cb_str *cb_concat(cb_str *a, cb_str *b) {
    if (!a->len) return b; if (!b->len) return a;
    u32 n = a->len + b->len;
    if (n < 64) { cb_str *f = cb_str_new(n); if (!f) return a; cb_str *fa = cb_flat(a), *fb = cb_flat(b); memcpy(f->c, fa->c, fa->len * 2); memcpy(f->c + fa->len, fb->c, fb->len * 2); f->ascii = fa->ascii & fb->ascii; return f; }
    cb_str *r = cb_alloc(CT_STR, sizeof(cb_str)); if (!r) return a; r->len = n; r->rope = 1; r->l = a; r->r = b; return r; }
cb_str *cb_substr(cb_str *s, u32 a, u32 b) { s = cb_flat(s); if (a > s->len) a = s->len; if (b > s->len) b = s->len; if (b < a) b = a; if (a == 0 && b == s->len) return s; cb_str *r = cb_str_new(b - a); if (!r) return s; memcpy(r->c, s->c + a, (b - a) * 2); r->ascii = s->ascii; return r; }
u32 cb_hash(cb_str *s) { if (s->hash) return s->hash; s = cb_flat(s); u32 h = 2166136261u; for (u32 i = 0; i < s->len; i++) { h ^= s->c[i]; h *= 16777619u; } if (!h) h = 1; s->hash = h; return h; }
int cb_str_eq(cb_str *a, cb_str *b) { if (a == b) return 1; if (a->len != b->len) return 0; if (a->atom && b->atom) return 0; a = cb_flat(a); b = cb_flat(b); return !memcmp(a->c, b->c, a->len * 2); }
int cb_str_cmp(cb_str *a, cb_str *b) { a = cb_flat(a); b = cb_flat(b); u32 n = a->len < b->len ? a->len : b->len; for (u32 i = 0; i < n; i++) if (a->c[i] != b->c[i]) return a->c[i] < b->c[i] ? -1 : 1; return a->len < b->len ? -1 : a->len > b->len; }
int cb_str_eqa(cb_str *a, const char *s) { a = cb_flat(a); u32 n = (u32)strlen(s); if (a->len != n) return 0; for (u32 i = 0; i < n; i++) if (a->c[i] != (u8)s[i]) return 0; return 1; }

/* ---- claves internadas ---- */
static cb_str **atoms; static u32 natoms, capatoms;
void cb_atoms_mark(void (*mark)(cb_cell *)) { for (u32 i = 0; i < capatoms; i++) if (atoms[i]) mark((cb_cell *)atoms[i]); }
void cb_atoms_free(void) { free(atoms); atoms = 0; natoms = capatoms = 0; }
cb_str *cb_atom_str(cb_str *s) {
    if (s->atom) return s; s = cb_flat(s);
    if (natoms * 2 >= capatoms) { u32 nc = capatoms ? capatoms * 2 : 4096; cb_str **na = calloc(nc, sizeof *na); for (u32 i = 0; i < capatoms; i++) if (atoms[i]) { u32 h = cb_hash(atoms[i]) & (nc - 1); while (na[h]) h = (h + 1) & (nc - 1); na[h] = atoms[i]; } free(atoms); atoms = na; capatoms = nc; }
    u32 h = cb_hash(s) & (capatoms - 1);
    while (atoms[h]) { cb_str *a = atoms[h]; if (a->len == s->len && !memcmp(a->c, s->c, s->len * 2)) return a; h = (h + 1) & (capatoms - 1); }
    s->atom = 1; atoms[h] = s; natoms++; return s; }
cbv cb_intern(cb_rt *R, cb_str *s) { return cb_strv(cb_atom_str(s)); }
cbv cb_atom(cb_rt *R, const char *s) { long n = (long)strlen(s); int asc = 1; for (long i = 0; i < n; i++) if ((u8)s[i] >= 0x80) asc = 0; cb_str *x = asc ? cb_str_ascii(s, (u32)n) : cb_str_from_utf8(s, n); return cb_strv(cb_atom_str(x)); }
cbv cb_str_utf8(cb_rt *R, const char *s, long n) { return cb_strv(cb_str_from_utf8(s, n)); }
