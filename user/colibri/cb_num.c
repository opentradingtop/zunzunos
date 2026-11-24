/* Colibri: numeros <-> texto con aritmetica exacta de enteros grandes.
   - A texto: las cifras mas cortas que vuelven a dar el mismo double (algoritmo de Steele y White / "Dragon4"),
     y cifras exactas para toFixed, toExponential y toPrecision.
   - Desde texto: aproximacion y correccion comparando con el valor exacto (redondeo correcto). */
#include "cbi.h"

#define BW 220   /* palabras de 32 bits: hasta ~7000 bits */
typedef struct { u32 w[BW]; int n; } big;
static void b_set(big *a, u64 v) { a->n = 0; while (v) { a->w[a->n++] = (u32)v; v >>= 32; } }
static void b_mul(big *a, u32 m) { u64 c = 0; for (int i = 0; i < a->n; i++) { c += (u64)a->w[i] * m; a->w[i] = (u32)c; c >>= 32; } if (c && a->n < BW) a->w[a->n++] = (u32)c; }
static void b_add(big *a, const big *b) { u64 c = 0; int n = a->n > b->n ? a->n : b->n; for (int i = 0; i < n; i++) { c += (u64)(i < a->n ? a->w[i] : 0) + (i < b->n ? b->w[i] : 0); a->w[i] = (u32)c; c >>= 32; } a->n = n; if (c && a->n < BW) a->w[a->n++] = (u32)c; }
static void b_sub(big *a, const big *b) { i64 c = 0; for (int i = 0; i < a->n; i++) { c += (i64)a->w[i] - (i < b->n ? b->w[i] : 0); a->w[i] = (u32)c; c >>= 32; } while (a->n && !a->w[a->n - 1]) a->n--; }
static int b_cmp(const big *a, const big *b) { if (a->n != b->n) return a->n < b->n ? -1 : 1; for (int i = a->n - 1; i >= 0; i--) if (a->w[i] != b->w[i]) return a->w[i] < b->w[i] ? -1 : 1; return 0; }
static void b_shl(big *a, int s) { if (!a->n || s <= 0) return; int ws = s / 32, bs = s % 32; if (a->n + ws + 1 > BW) ws = BW - a->n - 1;
    if (bs) { u32 c = 0; for (int i = 0; i < a->n; i++) { u32 x = a->w[i]; a->w[i] = x << bs | c; c = x >> (32 - bs); } if (c) a->w[a->n++] = c; }
    if (ws) { for (int i = a->n - 1; i >= 0; i--) a->w[i + ws] = a->w[i]; for (int i = 0; i < ws; i++) a->w[i] = 0; a->n += ws; } }
static void b_pow10(big *a, int k) { while (k >= 9) { b_mul(a, 1000000000); k -= 9; } static const u32 P[] = { 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000 }; if (k) b_mul(a, P[k]); }
static int b_divdig(big *r, const big *s) {   /* r < 10*s: cifra = r / s, r = r % s */
    int d = 0; while (b_cmp(r, s) >= 0) { b_sub(r, s); d++; } return d; }
static void split(double v, u64 *f, int *e) { union { double d; u64 u; } c = { v }; u64 b = c.u & ((1ULL << 63) - 1); int ex = (int)(b >> 52); u64 m = b & ((1ULL << 52) - 1); if (ex) { m |= 1ULL << 52; *e = ex - 1075; } else *e = -1074; *f = m; }

