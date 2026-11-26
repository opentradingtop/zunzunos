/* Colibri: matematicas propias en precision extendida (unidad x87): seno, coseno, tangente con reduccion exacta del
   argumento, exponenciales y logaritmos, potencias exactas para exponentes enteros, funciones hiperbolicas...
   Ademas: mayusculas/minusculas Unicode, descomposicion de acentos y el generador de numeros aleatorios. */
#include "cbi.h"
typedef long double ld;
/* ---------------- matematicas ---------------- */
static const ld LN2 = 0.693147180559945309417232121458176568L, LG2 = 0.301029995663981195213738894724493027L, LOG2E = 1.44269504088896340735992468100189214L;
static inline ld x_yl2x(ld x, ld y) { ld r; __asm__("fyl2x" : "=t"(r) : "0"(x), "u"(y) : "st(1)"); return r; }      /* y * m_log2(x) */
static inline ld x_yl2xp1(ld x, ld y) { ld r; __asm__("fyl2xp1" : "=t"(r) : "0"(x), "u"(y) : "st(1)"); return r; } /* y * m_log2(1+x), |x| < 0.29 */
static inline ld x_2xm1(ld x) { __asm__("f2xm1" : "+t"(x)); return x; }                                              /* 2^x - 1, |x| <= 1 */
static inline ld x_rnd(ld x) { __asm__("frndint" : "+t"(x)); return x; }
static inline ld x_scale(ld x, ld e) { ld r; __asm__("fscale" : "=t"(r) : "0"(x), "u"(e)); return r; }
static inline ld x_sqrt(ld x) { __asm__("fsqrt" : "+t"(x)); return x; }
static inline ld x_sin(ld x) { __asm__("fsin" : "+t"(x)); return x; }
static inline ld x_cos(ld x) { __asm__("fcos" : "+t"(x)); return x; }
static inline ld x_tan(ld x) { ld one; __asm__("fptan" : "=t"(one), "=u"(x) : "0"(x)); return x; }
static inline ld x_atan2(ld y, ld x) { ld r; __asm__("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)"); return r; }
static inline ld x_prem(ld x, ld y) { ld r; __asm__("1: fprem\n fnstsw %%ax\n testw $0x400, %%ax\n jnz 1b" : "=t"(r) : "0"(x), "u"(y) : "ax", "cc"); return r; }
static ld exp2_l(ld x) {
    if (x != x) return x; if (x > 16400) return (ld)__builtin_inf(); if (x < -16500) return 0;
    ld i = x_rnd(x), f = x - i; return x_scale(x_2xm1(f) + 1, i); }
static ld log2_l(ld x) { return x_yl2x(x, 1.0L); }

static double m_sqrt(double x) { double r; __asm__("sqrtsd %1, %0" : "=x"(r) : "x"(x)); return r; }
static float m_sqrtf(float x) { float r; __asm__("sqrtss %1, %0" : "=x"(r) : "x"(x)); return r; }
static double m_fabs(double x) { return __builtin_fabs(x); }
static float m_fabsf(float x) { return __builtin_fabsf(x); }
static double m_copysign(double x, double y) { return __builtin_copysign(x, y); }
static double m_trunc(double x) { if (!(__builtin_fabs(x) < 4503599627370496.0)) return x; double t = (double)(long)x; return m_copysign(t, x); }
static double m_floor(double x) { double t = m_trunc(x); if (t > x) t -= 1; return t == 0 ? m_copysign(0.0, x) : t; }
static double m_ceil(double x) { double t = m_trunc(x); if (t < x) t += 1; return t == 0 ? m_copysign(0.0, x) : t; }
static double m_round(double x) { double t = m_trunc(x); if (__builtin_fabs(x - t) >= 0.5) t += m_copysign(1.0, x); return t; }
static double m_rint(double x) { if (!(__builtin_fabs(x) < 4503599627370496.0)) return x; double r = (double)(x_rnd((ld)x)); return r == 0 ? m_copysign(0.0, x) : r; }
static double m_nearbyint(double x) { return m_rint(x); }
static long m_lrint(double x) { long r; __asm__("cvtsd2si %1, %0" : "=r"(r) : "x"(x)); return r; }
static double m_fmin(double a, double b) { if (a != a) return b; if (b != b) return a; if (a == b) return __builtin_signbit(a) ? a : b; return a < b ? a : b; }
static double m_fmax(double a, double b) { if (a != a) return b; if (b != b) return a; if (a == b) return __builtin_signbit(a) ? b : a; return a > b ? a : b; }
static double m_fmod(double x, double y) { if (x != x || y != y || __builtin_isinf(x) || y == 0) return __builtin_nan(""); if (__builtin_isinf(y)) return x; double r = (double)x_prem(x, y); return r == 0 ? m_copysign(0.0, x) : r; }
static double m_modf(double x, double *ip) { double t = m_trunc(x); *ip = t; return __builtin_isinf(x) ? m_copysign(0.0, x) : m_copysign(x - t, x); }
static double m_ldexp(double x, int e) { if (x == 0 || !__builtin_isfinite(x)) return x; return (double)x_scale(x, e); }
static double m_scalbn(double x, int e) { return m_ldexp(x, e); }
static double m_frexp(double x, int *e) { *e = 0; if (x == 0 || !__builtin_isfinite(x)) return x; ld a = __builtin_fabs(x); int k = (int)x_rnd(log2_l(a)) + 1; ld m = x_scale(a, -k); while (m >= 1) { m /= 2; k++; } while (m < 0.5L) { m *= 2; k--; } *e = k; return m_copysign((double)m, x); }

