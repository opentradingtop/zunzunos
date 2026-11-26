/* ZunzunOS - partes de libz propias de ARM (Raspberry Pi): copias de memoria y matematicas en doble precision.
   En x86 las matematicas usan la unidad x87; aqui se calculan con reduccion del argumento y series de Taylor
   (sin tablas de nadie: los coeficientes son 1/n!, 1/n y las constantes pi y ln 2). */
#include "../z.h"

void *memcpy(void *d, const void *s, size_t n) {
    u8 *a = d; const u8 *b = s;
    if ((((u32)a ^ (u32)b) & 3) == 0) {
        while (n && ((u32)a & 3)) { *a++ = *b++; n--; }
        u32 *x = (u32 *)a; const u32 *y = (const u32 *)b;
        while (n >= 32) { u32 t0 = y[0], t1 = y[1], t2 = y[2], t3 = y[3], t4 = y[4], t5 = y[5], t6 = y[6], t7 = y[7];
            x[0] = t0; x[1] = t1; x[2] = t2; x[3] = t3; x[4] = t4; x[5] = t5; x[6] = t6; x[7] = t7; x += 8; y += 8; n -= 32; }
        while (n >= 4) { *x++ = *y++; n -= 4; }
        a = (u8 *)x; b = (const u8 *)y;
    }
    while (n--) *a++ = *b++;
    return d;
}
void *memset(void *d, int v, size_t n) {
    u8 *a = d; u32 w = (u8)v * 0x01010101u;
    while (n && ((u32)a & 3)) { *a++ = (u8)v; n--; }
    u32 *x = (u32 *)a; while (n >= 16) { x[0] = w; x[1] = w; x[2] = w; x[3] = w; x += 4; n -= 16; }
    while (n >= 4) { *x++ = w; n -= 4; }
    a = (u8 *)x; while (n--) *a++ = (u8)v;
    return d;
}

#define PI 3.14159265358979323846
#define LN2 0.69314718055994530942
static u64 bits(double d) { union { double d; u64 u; } x; x.d = d; return x.u; }
static double from(u64 u) { union { double d; u64 u; } x; x.u = u; return x.d; }
static int finite(double x) { return ((bits(x) >> 52) & 0x7FF) != 0x7FF; }

double z_sqrt(double x) { return __builtin_sqrt(x); }   /* VSQRT.F64 */
double z_fabs(double x);
double z_floor(double x) {
    if (!finite(x) || z_fabs(x) >= 4503599627370496.0) return x;
    double t = (double)(i64)x; return t > x ? t - 1 : t;
}
/* x = k * pi/2 + r con |r| <= pi/4; pi/2 en tres trozos para no perder precision */
static double reduce(double x, int *q) {
    const double P1 = 1.5707963267341256, P2 = 6.077100506506192e-11, P3 = 2.0222662487959506e-21;
    double k = z_floor(x * (2 / PI) + 0.5); *q = (int)((i64)k & 3);
    return ((x - k * P1) - k * P2) - k * P3;
}
static double sin_s(double r) { double r2 = r * r, t = r, s = r; for (int n = 3; n <= 21; n += 2) { t *= -r2 / ((n - 1) * n); s += t; } return s; }
static double cos_s(double r) { double r2 = r * r, t = 1, s = 1; for (int n = 2; n <= 20; n += 2) { t *= -r2 / ((n - 1) * n); s += t; } return s; }
double z_sin(double x) {
    if (!finite(x)) return from(0x7FF8000000000000ULL);
    int q; double r = reduce(x, &q);
    switch (q) { case 0: return sin_s(r); case 1: return cos_s(r); case 2: return -sin_s(r); default: return -cos_s(r); }
}
double z_cos(double x) {
    if (!finite(x)) return from(0x7FF8000000000000ULL);
    int q; double r = reduce(x, &q);
    switch (q) { case 0: return cos_s(r); case 1: return -sin_s(r); case 2: return -cos_s(r); default: return sin_s(r); }
}
double z_tan(double x) { return z_sin(x) / z_cos(x); }
/* arcotangente: |x| > 1 -> pi/2 - atan(1/x); x > tan(pi/12) -> pi/6 + atan((x*sqrt3 - 1)/(sqrt3 + x)); luego la serie */
double z_atan(double x) {
    if (x != x) return x;
    int neg = x < 0; if (neg) x = -x;
    int inv = x > 1; if (inv) x = 1 / x;
    double add = 0; const double S3 = 1.7320508075688772935;
    if (x > 0.26794919243112270) { x = (x * S3 - 1) / (S3 + x); add = PI / 6; }
    double x2 = x * x, t = x, s = x; for (int n = 3; n <= 41; n += 2) { t *= -x2; s += t / n; }
    s += add; if (inv) s = PI / 2 - s;
    return neg ? -s : s;
}
/* ln x = e*ln2 + 2*atanh((m-1)/(m+1)) con m en [sqrt(1/2), sqrt(2)) */
double z_ln(double x) {
    if (x != x || x < 0) return from(0x7FF8000000000000ULL);
    if (x == 0) return -from(0x7FF0000000000000ULL);
    if (!finite(x)) return x;
    u64 b = bits(x); int e = (int)((b >> 52) & 0x7FF);
    if (!e) { x *= 4503599627370496.0; b = bits(x); e = (int)((b >> 52) & 0x7FF) - 52; }
    e -= 1023; double m = from((b & 0x000FFFFFFFFFFFFFULL) | 0x3FF0000000000000ULL);
    if (m > 1.4142135623730951) { m /= 2; e++; }
    double s = (m - 1) / (m + 1), s2 = s * s, t = s, r = s;
    for (int n = 3; n <= 31; n += 2) { t *= s2; r += t / n; }
    return e * LN2 + 2 * r;
}
/* e^x = 2^k * e^r con |r| <= ln2/2 */
double z_exp(double x) {
    if (x != x) return x;
    if (x > 709.78) return from(0x7FF0000000000000ULL);
    if (x < -745.2) return 0;
    double k = z_floor(x / LN2 + 0.5), r = (x - k * 0.6931471803691238) - k * 1.9082149292705877e-10;
    double t = 1, s = 1; for (int n = 1; n <= 18; n++) { t *= r / n; s += t; }
    int ki = (int)k;
    if (ki > 1023) { s *= from(0x7FE0000000000000ULL); ki -= 1023; }
    if (ki < -1022) { s *= from(0x0010000000000000ULL); ki += 1022; if (ki < -1022) { s *= from(0x0010000000000000ULL); ki += 1022; } }
    return s * from((u64)(ki + 1023) << 52);
}
double z_pow(double a, double b) {
    if (b == 0) return 1;
    if (a == 0) return b > 0 ? 0 : from(0x7FF0000000000000ULL);
    if (b == z_floor(b) && z_fabs(b) <= 64) {   /* exponente entero pequeno: multiplicaciones exactas */
        int n = (int)z_fabs(b); double r = 1, p = a; while (n) { if (n & 1) r *= p; p *= p; n >>= 1; }
        return b < 0 ? 1 / r : r;
    }
    if (a < 0) { i64 i = (i64)b; if ((double)i != b) return from(0x7FF8000000000000ULL); double r = z_exp(z_ln(-a) * b); return i & 1 ? -r : r; }
    return z_exp(z_ln(a) * b);
}
