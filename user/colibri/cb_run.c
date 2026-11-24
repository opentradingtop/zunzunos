/* Colibri: el interprete. Recorre el arbol: eval() calcula expresiones (CB_EMPTY = excepcion en R->exc) y exec()
   ejecuta sentencias devolviendo como terminaron (normal, return, break, continue o excepcion).
   Cada llamada crea un entorno con las variables de la funcion; los bloques con let/const crean el suyo.
   Generadores y funciones async corren en corrutinas (cb_coro.c). */
#include "cbi.h"
char *cb_stack_limit_now(char *main_limit);
void cb_define_acc(cb_rt *R, cb_obj *o, cbv key, cbv get, cbv set, u32 flags);
int cb_get_own_desc(cb_rt *R, cb_obj *o, cbv key, cbv *val, u32 *flags, cbv *get, cbv *set);
int cb_has_own(cb_rt *R, cb_obj *o, cbv key);

enum { C_NORM, C_RET, C_BRK, C_CONT, C_THROW };
typedef struct fr { cb_env *env; cbv ret; cbv label; int strict; cb_obj *callee; fninfo *fi; cbv labels[4]; int nl; struct gen *gen; } fr;
typedef struct frame_link { struct frame_link *up; fninfo *fi; u32 *line; } frame_link;
static cbv eval(cb_rt *R, fr *F, node *n);
static int exec(cb_rt *R, fr *F, node *n);
static int exec_list(cb_rt *R, fr *F, node *n);
static cbv RET_MARK;   /* excepcion especial: gen.return() desenrolla con los finally */
static node *dflt[2];   /* constructores por defecto (arbol) */
void cb_run_reset(void) { RET_MARK = 0; dflt[0] = dflt[1] = 0; }
#define EXC(R) CB_EMPTY

/* ---------------- entornos ---------------- */
static cb_env *env_new(cb_env *up, int n) { cb_env *e = cb_alloc(CT_ENV, sizeof(cb_env) + (u64)n * 8); e->up = up; e->n = (u32)n; for (int i = 0; i < n; i++) e->s[i] = CB_UNDEF; return e; }
static cb_env *env_at(cb_env *e, int hops) { while (hops-- > 0 && e) e = e->up; return e; }
static void init_scope_slots(cb_env *e, scope *s) { for (int i = 0; i < s->n && i < (int)e->n; i++) { int k = s->nkind[i]; e->s[i] = k == VK_LET || k == VK_CONST ? CB_EMPTY : CB_UNDEF; } }
static char *nm(cbv v, char *b, int cap) { if (cb_isstr(v)) { long n; char *u = cb_str_utf8z(cb_s(v), &n); cb_snprintf(b, cap, "%s", u); free(u); } else cb_snprintf(b, cap, "?"); return b; }

/* variables globales: las let/const/class de nivel superior viven en R->lexical_holder; var y function en el objeto global */
static cbv global_get(cb_rt *R, cbv name, int typeof_ok) {
    cb_prop *p = cb_own(R->lexical_holder, name);
    if (p) { if (p->val == CB_EMPTY) { char b[80]; return cb_throw(R, 4, "Cannot access '%s' before initialization", nm(name, b, 80)); } return p->val; }
    if (!cb_has(R, cb_objv(R->global), name)) { if (typeof_ok) return CB_UNDEF; char b[80]; return cb_throw(R, 4, "%s is not defined", nm(name, b, 80)); }
    return cb_get(R, cb_objv(R->global), name); }
static int global_set(cb_rt *R, cbv name, cbv v, int strict) {
    cb_prop *p = cb_own(R->lexical_holder, name);
    if (p) { char b[80]; if (p->val == CB_EMPTY) { cb_throw(R, 4, "Cannot access '%s' before initialization", nm(name, b, 80)); return -1; } if (!(p->flags & PF_W)) { cb_throw(R, 1, "Assignment to constant variable."); return -1; } p->val = v; return 0; }
    if (strict && !cb_has(R, cb_objv(R->global), name)) { char b[80]; cb_throw(R, 4, "%s is not defined", nm(name, b, 80)); return -1; }
    return cb_put(R, cb_objv(R->global), name, v, cb_objv(R->global), strict); }
static cbv var_get(cb_rt *R, fr *F, node *id) {
    if (id->hops >= 0) { cb_env *e = env_at(F->env, id->hops); cbv v = e->s[id->slot]; if (v == CB_EMPTY) { char b[80]; return cb_throw(R, 4, "Cannot access '%s' before initialization", nm(id->v, b, 80)); } return v; }
    return global_get(R, id->v, 0); }
static int var_set(cb_rt *R, fr *F, node *id, cbv v) {
    if (id->hops >= 0) { cb_env *e = env_at(F->env, id->hops); char b[80];
        if (e->s[id->slot] == CB_EMPTY) { cb_throw(R, 4, "Cannot access '%s' before initialization", nm(id->v, b, 80)); return -1; }
        if (id->op == VK_CONST) { cb_throw(R, 1, "Assignment to constant variable."); return -1; }
        if (id->op == 5) { if (F->strict) { cb_throw(R, 1, "Assignment to constant variable."); return -1; } return 0; }
        e->s[id->slot] = v; return 0; }
    return global_set(R, id->v, v, F->strict); }
/* declaracion (let/const/var con valor inicial o parametro): sin comprobar const ni TDZ */
static int var_init(cb_rt *R, fr *F, node *id, cbv v, int kind) {
    if (id->hops >= 0) { env_at(F->env, id->hops)->s[id->slot] = v; return 0; }
    if (kind == VK_LET || kind == VK_CONST) { cb_prop *p = cb_own(R->lexical_holder, id->v); if (p) { p->val = v; if (kind == VK_CONST) p->flags &= ~PF_W; } else cb_add_prop(R, R->lexical_holder, id->v, v, kind == VK_CONST ? PF_E : PF_W | PF_E); return 0; }
    return cb_put(R, cb_objv(R->global), id->v, v, cb_objv(R->global), 0); }

/* ---------------- errores ---------------- */
static const char *ERRN[] = { "Error", "TypeError", "RangeError", "SyntaxError", "ReferenceError", "EvalError", "URIError", "AggregateError" };
static cbv stack_text(cb_rt *R) {
    char b[1200]; int n = 0;
    int depth = 0; for (frame_link *f = R->frames; f && n < 1100 && depth < 10; f = f->up, depth++) { char fn[80] = "<anonymous>"; if (f->fi && f->fi->name) nm(f->fi->name, fn, 80); n += cb_snprintf(b + n, 1200 - n, "    at %s (%s:%u)\n", fn, f->fi && f->fi->file ? f->fi->file : "<script>", f->line ? *f->line : 0); }
    if (n) b[--n] = 0; else b[0] = 0;
    return cb_mkstr(R, b); }
cb_obj *cb_new_err(cb_rt *R, int kind, cbv msg) {
    cb_obj *e = cb_new_obj(R, R->ErrP[kind]); e->cls = OC_ERROR;
    if (!cb_isundef(msg)) cb_add_prop(R, e, A_message, msg, PF_W | PF_C);
    cbv st = stack_text(R); char hdr[300]; char m[200] = ""; if (cb_isstr(msg)) nm(msg, m, 200);
    cb_snprintf(hdr, 300, "%s%s%s\n", ERRN[kind], m[0] ? ": " : "", m);
    cb_add_prop(R, e, A_stack, cb_strv(cb_concat(cb_s(cb_mkstr(R, hdr)), cb_s(st))), PF_W | PF_C);
    return e; }
cbv cb_err_obj(cb_rt *R, int kind, const char *msg) { return cb_objv(cb_new_err(R, kind, cb_mkstr(R, msg))); }
cbv cb_throw(cb_rt *R, int kind, const char *fmt, ...) {
    char b[400]; va_list ap; va_start(ap, fmt); cb_vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    R->exc = cb_err_obj(R, kind, b); R->has_exc = 1; return CB_EMPTY; }
cbv cb_throwv(cb_rt *R, cbv v) { R->exc = v; R->has_exc = 1; return CB_EMPTY; }
cbv cb_take_exc(cb_rt *R) { cbv e = R->exc; R->exc = CB_UNDEF; R->has_exc = 0; return e; }
int cb_throwing(cb_rt *R) { return R->has_exc; }

/* ---------------- funciones ---------------- */
static void set_fn_name(cb_rt *R, cbv f, cbv name, const char *prefix) {
    if (!cb_isobj(f)) return; cb_obj *o = cb_o(f); cb_prop *p = cb_own(o, A_name); if (p && cb_isstr(p->val) && cb_s(p->val)->len) return;
    cbv nmv = name; if (cb_issym(name)) { cb_sym *s = CB_PTR(name); nmv = s->desc ? cb_strv(cb_concat(cb_concat(cb_s(cb_mkstr(R, "[")), s->desc), cb_s(cb_mkstr(R, "]")))) : cb_mkstr(R, ""); }
    if (prefix) nmv = cb_strv(cb_concat(cb_s(cb_mkstr(R, prefix)), cb_s(nmv)));
    if (p) p->val = nmv; else cb_add_prop(R, o, A_name, nmv, PF_C); }
cbv cb_new_func(cb_rt *R, node *fnode, cb_env *env, cbv home) {
    fninfo *fi = fnode->fn; cb_obj *f = cb_new_obj(R, R->FunctionP); f->cls = OC_FUNC; f->p1 = fi; f->p2 = env; f->v1 = home; f->fk = 0;
    cb_add_prop(R, f, A_length, cb_num(fi->length), PF_C);
    cb_add_prop(R, f, A_name, fi->name ? (fi->flags & FK_GET ? cb_strv(cb_concat(cb_s(cb_mkstr(R, "get ")), cb_s(fi->name))) : fi->flags & FK_SET ? cb_strv(cb_concat(cb_s(cb_mkstr(R, "set ")), cb_s(fi->name))) : fi->name) : cb_atom(R, ""), PF_C);
    if (fi->flags & FK_GEN) { cb_obj *pr = cb_new_obj(R, fi->flags & FK_ASYNC ? R->AsyncGenP : R->GenP); cb_add_prop(R, f, A_prototype, cb_objv(pr), PF_W); }
    else if (!(fi->flags & (FK_ARROW | FK_METHOD | FK_ASYNC))) { cb_obj *pr = cb_new_obj(R, R->ObjectP); cb_add_prop(R, pr, A_constructor, cb_objv(f), PF_W | PF_C); cb_add_prop(R, f, A_prototype, cb_objv(pr), PF_W); }
    return cb_objv(f); }
cbv cb_new_native(cb_rt *R, const char *name, cb_native fn, int nargs) {
    cb_obj *f = cb_new_obj(R, R->FunctionP); f->cls = OC_NATIVE; f->p1 = (void *)fn;
    cb_add_prop(R, f, A_length, cb_num(nargs), PF_C); cb_add_prop(R, f, A_name, cb_mkstr(R, name), PF_C); return cb_objv(f); }
int cb_is_ctor(cbv v) {
    if (!cb_isobj(v)) return 0; cb_obj *o = cb_o(v);
    if (o->cls == OC_FUNC) { fninfo *fi = o->p1; return (o->fk & 1) || !(fi->flags & (FK_ARROW | FK_METHOD | FK_ASYNC | FK_GEN)); }
    if (o->cls == OC_NATIVE) return (o->fk & 2) != 0;
    if (o->cls == OC_BOUND) return cb_is_ctor(o->v1);
    if (o->cls == OC_PROXY) return cb_is_ctor(o->v1);
    return 0; }
cbv cb_ctor_this(cb_rt *R, cbv nt, cb_obj *defproto) {
    cb_obj *pr = defproto;
    if (cb_isobj(nt)) { cbv p = cb_get(R, nt, A_prototype); if (p == CB_EMPTY) return p; if (cb_isobj(p)) pr = cb_o(p); }
    return cb_objv(cb_new_obj(R, pr)); }