static double m_exp2(double x) { return (double)exp2_l(x); }
static double m_exp(double x) { if (x != x) return x; return (double)exp2_l((ld)x * LOG2E); }
static double m_expm1(double x) {
    if (x != x || x == 0) return x; if (x > 710) return __builtin_inf(); if (x < -50) return -1;
    ld t = (ld)x * LOG2E; if (__builtin_fabs(x) < 0.5) return (double)x_2xm1(t); return (double)(exp2_l(t) - 1); }
static double m_log(double x) { if (x != x || x == __builtin_inf()) return x; if (x == 0) return -__builtin_inf(); if (x < 0) return __builtin_nan(""); return (double)x_yl2x(x, LN2); }
static double m_log2(double x) { if (x != x || x == __builtin_inf()) return x; if (x == 0) return -__builtin_inf(); if (x < 0) return __builtin_nan(""); return (double)log2_l(x); }
static double m_log10(double x) { if (x != x || x == __builtin_inf()) return x; if (x == 0) return -__builtin_inf(); if (x < 0) return __builtin_nan(""); return (double)x_yl2x(x, LG2); }
static double m_log1p(double x) {
    if (x != x || x == __builtin_inf() || x == 0) return x; if (x == -1) return -__builtin_inf(); if (x < -1) return __builtin_nan("");
    if (__builtin_fabs(x) < 0.29) return (double)x_yl2xp1(x, LN2); return (double)x_yl2x(1.0L + x, LN2); }
static int is_int(double y) { return __builtin_isfinite(y) && m_trunc(y) == y; }
static int is_odd(double y) { return is_int(y) && __builtin_fabs(y) < 9007199254740992.0 && ((long)y & 1); }
static double m_pow(double x, double y) {
    if (y == 0 || x == 1) return 1; if (x != x || y != y) return __builtin_nan("");
    if (x == 0) { if (y < 0) return is_odd(y) ? m_copysign(__builtin_inf(), x) : __builtin_inf(); return is_odd(y) ? x : 0.0; }
    if (__builtin_isinf(y)) { double a = __builtin_fabs(x); if (a == 1) return 1; return (a < 1) == (y > 0) ? 0.0 : __builtin_inf(); }
    if (__builtin_isinf(x)) { if (x > 0) return y < 0 ? 0.0 : __builtin_inf(); if (y < 0) return is_odd(y) ? -0.0 : 0.0; return is_odd(y) ? -__builtin_inf() : __builtin_inf(); }
    double sg = 1; if (x < 0) { if (!is_int(y)) return __builtin_nan(""); if (is_odd(y)) sg = -1; x = -x; }
    if (is_int(y) && __builtin_fabs(y) <= 64) {   /* potencias enteras pequenas: multiplicando, exactas cuando caben */
        long n = (long)__builtin_fabs(y); ld r = 1, b = x; while (n) { if (n & 1) r *= b; b *= b; n >>= 1; } if (y < 0) r = 1 / r; return sg * (double)r; }
    return sg * (double)exp2_l((ld)y * log2_l(x)); }
static double m_cbrt(double x) {
    if (x == 0 || !__builtin_isfinite(x)) return x; ld a = __builtin_fabs(x), r = exp2_l(log2_l(a) / 3); r = r - (r * r * r - a) / (3 * r * r); return m_copysign((double)r, x); }
static double m_hypot(double x, double y) { if (__builtin_isinf(x) || __builtin_isinf(y)) return __builtin_inf(); if (x != x || y != y) return __builtin_nan(""); return (double)x_sqrt((ld)x * x + (ld)y * y); }

