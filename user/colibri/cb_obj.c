/* Colibri: objetos, propiedades, arrays, conversiones e igualdad. */
#include "cbi.h"
int cb_fmt_num(double d, char *o);
double cb_parse_number(const char *s, int len);

cbv S_iterator, S_asyncIterator, S_hasInstance, S_toPrimitive, S_toStringTag, S_species, S_isConcatSpreadable, S_unscopables, S_match, S_matchAll, S_replace, S_search, S_split;
cbv A_length, A_prototype, A_constructor, A_name, A_message, A_value, A_done, A_next, A_get, A_set, A_toString, A_valueOf, A_then, A_writable, A_enumerable, A_configurable, A_lastIndex, A_index, A_input, A_groups, A_stack, A_cause, A_callee, A_return, A_throw, A_raw, A_default;
static cbv small_keys[1024];
static u32 sym_ids;

cbv cb_sym_new(cb_rt *R, cbv desc) { cb_sym *s = cb_alloc(CT_SYM, sizeof(cb_sym)); s->desc = cb_isstr(desc) ? cb_s(desc) : 0; s->id = ++sym_ids; return ((cbv)TAG_SYM << 48) | (u64)s; }
void cb_obj_init(cb_rt *R) {
    for (int i = 0; i < 1024; i++) { char b[8]; int n = cb_snprintf(b, 8, "%d", i); small_keys[i] = cb_strv(cb_atom_str(cb_str_ascii(b, (u32)n))); cb_root(&small_keys[i]); }
    struct { cbv *v; const char *n; } A[] = { { &A_length, "length" }, { &A_prototype, "prototype" }, { &A_constructor, "constructor" }, { &A_name, "name" }, { &A_message, "message" }, { &A_value, "value" }, { &A_done, "done" },
        { &A_next, "next" }, { &A_get, "get" }, { &A_set, "set" }, { &A_toString, "toString" }, { &A_valueOf, "valueOf" }, { &A_then, "then" }, { &A_writable, "writable" }, { &A_enumerable, "enumerable" },
        { &A_configurable, "configurable" }, { &A_lastIndex, "lastIndex" }, { &A_index, "index" }, { &A_input, "input" }, { &A_groups, "groups" }, { &A_stack, "stack" }, { &A_cause, "cause" }, { &A_callee, "callee" },
        { &A_return, "return" }, { &A_throw, "throw" }, { &A_raw, "raw" }, { &A_default, "default" } };
    for (u32 i = 0; i < sizeof A / sizeof *A; i++) *A[i].v = cb_atom(R, A[i].n);
    struct { cbv *v; const char *n; } S[] = { { &S_iterator, "Symbol.iterator" }, { &S_asyncIterator, "Symbol.asyncIterator" }, { &S_hasInstance, "Symbol.hasInstance" }, { &S_toPrimitive, "Symbol.toPrimitive" },
        { &S_toStringTag, "Symbol.toStringTag" }, { &S_species, "Symbol.species" }, { &S_isConcatSpreadable, "Symbol.isConcatSpreadable" }, { &S_unscopables, "Symbol.unscopables" }, { &S_match, "Symbol.match" },
        { &S_matchAll, "Symbol.matchAll" }, { &S_replace, "Symbol.replace" }, { &S_search, "Symbol.search" }, { &S_split, "Symbol.split" } };
    for (u32 i = 0; i < sizeof S / sizeof *S; i++) { *S[i].v = cb_sym_new(R, cb_atom(R, S[i].n)); cb_root(S[i].v); } }

/* ---------------- tabla de propiedades ---------------- */
static u32 khash(cbv k) { if (cb_isstr(k)) return cb_hash(cb_s(k)); return (u32)(k >> 4) * 2654435761u; }
cb_prop *cb_own(cb_obj *o, cbv key) {
    if (!o->np) return 0;
    if (!o->idx) { for (u32 i = 0; i < o->np; i++) if (o->props[i].key == key && !(o->props[i].flags & PF_DEL)) return &o->props[i]; return 0; }
    u32 m = o->icap - 1, h = khash(key) & m;
    for (;;) { u32 x = o->idx[h]; if (!x) return 0; cb_prop *p = &o->props[x - 1]; if (p->key == key && !(p->flags & PF_DEL)) return p; h = (h + 1) & m; } }
static void reindex(cb_obj *o) {
    if (o->np <= 8) { o->idx = 0; o->icap = 0; return; }
    u32 ic = 16; while (ic < o->np * 2) ic *= 2; u32 *ix = cb_raw(ic * 4); memset(ix, 0, ic * 4);
    for (u32 i = 0; i < o->np; i++) { if (o->props[i].flags & PF_DEL) continue; u32 h = khash(o->props[i].key) & (ic - 1); while (ix[h]) h = (h + 1) & (ic - 1); ix[h] = i + 1; }
    o->idx = ix; o->icap = ic; }
static void compact(cb_obj *o) { u32 k = 0; for (u32 i = 0; i < o->np; i++) if (!(o->props[i].flags & PF_DEL)) o->props[k++] = o->props[i]; o->np = k; o->ndel = 0; reindex(o); }
int cb_add_prop(cb_rt *R, cb_obj *o, cbv key, cbv v, u32 flags) {
    if (o->np == o->pcap) { if (o->ndel > o->np / 2) compact(o); if (o->np == o->pcap) { u32 nc = o->pcap ? o->pcap * 2 : 4; cb_prop *np = cb_blob(nc * sizeof(cb_prop)); if (o->np) memcpy(np, o->props, o->np * sizeof(cb_prop)); o->props = np; o->pcap = nc; } }
    cb_prop *p = &o->props[o->np++]; p->key = key; p->val = v; p->flags = flags;
    if (o->np > 8) { if (!o->idx || o->np * 2 > o->icap) reindex(o); else { u32 m = o->icap - 1, h = khash(key) & m; while (o->idx[h]) h = (h + 1) & m; o->idx[h] = o->np; } }
    return 0; }
