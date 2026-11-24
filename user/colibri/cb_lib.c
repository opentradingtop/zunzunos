/* Colibri: arranque del motor y biblioteca basica: Object, Function, Array, String, Number, Boolean, Symbol, Math,
   Error, JSON y funciones globales. */
#include "cbi.h"
int cb_fmt_num(double d, char *o);
int cb_dtoa_fixed(double v, int mode, int nd, char *dig, int *kout);
double cb_parse_number(const char *s, int len);
void cb_obj_init(cb_rt *R);
void cb_define_acc(cb_rt *R, cb_obj *o, cbv key, cbv get, cbv set, u32 flags);
int cb_get_own_desc(cb_rt *R, cb_obj *o, cbv key, cbv *val, u32 *flags, cbv *get, cbv *set);
int cb_has_own(cb_rt *R, cb_obj *o, cbv key);
void cb_ast_free(void);
void cb_run_reset(void);
void *cb_stack_top(void);
cbv cb_binop(cb_rt *R, int op, cbv a, cbv b);
double cb_math1(int which, double x);
double cb_pow(double a, double b);
double cb_fmod(double a, double b);
double cb_atan2(double a, double b);
double cb_random(void);
cbv cb_proxy_keys(cb_rt *R, cb_obj *px);

#define NF(name) static cbv name(cb_rt *R, cbv t, int argc, cbv *argv, cb_obj *fn)
#define ARG(i) ((i) < argc ? argv[i] : CB_UNDEF)
#define TRY(x) do { if ((x) == CB_EMPTY) return CB_EMPTY; } while (0)
static cb_obj *mkctor(cb_rt *R, const char *name, cb_native f, int nargs, cb_obj *proto) {
    cbv c = cb_new_native(R, name, f, nargs); cb_obj *co = cb_o(c); co->fk |= 2;
    if (proto) { cb_add_prop(R, co, A_prototype, cb_objv(proto), 0); cb_add_prop(R, proto, A_constructor, c, PF_W | PF_C); }
    cb_add_prop(R, R->global, cb_atom(R, name), c, PF_W | PF_C); return co; }
static cbv str(cb_rt *R, const char *s) { return cb_mkstr(R, s); }

/* ---------------- arranque ---------------- */
void cb_lib_init(cb_rt *R);
cb_rt *cb_new(void) {
    cb_rt *R = calloc(1, sizeof *R); if (!R) return 0;
    cb_mem_init(R); R->exc = CB_UNDEF; R->nt = CB_UNDEF; R->exports = CB_UNDEF;
    if (!cb_stack_top()) cb_set_stack_top((void *)0x7FFFFFF00000ULL);
    R->stack_limit = (char *)cb_stack_top() - 7 * 1024 * 1024;
    cb_gc_off = 1;
    cb_obj_init(R);
    R->ObjectP = cb_new_obj(R, 0);
    R->FunctionP = cb_new_obj(R, R->ObjectP); R->FunctionP->cls = OC_NATIVE;
    R->global = cb_new_obj(R, R->ObjectP); R->lexical_holder = cb_new_obj(R, 0);
    cb_lib_init(R); cb_lib2_init(R); cb_lib3_init(R);
    cb_gc_off = 0;
    return R; }
void cb_free(cb_rt *R) { if (!R) return; cb_coro_free_all(); cb_mem_free(); cb_atoms_free(); cb_ast_free(); cb_run_reset(); free(R); }
int cb_eval(cb_rt *R, const char *src, long len, const char *name, cbv *res, int module) {
    char err[400]; node *prog = cb_parse(R, src, len, name, module, err, sizeof err);
    if (!prog) { R->exc = cb_objv(cb_new_err(R, 3, cb_mkstr(R, err + (strncmp(err, "SyntaxError: ", 13) ? 0 : 13)))); R->has_exc = 1; return -1; }
    cbv v = cb_run_script(R, prog);
    if (v == CB_EMPTY) return -1;
    if (res) *res = v; return 0; }
int cb_call(cb_rt *R, cbv f, cbv thisv, int argc, cbv *argv, cbv *res) { cbv r = cb_call_v(R, f, thisv, argc, argv); if (r == CB_EMPTY) return -1; if (res) *res = r; return 0; }
void cb_enqueue(cb_rt *R, cbv fn, cbv a, cbv b) {
    if (R->njobs + 3 > R->capjobs) { u32 live = R->njobs - R->jhead; u32 nc = (live + 3) * 2 + 96; cbv *nj = cb_blob((u64)nc * 8); if (live) memcpy(nj, R->jobs + R->jhead, (u64)live * 8); R->jobs = nj; R->njobs = live; R->jhead = 0; R->capjobs = nc; }
    R->jobs[R->njobs++] = fn; R->jobs[R->njobs++] = a; R->jobs[R->njobs++] = b; }
int cb_run_jobs(cb_rt *R) {
    int n = 0;
    while (R->jhead < R->njobs) {
        cbv f = R->jobs[R->jhead], a[2] = { R->jobs[R->jhead + 1], R->jobs[R->jhead + 2] }; R->jhead += 3; n++;
        cbv r = cb_call_v(R, f, CB_UNDEF, 2, a);
        if (r == CB_EMPTY) { cbv e = cb_take_exc(R); char *s = cb_to_utf8(R, e, 0); if (!s) { cb_take_exc(R); s = strdup("?"); } cb_print(R, 2, s, (long)strlen(s)); free(s); } }
    R->jhead = R->njobs = 0; return n; }
void cb_print(cb_rt *R, int level, const char *s, long n) { if (R->print) R->print(R->pu, level, s, (int)n); }

/* ---------------- ayudas para objetos tipo array ---------------- */
static double lenof(cb_rt *R, cbv o) { if (cb_isobj(o) && cb_o(o)->cls == OC_ARRAY) return cb_o(o)->n1; cbv l = cb_get(R, o, A_length); if (l == CB_EMPTY) return -1; double d = cb_to_int(R, l); if (R->has_exc) return -1; return d < 0 ? 0 : d > 9007199254740991.0 ? 9007199254740991.0 : d; }
static cbv geti(cb_rt *R, cbv o, double i) { if (cb_isobj(o)) { cb_obj *a = cb_o(o); if (a->cls == OC_ARRAY && i < a->n1 && i < a->n2 && a->e[(u32)i] != CB_EMPTY) return a->e[(u32)i]; } return cb_get(R, o, cb_key(R, cb_num(i))); }
static int seti(cb_rt *R, cbv o, double i, cbv v) { if (cb_isobj(o)) { cb_obj *a = cb_o(o); if (a->cls == OC_ARRAY && i < 4294967295.0 && !a->sealed && a->ext) return cb_arr_set(R, a, (u32)i, v); } return cb_put(R, o, cb_key(R, cb_num(i)), v, o, 1); }
static int hasi(cb_rt *R, cbv o, double i) { if (cb_isobj(o)) { cb_obj *a = cb_o(o); if (a->cls == OC_ARRAY) { if (i < a->n1 && i < a->n2 && a->e[(u32)i] != CB_EMPTY) return 1; if (!a->proto || a->proto == R->ArrayP) return 0; } } return cb_has(R, o, cb_key(R, cb_num(i))); }
static int setlen(cb_rt *R, cbv o, double n) { if (cb_isobj(o) && cb_o(o)->cls == OC_ARRAY) { if (cb_o(o)->sealed) return cb_throw(R, 1, "Cannot modify a frozen array"), -1; cb_arr_setlen(R, cb_o(o), (u32)n); return 0; } return cb_put(R, o, A_length, cb_num(n), o, 1); }
static cbv thisobj(cb_rt *R, cbv t) { if (cb_isnullish(t)) return cb_throw(R, 1, "Array.prototype method called on null or undefined"); return cb_objv(cb_to_obj(R, t)); }
static double relidx(double r, double len) { if (r < 0) { r += len; return r < 0 ? 0 : r; } return r > len ? len : r; }

/* ---------------- Object ---------------- */
NF(Object_ctor) { cbv v = ARG(0); if (!cb_isundef(R->nt) && R->nt != cb_objv(fn)) return cb_ctor_this(R, R->nt, R->ObjectP); if (cb_isnullish(v)) return cb_objv(cb_new_obj(R, R->ObjectP)); return cb_objv(cb_to_obj(R, v)); }
static cbv own_list(cb_rt *R, cbv o, int which, int mode) {   /* mode 0 claves, 1 valores, 2 pares */
    cb_obj *ob = cb_to_obj(R, o); if (!ob) return CB_EMPTY; cbv ks = cb_own_keys(R, cb_objv(ob), which); TRY(ks); cb_obj *ka = cb_o(ks), *out = cb_new_array(R, 0);
    for (u32 i = 0; i < ka->n1; i++) { cbv k = ka->e[i], dv, g, s; u32 fl; int r = cb_get_own_desc(R, ob, k, &dv, &fl, &g, &s); if (r < 0) return CB_EMPTY; if (!r || !(fl & PF_E)) continue;
        if (mode == 0) cb_arr_push(R, out, k); else { cbv v = cb_get(R, cb_objv(ob), k); TRY(v); if (mode == 1) cb_arr_push(R, out, v); else { cbv pr[2] = { k, v }; cb_arr_push(R, out, cb_list_to_array(R, 2, pr)); } } }
    return cb_objv(out); }
NF(Object_keys) { return own_list(R, ARG(0), 1, 0); }
NF(Object_values) { return own_list(R, ARG(0), 1, 1); }
NF(Object_entries) { return own_list(R, ARG(0), 1, 2); }
NF(Object_assign) { cb_obj *to = cb_to_obj(R, ARG(0)); if (!to) return CB_EMPTY;
    for (int i = 1; i < argc; i++) { if (cb_isnullish(argv[i])) continue; cb_obj *from = cb_to_obj(R, argv[i]); cbv ks = cb_own_keys(R, cb_objv(from), 3); TRY(ks); cb_obj *ka = cb_o(ks);
        for (u32 j = 0; j < ka->n1; j++) { cbv k = ka->e[j], dv, g, s; u32 fl; int r = cb_get_own_desc(R, from, k, &dv, &fl, &g, &s); if (r < 0) return CB_EMPTY; if (!r || !(fl & PF_E)) continue; cbv v = cb_get(R, cb_objv(from), k); TRY(v); if (cb_put(R, cb_objv(to), k, v, cb_objv(to), 1)) return CB_EMPTY; } }
    return cb_objv(to); }
static int to_desc(cb_rt *R, cbv d, cbv *val, u32 *flags, cbv *get, cbv *set, u32 *has) {
    if (!cb_isobj(d)) { cb_throw(R, 1, "Property description must be an object"); return -1; }
    *val = *get = *set = CB_UNDEF; *flags = 0; *has = 0;
    struct { cbv k; u32 f; } bits[] = { { A_enumerable, PF_E }, { A_configurable, PF_C }, { A_writable, PF_W } };
    for (int i = 0; i < 3; i++) if (cb_has(R, d, bits[i].k)) { *has |= bits[i].f; cbv v = cb_get(R, d, bits[i].k); if (v == CB_EMPTY) return -1; if (cb_truthy(v)) *flags |= bits[i].f; }
    if (cb_has(R, d, A_value)) { *has |= 64; *val = cb_get(R, d, A_value); }
    if (cb_has(R, d, A_get)) { *has |= 128; *get = cb_get(R, d, A_get); if (!cb_isundef(*get) && !cb_isfunc(*get)) { cb_throw(R, 1, "Getter must be a function"); return -1; } }
    if (cb_has(R, d, A_set)) { *has |= 256; *set = cb_get(R, d, A_set); if (!cb_isundef(*set) && !cb_isfunc(*set)) { cb_throw(R, 1, "Setter must be a function"); return -1; } }
    return 0; }
static int define_from_desc(cb_rt *R, cb_obj *o, cbv k, cbv d) {
    cbv val, get, set; u32 fl, has; if (to_desc(R, d, &val, &fl, &get, &set, &has)) return -1;
    if (o->cls == OC_PROXY) { cbv tr = cb_getp(R, o->v2, "defineProperty"); if (tr == CB_EMPTY) return -1; if (cb_isfunc(tr)) { cbv a[3] = { o->v1, k, d }; return cb_call_v(R, tr, o->v2, 3, a) == CB_EMPTY ? -1 : 0; } return define_from_desc(R, cb_o(o->v1), k, d); }
    cbv ov, og, os; u32 of; int ex = cb_get_own_desc(R, o, k, &ov, &of, &og, &os); if (ex < 0) return -1;
    if (ex && !(of & PF_C)) {   /* no configurable: solo se puede cambiar el valor si es escribible */
        if ((has & 64) && (of & PF_W) && !(has & (128 | 256))) { cb_define(R, o, k, val, of & ~(u32)((has & PF_W) && !(fl & PF_W) ? PF_W : 0)); return 0; }
        if ((has & PF_W) && !(fl & PF_W) && (of & PF_W) && !(has & (128 | 256))) { cb_prop *p = cb_own(o, k); if (p) p->flags &= ~PF_W; return 0; }
        if (!(has & (64 | 128 | 256)) && (has & PF_E) == 0) return 0;
        if ((has & 64) && cb_same_value(val, ov, 0)) return 0;
        cb_throw(R, 1, "Cannot redefine property"); return -1; }
    if (has & (128 | 256)) { cb_define_acc(R, o, k, (has & 128) ? get : (ex && (of & PF_ACC) ? og : CB_UNDEF), (has & 256) ? set : (ex && (of & PF_ACC) ? os : CB_UNDEF), ((has & PF_E) ? fl & PF_E : ex ? of & PF_E : 0) | ((has & PF_C) ? fl & PF_C : ex ? of & PF_C : 0)); return 0; }
    u32 nf = ((has & PF_W) ? fl & PF_W : ex && !(of & PF_ACC) ? of & PF_W : 0) | ((has & PF_E) ? fl & PF_E : ex ? of & PF_E : 0) | ((has & PF_C) ? fl & PF_C : ex ? of & PF_C : 0);
    if (!ex && !o->ext) { cb_throw(R, 1, "Cannot define property, object is not extensible"); return -1; }
    return cb_define(R, o, k, (has & 64) ? val : ex && !(of & PF_ACC) ? ov : CB_UNDEF, nf); }
NF(Object_defineProperty) { if (!cb_isobj(ARG(0))) return cb_throw(R, 1, "Object.defineProperty called on non-object"); cbv k = cb_key(R, ARG(1)); TRY(k); if (define_from_desc(R, cb_o(ARG(0)), k, ARG(2))) return CB_EMPTY; return ARG(0); }
NF(Object_defineProperties) { if (!cb_isobj(ARG(0))) return cb_throw(R, 1, "Object.defineProperties called on non-object"); cb_obj *ps = cb_to_obj(R, ARG(1)); if (!ps) return CB_EMPTY; cbv ks = cb_own_keys(R, cb_objv(ps), 7); TRY(ks); cb_obj *ka = cb_o(ks);
    for (u32 i = 0; i < ka->n1; i++) { cbv d = cb_get(R, cb_objv(ps), ka->e[i]); TRY(d); if (define_from_desc(R, cb_o(ARG(0)), ka->e[i], d)) return CB_EMPTY; } return ARG(0); }
NF(Object_create) { cbv p = ARG(0); if (!cb_isobj(p) && !cb_isnull(p)) return cb_throw(R, 1, "Object prototype may only be an Object or null"); cb_obj *o = cb_new_obj(R, cb_isnull(p) ? 0 : cb_o(p)); if (!cb_isundef(ARG(1))) { cbv a[2] = { cb_objv(o), ARG(1) }; if (Object_defineProperties(R, CB_UNDEF, 2, a, fn) == CB_EMPTY) return CB_EMPTY; } return cb_objv(o); }
NF(Object_getOwnPropertyNames) { cb_obj *o = cb_to_obj(R, ARG(0)); if (!o) return CB_EMPTY; return cb_own_keys(R, cb_objv(o), 1); }
NF(Object_getOwnPropertySymbols) { cb_obj *o = cb_to_obj(R, ARG(0)); if (!o) return CB_EMPTY; cbv ks = cb_own_keys(R, cb_objv(o), 2); TRY(ks); cb_obj *ka = cb_o(ks), *out = cb_new_array(R, 0); for (u32 i = 0; i < ka->n1; i++) if (((cb_sym *)CB_PTR(ka->e[i]))->reg != 2) cb_arr_push(R, out, ka->e[i]); return cb_objv(out); }
static cbv from_desc(cb_rt *R, cbv val, u32 fl, cbv g, cbv s) {
    cb_obj *d = cb_new_obj(R, R->ObjectP);
    if (fl & PF_ACC) { cb_add_prop(R, d, A_get, g, PF_WEC); cb_add_prop(R, d, A_set, s, PF_WEC); }
    else { cb_add_prop(R, d, A_value, val, PF_WEC); cb_add_prop(R, d, A_writable, cb_bool(fl & PF_W), PF_WEC); }
    cb_add_prop(R, d, A_enumerable, cb_bool(fl & PF_E), PF_WEC); cb_add_prop(R, d, A_configurable, cb_bool(fl & PF_C), PF_WEC); return cb_objv(d); }
