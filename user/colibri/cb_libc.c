/* Colibri: las pocas funciones de C que el compilador pide para __builtin_floor y companias y que la libz no tiene */
#include "cbi.h"
#ifndef CB_HOST
double trunc(double x) { if (!(__builtin_fabs(x) < 4503599627370496.0)) return x; double t = (double)(long)x; return __builtin_copysign(t, x); }
double floor(double x) { double t = trunc(x); if (t > x) t -= 1; return t == 0 ? __builtin_copysign(0.0, x) : t; }
double ceil(double x) { double t = trunc(x); if (t < x) t += 1; return t == 0 ? __builtin_copysign(0.0, x) : t; }
double fmod(double x, double y) { extern double cb_fmod(double, double); return cb_fmod(x, y); }
double nextafter(double x, double y) {
    if (x != x || y != y) return x + y; if (x == y) return y;
    union { double d; u64 u; } c = { x }; if (x == 0) { c.u = 1; return y > 0 ? c.d : -c.d; }
    if ((x < y) == (x > 0)) c.u++; else c.u--; return c.d; }
#endif
