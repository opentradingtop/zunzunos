/* Colibri: BigInt (enteros de cualquier tamano). Valor etiquetado 0xFFFF con puntero a una celda CT_BIG:
   signo y magnitud en palabras de 32 bits (la menos significativa primero). */
#include "cbi.h"
#define NF(name) static cbv name(cb_rt *R, cbv t, int argc, cbv *argv, cb_obj *fn)
#define ARG(i) ((i) < argc ? argv[i] : CB_UNDEF)
typedef struct { cb_cell h; u8 neg; u32 n; u32 w[]; } big;
static big *bnew(u32 n) { big *b = cb_alloc(CT_BIG, sizeof(big) + (u64)(n ? n : 1) * 4); b->n = n; return b; }
static void trim(big *b) { while (b->n && !b->w[b->n - 1]) b->n--; if (!b->n) b->neg = 0; }
static cbv bval(big *b) { trim(b); return ((cbv)TAG_SPEC << 48) | (u64)b; }
static big *B(cbv v) { return (big *)CB_PTR(v); }
int cb_isbig(cbv v) { return CB_TAG(v) == TAG_SPEC && (v & 0xFFFFFFFFFFFFULL); }
cbv cb_big_from_double(cb_rt *R, double d) {
    int neg = d < 0; if (neg) d = -d; u32 n = 0; double t = d; while (t >= 1) { t /= 4294967296.0; n++; }
    big *b = bnew(n); for (u32 i = 0; i < n; i++) { double q = __builtin_floor(d / 4294967296.0); b->w[i] = (u32)(d - q * 4294967296.0); d = q; } b->neg = (u8)neg; return bval(b); }
double cb_big_to_double(cbv v) { big *b = B(v); double d = 0; for (u32 i = b->n; i-- > 0;) d = d * 4294967296.0 + b->w[i]; return b->neg ? -d : d; }
static int mcmp(const big *a, const big *b) { if (a->n != b->n) return a->n < b->n ? -1 : 1; for (u32 i = a->n; i-- > 0;) if (a->w[i] != b->w[i]) return a->w[i] < b->w[i] ? -1 : 1; return 0; }
static big *madd(const big *a, const big *b) { u32 n = (a->n > b->n ? a->n : b->n) + 1; big *r = bnew(n); u64 c = 0; for (u32 i = 0; i < n; i++) { c += (u64)(i < a->n ? a->w[i] : 0) + (i < b->n ? b->w[i] : 0); r->w[i] = (u32)c; c >>= 32; } return r; }
static big *msub(const big *a, const big *b) { big *r = bnew(a->n); i64 c = 0; for (u32 i = 0; i < a->n; i++) { c += (i64)a->w[i] - (i < b->n ? b->w[i] : 0); r->w[i] = (u32)c; c >>= 32; } return r; }   /* |a| >= |b| */
static big *mmul(const big *a, const big *b) { big *r = bnew(a->n + b->n); for (u32 i = 0; i < a->n; i++) { u64 c = 0; for (u32 j = 0; j < b->n; j++) { c += (u64)a->w[i] * b->w[j] + r->w[i + j]; r->w[i + j] = (u32)c; c >>= 32; } r->w[i + b->n] = (u32)c; } return r; }
static u32 mdivw(big *a, u32 d) { u64 r = 0; for (u32 i = a->n; i-- > 0;) { r = r << 32 | a->w[i]; a->w[i] = (u32)(r / d); r %= d; } trim(a); return (u32)r; }   /* en el sitio */
static big *bdup(const big *a) { big *r = bnew(a->n); memcpy(r->w, a->w, (u64)a->n * 4); r->neg = a->neg; return r; }
static u32 bitlen(const big *a) { if (!a->n) return 0; return (a->n - 1) * 32 + (32 - __builtin_clz(a->w[a->n - 1])); }
static void mdivmod(const big *a, const big *b, big **q, big **r) {   /* division por bits (sencilla) */
    if (b->n == 1) { big *qq = bdup(a); qq->neg = 0; u32 rm = mdivw(qq, b->w[0]); big *rr = bnew(1); rr->w[0] = rm; if (q) *q = qq; if (r) *r = rr; return; }
    if (mcmp(a, b) < 0) { if (q) *q = bnew(0); if (r) { *r = bdup(a); (*r)->neg = 0; } return; }
    u32 nb = bitlen(a); big *qq = bnew(a->n), *rr = bnew(b->n + 1); rr->n = 0;
    for (u32 i = nb; i-- > 0;) {
        u32 c = (a->w[i / 32] >> (i % 32)) & 1; u32 carry = c;   /* rr = rr*2 + bit */
        for (u32 k = 0; k < rr->n; k++) { u32 x = rr->w[k]; rr->w[k] = x << 1 | carry; carry = x >> 31; } if (carry) rr->w[rr->n++] = carry;
        if (mcmp(rr, b) >= 0) { i64 cc = 0; for (u32 k = 0; k < rr->n; k++) { cc += (i64)rr->w[k] - (k < b->n ? b->w[k] : 0); rr->w[k] = (u32)cc; cc >>= 32; } while (rr->n && !rr->w[rr->n - 1]) rr->n--; qq->w[i / 32] |= 1u << (i % 32); } }
    trim(qq); if (q) *q = qq; if (r) *r = rr; }