NF(Object_getOwnPropertyDescriptor) { cb_obj *o = cb_to_obj(R, ARG(0)); if (!o) return CB_EMPTY; cbv k = cb_key(R, ARG(1)); TRY(k); cbv v, g, s; u32 fl; int r = cb_get_own_desc(R, o, k, &v, &fl, &g, &s); if (r < 0) return CB_EMPTY; if (!r) return CB_UNDEF; return from_desc(R, v, fl, g, s); }
NF(Object_getOwnPropertyDescriptors) { cb_obj *o = cb_to_obj(R, ARG(0)); if (!o) return CB_EMPTY; cbv ks = cb_own_keys(R, cb_objv(o), 3); TRY(ks); cb_obj *ka = cb_o(ks), *out = cb_new_obj(R, R->ObjectP);
    for (u32 i = 0; i < ka->n1; i++) { cbv v, g, s; u32 fl; if (cb_get_own_desc(R, o, ka->e[i], &v, &fl, &g, &s) > 0) cb_add_prop(R, out, ka->e[i], from_desc(R, v, fl, g, s), PF_WEC); } return cb_objv(out); }
NF(Object_getPrototypeOf) { cb_obj *o = cb_to_obj(R, ARG(0)); if (!o) return CB_EMPTY; if (o->cls == OC_PROXY) { cbv tr = cb_getp(R, o->v2, "getPrototypeOf"); TRY(tr); if (cb_isfunc(tr)) return cb_call_v(R, tr, o->v2, 1, &o->v1); return Object_getPrototypeOf(R, t, 1, &o->v1, fn); } return o->proto ? cb_objv(o->proto) : CB_NULL; }
NF(Object_setPrototypeOf) { cbv o = ARG(0), p = ARG(1); if (!cb_isobj(p) && !cb_isnull(p)) return cb_throw(R, 1, "Object prototype may only be an Object or null"); if (!cb_isobj(o)) return o;
    for (cb_obj *c = cb_isnull(p) ? 0 : cb_o(p); c; c = c->proto) if (c == cb_o(o)) return cb_throw(R, 1, "Cyclic __proto__ value"); cb_o(o)->proto = cb_isnull(p) ? 0 : cb_o(p); return o; }
static void set_integrity(cb_rt *R, cb_obj *o, int lvl) { o->ext = 0; if (o->cls == OC_ARRAY || o->cls == OC_ARGS) o->sealed = (u8)lvl; for (u32 i = 0; i < o->np; i++) { cb_prop *p = &o->props[i]; if (p->flags & PF_DEL) continue; p->flags &= ~PF_C; if (lvl == 2 && !(p->flags & PF_ACC)) p->flags &= ~PF_W; } }
NF(Object_freeze) { if (cb_isobj(ARG(0))) set_integrity(R, cb_o(ARG(0)), 2); return ARG(0); }
NF(Object_seal) { if (cb_isobj(ARG(0))) set_integrity(R, cb_o(ARG(0)), 1); return ARG(0); }
NF(Object_preventExtensions) { if (cb_isobj(ARG(0))) cb_o(ARG(0))->ext = 0; return ARG(0); }
static int test_integrity(cb_obj *o, int lvl) { if (o->ext) return 0; if ((o->cls == OC_ARRAY || o->cls == OC_ARGS) && o->n1 && o->sealed < lvl) return 0; for (u32 i = 0; i < o->np; i++) { cb_prop *p = &o->props[i]; if (p->flags & PF_DEL) continue; if (p->flags & PF_C) return 0; if (lvl == 2 && !(p->flags & PF_ACC) && (p->flags & PF_W)) return 0; } return 1; }
NF(Object_isFrozen) { return cb_bool(!cb_isobj(ARG(0)) || test_integrity(cb_o(ARG(0)), 2)); }
NF(Object_isSealed) { return cb_bool(!cb_isobj(ARG(0)) || test_integrity(cb_o(ARG(0)), 1)); }
NF(Object_isExtensible) { return cb_bool(cb_isobj(ARG(0)) && cb_o(ARG(0))->ext); }
NF(Object_is) { return cb_bool(cb_same_value(ARG(0), ARG(1), 0)); }
NF(Object_hasOwn) { cb_obj *o = cb_to_obj(R, ARG(0)); if (!o) return CB_EMPTY; cbv k = cb_key(R, ARG(1)); TRY(k); return cb_bool(cb_has_own(R, o, k)); }
NF(Object_fromEntries) { cb_obj *o = cb_new_obj(R, R->ObjectP); cbv it = cb_get_iter(R, ARG(0), 0); TRY(it); cbv e; int r;
    while ((r = cb_iter_next(R, it, &e)) > 0) { cbv k = cb_get(R, e, cb_idx_key(R, 0)); TRY(k); cbv v = cb_get(R, e, cb_idx_key(R, 1)); TRY(v); k = cb_key(R, k); TRY(k); cb_define(R, o, k, v, PF_WEC); } if (r < 0) return CB_EMPTY; return cb_objv(o); }
NF(Object_groupBy) { cb_obj *o = cb_new_obj(R, 0); cbv it = cb_get_iter(R, ARG(0), 0); TRY(it); cbv e; int r, i = 0;
    while ((r = cb_iter_next(R, it, &e)) > 0) { cbv a[2] = { e, cb_num(i++) }; cbv k = cb_call_v(R, ARG(1), CB_UNDEF, 2, a); TRY(k); k = cb_key(R, k); TRY(k); cb_prop *p = cb_own(o, k); if (!p) { cb_add_prop(R, o, k, cb_objv(cb_new_array(R, 0)), PF_WEC); p = cb_own(o, k); } cb_arr_push(R, cb_o(p->val), e); } if (r < 0) return CB_EMPTY; return cb_objv(o); }
NF(OP_hasOwnProperty) { cbv k = cb_key(R, ARG(0)); TRY(k); cb_obj *o = cb_to_obj(R, t); if (!o) return CB_EMPTY; return cb_bool(cb_has_own(R, o, k)); }
NF(OP_isPrototypeOf) { if (!cb_isobj(ARG(0)) || !cb_isobj(t)) return CB_FALSE; for (cb_obj *c = cb_o(ARG(0))->proto; c; c = c->proto) if (c == cb_o(t)) return CB_TRUE; return CB_FALSE; }
NF(OP_propertyIsEnumerable) { cbv k = cb_key(R, ARG(0)); TRY(k); cb_obj *o = cb_to_obj(R, t); if (!o) return CB_EMPTY; cbv v, g, s; u32 fl; int r = cb_get_own_desc(R, o, k, &v, &fl, &g, &s); return cb_bool(r > 0 && (fl & PF_E)); }
NF(OP_toString) {
    if (cb_isundef(t)) return str(R, "[object Undefined]"); if (cb_isnull(t)) return str(R, "[object Null]");
    cb_obj *o = cb_to_obj(R, t); const char *b = "Object";
    switch (o->cls) { case OC_ARRAY: b = "Array"; break; case OC_FUNC: case OC_NATIVE: case OC_BOUND: b = "Function"; break; case OC_ERROR: b = "Error"; break; case OC_BOOL: b = "Boolean"; break; case OC_NUMBER: b = "Number"; break; case OC_STRING: b = "String"; break; case OC_DATE: b = "Date"; break; case OC_REGEXP: b = "RegExp"; break; case OC_ARGS: b = "Arguments"; break; }
    cbv tag = cb_get(R, cb_objv(o), S_toStringTag); TRY(tag);
    cb_str *r = cb_s(str(R, "[object ")); r = cb_concat(r, cb_isstr(tag) ? cb_s(tag) : cb_s(str(R, b))); return cb_strv(cb_concat(r, cb_s(str(R, "]")))); }
NF(OP_toLocaleString) { return cb_invoke(R, t, "toString", 0, 0); }
NF(OP_valueOf) { cb_obj *o = cb_to_obj(R, t); if (!o) return CB_EMPTY; return cb_objv(o); }
NF(OP_proto_get) { cb_obj *o = cb_to_obj(R, t); if (!o) return CB_EMPTY; return o->proto ? cb_objv(o->proto) : CB_NULL; }
NF(OP_proto_set) { cbv p = ARG(0); if (!cb_isobj(t) || (!cb_isobj(p) && !cb_isnull(p))) return CB_UNDEF; cbv a[2] = { t, p }; return Object_setPrototypeOf(R, CB_UNDEF, 2, a, fn) == CB_EMPTY ? CB_EMPTY : CB_UNDEF; }

/* ---------------- Function ---------------- */
NF(Function_ctor) {
    cb_str *params = cb_s(str(R, "")); for (int i = 0; i + 1 < argc; i++) { cbv s = cb_to_str(R, argv[i]); TRY(s); if (i) params = cb_concat(params, cb_s(str(R, ","))); params = cb_concat(params, cb_s(s)); }
    cbv body = argc ? cb_to_str(R, argv[argc - 1]) : str(R, ""); TRY(body);
    cb_str *src = cb_concat(cb_concat(cb_concat(cb_concat(cb_s(str(R, "(function anonymous(")), params), cb_s(str(R, "\n) {\n"))), cb_s(body)), cb_s(str(R, "\n})")));
    long n; char *u = cb_str_utf8z(src, &n); cbv res; int e = cb_eval(R, u, n, "Function", &res, 0); free(u); if (e) return CB_EMPTY; return res; }
NF(FP_call) { if (!cb_isfunc(t)) return cb_throw(R, 1, "Function.prototype.call called on non-function"); return cb_call_v(R, t, ARG(0), argc > 1 ? argc - 1 : 0, argv + 1); }
NF(FP_apply) { if (!cb_isfunc(t)) return cb_throw(R, 1, "Function.prototype.apply was called on a non-function"); u32 n; cbv *e; cbv r = cb_array_like(R, ARG(1), &n, &e); TRY(r); return cb_call_v(R, t, ARG(0), (int)n, e); }
NF(FP_bind) {
    if (!cb_isfunc(t)) return cb_throw(R, 1, "Bind must be called on a function");
    cb_obj *b = cb_new_obj(R, cb_o(t)->proto); b->cls = OC_BOUND; b->v1 = t; b->v2 = ARG(0); int nb = argc > 1 ? argc - 1 : 0; b->n1 = (u32)nb; if (nb) { b->e = cb_blob((u64)nb * 8); for (int i = 0; i < nb; i++) b->e[i] = argv[i + 1]; }
    cbv l = cb_get(R, t, A_length); TRY(l); double ln = cb_isnum(l) ? cb_d(l) - nb : 0; if (ln < 0) ln = 0;
    cbv n = cb_get(R, t, A_name); TRY(n); cb_add_prop(R, b, A_length, cb_num(ln), PF_C); cb_add_prop(R, b, A_name, cb_strv(cb_concat(cb_s(str(R, "bound ")), cb_isstr(n) ? cb_s(n) : cb_s(str(R, "")))), PF_C);
    return cb_objv(b); }
NF(FP_toString) {
    if (!cb_isfunc(t)) return cb_throw(R, 1, "Function.prototype.toString requires that 'this' be a Function");
    cb_obj *o = cb_o(t);
    if (o->cls == OC_FUNC) { fninfo *fi = o->p1; if (fi->src && fi->src_end > fi->src_start) return cb_str_utf8(R, fi->src + fi->src_start, fi->src_end - fi->src_start); }
    cbv n = cb_get(R, t, A_name); char b[200]; long nl = 0; char *u = cb_isstr(n) ? cb_str_utf8z(cb_s(n), &nl) : 0; cb_snprintf(b, 200, "function %s() { [native code] }", u ? u : ""); free(u); return str(R, b); }
NF(FP_hasInstance) { return cb_instance_of(R, ARG(0), t); }
NF(FP_empty) { return CB_UNDEF; }

/* ---------------- Array ---------------- */
NF(Array_ctor) {
    cb_obj *a;
    if (argc == 1 && cb_isnum(argv[0])) { double d = cb_d(argv[0]); if (d < 0 || d != (double)(u32)d) return cb_throw(R, 2, "Invalid array length"); a = cb_new_array(R, 0); cb_arr_setlen(R, a, (u32)d); }
    else { a = cb_new_array(R, (u32)argc); for (int i = 0; i < argc; i++) a->e[i] = argv[i]; }
    if (!cb_isundef(R->nt) && R->nt != cb_objv(fn)) { cbv p = cb_get(R, R->nt, A_prototype); TRY(p); if (cb_isobj(p)) a->proto = cb_o(p); }
    return cb_objv(a); }
NF(Array_isArray) { cbv v = ARG(0); while (cb_isobj(v) && cb_o(v)->cls == OC_PROXY) v = cb_o(v)->v1; return cb_bool(cb_isobj(v) && cb_o(v)->cls == OC_ARRAY); }
static void sub_proto(cb_rt *R, cbv ctor, cb_obj *a) {   /* Array.from/of llamados sobre una subclase de Array */
    if (cb_isobj(ctor) && cb_is_ctor(ctor) && cb_o(ctor) != R->ArrayC) { cbv p = cb_get(R, ctor, A_prototype); if (cb_isobj(p)) a->proto = cb_o(p); else if (p == CB_EMPTY) cb_take_exc(R); } }
NF(Array_of) { cbv a = cb_list_to_array(R, argc, argv); sub_proto(R, t, cb_o(a)); return a; }
NF(Array_from) {
    cbv src = ARG(0), mapf = ARG(1); if (!cb_isundef(mapf) && !cb_isfunc(mapf)) return cb_throw(R, 1, "Array.from: mapper is not a function");
    cb_obj *out = cb_new_array(R, 0); sub_proto(R, t, out); if (cb_isnullish(src)) return cb_throw(R, 1, "Array.from requires an array-like object");
    cbv itf = cb_get(R, src, S_iterator); TRY(itf);
    if (!cb_isnullish(itf)) { cbv it = cb_get_iter(R, src, 0); TRY(it); cbv x; int r; double i = 0;
        while ((r = cb_iter_next(R, it, &x)) > 0) { if (!cb_isundef(mapf)) { cbv a[2] = { x, cb_num(i) }; x = cb_call_v(R, mapf, ARG(2), 2, a); if (x == CB_EMPTY) { cb_iter_close(R, it); return x; } } cb_arr_push(R, out, x); i++; }
        if (r < 0) return CB_EMPTY; return cb_objv(out); }
    double n = lenof(R, src); if (n < 0) return CB_EMPTY;
    for (double i = 0; i < n; i++) { cbv x = geti(R, src, i); TRY(x); if (!cb_isundef(mapf)) { cbv a[2] = { x, cb_num(i) }; x = cb_call_v(R, mapf, ARG(2), 2, a); TRY(x); } cb_arr_push(R, out, x); }
    return cb_objv(out); }
NF(AP_push) { cbv o = thisobj(R, t); TRY(o); if (cb_o(o)->cls == OC_ARRAY && !cb_o(o)->sealed && cb_o(o)->ext) { for (int i = 0; i < argc; i++) cb_arr_push(R, cb_o(o), argv[i]); return cb_num(cb_o(o)->n1); }
    double n = lenof(R, o); if (n < 0) return CB_EMPTY; for (int i = 0; i < argc; i++) if (seti(R, o, n++, argv[i])) return CB_EMPTY; if (setlen(R, o, n)) return CB_EMPTY; return cb_num(n); }
