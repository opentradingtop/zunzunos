/* Colibri: iteradores, generadores, Promise, Map/Set/WeakMap/WeakSet, Reflect, Proxy, Date, ArrayBuffer,
   matrices tipadas y DataView. */
#include "cbi.h"
int cb_get_own_desc(cb_rt *R, cb_obj *o, cbv key, cbv *val, u32 *flags, cbv *get, cbv *set);
int cb_has_own(cb_rt *R, cb_obj *o, cbv key);
void cb_define_acc(cb_rt *R, cb_obj *o, cbv key, cbv get, cbv set, u32 flags);
cbv cb_asyncgen_resume(cb_rt *R, cb_obj *go, int mode, cbv v);
int cb_fmt_num(double d, char *o);
double cb_parse_number(const char *s, int len);
double cb_math1(int which, double x);
#define NF(name) static cbv name(cb_rt *R, cbv t, int argc, cbv *argv, cb_obj *fn)
#define ARG(i) ((i) < argc ? argv[i] : CB_UNDEF)
#define TRY(x) do { if ((x) == CB_EMPTY) return CB_EMPTY; } while (0)
static cbv iter_res(cb_rt *R, cbv v, int done) { cb_obj *o = cb_new_obj(R, R->ObjectP); cb_add_prop(R, o, A_value, v, PF_WEC); cb_add_prop(R, o, A_done, cb_bool(done), PF_WEC); return cb_objv(o); }
static cb_obj *mkctor(cb_rt *R, const char *name, cb_native f, int nargs, cb_obj *proto) {
    cbv c = cb_new_native(R, name, f, nargs); cb_obj *co = cb_o(c); co->fk |= 2;
    if (proto) { cb_add_prop(R, co, A_prototype, cb_objv(proto), 0); cb_add_prop(R, proto, A_constructor, c, PF_W | PF_C); }
    cb_add_prop(R, R->global, cb_atom(R, name), c, PF_W | PF_C); return co; }
static cb_obj *newthis(cb_rt *R, cb_obj *defproto, int cls) { cbv o = cb_ctor_this(R, R->nt, defproto); if (o == CB_EMPTY) return 0; cb_o(o)->cls = (u8)cls; return cb_o(o); }

/* ---------------- Map / Set (tabla con orden de insercion) ---------------- */
struct cb_map { cbv *k, *v; u32 n, cap, live; u32 *ix; u32 icap; };
static u32 vhash(cbv v) {
    if (cb_isnum(v)) { double d = cb_d(v); if (d == 0) d = 0; if (d != d) return 7; union { double d; u64 u; } c = { d }; return (u32)(c.u ^ (c.u >> 29) ^ (c.u >> 41)) * 2654435761u; }
    if (cb_isstr(v)) return cb_hash(cb_s(v)); if (cb_isbig(v)) return cb_big_hash(v); return (u32)(v >> 3) * 2654435761u; }
static int veq(cbv a, cbv b) { return cb_same_value(a, b, 1); }
static struct cb_map *map_of(cb_rt *R, cbv t, int cls) { if (!cb_isobj(t) || cb_o(t)->cls != cls) { cb_throw(R, 1, "Method called on incompatible receiver"); return 0; } cb_obj *o = cb_o(t); if (!o->p1) { struct cb_map *m = cb_blob(sizeof *m); memset(m, 0, sizeof *m); o->p1 = m; } return o->p1; }
static long map_find(struct cb_map *m, cbv k) {
    if (!m->ix) { for (u32 i = 0; i < m->n; i++) if (m->k[i] != CB_EMPTY && veq(m->k[i], k)) return i; return -1; }
    u32 h = vhash(k) & (m->icap - 1); for (;;) { u32 x = m->ix[h]; if (!x) return -1; if (m->k[x - 1] != CB_EMPTY && veq(m->k[x - 1], k)) return x - 1; h = (h + 1) & (m->icap - 1); } }
static void map_reindex(struct cb_map *m) {
    if (m->n < 8) { m->ix = 0; m->icap = 0; return; } u32 ic = 16; while (ic < m->n * 2) ic *= 2; u32 *ix = cb_raw(ic * 4); memset(ix, 0, ic * 4);
    for (u32 i = 0; i < m->n; i++) { if (m->k[i] == CB_EMPTY) continue; u32 h = vhash(m->k[i]) & (ic - 1); while (ix[h]) h = (h + 1) & (ic - 1); ix[h] = i + 1; } m->ix = ix; m->icap = ic; }
static void map_set(struct cb_map *m, cbv k, cbv v) {
    if (cb_isnum(k) && cb_d(k) == 0) k = cb_num(0);
    long i = map_find(m, k); if (i >= 0) { m->v[i] = v; return; }
    if (m->n == m->cap) { u32 nc = m->cap ? m->cap * 2 : 8; cbv *nk = cb_blob((u64)nc * 8), *nv = cb_blob((u64)nc * 8); if (m->n) { memcpy(nk, m->k, (u64)m->n * 8); memcpy(nv, m->v, (u64)m->n * 8); } m->k = nk; m->v = nv; m->cap = nc; }
    m->k[m->n] = k; m->v[m->n] = v; m->n++; m->live++;
    if (m->n >= 8) { if (!m->ix || m->n * 2 > m->icap) map_reindex(m); else { u32 h = vhash(k) & (m->icap - 1); while (m->ix[h]) h = (h + 1) & (m->icap - 1); m->ix[h] = m->n; } } }
static int map_del(struct cb_map *m, cbv k) { long i = map_find(m, k); if (i < 0) return 0; m->k[i] = CB_EMPTY; m->v[i] = CB_UNDEF; m->live--; return 1; }
static cbv map_fill(cb_rt *R, cbv o, cbv iterable, int isset) {
    if (cb_isnullish(iterable)) return o;
    cbv adder = cb_getp(R, o, isset ? "add" : "set"); TRY(adder); if (!cb_isfunc(adder)) return cb_throw(R, 1, "adder is not a function");
    cbv it = cb_get_iter(R, iterable, 0); TRY(it); cbv x; int r;
    while ((r = cb_iter_next(R, it, &x)) > 0) {
        if (isset) { if (cb_call_v(R, adder, o, 1, &x) == CB_EMPTY) { cb_iter_close(R, it); return CB_EMPTY; } continue; }
        if (!cb_isobj(x)) { cb_iter_close(R, it); return cb_throw(R, 1, "Iterator value is not an entry object"); }
        cbv a[2] = { cb_get(R, x, cb_idx_key(R, 0)), cb_get(R, x, cb_idx_key(R, 1)) }; if (a[0] == CB_EMPTY || a[1] == CB_EMPTY) return CB_EMPTY;
        if (cb_call_v(R, adder, o, 2, a) == CB_EMPTY) { cb_iter_close(R, it); return CB_EMPTY; } }
    return r < 0 ? CB_EMPTY : o; }
NF(Map_ctor) { if (cb_isundef(R->nt)) return cb_throw(R, 1, "Constructor Map requires 'new'"); cb_obj *o = newthis(R, R->MapP, OC_MAP); if (!o) return CB_EMPTY; return map_fill(R, cb_objv(o), ARG(0), 0); }
NF(Set_ctor) { if (cb_isundef(R->nt)) return cb_throw(R, 1, "Constructor Set requires 'new'"); cb_obj *o = newthis(R, R->SetP, OC_SET); if (!o) return CB_EMPTY; return map_fill(R, cb_objv(o), ARG(0), 1); }
NF(WeakMap_ctor) { if (cb_isundef(R->nt)) return cb_throw(R, 1, "Constructor WeakMap requires 'new'"); cb_obj *o = newthis(R, R->WeakMapP, OC_WEAKMAP); if (!o) return CB_EMPTY; return map_fill(R, cb_objv(o), ARG(0), 0); }
NF(WeakSet_ctor) { if (cb_isundef(R->nt)) return cb_throw(R, 1, "Constructor WeakSet requires 'new'"); cb_obj *o = newthis(R, R->WeakSetP, OC_WEAKSET); if (!o) return CB_EMPTY; return map_fill(R, cb_objv(o), ARG(0), 1); }
static int clsof(cbv t) { return cb_isobj(t) ? cb_o(t)->cls : -1; }
NF(MP_get) { struct cb_map *m = map_of(R, t, clsof(t) == OC_WEAKMAP ? OC_WEAKMAP : OC_MAP); if (!m) return CB_EMPTY; long i = map_find(m, ARG(0)); return i < 0 ? CB_UNDEF : m->v[i]; }
NF(MP_set) { int c = clsof(t) == OC_WEAKMAP ? OC_WEAKMAP : OC_MAP; struct cb_map *m = map_of(R, t, c); if (!m) return CB_EMPTY; if (c == OC_WEAKMAP && !cb_isobj(ARG(0))) return cb_throw(R, 1, "Invalid value used as weak map key"); map_set(m, ARG(0), ARG(1)); return t; }
NF(MP_has) { int c = clsof(t); struct cb_map *m = map_of(R, t, c == OC_WEAKMAP || c == OC_SET || c == OC_WEAKSET ? c : OC_MAP); if (!m) return CB_EMPTY; return cb_bool(map_find(m, ARG(0)) >= 0); }
NF(MP_delete) { int c = clsof(t); struct cb_map *m = map_of(R, t, c == OC_WEAKMAP || c == OC_SET || c == OC_WEAKSET ? c : OC_MAP); if (!m) return CB_EMPTY; return cb_bool(map_del(m, ARG(0))); }
NF(MP_clear) { int c = clsof(t); struct cb_map *m = map_of(R, t, c == OC_SET ? OC_SET : OC_MAP); if (!m) return CB_EMPTY; for (u32 i = 0; i < m->n; i++) { m->k[i] = CB_EMPTY; m->v[i] = CB_UNDEF; } m->live = 0; return CB_UNDEF; }
NF(MP_size) { int c = clsof(t); struct cb_map *m = map_of(R, t, c == OC_SET ? OC_SET : OC_MAP); if (!m) return CB_EMPTY; return cb_num(m->live); }
NF(SP_add) { int c = clsof(t) == OC_WEAKSET ? OC_WEAKSET : OC_SET; struct cb_map *m = map_of(R, t, c); if (!m) return CB_EMPTY; if (c == OC_WEAKSET && !cb_isobj(ARG(0))) return cb_throw(R, 1, "Invalid value used in weak set"); if (map_find(m, ARG(0)) < 0) map_set(m, ARG(0), ARG(0)); return t; }
NF(MP_forEach) { int c = clsof(t); struct cb_map *m = map_of(R, t, c == OC_SET ? OC_SET : OC_MAP); if (!m) return CB_EMPTY; if (!cb_isfunc(ARG(0))) return cb_throw(R, 1, "callback is not a function");
    for (u32 i = 0; i < m->n; i++) { if (m->k[i] == CB_EMPTY) continue; cbv a[3] = { m->v[i], m->k[i], t }; if (cb_call_v(R, ARG(0), ARG(1), 3, a) == CB_EMPTY) return CB_EMPTY; } return CB_UNDEF; }
NF(Map_groupBy) { cbv mp = cb_construct(R, cb_get(R, cb_objv(R->global), cb_atom(R, "Map")), 0, 0, cb_get(R, cb_objv(R->global), cb_atom(R, "Map"))); TRY(mp); struct cb_map *m = map_of(R, mp, OC_MAP);
    cbv it = cb_get_iter(R, ARG(0), 0); TRY(it); cbv e; int r, i = 0; while ((r = cb_iter_next(R, it, &e)) > 0) { cbv a[2] = { e, cb_num(i++) }; cbv k = cb_call_v(R, ARG(1), CB_UNDEF, 2, a); TRY(k); long j = map_find(m, k); if (j < 0) { map_set(m, k, cb_objv(cb_new_array(R, 0))); j = map_find(m, k); } cb_arr_push(R, cb_o(m->v[j]), e); } return r < 0 ? CB_EMPTY : mp; }