static void del_prop(cb_obj *o, cb_prop *p) { p->flags = PF_DEL; p->val = CB_UNDEF; o->ndel++; if (o->ndel > 16 && o->ndel > o->np / 2) compact(o); }
static cb_acc *acc_new(cbv g, cbv s) { cb_acc *a = cb_alloc(CT_ACC, sizeof(cb_acc)); a->get = g; a->set = s; return a; }

/* ---------------- objetos ---------------- */
cb_obj *cb_new_obj(cb_rt *R, cb_obj *proto) { cb_obj *o = cb_alloc(CT_OBJ, sizeof(cb_obj)); o->proto = proto; o->ext = 1; o->cls = OC_OBJECT; return o; }
cb_obj *cb_new_array(cb_rt *R, u32 n) { cb_obj *a = cb_new_obj(R, R->ArrayP); a->cls = OC_ARRAY; if (n) { a->e = cb_blob((u64)n * 8); a->n2 = n; for (u32 i = 0; i < n; i++) a->e[i] = CB_UNDEF; } a->n1 = n; return a; }
cbv cb_list_to_array(cb_rt *R, int n, cbv *v) { cb_obj *a = cb_new_array(R, (u32)n); for (int i = 0; i < n; i++) a->e[i] = v[i]; return cb_objv(a); }
static void arr_grow(cb_obj *a, u32 need) { if (need <= a->n2) return; u32 nc = a->n2 ? a->n2 * 2 : 8; if (nc < need) nc = need; cbv *ne = cb_blob((u64)nc * 8); if (a->n1 && a->e) memcpy(ne, a->e, (u64)(a->n1 < a->n2 ? a->n1 : a->n2) * 8); for (u32 i = a->n1 < a->n2 ? a->n1 : a->n2; i < nc; i++) ne[i] = CB_EMPTY; a->e = ne; a->n2 = nc; }
void cb_arr_push(cb_rt *R, cb_obj *a, cbv v) { arr_grow(a, a->n1 + 1); a->e[a->n1++] = v; }
int cb_arr_set(cb_rt *R, cb_obj *a, u32 i, cbv v) {
    if (a->sealed == 2) return -1;
    if (i >= a->n1) { if (!a->ext) return -1; if (i > a->n1 + 1000000 && i > a->n1 * 4) { /* muy disperso: se guarda igual (memoria) */ } arr_grow(a, i + 1); for (u32 k = a->n1; k < i; k++) a->e[k] = CB_EMPTY; a->n1 = i + 1; }
    a->e[i] = v; return 0; }
void cb_arr_setlen(cb_rt *R, cb_obj *a, u32 n) { if (n < a->n1) { for (u32 i = n; i < a->n1 && i < a->n2; i++) a->e[i] = CB_EMPTY; a->n1 = n; } else if (n > a->n1) { arr_grow(a, n < 100000000 ? n : a->n1); for (u32 i = a->n1; i < n && i < a->n2; i++) a->e[i] = CB_EMPTY; a->n1 = n; } }

/* ---------------- claves ---------------- */
cbv cb_idx_key(cb_rt *R, u32 i) { if (i < 1024) return small_keys[i]; char b[16]; int n = cb_snprintf(b, 16, "%u", i); return cb_strv(cb_atom_str(cb_str_ascii(b, (u32)n))); }
int cb_key_index(cbv key, u32 *out) {
    if (!cb_isstr(key)) return 0; cb_str *s = cb_flat(cb_s(key)); if (!s->len || s->len > 10) return 0;
    if (s->c[0] == '0') { if (s->len == 1) { *out = 0; return 1; } return 0; }
    u64 v = 0; for (u32 i = 0; i < s->len; i++) { u16 c = s->c[i]; if (c < '0' || c > '9') return 0; v = v * 10 + (c - '0'); }
    if (v >= 0xFFFFFFFFULL) return 0; *out = (u32)v; return 1; }
cbv cb_key(cb_rt *R, cbv v) {
    if (cb_isstr(v)) return cb_s(v)->atom ? v : cb_intern(R, cb_s(v));
    if (cb_issym(v)) return v;
    if (cb_isnum(v)) { double d = cb_d(v); if (d >= 0 && d < 4294967295.0 && d == (double)(u32)d) return cb_idx_key(R, (u32)d); }
    if (cb_isobj(v)) { v = cb_to_prim(R, v, 2); if (v == CB_EMPTY) return CB_EMPTY; if (cb_issym(v)) return v; }
    cbv s = cb_to_str(R, v); if (s == CB_EMPTY) return CB_EMPTY; return cb_intern(R, cb_s(s)); }

/* ---------------- lectura ---------------- */
static cbv proxy_get(cb_rt *R, cb_obj *px, cbv key, cbv recv);
static int proxy_set(cb_rt *R, cb_obj *px, cbv key, cbv v, cbv recv);
static int proxy_has(cb_rt *R, cb_obj *px, cbv key);
static int proxy_del(cb_rt *R, cb_obj *px, cbv key);
cbv cb_typed_get(cb_rt *R, cb_obj *t, u32 i);
int cb_typed_set(cb_rt *R, cb_obj *t, u32 i, cbv v);
static cbv str_char(cb_rt *R, cb_str *s, u32 i) { s = cb_flat(s); u16 c = s->c[i]; cb_str *r = cb_str_new(1); r->c[0] = c; r->ascii = c < 128; return cb_strv(r); }
cbv cb_get_proto_chain(cb_rt *R, cb_obj *o, cbv key, cbv recv) {
    int guard = 0;
    for (cb_obj *cur = o; cur && guard < 100000; cur = cur->proto, guard++) {
        switch (cur->cls) {
        case OC_ARRAY: case OC_ARGS: { u32 i; if (cb_key_index(key, &i)) { if (i < cur->n1 && i < cur->n2 && cur->e[i] != CB_EMPTY) return cur->e[i]; } else if (key == A_length && cur->cls == OC_ARRAY) return cb_num(cur->n1); break; }
        case OC_STRING: { cb_str *s = cb_s(cur->v1); u32 i; if (cb_key_index(key, &i)) { if (i < s->len) return str_char(R, s, i); } else if (key == A_length) return cb_num(s->len); break; }
        case OC_TYPED: { u32 i; if (cb_key_index(key, &i)) return cb_typed_get(R, cur, i); break; }
        case OC_PROXY: return proxy_get(R, cur, key, recv);
        }
        cb_prop *p = cb_own(cur, key);
        if (p) { if (p->flags & PF_ACC) { cb_acc *a = (cb_acc *)p->val; if (!cb_isfunc(a->get)) return CB_UNDEF; return cb_call_v(R, a->get, recv, 0, 0); } return p->val; } }
    return CB_UNDEF; }