NF(AP_pop) { cbv o = thisobj(R, t); TRY(o); double n = lenof(R, o); if (n < 0) return CB_EMPTY; if (n == 0) { setlen(R, o, 0); return CB_UNDEF; } cbv v = geti(R, o, n - 1); TRY(v); if (cb_o(o)->cls != OC_ARRAY) cb_del(R, o, cb_key(R, cb_num(n - 1)), 1); if (setlen(R, o, n - 1)) return CB_EMPTY; return v; }
NF(AP_shift) { cbv o = thisobj(R, t); TRY(o); double n = lenof(R, o); if (n < 0) return CB_EMPTY; if (n == 0) { setlen(R, o, 0); return CB_UNDEF; }
    cb_obj *a = cb_o(o); if (a->cls == OC_ARRAY && a->n1 <= a->n2 && !a->sealed) { cbv v = a->e[0] == CB_EMPTY ? CB_UNDEF : a->e[0]; memmove(a->e, a->e + 1, (u64)(a->n1 - 1) * 8); a->n1--; a->e[a->n1] = CB_EMPTY; return v; }
    cbv v = geti(R, o, 0); TRY(v); for (double i = 1; i < n; i++) { if (hasi(R, o, i)) { cbv x = geti(R, o, i); TRY(x); if (seti(R, o, i - 1, x)) return CB_EMPTY; } else cb_del(R, o, cb_key(R, cb_num(i - 1)), 1); } cb_del(R, o, cb_key(R, cb_num(n - 1)), 1); if (setlen(R, o, n - 1)) return CB_EMPTY; return v; }
NF(AP_unshift) { cbv o = thisobj(R, t); TRY(o); double n = lenof(R, o); if (n < 0) return CB_EMPTY;
    cb_obj *a = cb_o(o); if (a->cls == OC_ARRAY && a->n1 <= a->n2 && !a->sealed) { u32 old = a->n1; cb_arr_setlen(R, a, old + (u32)argc); memmove(a->e + argc, a->e, (u64)old * 8); for (int i = 0; i < argc; i++) a->e[i] = argv[i]; return cb_num(a->n1); }
    for (double i = n; i-- > 0;) { if (hasi(R, o, i)) { cbv x = geti(R, o, i); TRY(x); if (seti(R, o, i + argc, x)) return CB_EMPTY; } else cb_del(R, o, cb_key(R, cb_num(i + argc)), 1); }
    for (int i = 0; i < argc; i++) if (seti(R, o, i, argv[i])) return CB_EMPTY; if (setlen(R, o, n + argc)) return CB_EMPTY; return cb_num(n + argc); }
NF(AP_slice) { cbv o = thisobj(R, t); TRY(o); double n = lenof(R, o); if (n < 0) return CB_EMPTY; double a = relidx(cb_to_int(R, ARG(0)), n), b = cb_isundef(ARG(1)) ? n : relidx(cb_to_int(R, ARG(1)), n);
    cb_obj *out = cb_new_array(R, 0); for (double i = a; i < b; i++) { if (hasi(R, o, i)) { cbv x = geti(R, o, i); TRY(x); cb_arr_set(R, out, (u32)(i - a), x); } } cb_arr_setlen(R, out, b > a ? (u32)(b - a) : 0); return cb_objv(out); }
NF(AP_splice) { cbv o = thisobj(R, t); TRY(o); double n = lenof(R, o); if (n < 0) return CB_EMPTY; double st = relidx(cb_to_int(R, ARG(0)), n);
    double dc = argc == 0 ? 0 : argc == 1 ? n - st : cb_to_int(R, ARG(1)); if (dc < 0) dc = 0; if (dc > n - st) dc = n - st; int ins = argc > 2 ? argc - 2 : 0;
    cb_obj *rem = cb_new_array(R, 0); for (double i = 0; i < dc; i++) { if (hasi(R, o, st + i)) { cbv x = geti(R, o, st + i); TRY(x); cb_arr_set(R, rem, (u32)i, x); } } cb_arr_setlen(R, rem, (u32)dc);
    cb_obj *a = cb_o(o);
    if (a->cls == OC_ARRAY && a->n1 <= a->n2 && !a->sealed) { u32 S = (u32)st, D = (u32)dc, old = a->n1; u32 nl = old - D + (u32)ins; if (ins > (int)D) cb_arr_setlen(R, a, nl); memmove(a->e + S + ins, a->e + S + D, (u64)(old - S - D) * 8); for (int i = 0; i < ins; i++) a->e[S + i] = argv[2 + i]; if (ins < (int)D) cb_arr_setlen(R, a, nl); a->n1 = nl; return cb_objv(rem); }
    if (ins < dc) { for (double k = st; k < n - dc; k++) { if (hasi(R, o, k + dc)) { cbv x = geti(R, o, k + dc); TRY(x); if (seti(R, o, k + ins, x)) return CB_EMPTY; } else cb_del(R, o, cb_key(R, cb_num(k + ins)), 1); } for (double k = n; k > n - dc + ins; k--) cb_del(R, o, cb_key(R, cb_num(k - 1)), 1); }
    else if (ins > dc) { for (double k = n - dc; k > st; k--) { if (hasi(R, o, k + dc - 1)) { cbv x = geti(R, o, k + dc - 1); TRY(x); if (seti(R, o, k + ins - 1, x)) return CB_EMPTY; } else cb_del(R, o, cb_key(R, cb_num(k + ins - 1)), 1); } }
    for (int i = 0; i < ins; i++) if (seti(R, o, st + i, argv[2 + i])) return CB_EMPTY;
    if (setlen(R, o, n - dc + ins)) return CB_EMPTY; return cb_objv(rem); }
static int spreadable(cb_rt *R, cbv v) { if (!cb_isobj(v)) return 0; cbv s = cb_get(R, v, S_isConcatSpreadable); if (!cb_isundef(s)) return cb_truthy(s); return cb_o(v)->cls == OC_ARRAY; }
NF(AP_concat) { cbv o = thisobj(R, t); TRY(o); cb_obj *out = cb_new_array(R, 0); u32 k = 0;
    for (int i = -1; i < argc; i++) { cbv x = i < 0 ? o : argv[i];
        if (spreadable(R, x)) { double n = lenof(R, x); if (n < 0) return CB_EMPTY; for (double j = 0; j < n; j++, k++) { if (hasi(R, x, j)) { cbv v = geti(R, x, j); TRY(v); cb_arr_set(R, out, k, v); } } cb_arr_setlen(R, out, k); }
        else cb_arr_set(R, out, k++, x); }
    return cb_objv(out); }
NF(AP_join) { cbv o = thisobj(R, t); TRY(o); double n = lenof(R, o); if (n < 0) return CB_EMPTY; cbv sep = cb_isundef(ARG(0)) ? str(R, ",") : cb_to_str(R, ARG(0)); TRY(sep);
    static int depth; static cbv stack[64]; for (int i = 0; i < depth; i++) if (stack[i] == o) return str(R, ""); if (depth < 64) stack[depth] = o; depth++;
    cb_str *acc = cb_s(str(R, ""));
    for (double i = 0; i < n; i++) { if (i > 0) acc = cb_concat(acc, cb_s(sep)); cbv x = geti(R, o, i); if (x == CB_EMPTY) { depth--; return x; } if (!cb_isnullish(x)) { cbv s = cb_to_str(R, x); if (s == CB_EMPTY) { depth--; return s; } acc = cb_concat(acc, cb_s(s)); } }
    depth--; return cb_strv(acc); }
NF(AP_toString) { cbv j = cb_getp(R, t, "join"); TRY(j); if (cb_isfunc(j)) return cb_call_v(R, j, t, 0, 0); return OP_toString(R, t, 0, 0, fn); }
NF(AP_reverse) { cbv o = thisobj(R, t); TRY(o); double n = lenof(R, o); if (n < 0) return CB_EMPTY;
    for (double lo = 0, hi = n - 1; lo < hi; lo++, hi--) { int hl = hasi(R, o, lo), hh = hasi(R, o, hi); cbv a = hl ? geti(R, o, lo) : CB_UNDEF, b = hh ? geti(R, o, hi) : CB_UNDEF; TRY(a); TRY(b);
        if (hh) { if (seti(R, o, lo, b)) return CB_EMPTY; } else cb_del(R, o, cb_key(R, cb_num(lo)), 1); if (hl) { if (seti(R, o, hi, a)) return CB_EMPTY; } else cb_del(R, o, cb_key(R, cb_num(hi)), 1); }
    return o; }
static int idx_search(cb_rt *R, cbv o, cbv x, double from, int dir, int same0, double n) {
    for (double i = from; dir > 0 ? i < n : i >= 0; i += dir) { if (!same0 && !hasi(R, o, i)) continue; cbv v = geti(R, o, i); if (v == CB_EMPTY) return -2; if (same0 ? cb_same_value(v, x, 1) : cb_strict_eq(v, x)) return (int)i; } return -1; }
NF(AP_indexOf) { cbv o = thisobj(R, t); TRY(o); double n = lenof(R, o); if (n < 0) return CB_EMPTY; double f = argc > 1 ? cb_to_int(R, argv[1]) : 0; if (f < 0) { f += n; if (f < 0) f = 0; } int r = idx_search(R, o, ARG(0), f, 1, 0, n); if (r == -2) return CB_EMPTY; return cb_num(r); }
NF(AP_lastIndexOf) { cbv o = thisobj(R, t); TRY(o); double n = lenof(R, o); if (n < 0) return CB_EMPTY; double f = argc > 1 ? cb_to_int(R, argv[1]) : n - 1; if (f < 0) f += n; if (f > n - 1) f = n - 1; int r = idx_search(R, o, ARG(0), f, -1, 0, n); if (r == -2) return CB_EMPTY; return cb_num(r); }
NF(AP_includes) { cbv o = thisobj(R, t); TRY(o); double n = lenof(R, o); if (n < 0) return CB_EMPTY; double f = argc > 1 ? cb_to_int(R, argv[1]) : 0; if (f < 0) { f += n; if (f < 0) f = 0; } int r = idx_search(R, o, ARG(0), f, 1, 1, n); if (r == -2) return CB_EMPTY; return cb_bool(r >= 0); }
/* recorridos con funcion: kind 0 forEach, 1 map, 2 filter, 3 some, 4 every, 5 find, 6 findIndex, 7 findLast, 8 findLastIndex, 9 flatMap */
static cbv iterate(cb_rt *R, cbv t, int argc, cbv *argv, int kind) {
    cbv o = thisobj(R, t); TRY(o); double n = lenof(R, o); if (n < 0) return CB_EMPTY; cbv f = ARG(0), th = ARG(1);
    if (!cb_isfunc(f)) return cb_throw(R, 1, "callback is not a function");
    cb_obj *out = kind == 1 ? cb_new_array(R, (u32)n) : kind == 2 || kind == 9 ? cb_new_array(R, 0) : 0; if (kind == 1) for (u32 i = 0; i < (u32)n; i++) out->e[i] = CB_EMPTY;
    int rev = kind == 7 || kind == 8;
    for (double j = 0; j < n; j++) { double i = rev ? n - 1 - j : j;
        int has = kind >= 5 && kind <= 8 ? 1 : hasi(R, o, i); if (!has) continue;
        cbv v = geti(R, o, i); TRY(v); cbv a[3] = { v, cb_num(i), o }; cbv r = cb_call_v(R, f, th, 3, a); TRY(r);
        switch (kind) {
        case 1: out->e[(u32)i] = r; break; case 2: if (cb_truthy(r)) cb_arr_push(R, out, v); break;
        case 3: if (cb_truthy(r)) return CB_TRUE; break; case 4: if (!cb_truthy(r)) return CB_FALSE; break;
        case 5: case 7: if (cb_truthy(r)) return v; break; case 6: case 8: if (cb_truthy(r)) return cb_num(i); break;
        case 9: if (cb_isobj(r) && cb_o(r)->cls == OC_ARRAY) { cb_obj *ra = cb_o(r); for (u32 k = 0; k < ra->n1; k++) if (k < ra->n2 && ra->e[k] != CB_EMPTY) cb_arr_push(R, out, ra->e[k]); } else cb_arr_push(R, out, r); break; } }
    switch (kind) { case 1: case 2: case 9: return cb_objv(out); case 3: return CB_FALSE; case 4: return CB_TRUE; case 6: case 8: return cb_num(-1); default: return CB_UNDEF; } }
NF(AP_forEach) { return iterate(R, t, argc, argv, 0); } NF(AP_map) { return iterate(R, t, argc, argv, 1); } NF(AP_filter) { return iterate(R, t, argc, argv, 2); }
NF(AP_some) { return iterate(R, t, argc, argv, 3); } NF(AP_every) { return iterate(R, t, argc, argv, 4); } NF(AP_find) { return iterate(R, t, argc, argv, 5); }
NF(AP_findIndex) { return iterate(R, t, argc, argv, 6); } NF(AP_findLast) { return iterate(R, t, argc, argv, 7); } NF(AP_findLastIndex) { return iterate(R, t, argc, argv, 8); } NF(AP_flatMap) { return iterate(R, t, argc, argv, 9); }
static cbv reduce(cb_rt *R, cbv t, int argc, cbv *argv, int right) {
    cbv o = thisobj(R, t); TRY(o); double n = lenof(R, o); if (n < 0) return CB_EMPTY; cbv f = ARG(0); if (!cb_isfunc(f)) return cb_throw(R, 1, "reducer is not a function");
    double i = right ? n - 1 : 0; cbv acc;
    if (argc > 1) acc = argv[1]; else { while (right ? i >= 0 : i < n) { if (hasi(R, o, i)) break; i += right ? -1 : 1; } if (right ? i < 0 : i >= n) return cb_throw(R, 1, "Reduce of empty array with no initial value"); acc = geti(R, o, i); TRY(acc); i += right ? -1 : 1; }
    for (; right ? i >= 0 : i < n; i += right ? -1 : 1) { if (!hasi(R, o, i)) continue; cbv v = geti(R, o, i); TRY(v); cbv a[4] = { acc, v, cb_num(i), o }; acc = cb_call_v(R, f, CB_UNDEF, 4, a); TRY(acc); }
    return acc; }
NF(AP_reduce) { return reduce(R, t, argc, argv, 0); } NF(AP_reduceRight) { return reduce(R, t, argc, argv, 1); }
NF(AP_fill) { cbv o = thisobj(R, t); TRY(o); double n = lenof(R, o); if (n < 0) return CB_EMPTY; double a = relidx(cb_to_int(R, ARG(1)), n), b = cb_isundef(ARG(2)) ? n : relidx(cb_to_int(R, ARG(2)), n); for (double i = a; i < b; i++) if (seti(R, o, i, ARG(0))) return CB_EMPTY; return o; }
static void flat_into(cb_rt *R, cb_obj *out, cbv src, double depth) { double n = lenof(R, src); for (double i = 0; i < n; i++) { if (!hasi(R, src, i)) continue; cbv v = geti(R, src, i); if (v == CB_EMPTY) return; if (depth > 0 && cb_isobj(v) && cb_o(v)->cls == OC_ARRAY) flat_into(R, out, v, depth - 1); else cb_arr_push(R, out, v); } }
NF(AP_flat) { cbv o = thisobj(R, t); TRY(o); double d = cb_isundef(ARG(0)) ? 1 : cb_to_int(R, ARG(0)); cb_obj *out = cb_new_array(R, 0); flat_into(R, out, o, d); if (R->has_exc) return CB_EMPTY; return cb_objv(out); }
NF(AP_at) { cbv o = thisobj(R, t); TRY(o); double n = lenof(R, o); if (n < 0) return CB_EMPTY; double i = cb_to_int(R, ARG(0)); if (i < 0) i += n; if (i < 0 || i >= n) return CB_UNDEF; return geti(R, o, i); }
NF(AP_copyWithin) { cbv o = thisobj(R, t); TRY(o); double n = lenof(R, o); if (n < 0) return CB_EMPTY; double to = relidx(cb_to_int(R, ARG(0)), n), fr = relidx(cb_to_int(R, ARG(1)), n), en = cb_isundef(ARG(2)) ? n : relidx(cb_to_int(R, ARG(2)), n);
    double cnt = en - fr; if (cnt > n - to) cnt = n - to; if (cnt <= 0) return o; int dir = 1; if (fr < to && to < fr + cnt) { dir = -1; fr += cnt - 1; to += cnt - 1; }
    while (cnt-- > 0) { if (hasi(R, o, fr)) { cbv v = geti(R, o, fr); TRY(v); if (seti(R, o, to, v)) return CB_EMPTY; } else cb_del(R, o, cb_key(R, cb_num(to)), 1); fr += dir; to += dir; } return o; }