/* ---------------- iteradores ---------------- */
/* OC_ITER: v1 objeto, n1 posicion, fk tipo: 0 claves, 1 valores, 2 pares (array); 3 cadena; 4 pares, 5 claves, 6 valores (Map); 7 valores, 8 pares (Set) */
static cbv mkiter(cb_rt *R, cbv target, int kind, cb_obj *proto) { cb_obj *it = cb_new_obj(R, proto); it->cls = OC_ITER; it->v1 = target; it->fk = (u8)kind; it->n1 = 0; it->n2 = 0; return cb_objv(it); }
cbv cb_array_iter(cb_rt *R, cbv o, int kind) { return mkiter(R, o, kind, R->ArrayIterP); }
static cb_obj *StrIterP, *MapIterP, *SetIterP;
cbv cb_str_iter(cb_rt *R, cbv s) { return mkiter(R, s, 3, StrIterP); }
static cbv iter_step(cb_rt *R, cb_obj *it, int *done) {
    *done = 0; if (it->n2) { *done = 1; return CB_UNDEF; }
    cbv tg = it->v1;
    switch (it->fk) {
    case 0: case 1: case 2: {
        double len; if (cb_isobj(tg) && cb_o(tg)->cls == OC_ARRAY) len = cb_o(tg)->n1; else if (cb_isobj(tg) && cb_o(tg)->cls == OC_TYPED) len = cb_o(tg)->n2; else { cbv l = cb_get(R, tg, A_length); if (l == CB_EMPTY) return l; len = cb_to_int(R, l); }
        if (it->n1 >= len) { it->n2 = 1; *done = 1; return CB_UNDEF; }
        u32 i = it->n1++; if (it->fk == 0) return cb_num(i);
        cbv v; cb_obj *a = cb_isobj(tg) ? cb_o(tg) : 0; if (a && a->cls == OC_ARRAY && i < a->n2 && a->e[i] != CB_EMPTY) v = a->e[i]; else { v = cb_get(R, tg, cb_idx_key(R, i)); if (v == CB_EMPTY) return v; }
        if (it->fk == 1) return v; cbv pr[2] = { cb_num(i), v }; return cb_list_to_array(R, 2, pr); }
    case 3: { cb_str *s = cb_flat(cb_s(tg)); if (it->n1 >= s->len) { it->n2 = 1; *done = 1; return CB_UNDEF; } u32 i = it->n1; u32 c = s->c[i]; u32 n = 1; if (c >= 0xD800 && c < 0xDC00 && i + 1 < s->len && s->c[i + 1] >= 0xDC00 && s->c[i + 1] < 0xE000) n = 2; it->n1 += n; return cb_strv(cb_substr(s, i, i + n)); }
    default: { struct cb_map *m = cb_o(tg)->p1; while (m && it->n1 < m->n && m->k[it->n1] == CB_EMPTY) it->n1++; if (!m || it->n1 >= m->n) { it->n2 = 1; *done = 1; return CB_UNDEF; }
        u32 i = it->n1++; switch (it->fk) { case 5: case 7: return m->k[i]; case 6: return m->v[i]; default: { cbv pr[2] = { m->k[i], it->fk == 8 ? m->k[i] : m->v[i] }; return cb_list_to_array(R, 2, pr); } } } } }
NF(Iter_next) { if (!cb_isobj(t) || cb_o(t)->cls != OC_ITER) return cb_throw(R, 1, "next method called on incompatible receiver"); int done; cbv v = iter_step(R, cb_o(t), &done); TRY(v); return iter_res(R, v, done); }
NF(Iter_self) { return t; }
cbv cb_array_iter_next_fast(cb_rt *R, cbv it, int *done) {
    *done = 0; if (!cb_isobj(it)) return CB_EMPTY; cb_obj *o = cb_o(it); if (o->cls != OC_ITER || o->np) return CB_EMPTY;
    cb_obj *p = o->proto; if (p != R->ArrayIterP && p != StrIterP && p != MapIterP && p != SetIterP) return CB_EMPTY;
    cb_prop *nx = cb_own(p, A_next); if (!nx || !cb_isobj(nx->val) || cb_o(nx->val)->p1 != (void *)Iter_next) return CB_EMPTY;
    cbv v = iter_step(R, o, done); if (v == CB_EMPTY) { *done = 2; return CB_EMPTY; } return *done ? CB_UNDEF : v; }
/* ayudas de iteradores (Iterator.prototype.map/filter/take/drop/toArray/forEach/some/every/find/reduce) */
NF(IP_toArray) { cb_obj *a = cb_new_array(R, 0); cbv x; int r; while ((r = cb_iter_next(R, t, &x)) > 0) cb_arr_push(R, a, x); return r < 0 ? CB_EMPTY : cb_objv(a); }
NF(IP_forEach) { cbv x; int r, i = 0; while ((r = cb_iter_next(R, t, &x)) > 0) { cbv a[2] = { x, cb_num(i++) }; if (cb_call_v(R, ARG(0), CB_UNDEF, 2, a) == CB_EMPTY) return CB_EMPTY; } return r < 0 ? CB_EMPTY : CB_UNDEF; }
static cbv ip_pred(cb_rt *R, cbv t, cbv f, int kind) { cbv x; int r, i = 0; while ((r = cb_iter_next(R, t, &x)) > 0) { cbv a[2] = { x, cb_num(i++) }; cbv v = cb_call_v(R, f, CB_UNDEF, 2, a); TRY(v); int b = cb_truthy(v); if (kind == 0 && b) { cb_iter_close(R, t); return CB_TRUE; } if (kind == 1 && !b) { cb_iter_close(R, t); return CB_FALSE; } if (kind == 2 && b) { cb_iter_close(R, t); return x; } } if (r < 0) return CB_EMPTY; return kind == 0 ? CB_FALSE : kind == 1 ? CB_TRUE : CB_UNDEF; }
NF(IP_some) { return ip_pred(R, t, ARG(0), 0); } NF(IP_every) { return ip_pred(R, t, ARG(0), 1); } NF(IP_find) { return ip_pred(R, t, ARG(0), 2); }
NF(IP_reduce) { cbv x, acc = ARG(1); int r, i = 0, first = argc < 2; while ((r = cb_iter_next(R, t, &x)) > 0) { if (first) { acc = x; first = 0; i++; continue; } cbv a[3] = { acc, x, cb_num(i++) }; acc = cb_call_v(R, ARG(0), CB_UNDEF, 3, a); TRY(acc); } if (r < 0) return CB_EMPTY; if (first) return cb_throw(R, 1, "Reduce of empty iterator with no initial value"); return acc; }
/* map/filter/take/drop perezosos: se hacen con un array intermedio (sencillo) */
static cbv ip_lazy(cb_rt *R, cbv t, int kind, cbv f) {
    cb_obj *a = cb_new_array(R, 0); cbv x; int r; double i = 0, lim = kind >= 2 ? cb_to_int(R, f) : 0;
    while ((r = kind == 2 && i >= lim ? 0 : cb_iter_next(R, t, &x)) > 0) { cbv ar[2] = { x, cb_num(i) };
        if (kind == 0) { cbv v = cb_call_v(R, f, CB_UNDEF, 2, ar); TRY(v); cb_arr_push(R, a, v); } else if (kind == 1) { cbv v = cb_call_v(R, f, CB_UNDEF, 2, ar); TRY(v); if (cb_truthy(v)) cb_arr_push(R, a, x); } else if (kind == 2) cb_arr_push(R, a, x); else if (i >= lim) cb_arr_push(R, a, x); i++; }
    if (r < 0) return CB_EMPTY; return cb_array_iter(R, cb_objv(a), 1); }
NF(IP_map) { return ip_lazy(R, t, 0, ARG(0)); } NF(IP_filter) { return ip_lazy(R, t, 1, ARG(0)); } NF(IP_take) { return ip_lazy(R, t, 2, ARG(0)); } NF(IP_drop) { return ip_lazy(R, t, 3, ARG(0)); }
NF(MP_entries) { if (clsof(t) != OC_MAP) return cb_throw(R, 1, "not a Map"); return mkiter(R, t, 4, MapIterP); }
NF(MP_keys) { if (clsof(t) != OC_MAP) return cb_throw(R, 1, "not a Map"); return mkiter(R, t, 5, MapIterP); }
NF(MP_values) { if (clsof(t) != OC_MAP) return cb_throw(R, 1, "not a Map"); return mkiter(R, t, 6, MapIterP); }
NF(SP_values) { if (clsof(t) != OC_SET) return cb_throw(R, 1, "not a Set"); return mkiter(R, t, 7, SetIterP); }
NF(SP_entries) { if (clsof(t) != OC_SET) return cb_throw(R, 1, "not a Set"); return mkiter(R, t, 8, SetIterP); }
/* generadores */
NF(Gen_next) { if (clsof(t) != OC_GEN) return cb_throw(R, 1, "next method called on incompatible receiver"); return cb_gen_resume(R, cb_o(t), 0, ARG(0)); }
NF(Gen_return) { if (clsof(t) != OC_GEN) return cb_throw(R, 1, "return method called on incompatible receiver"); return cb_gen_resume(R, cb_o(t), 2, ARG(0)); }
NF(Gen_throw) { if (clsof(t) != OC_GEN) return cb_throw(R, 1, "throw method called on incompatible receiver"); return cb_gen_resume(R, cb_o(t), 1, ARG(0)); }
NF(AGen_next) { if (clsof(t) != OC_GEN) return cb_throw(R, 1, "incompatible receiver"); return cb_asyncgen_resume(R, cb_o(t), 0, ARG(0)); }
NF(AGen_return) { if (clsof(t) != OC_GEN) return cb_throw(R, 1, "incompatible receiver"); return cb_asyncgen_resume(R, cb_o(t), 2, ARG(0)); }
NF(AGen_throw) { if (clsof(t) != OC_GEN) return cb_throw(R, 1, "incompatible receiver"); return cb_asyncgen_resume(R, cb_o(t), 1, ARG(0)); }

/* ---------------- Promise ---------------- */
/* OC_PROMISE: n1 estado (0 pendiente, 1 cumplida, 2 rechazada); v1 valor; v2 lista de reacciones [ok, err, resolve, reject]...; n2 manejada */
static cbv unhandled;
static void promise_settle(cb_rt *R, cb_obj *p, int state, cbv v);
NF(job_reaction);
static void trigger(cb_rt *R, cb_obj *p) {
    if (!cb_isobj(p->v2)) return; cb_obj *L = cb_o(p->v2);
    for (u32 i = 0; i + 3 < L->n1; i += 4) { cbv r[4] = { L->e[i], L->e[i + 1], L->e[i + 2], L->e[i + 3] }; cbv tup = cb_list_to_array(R, 4, r); cb_enqueue(R, cb_new_native(R, "", job_reaction, 2), tup, cb_objv(p)); }
    p->v2 = CB_UNDEF; }
static void promise_settle(cb_rt *R, cb_obj *p, int state, cbv v) {
    if (p->n1) return; p->n1 = (u32)state; p->v1 = v;
    if (state == 2 && !p->n2) { if (!cb_isobj(unhandled)) { unhandled = cb_objv(cb_new_array(R, 0)); cb_root(&unhandled); } cb_arr_push(R, cb_o(unhandled), cb_objv(p)); }
    trigger(R, p); }
NF(job_reaction) {   /* argv[0] = [ok, err, resolve, reject], argv[1] = promesa ya resuelta */
    cb_obj *tup = cb_o(argv[0]); cb_obj *p = cb_o(argv[1]); cbv h = p->n1 == 1 ? tup->e[0] : tup->e[1];
    cbv res = tup->e[2], rej = tup->e[3];
    if (!cb_isfunc(h)) { cbv f = p->n1 == 1 ? res : rej; if (cb_isfunc(f)) cb_call_v(R, f, CB_UNDEF, 1, &p->v1); return CB_UNDEF; }
    cbv r = cb_call_v(R, h, CB_UNDEF, 1, &p->v1);
    if (r == CB_EMPTY) { cbv e = cb_take_exc(R); if (cb_isfunc(rej)) cb_call_v(R, rej, CB_UNDEF, 1, &e); return CB_UNDEF; }
    if (cb_isfunc(res)) cb_call_v(R, res, CB_UNDEF, 1, &r); return CB_UNDEF; }