/* seno, coseno y tangente: reduccion a [-pi/4, pi/4] con pi/2 partido en trozos de 33 bits (exacta hasta 2^31 vueltas) */
static ld u2ld(unsigned long u) { union { unsigned long u; double d; } c = { u }; return c.d; }
static ld reduce(double x, int *q) {
    ld P1 = u2ld(0x3FF921FB54400000ULL), P2 = u2ld(0x3DD0B4611A600000ULL), P3 = u2ld(0x3BA3198A2E000000ULL), P4 = u2ld(0x397B839A252049C1ULL);
    ld k = x_rnd((ld)x / (P1 + P2 + P3)); *q = (int)((long)k & 3); return (((ld)x - k * P1) - k * P2) - k * P3 - k * P4; }
static int big(double x) { return !(__builtin_fabs(x) < 3.0e9); }
static double m_sin(double x) { if (!__builtin_isfinite(x)) return __builtin_nan(""); if (x == 0) return x; if (big(x)) return (double)x_sin(x_prem(x, 6.28318530717958647692528676655900577L)); int q; ld r = reduce(x, &q); ld v = q == 0 ? x_sin(r) : q == 1 ? x_cos(r) : q == 2 ? -x_sin(r) : -x_cos(r); return (double)v; }
static double m_cos(double x) { if (!__builtin_isfinite(x)) return __builtin_nan(""); if (big(x)) return (double)x_cos(x_prem(x, 6.28318530717958647692528676655900577L)); int q; ld r = reduce(x, &q); ld v = q == 0 ? x_cos(r) : q == 1 ? -x_sin(r) : q == 2 ? -x_cos(r) : x_sin(r); return (double)v; }
static double m_tan(double x) { if (!__builtin_isfinite(x)) return __builtin_nan(""); if (x == 0) return x; if (big(x)) return (double)x_tan(x_prem(x, 3.14159265358979323846264338327950288L)); int q; ld r = reduce(x, &q); return (double)(q & 1 ? -1 / x_tan(r) : x_tan(r)); }
static double m_atan2(double y, double x) { return (double)x_atan2(y, x); }
static double m_atan(double x) { if (x != x) return x; return (double)x_atan2(x, 1.0L); }
static double m_asin(double x) { if (x != x) return x; if (__builtin_fabs(x) > 1) return __builtin_nan(""); if (x == 0) return x; return (double)x_atan2(x, x_sqrt((1.0L - x) * (1.0L + x))); }
static double m_acos(double x) { if (x != x) return x; if (__builtin_fabs(x) > 1) return __builtin_nan(""); return (double)x_atan2(x_sqrt((1.0L - x) * (1.0L + x)), x); }
static double m_sinh(double x) { if (x != x || x == 0 || __builtin_isinf(x)) return x; ld a = __builtin_fabs(x); if (a < 0.5) { ld e = m_expm1(a); return m_copysign((double)((e + e / (e + 1)) / 2), x); } ld e = exp2_l(a * LOG2E); return m_copysign((double)((e - 1 / e) / 2), x); }
static double m_cosh(double x) { if (x != x) return x; ld e = exp2_l((ld)__builtin_fabs(x) * LOG2E); return (double)((e + 1 / e) / 2); }
static double m_tanh(double x) { if (x != x || x == 0) return x; ld a = __builtin_fabs(x); if (a > 22) return m_copysign(1.0, x); ld t = a < 0.25 ? (ld)m_expm1(2 * (double)a) : exp2_l(2 * a * LOG2E) - 1; return m_copysign((double)(t / (t + 2)), x); }
static double m_asinh(double x) { if (x != x || x == 0 || __builtin_isinf(x)) return x; ld a = __builtin_fabs(x); ld r = a > 1e9 ? x_yl2x(a, LN2) + LN2 : a < 0.25 ? (ld)m_log1p((double)(a + a * a / (1 + x_sqrt(1 + a * a)))) : x_yl2x(a + x_sqrt(a * a + 1), LN2); return m_copysign((double)r, x); }
static double m_acosh(double x) { if (x != x) return x; if (x < 1) return __builtin_nan(""); if (x == __builtin_inf()) return x; ld a = x; return (double)(a > 1e9 ? x_yl2x(a, LN2) + LN2 : x_yl2x(a + x_sqrt((a - 1) * (a + 1)), LN2)); }
static double m_atanh(double x) { if (x != x || x == 0) return x; ld a = __builtin_fabs(x); if (a > 1) return __builtin_nan(""); if (a == 1) return m_copysign(__builtin_inf(), x); ld r = a < 0.25 ? 0.5L * (ld)m_log1p((double)(2 * a + 2 * a * a / (1 - a))) : 0.5L * x_yl2x((1 + a) / (1 - a), LN2); return m_copysign((double)r, x); }