/* cifras mas cortas: dig (sin punto), devuelve cuantas; *k = posicion del punto decimal (valor = 0.d1d2... * 10^k) */
int cb_dtoa_short(double v, char *dig, int *kout) {
    u64 f; int e; split(v, &f, &e); if (!f) { dig[0] = '0'; *kout = 1; return 1; }
    big r, s, mp, mm; int even = !(f & 1); int lowest = f == (1ULL << 52) && e > -1074;
    if (e >= 0) { if (!lowest) { b_set(&r, f); b_shl(&r, e + 1); b_set(&s, 2); b_set(&mp, 1); b_shl(&mp, e); b_set(&mm, 1); b_shl(&mm, e); }
                  else { b_set(&r, f); b_shl(&r, e + 2); b_set(&s, 4); b_set(&mp, 1); b_shl(&mp, e + 1); b_set(&mm, 1); b_shl(&mm, e); } }
    else { if (!lowest) { b_set(&r, f); b_shl(&r, 1); b_set(&s, 1); b_shl(&s, 1 - e); b_set(&mp, 1); b_set(&mm, 1); }
           else { b_set(&r, f); b_shl(&r, 2); b_set(&s, 1); b_shl(&s, 2 - e); b_set(&mp, 2); b_set(&mm, 1); } }
    int bl = 64 - __builtin_clzll(f); int k = (int)__builtin_ceil((e + bl - 1) * 0.30102999566398114 - 1e-10);
    if (k >= 0) b_pow10(&s, k); else { b_pow10(&r, -k); b_pow10(&mp, -k); b_pow10(&mm, -k); }
    /* ajuste: si r + m+ >= s la primera cifra esta una posicion mas arriba */
    { big t = r; b_add(&t, &mp); int c = b_cmp(&t, &s); if (even ? c >= 0 : c > 0) k++; else { b_mul(&r, 10); b_mul(&mp, 10); b_mul(&mm, 10); } }
    int n = 0;
    for (;;) {
        int d = b_divdig(&r, &s);
        int c1 = b_cmp(&r, &mm); int low = even ? c1 <= 0 : c1 < 0;
        big t = r; b_add(&t, &mp); int c2 = b_cmp(&t, &s); int high = even ? c2 >= 0 : c2 > 0;
        if (!low && !high) { dig[n++] = (char)('0' + d); b_mul(&r, 10); b_mul(&mp, 10); b_mul(&mm, 10); if (n > 30) break; continue; }
        if (low && high) { big t2 = r; b_shl(&t2, 1); int c3 = b_cmp(&t2, &s); if (c3 > 0 || (c3 == 0 && (d & 1))) d++; }
        else if (high) d++;
        if (d == 10) {   /* acarreo */
            int i = n; while (i > 0 && dig[i - 1] == '9') i--; if (i == 0) { dig[0] = '1'; n = 1; k++; } else { dig[i - 1]++; n = i; } break; }
        dig[n++] = (char)('0' + d); break; }
    while (n > 1 && dig[n - 1] == '0') n--;
    *kout = k; return n; }
/* cifras exactas: hasta nd cifras significativas (mode 0) o hasta fd decimales (mode 1), redondeando a la mitad hacia arriba.
   Devuelve cuantas cifras y *k como arriba. */
int cb_dtoa_fixed(double v, int mode, int nd, char *dig, int *kout) {
    u64 f; int e; split(v, &f, &e);
    if (!f) { int n = mode ? 0 : nd; if (mode) { *kout = 1; dig[0] = '0'; return 1; } for (int i = 0; i < n; i++) dig[i] = '0'; *kout = 1; return n; }
    big r, s; b_set(&r, f); b_set(&s, 1); if (e >= 0) b_shl(&r, e); else b_shl(&s, -e);
    int bl = 64 - __builtin_clzll(f); int k = (int)__builtin_ceil((e + bl - 1) * 0.30102999566398114 - 1e-10);
    if (k >= 0) b_pow10(&s, k); else b_pow10(&r, -k);
    if (b_cmp(&r, &s) >= 0) k++; else b_mul(&r, 10);
    int want = mode ? k + nd : nd; int n = 0;
    if (want <= 0) {   /* el numero es menor que la ultima cifra pedida */
        if (want < 0) { *kout = k; return 0; }
        big t = r; /* r ya esta multiplicado: la primera cifra esta en r/s; redondea si r/s >= 5 */
        int d = b_divdig(&t, &s); if (d >= 5) { dig[0] = '1'; *kout = k + 1; return 1; } *kout = k; return 0; }
    if (want > 1100) want = 1100;
    while (n < want) { int d = b_divdig(&r, &s); dig[n++] = (char)('0' + d); b_mul(&r, 10); }
    /* resto: r/(10 s); redondeo hacia arriba en la mitad */
    big t = s; b_mul(&t, 5);
    if (b_cmp(&r, &t) >= 0) { int i = n; while (i > 0 && dig[i - 1] == '9') { dig[i - 1] = '0'; i--; } if (i == 0) { memmove(dig + 1, dig, n); dig[0] = '1'; k++; if (!mode) n--; else n++; } else dig[i - 1]++; }
    *kout = k; return n; }