cb_obj *cb_proto_of_prim(cb_rt *R, cbv v) { if (cb_isbig(v)) return R->BigIntP; if (cb_isstr(v)) return R->StringP; if (cb_isnum(v)) return R->NumberP; if (cb_isbool(v)) return R->BooleanP; if (cb_issym(v)) return R->SymbolP; return 0; }
static cbv keyname(cb_rt *R, cbv key, char *b, int cap) { if (cb_isstr(key)) { long n; char *u = cb_str_utf8z(cb_s(key), &n); cb_snprintf(b, cap, "%s", u); free(u); } else cb_snprintf(b, cap, "Symbol()"); return 0; }
cbv cb_get(cb_rt *R, cbv o, cbv key) {
    if (cb_isobj(o)) return cb_get_proto_chain(R, cb_o(o), key, o);
    if (cb_isstr(o)) { cb_str *s = cb_s(o); u32 i; if (key == A_length) return cb_num(s->len); if (cb_key_index(key, &i)) return i < s->len ? str_char(R, s, i) : CB_UNDEF; return cb_get_proto_chain(R, R->StringP, key, o); }
    if (cb_isnullish(o)) { char b[100]; keyname(R, key, b, 100); return cb_throw(R, 1, "Cannot read properties of %s (reading '%s')", o == CB_NULL ? "null" : "undefined", b); }
    return cb_get_proto_chain(R, cb_proto_of_prim(R, o), key, o); }
cbv cb_getp(cb_rt *R, cbv o, const char *name) { return cb_get(R, o, cb_atom(R, name)); }

/* ---------------- escritura ---------------- */
int cb_put(cb_rt *R, cbv o, cbv key, cbv v, cbv recv, int strict) {
    if (!cb_isobj(o)) {
        if (cb_isnullish(o)) { char b[100]; keyname(R, key, b, 100); cb_throw(R, 1, "Cannot set properties of %s (setting '%s')", o == CB_NULL ? "null" : "undefined", b); return -1; }
        cb_obj *pr = cb_proto_of_prim(R, o); /* setters en el prototipo; si no, se ignora (o error en modo estricto) */
        for (cb_obj *c = pr; c; c = c->proto) { cb_prop *p = cb_own(c, key); if (p) { if (p->flags & PF_ACC) { cb_acc *a = (cb_acc *)p->val; if (cb_isfunc(a->set)) return cb_call_v(R, a->set, recv, 1, &v) == CB_EMPTY ? -1 : 0; } break; } }
        if (strict) { cb_throw(R, 1, "Cannot create property on a primitive value"); return -1; } return 0; }
    cb_obj *ob = cb_o(o);
    for (cb_obj *cur = ob; cur; cur = cur->proto) {
        switch (cur->cls) {
        case OC_ARRAY: case OC_ARGS: if (cur == cb_o(recv) || !cb_isobj(recv)) { u32 i; if (cb_key_index(key, &i)) { if (cb_arr_set(R, cur, i, v)) { if (strict) { cb_throw(R, 1, "Cannot assign to read only property"); return -1; } } return 0; }
                if (key == A_length && cur->cls == OC_ARRAY) { double d = cb_to_num(R, v); if (R->has_exc) return -1; if (d < 0 || d != (double)(u32)d) { cb_throw(R, 2, "Invalid array length"); return -1; } if (cur->sealed) return strict ? (cb_throw(R, 1, "Cannot assign to read only property 'length'"), -1) : 0; cb_arr_setlen(R, cur, (u32)d); return 0; } } break;
        case OC_TYPED: { u32 i; if (cb_key_index(key, &i)) return cb_typed_set(R, cur, i, v); break; }
        case OC_STRING: { u32 i; if (key == A_length || (cb_key_index(key, &i) && i < cb_s(cur->v1)->len)) { if (strict) { cb_throw(R, 1, "Cannot assign to read only property"); return -1; } return 0; } break; }
        case OC_PROXY: return proxy_set(R, cur, key, v, recv);
        }
        cb_prop *p = cb_own(cur, key);
        if (p) {
            if (p->flags & PF_ACC) { cb_acc *a = (cb_acc *)p->val; if (!cb_isfunc(a->set)) { if (strict) { cb_throw(R, 1, "Cannot set property which has only a getter"); return -1; } return 0; } return cb_call_v(R, a->set, recv, 1, &v) == CB_EMPTY ? -1 : 0; }
            if (!(p->flags & PF_W)) { if (strict) { char b[100]; keyname(R, key, b, 100); cb_throw(R, 1, "Cannot assign to read only property '%s' of object", b); return -1; } return 0; }
            if (cur == ob && recv == o) { p->val = v; return 0; }
            break; } }
    if (!cb_isobj(recv)) return 0;
    cb_obj *r = cb_o(recv);
    if (r != ob) { cb_prop *p = cb_own(r, key); if (p && !(p->flags & PF_ACC)) { if (!(p->flags & PF_W)) return 0; p->val = v; return 0; } }
    if (!r->ext) { if (strict) { cb_throw(R, 1, "Cannot add property, object is not extensible"); return -1; } return 0; }
    if (r->cls == OC_ARRAY) { u32 i; if (cb_key_index(key, &i)) return cb_arr_set(R, r, i, v); }
    return cb_add_prop(R, r, key, v, PF_WEC); }
