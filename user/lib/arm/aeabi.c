/* ZunzunOS - funciones de apoyo del ABI de ARM (RTABI) que el compilador llama por su cuenta en ARMv7 de 32 bits:
   division de 64 bits, conversiones entre enteros de 64 bits y coma flotante, y copias de memoria.
   Las usan el kernel de la Raspberry Pi y las aplicaciones. Todas usan la convencion base (enteros en r0-r3)
   aunque el programa use la FPU para pasar los double. No hacen operaciones de coma flotante. */
typedef unsigned long long u64; typedef long long i64; typedef unsigned int u32; typedef int i32; typedef unsigned char u8;
typedef __SIZE_TYPE__ size_t;
#define BASE __attribute__((pcs("aapcs"), used))

/* --- division --- */
u64 zz_udivmod64(u64 n, u64 d, u64 *rem) {
    if (!d) { *rem = n; return ~0ULL; }
    if (!(n >> 32) && !(d >> 32)) { u32 q = (u32)n / (u32)d; *rem = (u32)n - q * (u32)d; return q; }
    if (d > n) { *rem = n; return 0; }
    int sh = __builtin_clzll(d) - __builtin_clzll(n); u64 q = 0; d <<= sh;
    for (; sh >= 0; sh--) { q <<= 1; if (n >= d) { n -= d; q |= 1; } d >>= 1; }
    *rem = n; return q;
}
i64 zz_sdivmod64(i64 n, i64 d, i64 *rem) {
    int nn = n < 0, nd = d < 0; u64 r; u64 q = zz_udivmod64(nn ? -(u64)n : (u64)n, nd ? -(u64)d : (u64)d, &r);
    *rem = nn ? -(i64)r : (i64)r; return nn != nd ? -(i64)q : (i64)q;
}

/* --- coma flotante <-> enteros de 64 bits (por bits, IEEE 754) --- */
static u64 dbits(double d) { union { double d; u64 u; } x; x.d = d; return x.u; }
static double bitsd(u64 u) { union { double d; u64 u; } x; x.u = u; return x.d; }
static u32 fbits(float f) { union { float f; u32 u; } x; x.f = f; return x.u; }
static float bitsf(u32 u) { union { float f; u32 u; } x; x.u = u; return x.f; }
static u64 d2u(u64 b, int *neg, int sat_s) {
    *neg = b >> 63; int e = (b >> 52) & 0x7FF; u64 m = (b & ((1ULL << 52) - 1)) | (1ULL << 52);
    if (e == 0x7FF && (b & ((1ULL << 52) - 1))) return 0;   /* NaN */
    if (e < 1023) return 0;
    int p = e - 1023;   /* el valor es m * 2^(p-52) */
    if (p >= (sat_s ? 63 : 64)) return ~0ULL;
    return p >= 52 ? m << (p - 52) : m >> (52 - p);
}
BASE i64 __aeabi_d2lz(double d) {
    int neg; u64 v = d2u(dbits(d), &neg, 1);
    if (v == ~0ULL) return neg ? (i64)(1ULL << 63) : (i64)((1ULL << 63) - 1);
    if (!neg && v > (u64)((1ULL << 63) - 1)) return (i64)((1ULL << 63) - 1);
    return neg ? -(i64)v : (i64)v;
}
BASE u64 __aeabi_d2ulz(double d) { int neg; u64 v = d2u(dbits(d), &neg, 0); return neg ? 0 : v; }
static u64 u2bits(u64 x, int mbits, int bias) {   /* entero sin signo -> exponente y mantisa redondeados al par */
    if (!x) return 0;
    int p = 63 - __builtin_clzll(x); u64 e = p + bias, m;
    if (p <= mbits) m = x << (mbits - p);
    else {
        int sh = p - mbits; m = x >> sh; u64 rem = x & ((1ULL << sh) - 1), half = 1ULL << (sh - 1);
        if (rem > half || (rem == half && (m & 1))) { m++; if (m >> (mbits + 1)) { m >>= 1; e++; } }
    }
    return e << mbits | (m & ((1ULL << mbits) - 1));
}
BASE double __aeabi_ul2d(u64 x) { return bitsd(u2bits(x, 52, 1023)); }
BASE double __aeabi_l2d(i64 x) { return x < 0 ? bitsd(u2bits(-(u64)x, 52, 1023) | 1ULL << 63) : bitsd(u2bits(x, 52, 1023)); }
BASE float __aeabi_ul2f(u64 x) { return bitsf((u32)u2bits(x, 23, 127)); }
BASE float __aeabi_l2f(i64 x) { return x < 0 ? bitsf((u32)u2bits(-(u64)x, 23, 127) | 1u << 31) : bitsf((u32)u2bits(x, 23, 127)); }
static u64 f2u(u32 b, int *neg, int sat_s) {
    *neg = b >> 31; int e = (b >> 23) & 0xFF; u64 m = (b & 0x7FFFFF) | 0x800000;
    if (e == 0xFF && (b & 0x7FFFFF)) return 0;
    if (e < 127) return 0;
    int p = e - 127; if (p >= (sat_s ? 63 : 64)) return ~0ULL;
    return p >= 23 ? m << (p - 23) : m >> (23 - p);
}
BASE i64 __aeabi_f2lz(float f) {
    int neg; u64 v = f2u(fbits(f), &neg, 1);
    if (v == ~0ULL) return neg ? (i64)(1ULL << 63) : (i64)((1ULL << 63) - 1);
    return neg ? -(i64)v : (i64)v;
}
BASE u64 __aeabi_f2ulz(float f) { int neg; u64 v = f2u(fbits(f), &neg, 0); return neg ? 0 : v; }

/* --- memoria (ojo: __aeabi_memset recibe el valor en tercer lugar) --- */
void *memcpy(void *, const void *, size_t); void *memmove(void *, const void *, size_t); void *memset(void *, int, size_t);
BASE void __aeabi_memcpy(void *d, const void *s, size_t n) { memcpy(d, s, n); }
BASE void __aeabi_memcpy4(void *d, const void *s, size_t n) { memcpy(d, s, n); }
BASE void __aeabi_memcpy8(void *d, const void *s, size_t n) { memcpy(d, s, n); }
BASE void __aeabi_memmove(void *d, const void *s, size_t n) { memmove(d, s, n); }
BASE void __aeabi_memmove4(void *d, const void *s, size_t n) { memmove(d, s, n); }
BASE void __aeabi_memmove8(void *d, const void *s, size_t n) { memmove(d, s, n); }
BASE void __aeabi_memset(void *d, size_t n, int c) { memset(d, c, n); }
BASE void __aeabi_memset4(void *d, size_t n, int c) { memset(d, c, n); }
BASE void __aeabi_memset8(void *d, size_t n, int c) { memset(d, c, n); }
BASE void __aeabi_memclr(void *d, size_t n) { memset(d, 0, n); }
BASE void __aeabi_memclr4(void *d, size_t n) { memset(d, 0, n); }
BASE void __aeabi_memclr8(void *d, size_t n) { memset(d, 0, n); }