static void mark_handled(cb_rt *R, cb_obj *p) { if (p->n2) return; p->n2 = 1; if (p->n1 == 2 && cb_isobj(unhandled)) { cb_obj *u = cb_o(unhandled); for (u32 i = 0; i < u->n1; i++) if (u->e[i] == cb_objv(p)) { u->e[i] = u->e[u->n1 - 1]; u->n1--; break; } } }
static void add_reaction(cb_rt *R, cb_obj *p, cbv ok, cbv err, cbv res, cbv rej) {
    mark_handled(R, p);
    if (p->n1) { cbv r[4] = { ok, err, res, rej }; cb_enqueue(R, cb_new_native(R, "", job_reaction, 2), cb_list_to_array(R, 4, r), cb_objv(p)); return; }
    if (!cb_isobj(p->v2)) p->v2 = cb_objv(cb_new_array(R, 0)); cb_obj *L = cb_o(p->v2); cb_arr_push(R, L, ok); cb_arr_push(R, L, err); cb_arr_push(R, L, res); cb_arr_push(R, L, rej); }
NF(job_thenable) {   /* argv[0] = [promesa, thenable, then] */
    cb_obj *a = cb_o(argv[0]); cbv res, rej; extern void cb_resolvers(cb_rt *R, cb_obj *p, cbv *res, cbv *rej);
    cb_resolvers(R, cb_o(a->e[0]), &res, &rej); cbv args[2] = { res, rej };
    if (cb_call_v(R, a->e[2], a->e[1], 2, args) == CB_EMPTY) { cbv e = cb_take_exc(R); cb_call_v(R, rej, CB_UNDEF, 1, &e); }
    return CB_UNDEF; }
/* funciones resolve/reject: p2 = promesa, v1 = objeto compartido "ya resuelta" */
NF(res_fn) {
    cb_obj *flag = cb_o(fn->v1); if (flag->n1) return CB_UNDEF; flag->n1 = 1;
    cb_obj *p = fn->p2; cbv v = ARG(0);
    if (cb_isobj(v) && cb_o(v) == p) { promise_settle(R, p, 2, cb_err_obj(R, 1, "Chaining cycle detected for promise")); return CB_UNDEF; }
    if (!cb_isobj(v)) { promise_settle(R, p, 1, v); return CB_UNDEF; }
    cbv then = cb_get(R, v, A_then); if (then == CB_EMPTY) { cbv e = cb_take_exc(R); promise_settle(R, p, 2, e); return CB_UNDEF; }
    if (!cb_isfunc(then)) { promise_settle(R, p, 1, v); return CB_UNDEF; }
    cbv a[3] = { cb_objv(p), v, then }; cb_enqueue(R, cb_new_native(R, "", job_thenable, 1), cb_list_to_array(R, 3, a), CB_UNDEF); return CB_UNDEF; }
NF(rej_fn) { cb_obj *flag = cb_o(fn->v1); if (flag->n1) return CB_UNDEF; flag->n1 = 1; promise_settle(R, fn->p2, 2, ARG(0)); return CB_UNDEF; }
void cb_resolvers(cb_rt *R, cb_obj *p, cbv *res, cbv *rej) {
    cb_obj *flag = cb_new_obj(R, 0); cbv r = cb_new_native(R, "", res_fn, 1), j = cb_new_native(R, "", rej_fn, 1);
    cb_o(r)->p2 = p; cb_o(r)->v1 = cb_objv(flag); cb_o(j)->p2 = p; cb_o(j)->v1 = cb_objv(flag); *res = r; *rej = j; }
cbv cb_promise_new(cb_rt *R, cbv *res, cbv *rej) { cb_obj *p = cb_new_obj(R, R->PromiseP); p->cls = OC_PROMISE; p->v2 = CB_UNDEF; cb_resolvers(R, p, res, rej); return cb_objv(p); }
static int is_promise(cbv v) { return cb_isobj(v) && cb_o(v)->cls == OC_PROMISE; }
cbv cb_promise_resolve(cb_rt *R, cbv v) { if (is_promise(v) && cb_o(v)->proto == R->PromiseP) return v; cbv res, rej; cbv p = cb_promise_new(R, &res, &rej); if (cb_call_v(R, res, CB_UNDEF, 1, &v) == CB_EMPTY) return CB_EMPTY; return p; }
cbv cb_promise_then(cb_rt *R, cbv p, cbv ok, cbv err) { cbv res, rej; cbv np = cb_promise_new(R, &res, &rej); add_reaction(R, cb_o(p), ok, err, res, rej); return np; }
void cb_promise_settle(cb_rt *R, cbv p, int ok, cbv v) { promise_settle(R, cb_o(p), ok ? 1 : 2, v); }
cbv cb_unhandled_take(cb_rt *R) { if (!cb_isobj(unhandled) || !cb_o(unhandled)->n1) return CB_UNDEF; cbv u = unhandled; unhandled = CB_UNDEF; return u; }
NF(Promise_ctor) {
    if (cb_isundef(R->nt)) return cb_throw(R, 1, "Promise constructor cannot be invoked without 'new'");
    if (!cb_isfunc(ARG(0))) return cb_throw(R, 1, "Promise resolver is not a function");
    cb_obj *p = newthis(R, R->PromiseP, OC_PROMISE); if (!p) return CB_EMPTY; p->v2 = CB_UNDEF; cbv res, rej; cb_resolvers(R, p, &res, &rej);
    cbv a[2] = { res, rej }; if (cb_call_v(R, ARG(0), CB_UNDEF, 2, a) == CB_EMPTY) { cbv e = cb_take_exc(R); cb_call_v(R, rej, CB_UNDEF, 1, &e); }
    return cb_objv(p); }
NF(PP_then) { if (!is_promise(t)) return cb_throw(R, 1, "Method Promise.prototype.then called on incompatible receiver"); return cb_promise_then(R, t, cb_isfunc(ARG(0)) ? ARG(0) : CB_UNDEF, cb_isfunc(ARG(1)) ? ARG(1) : CB_UNDEF); }
NF(PP_catch) { cbv a[2] = { CB_UNDEF, ARG(0) }; return cb_invoke(R, t, "then", 2, a); }
NF(passthru) { return fn->v2 == CB_TRUE ? cb_throwv(R, fn->v1) : fn->v1; }
NF(fin_wrap) {   /* fn->v1 = onFinally, fn->v2 = viene de un rechazo */
    cbv r = cb_call_v(R, fn->v1, CB_UNDEF, 0, 0); TRY(r); cbv p = cb_promise_resolve(R, r); TRY(p);
    cbv k = cb_new_native(R, "", passthru, 0); cb_o(k)->v1 = ARG(0); cb_o(k)->v2 = fn->v2; return cb_promise_then(R, p, k, CB_UNDEF); }
NF(PP_finally) { cbv f = ARG(0); if (!cb_isfunc(f)) { cbv a[2] = { f, f }; return cb_invoke(R, t, "then", 2, a); }
    cbv ok = cb_new_native(R, "", fin_wrap, 1), er = cb_new_native(R, "", fin_wrap, 1); cb_o(ok)->v1 = f; cb_o(ok)->v2 = CB_FALSE; cb_o(er)->v1 = f; cb_o(er)->v2 = CB_TRUE;
    cbv a[2] = { ok, er }; return cb_invoke(R, t, "then", 2, a); }
NF(Promise_resolve) { return cb_promise_resolve(R, ARG(0)); }
NF(Promise_reject) { cbv res, rej; cbv p = cb_promise_new(R, &res, &rej); cb_call_v(R, rej, CB_UNDEF, 1, &argv[0]); return p; }
NF(Promise_withResolvers) { cbv res, rej; cbv p = cb_promise_new(R, &res, &rej); cb_obj *o = cb_new_obj(R, R->ObjectP); cb_add_prop(R, o, cb_atom(R, "promise"), p, PF_WEC); cb_add_prop(R, o, cb_atom(R, "resolve"), res, PF_WEC); cb_add_prop(R, o, cb_atom(R, "reject"), rej, PF_WEC); return cb_objv(o); }
/* combinadores: estado comun en un objeto (v1 resultados, n1 pendientes, v2 resolve, v3 reject, fk modo) */
NF(comb_elem) {   /* fn->v1 = estado, fn->n1 = indice, fn->fk = 0 valor / 1 settled ok / 2 settled err / 3 any err */
    cb_obj *st = cb_o(fn->v1); if (fn->n2) return CB_UNDEF; fn->n2 = 1; cb_obj *res = cb_o(st->v1); cbv v = ARG(0);
    if (fn->fk == 1 || fn->fk == 2) { cb_obj *o = cb_new_obj(R, R->ObjectP); cb_add_prop(R, o, cb_atom(R, "status"), cb_atom(R, fn->fk == 1 ? "fulfilled" : "rejected"), PF_WEC); cb_add_prop(R, o, fn->fk == 1 ? A_value : cb_atom(R, "reason"), v, PF_WEC); v = cb_objv(o); }
    res->e[fn->n1] = v;
    if (--st->n1 == 0) { if (st->fk == 3) { cbv e = cb_err_obj(R, 7, "All promises were rejected"); cb_set(R, e, cb_atom(R, "errors"), st->v1); cb_call_v(R, st->v3, CB_UNDEF, 1, &e); } else cb_call_v(R, st->v2, CB_UNDEF, 1, &st->v1); }
    return CB_UNDEF; }
static cbv combinator(cb_rt *R, cbv iterable, int mode) {   /* 0 all, 1 allSettled, 2 race, 3 any */
    cbv res, rej; cbv P = cb_promise_new(R, &res, &rej);
    cb_obj *st = cb_new_obj(R, 0); st->v1 = cb_objv(cb_new_array(R, 0)); st->n1 = 1; st->v2 = res; st->v3 = rej; st->fk = (u8)mode;
    cbv it = cb_get_iter(R, iterable, 0); if (it == CB_EMPTY) { cbv e = cb_take_exc(R); cb_call_v(R, rej, CB_UNDEF, 1, &e); return P; }
    cbv x; int r; u32 i = 0;
    while ((r = cb_iter_next(R, it, &x)) > 0) {
        cbv p = cb_promise_resolve(R, x); if (p == CB_EMPTY) { cbv e = cb_take_exc(R); cb_call_v(R, rej, CB_UNDEF, 1, &e); return P; }
        if (mode == 2) { cbv a[2] = { res, rej }; cb_invoke(R, p, "then", 2, a); continue; }
        cb_arr_push(R, cb_o(st->v1), CB_UNDEF); st->n1++;
        cbv ok = res, er = rej;
        if (mode == 0 || mode == 1) { ok = cb_new_native(R, "", comb_elem, 1); cb_o(ok)->v1 = cb_objv(st); cb_o(ok)->n1 = i; cb_o(ok)->fk = mode == 1 ? 1 : 0; }
        if (mode == 1 || mode == 3) { er = cb_new_native(R, "", comb_elem, 1); cb_o(er)->v1 = cb_objv(st); cb_o(er)->n1 = i; cb_o(er)->fk = mode == 1 ? 2 : 3; }
        cbv a[2] = { ok, er }; if (cb_invoke(R, p, "then", 2, a) == CB_EMPTY) { cbv e = cb_take_exc(R); cb_call_v(R, rej, CB_UNDEF, 1, &e); return P; }
        i++; }
    if (r < 0) { cbv e = cb_take_exc(R); cb_call_v(R, rej, CB_UNDEF, 1, &e); return P; }
    if (mode != 2 && --st->n1 == 0) { if (mode == 3) { cbv e = cb_err_obj(R, 7, "All promises were rejected"); cb_set(R, e, cb_atom(R, "errors"), st->v1); cb_call_v(R, rej, CB_UNDEF, 1, &e); } else cb_call_v(R, res, CB_UNDEF, 1, &st->v1); }
    return P; }