int cb_set(cb_rt *R, cbv o, cbv key, cbv v) { return cb_put(R, o, key, v, o, 1); }
int cb_setp(cb_rt *R, cbv o, const char *name, cbv v) { return cb_put(R, o, cb_atom(R, name), v, o, 1); }
int cb_define(cb_rt *R, cb_obj *o, cbv key, cbv v, u32 flags) {
    if (o->cls == OC_ARRAY || o->cls == OC_ARGS) { u32 i; if (cb_key_index(key, &i) && !(flags & PF_ACC)) return cb_arr_set(R, o, i, v); if (key == A_length && o->cls == OC_ARRAY && !(flags & PF_ACC)) { cb_arr_setlen(R, o, (u32)cb_to_num(R, v)); if (!(flags & PF_W)) o->sealed = o->sealed ? o->sealed : 1; return 0; } }
    cb_prop *p = cb_own(o, key);
    if (p) { p->val = v; p->flags = flags; return 0; }
    return cb_add_prop(R, o, key, v, flags); }
void cb_defp(cb_rt *R, cb_obj *o, const char *name, cbv v, u32 flags) { cb_define(R, o, cb_atom(R, name), v, flags); }
void cb_defn(cb_rt *R, cb_obj *o, const char *name, cb_native f, int nargs) { cbv fn = cb_new_native(R, name, f, nargs); cbv k = name[0] == '@' ? 0 : cb_atom(R, name); if (k) cb_define(R, o, k, fn, PF_W | PF_C); }
void cb_define_acc(cb_rt *R, cb_obj *o, cbv key, cbv get, cbv set, u32 flags) {
    cb_prop *p = cb_own(o, key);
    if (p && (p->flags & PF_ACC)) { cb_acc *a = (cb_acc *)p->val; if (!cb_isundef(get)) a->get = get; if (!cb_isundef(set)) a->set = set; p->flags = flags | PF_ACC; return; }
    cb_acc *a = acc_new(get, set); if (p) { p->val = (cbv)(u64)a; p->flags = flags | PF_ACC; } else cb_add_prop(R, o, key, (cbv)(u64)a, flags | PF_ACC); }
void cb_defgs(cb_rt *R, cb_obj *o, const char *name, cb_native get, cb_native set) {
    char gn[80], sn[80]; cb_snprintf(gn, 80, "get %s", name); cb_snprintf(sn, 80, "set %s", name);
    cb_define_acc(R, o, cb_atom(R, name), get ? cb_new_native(R, gn, get, 0) : CB_UNDEF, set ? cb_new_native(R, sn, set, 1) : CB_UNDEF, PF_C); }
int cb_del(cb_rt *R, cbv ov, cbv key, int strict) {
    if (!cb_isobj(ov)) return 0; cb_obj *o = cb_o(ov);
    if (o->cls == OC_PROXY) return proxy_del(R, o, key);
    if (o->cls == OC_ARRAY || o->cls == OC_ARGS) { u32 i; if (cb_key_index(key, &i)) { if (o->sealed) { if (strict) { cb_throw(R, 1, "Cannot delete property"); return -1; } return 0; } if (i < o->n1 && i < o->n2) o->e[i] = CB_EMPTY; return 0; } }
    cb_prop *p = cb_own(o, key); if (!p) return 0;
    if (!(p->flags & PF_C)) { if (strict) { char b[100]; keyname(R, key, b, 100); cb_throw(R, 1, "Cannot delete property '%s'", b); return -1; } return 0; }
    del_prop(o, p); return 0; }
int cb_has_own(cb_rt *R, cb_obj *o, cbv key) {
    switch (o->cls) {
    case OC_ARRAY: case OC_ARGS: { u32 i; if (cb_key_index(key, &i)) return i < o->n1 && i < o->n2 && o->e[i] != CB_EMPTY; if (key == A_length && o->cls == OC_ARRAY) return 1; break; }
    case OC_STRING: { u32 i; if (cb_key_index(key, &i) && i < cb_s(o->v1)->len) return 1; if (key == A_length) return 1; break; }
    case OC_TYPED: { u32 i; if (cb_key_index(key, &i)) return i < o->n2; break; }
    case OC_PROXY: return proxy_has(R, o, key); }
    return cb_own(o, key) != 0; }
int cb_has(cb_rt *R, cbv ov, cbv key) {
    if (!cb_isobj(ov)) return 0;
    for (cb_obj *o = cb_o(ov); o; o = o->proto) { if (o->cls == OC_PROXY) return proxy_has(R, o, key); if (cb_has_own(R, o, key)) return 1; }
    return 0; }