/* patrones (destructuracion): mode 0 asignacion, VK_VAR/VK_LET/VK_CONST declaracion */
static int bind(cb_rt *R, fr *F, node *pat, cbv v, int mode);
__attribute__((noinline)) static int assign_to(cb_rt *R, fr *F, node *t, cbv v, int mode) {
    switch (t->k) {
    case N_ID: return mode ? var_init(R, F, t, v, mode) : var_set(R, F, t, v);
    case N_DOT: { cbv o = eval(R, F, t->a); if (o == CB_EMPTY) return -1; return cb_put(R, o, t->v, v, o, F->strict); }
    case N_INDEX: { cbv o = eval(R, F, t->a); if (o == CB_EMPTY) return -1; cbv k = eval(R, F, t->b); if (k == CB_EMPTY) return -1;
        if (cb_isobj(o) && cb_o(o)->cls == OC_ARRAY && cb_isnum(k)) { double d = cb_d(k); if (d >= 0 && d < 4294967295.0 && d == (double)(u32)d) { cb_obj *a = cb_o(o); u32 i = (u32)d; if (i < a->n1 && i < a->n2 && !a->sealed) { a->e[i] = v; return 0; } } }
        k = cb_key(R, k); if (k == CB_EMPTY) return -1; return cb_put(R, o, k, v, o, F->strict); }
    case N_PRIVDOT: { cbv o = eval(R, F, t->a); if (o == CB_EMPTY) return -1; cbv key = env_at(F->env, t->hops)->s[t->slot]; if (!cb_isobj(o)) { cb_throw(R, 1, "Cannot write private member to a non-object"); return -1; }
        cb_prop *p = cb_own(cb_o(o), key); if (!p) { cb_obj *c = cb_o(o)->proto; while (c && !(p = cb_own(c, key))) c = c->proto; }
        if (p && (p->flags & PF_ACC)) { cb_acc *a = (cb_acc *)p->val; return cb_call_v(R, a->set, o, 1, &v) == CB_EMPTY ? -1 : 0; }
        if (!p) { cb_throw(R, 1, "Cannot write private member to an object whose class did not declare it"); return -1; } p->val = v; return 0; }
    default: return bind(R, F, t, v, mode); } }
__attribute__((noinline)) static int bind(cb_rt *R, fr *F, node *pat, cbv v, int mode) {
    switch (pat->k) {
    case N_ID: case N_DOT: case N_INDEX: case N_PRIVDOT: return assign_to(R, F, pat, v, mode);
    case N_DEFAULT: if (cb_isundef(v)) { v = eval(R, F, pat->b); if (v == CB_EMPTY) return -1; if (pat->a->k == N_ID && (pat->b->k == N_FUNC || pat->b->k == N_CLASS)) set_fn_name(R, v, pat->a->v, 0); } return bind(R, F, pat->a, v, mode);
    case N_ARRPAT: {
        cbv it = cb_get_iter(R, v, 0); if (it == CB_EMPTY) return -1; int done = 0;
        for (node *e = pat->a; e; e = e->next) {
            if (e->k == N_REST) { cb_obj *rest = cb_new_array(R, 0); cbv x; int r; while (!done && (r = cb_iter_next(R, it, &x)) > 0) cb_arr_push(R, rest, x); if (!done && r < 0) return -1; done = 1; if (bind(R, F, e->a, cb_objv(rest), mode)) return -1; continue; }
            cbv x = CB_UNDEF; if (!done) { int r = cb_iter_next(R, it, &x); if (r < 0) return -1; if (r == 0) { done = 1; x = CB_UNDEF; } }
            if (e->k == N_HOLE) continue;
            if (bind(R, F, e, x, mode)) { if (!done) cb_iter_close(R, it); return -1; } }
        if (!done) cb_iter_close(R, it); return R->has_exc ? -1 : 0; }
    case N_OBJPAT: {
        if (cb_isnullish(v)) { cb_throw(R, 1, "Cannot destructure '%s' as it is %s.", cb_isnull(v) ? "null" : "undefined", cb_isnull(v) ? "null" : "undefined"); return -1; }
        cbv used[64]; int nu = 0;
        for (node *e = pat->a; e; e = e->next) {
            if (e->k == N_REST) { cb_obj *rest = cb_new_obj(R, R->ObjectP); cb_obj *src = cb_to_obj(R, v); if (!src) return -1; cbv ks = cb_own_keys(R, cb_objv(src), 3); if (ks == CB_EMPTY) return -1;
                cb_obj *ka = cb_o(ks); for (u32 i = 0; i < ka->n1; i++) { cbv k = ka->e[i]; int skip = 0; for (int j = 0; j < nu; j++) if (used[j] == k) skip = 1; if (skip) continue;
                    cbv dv, g, s; u32 fl; if (cb_get_own_desc(R, src, k, &dv, &fl, &g, &s) > 0 && (fl & PF_E)) { cbv x = cb_get(R, v, k); if (x == CB_EMPTY) return -1; cb_add_prop(R, rest, k, x, PF_WEC); } }
                if (bind(R, F, e->a, cb_objv(rest), mode)) return -1; continue; }
            cbv k; if (e->flags & 1) { k = eval(R, F, e->a); if (k == CB_EMPTY) return -1; k = cb_key(R, k); if (k == CB_EMPTY) return -1; } else k = e->a->k == N_NUM ? cb_key(R, e->a->v) : e->a->v;
            if (nu < 64) used[nu++] = k;
            cbv x = cb_get(R, v, k); if (x == CB_EMPTY) return -1;
            if (bind(R, F, e->b, x, mode)) return -1; }
        return 0; }
    case N_ASSIGN: if (pat->op == O_ASSIGN) { node d; memset(&d, 0, sizeof d); d.k = N_DEFAULT; d.a = pat->a; d.b = pat->b; return bind(R, F, &d, v, mode); } break;
    default: break; }
    cb_throw(R, 3, "Invalid destructuring assignment target"); return -1; }

/* ---- llamadas ---- */
__attribute__((noinline)) static int collect_args(cb_rt *R, fr *F, node *a, cbv *buf, int cap, cbv **out, int *n) {
    int k = 0; cbv *v = buf;
    for (; a; a = a->next) {
        if (a->k == N_SPREAD) { cbv src = eval(R, F, a->a); if (src == CB_EMPTY) return -1;
            if (cb_isobj(src) && cb_o(src)->cls == OC_ARRAY && cb_o(src)->proto == R->ArrayP) { cb_obj *ar = cb_o(src); for (u32 i = 0; i < ar->n1; i++) { if (k == cap) { int nc = cap * 2; cbv *nv = cb_blob((u64)nc * 8); memcpy(nv, v, k * 8); v = nv; cap = nc; } v[k++] = i < ar->n2 && ar->e[i] != CB_EMPTY ? ar->e[i] : CB_UNDEF; } continue; }
            cbv it = cb_get_iter(R, src, 0); if (it == CB_EMPTY) return -1; cbv x; int r;
            while ((r = cb_iter_next(R, it, &x)) > 0) { if (k == cap) { int nc = cap * 2; cbv *nv = cb_blob((u64)nc * 8); memcpy(nv, v, k * 8); v = nv; cap = nc; } v[k++] = x; }
            if (r < 0) return -1; continue; }
        cbv x = eval(R, F, a); if (x == CB_EMPTY) return -1;
        if (k == cap) { int nc = cap * 2; cbv *nv = cb_blob((u64)nc * 8); memcpy(nv, v, k * 8); v = nv; cap = nc; } v[k++] = x; }
    *out = v; *n = k; return 0; }
static char *callee_name(node *c, char *b, int cap) {
    char x[80];
    if (c->k == N_ID) return nm(c->v, b, cap);
    if (c->k == N_DOT) { char y[80]; cb_snprintf(b, cap, "%s.%s", c->a->k == N_ID ? nm(c->a->v, y, 80) : c->a->k == N_THIS ? "this" : c->a->k == N_DOT ? nm(c->a->v, y, 80) : "(intermediate value)", nm(c->v, x, 80)); return b; }
    cb_snprintf(b, cap, "%s", c->k == N_INDEX ? "(intermediate value)[...]" : "expression"); return b; }
cbv cb_call_v(cb_rt *R, cbv f, cbv thisv, int argc, cbv *argv) {
    if (!cb_isobj(f)) return cb_throw(R, 1, "not a function");
    cb_obj *o = cb_o(f);
    switch (o->cls) {
    case OC_FUNC: if (o->fk & 1) return cb_throw(R, 1, "Class constructor %s cannot be invoked without 'new'", ({ static char b[80]; fninfo *fi = o->p1; fi->name ? nm(fi->name, b, 80) : "anonymous"; }));
        return cb_call_func(R, o, thisv, argc, argv, CB_UNDEF);
    case OC_NATIVE: { cbv nt = R->nt; R->nt = CB_UNDEF; cbv r = ((cb_native)o->p1)(R, thisv, argc, argv, o); R->nt = nt; if (r == CB_EMPTY && !R->has_exc) r = CB_UNDEF; return r; }
    case OC_BOUND: { u32 nb = o->n1; cbv *bv = o->e; int n = (int)nb + argc; cbv buf[16]; cbv *a = n <= 16 ? buf : cb_blob((u64)n * 8); for (u32 i = 0; i < nb; i++) a[i] = bv[i]; for (int i = 0; i < argc; i++) a[nb + i] = argv[i]; return cb_call_v(R, o->v1, o->v2, n, a); }
    case OC_PROXY: { if (cb_isnull(o->v2)) return cb_throw(R, 1, "proxy revoked"); cbv t = cb_getp(R, o->v2, "apply"); if (t == CB_EMPTY) return t; if (cb_isnullish(t)) return cb_call_v(R, o->v1, thisv, argc, argv);
        cbv a[3] = { o->v1, thisv, cb_list_to_array(R, argc, argv) }; return cb_call_v(R, t, o->v2, 3, a); } }
    return cb_throw(R, 1, "not a function"); }
cbv cb_construct(cb_rt *R, cbv f, int argc, cbv *argv, cbv nt) {
    if (!cb_is_ctor(f)) return cb_throw(R, 1, "not a constructor");
    cb_obj *o = cb_o(f);
    switch (o->cls) {
    case OC_FUNC: return cb_call_func(R, o, CB_UNDEF, argc, argv, nt);
    case OC_NATIVE: { cbv old = R->nt; R->nt = nt; cbv r = ((cb_native)o->p1)(R, CB_UNDEF, argc, argv, o); R->nt = old; return r; }
    case OC_BOUND: { u32 nb = o->n1; int n = (int)nb + argc; cbv buf[16]; cbv *a = n <= 16 ? buf : cb_blob((u64)n * 8); for (u32 i = 0; i < nb; i++) a[i] = o->e[i]; for (int i = 0; i < argc; i++) a[nb + i] = argv[i]; return cb_construct(R, o->v1, n, a, nt == f ? o->v1 : nt); }
    case OC_PROXY: { cbv t = cb_getp(R, o->v2, "construct"); if (t == CB_EMPTY) return t; if (cb_isnullish(t)) return cb_construct(R, o->v1, argc, argv, nt == f ? o->v1 : nt);
        cbv a[3] = { o->v1, cb_list_to_array(R, argc, argv), nt }; cbv r = cb_call_v(R, t, o->v2, 3, a); if (r != CB_EMPTY && !cb_isobj(r)) return cb_throw(R, 1, "proxy [[Construct]] must return an object"); return r; } }
    return cb_throw(R, 1, "not a constructor"); }
cbv cb_invoke(cb_rt *R, cbv o, const char *m, int argc, cbv *argv) { cbv f = cb_getp(R, o, m); if (f == CB_EMPTY) return f; if (!cb_isfunc(f)) return cb_throw(R, 1, "%s is not a function", m); return cb_call_v(R, f, o, argc, argv); }