/* orden estable (mezcla) */
static cbv sort_fn; static int sort_err;
static int sort_cmp(cb_rt *R, cbv a, cbv b) {
    if (sort_err) return 0;
    if (cb_isundef(a)) return cb_isundef(b) ? 0 : 1; if (cb_isundef(b)) return -1;
    if (!cb_isundef(sort_fn)) { cbv ar[2] = { a, b }; cbv r = cb_call_v(R, sort_fn, CB_UNDEF, 2, ar); if (r == CB_EMPTY) { sort_err = 1; return 0; } double d = cb_to_num(R, r); return d < 0 ? -1 : d > 0 ? 1 : 0; }
    if (cb_isnum(a) && cb_isnum(b) && 0) {}
    cbv sa = cb_to_str(R, a), sb = cb_to_str(R, b); if (sa == CB_EMPTY || sb == CB_EMPTY) { sort_err = 1; return 0; } return cb_str_cmp(cb_s(sa), cb_s(sb)); }
static void msort(cb_rt *R, cbv *a, cbv *tmp, u32 n) {
    if (n < 2) return; if (n < 12) { for (u32 i = 1; i < n; i++) { cbv x = a[i]; u32 j = i; while (j > 0 && sort_cmp(R, a[j - 1], x) > 0) { a[j] = a[j - 1]; j--; } a[j] = x; } return; }
    u32 m = n / 2; msort(R, a, tmp, m); msort(R, a + m, tmp, n - m);
    u32 i = 0, j = m, k = 0; while (i < m && j < n) tmp[k++] = sort_cmp(R, a[j], a[i]) < 0 ? a[j++] : a[i++]; while (i < m) tmp[k++] = a[i++]; while (j < n) tmp[k++] = a[j++]; memcpy(a, tmp, (u64)n * 8); }
static cbv do_sort(cb_rt *R, cbv o, cbv cmp) {
    if (!cb_isundef(cmp) && !cb_isfunc(cmp)) return cb_throw(R, 1, "The comparison function must be either a function or undefined");
    double n = lenof(R, o); if (n < 0) return CB_EMPTY;
    cbv *v = cb_blob((u64)n * 8 + 8), *tmp = cb_blob((u64)n * 8 + 8); u32 k = 0, holes = 0;
    for (double i = 0; i < n; i++) { if (!hasi(R, o, i)) { holes++; continue; } cbv x = geti(R, o, i); TRY(x); v[k++] = x; }
    cbv old = sort_fn; int olde = sort_err; sort_fn = cmp; sort_err = 0; msort(R, v, tmp, k); int e = sort_err; sort_fn = old; sort_err = olde; if (e) return CB_EMPTY;
    for (u32 i = 0; i < k; i++) if (seti(R, o, i, v[i])) return CB_EMPTY;
    for (u32 i = k; i < (u32)n; i++) cb_del(R, o, cb_key(R, cb_num(i)), 1);
    return o; }
NF(AP_sort) { cbv o = thisobj(R, t); TRY(o); return do_sort(R, o, ARG(0)); }
static cbv copy_arr(cb_rt *R, cbv o) { double n = lenof(R, o); if (n < 0) return CB_EMPTY; cb_obj *out = cb_new_array(R, (u32)n); for (double i = 0; i < n; i++) { cbv x = geti(R, o, i); TRY(x); out->e[(u32)i] = x; } return cb_objv(out); }
NF(AP_toSorted) { cbv o = thisobj(R, t); TRY(o); cbv c = copy_arr(R, o); TRY(c); return do_sort(R, c, ARG(0)); }
NF(AP_toReversed) { cbv o = thisobj(R, t); TRY(o); cbv c = copy_arr(R, o); TRY(c); return AP_reverse(R, c, 0, 0, fn); }
NF(AP_toSpliced) { cbv o = thisobj(R, t); TRY(o); cbv c = copy_arr(R, o); TRY(c); cbv r = AP_splice(R, c, argc, argv, fn); TRY(r); return c; }
NF(AP_with) { cbv o = thisobj(R, t); TRY(o); cbv c = copy_arr(R, o); TRY(c); double n = cb_o(c)->n1, i = cb_to_int(R, ARG(0)); if (i < 0) i += n; if (i < 0 || i >= n) return cb_throw(R, 2, "Invalid index"); cb_o(c)->e[(u32)i] = ARG(1); return c; }
cbv cb_array_iter(cb_rt *R, cbv o, int kind);
NF(AP_keys) { cbv o = thisobj(R, t); TRY(o); return cb_array_iter(R, o, 0); }
NF(AP_values) { cbv o = thisobj(R, t); TRY(o); return cb_array_iter(R, o, 1); }
NF(AP_entries) { cbv o = thisobj(R, t); TRY(o); return cb_array_iter(R, o, 2); }

/* ---------------- String ---------------- */
static cb_str *thisstr(cb_rt *R, cbv t) { if (cb_isstr(t)) return cb_s(t); if (cb_isnullish(t)) { cb_throw(R, 1, "String.prototype method called on null or undefined"); return 0; } cbv s = cb_to_str(R, t); return s == CB_EMPTY ? 0 : cb_s(s); }
#define THISS cb_str *s = thisstr(R, t); if (!s) return CB_EMPTY; s = cb_flat(s)
NF(String_ctor) {
    cbv v = argc ? argv[0] : str(R, "");
    if (cb_isundef(R->nt) && cb_issym(v)) { cb_sym *sy = CB_PTR(v); return cb_strv(cb_concat(cb_concat(cb_s(str(R, "Symbol(")), sy->desc ? sy->desc : cb_s(str(R, ""))), cb_s(str(R, ")")))); }
    cbv s = cb_to_str(R, v); TRY(s);
    if (!cb_isundef(R->nt)) { cb_obj *o = cb_to_obj(R, s); cbv p = cb_get(R, R->nt, A_prototype); if (cb_isobj(p)) o->proto = cb_o(p); return cb_objv(o); }
    return s; }
NF(String_fromCharCode) { cb_str *s = cb_str_new((u32)argc); int asc = 1; for (int i = 0; i < argc; i++) { double d = cb_to_num(R, argv[i]); if (R->has_exc) return CB_EMPTY; s->c[i] = (u16)cb_to_u32(d); if (s->c[i] >= 128) asc = 0; } s->ascii = (u8)asc; return cb_strv(s); }
NF(String_fromCodePoint) { cb_str *acc = cb_s(str(R, "")); for (int i = 0; i < argc; i++) { double d = cb_to_num(R, argv[i]); if (R->has_exc) return CB_EMPTY; if (d < 0 || d > 0x10FFFF || d != (double)(u32)d) { char nb[40]; extern int cb_fmt_num(double, char *); cb_fmt_num(d, nb); return cb_throw(R, 2, "Invalid code point %s", nb); } acc = cb_concat(acc, cb_s(cb_str_from_cp(R, (u32)d))); } return cb_strv(acc); }
NF(String_raw) { cbv strs = ARG(0); cbv raw = cb_get(R, strs, A_raw); TRY(raw); double n = lenof(R, raw); if (n < 0) return CB_EMPTY; cb_str *acc = cb_s(str(R, ""));
    for (double i = 0; i < n; i++) { cbv s = geti(R, raw, i); TRY(s); s = cb_to_str(R, s); TRY(s); acc = cb_concat(acc, cb_s(s)); if (i + 1 < n && i + 1 < argc) { cbv x = cb_to_str(R, argv[(int)i + 1]); TRY(x); acc = cb_concat(acc, cb_s(x)); } } return cb_strv(acc); }
NF(SP_toString) { if (cb_isstr(t)) return t; if (cb_isobj(t) && cb_o(t)->cls == OC_STRING) return cb_o(t)->v1; return cb_throw(R, 1, "String.prototype.toString requires that 'this' be a String"); }
NF(SP_charAt) { THISS; double i = cb_to_int(R, ARG(0)); if (i < 0 || i >= s->len) return str(R, ""); return cb_strv(cb_substr(s, (u32)i, (u32)i + 1)); }
NF(SP_charCodeAt) { THISS; double i = cb_to_int(R, ARG(0)); if (i < 0 || i >= s->len) return cb_num(__builtin_nan("")); return cb_num(s->c[(u32)i]); }
NF(SP_codePointAt) { THISS; double i = cb_to_int(R, ARG(0)); if (i < 0 || i >= s->len) return CB_UNDEF; u32 c = s->c[(u32)i]; if (c >= 0xD800 && c < 0xDC00 && i + 1 < s->len) { u32 d = s->c[(u32)i + 1]; if (d >= 0xDC00 && d < 0xE000) c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00); } return cb_num(c); }
NF(SP_at) { THISS; double i = cb_to_int(R, ARG(0)); if (i < 0) i += s->len; if (i < 0 || i >= s->len) return CB_UNDEF; return cb_strv(cb_substr(s, (u32)i, (u32)i + 1)); }
static long sfind(cb_str *s, cb_str *p, long from) { s = cb_flat(s); p = cb_flat(p); if (from < 0) from = 0; if (p->len == 0) return from <= (long)s->len ? from : (long)s->len; for (long i = from; i + (long)p->len <= (long)s->len; i++) if (s->c[i] == p->c[0] && !memcmp(s->c + i, p->c, p->len * 2)) return i; return -1; }
static long srfind(cb_str *s, cb_str *p, long from) { s = cb_flat(s); p = cb_flat(p); if (from > (long)s->len - (long)p->len) from = (long)s->len - (long)p->len; for (long i = from; i >= 0; i--) if (!memcmp(s->c + i, p->c, p->len * 2)) return i; return -1; }
long cb_str_find(cb_str *s, cb_str *p, long from) { return sfind(s, p, from); }
NF(SP_indexOf) { THISS; cbv p = cb_to_str(R, ARG(0)); TRY(p); double f = cb_to_int(R, ARG(1)); return cb_num((double)sfind(s, cb_s(p), f > s->len ? s->len : (long)f)); }
NF(SP_lastIndexOf) { THISS; cbv p = cb_to_str(R, ARG(0)); TRY(p); double f = cb_to_num(R, ARG(1)); long fi = f != f ? (long)s->len : f < 0 ? 0 : (long)f; return cb_num((double)srfind(s, cb_s(p), fi)); }
static int is_regexp(cb_rt *R, cbv v) { return cb_isobj(v) && cb_o(v)->cls == OC_REGEXP; }
NF(SP_includes) { THISS; if (is_regexp(R, ARG(0))) return cb_throw(R, 1, "First argument to String.prototype.includes must not be a regular expression"); cbv p = cb_to_str(R, ARG(0)); TRY(p); double f = cb_to_int(R, ARG(1)); return cb_bool(sfind(s, cb_s(p), (long)f) >= 0); }
NF(SP_startsWith) { THISS; if (is_regexp(R, ARG(0))) return cb_throw(R, 1, "First argument to String.prototype.startsWith must not be a regular expression"); cbv pv = cb_to_str(R, ARG(0)); TRY(pv); cb_str *p = cb_flat(cb_s(pv)); double f = cb_to_int(R, ARG(1)); if (f < 0) f = 0; if (f + p->len > s->len) return CB_FALSE; return cb_bool(!memcmp(s->c + (u32)f, p->c, p->len * 2)); }
NF(SP_endsWith) { THISS; if (is_regexp(R, ARG(0))) return cb_throw(R, 1, "First argument to String.prototype.endsWith must not be a regular expression"); cbv pv = cb_to_str(R, ARG(0)); TRY(pv); cb_str *p = cb_flat(cb_s(pv)); double e = cb_isundef(ARG(1)) ? s->len : cb_to_int(R, ARG(1)); if (e > s->len) e = s->len; if (e < p->len) return CB_FALSE; return cb_bool(!memcmp(s->c + (u32)e - p->len, p->c, p->len * 2)); }
NF(SP_slice) { THISS; double n = s->len, a = relidx(cb_to_int(R, ARG(0)), n), b = cb_isundef(ARG(1)) ? n : relidx(cb_to_int(R, ARG(1)), n); return cb_strv(cb_substr(s, (u32)a, b > a ? (u32)b : (u32)a)); }
NF(SP_substring) { THISS; double n = s->len, a = cb_to_int(R, ARG(0)), b = cb_isundef(ARG(1)) ? n : cb_to_int(R, ARG(1)); a = a < 0 ? 0 : a > n ? n : a; b = b < 0 ? 0 : b > n ? n : b; if (a > b) { double x = a; a = b; b = x; } return cb_strv(cb_substr(s, (u32)a, (u32)b)); }
NF(SP_substr) { THISS; double n = s->len, a = relidx(cb_to_int(R, ARG(0)), n), l = cb_isundef(ARG(1)) ? n - a : cb_to_int(R, ARG(1)); if (l < 0) l = 0; if (a + l > n) l = n - a; return cb_strv(cb_substr(s, (u32)a, (u32)(a + l))); }
NF(SP_concat) { THISS; cb_str *acc = s; for (int i = 0; i < argc; i++) { cbv x = cb_to_str(R, argv[i]); TRY(x); acc = cb_concat(acc, cb_s(x)); } return cb_strv(acc); }
NF(SP_repeat) { THISS; double n = cb_to_int(R, ARG(0)); if (n < 0 || __builtin_isinf(n)) { char nb[40]; extern int cb_fmt_num(double, char *); cb_fmt_num(n, nb); return cb_throw(R, 2, "Invalid count value: %s", nb); } if (n * s->len > 1e9) return cb_throw(R, 2, "Invalid string length"); cb_str *r = cb_str_new((u32)(n * s->len)); for (u32 i = 0; i < (u32)n; i++) memcpy(r->c + i * s->len, s->c, s->len * 2); r->ascii = s->ascii; return cb_strv(r); }
static cbv pad(cb_rt *R, cbv t, int argc, cbv *argv, int start) {
    THISS; double n = cb_to_int(R, ARG(0)); if (n <= s->len) return cb_strv(s); cb_str *f = cb_isundef(ARG(1)) ? cb_s(str(R, " ")) : cb_s(cb_to_str(R, ARG(1))); f = cb_flat(f); if (!f->len) return cb_strv(s);
    u32 fill = (u32)n - s->len; cb_str *p = cb_str_new(fill); for (u32 i = 0; i < fill; i++) p->c[i] = f->c[i % f->len]; p->ascii = f->ascii; return cb_strv(start ? cb_concat(p, s) : cb_concat(s, p)); }
NF(SP_padStart) { return pad(R, t, argc, argv, 1); } NF(SP_padEnd) { return pad(R, t, argc, argv, 0); }
static int ws(u16 c) { return c == 9 || c == 10 || c == 11 || c == 12 || c == 13 || c == 32 || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000 || c == 0xFEFF; }
static cbv trim(cb_rt *R, cbv t, int which) { THISS; u32 a = 0, b = s->len; if (which & 1) while (a < b && ws(s->c[a])) a++; if (which & 2) while (b > a && ws(s->c[b - 1])) b--; return cb_strv(cb_substr(s, a, b)); }
NF(SP_trim) { return trim(R, t, 3); } NF(SP_trimStart) { return trim(R, t, 1); } NF(SP_trimEnd) { return trim(R, t, 2); }
u32 cb_case(u32 c, int upper);
static cbv recase(cb_rt *R, cbv t, int upper) {
    THISS; if (s->ascii) { cb_str *r = cb_str_new(s->len); for (u32 i = 0; i < s->len; i++) { u16 c = s->c[i]; r->c[i] = upper ? (c >= 'a' && c <= 'z' ? c - 32 : c) : (c >= 'A' && c <= 'Z' ? c + 32 : c); } r->ascii = 1; return cb_strv(r); }
    cb_str *r = cb_str_new(s->len * 2); u32 k = 0;
    for (u32 i = 0; i < s->len; i++) { u32 c = s->c[i]; if (upper && c == 0xDF) { r->c[k++] = 'S'; r->c[k++] = 'S'; continue; } if (!upper && c == 0x130) { r->c[k++] = 'i'; r->c[k++] = 0x307; continue; }
        if (!upper && c == 0x3A3) { int fin = (i + 1 >= s->len || !((s->c[i + 1] | 32) >= 'a' && (s->c[i + 1] | 32) <= 'z') ) && i > 0; u32 nx = i + 1 < s->len ? s->c[i + 1] : 0; fin = i > 0 && !(nx >= 0x391 && nx <= 0x3C9); r->c[k++] = (u16)(fin ? 0x3C2 : 0x3C3); continue; }
        r->c[k++] = (u16)cb_case(c, upper); }
    r->len = k; return cb_strv(r); }