/* ---------------- claves propias (en orden: indices, cadenas, simbolos) ---------------- */
static int cmpu32(const void *a, const void *b) { u32 x = *(const u32 *)a, y = *(const u32 *)b; return x < y ? -1 : x > y; }
cbv cb_proxy_keys(cb_rt *R, cb_obj *px);
cbv cb_own_keys(cb_rt *R, cbv ov, int which) {
    cb_obj *o = cb_o(ov); cb_obj *out = cb_new_array(R, 0); int enumo = which & 4; which &= 3;
    if (o->cls == OC_PROXY) { cbv k = cb_proxy_keys(R, o); if (k == CB_EMPTY) return k; cb_obj *ka = cb_o(k); for (u32 i = 0; i < ka->n1; i++) { cbv x = ka->e[i]; if ((cb_issym(x) ? 2 : 1) & which) cb_arr_push(R, out, x); } return cb_objv(out); }
    if (which & 1) {
        if (o->cls == OC_ARRAY || o->cls == OC_ARGS) for (u32 i = 0; i < o->n1 && i < o->n2; i++) { if (o->e[i] != CB_EMPTY) cb_arr_push(R, out, cb_idx_key(R, i)); }
        if (o->cls == OC_STRING) for (u32 i = 0; i < cb_s(o->v1)->len; i++) cb_arr_push(R, out, cb_idx_key(R, i));
        if (o->cls == OC_TYPED) for (u32 i = 0; i < o->n2; i++) cb_arr_push(R, out, cb_idx_key(R, i));
        /* indices guardados como propiedades, ordenados */
        u32 nidx = 0; for (u32 i = 0; i < o->np; i++) { u32 x; if (!(o->props[i].flags & PF_DEL) && cb_key_index(o->props[i].key, &x) && (!enumo || (o->props[i].flags & PF_E))) nidx++; }
        if (nidx) { u32 *ix = malloc(nidx * 4), k = 0; for (u32 i = 0; i < o->np; i++) { u32 x; if (!(o->props[i].flags & PF_DEL) && cb_key_index(o->props[i].key, &x) && (!enumo || (o->props[i].flags & PF_E))) ix[k++] = x; } qsort(ix, nidx, 4, cmpu32); for (u32 i = 0; i < nidx; i++) cb_arr_push(R, out, cb_idx_key(R, ix[i])); free(ix); }
        if (o->cls == OC_ARRAY && !enumo) cb_arr_push(R, out, A_length);
        if (o->cls == OC_STRING && !enumo) cb_arr_push(R, out, A_length);
        for (u32 i = 0; i < o->np; i++) { cb_prop *p = &o->props[i]; u32 x; if ((p->flags & PF_DEL) || !cb_isstr(p->key) || cb_key_index(p->key, &x)) continue; if (enumo && !(p->flags & PF_E)) continue; cb_arr_push(R, out, p->key); } }
    if (which & 2) for (u32 i = 0; i < o->np; i++) { cb_prop *p = &o->props[i]; if ((p->flags & PF_DEL) || !cb_issym(p->key)) continue; if (enumo && !(p->flags & PF_E)) continue; cb_arr_push(R, out, p->key); }
    return cb_objv(out); }
/* descriptor propio: 1 si existe */
int cb_get_own_desc(cb_rt *R, cb_obj *o, cbv key, cbv *val, u32 *flags, cbv *get, cbv *set);

/* ---------------- conversiones ---------------- */
int cb_truthy(cbv v) {
    if (cb_isbig(v)) return cb_big_truthy(v);
    if (cb_isnum(v)) { double d = cb_d(v); return d == d && d != 0; }
    if (cb_isbool(v)) return (int)(v & 1);
    if (cb_isstr(v)) return cb_s(v)->len > 0;
    if (cb_isnullish(v)) return 0;
    return 1; }
cbv cb_to_prim(cb_rt *R, cbv v, int hint) {
    if (!cb_isobj(v)) return v;
    cbv ex = cb_get(R, v, S_toPrimitive); if (ex == CB_EMPTY) return ex;
    if (!cb_isnullish(ex)) { if (!cb_isfunc(ex)) return cb_throw(R, 1, "Symbol.toPrimitive is not a function");
        cbv h = cb_atom(R, hint == 1 ? "number" : hint == 2 ? "string" : "default"); cbv r = cb_call_v(R, ex, v, 1, &h); if (r == CB_EMPTY) return r; if (cb_isobj(r)) return cb_throw(R, 1, "Cannot convert object to primitive value"); return r; }
    cbv order[2] = { hint == 2 ? A_toString : A_valueOf, hint == 2 ? A_valueOf : A_toString };
    for (int i = 0; i < 2; i++) { cbv f = cb_get(R, v, order[i]); if (f == CB_EMPTY) return f; if (cb_isfunc(f)) { cbv r = cb_call_v(R, f, v, 0, 0); if (r == CB_EMPTY) return r; if (!cb_isobj(r)) return r; } }
    return cb_throw(R, 1, "Cannot convert object to primitive value"); }
static int jsws(u16 c) { return c == 9 || c == 10 || c == 11 || c == 12 || c == 13 || c == 32 || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000 || c == 0xFEFF; }
double cb_str_to_num(cb_str *s) {
    s = cb_flat(s); u32 a = 0, b = s->len; while (a < b && jsws(s->c[a])) a++; while (b > a && jsws(s->c[b - 1])) b--;
    if (a == b) return 0;
    u32 n = b - a; if (n > 2000) return __builtin_nan("");
    char buf[2048]; for (u32 i = 0; i < n; i++) { u16 c = s->c[a + i]; if (c > 127) return __builtin_nan(""); buf[i] = (char)c; } buf[n] = 0;
    const char *p = buf;
    if (n > 2 && p[0] == '0' && strchr("xXoObB", p[1])) { int base = (p[1] | 32) == 'x' ? 16 : (p[1] | 32) == 'o' ? 8 : 2; double v = 0; for (u32 i = 2; i < n; i++) { int c = p[i], d = c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : 99; if (d >= base) return __builtin_nan(""); v = v * base + d; } return v; }
    int sgn = 0; if (*p == '+' || *p == '-') { sgn = *p == '-'; p++; }
    if (!strcmp(p, "Infinity")) return sgn ? -__builtin_inf() : __builtin_inf();
    /* forma valida: cifras [. cifras] [e [+-] cifras] */
    const char *q = p; int dig = 0; while (*q >= '0' && *q <= '9') { q++; dig++; } if (*q == '.') { q++; while (*q >= '0' && *q <= '9') { q++; dig++; } }
    if (!dig) return __builtin_nan(""); if (*q == 'e' || *q == 'E') { q++; if (*q == '+' || *q == '-') q++; if (!(*q >= '0' && *q <= '9')) return __builtin_nan(""); while (*q >= '0' && *q <= '9') q++; }
    if (*q) return __builtin_nan("");
    double v = cb_parse_number(p, (int)(q - p)); return sgn ? -v : v; }