/* ---- campos de instancia de las clases ---- */
static int init_fields(cb_rt *R, cb_obj *ctor, cbv thisv) {
    if (!cb_isobj(ctor->v2)) return 0; cb_obj *L = cb_o(ctor->v2);
    for (u32 i = 0; i + 1 < L->n1; i += 2) { cbv k = L->e[i], f = L->e[i + 1]; cbv v = CB_UNDEF;
        if (cb_isfunc(f)) { v = cb_call_v(R, f, thisv, 0, 0); if (v == CB_EMPTY) return -1; if (cb_isobj(v) && cb_o(v)->cls == OC_FUNC) set_fn_name(R, v, k, 0); }
        if (cb_issym(k) && ((cb_sym *)CB_PTR(k))->reg == 2) { if (cb_own(cb_o(thisv), k)) { cb_throw(R, 1, "Cannot initialize private field twice"); return -1; } cb_add_prop(R, cb_o(thisv), k, v, PF_W); }
        else if (cb_define(R, cb_o(thisv), k, v, PF_WEC)) return -1; }
    return 0; }

/* ---- generadores y funciones async ---- */
typedef struct gen { cb_coro *co; cb_obj *f; cbv thisv; cb_env *env; fr *F; int mode; cbv sent, yielded, result, exc; int done, running, started, async, awaiting, isasyncgen; cbv promise, resolve, reject; cbv queue; cb_obj *obj; } gen;
static void gen_body(void *arg);
static gen *gen_new(cb_rt *R) { gen *g = cb_blob(sizeof(gen)); memset(g, 0, sizeof *g); g->thisv = g->sent = g->yielded = g->result = g->exc = g->promise = g->resolve = g->reject = g->queue = CB_UNDEF; return g; }
static cb_rt *GR;
static void run_body(cb_rt *R, gen *g);

static int setup_call(cb_rt *R, fr *F, cb_obj *f, cbv thisv, int argc, cbv *argv, cbv nt) {
    fninfo *fi = f->p1; scope *fs = fi->sc;
    cb_env *env = env_new(f->p2, fs->n); init_scope_slots(env, fs);
    for (int i = 0; i < fs->n; i++) if (fs->nkind[i] == 5) env->s[i] = cb_objv(f);
    F->env = env; F->strict = (fi->flags & FK_STRICT) != 0; F->callee = f; F->fi = fi; F->ret = CB_UNDEF; F->nl = 0; F->gen = 0;
    if (fi->this_slot) { cbv t = thisv; if (!F->strict && !(fi->flags & FK_CTOR) && cb_isnullish(t)) t = cb_objv(R->global); else if (!F->strict && !cb_isobj(t) && !cb_isnullish(t)) t = cb_objv(cb_to_obj(R, t)); env->s[fi->this_slot - 1] = t; }
    if (fi->nt_slot) env->s[fi->nt_slot - 1] = nt;
    if (fi->home_slot) env->s[fi->home_slot - 1] = cb_objv(f);
    if (fi->args_slot) { cb_obj *a = cb_new_array(R, (u32)argc); a->cls = OC_ARGS; a->proto = R->ObjectP; for (int i = 0; i < argc; i++) a->e[i] = argv[i]; cb_add_prop(R, a, A_length, cb_num(argc), PF_W | PF_C); cb_add_prop(R, a, S_iterator, cb_get(R, cb_objv(R->ArrayP), cb_atom(R, "values")), PF_W | PF_C); if (!F->strict) cb_add_prop(R, a, A_callee, cb_objv(f), PF_W | PF_C); env->s[fi->args_slot - 1] = cb_objv(a); }
    /* parametros */
    int i = 0;
    for (node *p = fi->params; p; p = p->next, i++) {
        if (p->k == N_ID && p->hops == 0) { env->s[p->slot] = i < argc ? argv[i] : CB_UNDEF; continue; }
        if (p->k == N_REST) { cb_obj *rest = cb_new_array(R, 0); for (int k = i; k < argc; k++) cb_arr_push(R, rest, argv[k]); if (bind(R, F, p->a, cb_objv(rest), VK_LET)) return -1; break; }
        if (bind(R, F, p, i < argc ? argv[i] : CB_UNDEF, VK_LET)) return -1; }
    for (int k = 0; k < fs->nf; k++) { node *d = fs->funcs[k]; env->s[d->slot] = cb_new_func(R, d->a, env, CB_UNDEF); }
    return 0; }
cbv cb_call_func(cb_rt *R, cb_obj *f, cbv thisv, int argc, cbv *argv, cbv nt) {
    char probe; if (&probe < cb_stack_limit_now(R->stack_limit)) return cb_throw(R, 2, "Maximum call stack size exceeded");
    fninfo *fi = f->p1; cbv self = CB_UNDEF;
    if (!cb_isundef(nt)) {   /* new */
        if (fi->flags & FK_DERIVED) thisv = CB_EMPTY;
        else { thisv = cb_ctor_this(R, nt, R->ObjectP); if (thisv == CB_EMPTY) return thisv; if (f->fk & 1) { if (init_fields(R, f, thisv)) return CB_EMPTY; } }
        self = thisv; }
    frame_link fl = { R->frames, fi, 0 }; u32 line = 0; fl.line = &line;
    if (fi->flags & (FK_GEN | FK_ASYNC)) {
        gen *g = gen_new(R); fr *F = cb_blob(sizeof(fr)); memset(F, 0, sizeof *F);
        if (setup_call(R, F, f, thisv, argc, argv, nt)) return CB_EMPTY;
        F->gen = g; g->F = F; g->f = f; g->async = (fi->flags & FK_ASYNC) != 0;
        if (fi->flags & FK_GEN) {   /* objeto generador: el cuerpo empieza con el primer next() */
            cbv pr = cb_get(R, cb_objv(f), A_prototype); cb_obj *go = cb_new_obj(R, cb_isobj(pr) ? cb_o(pr) : (g->async ? R->AsyncGenP : R->GenP)); go->cls = OC_GEN; go->p1 = g; g->obj = go; g->isasyncgen = g->async; g->async = 0; return cb_objv(go); }
        /* async: corre ya hasta el primer await; devuelve la promesa */
        g->promise = cb_promise_new(R, &g->resolve, &g->reject);
        g->co = cb_coro_new(gen_body, g); GR = R; g->running = 1; cb_coro_resume(g->co); g->running = 0;
        if (g->done) { cb_coro_free(g->co); g->co = 0; }
        return g->promise; }
    fr F; if (setup_call(R, &F, f, thisv, argc, argv, nt)) return CB_EMPTY;
    R->frames = &fl;
    int c; cbv r;
    if (fi->flags & FK_EXPR) { r = eval(R, &F, fi->body->a); c = r == CB_EMPTY ? C_THROW : C_RET; F.ret = r; }
    else c = exec_list(R, &F, fi->body->a);
    R->frames = fl.up;
    if (c == C_THROW) return CB_EMPTY;
    r = c == C_RET ? F.ret : CB_UNDEF;
    if (!cb_isundef(nt)) {
        if (cb_isobj(r)) return r;
        if (fi->flags & FK_DERIVED) { if (!cb_isundef(r)) return cb_throw(R, 1, "Derived constructors may only return object or undefined"); cbv t = F.env->s[fi->this_slot ? fi->this_slot - 1 : 0]; if (!fi->this_slot) { /* buscar this en el entorno de la funcion */ } if (t == CB_EMPTY || !fi->this_slot) return cb_throw(R, 4, "Must call super constructor in derived class before accessing 'this' or returning from derived constructor"); return t; }
        return self; }
    return r; }
/* el cuerpo de un generador o funcion async, dentro de su corrutina */
static void gen_body(void *arg) {
    gen *g = arg; cb_rt *R = GR; fr *F = g->F; fninfo *fi = g->f->p1;
    frame_link fl = { R->frames, fi, 0 }; u32 line = 0; fl.line = &line; void *oldg = R->curgen; R->curgen = g;
    int c; cbv r = CB_UNDEF;
    if (fi->flags & FK_EXPR) { r = eval(R, F, fi->body->a); c = r == CB_EMPTY ? C_THROW : C_RET; }
    else { c = exec_list(R, F, fi->body->a); r = c == C_RET ? F->ret : CB_UNDEF; }
    R->curgen = oldg; (void)fl;
    if (c == C_THROW) { cbv e = cb_take_exc(R); if (e == RET_MARK) { g->result = g->sent; g->exc = CB_UNDEF; } else { g->exc = e; g->result = CB_UNDEF; } }
    else { g->result = r; g->exc = CB_UNDEF; }
    g->done = 1;
    if (g->async) { if (!cb_isundef(g->exc) || c == C_THROW) { if (cb_isundef(g->exc) && c == C_THROW) {} cb_call_v(R, g->reject, CB_UNDEF, 1, &g->exc); } else cb_call_v(R, g->resolve, CB_UNDEF, 1, &g->result); } }
/* reanudar un generador: mode 0 next, 1 throw, 2 return. Devuelve {value, done} o CB_EMPTY */
static cbv iter_result(cb_rt *R, cbv v, int done) { cb_obj *o = cb_new_obj(R, R->ObjectP); cb_add_prop(R, o, A_value, v, PF_WEC); cb_add_prop(R, o, A_done, cb_bool(done), PF_WEC); return cb_objv(o); }
cbv cb_gen_resume(cb_rt *R, cb_obj *go, int mode, cbv v) {
    gen *g = go->p1;
    if (g->running) return cb_throw(R, 1, "Generator is already running");
    if (g->done) { if (mode == 1) return cb_throwv(R, v); return iter_result(R, mode == 2 ? v : CB_UNDEF, 1); }
    if (!g->started && mode != 0) { g->done = 1; if (mode == 1) return cb_throwv(R, v); return iter_result(R, v, 1); }
    if (!g->co) { g->co = cb_coro_new(gen_body, g); if (!g->co) return cb_throw(R, 2, "sin memoria para el generador"); }
    g->started = 1; g->mode = mode; g->sent = v; g->running = 1; GR = R;
    void *old = R->curgen; cb_coro_resume(g->co); R->curgen = old; g->running = 0;
    if (g->done) { cb_coro_free(g->co); g->co = 0; if (!cb_isundef(g->exc)) { cbv e = g->exc; g->exc = CB_UNDEF; return cb_throwv(R, e); } return iter_result(R, g->result, 1); }
    return iter_result(R, g->yielded, 0); }
__attribute__((noinline)) static cbv do_yield(cb_rt *R, fr *F, cbv v) {
    gen *g = F->gen; if (!g) return cb_throw(R, 3, "yield fuera de un generador");
    g->yielded = v; R->curgen = 0; cb_coro_yield(); R->curgen = g;
    if (g->mode == 1) return cb_throwv(R, g->sent);
    if (g->mode == 2) { R->exc = RET_MARK; R->has_exc = 1; return CB_EMPTY; }
    return g->sent; }
/* await: espera a la promesa dentro de la corrutina */
void cb_agen_after_await(cb_rt *R, void *gp);
static cbv await_ok(cb_rt *R, cbv t, int argc, cbv *argv, cb_obj *fn) { gen *g = fn->p2; g->mode = 0; g->sent = argc ? argv[0] : CB_UNDEF; g->awaiting = 0; GR = R; void *old = R->curgen; cb_coro_resume(g->co); R->curgen = old; if (g->isasyncgen) { cb_agen_after_await(R, g); return CB_UNDEF; } if (g->done && g->co) { cb_coro_free(g->co); g->co = 0; } return CB_UNDEF; }
static cbv await_err(cb_rt *R, cbv t, int argc, cbv *argv, cb_obj *fn) { gen *g = fn->p2; g->mode = 1; g->sent = argc ? argv[0] : CB_UNDEF; g->awaiting = 0; GR = R; void *old = R->curgen; cb_coro_resume(g->co); R->curgen = old; if (g->isasyncgen) { cb_agen_after_await(R, g); return CB_UNDEF; } if (g->done && g->co) { cb_coro_free(g->co); g->co = 0; } return CB_UNDEF; }
__attribute__((noinline)) static cbv do_await(cb_rt *R, fr *F, cbv v) {
    gen *g = F->gen; if (!g || !g->co) return cb_throw(R, 3, "await fuera de una funcion async");
    cbv p = cb_promise_resolve(R, v); if (p == CB_EMPTY) return p;
    cbv ok = cb_new_native(R, "", await_ok, 1), er = cb_new_native(R, "", await_err, 1); cb_o(ok)->p2 = g; cb_o(er)->p2 = g;
    cbv kept = cb_list_to_array(R, 2, (cbv[]){ ok, er }); (void)kept;
    cb_promise_then(R, p, ok, er);
    g->awaiting = 1; R->curgen = 0; cb_coro_yield(); R->curgen = g;
    if (g->mode == 1) return cb_throwv(R, g->sent);
    return g->sent; }