NF(Promise_all) { return combinator(R, ARG(0), 0); } NF(Promise_allSettled) { return combinator(R, ARG(0), 1); } NF(Promise_race) { return combinator(R, ARG(0), 2); } NF(Promise_any) { return combinator(R, ARG(0), 3); }
NF(g_queueMicrotask) { if (!cb_isfunc(ARG(0))) return cb_throw(R, 1, "queueMicrotask: argument is not a function"); cb_enqueue(R, ARG(0), CB_UNDEF, CB_UNDEF); return CB_UNDEF; }

/* ---------------- Reflect y Proxy ---------------- */
NF(Reflect_apply) { u32 n; cbv *e; cbv r = cb_array_like(R, ARG(2), &n, &e); TRY(r); if (!cb_isfunc(ARG(0))) return cb_throw(R, 1, "Reflect.apply target is not a function"); return cb_call_v(R, ARG(0), ARG(1), (int)n, e); }
NF(Reflect_construct) { u32 n; cbv *e; cbv r = cb_array_like(R, ARG(1), &n, &e); TRY(r); cbv nt = argc > 2 ? argv[2] : ARG(0); if (!cb_is_ctor(ARG(0)) || !cb_is_ctor(nt)) return cb_throw(R, 1, "Reflect.construct: not a constructor"); return cb_construct(R, ARG(0), (int)n, e, nt); }
NF(Reflect_get) { if (!cb_isobj(ARG(0))) return cb_throw(R, 1, "Reflect.get called on non-object"); cbv k = cb_key(R, ARG(1)); TRY(k); return cb_get_proto_chain(R, cb_o(ARG(0)), k, argc > 2 ? argv[2] : ARG(0)); }
NF(Reflect_set) { if (!cb_isobj(ARG(0))) return cb_throw(R, 1, "Reflect.set called on non-object"); cbv k = cb_key(R, ARG(1)); TRY(k); int r = cb_put(R, ARG(0), k, ARG(2), argc > 3 ? argv[3] : ARG(0), 0); if (r < 0) return CB_EMPTY; return CB_TRUE; }
NF(Reflect_has) { if (!cb_isobj(ARG(0))) return cb_throw(R, 1, "Reflect.has called on non-object"); cbv k = cb_key(R, ARG(1)); TRY(k); return cb_bool(cb_has(R, ARG(0), k)); }
NF(Reflect_deleteProperty) { if (!cb_isobj(ARG(0))) return cb_throw(R, 1, "Reflect.deleteProperty called on non-object"); cbv k = cb_key(R, ARG(1)); TRY(k); cb_obj *o = cb_o(ARG(0)); cb_prop *p = cb_own(o, k); if (p && !(p->flags & PF_C)) return CB_FALSE; return cb_del(R, ARG(0), k, 0) < 0 ? CB_EMPTY : CB_TRUE; }
NF(Reflect_ownKeys) { if (!cb_isobj(ARG(0))) return cb_throw(R, 1, "Reflect.ownKeys called on non-object"); return cb_own_keys(R, ARG(0), 3); }
NF(Reflect_getPrototypeOf) { if (!cb_isobj(ARG(0))) return cb_throw(R, 1, "Reflect.getPrototypeOf called on non-object"); cb_obj *o = cb_o(ARG(0)); return o->proto ? cb_objv(o->proto) : CB_NULL; }
NF(Reflect_setPrototypeOf) { if (!cb_isobj(ARG(0))) return cb_throw(R, 1, "Reflect.setPrototypeOf called on non-object"); cbv p = ARG(1); if (!cb_isobj(p) && !cb_isnull(p)) return cb_throw(R, 1, "Object prototype may only be an Object or null"); cb_obj *o = cb_o(ARG(0)); if (!o->ext) return CB_FALSE; for (cb_obj *c = cb_isnull(p) ? 0 : cb_o(p); c; c = c->proto) if (c == o) return CB_FALSE; o->proto = cb_isnull(p) ? 0 : cb_o(p); return CB_TRUE; }
NF(Reflect_isExtensible) { if (!cb_isobj(ARG(0))) return cb_throw(R, 1, "Reflect.isExtensible called on non-object"); return cb_bool(cb_o(ARG(0))->ext); }
NF(Reflect_preventExtensions) { if (!cb_isobj(ARG(0))) return cb_throw(R, 1, "Reflect.preventExtensions called on non-object"); cb_o(ARG(0))->ext = 0; return CB_TRUE; }
NF(Reflect_defineProperty) { cbv f = cb_getp(R, cb_objv(R->global), "Object"); TRY(f); cbv dp = cb_getp(R, f, "defineProperty"); TRY(dp); cbv r = cb_call_v(R, dp, f, argc, argv); if (r == CB_EMPTY) { cb_take_exc(R); return CB_FALSE; } return CB_TRUE; }
NF(Reflect_getOwnPropertyDescriptor) { cbv f = cb_getp(R, cb_objv(R->global), "Object"); TRY(f); cbv g = cb_getp(R, f, "getOwnPropertyDescriptor"); TRY(g); return cb_call_v(R, g, f, argc, argv); }
NF(Proxy_ctor) { if (cb_isundef(R->nt)) return cb_throw(R, 1, "Constructor Proxy requires 'new'"); if (!cb_isobj(ARG(0)) || !cb_isobj(ARG(1))) return cb_throw(R, 1, "Cannot create proxy with a non-object as target or handler");
    cb_obj *p = cb_new_obj(R, cb_o(ARG(0))->proto); p->cls = OC_PROXY; p->v1 = ARG(0); p->v2 = ARG(1); return cb_objv(p); }
NF(revoke_fn) { if (cb_isobj(fn->v1)) { cb_o(fn->v1)->v2 = CB_NULL; fn->v1 = CB_NULL; } return CB_UNDEF; }
NF(Proxy_revocable) { cbv nt = R->nt; R->nt = CB_TRUE; cbv p = Proxy_ctor(R, t, argc, argv, fn); R->nt = nt; TRY(p); cb_obj *o = cb_new_obj(R, R->ObjectP); cbv rv = cb_new_native(R, "", revoke_fn, 0); cb_o(rv)->v1 = p; cb_add_prop(R, o, cb_atom(R, "proxy"), p, PF_WEC); cb_add_prop(R, o, cb_atom(R, "revoke"), rv, PF_WEC); return cb_objv(o); }

/* ---------------- Date (la hora del equipo se toma como UTC) ---------------- */
static double day_from_year(double y) { return 365 * (y - 1970) + __builtin_floor((y - 1969) / 4) - __builtin_floor((y - 1901) / 100) + __builtin_floor((y - 1601) / 400); }
static int leap(double y) { long yi = (long)y; return (yi % 4 == 0 && yi % 100 != 0) || yi % 400 == 0; }
static const int MD[2][12] = { { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 }, { 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335 } };
double cb_make_time(double y, double mo, double d, double h, double mi, double s, double ms) {
    if (!__builtin_isfinite(y) || !__builtin_isfinite(mo) || !__builtin_isfinite(d) || !__builtin_isfinite(h) || !__builtin_isfinite(mi) || !__builtin_isfinite(s) || !__builtin_isfinite(ms)) return __builtin_nan("");
    y = __builtin_trunc(y); mo = __builtin_trunc(mo); double ym = y + __builtin_floor(mo / 12); double mn = mo - __builtin_floor(mo / 12) * 12;
    double day = day_from_year(ym) + MD[leap(ym)][(int)mn] + __builtin_trunc(d) - 1;
    double t = day * 86400000.0 + __builtin_trunc(h) * 3600000.0 + __builtin_trunc(mi) * 60000.0 + __builtin_trunc(s) * 1000.0 + __builtin_trunc(ms);
    if (__builtin_fabs(t) > 8.64e15) return __builtin_nan(""); return t; }
typedef struct { double y, mo, d, h, mi, s, ms, wd; } dparts;
static void split_time(double t, dparts *p) {
    double day = __builtin_floor(t / 86400000.0); double ms = t - day * 86400000.0;
    p->h = __builtin_floor(ms / 3600000.0); p->mi = __builtin_floor(ms / 60000.0) - p->h * 60; p->s = __builtin_floor(ms / 1000.0) - p->h * 3600 - p->mi * 60; p->ms = ms - __builtin_floor(ms / 1000.0) * 1000;
    p->wd = (double)(((long)day % 7 + 11) % 7);
    double y = __builtin_floor(day / 365.2425) + 1970; while (day_from_year(y) > day) y--; while (day_from_year(y + 1) <= day) y++;
    double yd = day - day_from_year(y); int L = leap(y), m = 11; while (m > 0 && MD[L][m] > yd) m--;
    p->y = y; p->mo = m; p->d = yd - MD[L][m] + 1; }