double cb_to_num(cb_rt *R, cbv v) {
    if (cb_isnum(v)) return cb_d(v);
    if (cb_isbool(v)) return (double)(v & 1);
    if (cb_isundef(v)) return __builtin_nan("");
    if (cb_isnull(v)) return 0;
    if (cb_isstr(v)) return cb_str_to_num(cb_s(v));
    if (cb_issym(v)) { cb_throw(R, 1, "Cannot convert a Symbol value to a number"); return 0; }
    if (cb_isbig(v)) { cb_throw(R, 1, "Cannot convert a BigInt value to a number"); return 0; }
    cbv p = cb_to_prim(R, v, 1); if (p == CB_EMPTY) return 0; return cb_to_num(R, p); }
cbv cb_mkstr(cb_rt *R, const char *s) { long n = (long)strlen(s); for (long i = 0; i < n; i++) if ((u8)s[i] >= 0x80) return cb_strv(cb_str_from_utf8(s, n)); return cb_strv(cb_str_ascii(s, (u32)n)); }
cbv cb_num_to_str(cb_rt *R, double d) {
    if (d >= 0 && d < 1024 && d == (double)(int)d && !(d == 0 && __builtin_signbit(d))) return small_keys[(int)d];
    char b[40]; int n = cb_fmt_num(d, b); return cb_strv(cb_str_ascii(b, (u32)n)); }
cbv cb_to_str(cb_rt *R, cbv v) {
    if (cb_isstr(v)) return v;
    if (cb_isnum(v)) return cb_num_to_str(R, cb_d(v));
    if (v == CB_TRUE) return cb_atom(R, "true"); if (v == CB_FALSE) return cb_atom(R, "false");
    if (cb_isundef(v)) return cb_atom(R, "undefined"); if (cb_isnull(v)) return cb_atom(R, "null");
    if (cb_issym(v)) return cb_throw(R, 1, "Cannot convert a Symbol value to a string");
    if (cb_isbig(v)) return cb_big_to_str(R, v, 10);
    cbv p = cb_to_prim(R, v, 2); if (p == CB_EMPTY) return p; return cb_to_str(R, p); }
char *cb_to_utf8(cb_rt *R, cbv v, long *n) { cbv s = cb_to_str(R, v); if (s == CB_EMPTY) return 0; return cb_str_utf8z(cb_s(s), n); }
cb_obj *cb_to_obj(cb_rt *R, cbv v) {
    if (cb_isobj(v)) return cb_o(v);
    if (cb_isnullish(v)) { cb_throw(R, 1, "Cannot convert undefined or null to object"); return 0; }
    if (cb_isbig(v)) { cb_obj *o = cb_new_obj(R, R->BigIntP); o->v1 = v; return o; }
    cb_obj *o = cb_new_obj(R, cb_proto_of_prim(R, v)); o->cls = cb_isstr(v) ? OC_STRING : cb_isnum(v) ? OC_NUMBER : cb_isbool(v) ? OC_BOOL : OC_SYMBOL; o->v1 = v; return o; }
double cb_to_int(cb_rt *R, cbv v) { double d = cb_to_num(R, v); if (d != d) return 0; if (__builtin_isinf(d)) return d; return __builtin_trunc(d) + 0.0; }
int32_t cb_to_i32(double d) { if (d != d || __builtin_isinf(d)) return 0; if (d >= -2147483648.0 && d < 2147483648.0) return (int32_t)d; d = __builtin_trunc(d); d = __builtin_fmod(d, 4294967296.0); if (d < 0) d += 4294967296.0; return (int32_t)(u32)(u64)d; }
u32 cb_to_u32(double d) { return (u32)cb_to_i32(d); }

/* ---------------- igualdad y typeof ---------------- */
int cb_strict_eq(cbv a, cbv b) {
    if (cb_isnum(a) && cb_isnum(b)) return cb_d(a) == cb_d(b);
    if (cb_isstr(a) && cb_isstr(b)) return cb_str_eq(cb_s(a), cb_s(b));
    if (cb_isbig(a) && cb_isbig(b)) return cb_big_cmp(a, b) == 0;
    return a == b; }
int cb_same_value(cbv a, cbv b, int zero) {
    if (cb_isnum(a) && cb_isnum(b)) { double x = cb_d(a), y = cb_d(b); if (x != x) return y != y; if (!zero && x == 0 && y == 0) return __builtin_signbit(x) == __builtin_signbit(y); return x == y; }
    return cb_strict_eq(a, b); }
int cb_loose_eq(cb_rt *R, cbv a, cbv b) {
    for (int guard = 0; guard < 8; guard++) {
        if (CB_TAG(a) == CB_TAG(b) || (cb_isnum(a) && cb_isnum(b))) return cb_strict_eq(a, b);
        if (cb_isnullish(a) && cb_isnullish(b)) return 1;
        if (cb_isnullish(a) || cb_isnullish(b)) return 0;
        if (cb_isbig(a) && cb_isnum(b)) return cb_big_cmp_num(a, cb_d(b)) == 0;
        if (cb_isnum(a) && cb_isbig(b)) return cb_big_cmp_num(b, cb_d(a)) == 0;
        if (cb_isbig(a) && cb_isstr(b)) { cbv x = cb_big_from_str(R, cb_s(b), 1); return x != CB_EMPTY && cb_big_cmp(a, x) == 0; }
        if (cb_isstr(a) && cb_isbig(b)) { cbv x = cb_big_from_str(R, cb_s(a), 1); return x != CB_EMPTY && cb_big_cmp(b, x) == 0; }
        if (cb_isnum(a) && cb_isstr(b)) return cb_d(a) == cb_str_to_num(cb_s(b));
        if (cb_isstr(a) && cb_isnum(b)) return cb_str_to_num(cb_s(a)) == cb_d(b);
        if (cb_isbool(a)) { a = cb_num((double)(a & 1)); continue; }
        if (cb_isbool(b)) { b = cb_num((double)(b & 1)); continue; }
        if (cb_isobj(a) && !cb_isobj(b)) { a = cb_to_prim(R, a, 0); if (a == CB_EMPTY) return 0; continue; }
        if (cb_isobj(b) && !cb_isobj(a)) { b = cb_to_prim(R, b, 0); if (b == CB_EMPTY) return 0; continue; }
        return 0; }
    return 0; }