/* ---------------- expresiones ---------------- */
__attribute__((noinline)) static cbv add(cb_rt *R, cbv a, cbv b) {
    if (cb_isnum(a) && cb_isnum(b)) return cb_num(cb_d(a) + cb_d(b));
    if (cb_isstr(a) && cb_isstr(b)) return cb_strv(cb_concat(cb_s(a), cb_s(b)));
    a = cb_to_prim(R, a, 0); if (a == CB_EMPTY) return a; b = cb_to_prim(R, b, 0); if (b == CB_EMPTY) return b;
    if (cb_isstr(a) || cb_isstr(b)) { cbv sa = cb_to_str(R, a); if (sa == CB_EMPTY) return sa; cbv sb = cb_to_str(R, b); if (sb == CB_EMPTY) return sb; return cb_strv(cb_concat(cb_s(sa), cb_s(sb))); }
    if (cb_isbig(a) || cb_isbig(b)) { if (cb_isbig(a) && cb_isbig(b)) return cb_big_op(R, O_ADD, a, b); return cb_throw(R, 1, "Cannot mix BigInt and other types, use explicit conversions"); }
    double x = cb_to_num(R, a); if (R->has_exc) return CB_EMPTY; double y = cb_to_num(R, b); if (R->has_exc) return CB_EMPTY; return cb_num(x + y); }
double cb_pow(double a, double b);
double cb_fmod(double a, double b);
__attribute__((noinline)) static cbv compare(cb_rt *R, cbv a, cbv b, int op) {   /* < > <= >= */
    if (cb_isnum(a) && cb_isnum(b)) { double x = cb_d(a), y = cb_d(b); return cb_bool(op == O_LT ? x < y : op == O_GT ? x > y : op == O_LE ? x <= y : x >= y); }
    a = cb_to_prim(R, a, 1); if (a == CB_EMPTY) return a; b = cb_to_prim(R, b, 1); if (b == CB_EMPTY) return b;
    if (cb_isstr(a) && cb_isstr(b)) { int c = cb_str_cmp(cb_s(a), cb_s(b)); return cb_bool(op == O_LT ? c < 0 : op == O_GT ? c > 0 : op == O_LE ? c <= 0 : c >= 0); }
    if (cb_isbig(a) || cb_isbig(b)) {   /* BigInt con BigInt, numero o cadena */
        int c, sw = !cb_isbig(a); cbv x = sw ? b : a, y = sw ? a : b;
        if (cb_isbig(y)) c = cb_big_cmp(x, y);
        else if (cb_isstr(y)) { cbv yb = cb_big_from_str(R, cb_s(y), 1); if (yb == CB_EMPTY) return CB_FALSE; c = cb_big_cmp(x, yb); }
        else { double d = cb_to_num(R, y); if (R->has_exc) return CB_EMPTY; c = cb_big_cmp_num(x, d); if (c == 2) return CB_FALSE; }
        if (sw) c = -c; return cb_bool(op == O_LT ? c < 0 : op == O_GT ? c > 0 : op == O_LE ? c <= 0 : c >= 0); }
    double x = cb_to_num(R, a); if (R->has_exc) return CB_EMPTY; double y = cb_to_num(R, b); if (R->has_exc) return CB_EMPTY;
    return cb_bool(op == O_LT ? x < y : op == O_GT ? x > y : op == O_LE ? x <= y : x >= y); }
cbv cb_binop(cb_rt *R, int op, cbv a, cbv b) {
    switch (op) {
    case O_ADD: return add(R, a, b);
    case O_EQ: return cb_bool(cb_loose_eq(R, a, b)); case O_NE: { int r = cb_loose_eq(R, a, b); return R->has_exc ? CB_EMPTY : cb_bool(!r); }
    case O_SEQ: return cb_bool(cb_strict_eq(a, b)); case O_SNE: return cb_bool(!cb_strict_eq(a, b));
    case O_LT: case O_GT: case O_LE: case O_GE: return compare(R, a, b, op);
    case O_IN: { if (!cb_isobj(b)) return cb_throw(R, 1, "Cannot use 'in' operator to search for a key in a non-object"); cbv k = cb_key(R, a); if (k == CB_EMPTY) return k; return cb_bool(cb_has(R, b, k)); }
    case O_INSTANCEOF: return cb_instance_of(R, a, b); }
    if (cb_isbig(a) || cb_isbig(b) || (cb_isobj(a) && 0)) { cbv pa = cb_to_prim(R, a, 1); if (pa == CB_EMPTY) return pa; cbv pb = cb_to_prim(R, b, 1); if (pb == CB_EMPTY) return pb;
        if (cb_isbig(pa) && cb_isbig(pb)) return cb_big_op(R, op, pa, pb); return cb_throw(R, 1, "Cannot mix BigInt and other types, use explicit conversions"); }
    if (cb_isobj(a) || cb_isobj(b)) { cbv pa = cb_to_prim(R, a, 1); if (pa == CB_EMPTY) return pa; cbv pb = cb_to_prim(R, b, 1); if (pb == CB_EMPTY) return pb; if (cb_isbig(pa) || cb_isbig(pb)) return cb_binop(R, op, pa, pb); a = pa; b = pb; }
    double x = cb_to_num(R, a); if (R->has_exc) return CB_EMPTY; double y = cb_to_num(R, b); if (R->has_exc) return CB_EMPTY;
    switch (op) {
    case O_SUB: return cb_num(x - y); case O_MUL: return cb_num(x * y); case O_DIV: return cb_num(x / y);
    case O_MOD: return cb_num(cb_fmod(x, y)); case O_EXP: return cb_num(cb_pow(x, y));
    case O_SHL: return cb_num((double)(int32_t)((u32)cb_to_i32(x) << (cb_to_u32(y) & 31)));
    case O_SAR: return cb_num((double)(cb_to_i32(x) >> (cb_to_u32(y) & 31)));
    case O_SHR: return cb_num((double)(cb_to_u32(x) >> (cb_to_u32(y) & 31)));
    case O_AND: return cb_num((double)(cb_to_i32(x) & cb_to_i32(y))); case O_OR: return cb_num((double)(cb_to_i32(x) | cb_to_i32(y))); case O_XOR: return cb_num((double)(cb_to_i32(x) ^ cb_to_i32(y))); }
    return CB_UNDEF; }
static cbv this_val(cb_rt *R, fr *F, node *n) {
    if (n->hops < 0) return R->exports != CB_UNDEF && 0 ? CB_UNDEF : cb_objv(R->global);
    cbv t = env_at(F->env, n->hops)->s[n->slot];
    if (t == CB_EMPTY) return cb_throw(R, 4, "Must call super constructor in derived class before accessing 'this' or returning from derived constructor");
    return t; }
static cb_obj *home_fn(fr *F, node *sup) { if (sup->hops < 0) return 0; cbv f = env_at(F->env, sup->hops)->s[sup->slot]; return cb_isobj(f) ? cb_o(f) : 0; }
static cbv super_base(cb_rt *R, fr *F, node *sup) { cb_obj *f = home_fn(F, sup); if (!f || !cb_isobj(f->v1)) return cb_throw(R, 3, "'super' keyword unexpected here"); cb_obj *h = cb_o(f->v1); return h->proto ? cb_objv(h->proto) : CB_NULL; }
static cbv super_this(cb_rt *R, fr *F, node *sup) { node t; memset(&t, 0, sizeof t); t.hops = (int)sup->flags; t.slot = sup->op; if ((int)sup->flags < 0) return cb_objv(R->global); return this_val(R, F, &t); }
#define OPT_CUT (CB_EMPTY - 1)
__attribute__((noinline)) static cbv member(cb_rt *R, fr *F, node *n, cbv *thisout) {   /* lee a.b / a[b] / a.#b; *thisout = a */
    if (n->a->k == N_SUPER) { cbv base = super_base(R, F, n->a); if (base == CB_EMPTY) return base; cbv t = super_this(R, F, n->a); if (t == CB_EMPTY) return t; cbv k = n->k == N_DOT ? n->v : cb_key(R, eval(R, F, n->b)); if (k == CB_EMPTY) return k; if (thisout) *thisout = t; if (cb_isnull(base)) return CB_UNDEF; return cb_get_proto_chain(R, cb_o(base), k, t); }
    cbv o = eval(R, F, n->a); if (o == CB_EMPTY || o == OPT_CUT) return o;
    if (thisout) *thisout = o;
    if ((n->flags & 1) && cb_isnullish(o)) return CB_EMPTY - 1;   /* cadena opcional cortada */
    if (n->k == N_DOT) { if (cb_isobj(o)) return cb_get_proto_chain(R, cb_o(o), n->v, o); return cb_get(R, o, n->v); }
    if (n->k == N_PRIVDOT) { cbv key = env_at(F->env, n->hops)->s[n->slot]; if (!cb_isobj(o)) return cb_throw(R, 1, "Cannot read private member from a non-object");
        for (cb_obj *c = cb_o(o); c; c = c->proto) { cb_prop *p = cb_own(c, key); if (p) { if (p->flags & PF_ACC) { cb_acc *a = (cb_acc *)p->val; return cb_call_v(R, a->get, o, 0, 0); } return p->val; } }
        return cb_throw(R, 1, "Cannot read private member from an object whose class did not declare it"); }
    cbv k = eval(R, F, n->b); if (k == CB_EMPTY) return k;
    if (cb_isobj(o) && cb_isnum(k)) { cb_obj *a = cb_o(o); double d = cb_d(k); if ((a->cls == OC_ARRAY || a->cls == OC_ARGS) && d >= 0 && d < a->n1 && d == (double)(u32)d) { u32 i = (u32)d; if (i < a->n2 && a->e[i] != CB_EMPTY) return a->e[i]; } }
    if (cb_isstr(o) && cb_isnum(k)) { double d = cb_d(k); cb_str *s = cb_s(o); if (d >= 0 && d < s->len && d == (double)(u32)d) { s = cb_flat(s); cb_str *r = cb_str_new(1); r->c[0] = s->c[(u32)d]; r->ascii = r->c[0] < 128; return cb_strv(r); } }
    k = cb_key(R, k); if (k == CB_EMPTY) return k;
    return cb_get(R, o, k); }
__attribute__((noinline)) static cbv call_expr(cb_rt *R, fr *F, node *n) {
    node *c = n->a; cbv f, thisv = CB_UNDEF;
    if (c->k == N_SUPER) {   /* super(...) */
        cb_obj *h = home_fn(F, c); if (!h) return cb_throw(R, 3, "'super' keyword unexpected here");
        cbv parent = h->proto ? cb_objv(h->proto) : CB_NULL; if (!cb_is_ctor(parent)) return cb_throw(R, 1, "Super constructor is not a constructor");
        cbv buf[16]; cbv *av; int ac; if (collect_args(R, F, n->b, buf, 16, &av, &ac)) return CB_EMPTY;
        cbv newt = CB_UNDEF; if (c->d && c->d->hops >= 0) newt = env_at(F->env, c->d->hops)->s[c->d->slot];
        if (cb_isundef(newt)) newt = cb_objv(h);
        cbv r = cb_construct(R, parent, ac, av, newt); if (r == CB_EMPTY) return r;
        node t; memset(&t, 0, sizeof t); t.hops = (int)c->flags; t.slot = c->op; cb_env *e = env_at(F->env, t.hops);
        if (e->s[t.slot] != CB_EMPTY) return cb_throw(R, 4, "Super constructor may only be called once");
        e->s[t.slot] = r;
        if (init_fields(R, h, r)) return CB_EMPTY;
        return r; }
    if (c->k == N_DOT || c->k == N_INDEX || c->k == N_PRIVDOT) { f = member(R, F, c, &thisv); if (f == OPT_CUT) return OPT_CUT; }
    else if (c->k == N_OPTCHAIN) { f = eval(R, F, c); }
    else { f = eval(R, F, c); }
    if (f == CB_EMPTY) return f;
    if (f == OPT_CUT) return OPT_CUT;
    if ((n->flags & 1) && cb_isnullish(f)) return OPT_CUT;
    cbv buf[16]; cbv *av; int ac; if (collect_args(R, F, n->b, buf, 16, &av, &ac)) return CB_EMPTY;
    if (!cb_isfunc(f)) { char b[120]; return cb_throw(R, 1, "%s is not a function", callee_name(c, b, 120)); }
    return cb_call_v(R, f, thisv, ac, av); }