static double tclip(double t) { if (!__builtin_isfinite(t) || __builtin_fabs(t) > 8.64e15) return __builtin_nan(""); return __builtin_trunc(t) + 0.0; }
static int dig(cb_str *s, u32 *i, int n, int *out) { int v = 0; for (int k = 0; k < n; k++) { if (*i >= s->len || s->c[*i] < '0' || s->c[*i] > '9') return 0; v = v * 10 + (s->c[*i] - '0'); (*i)++; } *out = v; return 1; }
static double parse_date(cb_str *s) {
    s = cb_flat(s); u32 i = 0; int y, mo = 1, d = 1, h = 0, mi = 0, sec = 0, ms = 0, sign = 1;
    /* ISO: YYYY-MM-DDTHH:mm:ss.sssZ (y variantes) */
    if (s->len >= 4) { u32 j = 0; if (s->c[0] == '+' || s->c[0] == '-') { sign = s->c[0] == '-' ? -1 : 1; j = 1; } int ok = j ? dig(s, &(u32){ j }, 6, &y) : 0; if (j) { i = 7; } else ok = dig(s, &i, 4, &y);
        if (ok) { y *= sign; int dateonly = 1, hastz = 0; double tz = 0;
            if (i < s->len && s->c[i] == '-') { i++; if (!dig(s, &i, 2, &mo)) return __builtin_nan(""); if (i < s->len && s->c[i] == '-') { i++; if (!dig(s, &i, 2, &d)) return __builtin_nan(""); } }
            if (i < s->len && (s->c[i] == 'T' || s->c[i] == 't' || s->c[i] == ' ')) { dateonly = 0; i++; if (!dig(s, &i, 2, &h) || i >= s->len || s->c[i] != ':') return __builtin_nan(""); i++; if (!dig(s, &i, 2, &mi)) return __builtin_nan("");
                if (i < s->len && s->c[i] == ':') { i++; if (!dig(s, &i, 2, &sec)) return __builtin_nan(""); if (i < s->len && (s->c[i] == '.' || s->c[i] == ',')) { i++; int k = 0, v = 0; while (i < s->len && s->c[i] >= '0' && s->c[i] <= '9') { if (k < 3) { v = v * 10 + (s->c[i] - '0'); k++; } i++; } while (k < 3) { v *= 10; k++; } ms = v; } } }
            if (i < s->len && (s->c[i] == 'Z' || s->c[i] == 'z')) { hastz = 1; i++; }
            else if (i < s->len && (s->c[i] == '+' || s->c[i] == '-')) { int sg = s->c[i] == '-' ? -1 : 1, th, tm = 0; i++; if (!dig(s, &i, 2, &th)) return __builtin_nan(""); if (i < s->len && s->c[i] == ':') i++; dig(s, &i, 2, &tm); tz = sg * (th * 60 + tm); hastz = 1; }
            if (i == s->len && mo >= 1 && mo <= 12 && d >= 1 && d <= 31 && h <= 24 && mi < 60 && sec < 60) { (void)dateonly; (void)hastz; return cb_make_time(y, mo - 1, d, h, mi, sec, ms) - tz * 60000; } } }
    /* formatos de texto: "Mon Nov 23 2026 18:30:15 GMT+0000", "Nov 23, 2026", "23 Nov 2026 10:00:00 GMT", "11/23/2026" */
    static const char *MON[] = { "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec" };
    int num[6], nn = 0, month = -1; double tz = 0; int pm = -1; i = 0;
    while (i < s->len) { u16 c = s->c[i];
        if ((c | 32) >= 'a' && (c | 32) <= 'z') { char w[8]; int k = 0; while (i < s->len && ((s->c[i] | 32) >= 'a' && (s->c[i] | 32) <= 'z')) { if (k < 7) w[k++] = (char)(s->c[i] | 32); i++; } w[k] = 0;
            for (int m = 0; m < 12; m++) if (k >= 3 && !memcmp(w, MON[m], 3)) month = m; if (!strcmp(w, "pm")) pm = 1; if (!strcmp(w, "am")) pm = 0;
            if ((!strcmp(w, "gmt") || !strcmp(w, "utc") || !strcmp(w, "z")) && i < s->len && (s->c[i] == '+' || s->c[i] == '-')) { int sg = s->c[i] == '-' ? -1 : 1; i++; int v = 0, k2 = 0; while (i < s->len && s->c[i] >= '0' && s->c[i] <= '9' && k2 < 4) { v = v * 10 + s->c[i] - '0'; i++; k2++; } tz = sg * ((v / 100) * 60 + v % 100); }
            continue; }
        if (c >= '0' && c <= '9') { int v = 0; while (i < s->len && s->c[i] >= '0' && s->c[i] <= '9') { v = v * 10 + (s->c[i] - '0'); i++; } if (nn < 6) num[nn++] = v; continue; }
        if (c == '(') { while (i < s->len && s->c[i] != ')') i++; } i++; }
    if (month >= 0 && nn >= 2) { int dd, yy, k = 0; if (num[0] > 31) { yy = num[0]; dd = num[1]; } else { dd = num[0]; yy = num[1]; } k = 2; int hh = k < nn ? num[k++] : 0, mm = k < nn ? num[k++] : 0, ss = k < nn ? num[k++] : 0; if (pm == 1 && hh < 12) hh += 12; if (pm == 0 && hh == 12) hh = 0; return cb_make_time(yy, month, dd, hh, mm, ss, 0) - tz * 60000; }
    if (month < 0 && nn >= 3) { /* M/D/YYYY */ int hh = nn > 3 ? num[3] : 0, mm = nn > 4 ? num[4] : 0, ss = nn > 5 ? num[5] : 0; if (pm == 1 && hh < 12) hh += 12; return cb_make_time(num[2], num[0] - 1, num[1], hh, mm, ss, 0); }
    return __builtin_nan(""); }