int cb_isfunc(cbv v) { if (!cb_isobj(v)) return 0; cb_obj *o = cb_o(v); if (o->cls == OC_PROXY) return cb_isfunc(o->v1); return o->cls == OC_FUNC || o->cls == OC_NATIVE || o->cls == OC_BOUND; }
int cb_iserror(cbv v) { return cb_isobj(v) && cb_o(v)->cls == OC_ERROR; }
cbv cb_typeof(cb_rt *R, cbv v) {
    const char *t = cb_isbig(v) ? "bigint" : cb_isnum(v) ? "number" : cb_isstr(v) ? "string" : cb_isbool(v) ? "boolean" : cb_isundef(v) ? "undefined" : cb_isnull(v) ? "object" : cb_issym(v) ? "symbol" : cb_isfunc(v) ? "function" : "object";
    return cb_atom(R, t); }
cbv cb_instance_of(cb_rt *R, cbv v, cbv c) {
    if (!cb_isobj(c)) return cb_throw(R, 1, "Right-hand side of 'instanceof' is not an object");
    cbv h = cb_get(R, c, S_hasInstance); if (h == CB_EMPTY) return h;
    if (!cb_isnullish(h) && !(cb_isobj(h) && cb_o(h)->cls == OC_NATIVE && cb_o(h)->n2 == 0x1E5)) { cbv r = cb_call_v(R, h, c, 1, &v); if (r == CB_EMPTY) return r; return cb_bool(cb_truthy(r)); }
    if (!cb_isfunc(c)) return cb_throw(R, 1, "Right-hand side of 'instanceof' is not callable");
    if (cb_o(c)->cls == OC_BOUND) return cb_instance_of(R, v, cb_o(c)->v1);
    if (!cb_isobj(v)) return CB_FALSE;
    cbv pr = cb_get(R, c, A_prototype); if (pr == CB_EMPTY) return pr; if (!cb_isobj(pr)) return cb_throw(R, 1, "Function has non-object prototype in instanceof check");
    for (cb_obj *o = cb_o(v)->proto; o; o = o->proto) if (o == cb_o(pr)) return CB_TRUE;
    return CB_FALSE; }

/* ---------------- iteradores ---------------- */
cbv cb_get_iter(cb_rt *R, cbv v, int async) {
    cbv f = CB_UNDEF;
    if (async) { f = cb_get(R, v, S_asyncIterator); if (f == CB_EMPTY) return f; }
    if (cb_isnullish(f)) { f = cb_get(R, v, S_iterator); if (f == CB_EMPTY) return f; }
    if (!cb_isfunc(f)) return cb_throw(R, 1, "object is not iterable");
    cbv it = cb_call_v(R, f, v, 0, 0); if (it == CB_EMPTY) return it;
    if (!cb_isobj(it)) return cb_throw(R, 1, "Result of the Symbol.iterator method is not an object");
    return it; }
cbv cb_array_iter_next_fast(cb_rt *R, cbv it, int *done);
int cb_iter_next(cb_rt *R, cbv it, cbv *val) {
    int done; cbv fast = cb_array_iter_next_fast(R, it, &done); if (fast != CB_EMPTY || done == 2) { if (done == 2) return -1; if (done) return 0; *val = fast; return 1; }
    cbv nx = cb_get(R, it, A_next); if (nx == CB_EMPTY) return -1;
    cbv r = cb_call_v(R, nx, it, 0, 0); if (r == CB_EMPTY) return -1;
    if (!cb_isobj(r)) { cb_throw(R, 1, "Iterator result is not an object"); return -1; }
    cbv d = cb_get(R, r, A_done); if (d == CB_EMPTY) return -1; if (cb_truthy(d)) return 0;
    cbv v = cb_get(R, r, A_value); if (v == CB_EMPTY) return -1; *val = v; return 1; }
void cb_iter_close(cb_rt *R, cbv it) {
    cbv exc = R->exc; int had = R->has_exc; R->has_exc = 0;
    cbv f = cb_get(R, it, A_return); if (f != CB_EMPTY && cb_isfunc(f)) cb_call_v(R, f, it, 0, 0);
    if (had) { R->exc = exc; R->has_exc = 1; } }

/* ---------------- proxies ---------------- */
static cbv trap(cb_rt *R, cb_obj *px, const char *name) {
    if (cb_isnull(px->v2)) return cb_throw(R, 1, "Cannot perform '%s' on a proxy that has been revoked", name);
    cbv t = cb_getp(R, px->v2, name); if (t == CB_EMPTY) return t; if (cb_isnullish(t)) return CB_UNDEF; return t; }
static cbv proxy_get(cb_rt *R, cb_obj *px, cbv key, cbv recv) {
    cbv t = trap(R, px, "get"); if (t == CB_EMPTY) return t;
    if (cb_isundef(t)) return cb_isobj(px->v1) ? cb_get_proto_chain(R, cb_o(px->v1), key, recv) : CB_UNDEF;
    cbv a[3] = { px->v1, key, recv }; return cb_call_v(R, t, px->v2, 3, a); }
static int proxy_set(cb_rt *R, cb_obj *px, cbv key, cbv v, cbv recv) {
    cbv t = trap(R, px, "set"); if (t == CB_EMPTY) return -1;
    if (cb_isundef(t)) return cb_put(R, px->v1, key, v, recv == cb_objv(px) ? px->v1 : recv, 1);
    cbv a[4] = { px->v1, key, v, recv }; cbv r = cb_call_v(R, t, px->v2, 4, a); if (r == CB_EMPTY) return -1;
    if (!cb_truthy(r)) { cb_throw(R, 1, "'set' on proxy: trap returned falsish"); return -1; } return 0; }