static cbv eval_class(cb_rt *R, fr *F, node *n);
__attribute__((noinline)) static cbv obj_literal(cb_rt *R, fr *F, node *n) {
    cb_obj *o = cb_new_obj(R, R->ObjectP); cbv ov = cb_objv(o);
    for (node *p = n->a; p; p = p->next) {
        if (p->flags & 256) { cbv src = eval(R, F, p->b); if (src == CB_EMPTY) return src; if (cb_isnullish(src)) continue;
            cb_obj *so = cb_to_obj(R, src); cbv ks = cb_own_keys(R, cb_objv(so), 3); if (ks == CB_EMPTY) return ks; cb_obj *ka = cb_o(ks);
            for (u32 i = 0; i < ka->n1; i++) { cbv k = ka->e[i], dv, g, s; u32 fl; int r = cb_get_own_desc(R, so, k, &dv, &fl, &g, &s); if (r < 0) return CB_EMPTY; if (!r || !(fl & PF_E)) continue; cbv v = cb_get(R, cb_objv(so), k); if (v == CB_EMPTY) return v; cb_define(R, o, k, v, PF_WEC); }
            continue; }
        cbv k; if (p->flags & 1) { k = eval(R, F, p->a); if (k == CB_EMPTY) return k; k = cb_key(R, k); if (k == CB_EMPTY) return k; } else k = p->a->k == N_NUM ? cb_key(R, p->a->v) : p->a->v;
        if (p->flags & (8 | 16 | 32)) { cbv f = cb_new_func(R, p->b, F->env, ov); if (p->flags & 1) set_fn_name(R, f, k, p->flags & 16 ? "get " : p->flags & 32 ? "set " : 0);
            if (p->flags & 16) cb_define_acc(R, o, k, f, CB_UNDEF, PF_E | PF_C); else if (p->flags & 32) cb_define_acc(R, o, k, CB_UNDEF, f, PF_E | PF_C); else cb_define(R, o, k, f, PF_WEC); continue; }
        cbv v = eval(R, F, p->b); if (v == CB_EMPTY) return v;
        if (!(p->flags & 3) && cb_isstr(k) && cb_str_eqa(cb_s(k), "__proto__")) { if (cb_isobj(v)) o->proto = cb_o(v); else if (cb_isnull(v)) o->proto = 0; continue; }
        if ((p->b->k == N_FUNC && !p->b->fn->name) || (p->b->k == N_CLASS && !p->b->fn->name)) set_fn_name(R, v, k, 0);
        cb_define(R, o, k, v, PF_WEC); }
    return ov; }
__attribute__((noinline)) static cbv tmpl(cb_rt *R, fr *F, node *n) {
    if (n->k == N_TAGGED) {
        if (!n->v || n->v == CB_UNDEF) {   /* el objeto de cadenas es el mismo cada vez (por sitio) */
            cb_obj *strs = cb_new_array(R, 0), *raw = cb_new_array(R, 0); node *q = n->a; node *rq = n->c; int i = 0;
            for (; q; q = q->next) { if (i % 2 == 0) { cb_arr_push(R, strs, q->v); cb_arr_push(R, raw, rq ? rq->v : q->v); if (rq) rq = rq->next; } i++; }
            raw->sealed = 2; raw->ext = 0; cb_add_prop(R, strs, A_raw, cb_objv(raw), 0); strs->sealed = 2; strs->ext = 0; n->v = cb_objv(strs); cb_root(&n->v); }
        cbv f, thisv = CB_UNDEF; node *t = n->b;
        if (t->k == N_DOT || t->k == N_INDEX) f = member(R, F, t, &thisv); else f = eval(R, F, t); if (f == CB_EMPTY) return f;
        cbv args[64]; int ac = 0; args[ac++] = n->v; int i = 0;
        for (node *q = n->a; q; q = q->next, i++) if (i % 2 == 1 && ac < 64) { cbv v = eval(R, F, q); if (v == CB_EMPTY) return v; args[ac++] = v; }
        if (!cb_isfunc(f)) return cb_throw(R, 1, "tag is not a function");
        return cb_call_v(R, f, thisv, ac, args); }
    cb_str *acc = cb_s(cb_mkstr(R, "")); int i = 0;
    for (node *q = n->a; q; q = q->next, i++) { cbv v; if (i % 2 == 0) v = q->v; else { v = eval(R, F, q); if (v == CB_EMPTY) return v; if (cb_issym(v)) return cb_throw(R, 1, "Cannot convert a Symbol value to a string"); v = cb_to_str(R, v); if (v == CB_EMPTY) return v; } acc = cb_concat(acc, cb_s(v)); }
    return cb_strv(acc); }
__attribute__((noinline)) static cbv update(cb_rt *R, fr *F, node *n) {
    node *t = n->a; cbv old, obj = CB_UNDEF, key = CB_UNDEF;
    if (t->k == N_ID) { old = var_get(R, F, t); }
    else if (t->k == N_DOT || t->k == N_INDEX) { obj = eval(R, F, t->a); if (obj == CB_EMPTY) return obj; key = t->k == N_DOT ? t->v : cb_key(R, eval(R, F, t->b)); if (key == CB_EMPTY) return key; old = cb_get(R, obj, key); }
    else if (t->k == N_PRIVDOT) { old = member(R, F, t, 0); }
    else return cb_throw(R, 3, "Invalid left-hand side expression in postfix operation");
    if (old == CB_EMPTY) return old;
    if (cb_isobj(old)) { old = cb_to_prim(R, old, 1); if (old == CB_EMPTY) return old; }
    if (cb_isbig(old)) { cbv nb = cb_big_op(R, n->op, old, 0); if (nb == CB_EMPTY) return nb; if (t->k == N_ID) { if (var_set(R, F, t, nb)) return CB_EMPTY; } else if (t->k == N_PRIVDOT) { if (assign_to(R, F, t, nb, 0)) return CB_EMPTY; } else if (cb_put(R, obj, key, nb, obj, F->strict)) return CB_EMPTY; return n->flags ? nb : old; }
    double d = cb_to_num(R, old); if (R->has_exc) return CB_EMPTY; double nv = n->op == O_INC ? d + 1 : d - 1;
    if (t->k == N_ID) { if (var_set(R, F, t, cb_num(nv))) return CB_EMPTY; }
    else if (t->k == N_PRIVDOT) { if (assign_to(R, F, t, cb_num(nv), 0)) return CB_EMPTY; }
    else if (cb_put(R, obj, key, cb_num(nv), obj, F->strict)) return CB_EMPTY;
    return cb_num(n->flags ? nv : d); }
__attribute__((noinline)) static cbv assign_expr(cb_rt *R, fr *F, node *n) {
    node *t = n->a;
    if (n->op == O_ASSIGN) {
        if (t->k == N_ID) { cbv v = eval(R, F, n->b); if (v == CB_EMPTY) return v; if ((n->b->k == N_FUNC || n->b->k == N_CLASS) && !n->b->fn->name) set_fn_name(R, v, t->v, 0); return var_set(R, F, t, v) ? CB_EMPTY : v; }
        if (t->k == N_DOT || t->k == N_INDEX) {
            cbv o = eval(R, F, t->a); if (o == CB_EMPTY) return o; cbv k;
            if (t->k == N_DOT) k = t->v; else { k = eval(R, F, t->b); if (k == CB_EMPTY) return k; }
            cbv v = eval(R, F, n->b); if (v == CB_EMPTY) return v;
            if (t->k == N_INDEX && cb_isobj(o) && cb_o(o)->cls == OC_ARRAY && cb_isnum(k)) { double d = cb_d(k); cb_obj *a = cb_o(o); if (d >= 0 && d == (double)(u32)d && d < 4294967295.0 && !a->sealed && a->ext) { if (cb_arr_set(R, a, (u32)d, v)) return CB_EMPTY; return v; } }
            if (t->k == N_INDEX) { k = cb_key(R, k); if (k == CB_EMPTY) return k; }
            if (t->a->k == N_SUPER) { cbv th = super_this(R, F, t->a); if (th == CB_EMPTY) return th; return cb_put(R, th, k, v, th, F->strict) ? CB_EMPTY : v; }
            return cb_put(R, o, k, v, o, F->strict) ? CB_EMPTY : v; }
        cbv v = eval(R, F, n->b); if (v == CB_EMPTY) return v; return assign_to(R, F, t, v, 0) ? CB_EMPTY : v; }
    /* compuestas: a op= b */
    cbv obj = CB_UNDEF, key = CB_UNDEF, old;
    if (t->k == N_ID) old = var_get(R, F, t);
    else if (t->k == N_DOT || t->k == N_INDEX) { obj = eval(R, F, t->a); if (obj == CB_EMPTY) return obj; key = t->k == N_DOT ? t->v : cb_key(R, eval(R, F, t->b)); if (key == CB_EMPTY) return key; old = cb_get(R, obj, key); }
    else if (t->k == N_PRIVDOT) old = member(R, F, t, 0);
    else return cb_throw(R, 3, "Invalid left-hand side in assignment");
    if (old == CB_EMPTY) return old;
    cbv v;
    if (n->op == O_LAND || n->op == O_LOR || n->op == O_NULLISH) {
        int keep = n->op == O_LAND ? !cb_truthy(old) : n->op == O_LOR ? cb_truthy(old) : !cb_isnullish(old);
        if (keep) return old;
        v = eval(R, F, n->b); if (v == CB_EMPTY) return v; if (t->k == N_ID && (n->b->k == N_FUNC || n->b->k == N_CLASS) && !n->b->fn->name) set_fn_name(R, v, t->v, 0); }
    else { cbv b = eval(R, F, n->b); if (b == CB_EMPTY) return b; v = cb_binop(R, n->op, old, b); if (v == CB_EMPTY) return v; }
    if (t->k == N_ID) return var_set(R, F, t, v) ? CB_EMPTY : v;
    if (t->k == N_PRIVDOT) return assign_to(R, F, t, v, 0) ? CB_EMPTY : v;
    return cb_put(R, obj, key, v, obj, F->strict) ? CB_EMPTY : v; }