/* ---- entrada para el motor ---- */
double cb_sqrtl(long double s) { return (double)x_sqrt(s); }
double cb_pow(double a, double b) { if (b != b) return __builtin_nan(""); if (__builtin_isinf(b) && __builtin_fabs(a) == 1) return __builtin_nan(""); return m_pow(a, b); }   /* JavaScript: 1 ** Infinity y x ** NaN son NaN */
double cb_fmod(double a, double b) { return m_fmod(a, b); }
double cb_atan2(double a, double b) { return m_atan2(a, b); }
double cb_math1(int w, double x) {
    switch (w) {
    case 0: return m_sqrt(x); case 1: return m_sin(x); case 2: return m_cos(x); case 3: return m_tan(x); case 4: return m_asin(x); case 5: return m_acos(x); case 6: return m_atan(x);
    case 7: return m_exp(x); case 8: return m_log(x); case 9: return m_log2(x); case 10: return m_log10(x); case 11: return m_expm1(x); case 12: return m_log1p(x);
    case 13: return m_sinh(x); case 14: return m_cosh(x); case 15: return m_tanh(x); case 16: return m_asinh(x); case 17: return m_acosh(x); case 18: return m_atanh(x); case 19: return m_cbrt(x);
    case 20: return m_floor(x); case 21: return m_ceil(x); case 22: return m_trunc(x);
    case 23: { if (!__builtin_isfinite(x) || x == 0) return x; if (x > 0 && x < 0.5) return 0.0; if (x < 0 && x >= -0.5) return -0.0; double f = m_floor(x); return x - f >= 0.5 ? f + 1 : f; }
    case 24: return (double)(float)x; }
    return __builtin_nan(""); }
/* aleatorios: xorshift128+ sembrado con el reloj */
static u64 rs0, rs1;
double cb_random(void) {
    if (!rs0 && !rs1) { u64 t; __asm__ volatile("rdtsc; shl $32, %%rdx; or %%rdx, %%rax" : "=a"(t) : : "rdx"); rs0 = t ^ 0x9E3779B97F4A7C15ULL; rs1 = (t << 17) ^ 0xBF58476D1CE4E5B9ULL; if (!rs1) rs1 = 1; }
    u64 x = rs0, y = rs1; rs0 = y; x ^= x << 23; rs1 = x ^ y ^ (x >> 17) ^ (y >> 26); return (double)((rs1 + y) >> 11) / 9007199254740992.0; }
/* mayusculas y minusculas: latin, griego, cirilico, armenio y otros bloques por desplazamiento */
u32 cb_case(u32 c, int up) {
    if (c < 128) return up ? (c >= 'a' && c <= 'z' ? c - 32 : c) : (c >= 'A' && c <= 'Z' ? c + 32 : c);
    if (up) {
        if (c == 0xB5) return 0x39C; if (c == 0xFF) return 0x178;
        if ((c >= 0xE0 && c <= 0xFE && c != 0xF7)) return c - 32;
        if (c >= 0x100 && c <= 0x17F) { if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return c & 1 ? c : c - 1; if (c == 0x131) return 'I'; if (c == 0x17F) return 'S'; return c & 1 ? c - 1 : c; }
        if (c >= 0x3B1 && c <= 0x3C9 && c != 0x3C2) return c - 32; if (c == 0x3C2) return 0x3A3; if (c >= 0x3AC && c <= 0x3AF) { static const u16 m[] = { 0x386, 0x388, 0x389, 0x38A }; return m[c - 0x3AC]; } if (c == 0x3CC) return 0x38C; if (c == 0x3CD) return 0x38E; if (c == 0x3CE) return 0x38F;
        if (c >= 0x430 && c <= 0x44F) return c - 32; if (c >= 0x450 && c <= 0x45F) return c - 80; if (c >= 0x460 && c <= 0x4FF && c != 0x482 && !(c >= 0x483 && c <= 0x489)) return c & 1 ? c - 1 : c;
        if (c >= 0x561 && c <= 0x586) return c - 48; if (c >= 0x1E00 && c <= 0x1EFF) return c & 1 ? c - 1 : c; if (c >= 0x24D0 && c <= 0x24E9) return c - 26; if (c >= 0xFF41 && c <= 0xFF5A) return c - 32;
        return c; }
    if ((c >= 0xC0 && c <= 0xDE && c != 0xD7)) return c + 32;
    if (c >= 0x100 && c <= 0x17F) { if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return c & 1 ? c + 1 : c; if (c == 0x130) return 'i'; if (c == 0x178) return 0xFF; return c & 1 ? c : c + 1; }
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return c + 32; if (c == 0x386) return 0x3AC; if (c >= 0x388 && c <= 0x38A) return c + 37; if (c == 0x38C) return 0x3CC; if (c == 0x38E) return 0x3CD; if (c == 0x38F) return 0x3CE;
    if (c >= 0x410 && c <= 0x42F) return c + 32; if (c >= 0x400 && c <= 0x40F) return c + 80; if (c >= 0x460 && c <= 0x4FF && c != 0x482 && !(c >= 0x483 && c <= 0x489)) return c & 1 ? c : c + 1;
    if (c >= 0x531 && c <= 0x556) return c + 48; if (c >= 0x1E00 && c <= 0x1EFF) return c & 1 ? c : c + 1; if (c >= 0x24B6 && c <= 0x24CF) return c + 26; if (c >= 0xFF21 && c <= 0xFF3A) return c + 32;
    return c; }