static int proxy_has(cb_rt *R, cb_obj *px, cbv key) {
    cbv t = trap(R, px, "has"); if (t == CB_EMPTY) return 0;
    if (cb_isundef(t)) return cb_has(R, px->v1, key);
    cbv a[2] = { px->v1, key }; cbv r = cb_call_v(R, t, px->v2, 2, a); return r != CB_EMPTY && cb_truthy(r); }
static int proxy_del(cb_rt *R, cb_obj *px, cbv key) {
    cbv t = trap(R, px, "deleteProperty"); if (t == CB_EMPTY) return -1;
    if (cb_isundef(t)) return cb_del(R, px->v1, key, 1);
    cbv a[2] = { px->v1, key }; return cb_call_v(R, t, px->v2, 2, a) == CB_EMPTY ? -1 : 0; }
cbv cb_proxy_keys(cb_rt *R, cb_obj *px) {
    cbv t = trap(R, px, "ownKeys"); if (t == CB_EMPTY) return t;
    if (cb_isundef(t)) return cb_own_keys(R, px->v1, 3);
    cbv r = cb_call_v(R, t, px->v2, 1, &px->v1); if (r == CB_EMPTY) return r;
    u32 n; cbv *e; cbv a = cb_array_like(R, r, &n, &e); if (a == CB_EMPTY) return a; return cb_list_to_array(R, (int)n, e); }
int cb_get_own_desc(cb_rt *R, cb_obj *o, cbv key, cbv *val, u32 *flags, cbv *get, cbv *set) {
    *get = *set = CB_UNDEF; *val = CB_UNDEF; *flags = 0;
    switch (o->cls) {
    case OC_ARRAY: case OC_ARGS: { u32 i; if (cb_key_index(key, &i)) { if (i < o->n1 && i < o->n2 && o->e[i] != CB_EMPTY) { *val = o->e[i]; *flags = o->sealed == 2 ? PF_E : o->sealed == 1 ? PF_W | PF_E : PF_WEC; return 1; } return 0; }
        if (key == A_length && o->cls == OC_ARRAY) { *val = cb_num(o->n1); *flags = o->sealed ? 0 : PF_W; return 1; } break; }
    case OC_STRING: { u32 i; if (cb_key_index(key, &i) && i < cb_s(o->v1)->len) { *val = str_char(R, cb_s(o->v1), i); *flags = PF_E; return 1; } if (key == A_length) { *val = cb_num(cb_s(o->v1)->len); *flags = 0; return 1; } break; }
    case OC_TYPED: { u32 i; if (cb_key_index(key, &i) && i < o->n2) { *val = cb_typed_get(R, o, i); *flags = PF_W | PF_E | PF_C; return 1; } break; }
    case OC_PROXY: {
        cbv t = trap(R, o, "getOwnPropertyDescriptor"); if (t == CB_EMPTY) return -1;
        if (cb_isundef(t)) return cb_isobj(o->v1) ? cb_get_own_desc(R, cb_o(o->v1), key, val, flags, get, set) : 0;
        cbv a[2] = { o->v1, key }; cbv d = cb_call_v(R, t, o->v2, 2, a); if (d == CB_EMPTY) return -1; if (!cb_isobj(d)) return 0;
        cbv g = cb_get(R, d, A_get), s = cb_get(R, d, A_set);
        if (!cb_isundef(g) || !cb_isundef(s)) { *get = g; *set = s; *flags = PF_ACC; } else { *val = cb_get(R, d, A_value); if (cb_truthy(cb_get(R, d, A_writable))) *flags |= PF_W; }
        if (cb_truthy(cb_get(R, d, A_enumerable))) *flags |= PF_E; if (cb_truthy(cb_get(R, d, A_configurable))) *flags |= PF_C; return 1; } }
    cb_prop *p = cb_own(o, key); if (!p) return 0;
    *flags = p->flags; if (p->flags & PF_ACC) { cb_acc *a = (cb_acc *)p->val; *get = a->get; *set = a->set; } else *val = p->val; return 1; }
cbv cb_array_like(cb_rt *R, cbv v, u32 *n, cbv **out) {
    *n = 0; *out = 0; if (cb_isnullish(v)) return CB_UNDEF;
    if (!cb_isobj(v)) return cb_throw(R, 1, "CreateListFromArrayLike called on non-object");
    cb_obj *o = cb_o(v);
    if (o->cls == OC_ARRAY || o->cls == OC_ARGS) { u32 len = o->n1; cbv *e = cb_blob((u64)len * 8 + 8); for (u32 i = 0; i < len; i++) e[i] = i < o->n2 && o->e[i] != CB_EMPTY ? o->e[i] : cb_get(R, v, cb_idx_key(R, i)); *n = len; *out = e; return v; }
    cbv l = cb_get(R, v, A_length); if (l == CB_EMPTY) return l; double d = cb_to_int(R, l); if (d < 0) d = 0; if (d > 10000000) return cb_throw(R, 2, "Too many arguments");
    u32 len = (u32)d; cbv *e = cb_blob((u64)len * 8 + 8); for (u32 i = 0; i < len; i++) { e[i] = cb_get(R, v, cb_idx_key(R, i)); if (e[i] == CB_EMPTY) return CB_EMPTY; } *n = len; *out = e; return v; }
cbv cb_str_from_cp(cb_rt *R, u32 cp) { cb_str *s = cb_str_new(cp >= 0x10000 ? 2 : 1); if (cp >= 0x10000) { cp -= 0x10000; s->c[0] = (u16)(0xD800 + (cp >> 10)); s->c[1] = (u16)(0xDC00 + (cp & 1023)); } else { s->c[0] = (u16)cp; s->ascii = cp < 128; } return cb_strv(s); }
void *cb_opaque(cbv v, int cls) { if (!cb_isobj(v)) return 0; cb_obj *o = cb_o(v); return o->cls == OC_HOST && (int)o->n1 == cls ? o->p1 : 0; }
cbv cb_new_host(cb_rt *R, cb_obj *proto, void *p) { cb_obj *o = cb_new_obj(R, proto); o->cls = OC_HOST; o->p1 = p; return cb_objv(o); }