static cbv eval(cb_rt *R, fr *F, node *n) {
    switch (n->k) {
    case N_NUM: if (n->flags & 1) return cb_big_from_str(R, cb_s(n->v), 0); return n->v;
    case N_STR: return n->v;
    case N_TRUE: return CB_TRUE; case N_FALSE: return CB_FALSE; case N_NULL: return CB_NULL;
    case N_ID: return var_get(R, F, n);
    case N_THIS: return this_val(R, F, n);
    case N_NEWTARGET: if (n->hops < 0) return CB_UNDEF; return env_at(F->env, n->hops)->s[n->slot];
    case N_TMPL: case N_TAGGED: return tmpl(R, F, n);
    case N_REGEX: return cb_regexp_new(R, n->v, n->a->v);
    case N_ARR: { cb_obj *a = cb_new_array(R, 0);
        for (node *e = n->a; e; e = e->next) {
            if (e->k == N_HOLE) { cb_arr_push(R, a, CB_EMPTY); continue; }
            if (e->k == N_SPREAD) { cbv src = eval(R, F, e->a); if (src == CB_EMPTY) return src; cbv it = cb_get_iter(R, src, 0); if (it == CB_EMPTY) return it; cbv x; int r; while ((r = cb_iter_next(R, it, &x)) > 0) cb_arr_push(R, a, x); if (r < 0) return CB_EMPTY; continue; }
            cbv v = eval(R, F, e); if (v == CB_EMPTY) return v; cb_arr_push(R, a, v); }
        return cb_objv(a); }
    case N_OBJ: return obj_literal(R, F, n);
    case N_FUNC: { cbv f = cb_new_func(R, n, F->env, CB_UNDEF); if ((n->flags & 1) && n->fn->name) { /* el nombre propio vive en el entorno de la funcion (tipo 5) */ } return f; }
    case N_CLASS: return eval_class(R, F, n);
    case N_UNARY: {
        if (n->op == O_TYPEOF && n->a->k == N_ID && n->a->hops < 0) { cbv v = global_get(R, n->a->v, 1); if (v == CB_EMPTY) return v; return cb_typeof(R, v); }
        if (n->op == O_DELETE) { node *t = n->a;
            if (t->k == N_DOT || t->k == N_INDEX) { cbv o = eval(R, F, t->a); if (o == CB_EMPTY) return o; cbv k = t->k == N_DOT ? t->v : cb_key(R, eval(R, F, t->b)); if (k == CB_EMPTY) return k; if (cb_isnullish(o)) return cb_throw(R, 1, "Cannot convert undefined or null to object"); return cb_del(R, o, k, F->strict) ? CB_EMPTY : CB_TRUE; }
            if (t->k == N_ID) { if (t->hops >= 0) return CB_FALSE; if (cb_own(R->lexical_holder, t->v)) return CB_FALSE; return cb_del(R, cb_objv(R->global), t->v, 0) ? CB_EMPTY : CB_TRUE; }
            cbv v = eval(R, F, t); if (v == CB_EMPTY) return v; return CB_TRUE; }
        cbv v = eval(R, F, n->a); if (v == CB_EMPTY) return v;
        switch (n->op) {
        case O_NOT: return cb_bool(!cb_truthy(v));
        case O_NEG: { if (cb_isnum(v)) return cb_num(-cb_d(v)); if (cb_isobj(v)) { v = cb_to_prim(R, v, 1); if (v == CB_EMPTY) return v; } if (cb_isbig(v)) return cb_big_op(R, O_NEG, v, 0); double d = cb_to_num(R, v); if (R->has_exc) return CB_EMPTY; return cb_num(-d); }
        case O_PLUS: { double d = cb_to_num(R, v); if (R->has_exc) return CB_EMPTY; return cb_num(d); }
        case O_BITNOT: { if (cb_isobj(v)) { v = cb_to_prim(R, v, 1); if (v == CB_EMPTY) return v; } if (cb_isbig(v)) return cb_big_op(R, O_BITNOT, v, 0); double d = cb_to_num(R, v); if (R->has_exc) return CB_EMPTY; return cb_num((double)~cb_to_i32(d)); }
        case O_TYPEOF: return cb_typeof(R, v);
        case O_VOID: return CB_UNDEF; }
        return CB_UNDEF; }
    case N_UPDATE: return update(R, F, n);
    case N_BIN: {
        if (n->op == O_IN && n->a->k == N_PRIVIN) { cbv o = eval(R, F, n->b); if (o == CB_EMPTY) return o; if (!cb_isobj(o)) return cb_throw(R, 1, "Cannot use 'in' operator to search for a private field in a non-object"); cbv key = env_at(F->env, n->a->hops)->s[n->a->slot]; for (cb_obj *c = cb_o(o); c; c = c->proto) if (cb_own(c, key)) return CB_TRUE; return CB_FALSE; }
        cbv a = eval(R, F, n->a); if (a == CB_EMPTY) return a; cbv b = eval(R, F, n->b); if (b == CB_EMPTY) return b;
        if (cb_isnum(a) && cb_isnum(b)) { double x = cb_d(a), y = cb_d(b); switch (n->op) { case O_ADD: return cb_num(x + y); case O_SUB: return cb_num(x - y); case O_MUL: return cb_num(x * y); case O_LT: return cb_bool(x < y); case O_GT: return cb_bool(x > y); case O_LE: return cb_bool(x <= y); case O_GE: return cb_bool(x >= y); case O_SEQ: case O_EQ: return cb_bool(x == y); case O_SNE: case O_NE: return cb_bool(x != y); } }
        return cb_binop(R, n->op, a, b); }
    case N_LOGIC: { cbv a = eval(R, F, n->a); if (a == CB_EMPTY) return a;
        if (n->op == O_LAND ? !cb_truthy(a) : n->op == O_LOR ? cb_truthy(a) : !cb_isnullish(a)) return a; return eval(R, F, n->b); }
    case N_ASSIGN: return assign_expr(R, F, n);
    case N_COND: { cbv c = eval(R, F, n->a); if (c == CB_EMPTY) return c; return eval(R, F, cb_truthy(c) ? n->b : n->c); }
    case N_CALL: return call_expr(R, F, n);
    case N_NEW: { cbv f = eval(R, F, n->a); if (f == CB_EMPTY) return f; cbv buf[16]; cbv *av; int ac; if (collect_args(R, F, n->b, buf, 16, &av, &ac)) return CB_EMPTY;
        if (!cb_is_ctor(f)) { char b[120]; return cb_throw(R, 1, "%s is not a constructor", callee_name(n->a, b, 120)); } return cb_construct(R, f, ac, av, f); }
    case N_DOT: case N_INDEX: case N_PRIVDOT: return member(R, F, n, 0);
    case N_OPTCHAIN: { cbv v = eval(R, F, n->a); return v == OPT_CUT ? CB_UNDEF : v; }
    case N_SEQ: { cbv v = CB_UNDEF; for (node *e = n->a; e; e = e->next) { v = eval(R, F, e); if (v == CB_EMPTY) return v; } return v; }
    case N_YIELD: {
        if (n->flags & 1) {   /* yield* : delega en otro iterador */
            cbv src = eval(R, F, n->a); if (src == CB_EMPTY) return src; cbv it = cb_get_iter(R, src, 0); if (it == CB_EMPTY) return it;
            gen *g = F->gen; cbv sent = CB_UNDEF; int mode = 0;
            for (;;) { cbv r;
                if (mode == 0) { cbv nx = cb_get(R, it, A_next); if (nx == CB_EMPTY) return nx; r = cb_call_v(R, nx, it, 1, &sent); }
                else if (mode == 1) { cbv th = cb_get(R, it, A_throw); if (th == CB_EMPTY) return th; if (cb_isnullish(th)) { cb_iter_close(R, it); return cb_throw(R, 1, "The iterator does not provide a 'throw' method"); } r = cb_call_v(R, th, it, 1, &sent); }
                else { cbv rt = cb_get(R, it, A_return); if (rt == CB_EMPTY) return rt; if (cb_isnullish(rt)) { g->sent = sent; R->exc = RET_MARK; R->has_exc = 1; return CB_EMPTY; } r = cb_call_v(R, rt, it, 1, &sent); }
                if (r == CB_EMPTY) return r; if (!cb_isobj(r)) return cb_throw(R, 1, "iterator result is not an object");
                cbv d = cb_get(R, r, A_done); if (d == CB_EMPTY) return d; cbv v = cb_get(R, r, A_value); if (v == CB_EMPTY) return v;
                if (cb_truthy(d)) { if (mode == 2) { g->sent = v; R->exc = RET_MARK; R->has_exc = 1; return CB_EMPTY; } return v; }
                g->yielded = v; R->curgen = 0; cb_coro_yield(); R->curgen = g; mode = g->mode; sent = g->sent; } }
        cbv v = n->a ? eval(R, F, n->a) : CB_UNDEF; if (v == CB_EMPTY) return v;
        if (F->gen && F->gen->isasyncgen) { v = do_await(R, F, v); if (v == CB_EMPTY) return v; }
        return do_yield(R, F, v); }
    case N_AWAIT: { cbv v = eval(R, F, n->a); if (v == CB_EMPTY) return v; return do_await(R, F, v); }
    case N_SUPER: return cb_throw(R, 3, "'super' keyword unexpected here");
    case N_IMPORTMETA: { cb_obj *o = cb_new_obj(R, 0); cb_add_prop(R, o, cb_atom(R, "url"), cb_mkstr(R, n->fn && n->fn->file ? n->fn->file : ""), PF_WEC); return cb_objv(o); }
    case N_DYNIMPORT: { cbv spec = eval(R, F, n->a); if (spec == CB_EMPTY) return spec; cbv res, rej; cbv p = cb_promise_new(R, &res, &rej);
        if (!R->import_hook) { cbv e = cb_err_obj(R, 1, "import() no disponible"); cb_call_v(R, rej, CB_UNDEF, 1, &e); return p; }
        char *s = cb_to_utf8(R, spec, 0); if (!s) return CB_EMPTY; cbv ns = R->import_hook(R, s, F->fi && F->fi->file ? F->fi->file : ""); free(s);
        if (ns == CB_EMPTY) { cbv e = cb_take_exc(R); cb_call_v(R, rej, CB_UNDEF, 1, &e); } else cb_call_v(R, res, CB_UNDEF, 1, &ns); return p; }
    default: break; }
    return cb_throw(R, 3, "expresion no soportada (%d)", n->k); }

/* ---------------- clases ---------------- */
static cbv default_ctor(cb_rt *R, cbv t, int argc, cbv *argv, cb_obj *fn) { return CB_UNDEF; }
__attribute__((noinline)) static cbv eval_class(cb_rt *R, fr *F, node *n) {
    fninfo *ci = n->fn; scope *cs = n->sc; cb_env *outer = F->env; cb_env *cenv = outer;
    if (cs && cs->env) { cenv = env_new(outer, cs->n); init_scope_slots(cenv, cs); for (int i = 0; i < cs->n; i++) { cb_str *s = cb_isstr(cs->names[i]) ? cb_s(cs->names[i]) : 0; if (s && s->len && cb_flat(s)->c[0] == '#') { cbv sy = cb_sym_new(R, cs->names[i]); ((cb_sym *)CB_PTR(sy))->reg = 2; cenv->s[i] = sy; } } }
    F->env = cenv;
    cbv protoParent = cb_objv(R->ObjectP), ctorParent = cb_objv(R->FunctionP);
    if (n->b) { cbv sup = eval(R, F, n->b); if (sup == CB_EMPTY) { F->env = outer; return sup; }
        if (cb_isnull(sup)) { protoParent = CB_NULL; }
        else { if (!cb_is_ctor(sup)) { F->env = outer; return cb_throw(R, 1, "Class extends value is not a constructor or null"); } cbv pp = cb_get(R, sup, A_prototype); if (pp == CB_EMPTY) { F->env = outer; return pp; } if (!cb_isobj(pp) && !cb_isnull(pp)) { F->env = outer; return cb_throw(R, 1, "Class extends value does not have valid prototype property"); } protoParent = pp; ctorParent = sup; } }
    cb_obj *proto = cb_new_obj(R, cb_isnull(protoParent) ? 0 : cb_o(protoParent));
    node *ctor = 0; for (node *m = n->a; m; m = m->next) if (m->flags & 1024) ctor = m;
    cbv F_; cb_obj *Fo;
    if (ctor) { F_ = cb_new_func(R, ctor->b, cenv, cb_objv(proto)); Fo = cb_o(F_); }
    else {   /* constructor por defecto: constructor(...args) { super(...args) } */
        int derived = n->b != 0;
        if (!dflt[derived]) {
            char err[200]; const char *src = derived ? "(class extends Object { constructor(...a) { super(...a); } })" : "(class { constructor() {} })";
            node *prog = cb_parse(R, src, (long)strlen(src), "<clase>", 0, err, sizeof err);
            node *cl = prog->a->a; for (node *m = cl->a; m; m = m->next) if (m->flags & 1024) dflt[derived] = m->b; }
        F_ = cb_new_func(R, dflt[derived], cenv, cb_objv(proto)); Fo = cb_o(F_); }
    Fo->fk |= 1; Fo->proto = cb_o(ctorParent);
    if (ci->name) { cb_prop *np = cb_own(Fo, A_name); if (np) np->val = ci->name; }
    else { cb_prop *np = cb_own(Fo, A_name); if (np) np->val = cb_atom(R, ""); }
    cb_prop *pp = cb_own(Fo, A_prototype); if (pp) { pp->val = cb_objv(proto); pp->flags = 0; } else cb_add_prop(R, Fo, A_prototype, cb_objv(proto), 0);
    cb_add_prop(R, proto, A_constructor, F_, PF_W | PF_C);
    cb_obj *fields = cb_new_array(R, 0); Fo->v2 = cb_objv(fields);
    node *statics = 0; (void)statics;
    for (node *m = n->a; m; m = m->next) {
        if (m->flags & 1024) continue;
        int isstatic = (m->flags & 4) != 0; cb_obj *target = isstatic ? Fo : proto;
        if (m->k == N_STATICBLOCK) continue;
        cbv k;
        if (m->flags & 64) { int found = 0; for (int i = 0; cs && i < cs->n; i++) if (cs->names[i] == m->a->v) { k = cenv->s[i]; found = 1; } if (!found) { F->env = outer; return cb_throw(R, 3, "nombre privado no declarado"); } }
        else if (m->flags & 1) { k = eval(R, F, m->a); if (k == CB_EMPTY) { F->env = outer; return k; } k = cb_key(R, k); if (k == CB_EMPTY) { F->env = outer; return k; } }
        else k = m->a->k == N_NUM ? cb_key(R, m->a->v) : m->a->v;
        if (m->flags & 128) {   /* campo */
            cbv init = m->b ? cb_new_func(R, m->b, cenv, cb_objv(target)) : CB_UNDEF;
            if (isstatic) { cbv v = CB_UNDEF; if (cb_isfunc(init)) { v = cb_call_v(R, init, F_, 0, 0); if (v == CB_EMPTY) { F->env = outer; return v; } if (cb_isobj(v) && cb_o(v)->cls == OC_FUNC) set_fn_name(R, v, k, 0); } cb_define(R, Fo, k, v, (m->flags & 64) ? PF_W : PF_WEC); }
            else { cb_arr_push(R, fields, k); cb_arr_push(R, fields, init); }
            continue; }
        cbv f = cb_new_func(R, m->b, cenv, cb_objv(target)); set_fn_name(R, f, k, m->flags & 16 ? "get " : m->flags & 32 ? "set " : 0);
        if (m->flags & 16) cb_define_acc(R, target, k, f, CB_UNDEF, PF_C);
        else if (m->flags & 32) cb_define_acc(R, target, k, CB_UNDEF, f, PF_C);
        else cb_define(R, target, k, f, PF_W | PF_C); }
    /* nombre de la clase dentro de su ambito, y bloques estaticos */
    if (ci->name && cs && cs->env) for (int i = 0; i < cs->n; i++) if (cs->names[i] == ci->name) cenv->s[i] = F_;
    for (node *m = n->a; m; m = m->next) if (m->k == N_STATICBLOCK) { cbv f = cb_new_func(R, m->b, cenv, F_); if (cb_call_v(R, f, F_, 0, 0) == CB_EMPTY) { F->env = outer; return CB_EMPTY; } }
    F->env = outer; return F_; }