static big *sadd(const big *a, const big *b) {   /* con signo */
    if (a->neg == b->neg) { big *r = madd(a, b); r->neg = a->neg; return r; }
    int c = mcmp(a, b); if (c == 0) return bnew(0);
    if (c > 0) { big *r = msub(a, b); r->neg = a->neg; return r; } big *r = msub(b, a); r->neg = b->neg; return r; }
static big *bneg(const big *a) { big *r = bdup(a); r->neg = !a->neg; trim(r); return r; }
static big *small(i64 v) { big *b = bnew(2); u64 m = v < 0 ? (u64)(-v) : (u64)v; b->w[0] = (u32)m; b->w[1] = (u32)(m >> 32); b->neg = v < 0; trim(b); return b; }
/* complemento a dos sobre n palabras */
static void to2c(const big *a, u32 *out, u32 n) { for (u32 i = 0; i < n; i++) out[i] = i < a->n ? a->w[i] : 0; if (a->neg) { u64 c = 1; for (u32 i = 0; i < n; i++) { c += (u32)~out[i]; out[i] = (u32)c; c >>= 32; } } }
static big *from2c(const u32 *in, u32 n) { big *r = bnew(n); int neg = n && (in[n - 1] >> 31); memcpy(r->w, in, (u64)n * 4); if (neg) { u64 c = 1; for (u32 i = 0; i < n; i++) { c += (u32)~r->w[i]; r->w[i] = (u32)c; c >>= 32; } r->neg = 1; } return r; }
static big *bshl(const big *a, i64 s) {
    if (s < 0) {   /* desplazamiento aritmetico a la derecha (redondea hacia -infinito) */
        u64 sh = (u64)-s; u32 ws = (u32)(sh / 32), bs = (u32)(sh % 32);
        if (ws >= a->n) return a->neg ? small(-1) : bnew(0);
        big *r = bnew(a->n - ws); int lost = 0; for (u32 i = 0; i < ws; i++) if (a->w[i]) lost = 1; if (bs && (a->w[ws] & ((1u << bs) - 1))) lost = 1;
        for (u32 i = 0; i < r->n; i++) { u64 x = a->w[i + ws]; if (bs) x = (x >> bs) | (i + ws + 1 < a->n ? (u64)a->w[i + ws + 1] << (32 - bs) : 0); r->w[i] = (u32)x; }
        r->neg = a->neg; trim(r); if (a->neg && lost) { big *one = small(1); r = madd(r, one); r->neg = 1; trim(r); } return r; }
    u32 ws = (u32)(s / 32), bs = (u32)(s % 32); big *r = bnew(a->n + ws + 1);
    for (u32 i = 0; i < a->n; i++) { u64 x = (u64)a->w[i] << bs; r->w[i + ws] |= (u32)x; r->w[i + ws + 1] |= (u32)(x >> 32); } r->neg = a->neg; return r; }
cbv cb_big_from_str(cb_rt *R, cb_str *s, int allow_ws) {   /* CB_EMPTY si no es valida (sin lanzar) */
    s = cb_flat(s); u32 i = 0, e = s->len;
    if (allow_ws) { while (i < e && (s->c[i] == ' ' || (s->c[i] >= 9 && s->c[i] <= 13) || s->c[i] == 0xA0 || s->c[i] == 0xFEFF)) i++; while (e > i && (s->c[e - 1] == ' ' || (s->c[e - 1] >= 9 && s->c[e - 1] <= 13) || s->c[e - 1] == 0xA0)) e--; if (i == e) return bval(bnew(0)); }
    int neg = 0, base = 10;
    if (i + 1 < e && s->c[i] == '0' && ((s->c[i + 1] | 32) == 'x' || (s->c[i + 1] | 32) == 'o' || (s->c[i + 1] | 32) == 'b')) { base = (s->c[i + 1] | 32) == 'x' ? 16 : (s->c[i + 1] | 32) == 'o' ? 8 : 2; i += 2; }
    else if (allow_ws && i < e && (s->c[i] == '+' || s->c[i] == '-')) { neg = s->c[i] == '-'; i++; }
    if (i == e) return CB_EMPTY;
    big *r = bnew(1); r->n = 0;
    for (; i < e; i++) { u16 c = s->c[i]; if (c == '_' && !allow_ws) continue; int d = c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : 99; if (d >= base) return CB_EMPTY;
        big *n2 = bnew(r->n + 1); u64 cc = (u64)d; for (u32 k = 0; k < r->n; k++) { cc += (u64)r->w[k] * base; n2->w[k] = (u32)cc; cc >>= 32; } n2->w[r->n] = (u32)cc; n2->n = r->n + 1; trim(n2); r = n2; }
    r->neg = (u8)neg; return bval(r); }