NF(SP_toLowerCase) { return recase(R, t, 0); } NF(SP_toUpperCase) { return recase(R, t, 1); }
NF(SP_localeCompare) { THISS; cbv o = cb_to_str(R, ARG(0)); TRY(o); cb_str *a = s, *b = cb_flat(cb_s(o));
    /* orden aproximado por letra base (ignora acentos primero, luego mayusculas) */
    u32 n = a->len < b->len ? a->len : b->len; extern u32 cb_base_letter(u32 c);
    for (u32 i = 0; i < n; i++) { u32 x = cb_base_letter(a->c[i]), y = cb_base_letter(b->c[i]); if (x != y) return cb_num(x < y ? -1 : 1); }
    if (a->len != b->len) return cb_num(a->len < b->len ? -1 : 1);
    for (u32 i = 0; i < n; i++) if (a->c[i] != b->c[i]) return cb_num(a->c[i] < b->c[i] ? -1 : 1); return cb_num(0); }
u32 cb_decompose(u32 c, u32 *out);
NF(SP_normalize) { THISS; cbv f = ARG(0); int nfd = 0; if (!cb_isundef(f)) { cbv fs = cb_to_str(R, f); TRY(fs); if (cb_str_eqa(cb_s(fs), "NFD") || cb_str_eqa(cb_s(fs), "NFKD")) nfd = 1; else if (!cb_str_eqa(cb_s(fs), "NFC") && !cb_str_eqa(cb_s(fs), "NFKC")) return cb_throw(R, 2, "The normalization form should be one of NFC, NFD, NFKC, NFKD."); }
    extern u32 cb_compose(u32 a, u32 b);
    cb_str *r = cb_str_new(s->len * 2); u32 k = 0;
    for (u32 i = 0; i < s->len; i++) { u32 c = s->c[i]; if (nfd) { u32 d[2]; if (cb_decompose(c, d)) { r->c[k++] = (u16)d[0]; r->c[k++] = (u16)d[1]; } else r->c[k++] = (u16)c; } else { if (k && i < s->len) { u32 m = cb_compose(r->c[k - 1], c); if (m) { r->c[k - 1] = (u16)m; continue; } } r->c[k++] = (u16)c; } }
    r->len = k; return cb_strv(r); }
cbv cb_str_iter(cb_rt *R, cbv s);
NF(SP_iterator) { THISS; return cb_str_iter(R, cb_strv(s)); }
NF(SP_isWellFormed) { THISS; for (u32 i = 0; i < s->len; i++) { u16 c = s->c[i]; if (c >= 0xD800 && c < 0xDC00) { if (i + 1 < s->len && s->c[i + 1] >= 0xDC00 && s->c[i + 1] < 0xE000) { i++; continue; } return CB_FALSE; } if (c >= 0xDC00 && c < 0xE000) return CB_FALSE; } return CB_TRUE; }
/* metodos HTML antiguos (aun usados): anchor, link... solo los mas comunes */
NF(SP_valueOf) { return SP_toString(R, t, argc, argv, fn); }

/* ---------------- Number ---------------- */
static double thisnum(cb_rt *R, cbv t, int *ok) { *ok = 1; if (cb_isnum(t)) return cb_d(t); if (cb_isobj(t) && cb_o(t)->cls == OC_NUMBER) return cb_d(cb_o(t)->v1); *ok = 0; cb_throw(R, 1, "Number.prototype method requires that 'this' be a Number"); return 0; }
NF(Number_ctor) { if (argc) { cbv p = cb_to_prim(R, argv[0], 1); if (p == CB_EMPTY) return p; if (cb_isbig(p)) { double d = cb_big_to_double(p); return cb_isundef(R->nt) ? cb_num(d) : cb_objv(cb_to_obj(R, cb_num(d))); } } double d = argc ? cb_to_num(R, argv[0]) : 0; if (R->has_exc) return CB_EMPTY; if (!cb_isundef(R->nt)) { cb_obj *o = cb_to_obj(R, cb_num(d)); cbv p = cb_get(R, R->nt, A_prototype); if (cb_isobj(p)) o->proto = cb_o(p); return cb_objv(o); } return cb_num(d); }
NF(Number_isFinite) { return cb_bool(cb_isnum(ARG(0)) && __builtin_isfinite(cb_d(ARG(0)))); }
NF(Number_isNaN) { return cb_bool(cb_isnum(ARG(0)) && cb_d(ARG(0)) != cb_d(ARG(0))); }
NF(Number_isInteger) { if (!cb_isnum(ARG(0))) return CB_FALSE; double d = cb_d(ARG(0)); return cb_bool(__builtin_isfinite(d) && __builtin_trunc(d) == d); }
NF(Number_isSafeInteger) { if (!cb_isnum(ARG(0))) return CB_FALSE; double d = cb_d(ARG(0)); return cb_bool(__builtin_isfinite(d) && __builtin_trunc(d) == d && __builtin_fabs(d) <= 9007199254740991.0); }
NF(g_isNaN) { double d = cb_to_num(R, ARG(0)); if (R->has_exc) return CB_EMPTY; return cb_bool(d != d); }
NF(g_isFinite) { double d = cb_to_num(R, ARG(0)); if (R->has_exc) return CB_EMPTY; return cb_bool(__builtin_isfinite(d)); }
NF(g_parseFloat) {
    cbv sv = cb_to_str(R, ARG(0)); TRY(sv); cb_str *s = cb_flat(cb_s(sv)); u32 i = 0; while (i < s->len && ws(s->c[i])) i++;
    char b[400]; int k = 0; if (i < s->len && (s->c[i] == '+' || s->c[i] == '-')) b[k++] = (char)s->c[i++];
    if (s->len - i >= 8) { int inf = 1; const char *w = "Infinity"; for (int j = 0; j < 8; j++) if (s->c[i + j] != (u8)w[j]) inf = 0; if (inf) return cb_num(b[0] == '-' ? -__builtin_inf() : __builtin_inf()); }
    int dig = 0, dot = 0; while (i < s->len && k < 390) { u16 c = s->c[i]; if (c >= '0' && c <= '9') { b[k++] = (char)c; dig = 1; } else if (c == '.' && !dot) { b[k++] = '.'; dot = 1; } else break; i++; }
    if (!dig) return cb_num(__builtin_nan(""));
    if (i < s->len && (s->c[i] | 32) == 'e') { int save = k; b[k++] = 'e'; i++; if (i < s->len && (s->c[i] == '+' || s->c[i] == '-')) b[k++] = (char)s->c[i++]; int ed = 0; while (i < s->len && s->c[i] >= '0' && s->c[i] <= '9' && k < 398) { b[k++] = (char)s->c[i++]; ed = 1; } if (!ed) k = save; }
    b[k] = 0; return cb_num(cb_parse_number(b, k)); }
NF(g_parseInt) {
    cbv sv = cb_to_str(R, ARG(0)); TRY(sv); cb_str *s = cb_flat(cb_s(sv)); u32 i = 0; while (i < s->len && ws(s->c[i])) i++;
    int neg = 0; if (i < s->len && (s->c[i] == '+' || s->c[i] == '-')) neg = s->c[i++] == '-';
    int radix = (int)cb_to_i32(cb_to_num(R, ARG(1))); if (R->has_exc) return CB_EMPTY;
    if (radix == 0) radix = 10, ({ if (i + 1 < s->len && s->c[i] == '0' && (s->c[i + 1] | 32) == 'x') { radix = 16; i += 2; } 0; });
    else if (radix == 16 && i + 1 < s->len && s->c[i] == '0' && (s->c[i + 1] | 32) == 'x') i += 2;
    if (radix < 2 || radix > 36) return cb_num(__builtin_nan(""));
    double v = 0; int any = 0; u32 st = i; char dec[400]; int dk = 0;
    for (; i < s->len; i++) { u16 c = s->c[i]; int d = c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'z' ? (c | 32) - 'a' + 10 : 99; if (d >= radix) break; v = v * radix + d; any = 1; if (dk < 390) dec[dk++] = (char)c; }
    if (!any) return cb_num(__builtin_nan("")); (void)st;
    if (radix == 10 && dk > 15) { dec[dk] = 0; v = cb_parse_number(dec, dk); }
    return cb_num(neg ? -v : v); }
NF(NP_toString) { int ok; double d = thisnum(R, t, &ok); if (!ok) return CB_EMPTY; int radix = cb_isundef(ARG(0)) ? 10 : (int)cb_to_int(R, ARG(0)); if (radix < 2 || radix > 36) return cb_throw(R, 2, "toString() radix must be between 2 and 36"); if (radix == 10) return cb_num_to_str(R, d); return cb_num_to_str_radix(R, d, radix); }
NF(NP_valueOf) { int ok; double d = thisnum(R, t, &ok); if (!ok) return CB_EMPTY; return cb_num(d); }
static cbv fmt_digits(cb_rt *R, int neg, const char *dig, int nd, int k, int frac) {   /* toFixed */
    char b[1300]; int n = 0; if (neg) b[n++] = '-';
    if (k <= 0) { b[n++] = '0'; if (frac) { b[n++] = '.'; for (int i = 0; i < frac; i++) { int pos = i + k; b[n++] = pos < 0 ? '0' : pos < nd ? dig[pos] : '0'; } } }
    else { for (int i = 0; i < k; i++) b[n++] = i < nd ? dig[i] : '0'; if (frac) { b[n++] = '.'; for (int i = 0; i < frac; i++) b[n++] = k + i < nd ? dig[k + i] : '0'; } }
    b[n] = 0; return cb_mkstr(R, b); }
NF(NP_toFixed) { int ok; double d = thisnum(R, t, &ok); if (!ok) return CB_EMPTY; double f = cb_to_int(R, ARG(0)); if (f < 0 || f > 100) return cb_throw(R, 2, "toFixed() digits argument must be between 0 and 100");
    if (d != d) return str(R, "NaN"); if (__builtin_fabs(d) >= 1e21 || __builtin_isinf(d)) return cb_num_to_str(R, d);
    int neg = d < 0; if (neg) d = -d; char dig[1200]; int k; int nd = cb_dtoa_fixed(d, 1, (int)f, dig, &k);
    if (nd == 0) { k = 1; dig[0] = '0'; nd = 1; } cbv r = fmt_digits(R, neg && (nd > 1 || dig[0] != '0' || k > 1), dig, nd, k, (int)f); return r; }
NF(NP_toExponential) { int ok; double d = thisnum(R, t, &ok); if (!ok) return CB_EMPTY; double f = cb_to_int(R, ARG(0)); if (d != d || __builtin_isinf(d)) return cb_num_to_str(R, d); if (f < 0 || f > 100) return cb_throw(R, 2, "toExponential() argument must be between 0 and 100");
    int neg = d < 0; if (neg) d = -d; char dig[1200]; int k, nd;
    if (cb_isundef(ARG(0))) { extern int cb_dtoa_short(double v, char *dig, int *kout); nd = cb_dtoa_short(d, dig, &k); f = nd - 1; } else { nd = cb_dtoa_fixed(d, 0, (int)f + 1, dig, &k); }
    if (d == 0) { k = 1; for (int i = 0; i <= f; i++) dig[i] = '0'; nd = (int)f + 1; }
    char b[1300]; int n = 0; if (neg) b[n++] = '-'; b[n++] = dig[0]; if (f > 0) { b[n++] = '.'; for (int i = 1; i <= f; i++) b[n++] = i < nd ? dig[i] : '0'; }
    n += cb_snprintf(b + n, 20, "e%c%d", k - 1 >= 0 ? '+' : '-', k - 1 >= 0 ? k - 1 : 1 - k); return cb_mkstr(R, b); }
NF(NP_toPrecision) { int ok; double d = thisnum(R, t, &ok); if (!ok) return CB_EMPTY; if (cb_isundef(ARG(0))) return cb_num_to_str(R, d); double p = cb_to_int(R, ARG(0)); if (d != d || __builtin_isinf(d)) return cb_num_to_str(R, d); if (p < 1 || p > 100) return cb_throw(R, 2, "toPrecision() argument must be between 1 and 100");
    int neg = d < 0; if (neg) d = -d; char dig[1200]; int k, nd; if (d == 0) { k = 1; nd = (int)p; for (int i = 0; i < nd; i++) dig[i] = '0'; } else nd = cb_dtoa_fixed(d, 0, (int)p, dig, &k);
    char b[1300]; int n = 0; if (neg) b[n++] = '-'; int e = k - 1;
    if (e < -6 || e >= p) { b[n++] = dig[0]; if (p > 1) { b[n++] = '.'; for (int i = 1; i < p; i++) b[n++] = i < nd ? dig[i] : '0'; } n += cb_snprintf(b + n, 20, "e%c%d", e >= 0 ? '+' : '-', e >= 0 ? e : -e); }
    else if (k <= 0) { b[n++] = '0'; b[n++] = '.'; for (int i = 0; i < -k; i++) b[n++] = '0'; for (int i = 0; i < p; i++) b[n++] = i < nd ? dig[i] : '0'; }
    else { for (int i = 0; i < p; i++) { if (i == k) b[n++] = '.'; b[n++] = i < nd ? dig[i] : '0'; } }
    b[n] = 0; return cb_mkstr(R, b); }
NF(NP_toLocaleString) { int ok; double d = thisnum(R, t, &ok); if (!ok) return CB_EMPTY; if (d != d || __builtin_isinf(d)) return cb_num_to_str(R, d);
    int neg = d < 0; if (neg) d = -d; char dig[1200]; int k; int nd = cb_dtoa_fixed(d, 1, 3, dig, &k); while (nd > 0 && nd > k && dig[nd - 1] == '0') nd--;
    char b[600]; int n = 0; if (neg) b[n++] = '-'; if (k <= 0) b[n++] = '0'; for (int i = 0; i < k; i++) { if (i && (k - i) % 3 == 0) b[n++] = ','; b[n++] = i < nd ? dig[i] : '0'; }
    if (nd > k) { b[n++] = '.'; for (int i = k < 0 ? k : k; i < nd; i++) b[n++] = i < 0 ? '0' : dig[i]; } b[n] = 0; return cb_mkstr(R, b); }
cbv cb_num_to_str_radix(cb_rt *R, double d, int radix) {
    if (d != d) return str(R, "NaN"); if (__builtin_isinf(d)) return str(R, d < 0 ? "-Infinity" : "Infinity");
    char b[1200]; int n = 0; int neg = d < 0; if (neg) d = -d; double ip = __builtin_floor(d), fp = d - ip;
    char t2[1100]; int k = 0; if (ip == 0) t2[k++] = '0'; while (ip > 0 && k < 1100) { double q = __builtin_floor(ip / radix); int dg = (int)(ip - q * radix); t2[k++] = (char)(dg < 10 ? '0' + dg : 'a' + dg - 10); ip = q; }
    if (neg) b[n++] = '-'; while (k) b[n++] = t2[--k];
    if (fp > 0) { b[n++] = '.'; /* cifras hasta que el valor quede determinado (precision de double) */ double eps = __builtin_nextafter(d, __builtin_inf()) - d; if (eps <= 0) eps = 1e-300; eps /= 2;
        for (int i = 0; i < 52 && fp > eps && n < 1190; i++) { fp *= radix; eps *= radix; int dg = (int)fp; fp -= dg; if (fp > 1 - eps) { dg++; fp = 0; }
            b[n++] = (char)(dg < 10 ? '0' + dg : 'a' + dg - 10); } }
    b[n] = 0; return cb_mkstr(R, b); }