/* ---------------- sentencias ---------------- */
static int label_match(fr *F, cbv label, cbv *mine, int nmine) { if (!label) return 1; for (int i = 0; i < nmine; i++) if (mine[i] == label) return 1; return 0; }
static cb_env *block_env(cb_rt *R, fr *F, scope *s) {
    if (!s || !s->env) return F->env;
    cb_env *e = env_new(F->env, s->n); init_scope_slots(e, s);
    for (int i = 0; i < s->nf; i++) { node *d = s->funcs[i]; e->s[d->slot] = CB_UNDEF; }
    return e; }
static void hoist_funcs(cb_rt *R, fr *F, scope *s) { if (!s) return; for (int i = 0; i < s->nf; i++) { node *d = s->funcs[i]; if (s->env) F->env->s[d->slot] = cb_new_func(R, d->a, F->env, CB_UNDEF); } }
static cb_env *copy_env(cb_env *e) { cb_env *c = env_new(e->up, (int)e->n); memcpy(c->s, e->s, e->n * 8); return c; }
static int loop_body(cb_rt *R, fr *F, node *body, cbv *labels, int nl, int *stop) {   /* 0 seguir, otro: salir con ese codigo */
    int c = exec(R, F, body); *stop = 0;
    if (c == C_BRK) { if (label_match(F, F->label, labels, nl)) { F->label = 0; *stop = 1; return C_NORM; } *stop = 1; return C_BRK; }
    if (c == C_CONT) { if (label_match(F, F->label, labels, nl)) { F->label = 0; return C_NORM; } *stop = 1; return C_CONT; }
    if (c != C_NORM) { *stop = 1; return c; }
    return C_NORM; }
__attribute__((noinline)) static int for_in_of(cb_rt *R, fr *F, node *n, cbv *labels, int nl) {
    cb_env *outer = F->env; scope *s = n->sc;
    cbv src; /* el objeto se evalua con las variables del for aun sin iniciar (TDZ) */
    src = eval(R, F, n->b);   /* resuelto en el ambito de fuera (ver cb_parse.c) */
    if (src == CB_EMPTY) return C_THROW;
    node *lhs = n->a; int kind = 0; node *target = lhs;
    if (lhs->k == N_VAR) { kind = lhs->op; target = lhs->a->a; }
    int result = C_NORM;
    if (n->k == N_FORIN) {
        if (cb_isnullish(src)) return C_NORM;
        cb_obj *o = cb_to_obj(R, src); cb_obj *keys = cb_new_array(R, 0); cb_obj *seen = cb_new_obj(R, 0);
        for (cb_obj *c = o; c; c = c->proto) { cbv ks = cb_own_keys(R, cb_objv(c), 1); if (ks == CB_EMPTY) return C_THROW; cb_obj *ka = cb_o(ks);
            for (u32 i = 0; i < ka->n1; i++) { cbv k = ka->e[i]; if (cb_own(seen, k)) continue; cb_add_prop(R, seen, k, CB_TRUE, PF_WEC); cbv dv, g, st; u32 fl; int r = cb_get_own_desc(R, c, k, &dv, &fl, &g, &st); if (r > 0 && (fl & PF_E)) cb_arr_push(R, keys, k); } }
        for (u32 i = 0; i < keys->n1; i++) {
            cbv k = keys->e[i]; if (!cb_has(R, cb_objv(o), k)) continue;   /* borrada durante el bucle */
            if (s && s->env) F->env = block_env(R, F, s);
            if (bind(R, F, target, k, kind)) { F->env = outer; return C_THROW; }
            int stop; int c = loop_body(R, F, n->c, labels, nl, &stop); F->env = outer; if (stop) return c; }
        return C_NORM; }
    int isawait = n->flags & 1;
    cbv it = cb_get_iter(R, src, isawait); if (it == CB_EMPTY) return C_THROW;
    for (;;) {
        cbv x; int r;
        if (isawait) { cbv nx = cb_get(R, it, A_next); if (nx == CB_EMPTY) return C_THROW; cbv res = cb_call_v(R, nx, it, 0, 0); if (res == CB_EMPTY) return C_THROW; res = do_await(R, F, res); if (res == CB_EMPTY) return C_THROW; cbv d = cb_get(R, res, A_done); if (d == CB_EMPTY) return C_THROW; if (cb_truthy(d)) break; x = cb_get(R, res, A_value); if (x == CB_EMPTY) return C_THROW; r = 1; }
        else { r = cb_iter_next(R, it, &x); if (r < 0) return C_THROW; if (!r) break; }
        if (s && s->env) F->env = block_env(R, F, s);
        if (bind(R, F, target, x, kind)) { F->env = outer; cb_iter_close(R, it); return C_THROW; }
        int stop; int c = loop_body(R, F, n->c, labels, nl, &stop); F->env = outer;
        if (stop) { if (c != C_THROW || 1) { if (c == C_THROW) { cb_iter_close(R, it); } else cb_iter_close(R, it); } return c; } }
    return result; }
__attribute__((noinline)) static int exec_loop(cb_rt *R, fr *F, node *n, cbv *labels, int nl) {
    switch (n->k) {
    case N_WHILE: for (;;) { cbv c = eval(R, F, n->a); if (c == CB_EMPTY) return C_THROW; if (!cb_truthy(c)) return C_NORM; int stop; int r = loop_body(R, F, n->b, labels, nl, &stop); if (stop) return r; }
    case N_DOWHILE: for (;;) { int stop; int r = loop_body(R, F, n->a, labels, nl, &stop); if (stop) return r; cbv c = eval(R, F, n->b); if (c == CB_EMPTY) return C_THROW; if (!cb_truthy(c)) return C_NORM; }
    case N_FOR: {
        cb_env *outer = F->env; scope *s = n->sc; int perit = s && s->env && n->a && n->a->k == N_VAR && n->a->op != VK_VAR;
        if (s && s->env) F->env = block_env(R, F, s);
        if (n->a) { if (n->a->k == N_VAR) { if (exec(R, F, n->a) == C_THROW) { F->env = outer; return C_THROW; } } else if (eval(R, F, n->a) == CB_EMPTY) { F->env = outer; return C_THROW; } }
        for (;;) {
            if (perit) F->env = copy_env(F->env);
            if (n->b) { cbv c = eval(R, F, n->b); if (c == CB_EMPTY) { F->env = outer; return C_THROW; } if (!cb_truthy(c)) break; }
            int stop; int r = loop_body(R, F, n->d, labels, nl, &stop); if (stop) { F->env = outer; return r; }
            if (perit) F->env = copy_env(F->env);
            if (n->c && eval(R, F, n->c) == CB_EMPTY) { F->env = outer; return C_THROW; } }
        F->env = outer; return C_NORM; }
    case N_FORIN: case N_FOROF: return for_in_of(R, F, n, labels, nl); }
    return C_NORM; }