static double thistime(cb_rt *R, cbv t, int *ok) { *ok = cb_isobj(t) && cb_o(t)->cls == OC_DATE; if (!*ok) { cb_throw(R, 1, "this is not a Date object."); return 0; } return cb_o(t)->d; }
static const char *WD[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" }, *MN[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
static cbv date_string(cb_rt *R, double tv, int kind) {
    if (tv != tv) return cb_atom(R, "Invalid Date"); dparts p; split_time(tv, &p); char b[120];
    switch (kind) {
    case 0: cb_snprintf(b, 120, "%s %s %02d %04d %02d:%02d:%02d GMT+0000 (Coordinated Universal Time)", WD[(int)p.wd], MN[(int)p.mo], (int)p.d, (int)p.y, (int)p.h, (int)p.mi, (int)p.s); break;
    case 1: cb_snprintf(b, 120, "%s %s %02d %04d", WD[(int)p.wd], MN[(int)p.mo], (int)p.d, (int)p.y); break;
    case 2: cb_snprintf(b, 120, "%02d:%02d:%02d GMT+0000 (Coordinated Universal Time)", (int)p.h, (int)p.mi, (int)p.s); break;
    case 3: cb_snprintf(b, 120, "%s, %02d %s %04d %02d:%02d:%02d GMT", WD[(int)p.wd], (int)p.d, MN[(int)p.mo], (int)p.y, (int)p.h, (int)p.mi, (int)p.s); break;
    case 4: if (p.y >= 0 && p.y <= 9999) cb_snprintf(b, 120, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", (int)p.y, (int)p.mo + 1, (int)p.d, (int)p.h, (int)p.mi, (int)p.s, (int)p.ms); else cb_snprintf(b, 120, "%c%06d-%02d-%02dT%02d:%02d:%02d.%03dZ", p.y < 0 ? '-' : '+', (int)__builtin_fabs(p.y), (int)p.mo + 1, (int)p.d, (int)p.h, (int)p.mi, (int)p.s, (int)p.ms); break;
    case 5: cb_snprintf(b, 120, "%d/%d/%d, %d:%02d:%02d %s", (int)p.mo + 1, (int)p.d, (int)p.y, ((int)p.h + 11) % 12 + 1, (int)p.mi, (int)p.s, p.h < 12 ? "AM" : "PM"); break;
    case 6: cb_snprintf(b, 120, "%d/%d/%d", (int)p.mo + 1, (int)p.d, (int)p.y); break;
    case 7: cb_snprintf(b, 120, "%d:%02d:%02d %s", ((int)p.h + 11) % 12 + 1, (int)p.mi, (int)p.s, p.h < 12 ? "AM" : "PM"); break; }
    return cb_mkstr(R, b); }
NF(Date_ctor) {
    if (cb_isundef(R->nt)) return date_string(R, cb_date_now(), 0);
    double tv;
    if (argc == 0) tv = cb_date_now();
    else if (argc == 1) { cbv v = argv[0]; if (cb_isobj(v) && cb_o(v)->cls == OC_DATE) tv = cb_o(v)->d; else { v = cb_to_prim(R, v, 0); TRY(v); if (cb_isstr(v)) tv = parse_date(cb_s(v)); else { tv = cb_to_num(R, v); if (R->has_exc) return CB_EMPTY; } } }
    else { double f[7] = { 0, 0, 1, 0, 0, 0, 0 }; for (int i = 0; i < 7 && i < argc; i++) { f[i] = cb_to_num(R, argv[i]); if (R->has_exc) return CB_EMPTY; } if (f[0] == f[0] && f[0] >= 0 && f[0] <= 99 && __builtin_trunc(f[0]) == f[0]) f[0] += 1900; tv = cb_make_time(f[0], f[1], f[2], f[3], f[4], f[5], f[6]); }
    cb_obj *o = newthis(R, R->DateP, OC_DATE); if (!o) return CB_EMPTY; o->d = tclip(tv); return cb_objv(o); }
NF(Date_now) { return cb_num(__builtin_floor(cb_date_now())); }
NF(Date_parse) { cbv s = cb_to_str(R, ARG(0)); TRY(s); return cb_num(parse_date(cb_s(s))); }
NF(Date_UTC) { double f[7] = { 0, 0, 1, 0, 0, 0, 0 }; for (int i = 0; i < 7 && i < argc; i++) { f[i] = cb_to_num(R, argv[i]); if (R->has_exc) return CB_EMPTY; } if (f[0] == f[0] && f[0] >= 0 && f[0] <= 99 && __builtin_trunc(f[0]) == f[0]) f[0] += 1900; return cb_num(tclip(cb_make_time(f[0], f[1], f[2], f[3], f[4], f[5], f[6]))); }
static cbv dget(cb_rt *R, cbv t, int which) { int ok; double tv = thistime(R, t, &ok); if (!ok) return CB_EMPTY; if (tv != tv) return cb_num(tv); if (which == 8) return cb_num(tv); if (which == 9) return cb_num(0); dparts p; split_time(tv, &p);
    double v[] = { p.y, p.mo, p.d, p.h, p.mi, p.s, p.ms, p.wd }; return cb_num(v[which]); }
NF(DP_getFullYear) { return dget(R, t, 0); } NF(DP_getMonth) { return dget(R, t, 1); } NF(DP_getDate) { return dget(R, t, 2); } NF(DP_getHours) { return dget(R, t, 3); } NF(DP_getMinutes) { return dget(R, t, 4); }
NF(DP_getSeconds) { return dget(R, t, 5); } NF(DP_getMilliseconds) { return dget(R, t, 6); } NF(DP_getDay) { return dget(R, t, 7); } NF(DP_getTime) { return dget(R, t, 8); } NF(DP_getTimezoneOffset) { return dget(R, t, 9); }
NF(DP_getYear) { cbv y = dget(R, t, 0); TRY(y); return cb_num(cb_d(y) - 1900); }
static cbv dset(cb_rt *R, cbv t, int argc, cbv *argv, int first, int n) {
    int ok; double tv = thistime(R, t, &ok); if (!ok) return CB_EMPTY; dparts p; if (tv != tv) { if (first != 0) return cb_num(tv); tv = 0; } split_time(tv, &p);
    double f[7] = { p.y, p.mo, p.d, p.h, p.mi, p.s, p.ms };
    for (int i = 0; i < n && i < argc; i++) { f[first + i] = cb_to_num(R, argv[i]); if (R->has_exc) return CB_EMPTY; }
    if (!argc) f[first] = __builtin_nan("");
    double nt = tclip(cb_make_time(f[0], f[1], f[2], f[3], f[4], f[5], f[6])); cb_o(t)->d = nt; return cb_num(nt); }
NF(DP_setFullYear) { return dset(R, t, argc, argv, 0, 3); } NF(DP_setMonth) { return dset(R, t, argc, argv, 1, 2); } NF(DP_setDate) { return dset(R, t, argc, argv, 2, 1); } NF(DP_setHours) { return dset(R, t, argc, argv, 3, 4); }
NF(DP_setMinutes) { return dset(R, t, argc, argv, 4, 3); } NF(DP_setSeconds) { return dset(R, t, argc, argv, 5, 2); } NF(DP_setMilliseconds) { return dset(R, t, argc, argv, 6, 1); }
NF(DP_setTime) { int ok; thistime(R, t, &ok); if (!ok) return CB_EMPTY; double v = cb_to_num(R, ARG(0)); if (R->has_exc) return CB_EMPTY; cb_o(t)->d = tclip(v); return cb_num(cb_o(t)->d); }
NF(DP_toISOString) { int ok; double tv = thistime(R, t, &ok); if (!ok) return CB_EMPTY; if (tv != tv) return cb_throw(R, 2, "Invalid time value"); return date_string(R, tv, 4); }
NF(DP_toJSON) { cbv p = cb_to_prim(R, t, 1); TRY(p); if (cb_isnum(p) && !__builtin_isfinite(cb_d(p))) return CB_NULL; return cb_invoke(R, t, "toISOString", 0, 0); }
NF(DP_toString) { int ok; double tv = thistime(R, t, &ok); if (!ok) return CB_EMPTY; return date_string(R, tv, 0); }
NF(DP_toDateString) { int ok; double tv = thistime(R, t, &ok); if (!ok) return CB_EMPTY; return date_string(R, tv, 1); }
NF(DP_toTimeString) { int ok; double tv = thistime(R, t, &ok); if (!ok) return CB_EMPTY; return date_string(R, tv, 2); }
NF(DP_toUTCString) { int ok; double tv = thistime(R, t, &ok); if (!ok) return CB_EMPTY; return date_string(R, tv, 3); }
NF(DP_toLocaleString) { int ok; double tv = thistime(R, t, &ok); if (!ok) return CB_EMPTY; return date_string(R, tv, 5); }
NF(DP_toLocaleDateString) { int ok; double tv = thistime(R, t, &ok); if (!ok) return CB_EMPTY; return date_string(R, tv, 6); }
NF(DP_toLocaleTimeString) { int ok; double tv = thistime(R, t, &ok); if (!ok) return CB_EMPTY; return date_string(R, tv, 7); }
NF(DP_valueOf) { return dget(R, t, 8); }
NF(DP_toPrimitive) { cbv h = ARG(0); int str = !(cb_isstr(h) && cb_str_eqa(cb_s(h), "number")); if (!cb_isobj(t)) return cb_throw(R, 1, "Date.prototype[Symbol.toPrimitive] called on non-object");
    cbv order[2] = { str ? A_toString : A_valueOf, str ? A_valueOf : A_toString }; for (int i = 0; i < 2; i++) { cbv f = cb_get(R, t, order[i]); TRY(f); if (cb_isfunc(f)) { cbv r = cb_call_v(R, f, t, 0, 0); TRY(r); if (!cb_isobj(r)) return r; } } return cb_throw(R, 1, "Cannot convert object to primitive value"); }

/* ---------------- ArrayBuffer, matrices tipadas, DataView ---------------- */
enum { TA_I8, TA_U8, TA_U8C, TA_I16, TA_U16, TA_I32, TA_U32, TA_F32, TA_F64, TA_N };
static const char *TAN[] = { "Int8Array", "Uint8Array", "Uint8ClampedArray", "Int16Array", "Uint16Array", "Int32Array", "Uint32Array", "Float32Array", "Float64Array" };
static const int TAS[] = { 1, 1, 1, 2, 2, 4, 4, 4, 8 };
static cb_obj *new_buffer(cb_rt *R, double len, cb_obj *proto) { if (len < 0 || len > 1e9) { cb_throw(R, 2, "Array buffer allocation failed"); return 0; } cb_obj *b = cb_new_obj(R, proto ? proto : R->ABufP); b->cls = OC_ABUF; b->n1 = (u32)len; b->p1 = cb_raw((u64)len + 8); memset(b->p1, 0, (u64)len + 8); return b; }
NF(ArrayBuffer_ctor) { if (cb_isundef(R->nt)) return cb_throw(R, 1, "Constructor ArrayBuffer requires 'new'"); double n = cb_to_int(R, ARG(0)); if (R->has_exc) return CB_EMPTY; cbv o = cb_ctor_this(R, R->nt, R->ABufP); TRY(o); cb_obj *b = new_buffer(R, n, cb_o(o)->proto); return b ? cb_objv(b) : CB_EMPTY; }
NF(ArrayBuffer_isView) { int c = clsof(ARG(0)); return cb_bool(c == OC_TYPED || c == OC_DVIEW); }
NF(ABP_byteLength) { if (clsof(t) != OC_ABUF) return cb_throw(R, 1, "not an ArrayBuffer"); return cb_num(cb_o(t)->n1); }
static double relidx(double r, double len) { if (r < 0) { r += len; return r < 0 ? 0 : r; } return r > len ? len : r; }
NF(ABP_slice) { if (clsof(t) != OC_ABUF) return cb_throw(R, 1, "not an ArrayBuffer"); cb_obj *b = cb_o(t); double n = b->n1, a = relidx(cb_to_int(R, ARG(0)), n), e = cb_isundef(ARG(1)) ? n : relidx(cb_to_int(R, ARG(1)), n); double len = e > a ? e - a : 0;
    cb_obj *nb = new_buffer(R, len, 0); if (!nb) return CB_EMPTY; memcpy(nb->p1, (u8 *)b->p1 + (u32)a, (u32)len); return cb_objv(nb); }
static u8 *ta_ptr(cb_obj *t) { cb_obj *b = cb_o(t->v1); return (u8 *)b->p1 + t->n1; }
cbv cb_typed_get(cb_rt *R, cb_obj *t, u32 i) {
    if (i >= t->n2) return CB_UNDEF; u8 *p = ta_ptr(t) + (u64)i * TAS[t->fk];
    switch (t->fk) { case TA_I8: return cb_num(*(int8_t *)p); case TA_U8: case TA_U8C: return cb_num(*p); case TA_I16: { int16_t v; memcpy(&v, p, 2); return cb_num(v); } case TA_U16: { u16 v; memcpy(&v, p, 2); return cb_num(v); }
        case TA_I32: { int32_t v; memcpy(&v, p, 4); return cb_num(v); } case TA_U32: { u32 v; memcpy(&v, p, 4); return cb_num(v); } case TA_F32: { float v; memcpy(&v, p, 4); return cb_num(v); } default: { double v; memcpy(&v, p, 8); return cb_num(v); } } }
static void ta_store(int k, u8 *p, double d) {
    switch (k) { case TA_I8: case TA_U8: { u8 v = (u8)cb_to_i32(d); *p = v; break; } case TA_U8C: { double c = d != d ? 0 : d < 0 ? 0 : d > 255 ? 255 : d; double f = __builtin_floor(c); double r = c - f; u8 v = (u8)(r > 0.5 ? f + 1 : r < 0.5 ? f : ((long)f & 1 ? f + 1 : f)); *p = v; break; }
        case TA_I16: case TA_U16: { u16 v = (u16)cb_to_i32(d); memcpy(p, &v, 2); break; } case TA_I32: case TA_U32: { u32 v = cb_to_u32(d); memcpy(p, &v, 4); break; }
        case TA_F32: { float v = (float)d; memcpy(p, &v, 4); break; } default: memcpy(p, &d, 8); } }
int cb_typed_set(cb_rt *R, cb_obj *t, u32 i, cbv v) { double d = cb_to_num(R, v); if (R->has_exc) return -1; if (i < t->n2) ta_store(t->fk, ta_ptr(t) + (u64)i * TAS[t->fk], d); return 0; }
static cbv make_ta(cb_rt *R, int k, cbv nt, int argc, cbv *argv) {
    cbv o = cb_ctor_this(R, nt, R->TypedPs[k]); TRY(o); cb_obj *t = cb_o(o); t->cls = OC_TYPED; t->fk = (u8)k; cbv a0 = ARG(0);
    if (!cb_isobj(a0)) { double n = cb_to_int(R, a0); if (R->has_exc) return CB_EMPTY; cb_obj *b = new_buffer(R, n * TAS[k], 0); if (!b) return CB_EMPTY; t->v1 = cb_objv(b); t->n1 = 0; t->n2 = (u32)n; return o; }
    cb_obj *src = cb_o(a0);
    if (src->cls == OC_ABUF) { double off = cb_to_int(R, ARG(1)); if ((u32)off % TAS[k]) return cb_throw(R, 2, "start offset of %s should be a multiple of %d", TAN[k], TAS[k]); double len = cb_isundef(ARG(2)) ? (src->n1 - off) / TAS[k] : cb_to_int(R, ARG(2)); if (off < 0 || off + len * TAS[k] > src->n1) return cb_throw(R, 2, "Invalid typed array length"); t->v1 = a0; t->n1 = (u32)off; t->n2 = (u32)len; return o; }
    /* desde otra matriz tipada, iterable o array-like */
    cb_obj *list = 0; u32 n;
    if (src->cls == OC_TYPED) { n = src->n2; cb_obj *b = new_buffer(R, (double)n * TAS[k], 0); if (!b) return CB_EMPTY; t->v1 = cb_objv(b); t->n2 = n; for (u32 i = 0; i < n; i++) ta_store(k, ta_ptr(t) + (u64)i * TAS[k], cb_d(cb_typed_get(R, src, i))); return o; }
    cbv itf = cb_get(R, a0, S_iterator); TRY(itf);
    if (cb_isfunc(itf)) { list = cb_new_array(R, 0); cbv it = cb_get_iter(R, a0, 0); TRY(it); cbv x; int r; while ((r = cb_iter_next(R, it, &x)) > 0) cb_arr_push(R, list, x); if (r < 0) return CB_EMPTY; a0 = cb_objv(list); }
    cbv l = cb_get(R, a0, A_length); TRY(l); n = (u32)cb_to_int(R, l);
    cb_obj *b = new_buffer(R, (double)n * TAS[k], 0); if (!b) return CB_EMPTY; t->v1 = cb_objv(b); t->n2 = n;
    for (u32 i = 0; i < n; i++) { cbv x = cb_get(R, a0, cb_idx_key(R, i)); TRY(x); if (cb_typed_set(R, t, i, x)) return CB_EMPTY; }
    return o; }
NF(TA_ctor) { if (cb_isundef(R->nt)) return cb_throw(R, 1, "Constructor requires 'new'"); return make_ta(R, (int)fn->n2, R->nt, argc, argv); }
static cb_obj *ta_this(cb_rt *R, cbv t) { if (clsof(t) != OC_TYPED) { cb_throw(R, 1, "this is not a typed array."); return 0; } return cb_o(t); }
NF(TP_length) { cb_obj *a = ta_this(R, t); return a ? cb_num(a->n2) : CB_EMPTY; }
NF(TP_byteLength) { cb_obj *a = ta_this(R, t); return a ? cb_num((double)a->n2 * TAS[a->fk]) : CB_EMPTY; }
NF(TP_byteOffset) { cb_obj *a = ta_this(R, t); return a ? cb_num(a->n1) : CB_EMPTY; }
NF(TP_buffer) { cb_obj *a = ta_this(R, t); return a ? a->v1 : CB_EMPTY; }
NF(TP_set) { cb_obj *a = ta_this(R, t); if (!a) return CB_EMPTY; double off = cb_to_int(R, ARG(1)); cbv src = ARG(0); cbv l = cb_get(R, src, A_length); TRY(l); double n = cb_to_int(R, l); if (off < 0 || off + n > a->n2) return cb_throw(R, 2, "offset is out of bounds");
    for (double i = 0; i < n; i++) { cbv x = cb_get(R, src, cb_idx_key(R, (u32)i)); TRY(x); if (cb_typed_set(R, a, (u32)(off + i), x)) return CB_EMPTY; } return CB_UNDEF; }
NF(TP_subarray) { cb_obj *a = ta_this(R, t); if (!a) return CB_EMPTY; double n = a->n2, b = relidx(cb_to_int(R, ARG(0)), n), e = cb_isundef(ARG(1)) ? n : relidx(cb_to_int(R, ARG(1)), n);
    cb_obj *s = cb_new_obj(R, a->proto); s->cls = OC_TYPED; s->fk = a->fk; s->v1 = a->v1; s->n1 = a->n1 + (u32)b * TAS[a->fk]; s->n2 = e > b ? (u32)(e - b) : 0; return cb_objv(s); }
NF(TP_slice) { cb_obj *a = ta_this(R, t); if (!a) return CB_EMPTY; double n = a->n2, b = relidx(cb_to_int(R, ARG(0)), n), e = cb_isundef(ARG(1)) ? n : relidx(cb_to_int(R, ARG(1)), n); u32 len = e > b ? (u32)(e - b) : 0;
    cbv ln = cb_num(len); cbv r = make_ta(R, a->fk, cb_get(R, cb_objv(a->proto), A_constructor), 1, &ln); TRY(r); memcpy(ta_ptr(cb_o(r)), ta_ptr(a) + (u64)b * TAS[a->fk], (u64)len * TAS[a->fk]); return r; }
NF(TP_fill) { cb_obj *a = ta_this(R, t); if (!a) return CB_EMPTY; double v = cb_to_num(R, ARG(0)); double n = a->n2, b = relidx(cb_to_int(R, ARG(1)), n), e = cb_isundef(ARG(2)) ? n : relidx(cb_to_int(R, ARG(2)), n); for (double i = b; i < e; i++) ta_store(a->fk, ta_ptr(a) + (u64)i * TAS[a->fk], v); return t; }
NF(TP_map) { cb_obj *a = ta_this(R, t); if (!a) return CB_EMPTY; cbv ln = cb_num(a->n2); cbv r = make_ta(R, a->fk, cb_get(R, cb_objv(a->proto), A_constructor), 1, &ln); TRY(r);
    for (u32 i = 0; i < a->n2; i++) { cbv ar[3] = { cb_typed_get(R, a, i), cb_num(i), t }; cbv v = cb_call_v(R, ARG(0), ARG(1), 3, ar); TRY(v); if (cb_typed_set(R, cb_o(r), i, v)) return CB_EMPTY; } return r; }
NF(TP_filter) { cb_obj *a = ta_this(R, t); if (!a) return CB_EMPTY; cb_obj *keep = cb_new_array(R, 0); for (u32 i = 0; i < a->n2; i++) { cbv x = cb_typed_get(R, a, i); cbv ar[3] = { x, cb_num(i), t }; cbv v = cb_call_v(R, ARG(0), ARG(1), 3, ar); TRY(v); if (cb_truthy(v)) cb_arr_push(R, keep, x); }
    cbv kv = cb_objv(keep); return make_ta(R, a->fk, cb_get(R, cb_objv(a->proto), A_constructor), 1, &kv); }
NF(TP_reverse) { cb_obj *a = ta_this(R, t); if (!a) return CB_EMPTY; int s = TAS[a->fk]; u8 *p = ta_ptr(a), tmp[8]; for (u32 i = 0, j = a->n2 ? a->n2 - 1 : 0; i < j; i++, j--) { memcpy(tmp, p + i * s, s); memcpy(p + i * s, p + j * s, s); memcpy(p + j * s, tmp, s); } return t; }
static int tacmp(const void *x, const void *y) { double a = *(const double *)x, b = *(const double *)y; if (a != a) return b != b ? 0 : 1; if (b != b) return -1; return a < b ? -1 : a > b ? 1 : 0; }
NF(TP_sort) { cb_obj *a = ta_this(R, t); if (!a) return CB_EMPTY; u32 n = a->n2; double *v = malloc((u64)n * 8 + 8); for (u32 i = 0; i < n; i++) v[i] = cb_d(cb_typed_get(R, a, i));
    if (cb_isfunc(ARG(0))) { for (u32 i = 1; i < n; i++) { double x = v[i]; u32 j = i; while (j > 0) { cbv ar[2] = { cb_num(v[j - 1]), cb_num(x) }; cbv r = cb_call_v(R, ARG(0), CB_UNDEF, 2, ar); if (r == CB_EMPTY) { free(v); return r; } if (cb_to_num(R, r) <= 0) break; v[j] = v[j - 1]; j--; } v[j] = x; } }
    else qsort(v, n, 8, tacmp);
    for (u32 i = 0; i < n; i++) ta_store(a->fk, ta_ptr(a) + (u64)i * TAS[a->fk], v[i]); free(v); return t; }
NF(TP_toStringTag) { return clsof(t) == OC_TYPED ? cb_atom(R, TAN[cb_o(t)->fk]) : CB_UNDEF; }
NF(TA_from) { cbv nt = t; cbv src = ARG(0); cbv list = src; if (cb_isfunc(ARG(1))) { cbv af = cb_getp(R, cb_objv(R->global), "Array"); cbv fr = cb_getp(R, af, "from"); cbv a[2] = { src, ARG(1) }; list = cb_call_v(R, fr, af, 2, a); TRY(list); } if (!cb_is_ctor(nt)) return cb_throw(R, 1, "not a constructor"); return cb_construct(R, nt, 1, &list, nt); }
NF(TA_of) { cbv list = cb_list_to_array(R, argc, argv); if (!cb_is_ctor(t)) return cb_throw(R, 1, "not a constructor"); return cb_construct(R, t, 1, &list, t); }
NF(DataView_ctor) { if (cb_isundef(R->nt)) return cb_throw(R, 1, "Constructor DataView requires 'new'"); if (clsof(ARG(0)) != OC_ABUF) return cb_throw(R, 1, "First argument to DataView constructor must be an ArrayBuffer");
    cb_obj *b = cb_o(ARG(0)); double off = cb_to_int(R, ARG(1)), len = cb_isundef(ARG(2)) ? b->n1 - off : cb_to_int(R, ARG(2)); if (off < 0 || off + len > b->n1) return cb_throw(R, 2, "Invalid DataView length");
    cbv o = cb_ctor_this(R, R->nt, R->DViewP); TRY(o); cb_obj *d = cb_o(o); d->cls = OC_DVIEW; d->v1 = ARG(0); d->n1 = (u32)off; d->n2 = (u32)len; return o; }
static cbv dv_access(cb_rt *R, cbv t, int argc, cbv *argv, int k, int set) {
    if (clsof(t) != OC_DVIEW) return cb_throw(R, 1, "not a DataView"); cb_obj *d = cb_o(t); double off = cb_to_int(R, ARG(0)); int s = TAS[k];
    if (off < 0 || off + s > d->n2) return cb_throw(R, 2, "Offset is outside the bounds of the DataView");
    u8 *p = (u8 *)cb_o(d->v1)->p1 + d->n1 + (u32)off; int little = cb_truthy(ARG(set ? 2 : 1)); u8 tmp[8];
    if (set) { double v = cb_to_num(R, ARG(1)); if (R->has_exc) return CB_EMPTY; ta_store(k, tmp, v); for (int i = 0; i < s; i++) p[i] = little ? tmp[i] : tmp[s - 1 - i]; return CB_UNDEF; }
    for (int i = 0; i < s; i++) tmp[i] = little ? p[i] : p[s - 1 - i];
    cb_obj fake; memset(&fake, 0, sizeof fake); cb_obj buf; memset(&buf, 0, sizeof buf); buf.p1 = tmp; fake.v1 = cb_objv(&buf); fake.fk = (u8)k; fake.n2 = 1; return cb_typed_get(R, &fake, 0); }
#define DVF(nm, k) NF(DV_get##nm) { return dv_access(R, t, argc, argv, k, 0); } NF(DV_set##nm) { return dv_access(R, t, argc, argv, k, 1); }
DVF(Int8, TA_I8) DVF(Uint8, TA_U8) DVF(Int16, TA_I16) DVF(Uint16, TA_U16) DVF(Int32, TA_I32) DVF(Uint32, TA_U32) DVF(Float32, TA_F32) DVF(Float64, TA_F64)
NF(DVP_byteLength) { return clsof(t) == OC_DVIEW ? cb_num(cb_o(t)->n2) : cb_throw(R, 1, "not a DataView"); }
NF(DVP_byteOffset) { return clsof(t) == OC_DVIEW ? cb_num(cb_o(t)->n1) : cb_throw(R, 1, "not a DataView"); }
NF(DVP_buffer) { return clsof(t) == OC_DVIEW ? cb_o(t)->v1 : cb_throw(R, 1, "not a DataView"); }
NF(g_structuredClone) { cbv s = cb_json_str(R, ARG(0)); TRY(s); if (cb_isundef(s)) return CB_UNDEF; cbv J = cb_getp(R, cb_objv(R->global), "JSON"); return cb_invoke(R, J, "parse", 1, &s); }

void cb_lib2_init(cb_rt *R) {
    cb_obj *G = R->global;
    /* iteradores */
    R->IterP = cb_new_obj(R, R->ObjectP); cb_add_prop(R, R->IterP, S_iterator, cb_new_native(R, "[Symbol.iterator]", Iter_self, 0), PF_W | PF_C);
    struct { const char *n; cb_native f; int a; } IH[] = { { "toArray", IP_toArray, 0 }, { "forEach", IP_forEach, 1 }, { "some", IP_some, 1 }, { "every", IP_every, 1 }, { "find", IP_find, 1 }, { "reduce", IP_reduce, 1 }, { "map", IP_map, 1 }, { "filter", IP_filter, 1 }, { "take", IP_take, 1 }, { "drop", IP_drop, 1 } };
    for (u32 i = 0; i < sizeof IH / sizeof *IH; i++) cb_defn(R, R->IterP, IH[i].n, IH[i].f, IH[i].a);
    { cb_obj *Ic = mkctor(R, "Iterator", Iter_self, 0, R->IterP); (void)Ic; }
    R->ArrayIterP = cb_new_obj(R, R->IterP); cb_defn(R, R->ArrayIterP, "next", Iter_next, 0); cb_add_prop(R, R->ArrayIterP, S_toStringTag, cb_atom(R, "Array Iterator"), PF_C);
    StrIterP = cb_new_obj(R, R->IterP); cb_defn(R, StrIterP, "next", Iter_next, 0); cb_add_prop(R, StrIterP, S_toStringTag, cb_atom(R, "String Iterator"), PF_C);
    MapIterP = cb_new_obj(R, R->IterP); cb_defn(R, MapIterP, "next", Iter_next, 0); cb_add_prop(R, MapIterP, S_toStringTag, cb_atom(R, "Map Iterator"), PF_C);
    SetIterP = cb_new_obj(R, R->IterP); cb_defn(R, SetIterP, "next", Iter_next, 0); cb_add_prop(R, SetIterP, S_toStringTag, cb_atom(R, "Set Iterator"), PF_C);
    { static cbv keep[4]; keep[0] = cb_objv(StrIterP); keep[1] = cb_objv(MapIterP); keep[2] = cb_objv(SetIterP); for (int i = 0; i < 3; i++) cb_root(&keep[i]); }
    R->GenP = cb_new_obj(R, R->IterP); cb_defn(R, R->GenP, "next", Gen_next, 1); cb_defn(R, R->GenP, "return", Gen_return, 1); cb_defn(R, R->GenP, "throw", Gen_throw, 1); cb_add_prop(R, R->GenP, S_toStringTag, cb_atom(R, "Generator"), PF_C);
    R->AsyncGenP = cb_new_obj(R, R->ObjectP); cb_defn(R, R->AsyncGenP, "next", AGen_next, 1); cb_defn(R, R->AsyncGenP, "return", AGen_return, 1); cb_defn(R, R->AsyncGenP, "throw", AGen_throw, 1);
    cb_add_prop(R, R->AsyncGenP, S_asyncIterator, cb_new_native(R, "[Symbol.asyncIterator]", Iter_self, 0), PF_W | PF_C);
    /* Map, Set */
    R->MapP = cb_new_obj(R, R->ObjectP); cb_obj *Mc = mkctor(R, "Map", Map_ctor, 0, R->MapP); cb_defn(R, Mc, "groupBy", Map_groupBy, 2);
    cb_defn(R, R->MapP, "get", MP_get, 1); cb_defn(R, R->MapP, "set", MP_set, 2); cb_defn(R, R->MapP, "has", MP_has, 1); cb_defn(R, R->MapP, "delete", MP_delete, 1); cb_defn(R, R->MapP, "clear", MP_clear, 0);
    cb_defn(R, R->MapP, "forEach", MP_forEach, 1); cb_defn(R, R->MapP, "keys", MP_keys, 0); cb_defn(R, R->MapP, "values", MP_values, 0); cb_defgs(R, R->MapP, "size", MP_size, 0);
    { cbv e = cb_new_native(R, "entries", MP_entries, 0); cb_add_prop(R, R->MapP, cb_atom(R, "entries"), e, PF_W | PF_C); cb_add_prop(R, R->MapP, S_iterator, e, PF_W | PF_C); } cb_add_prop(R, R->MapP, S_toStringTag, cb_atom(R, "Map"), PF_C);
    R->SetP = cb_new_obj(R, R->ObjectP); mkctor(R, "Set", Set_ctor, 0, R->SetP);
    cb_defn(R, R->SetP, "add", SP_add, 1); cb_defn(R, R->SetP, "has", MP_has, 1); cb_defn(R, R->SetP, "delete", MP_delete, 1); cb_defn(R, R->SetP, "clear", MP_clear, 0); cb_defn(R, R->SetP, "forEach", MP_forEach, 1); cb_defn(R, R->SetP, "entries", SP_entries, 0); cb_defgs(R, R->SetP, "size", MP_size, 0);
    { cbv v = cb_new_native(R, "values", SP_values, 0); cb_add_prop(R, R->SetP, cb_atom(R, "values"), v, PF_W | PF_C); cb_add_prop(R, R->SetP, cb_atom(R, "keys"), v, PF_W | PF_C); cb_add_prop(R, R->SetP, S_iterator, v, PF_W | PF_C); } cb_add_prop(R, R->SetP, S_toStringTag, cb_atom(R, "Set"), PF_C);
    R->WeakMapP = cb_new_obj(R, R->ObjectP); mkctor(R, "WeakMap", WeakMap_ctor, 0, R->WeakMapP); cb_defn(R, R->WeakMapP, "get", MP_get, 1); cb_defn(R, R->WeakMapP, "set", MP_set, 2); cb_defn(R, R->WeakMapP, "has", MP_has, 1); cb_defn(R, R->WeakMapP, "delete", MP_delete, 1);
    R->WeakSetP = cb_new_obj(R, R->ObjectP); mkctor(R, "WeakSet", WeakSet_ctor, 0, R->WeakSetP); cb_defn(R, R->WeakSetP, "add", SP_add, 1); cb_defn(R, R->WeakSetP, "has", MP_has, 1); cb_defn(R, R->WeakSetP, "delete", MP_delete, 1);
    { cb_obj *wr = cb_new_obj(R, R->ObjectP); (void)wr; }
    /* Promise */
    R->PromiseP = cb_new_obj(R, R->ObjectP); cb_obj *Pc = mkctor(R, "Promise", Promise_ctor, 1, R->PromiseP); R->Promise = Pc;
    cb_defn(R, R->PromiseP, "then", PP_then, 2); cb_defn(R, R->PromiseP, "catch", PP_catch, 1); cb_defn(R, R->PromiseP, "finally", PP_finally, 1); cb_add_prop(R, R->PromiseP, S_toStringTag, cb_atom(R, "Promise"), PF_C);
    cb_defn(R, Pc, "resolve", Promise_resolve, 1); cb_defn(R, Pc, "reject", Promise_reject, 1); cb_defn(R, Pc, "all", Promise_all, 1); cb_defn(R, Pc, "allSettled", Promise_allSettled, 1);
    cb_defn(R, Pc, "race", Promise_race, 1); cb_defn(R, Pc, "any", Promise_any, 1); cb_defn(R, Pc, "withResolvers", Promise_withResolvers, 0);
    cb_defn(R, G, "queueMicrotask", g_queueMicrotask, 1); cb_defn(R, G, "structuredClone", g_structuredClone, 1);
    /* Reflect, Proxy */
    cb_obj *Rf = cb_new_obj(R, R->ObjectP); cb_add_prop(R, G, cb_atom(R, "Reflect"), cb_objv(Rf), PF_W | PF_C); cb_add_prop(R, Rf, S_toStringTag, cb_atom(R, "Reflect"), PF_C);
    struct { const char *n; cb_native f; int a; } RM[] = { { "apply", Reflect_apply, 3 }, { "construct", Reflect_construct, 2 }, { "get", Reflect_get, 2 }, { "set", Reflect_set, 3 }, { "has", Reflect_has, 2 },
        { "deleteProperty", Reflect_deleteProperty, 2 }, { "ownKeys", Reflect_ownKeys, 1 }, { "getPrototypeOf", Reflect_getPrototypeOf, 1 }, { "setPrototypeOf", Reflect_setPrototypeOf, 2 }, { "isExtensible", Reflect_isExtensible, 1 },
        { "preventExtensions", Reflect_preventExtensions, 1 }, { "defineProperty", Reflect_defineProperty, 3 }, { "getOwnPropertyDescriptor", Reflect_getOwnPropertyDescriptor, 2 } };
    for (u32 i = 0; i < sizeof RM / sizeof *RM; i++) cb_defn(R, Rf, RM[i].n, RM[i].f, RM[i].a);
    cb_obj *Px = mkctor(R, "Proxy", Proxy_ctor, 2, 0); cb_defn(R, Px, "revocable", Proxy_revocable, 2); R->ProxyC = Px;
    /* Date */
    R->DateP = cb_new_obj(R, R->ObjectP); cb_obj *Dc = mkctor(R, "Date", Date_ctor, 7, R->DateP);
    cb_defn(R, Dc, "now", Date_now, 0); cb_defn(R, Dc, "parse", Date_parse, 1); cb_defn(R, Dc, "UTC", Date_UTC, 7);
    struct { const char *n; cb_native f; int a; } DM[] = { { "getFullYear", DP_getFullYear, 0 }, { "getMonth", DP_getMonth, 0 }, { "getDate", DP_getDate, 0 }, { "getHours", DP_getHours, 0 }, { "getMinutes", DP_getMinutes, 0 },
        { "getSeconds", DP_getSeconds, 0 }, { "getMilliseconds", DP_getMilliseconds, 0 }, { "getDay", DP_getDay, 0 }, { "getUTCFullYear", DP_getFullYear, 0 }, { "getUTCMonth", DP_getMonth, 0 }, { "getUTCDate", DP_getDate, 0 },
        { "getUTCHours", DP_getHours, 0 }, { "getUTCMinutes", DP_getMinutes, 0 }, { "getUTCSeconds", DP_getSeconds, 0 }, { "getUTCMilliseconds", DP_getMilliseconds, 0 }, { "getUTCDay", DP_getDay, 0 }, { "getYear", DP_getYear, 0 },
        { "getTime", DP_getTime, 0 }, { "valueOf", DP_valueOf, 0 }, { "getTimezoneOffset", DP_getTimezoneOffset, 0 }, { "setFullYear", DP_setFullYear, 3 }, { "setMonth", DP_setMonth, 2 }, { "setDate", DP_setDate, 1 },
        { "setHours", DP_setHours, 4 }, { "setMinutes", DP_setMinutes, 3 }, { "setSeconds", DP_setSeconds, 2 }, { "setMilliseconds", DP_setMilliseconds, 1 }, { "setUTCFullYear", DP_setFullYear, 3 }, { "setUTCMonth", DP_setMonth, 2 },
        { "setUTCDate", DP_setDate, 1 }, { "setUTCHours", DP_setHours, 4 }, { "setUTCMinutes", DP_setMinutes, 3 }, { "setUTCSeconds", DP_setSeconds, 2 }, { "setUTCMilliseconds", DP_setMilliseconds, 1 }, { "setTime", DP_setTime, 1 },
        { "toISOString", DP_toISOString, 0 }, { "toJSON", DP_toJSON, 1 }, { "toString", DP_toString, 0 }, { "toDateString", DP_toDateString, 0 }, { "toTimeString", DP_toTimeString, 0 }, { "toUTCString", DP_toUTCString, 0 },
        { "toGMTString", DP_toUTCString, 0 }, { "toLocaleString", DP_toLocaleString, 0 }, { "toLocaleDateString", DP_toLocaleDateString, 0 }, { "toLocaleTimeString", DP_toLocaleTimeString, 0 } };
    for (u32 i = 0; i < sizeof DM / sizeof *DM; i++) cb_defn(R, R->DateP, DM[i].n, DM[i].f, DM[i].a);
    cb_add_prop(R, R->DateP, S_toPrimitive, cb_new_native(R, "[Symbol.toPrimitive]", DP_toPrimitive, 1), PF_C);
    /* ArrayBuffer, matrices tipadas, DataView */
    R->ABufP = cb_new_obj(R, R->ObjectP); cb_obj *ABc = mkctor(R, "ArrayBuffer", ArrayBuffer_ctor, 1, R->ABufP); cb_defn(R, ABc, "isView", ArrayBuffer_isView, 1);
    cb_defgs(R, R->ABufP, "byteLength", ABP_byteLength, 0); cb_defn(R, R->ABufP, "slice", ABP_slice, 2); cb_add_prop(R, R->ABufP, S_toStringTag, cb_atom(R, "ArrayBuffer"), PF_C);
    R->TypedP = cb_new_obj(R, R->ObjectP); cbv taC = cb_new_native(R, "TypedArray", Iter_self, 0); cb_add_prop(R, cb_o(taC), A_prototype, cb_objv(R->TypedP), 0); cb_add_prop(R, R->TypedP, A_constructor, taC, PF_W | PF_C);
    cb_defn(R, cb_o(taC), "from", TA_from, 1); cb_defn(R, cb_o(taC), "of", TA_of, 0);
    cb_defgs(R, R->TypedP, "length", TP_length, 0); cb_defgs(R, R->TypedP, "byteLength", TP_byteLength, 0); cb_defgs(R, R->TypedP, "byteOffset", TP_byteOffset, 0); cb_defgs(R, R->TypedP, "buffer", TP_buffer, 0);
    cb_define_acc(R, R->TypedP, S_toStringTag, cb_new_native(R, "get [Symbol.toStringTag]", TP_toStringTag, 0), CB_UNDEF, PF_C);
    cb_defn(R, R->TypedP, "set", TP_set, 1); cb_defn(R, R->TypedP, "subarray", TP_subarray, 2); cb_defn(R, R->TypedP, "slice", TP_slice, 2); cb_defn(R, R->TypedP, "fill", TP_fill, 1); cb_defn(R, R->TypedP, "map", TP_map, 1);
    cb_defn(R, R->TypedP, "filter", TP_filter, 1); cb_defn(R, R->TypedP, "reverse", TP_reverse, 0); cb_defn(R, R->TypedP, "sort", TP_sort, 1);
    { const char *gen[] = { "join", "indexOf", "lastIndexOf", "includes", "forEach", "every", "some", "find", "findIndex", "findLast", "findLastIndex", "reduce", "reduceRight", "at", "keys", "entries", "toString", "toLocaleString" };
      for (u32 i = 0; i < sizeof gen / sizeof *gen; i++) { cbv f = cb_get(R, cb_objv(R->ArrayP), cb_atom(R, gen[i])); cb_add_prop(R, R->TypedP, cb_atom(R, gen[i]), f, PF_W | PF_C); }
      cbv vf = cb_get(R, cb_objv(R->ArrayP), cb_atom(R, "values")); cb_add_prop(R, R->TypedP, cb_atom(R, "values"), vf, PF_W | PF_C); cb_add_prop(R, R->TypedP, S_iterator, vf, PF_W | PF_C); }
    for (int k = 0; k < TA_N; k++) { cb_obj *p = cb_new_obj(R, R->TypedP); R->TypedPs[k] = p; cb_obj *c = mkctor(R, TAN[k], TA_ctor, 3, p); c->n2 = (u32)k; c->proto = cb_o(taC); cb_defp(R, c, "BYTES_PER_ELEMENT", cb_num(TAS[k]), 0); cb_defp(R, p, "BYTES_PER_ELEMENT", cb_num(TAS[k]), 0); }
    R->DViewP = cb_new_obj(R, R->ObjectP); mkctor(R, "DataView", DataView_ctor, 1, R->DViewP);
    struct { const char *n; cb_native f; } DV[] = { { "getInt8", DV_getInt8 }, { "setInt8", DV_setInt8 }, { "getUint8", DV_getUint8 }, { "setUint8", DV_setUint8 }, { "getInt16", DV_getInt16 }, { "setInt16", DV_setInt16 },
        { "getUint16", DV_getUint16 }, { "setUint16", DV_setUint16 }, { "getInt32", DV_getInt32 }, { "setInt32", DV_setInt32 }, { "getUint32", DV_getUint32 }, { "setUint32", DV_setUint32 },
        { "getFloat32", DV_getFloat32 }, { "setFloat32", DV_setFloat32 }, { "getFloat64", DV_getFloat64 }, { "setFloat64", DV_setFloat64 } };
    for (u32 i = 0; i < sizeof DV / sizeof *DV; i++) cb_defn(R, R->DViewP, DV[i].n, DV[i].f, 1);
    cb_defgs(R, R->DViewP, "byteLength", DVP_byteLength, 0); cb_defgs(R, R->DViewP, "byteOffset", DVP_byteOffset, 0); cb_defgs(R, R->DViewP, "buffer", DVP_buffer, 0);
}