/* texto (ya limpio: cifras, punto, e) -> double con redondeo correcto */
double cb_parse_number(const char *s, int len) {
    char dg[820]; int nd = 0, dp = -1, sticky = 0; long ex = 0; int i = 0, neg = 0;
    if (i < len && (s[i] == '+' || s[i] == '-')) { neg = s[i] == '-'; i++; }
    int lead = 1;
    for (; i < len; i++) { char c = s[i];
        if (c == '.') { if (dp >= 0) break; dp = nd; continue; }
        if (c < '0' || c > '9') break;
        if (c == '0' && lead) { if (dp >= 0) ex--; continue; }
        lead = 0; if (nd < 800) dg[nd++] = c; else { if (c != '0') sticky = 1; if (dp < 0) ex++; } }
    if (dp >= 0 && dp < nd) ex -= nd - dp;   /* cifras despues del punto */
    if (i < len && (s[i] | 32) == 'e') { i++; int en = 0; long ev = 0; if (i < len && (s[i] == '+' || s[i] == '-')) { en = s[i] == '-'; i++; } while (i < len && s[i] >= '0' && s[i] <= '9') { if (ev < 100000) ev = ev * 10 + s[i] - '0'; i++; } ex += en ? -ev : ev; }
    while (nd > 0 && dg[nd - 1] == '0' && !sticky) { nd--; ex++; }
    if (sticky && nd < 820) dg[nd++] = '1', ex--;
    if (!nd) return neg ? -0.0 : 0.0;
    if (nd + ex > 310) return neg ? -__builtin_inf() : __builtin_inf();
    if (nd + ex < -330) return neg ? -0.0 : 0.0;
    /* aproximacion con long double */
    long double a = 0; for (int j = 0; j < nd && j < 19; j++) a = a * 10 + (dg[j] - '0'); long e10 = ex + (nd > 19 ? nd - 19 : 0);
    while (e10 < -280) { a /= 1e280L; e10 += 280; } while (e10 > 280) { a *= 1e280L; e10 -= 280; }   /* sin desbordar aunque long double sea double */
    long double p10 = 1, b10 = 10; long ee = e10 < 0 ? -e10 : e10; while (ee) { if (ee & 1) p10 *= b10; b10 *= b10; ee >>= 1; }
    a = e10 < 0 ? a / p10 : a * p10; double z = (double)a;
    if (z == 0) z = 4.9406564584124654e-324; if (__builtin_isinf(z)) z = 1.7976931348623157e308;
    /* correccion exacta: compara D*10^ex con los puntos medios alrededor de z */
    big D; D.n = 0; for (int j = 0; j < nd; j++) { b_mul(&D, 10); big t; b_set(&t, (u64)(dg[j] - '0')); b_add(&D, &t); }
    for (int it = 0; it < 64; it++) {
        u64 f; int e; split(z, &f, &e);
        /* punto medio superior: (2f+1) * 2^(e-1) ; comparar X = D*10^ex  con M */
        big X = D, M; b_set(&M, 2 * f + 1); int e2 = e - 1;
        if (ex >= 0) b_pow10(&X, (int)ex); else b_pow10(&M, (int)-ex);
        if (e2 >= 0) b_shl(&M, e2); else b_shl(&X, -e2);
        int cu = b_cmp(&X, &M);
        big X2 = D, M2; b_set(&M2, 2 * f - 1); int e3 = e - 1; if (f == (1ULL << 52) && e > -1074) { b_set(&M2, 4 * f - 1); e3 = e - 2; }
        if (ex >= 0) b_pow10(&X2, (int)ex); else b_pow10(&M2, (int)-ex);
        if (e3 >= 0) b_shl(&M2, e3); else b_shl(&X2, -e3);
        int cl = b_cmp(&X2, &M2);
        if (cu > 0 || (cu == 0 && (f & 1))) { z = __builtin_nextafter(z, __builtin_inf()); if (__builtin_isinf(z)) break; continue; }
        if (cl < 0 || (cl == 0 && (f & 1))) { double y = __builtin_nextafter(z, 0); if (y == 0 && cl < 0) { /* por debajo de la mitad del menor */ big h; b_set(&h, 1); big X3 = D; if (ex >= 0) b_pow10(&X3, (int)ex); else b_pow10(&h, (int)-ex); b_shl(&X3, 1075); if (b_cmp(&X3, &h) <= 0) { z = 0; break; } } z = y; if (z == 0) break; continue; }
        break; }
    return neg ? -z : z; }

/* JavaScript: Number::toString(10) */
int cb_fmt_num(double d, char *o) {
    if (d != d) { strcpy(o, "NaN"); return 3; }
    if (d == 0) { strcpy(o, "0"); return 1; }
    if (__builtin_isinf(d)) { strcpy(o, d < 0 ? "-Infinity" : "Infinity"); return (int)strlen(o); }
    int n = 0; if (d < 0) { o[n++] = '-'; d = -d; }
    char dig[40]; int k; int nd = cb_dtoa_short(d, dig, &k);
    if (nd <= k && k <= 21) { memcpy(o + n, dig, nd); n += nd; for (int i = nd; i < k; i++) o[n++] = '0'; }
    else if (0 < k && k <= 21) { memcpy(o + n, dig, k); n += k; o[n++] = '.'; memcpy(o + n, dig + k, nd - k); n += nd - k; }
    else if (-6 < k && k <= 0) { o[n++] = '0'; o[n++] = '.'; for (int i = 0; i < -k; i++) o[n++] = '0'; memcpy(o + n, dig, nd); n += nd; }
    else { o[n++] = dig[0]; if (nd > 1) { o[n++] = '.'; memcpy(o + n, dig + 1, nd - 1); n += nd - 1; } n += cb_snprintf(o + n, 16, "e%c%d", k - 1 >= 0 ? '+' : '-', k - 1 >= 0 ? k - 1 : 1 - k); }
    o[n] = 0; return n; }