cbv cb_big_to_str(cb_rt *R, cbv v, int radix) {
    big *a = bdup(B(v)); a->neg = 0; if (!a->n) return cb_mkstr(R, "0");
    char *buf = malloc((u64)a->n * 33 + 3); int k = 0;
    while (a->n) { u32 r = mdivw(a, (u32)radix); buf[k++] = (char)(r < 10 ? '0' + r : 'a' + r - 10); }
    if (B(v)->neg) buf[k++] = '-'; for (int i = 0; i < k / 2; i++) { char x = buf[i]; buf[i] = buf[k - 1 - i]; buf[k - 1 - i] = x; } buf[k] = 0;
    cbv s = cb_mkstr(R, buf); free(buf); return s; }
int cb_big_cmp(cbv a, cbv b) { big *x = B(a), *y = B(b); if (x->neg != y->neg) return x->neg ? -1 : 1; int c = mcmp(x, y); return x->neg ? -c : c; }
int cb_big_cmp_num(cbv a, double d) {   /* -1, 0, 1; 2 si d es NaN */
    if (d != d) return 2; if (__builtin_isinf(d)) return d > 0 ? -1 : 1;
    double fl = __builtin_floor(d); cbv bd = cb_big_from_double(0, fl); int c = cb_big_cmp(a, bd); if (c) return c; return d > fl ? -1 : 0; }
int cb_big_truthy(cbv v) { return B(v)->n != 0; }
u32 cb_big_hash(cbv v) { big *b = B(v); u32 h = b->neg ? 7 : 3; for (u32 i = 0; i < b->n; i++) h = h * 16777619u ^ b->w[i]; return h; }
/* operaciones: op de cbi.h (O_ADD...). CB_EMPTY si lanza */
cbv cb_big_op(cb_rt *R, int op, cbv av, cbv bv) {
    big *a = B(av), *b = bv ? B(bv) : 0;
    switch (op) {
    case O_ADD: return bval(sadd(a, b));
    case O_SUB: return bval(sadd(a, bneg(b)));
    case O_MUL: { big *r = mmul(a, b); r->neg = a->neg != b->neg; return bval(r); }
    case O_DIV: case O_MOD: { if (!b->n) return cb_throw(R, 2, "Division by zero"); big *q, *r; mdivmod(a, b, &q, &r); if (op == O_DIV) { q->neg = a->neg != b->neg; return bval(q); } r->neg = a->neg; return bval(r); }
    case O_EXP: { if (b->neg) return cb_throw(R, 2, "Exponent must be non-negative"); if (b->n > 1) { if (a->n == 0 || (a->n == 1 && a->w[0] == 1)) return bval(a->neg && (b->w[0] & 1) ? bdup(a) : small(a->n ? 1 : 0)); return cb_throw(R, 2, "Maximum BigInt size exceeded"); }
        u64 e = b->n ? b->w[0] : 0; big *r = small(1), *base = bdup(a); base->neg = 0; while (e) { if (e & 1) r = mmul(r, base); e >>= 1; if (e) base = mmul(base, base); trim(r); trim(base); if (r->n > 100000) return cb_throw(R, 2, "Maximum BigInt size exceeded"); } r->neg = a->neg && (b->n && (b->w[0] & 1)); return bval(r); }
    case O_SHL: case O_SAR: { if (b->n > 1) return cb_throw(R, 2, "Maximum BigInt size exceeded"); i64 s = b->n ? (i64)b->w[0] : 0; if (b->neg) s = -s; if (op == O_SAR) s = -s; if (s > 1000000) return cb_throw(R, 2, "Maximum BigInt size exceeded"); return bval(bshl(a, s)); }
    case O_SHR: return cb_throw(R, 1, "BigInts have no unsigned right shift, use >> instead");
    case O_AND: case O_OR: case O_XOR: { u32 n = (a->n > b->n ? a->n : b->n) + 1; u32 *x = malloc(n * 4), *y = malloc(n * 4); to2c(a, x, n); to2c(b, y, n); for (u32 i = 0; i < n; i++) x[i] = op == O_AND ? x[i] & y[i] : op == O_OR ? x[i] | y[i] : x[i] ^ y[i]; big *r = from2c(x, n); free(x); free(y); return bval(r); }
    case O_NEG: return bval(bneg(a));
    case O_BITNOT: { big *one = small(1); return bval(bneg(sadd(a, one))); }
    case O_INC: return bval(sadd(a, small(1)));
    case O_DEC: return bval(sadd(a, small(-1))); }
    return cb_throw(R, 1, "operacion no valida con BigInt"); }