/* ---------------- Boolean y Symbol ---------------- */
NF(Boolean_ctor) { int b = cb_truthy(ARG(0)); if (!cb_isundef(R->nt)) { cb_obj *o = cb_to_obj(R, cb_bool(b)); cbv p = cb_get(R, R->nt, A_prototype); if (cb_isobj(p)) o->proto = cb_o(p); return cb_objv(o); } return cb_bool(b); }
NF(BP_toString) { cbv v = cb_isbool(t) ? t : cb_isobj(t) && cb_o(t)->cls == OC_BOOL ? cb_o(t)->v1 : CB_EMPTY; if (v == CB_EMPTY) return cb_throw(R, 1, "Boolean.prototype.toString requires that 'this' be a Boolean"); return str(R, v == CB_TRUE ? "true" : "false"); }
NF(BP_valueOf) { cbv v = cb_isbool(t) ? t : cb_isobj(t) && cb_o(t)->cls == OC_BOOL ? cb_o(t)->v1 : CB_EMPTY; if (v == CB_EMPTY) return cb_throw(R, 1, "Boolean.prototype.valueOf requires that 'this' be a Boolean"); return v; }
static cb_obj *registry;
NF(Symbol_ctor) { if (!cb_isundef(R->nt)) return cb_throw(R, 1, "Symbol is not a constructor"); cbv d = CB_UNDEF; if (!cb_isundef(ARG(0))) { d = cb_to_str(R, ARG(0)); TRY(d); } return cb_sym_new(R, d); }
NF(Symbol_for) { cbv k = cb_to_str(R, ARG(0)); TRY(k); k = cb_intern(R, cb_s(k)); cb_prop *p = cb_own(registry, k); if (p) return p->val; cbv s = cb_sym_new(R, k); ((cb_sym *)CB_PTR(s))->reg = 1; cb_add_prop(R, registry, k, s, PF_WEC); return s; }
NF(Symbol_keyFor) { if (!cb_issym(ARG(0))) return cb_throw(R, 1, "not a symbol"); cb_sym *s = CB_PTR(ARG(0)); return s->reg == 1 ? cb_strv(s->desc) : CB_UNDEF; }
static cbv thissym(cb_rt *R, cbv t) { if (cb_issym(t)) return t; if (cb_isobj(t) && cb_o(t)->cls == OC_SYMBOL) return cb_o(t)->v1; return cb_throw(R, 1, "not a symbol"); }
NF(SyP_toString) { cbv s = thissym(R, t); TRY(s); cb_sym *y = CB_PTR(s); return cb_strv(cb_concat(cb_concat(cb_s(str(R, "Symbol(")), y->desc ? y->desc : cb_s(str(R, ""))), cb_s(str(R, ")")))); }
NF(SyP_valueOf) { return thissym(R, t); }
NF(SyP_description) { cbv s = thissym(R, t); TRY(s); cb_sym *y = CB_PTR(s); return y->desc ? cb_strv(y->desc) : CB_UNDEF; }

/* ---------------- Math ---------------- */
static double argn(cb_rt *R, int argc, cbv *argv, int i) { return cb_to_num(R, ARG(i)); }
#define M1(name, w) NF(name) { double x = argn(R, argc, argv, 0); if (R->has_exc) return CB_EMPTY; return cb_num(cb_math1(w, x)); }
M1(M_sqrt, 0) M1(M_sin, 1) M1(M_cos, 2) M1(M_tan, 3) M1(M_asin, 4) M1(M_acos, 5) M1(M_atan, 6) M1(M_exp, 7) M1(M_log, 8) M1(M_log2, 9) M1(M_log10, 10)
M1(M_expm1, 11) M1(M_log1p, 12) M1(M_sinh, 13) M1(M_cosh, 14) M1(M_tanh, 15) M1(M_asinh, 16) M1(M_acosh, 17) M1(M_atanh, 18) M1(M_cbrt, 19) M1(M_floor, 20) M1(M_ceil, 21) M1(M_trunc, 22) M1(M_round, 23) M1(M_fround, 24)
NF(M_abs) { double x = argn(R, argc, argv, 0); if (R->has_exc) return CB_EMPTY; return cb_num(__builtin_fabs(x)); }
NF(M_sign) { double x = argn(R, argc, argv, 0); if (R->has_exc) return CB_EMPTY; return cb_num(x > 0 ? 1 : x < 0 ? -1 : x); }
NF(M_pow) { double a = argn(R, argc, argv, 0), b = argn(R, argc, argv, 1); if (R->has_exc) return CB_EMPTY; return cb_num(cb_pow(a, b)); }
NF(M_atan2) { double a = argn(R, argc, argv, 0), b = argn(R, argc, argv, 1); if (R->has_exc) return CB_EMPTY; return cb_num(cb_atan2(a, b)); }
NF(M_max) { double r = -__builtin_inf(); int nan = 0; for (int i = 0; i < argc; i++) { double x = cb_to_num(R, argv[i]); if (R->has_exc) return CB_EMPTY; if (x != x) nan = 1; else if (x > r || (x == 0 && r == 0 && !__builtin_signbit(x))) r = x; } return cb_num(nan ? __builtin_nan("") : r); }
NF(M_min) { double r = __builtin_inf(); int nan = 0; for (int i = 0; i < argc; i++) { double x = cb_to_num(R, argv[i]); if (R->has_exc) return CB_EMPTY; if (x != x) nan = 1; else if (x < r || (x == 0 && r == 0 && __builtin_signbit(x))) r = x; } return cb_num(nan ? __builtin_nan("") : r); }
NF(M_hypot) { long double s = 0; int inf = 0, nan = 0; for (int i = 0; i < argc; i++) { double x = cb_to_num(R, argv[i]); if (R->has_exc) return CB_EMPTY; if (__builtin_isinf(x)) inf = 1; else if (x != x) nan = 1; else s += (long double)x * x; } if (inf) return cb_num(__builtin_inf()); if (nan) return cb_num(__builtin_nan("")); extern double cb_sqrtl(long double); return cb_num(cb_sqrtl(s)); }
NF(M_random) { return cb_num(cb_random()); }
NF(M_imul) { double a = argn(R, argc, argv, 0), b = argn(R, argc, argv, 1); if (R->has_exc) return CB_EMPTY; return cb_num((double)(int32_t)((u32)cb_to_i32(a) * (u32)cb_to_i32(b))); }
NF(M_clz32) { double a = argn(R, argc, argv, 0); if (R->has_exc) return CB_EMPTY; u32 x = cb_to_u32(a); return cb_num(x ? __builtin_clz(x) : 32); }

/* ---------------- Error ---------------- */
static cbv err_ctor(cb_rt *R, int kind, int argc, cbv *argv, cb_obj *fn) {
    cbv msg = ARG(0); if (!cb_isundef(msg)) { msg = cb_to_str(R, msg); TRY(msg); }
    cb_obj *e = cb_new_err(R, kind, msg);
    cbv nt = cb_isundef(R->nt) ? cb_objv(fn) : R->nt; cbv p = cb_get(R, nt, A_prototype); TRY(p); if (cb_isobj(p)) e->proto = cb_o(p);
    cbv opt = ARG(1); if (cb_isobj(opt) && cb_has(R, opt, A_cause)) { cbv c = cb_get(R, opt, A_cause); TRY(c); cb_add_prop(R, e, A_cause, c, PF_W | PF_C); }
    return cb_objv(e); }
NF(E_Error) { return err_ctor(R, 0, argc, argv, fn); } NF(E_Type) { return err_ctor(R, 1, argc, argv, fn); } NF(E_Range) { return err_ctor(R, 2, argc, argv, fn); } NF(E_Syntax) { return err_ctor(R, 3, argc, argv, fn); }
NF(E_Reference) { return err_ctor(R, 4, argc, argv, fn); } NF(E_Eval) { return err_ctor(R, 5, argc, argv, fn); } NF(E_URI) { return err_ctor(R, 6, argc, argv, fn); }
NF(E_Aggregate) { cbv e = err_ctor(R, 7, argc > 1 ? argc - 1 : 0, argv + 1, fn); TRY(e); cb_obj *a = cb_new_array(R, 0); cbv it = cb_get_iter(R, ARG(0), 0); TRY(it); cbv x; int r; while ((r = cb_iter_next(R, it, &x)) > 0) cb_arr_push(R, a, x); if (r < 0) return CB_EMPTY; cb_add_prop(R, cb_o(e), cb_atom(R, "errors"), cb_objv(a), PF_W | PF_C); return e; }
NF(EP_toString) { if (!cb_isobj(t)) return cb_throw(R, 1, "Error.prototype.toString called on non-object"); cbv n = cb_get(R, t, A_name); TRY(n); cbv m = cb_get(R, t, A_message); TRY(m);
    cbv ns = cb_isundef(n) ? str(R, "Error") : cb_to_str(R, n); TRY(ns); cbv ms = cb_isundef(m) ? str(R, "") : cb_to_str(R, m); TRY(ms);
    if (!cb_s(ns)->len) return ms; if (!cb_s(ms)->len) return ns; return cb_strv(cb_concat(cb_concat(cb_s(ns), cb_s(str(R, ": "))), cb_s(ms))); }
NF(E_captureStackTrace) { if (cb_isobj(ARG(0))) { cbv e = cb_err_obj(R, 0, ""); cbv st = cb_get(R, e, A_stack); cb_set(R, ARG(0), A_stack, st); } return CB_UNDEF; }

/* ---------------- globales: URI ---------------- */
static const char *HEXD = "0123456789ABCDEF";
static cbv uri_encode(cb_rt *R, cbv v, const char *keep) {
    cbv sv = cb_to_str(R, v); TRY(sv); cb_str *s = cb_flat(cb_s(sv)); cb_str *o = cb_str_new(s->len * 12 + 1); u32 k = 0;
    for (u32 i = 0; i < s->len; i++) { u32 c = s->c[i];
        if (c < 128 && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr(keep, (int)c))) { if (c) { o->c[k++] = (u16)c; continue; } }
        if (c >= 0xDC00 && c < 0xE000) return cb_throw(R, 6, "URI malformed");
        if (c >= 0xD800 && c < 0xDC00) { if (i + 1 >= s->len || s->c[i + 1] < 0xDC00 || s->c[i + 1] >= 0xE000) return cb_throw(R, 6, "URI malformed"); c = 0x10000 + ((c - 0xD800) << 10) + (s->c[++i] - 0xDC00); }
        u8 b[4]; int n = c < 0x80 ? (b[0] = (u8)c, 1) : c < 0x800 ? (b[0] = (u8)(0xC0 | c >> 6), b[1] = (u8)(0x80 | (c & 63)), 2) : c < 0x10000 ? (b[0] = (u8)(0xE0 | c >> 12), b[1] = (u8)(0x80 | ((c >> 6) & 63)), b[2] = (u8)(0x80 | (c & 63)), 3) : (b[0] = (u8)(0xF0 | c >> 18), b[1] = (u8)(0x80 | ((c >> 12) & 63)), b[2] = (u8)(0x80 | ((c >> 6) & 63)), b[3] = (u8)(0x80 | (c & 63)), 4);
        for (int j = 0; j < n; j++) { o->c[k++] = '%'; o->c[k++] = (u16)HEXD[b[j] >> 4]; o->c[k++] = (u16)HEXD[b[j] & 15]; } }
    o->len = k; o->ascii = 1; return cb_strv(o); }
static int hv(u16 c) { return c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1; }
static cbv uri_decode(cb_rt *R, cbv v, const char *reserved) {
    cbv sv = cb_to_str(R, v); TRY(sv); cb_str *s = cb_flat(cb_s(sv)); cb_str *o = cb_str_new(s->len); u32 k = 0;
    for (u32 i = 0; i < s->len; i++) { u16 c = s->c[i]; if (c != '%') { o->c[k++] = c; continue; }
        if (i + 2 >= s->len || hv(s->c[i + 1]) < 0 || hv(s->c[i + 2]) < 0) return cb_throw(R, 6, "URI malformed");
        u32 b = (u32)(hv(s->c[i + 1]) * 16 + hv(s->c[i + 2]));
        if (b < 0x80) { if (strchr(reserved, (int)b) && b) { o->c[k++] = c; o->c[k++] = s->c[i + 1]; o->c[k++] = s->c[i + 2]; } else o->c[k++] = (u16)b; i += 2; continue; }
        int n = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 0; if (!n) return cb_throw(R, 6, "URI malformed");
        u32 cp = n == 4 ? b & 7 : n == 3 ? b & 15 : b & 31; i += 2;
        for (int j = 1; j < n; j++) { if (i + 3 > s->len || s->c[i + 1] != '%') return cb_throw(R, 6, "URI malformed"); int x = hv(s->c[i + 2]) * 16 + hv(s->c[i + 3]); if (x < 0 || (x & 0xC0) != 0x80) return cb_throw(R, 6, "URI malformed"); cp = cp << 6 | (x & 63); i += 3; }
        if (cp >= 0x10000) { cp -= 0x10000; o->c[k++] = (u16)(0xD800 + (cp >> 10)); o->c[k++] = (u16)(0xDC00 + (cp & 1023)); } else o->c[k++] = (u16)cp; }
    o->len = k; return cb_strv(o); }
NF(g_encodeURIComponent) { return uri_encode(R, ARG(0), "-_.!~*'()"); }
NF(g_encodeURI) { return uri_encode(R, ARG(0), "-_.!~*'();/?:@&=+$,#"); }
NF(g_decodeURIComponent) { return uri_decode(R, ARG(0), ""); }
NF(g_decodeURI) { return uri_decode(R, ARG(0), ";/?:@&=+$,#"); }
NF(g_escape) { cbv sv = cb_to_str(R, ARG(0)); TRY(sv); cb_str *s = cb_flat(cb_s(sv)); cb_str *o = cb_str_new(s->len * 6 + 1); u32 k = 0;
    for (u32 i = 0; i < s->len; i++) { u32 c = s->c[i]; if (c < 128 && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("@*_+-./", (int)c)) && c) o->c[k++] = (u16)c; else if (c < 256) { o->c[k++] = '%'; o->c[k++] = (u16)HEXD[c >> 4]; o->c[k++] = (u16)HEXD[c & 15]; } else { o->c[k++] = '%'; o->c[k++] = 'u'; for (int j = 3; j >= 0; j--) o->c[k++] = (u16)HEXD[(c >> (4 * j)) & 15]; } }
    o->len = k; o->ascii = 1; return cb_strv(o); }
NF(g_unescape) { cbv sv = cb_to_str(R, ARG(0)); TRY(sv); cb_str *s = cb_flat(cb_s(sv)); cb_str *o = cb_str_new(s->len); u32 k = 0;
    for (u32 i = 0; i < s->len; i++) { u16 c = s->c[i]; if (c == '%' && i + 5 < s->len && s->c[i + 1] == 'u' && hv(s->c[i + 2]) >= 0 && hv(s->c[i + 3]) >= 0 && hv(s->c[i + 4]) >= 0 && hv(s->c[i + 5]) >= 0) { o->c[k++] = (u16)(hv(s->c[i + 2]) << 12 | hv(s->c[i + 3]) << 8 | hv(s->c[i + 4]) << 4 | hv(s->c[i + 5])); i += 5; }
        else if (c == '%' && i + 2 < s->len && hv(s->c[i + 1]) >= 0 && hv(s->c[i + 2]) >= 0) { o->c[k++] = (u16)(hv(s->c[i + 1]) * 16 + hv(s->c[i + 2])); i += 2; } else o->c[k++] = c; }
    o->len = k; return cb_strv(o); }
NF(g_eval) { cbv src = ARG(0); if (!cb_isstr(src)) return src; long n; char *u = cb_str_utf8z(cb_s(src), &n); cbv r = CB_UNDEF; int e = cb_eval(R, u, n, "eval", &r, 0); free(u); return e ? CB_EMPTY : r; }