/* letras latinas con acento: letra base y signo combinable (para normalize y localeCompare) */
static const struct { u16 c, b, m; } DEC[] = {
    {0xC0,'A',0x300},{0xC1,'A',0x301},{0xC2,'A',0x302},{0xC3,'A',0x303},{0xC4,'A',0x308},{0xC5,'A',0x30A},{0xC7,'C',0x327},{0xC8,'E',0x300},{0xC9,'E',0x301},{0xCA,'E',0x302},{0xCB,'E',0x308},
    {0xCC,'I',0x300},{0xCD,'I',0x301},{0xCE,'I',0x302},{0xCF,'I',0x308},{0xD1,'N',0x303},{0xD2,'O',0x300},{0xD3,'O',0x301},{0xD4,'O',0x302},{0xD5,'O',0x303},{0xD6,'O',0x308},{0xD9,'U',0x300},
    {0xDA,'U',0x301},{0xDB,'U',0x302},{0xDC,'U',0x308},{0xDD,'Y',0x301},{0xE0,'a',0x300},{0xE1,'a',0x301},{0xE2,'a',0x302},{0xE3,'a',0x303},{0xE4,'a',0x308},{0xE5,'a',0x30A},{0xE7,'c',0x327},
    {0xE8,'e',0x300},{0xE9,'e',0x301},{0xEA,'e',0x302},{0xEB,'e',0x308},{0xEC,'i',0x300},{0xED,'i',0x301},{0xEE,'i',0x302},{0xEF,'i',0x308},{0xF1,'n',0x303},{0xF2,'o',0x300},{0xF3,'o',0x301},
    {0xF4,'o',0x302},{0xF5,'o',0x303},{0xF6,'o',0x308},{0xF9,'u',0x300},{0xFA,'u',0x301},{0xFB,'u',0x302},{0xFC,'u',0x308},{0xFD,'y',0x301},{0xFF,'y',0x308},{0x106,'C',0x301},{0x107,'c',0x301},
    {0x10C,'C',0x30C},{0x10D,'c',0x30C},{0x10E,'D',0x30C},{0x10F,'d',0x30C},{0x11A,'E',0x30C},{0x11B,'e',0x30C},{0x143,'N',0x301},{0x144,'n',0x301},{0x147,'N',0x30C},{0x148,'n',0x30C},
    {0x158,'R',0x30C},{0x159,'r',0x30C},{0x15A,'S',0x301},{0x15B,'s',0x301},{0x160,'S',0x30C},{0x161,'s',0x30C},{0x164,'T',0x30C},{0x165,'t',0x30C},{0x16E,'U',0x30A},{0x16F,'u',0x30A},
    {0x178,'Y',0x308},{0x179,'Z',0x301},{0x17A,'z',0x301},{0x17D,'Z',0x30C},{0x17E,'z',0x30C} };
u32 cb_decompose(u32 c, u32 *out) { if (c < 0xC0) return 0; for (u32 i = 0; i < sizeof DEC / sizeof *DEC; i++) if (DEC[i].c == c) { out[0] = DEC[i].b; out[1] = DEC[i].m; return 1; } return 0; }
u32 cb_compose(u32 a, u32 m) { if (m < 0x300 || m > 0x36F) return 0; for (u32 i = 0; i < sizeof DEC / sizeof *DEC; i++) if (DEC[i].b == a && DEC[i].m == m) return DEC[i].c; return 0; }
u32 cb_base_letter(u32 c) { u32 d[2]; if (cb_decompose(c, d)) c = d[0]; if (c == 0xD1 || c == 0xF1) return 'n' * 4 + 1; c = cb_case(c, 0); return c < 128 ? c * 4 : c * 4 + 2; }