static cbv asN(cb_rt *R, cbv v, double bits, int sign) {
    u32 nb = (u32)bits; u32 n = (nb + 31) / 32 + 1; u32 *x = calloc(n + 1, 4); to2c(B(v), x, n);
    if (nb % 32) x[nb / 32] &= (1u << (nb % 32)) - 1; for (u32 i = (nb + 31) / 32; i < n; i++) x[i] = 0;
    big *r;
    if (sign && nb && ((x[(nb - 1) / 32] >> ((nb - 1) % 32)) & 1)) { /* negativo: restar 2^bits */ big *u = bnew(n); memcpy(u->w, x, n * 4); trim(u); big *p = bshl(small(1), nb); r = sadd(u, bneg(p)); }
    else { r = bnew(n); memcpy(r->w, x, n * 4); }
    free(x); return bval(r); }
static cbv thisbig(cb_rt *R, cbv t) { if (cb_isbig(t)) return t; if (cb_isobj(t) && cb_isbig(cb_o(t)->v1)) return cb_o(t)->v1; return cb_throw(R, 1, "BigInt.prototype method requires that 'this' be a BigInt"); }
cbv cb_to_big(cb_rt *R, cbv v) {
    v = cb_to_prim(R, v, 1); if (v == CB_EMPTY) return v;
    if (cb_isbig(v)) return v;
    if (cb_isbool(v)) return bval(small((i64)(v & 1)));
    if (cb_isstr(v)) { cbv r = cb_big_from_str(R, cb_s(v), 1); if (r == CB_EMPTY) { char *u = cb_str_utf8z(cb_s(v), 0); cbv e = cb_throw(R, 3, "Cannot convert %s to a BigInt", u); free(u); return e; } return r; }
    if (cb_isnum(v)) { double d = cb_d(v); if (d != d || __builtin_isinf(d) || __builtin_trunc(d) != d) { char nb[40]; extern int cb_fmt_num(double, char *); cb_fmt_num(d, nb); return cb_throw(R, 2, "The number %s cannot be converted to a BigInt because it is not an integer", nb); } return cb_big_from_double(R, d); }
    return cb_throw(R, 1, "Cannot convert value to a BigInt"); }
NF(BigInt_ctor) { if (!cb_isundef(R->nt)) return cb_throw(R, 1, "BigInt is not a constructor"); return cb_to_big(R, ARG(0)); }
NF(BigInt_asIntN) { double n = cb_to_int(R, ARG(0)); if (R->has_exc) return CB_EMPTY; if (n < 0 || n > 1e6) return cb_throw(R, 2, "Invalid value: not (convertible to) a safe integer"); cbv b = cb_to_big(R, ARG(1)); if (b == CB_EMPTY) return b; return asN(R, b, n, 1); }
NF(BigInt_asUintN) { double n = cb_to_int(R, ARG(0)); if (R->has_exc) return CB_EMPTY; if (n < 0 || n > 1e6) return cb_throw(R, 2, "Invalid value: not (convertible to) a safe integer"); cbv b = cb_to_big(R, ARG(1)); if (b == CB_EMPTY) return b; return asN(R, b, n, 0); }
NF(BIP_toString) { cbv b = thisbig(R, t); if (b == CB_EMPTY) return b; int radix = cb_isundef(ARG(0)) ? 10 : (int)cb_to_int(R, ARG(0)); if (radix < 2 || radix > 36) return cb_throw(R, 2, "toString() radix must be between 2 and 36"); return cb_big_to_str(R, b, radix); }
NF(BIP_valueOf) { return thisbig(R, t); }
void cb_big_init(cb_rt *R) {
    R->BigIntP = cb_new_obj(R, R->ObjectP);
    cbv c = cb_new_native(R, "BigInt", BigInt_ctor, 1); cb_obj *co = cb_o(c);
    cb_add_prop(R, co, A_prototype, cb_objv(R->BigIntP), 0); cb_add_prop(R, R->BigIntP, A_constructor, c, PF_W | PF_C); cb_add_prop(R, R->global, cb_atom(R, "BigInt"), c, PF_W | PF_C);
    cb_defn(R, co, "asIntN", BigInt_asIntN, 2); cb_defn(R, co, "asUintN", BigInt_asUintN, 2);
    cb_defn(R, R->BigIntP, "toString", BIP_toString, 0); cb_defn(R, R->BigIntP, "toLocaleString", BIP_toString, 0); cb_defn(R, R->BigIntP, "valueOf", BIP_valueOf, 0);
    cb_add_prop(R, R->BigIntP, S_toStringTag, cb_atom(R, "BigInt"), PF_C); }