/* ---------------- JSON ---------------- */
typedef struct { cb_rt *R; cb_str *s; u32 i; } jp;
static void jws(jp *p) { while (p->i < p->s->len && (p->s->c[p->i] == ' ' || p->s->c[p->i] == '\t' || p->s->c[p->i] == '\n' || p->s->c[p->i] == '\r')) p->i++; }
static cbv jerr(jp *p) { if (p->i >= p->s->len) return cb_throw(p->R, 3, "Unexpected end of JSON input"); u16 c = p->s->c[p->i]; char b[8]; if (c < 128) { b[0] = (char)c; b[1] = 0; } else strcpy(b, "?"); return cb_throw(p->R, 3, "Unexpected token '%s', \"...\" is not valid JSON (posicion %u)", b, p->i); }
static cbv jval(jp *p, int depth) {
    cb_rt *R = p->R; jws(p); if (p->i >= p->s->len) return jerr(p); if (depth > 5000) return cb_throw(R, 2, "JSON demasiado anidado");
    u16 c = p->s->c[p->i];
    if (c == '{') { p->i++; cb_obj *o = cb_new_obj(R, R->ObjectP); jws(p); if (p->i < p->s->len && p->s->c[p->i] == '}') { p->i++; return cb_objv(o); }
        for (;;) { jws(p); if (p->i >= p->s->len || p->s->c[p->i] != '"') return jerr(p); cbv k = jval(p, depth + 1); TRY(k); k = cb_intern(R, cb_s(k)); jws(p); if (p->i >= p->s->len || p->s->c[p->i] != ':') return jerr(p); p->i++; cbv v = jval(p, depth + 1); TRY(v); cb_define(R, o, k, v, PF_WEC); jws(p); if (p->i < p->s->len && p->s->c[p->i] == ',') { p->i++; continue; } if (p->i < p->s->len && p->s->c[p->i] == '}') { p->i++; return cb_objv(o); } return jerr(p); } }
    if (c == '[') { p->i++; cb_obj *a = cb_new_array(R, 0); jws(p); if (p->i < p->s->len && p->s->c[p->i] == ']') { p->i++; return cb_objv(a); }
        for (;;) { cbv v = jval(p, depth + 1); TRY(v); cb_arr_push(R, a, v); jws(p); if (p->i < p->s->len && p->s->c[p->i] == ',') { p->i++; continue; } if (p->i < p->s->len && p->s->c[p->i] == ']') { p->i++; return cb_objv(a); } return jerr(p); } }
    if (c == '"') { p->i++; u32 st = p->i, esc = 0; while (p->i < p->s->len && p->s->c[p->i] != '"') { if (p->s->c[p->i] == '\\') { esc = 1; p->i++; } else if (p->s->c[p->i] < 32) return jerr(p); p->i++; } if (p->i >= p->s->len) return jerr(p);
        if (!esc) { cb_str *r = cb_substr(p->s, st, p->i); p->i++; return cb_strv(r); }
        cb_str *r = cb_str_new(p->i - st); u32 k = 0;
        for (u32 j = st; j < p->i; j++) { u16 d = p->s->c[j]; if (d != '\\') { r->c[k++] = d; continue; } d = p->s->c[++j];
            switch (d) { case 'n': r->c[k++] = '\n'; break; case 't': r->c[k++] = '\t'; break; case 'r': r->c[k++] = '\r'; break; case 'b': r->c[k++] = '\b'; break; case 'f': r->c[k++] = '\f'; break; case '/': case '\\': case '"': r->c[k++] = d; break;
                case 'u': { if (j + 4 >= p->i + 0 && j + 4 > p->s->len) return jerr(p); int x = 0; for (int q = 1; q <= 4; q++) { int h = hv(p->s->c[j + q]); if (h < 0) { p->i = j; return jerr(p); } x = x * 16 + h; } r->c[k++] = (u16)x; j += 4; break; }
                default: p->i = j; return jerr(p); } }
        r->len = k; p->i++; return cb_strv(r); }
    if (c == '-' || (c >= '0' && c <= '9')) { char b[400]; int k = 0; u32 st = p->i; if (c == '-') b[k++] = (char)p->s->c[p->i++];
        if (p->i < p->s->len && p->s->c[p->i] == '0' && p->i + 1 < p->s->len && p->s->c[p->i + 1] >= '0' && p->s->c[p->i + 1] <= '9') return jerr(p);
        int dig = 0; while (p->i < p->s->len && k < 390) { u16 d = p->s->c[p->i]; if ((d >= '0' && d <= '9') || d == '.' || d == 'e' || d == 'E' || ((d == '+' || d == '-') && (b[k - 1] == 'e' || b[k - 1] == 'E'))) { if (d >= '0' && d <= '9') dig = 1; b[k++] = (char)d; p->i++; } else break; }
        if (!dig || b[k - 1] == '.' || b[k - 1] == 'e' || b[k - 1] == 'E') { p->i = st; return jerr(p); } b[k] = 0; return cb_num(cb_parse_number(b, k)); }
    static const char *lit[] = { "true", "false", "null" }; static const cbv lv[] = { CB_TRUE, CB_FALSE, CB_NULL };
    for (int q = 0; q < 3; q++) { u32 l = (u32)strlen(lit[q]); if (p->i + l <= p->s->len) { int ok = 1; for (u32 j = 0; j < l; j++) if (p->s->c[p->i + j] != (u8)lit[q][j]) ok = 0; if (ok) { p->i += l; return lv[q]; } } }
    return jerr(p); }
static cbv revive(cb_rt *R, cbv holder, cbv key, cbv rev) {
    cbv v = cb_get(R, holder, key); TRY(v);
    if (cb_isobj(v)) { cbv ks = cb_own_keys(R, v, 5); TRY(ks); cb_obj *ka = cb_o(ks); for (u32 i = 0; i < ka->n1; i++) { cbv nv = revive(R, v, ka->e[i], rev); TRY(nv); if (cb_isundef(nv)) cb_del(R, v, ka->e[i], 0); else cb_define(R, cb_o(v), ka->e[i], nv, PF_WEC); } }
    cbv a[2] = { key, v }; return cb_call_v(R, rev, holder, 2, a); }
NF(JSON_parse) { cbv sv = cb_to_str(R, ARG(0)); TRY(sv); jp p = { R, cb_flat(cb_s(sv)), 0 }; cbv v = jval(&p, 0); TRY(v); jws(&p); if (p.i < p.s->len) return jerr(&p);
    if (cb_isfunc(ARG(1))) { cb_obj *root = cb_new_obj(R, R->ObjectP); cbv e = cb_atom(R, ""); cb_add_prop(R, root, e, v, PF_WEC); return revive(R, cb_objv(root), e, ARG(1)); } return v; }
typedef struct { cb_rt *R; cb_str *out; cbv repl; cbv allow; cb_str *gap, *indent; cbv stack[128]; int depth; } js;
static void jout(js *j, cb_str *s) { j->out = cb_concat(j->out, s); }
static void jouta(js *j, const char *s) { jout(j, cb_str_ascii(s, (u32)strlen(s))); }
static void jquote(js *j, cb_str *s) {
    s = cb_flat(s); cb_str *o = cb_str_new(s->len * 6 + 2); u32 k = 0; o->c[k++] = '"';
    for (u32 i = 0; i < s->len; i++) { u16 c = s->c[i];
        if (c == '"' || c == '\\') { o->c[k++] = '\\'; o->c[k++] = c; }
        else if (c < 32) { o->c[k++] = '\\'; switch (c) { case '\n': o->c[k++] = 'n'; break; case '\r': o->c[k++] = 'r'; break; case '\t': o->c[k++] = 't'; break; case '\b': o->c[k++] = 'b'; break; case '\f': o->c[k++] = 'f'; break; default: o->c[k++] = 'u'; o->c[k++] = '0'; o->c[k++] = '0'; o->c[k++] = (u16)HEXD[c >> 4]; o->c[k++] = (u16)(HEXD[c & 15] | 32); if ((c & 15) < 10) o->c[k - 1] = (u16)HEXD[c & 15]; } }
        else if (c >= 0xD800 && c < 0xE000 && !(c < 0xDC00 && i + 1 < s->len && s->c[i + 1] >= 0xDC00 && s->c[i + 1] < 0xE000) && !(c >= 0xDC00 && i > 0 && s->c[i - 1] >= 0xD800 && s->c[i - 1] < 0xDC00)) { const char *hx = "0123456789abcdef"; o->c[k++] = '\\'; o->c[k++] = 'u'; for (int q = 3; q >= 0; q--) o->c[k++] = (u16)hx[(c >> (4 * q)) & 15]; }
        else o->c[k++] = c; }
    o->c[k++] = '"'; o->len = k; jout(j, o); }
static int jser(js *j, cbv holder, cbv key, cbv v);
static int jser_nl(js *j) { if (!j->gap->len) return 0; jouta(j, "\n"); jout(j, j->indent); return 0; }
static int jser(js *j, cbv holder, cbv key, cbv v) {   /* 1 escrito, 0 omitido (undefined), -1 excepcion */
    cb_rt *R = j->R;
    if (cb_isobj(v)) { cbv tj = cb_get(R, v, cb_atom(R, "toJSON")); if (tj == CB_EMPTY) return -1; if (cb_isfunc(tj)) { v = cb_call_v(R, tj, v, 1, &key); if (v == CB_EMPTY) return -1; } }
    if (cb_isfunc(j->repl)) { cbv a[2] = { key, v }; v = cb_call_v(R, j->repl, holder, 2, a); if (v == CB_EMPTY) return -1; }
    if (cb_isobj(v)) { cb_obj *o = cb_o(v); if (o->cls == OC_NUMBER) { double d = cb_to_num(R, v); v = cb_num(d); } else if (o->cls == OC_STRING) v = cb_to_str(R, v); else if (o->cls == OC_BOOL) v = o->v1; }
    if (cb_isnull(v)) { jouta(j, "null"); return 1; }
    if (cb_isbool(v)) { jouta(j, v == CB_TRUE ? "true" : "false"); return 1; }
    if (cb_isstr(v)) { jquote(j, cb_s(v)); return 1; }
    if (cb_isnum(v)) { double d = cb_d(v); if (!__builtin_isfinite(d)) jouta(j, "null"); else jout(j, cb_s(cb_num_to_str(R, d))); return 1; }
    if (cb_isbig(v)) { cb_throw(R, 1, "Do not know how to serialize a BigInt"); return -1; }
    if (!cb_isobj(v) || cb_isfunc(v)) return 0;
    for (int i = 0; i < j->depth; i++) if (j->stack[i] == v) { cb_throw(R, 1, "Converting circular structure to JSON"); return -1; }
    if (j->depth >= 128) { cb_throw(R, 2, "Maximum call stack size exceeded"); return -1; }
    j->stack[j->depth++] = v; cb_str *oldind = j->indent; j->indent = cb_concat(j->indent, j->gap);
    int isarr = cb_o(v)->cls == OC_ARRAY || (cb_o(v)->cls == OC_PROXY && cb_isobj(cb_o(v)->v1) && cb_o(cb_o(v)->v1)->cls == OC_ARRAY);
    if (isarr) { double n = lenof(R, v); if (n < 0) return -1; jouta(j, "[");
        for (double i = 0; i < n; i++) { if (i > 0) jouta(j, ","); jser_nl(j); cbv x = geti(R, v, i); if (x == CB_EMPTY) return -1; int r = jser(j, v, cb_num_to_str(R, i), x); if (r < 0) return -1; if (!r) jouta(j, "null"); }
        j->indent = oldind; if (n > 0) jser_nl(j); jouta(j, "]"); }
    else { cbv ks = cb_isobj(j->allow) ? j->allow : cb_own_keys(R, v, 5); if (ks == CB_EMPTY) return -1; cb_obj *ka = cb_o(ks); jouta(j, "{"); int any = 0;
        for (u32 i = 0; i < ka->n1; i++) { cbv k = ka->e[i]; if (!cb_isstr(k)) continue; cbv x = cb_get(R, v, k); if (x == CB_EMPTY) return -1;
            cb_str *save = j->out; if (any) jouta(j, ","); jser_nl(j); jquote(j, cb_s(k)); jouta(j, j->gap->len ? ": " : ":");
            int r = jser(j, v, k, x); if (r < 0) return -1; if (!r) j->out = save; else any = 1; }
        j->indent = oldind; if (any) jser_nl(j); jouta(j, "}"); }
    j->depth--; return 1; }
cbv cb_json_str(cb_rt *R, cbv v) {
    js j; memset(&j, 0, sizeof j); j.R = R; j.out = cb_str_ascii("", 0); j.repl = CB_UNDEF; j.allow = CB_UNDEF; j.gap = cb_str_ascii("", 0); j.indent = j.gap;
    cb_obj *h = cb_new_obj(R, R->ObjectP); cbv e = cb_atom(R, ""); cb_add_prop(R, h, e, v, PF_WEC);
    int r = jser(&j, cb_objv(h), e, v); if (r < 0) return CB_EMPTY; if (!r) return CB_UNDEF; return cb_strv(j.out); }
NF(JSON_stringify) {
    js j; memset(&j, 0, sizeof j); j.R = R; j.out = cb_str_ascii("", 0); j.repl = CB_UNDEF; j.allow = CB_UNDEF; j.gap = cb_str_ascii("", 0);
    cbv rp = ARG(1); if (cb_isfunc(rp)) j.repl = rp;
    else if (cb_isobj(rp) && cb_o(rp)->cls == OC_ARRAY) { cb_obj *al = cb_new_array(R, 0); cb_obj *ra = cb_o(rp); for (u32 i = 0; i < ra->n1; i++) { cbv x = ra->e[i]; if (cb_isstr(x) || cb_isnum(x) || (cb_isobj(x) && (cb_o(x)->cls == OC_STRING || cb_o(x)->cls == OC_NUMBER))) { cbv k = cb_key(R, cb_to_str(R, x)); int dup = 0; for (u32 q = 0; q < al->n1; q++) if (al->e[q] == k) dup = 1; if (!dup) cb_arr_push(R, al, k); } } j.allow = cb_objv(al); }
    cbv sp = ARG(2); if (cb_isobj(sp)) { if (cb_o(sp)->cls == OC_NUMBER) sp = cb_num(cb_to_num(R, sp)); else if (cb_o(sp)->cls == OC_STRING) sp = cb_to_str(R, sp); }
    if (cb_isnum(sp)) { int n = (int)cb_to_int(R, sp); if (n > 10) n = 10; if (n > 0) { j.gap = cb_str_new((u32)n); for (int i = 0; i < n; i++) j.gap->c[i] = ' '; j.gap->ascii = 1; } }
    else if (cb_isstr(sp)) j.gap = cb_substr(cb_s(sp), 0, 10);
    j.indent = cb_str_ascii("", 0);
    cb_obj *h = cb_new_obj(R, R->ObjectP); cbv e = cb_atom(R, ""); cb_add_prop(R, h, e, ARG(0), PF_WEC);
    int r = jser(&j, cb_objv(h), e, ARG(0)); if (r < 0) return CB_EMPTY; if (!r) return CB_UNDEF; return cb_strv(j.out); }