static int exec_list(cb_rt *R, fr *F, node *n) { for (; n; n = n->next) { int c = exec(R, F, n); if (c != C_NORM) return c; } return C_NORM; }
static int exec(cb_rt *R, fr *F, node *n) {
    if (R->frames) *((frame_link *)R->frames)->line = n->line;
    switch (n->k) {
    case N_EXPR: return eval(R, F, n->a) == CB_EMPTY ? C_THROW : C_NORM;
    case N_VAR:
        for (node *d = n->a; d; d = d->next) {
            if (!d->b) { if (n->op == VK_VAR) continue; if (bind(R, F, d->a, CB_UNDEF, n->op)) return C_THROW; continue; }
            cbv v = eval(R, F, d->b); if (v == CB_EMPTY) return C_THROW;
            if (d->a->k == N_ID && (d->b->k == N_FUNC || d->b->k == N_CLASS) && !d->b->fn->name) set_fn_name(R, v, d->a->v, 0);
            if (bind(R, F, d->a, v, n->op)) return C_THROW; }
        return C_NORM;
    case N_RETURN: { cbv v = n->a ? eval(R, F, n->a) : CB_UNDEF; if (v == CB_EMPTY) return C_THROW; if (F->gen && F->gen->isasyncgen && n->a) { v = do_await(R, F, v); if (v == CB_EMPTY) return C_THROW; } F->ret = v; return C_RET; }
    case N_IF: { cbv c = eval(R, F, n->a); if (c == CB_EMPTY) return C_THROW; if (cb_truthy(c)) return exec(R, F, n->b); return n->c ? exec(R, F, n->c) : C_NORM; }
    case N_BLOCK: { if (!n->sc || !n->sc->env) return exec_list(R, F, n->a); cb_env *outer = F->env; F->env = block_env(R, F, n->sc); hoist_funcs(R, F, n->sc); int c = exec_list(R, F, n->a); F->env = outer; return c; }
    case N_WHILE: case N_DOWHILE: case N_FOR: case N_FORIN: case N_FOROF: { cbv labels[4]; int nl = F->nl; memcpy(labels, F->labels, sizeof labels); F->nl = 0; return exec_loop(R, F, n, labels, nl); }
    case N_BREAK: F->label = n->v; return C_BRK;
    case N_CONTINUE: F->label = n->v; return C_CONT;
    case N_THROW: { cbv v = eval(R, F, n->a); if (v == CB_EMPTY) return C_THROW; cb_throwv(R, v); return C_THROW; }
    case N_LABEL: {
        node *t = n->a; if (F->nl < 4) F->labels[F->nl++] = n->v;
        int c = (t->k == N_WHILE || t->k == N_DOWHILE || t->k == N_FOR || t->k == N_FORIN || t->k == N_FOROF || t->k == N_LABEL) ? exec(R, F, t) : (F->nl = 0, exec(R, F, t));
        F->nl = 0; if (c == C_BRK && F->label == n->v) { F->label = 0; return C_NORM; } return c; }
    case N_TRY: {
        int c = exec(R, F, n->a); cbv saved_ret = F->ret;
        if (c == C_THROW && n->c) {
            cbv e = cb_take_exc(R);
            if (e == RET_MARK) { R->exc = e; R->has_exc = 1; }
            else { cb_env *outer = F->env; if (n->sc && n->sc->env) F->env = block_env(R, F, n->sc);
                if (n->b && bind(R, F, n->b, e, VK_LET)) c = C_THROW; else c = exec(R, F, n->c);
                F->env = outer; saved_ret = F->ret; } }
        if (n->d) {
            cbv exc = R->exc; int had = R->has_exc; R->has_exc = 0; cbv lab = F->label;
            int fc = exec(R, F, n->d);
            if (fc != C_NORM) return fc;
            if (had) { R->exc = exc; R->has_exc = 1; } F->label = lab; F->ret = saved_ret; }
        return c; }
    case N_SWITCH: {
        cbv d = eval(R, F, n->a); if (d == CB_EMPTY) return C_THROW;
        cb_env *outer = F->env; if (n->sc && n->sc->env) { F->env = block_env(R, F, n->sc); hoist_funcs(R, F, n->sc); }
        node *start = 0, *dflt = 0;
        for (node *c = n->b; c; c = c->next) { if (c->flags & 1) { dflt = c; continue; } cbv v = eval(R, F, c->a); if (v == CB_EMPTY) { F->env = outer; return C_THROW; } if (cb_strict_eq(d, v)) { start = c; break; } }
        if (!start) start = dflt;
        int r = C_NORM;
        for (node *c = start; c; c = c->next) { r = exec_list(R, F, c->b); if (r != C_NORM) break; }
        F->env = outer;
        if (r == C_BRK && !F->label) return C_NORM;
        return r; }
    case N_FUNCDECL: return C_NORM;
    case N_CLASSDECL: { cbv c = eval(R, F, n->a); if (c == CB_EMPTY) return C_THROW; node id; memset(&id, 0, sizeof id); id.v = n->v; id.hops = n->hops; id.slot = n->slot; return var_init(R, F, &id, c, VK_LET) ? C_THROW : C_NORM; }
    case N_EMPTY: case N_DEBUGGER: return C_NORM;
    case N_IMPORT: {
        if (!R->import_hook) { cb_throw(R, 3, "import no disponible"); return C_THROW; }
        char *spec = cb_str_utf8z(cb_s(n->v), 0); cbv ns = R->import_hook(R, spec, F->fi && F->fi->file ? F->fi->file : ""); free(spec); if (ns == CB_EMPTY) return C_THROW;
        for (node *q = n->a; q; q = q->next) { cbv v = (q->flags & 1) ? ns : cb_get(R, ns, q->a->v); if (v == CB_EMPTY) return C_THROW; if (var_init(R, F, q->b, v, VK_LET)) return C_THROW; }
        return C_NORM; }
    case N_EXPORT: {
        cbv ex = R->exports; if (!cb_isobj(ex)) return n->a ? exec(R, F, n->a) : C_NORM;
        if (n->flags == 1) { if (n->a) { int c = exec(R, F, n->a); if (c != C_NORM) return c; node *d = n->a; cbv v = CB_UNDEF; if (d->k == N_FUNCDECL || d->k == N_CLASSDECL) { node id; memset(&id, 0, sizeof id); id.v = d->v; id.hops = d->k == N_FUNCDECL ? 0 : d->hops; id.slot = d->slot; if (d->k == N_FUNCDECL) { id.hops = 0; } v = var_get(R, F, &id); } cb_set(R, ex, A_default, v); return C_NORM; }
            cbv v = eval(R, F, n->b); if (v == CB_EMPTY) return C_THROW; cb_set(R, ex, A_default, v); return C_NORM; }
        if (n->flags == 2) { char *spec = cb_str_utf8z(cb_s(n->v), 0); cbv ns = R->import_hook ? R->import_hook(R, spec, "") : CB_UNDEF; free(spec); if (ns == CB_EMPTY) return C_THROW;
            if (n->c) cb_set(R, ex, n->c->v, ns); else if (cb_isobj(ns)) { cbv ks = cb_own_keys(R, ns, 1); cb_obj *ka = cb_o(ks); for (u32 i = 0; i < ka->n1; i++) cb_set(R, ex, ka->e[i], cb_get(R, ns, ka->e[i])); } return C_NORM; }
        if (n->flags == 3) { cbv src = CB_UNDEF; if (n->v) { char *spec = cb_str_utf8z(cb_s(n->v), 0); src = R->import_hook ? R->import_hook(R, spec, "") : CB_UNDEF; free(spec); if (src == CB_EMPTY) return C_THROW; }
            for (node *q = n->c; q; q = q->next) { cbv v = n->v ? cb_get(R, src, q->a->v) : var_get(R, F, q->a); if (v == CB_EMPTY) return C_THROW; cb_set(R, ex, q->b->v, v); } return C_NORM; }
        int c = exec(R, F, n->a); if (c != C_NORM) return c;
        node *d = n->a;
        if (d->k == N_VAR) { for (node *x = d->a; x; x = x->next) if (x->a->k == N_ID) { cbv v = var_get(R, F, x->a); if (v == CB_EMPTY) return C_THROW; cb_set(R, ex, x->a->v, v); } }
        else if (d->k == N_FUNCDECL) { node id; memset(&id, 0, sizeof id); id.v = d->v; id.hops = 0; id.slot = d->slot; cbv v = var_get(R, F, &id); cb_set(R, ex, d->v, v); }
        else if (d->k == N_CLASSDECL) { node id; memset(&id, 0, sizeof id); id.v = d->v; id.hops = d->hops; id.slot = d->slot; cbv v = var_get(R, F, &id); cb_set(R, ex, d->v, v); }
        return C_NORM; }
    default: return eval(R, F, n) == CB_EMPTY ? C_THROW : C_NORM; } }

/* ---------------- scripts ---------------- */
cbv cb_run_script(cb_rt *R, node *prog) {
    if (!RET_MARK) { RET_MARK = cb_objv(cb_new_obj(R, 0)); cb_root(&RET_MARK); }
    if (prog->fn) {   /* modulo: como una funcion async con su propio ambito */
        cb_obj *f = cb_new_obj(R, R->FunctionP); f->cls = OC_FUNC; f->p1 = prog->fn; f->p2 = 0;
        node body; memset(&body, 0, sizeof body); body.k = N_BLOCK; body.a = prog->a; prog->fn->body = cb_blob(sizeof(node)); memcpy(prog->fn->body, &body, sizeof body);
        return cb_call_func(R, f, CB_UNDEF, 0, 0, CB_UNDEF); }
    fr F; memset(&F, 0, sizeof F); F.env = 0; F.strict = (int)prog->flags; F.ret = CB_UNDEF;
    scope *g = prog->sc;
    /* var y function de nivel superior: propiedades del objeto global; let/const/class: sin iniciar */
    for (int i = 0; i < g->n; i++) { cbv name = g->names[i]; int k = g->nkind[i];
        if (k == VK_VAR) { if (!cb_has(R, cb_objv(R->global), name)) cb_add_prop(R, R->global, name, CB_UNDEF, PF_W | PF_E); }
        else if (k == VK_LET || k == VK_CONST) { cb_prop *p = cb_own(R->lexical_holder, name); if (p) { char b[80]; return cb_throw(R, 3, "Identifier '%s' has already been declared", nm(name, b, 80)); } cb_add_prop(R, R->lexical_holder, name, CB_EMPTY, k == VK_CONST ? PF_E : PF_W | PF_E); } }
    for (int i = 0; i < g->nf; i++) { node *d = g->funcs[i]; cbv f = cb_new_func(R, d->a, 0, CB_UNDEF); cb_put(R, cb_objv(R->global), d->v, f, cb_objv(R->global), 0); }
    frame_link fl = { R->frames, 0, 0 }; u32 line = 0; fl.line = &line; R->frames = &fl;
    cbv last = CB_UNDEF; int c = C_NORM;
    for (node *s = prog->a; s; s = s->next) {
        if (s->k == N_EXPR) { cbv v = eval(R, &F, s->a); if (v == CB_EMPTY) { c = C_THROW; break; } last = v; continue; }
        c = exec(R, &F, s); if (c != C_NORM) break; }
    R->frames = fl.up;
    if (c == C_THROW) return CB_EMPTY;
    return last; }

/* ---------------- generadores async: cola de peticiones next/throw/return, cada una con su promesa ---------------- */
static void agen_drive(cb_rt *R, gen *g);
static void agen_settle_head(cb_rt *R, gen *g, int ok, cbv v) {
    cb_obj *q = cb_o(g->queue); u32 h = (u32)g->awaiting; (void)h;
    if (q->n1 < 4) return; cbv res = q->e[2], rej = q->e[3];
    memmove(q->e, q->e + 4, (u64)(q->n1 - 4) * 8); q->n1 -= 4;
    cb_call_v(R, ok ? res : rej, CB_UNDEF, 1, &v); }
static void agen_after(cb_rt *R, gen *g) {   /* tras reanudar: await pendiente, yield o fin */
    if (g->awaiting) return;
    if (g->done) { if (g->co) { cb_coro_free(g->co); g->co = 0; } if (!cb_isundef(g->exc)) { cbv e = g->exc; g->exc = CB_UNDEF; agen_settle_head(R, g, 0, e); } else agen_settle_head(R, g, 1, iter_result(R, g->result, 1)); g->result = CB_UNDEF; }
    else agen_settle_head(R, g, 1, iter_result(R, g->yielded, 0));
    agen_drive(R, g); }
static void agen_drive(cb_rt *R, gen *g) {
    while (!g->running && !g->awaiting && cb_isobj(g->queue) && cb_o(g->queue)->n1 >= 4) {
        cb_obj *q = cb_o(g->queue); int mode = (int)cb_d(q->e[0]); cbv v = q->e[1];
        if (!g->started && mode != 0) g->done = 1;
        if (g->done) { if (mode == 1) agen_settle_head(R, g, 0, v); else agen_settle_head(R, g, 1, iter_result(R, mode == 2 ? v : CB_UNDEF, 1)); continue; }
        if (!g->co) { g->co = cb_coro_new(gen_body, g); if (!g->co) { agen_settle_head(R, g, 0, cb_err_obj(R, 2, "sin memoria")); continue; } }
        g->started = 1; g->mode = mode; g->sent = v; g->running = 1; GR = R; void *old = R->curgen; cb_coro_resume(g->co); R->curgen = old; g->running = 0;
        if (g->awaiting) return;
        if (g->done) { if (g->co) { cb_coro_free(g->co); g->co = 0; } if (!cb_isundef(g->exc)) { cbv e = g->exc; g->exc = CB_UNDEF; agen_settle_head(R, g, 0, e); } else agen_settle_head(R, g, 1, iter_result(R, g->result, 1)); }
        else agen_settle_head(R, g, 1, iter_result(R, g->yielded, 0)); } }
cbv cb_asyncgen_resume(cb_rt *R, cb_obj *go, int mode, cbv v) {
    gen *g = go->p1; cbv res, rej; cbv P = cb_promise_new(R, &res, &rej);
    if (!cb_isobj(g->queue)) g->queue = cb_objv(cb_new_array(R, 0));
    cb_obj *q = cb_o(g->queue); cb_arr_push(R, q, cb_num(mode)); cb_arr_push(R, q, v); cb_arr_push(R, q, res); cb_arr_push(R, q, rej);
    agen_drive(R, g); return P; }
void cb_agen_after_await(cb_rt *R, void *gp) { gen *g = gp; if (g->isasyncgen) agen_after(R, g); }