/* ---------------- registro de todo ---------------- */
void cb_lib_init(cb_rt *R) {
    cb_obj *G = R->global;
    cb_add_prop(R, G, cb_atom(R, "globalThis"), cb_objv(G), PF_W | PF_C);
    cb_add_prop(R, G, cb_atom(R, "undefined"), CB_UNDEF, 0); cb_add_prop(R, G, cb_atom(R, "NaN"), cb_num(__builtin_nan("")), 0); cb_add_prop(R, G, cb_atom(R, "Infinity"), cb_num(__builtin_inf()), 0);
    R->FunctionP->p1 = (void *)FP_empty; cb_add_prop(R, R->FunctionP, A_length, cb_num(0), PF_C); cb_add_prop(R, R->FunctionP, A_name, cb_atom(R, ""), PF_C);
    /* Object */
    cb_obj *Oc = mkctor(R, "Object", Object_ctor, 1, R->ObjectP); R->ObjectC = Oc;
    cb_defn(R, Oc, "keys", Object_keys, 1); cb_defn(R, Oc, "values", Object_values, 1); cb_defn(R, Oc, "entries", Object_entries, 1); cb_defn(R, Oc, "assign", Object_assign, 2);
    cb_defn(R, Oc, "create", Object_create, 2); cb_defn(R, Oc, "defineProperty", Object_defineProperty, 3); cb_defn(R, Oc, "defineProperties", Object_defineProperties, 2);
    cb_defn(R, Oc, "getOwnPropertyNames", Object_getOwnPropertyNames, 1); cb_defn(R, Oc, "getOwnPropertySymbols", Object_getOwnPropertySymbols, 1); cb_defn(R, Oc, "getOwnPropertyDescriptor", Object_getOwnPropertyDescriptor, 2);
    cb_defn(R, Oc, "getOwnPropertyDescriptors", Object_getOwnPropertyDescriptors, 1); cb_defn(R, Oc, "getPrototypeOf", Object_getPrototypeOf, 1); cb_defn(R, Oc, "setPrototypeOf", Object_setPrototypeOf, 2);
    cb_defn(R, Oc, "freeze", Object_freeze, 1); cb_defn(R, Oc, "seal", Object_seal, 1); cb_defn(R, Oc, "preventExtensions", Object_preventExtensions, 1); cb_defn(R, Oc, "isFrozen", Object_isFrozen, 1);
    cb_defn(R, Oc, "isSealed", Object_isSealed, 1); cb_defn(R, Oc, "isExtensible", Object_isExtensible, 1); cb_defn(R, Oc, "is", Object_is, 2); cb_defn(R, Oc, "hasOwn", Object_hasOwn, 2);
    cb_defn(R, Oc, "fromEntries", Object_fromEntries, 1); cb_defn(R, Oc, "groupBy", Object_groupBy, 2);
    cb_obj *OP = R->ObjectP; cb_defn(R, OP, "hasOwnProperty", OP_hasOwnProperty, 1); cb_defn(R, OP, "isPrototypeOf", OP_isPrototypeOf, 1); cb_defn(R, OP, "propertyIsEnumerable", OP_propertyIsEnumerable, 1);
    cb_defn(R, OP, "toString", OP_toString, 0); cb_defn(R, OP, "toLocaleString", OP_toLocaleString, 0); cb_defn(R, OP, "valueOf", OP_valueOf, 0); cb_defgs(R, OP, "__proto__", OP_proto_get, OP_proto_set);
    /* Function */
    cb_obj *Fc = mkctor(R, "Function", Function_ctor, 1, R->FunctionP); R->FunctionC = Fc;
    cb_defn(R, R->FunctionP, "call", FP_call, 1); cb_defn(R, R->FunctionP, "apply", FP_apply, 2); cb_defn(R, R->FunctionP, "bind", FP_bind, 1); cb_defn(R, R->FunctionP, "toString", FP_toString, 0);
    { cbv hi = cb_new_native(R, "[Symbol.hasInstance]", FP_hasInstance, 1); cb_o(hi)->n2 = 0x1E5; cb_add_prop(R, R->FunctionP, S_hasInstance, hi, 0); }
    /* Array */
    R->ArrayP = cb_new_array(R, 0); R->ArrayP->proto = R->ObjectP;
    cb_obj *Ac = mkctor(R, "Array", Array_ctor, 1, R->ArrayP); R->ArrayC = Ac;
    cb_defn(R, Ac, "isArray", Array_isArray, 1); cb_defn(R, Ac, "of", Array_of, 0); cb_defn(R, Ac, "from", Array_from, 1);
    struct { const char *n; cb_native f; int a; } AM[] = { { "push", AP_push, 1 }, { "pop", AP_pop, 0 }, { "shift", AP_shift, 0 }, { "unshift", AP_unshift, 1 }, { "slice", AP_slice, 2 }, { "splice", AP_splice, 2 },
        { "concat", AP_concat, 1 }, { "join", AP_join, 1 }, { "toString", AP_toString, 0 }, { "toLocaleString", AP_toString, 0 }, { "reverse", AP_reverse, 0 }, { "indexOf", AP_indexOf, 1 }, { "lastIndexOf", AP_lastIndexOf, 1 },
        { "includes", AP_includes, 1 }, { "forEach", AP_forEach, 1 }, { "map", AP_map, 1 }, { "filter", AP_filter, 1 }, { "some", AP_some, 1 }, { "every", AP_every, 1 }, { "find", AP_find, 1 },
        { "findIndex", AP_findIndex, 1 }, { "findLast", AP_findLast, 1 }, { "findLastIndex", AP_findLastIndex, 1 }, { "flatMap", AP_flatMap, 1 }, { "reduce", AP_reduce, 1 }, { "reduceRight", AP_reduceRight, 1 },
        { "fill", AP_fill, 1 }, { "flat", AP_flat, 0 }, { "at", AP_at, 1 }, { "copyWithin", AP_copyWithin, 2 }, { "sort", AP_sort, 1 }, { "toSorted", AP_toSorted, 1 }, { "toReversed", AP_toReversed, 0 },
        { "toSpliced", AP_toSpliced, 2 }, { "with", AP_with, 2 }, { "keys", AP_keys, 0 }, { "entries", AP_entries, 0 } };
    for (u32 i = 0; i < sizeof AM / sizeof *AM; i++) cb_defn(R, R->ArrayP, AM[i].n, AM[i].f, AM[i].a);
    { cbv vf = cb_new_native(R, "values", AP_values, 0); cb_add_prop(R, R->ArrayP, cb_atom(R, "values"), vf, PF_W | PF_C); cb_add_prop(R, R->ArrayP, S_iterator, vf, PF_W | PF_C); }
    /* String */
    R->StringP = cb_new_obj(R, R->ObjectP); R->StringP->cls = OC_STRING; R->StringP->v1 = cb_strv(cb_str_ascii("", 0));
    cb_obj *Sc = mkctor(R, "String", String_ctor, 1, R->StringP);
    cb_defn(R, Sc, "fromCharCode", String_fromCharCode, 1); cb_defn(R, Sc, "fromCodePoint", String_fromCodePoint, 1); cb_defn(R, Sc, "raw", String_raw, 1);
    struct { const char *n; cb_native f; int a; } SM[] = { { "toString", SP_toString, 0 }, { "valueOf", SP_valueOf, 0 }, { "charAt", SP_charAt, 1 }, { "charCodeAt", SP_charCodeAt, 1 }, { "codePointAt", SP_codePointAt, 1 },
        { "at", SP_at, 1 }, { "indexOf", SP_indexOf, 1 }, { "lastIndexOf", SP_lastIndexOf, 1 }, { "includes", SP_includes, 1 }, { "startsWith", SP_startsWith, 1 }, { "endsWith", SP_endsWith, 1 },
        { "slice", SP_slice, 2 }, { "substring", SP_substring, 2 }, { "substr", SP_substr, 2 }, { "concat", SP_concat, 1 }, { "repeat", SP_repeat, 1 }, { "padStart", SP_padStart, 2 }, { "padEnd", SP_padEnd, 2 },
        { "trim", SP_trim, 0 }, { "trimStart", SP_trimStart, 0 }, { "trimEnd", SP_trimEnd, 0 }, { "trimLeft", SP_trimStart, 0 }, { "trimRight", SP_trimEnd, 0 }, { "toLowerCase", SP_toLowerCase, 0 },
        { "toUpperCase", SP_toUpperCase, 0 }, { "toLocaleLowerCase", SP_toLowerCase, 0 }, { "toLocaleUpperCase", SP_toUpperCase, 0 }, { "localeCompare", SP_localeCompare, 1 }, { "normalize", SP_normalize, 0 },
        { "isWellFormed", SP_isWellFormed, 0 } };
    for (u32 i = 0; i < sizeof SM / sizeof *SM; i++) cb_defn(R, R->StringP, SM[i].n, SM[i].f, SM[i].a);
    cb_add_prop(R, R->StringP, S_iterator, cb_new_native(R, "[Symbol.iterator]", SP_iterator, 0), PF_W | PF_C);
    /* Number */
    R->NumberP = cb_new_obj(R, R->ObjectP); R->NumberP->cls = OC_NUMBER; R->NumberP->v1 = cb_num(0);
    cb_obj *Nc = mkctor(R, "Number", Number_ctor, 1, R->NumberP);
    cb_defp(R, Nc, "MAX_SAFE_INTEGER", cb_num(9007199254740991.0), 0); cb_defp(R, Nc, "MIN_SAFE_INTEGER", cb_num(-9007199254740991.0), 0); cb_defp(R, Nc, "EPSILON", cb_num(2.220446049250313e-16), 0);
    cb_defp(R, Nc, "MAX_VALUE", cb_num(1.7976931348623157e308), 0); cb_defp(R, Nc, "MIN_VALUE", cb_num(5e-324), 0); cb_defp(R, Nc, "NaN", cb_num(__builtin_nan("")), 0);
    cb_defp(R, Nc, "POSITIVE_INFINITY", cb_num(__builtin_inf()), 0); cb_defp(R, Nc, "NEGATIVE_INFINITY", cb_num(-__builtin_inf()), 0);
    cb_defn(R, Nc, "isFinite", Number_isFinite, 1); cb_defn(R, Nc, "isNaN", Number_isNaN, 1); cb_defn(R, Nc, "isInteger", Number_isInteger, 1); cb_defn(R, Nc, "isSafeInteger", Number_isSafeInteger, 1);
    cbv pf = cb_new_native(R, "parseFloat", g_parseFloat, 1), pi = cb_new_native(R, "parseInt", g_parseInt, 2);
    cb_defp(R, Nc, "parseFloat", pf, PF_W | PF_C); cb_defp(R, Nc, "parseInt", pi, PF_W | PF_C); cb_defp(R, G, "parseFloat", pf, PF_W | PF_C); cb_defp(R, G, "parseInt", pi, PF_W | PF_C);
    cb_defn(R, R->NumberP, "toString", NP_toString, 1); cb_defn(R, R->NumberP, "valueOf", NP_valueOf, 0); cb_defn(R, R->NumberP, "toFixed", NP_toFixed, 1);
    cb_defn(R, R->NumberP, "toExponential", NP_toExponential, 1); cb_defn(R, R->NumberP, "toPrecision", NP_toPrecision, 1); cb_defn(R, R->NumberP, "toLocaleString", NP_toLocaleString, 0);
    /* Boolean, Symbol */
    R->BooleanP = cb_new_obj(R, R->ObjectP); R->BooleanP->cls = OC_BOOL; R->BooleanP->v1 = CB_FALSE;
    mkctor(R, "Boolean", Boolean_ctor, 1, R->BooleanP); cb_defn(R, R->BooleanP, "toString", BP_toString, 0); cb_defn(R, R->BooleanP, "valueOf", BP_valueOf, 0);
    R->SymbolP = cb_new_obj(R, R->ObjectP); registry = cb_new_obj(R, 0); { static cbv regv; regv = cb_objv(registry); cb_root(&regv); }
    cb_obj *Yc = mkctor(R, "Symbol", Symbol_ctor, 0, R->SymbolP); Yc->fk &= ~2;
    cb_defn(R, Yc, "for", Symbol_for, 1); cb_defn(R, Yc, "keyFor", Symbol_keyFor, 1);
    struct { const char *n; cbv *s; } WK[] = { { "iterator", &S_iterator }, { "asyncIterator", &S_asyncIterator }, { "hasInstance", &S_hasInstance }, { "toPrimitive", &S_toPrimitive }, { "toStringTag", &S_toStringTag },
        { "species", &S_species }, { "isConcatSpreadable", &S_isConcatSpreadable }, { "unscopables", &S_unscopables }, { "match", &S_match }, { "matchAll", &S_matchAll }, { "replace", &S_replace }, { "search", &S_search }, { "split", &S_split } };
    for (u32 i = 0; i < sizeof WK / sizeof *WK; i++) cb_defp(R, Yc, WK[i].n, *WK[i].s, 0);
    cb_defn(R, R->SymbolP, "toString", SyP_toString, 0); cb_defn(R, R->SymbolP, "valueOf", SyP_valueOf, 0); cb_defgs(R, R->SymbolP, "description", SyP_description, 0);
    cb_add_prop(R, R->SymbolP, S_toStringTag, cb_atom(R, "Symbol"), PF_C);
    /* Math */
    cb_obj *M = cb_new_obj(R, R->ObjectP); cb_add_prop(R, G, cb_atom(R, "Math"), cb_objv(M), PF_W | PF_C); cb_add_prop(R, M, S_toStringTag, cb_atom(R, "Math"), PF_C);
    struct { const char *n; double v; } MC[] = { { "PI", 3.141592653589793 }, { "E", 2.718281828459045 }, { "LN2", 0.6931471805599453 }, { "LN10", 2.302585092994046 }, { "LOG2E", 1.4426950408889634 }, { "LOG10E", 0.4342944819032518 }, { "SQRT2", 1.4142135623730951 }, { "SQRT1_2", 0.7071067811865476 } };
    for (u32 i = 0; i < sizeof MC / sizeof *MC; i++) cb_defp(R, M, MC[i].n, cb_num(MC[i].v), 0);
    struct { const char *n; cb_native f; int a; } MM[] = { { "sqrt", M_sqrt, 1 }, { "sin", M_sin, 1 }, { "cos", M_cos, 1 }, { "tan", M_tan, 1 }, { "asin", M_asin, 1 }, { "acos", M_acos, 1 }, { "atan", M_atan, 1 },
        { "exp", M_exp, 1 }, { "log", M_log, 1 }, { "log2", M_log2, 1 }, { "log10", M_log10, 1 }, { "expm1", M_expm1, 1 }, { "log1p", M_log1p, 1 }, { "sinh", M_sinh, 1 }, { "cosh", M_cosh, 1 }, { "tanh", M_tanh, 1 },
        { "asinh", M_asinh, 1 }, { "acosh", M_acosh, 1 }, { "atanh", M_atanh, 1 }, { "cbrt", M_cbrt, 1 }, { "floor", M_floor, 1 }, { "ceil", M_ceil, 1 }, { "trunc", M_trunc, 1 }, { "round", M_round, 1 }, { "fround", M_fround, 1 },
        { "abs", M_abs, 1 }, { "sign", M_sign, 1 }, { "pow", M_pow, 2 }, { "atan2", M_atan2, 2 }, { "max", M_max, 2 }, { "min", M_min, 2 }, { "hypot", M_hypot, 2 }, { "random", M_random, 0 }, { "imul", M_imul, 2 }, { "clz32", M_clz32, 1 } };
    for (u32 i = 0; i < sizeof MM / sizeof *MM; i++) cb_defn(R, M, MM[i].n, MM[i].f, MM[i].a);
    /* Error */
    static const char *EN[] = { "Error", "TypeError", "RangeError", "SyntaxError", "ReferenceError", "EvalError", "URIError", "AggregateError" };
    cb_native EF[] = { E_Error, E_Type, E_Range, E_Syntax, E_Reference, E_Eval, E_URI, E_Aggregate };
    for (int i = 0; i < 8; i++) { cb_obj *p = cb_new_obj(R, i ? R->ErrP[0] : R->ObjectP); R->ErrP[i] = p; cb_obj *c = mkctor(R, EN[i], EF[i], 1, p); if (i) c->proto = cb_o(cb_get(R, cb_objv(G), cb_atom(R, "Error"))); cb_defp(R, p, "name", cb_atom(R, EN[i]), PF_W | PF_C); cb_defp(R, p, "message", cb_atom(R, ""), PF_W | PF_C); if (!i) { cb_defn(R, p, "toString", EP_toString, 0); cb_defn(R, c, "captureStackTrace", E_captureStackTrace, 1); } }
    R->ErrorP = R->ErrP[0];
    /* globales */
    cb_defn(R, G, "isNaN", g_isNaN, 1); cb_defn(R, G, "isFinite", g_isFinite, 1); cb_defn(R, G, "encodeURIComponent", g_encodeURIComponent, 1); cb_defn(R, G, "encodeURI", g_encodeURI, 1);
    cb_defn(R, G, "decodeURIComponent", g_decodeURIComponent, 1); cb_defn(R, G, "decodeURI", g_decodeURI, 1); cb_defn(R, G, "escape", g_escape, 1); cb_defn(R, G, "unescape", g_unescape, 1); cb_defn(R, G, "eval", g_eval, 1);
    cb_obj *J = cb_new_obj(R, R->ObjectP); cb_add_prop(R, G, cb_atom(R, "JSON"), cb_objv(J), PF_W | PF_C); cb_add_prop(R, J, S_toStringTag, cb_atom(R, "JSON"), PF_C);
    cb_defn(R, J, "parse", JSON_parse, 2); cb_defn(R, J, "stringify", JSON_stringify, 3);
    cb_big_init(R);
}
