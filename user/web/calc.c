/* Evaluador de expresiones para los formularios HTML de MiOS
 * (atributos data-calc y data-if). Usa la FPU x87 del procesador, asi que
 * este archivo se compila con coma flotante; el resto del kernel no.
 *
 * Gramatica:
 *   cond  := or ['?' cond ':' cond]
 *   or    := and {'||' and}        and := cmp {'&&' cmp}
 *   cmp   := add [('<'|'>'|'<='|'>='|'=='|'!=') add]
 *   add   := mul {('+'|'-') mul}   mul := pow {('*'|'/'|'%') pow}
 *   pow   := unary ['^' pow]       unary := ('-'|'!') unary | prim
 *   prim  := numero | nombre | nombre '(' args ')' | '(' cond ')'
 * Funciones: min max round floor ceil abs sqrt pow if clamp */
#include "kernel.h"

struct ctx {
    const char *p;
    calc_lookup_fn lookup;
    void *user;
    int err, depth;
};

#ifdef MIOS_POCKET
/* En el ESP32 hay libreria matematica normal */
#include <math.h>
static double f_sqrt(double x) { return sqrt(x); }
static double f_floor(double x) { return floor(x); }
static double f_pow(double a, double b) { return pow(a, b); }
#else
static double f_sqrt(double x) { double r; __asm__("fsqrt" : "=t"(r) : "0"(x)); return r; }

static double f_floor(double x) {
    if (x >= 2e9 || x <= -2e9) return x;
    int i = (int)x;
    if ((double)i > x) i--;
    return (double)i;
}

/* 2^x con la FPU: f2xm1 trabaja en [-1,1], fscale suma la parte entera */
static double f_exp2(double x) {
    double fl = f_floor(x), fr = x - fl, r;
    __asm__("f2xm1" : "=t"(r) : "0"(fr));
    r += 1.0;
    __asm__("fscale" : "=t"(r) : "0"(r), "u"(fl));
    return r;
}

static double f_log2(double x) {
    double r;
    __asm__("fld1; fxch; fyl2x" : "=t"(r) : "0"(x));
    return r;
}

static double f_pow(double a, double b) {
    if (b == f_floor(b) && b >= 0 && b <= 64) {
        double r = 1;
        for (int i = 0; i < (int)b; i++) r *= a;
        return r;
    }
    if (a <= 0) return a == 0 ? 0 : 0.0 / 0.0;
    return f_exp2(b * f_log2(a));
}
#endif

static double f_round(double x, int d) {
    double m = f_pow(10, d < 0 ? 0 : d);
    double v = x * m;
    v = v < 0 ? -f_floor(-v + 0.5) : f_floor(v + 0.5);
    return v / m;
}

static void ws(struct ctx *c) { while (*c->p == ' ' || *c->p == '\t' || *c->p == '\n') c->p++; }

static int is_alpha(char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_'; }
static int is_digit(char ch) { return ch >= '0' && ch <= '9'; }

/* Convierte texto escrito por una persona a numero: "1.234,5", "12,5 %", "3.75" */
static double parse_human(const char *s, int *ok) {
    char buf[48];
    int n = 0, commas = 0, dots = 0;
    for (const char *p = s; *p && n < 46; p++) {
        if (is_digit(*p) || *p == '-') buf[n++] = *p;
        else if (*p == ',') { buf[n++] = ','; commas++; }
        else if (*p == '.') { buf[n++] = '.'; dots++; }
    }
    buf[n] = 0;
    double v = 0, scale = 0;
    int neg = 0, any = 0;
    for (int i = 0; i < n; i++) {
        char ch = buf[i];
        if (ch == '-') { if (!any) neg = 1; continue; }
        if (ch == '.' || ch == ',') {
            int decimal = (ch == ',') ? 1 : (commas == 0 && dots == 1);
            if (ch == '.' && commas > 0) decimal = 0;            /* 1.234,5 */
            if (decimal && !scale) scale = 1;
            continue;
        }
        any = 1;
        if (scale) { scale /= 10; v += (ch - '0') * scale; }
        else v = v * 10 + (ch - '0');
    }
    *ok = any;
    return neg ? -v : v;
}

static double cond(struct ctx *c);

static double variable(struct ctx *c, const char *name, int len) {
    const char *text;
    int is_expr;
    if (!c->lookup(c->user, name, len, &text, &is_expr)) { c->err = 1; return 0; }
    if (is_expr) {
        if (c->depth > 12) { c->err = 1; return 0; }
        struct ctx sub = { text, c->lookup, c->user, 0, c->depth + 1 };
        double v = cond(&sub);
        if (sub.err) c->err = 1;
        return v;
    }
    int ok;
    return parse_human(text, &ok);
}

static double prim(struct ctx *c) {
    ws(c);
    if (*c->p == '(') {
        c->p++;
        double v = cond(c);
        ws(c);
        if (*c->p == ')') c->p++; else c->err = 1;
        return v;
    }
    if (is_digit(*c->p) || *c->p == '.') {
        double v = 0, scale = 0;
        while (is_digit(*c->p) || *c->p == '.') {
            if (*c->p == '.') { scale = 1; c->p++; continue; }
            if (scale) { scale /= 10; v += (*c->p - '0') * scale; }
            else v = v * 10 + (*c->p - '0');
            c->p++;
        }
        return v;
    }
    if (is_alpha(*c->p)) {
        const char *s = c->p;
        while (is_alpha(*c->p) || is_digit(*c->p) || *c->p == '-') {
            if (*c->p == '-' && !(is_alpha(c->p[1]) || is_digit(c->p[1]))) break;
            /* "a-b" es una resta salvo que el nombre siga (ids con guion) */
            if (*c->p == '-' && c->lookup(c->user, s, (int)(c->p - s), NULL, NULL)) break;
            c->p++;
        }
        int len = (int)(c->p - s);
        ws(c);
        if (*c->p != '(') return variable(c, s, len);
        c->p++;
        double a[4] = { 0, 0, 0, 0 };
        int n = 0;
        ws(c);
        if (*c->p != ')')
            for (;;) {
                double v = cond(c);
                if (n < 4) a[n] = v;
                n++;
                ws(c);
                if (*c->p == ',') { c->p++; continue; }
                break;
            }
        if (*c->p == ')') c->p++; else c->err = 1;
#define IS(name) (len == (int)sizeof(name) - 1 && !strncmp(s, name, sizeof(name) - 1))
        if (IS("min")) { double r = a[0]; for (int i = 1; i < n && i < 4; i++) if (a[i] < r) r = a[i]; return r; }
        if (IS("max")) { double r = a[0]; for (int i = 1; i < n && i < 4; i++) if (a[i] > r) r = a[i]; return r; }
        if (IS("round")) return f_round(a[0], n > 1 ? (int)a[1] : 0);
        if (IS("floor")) return f_floor(a[0]);
        if (IS("ceil")) { double f = f_floor(a[0]); return f < a[0] ? f + 1 : f; }
        if (IS("abs")) return a[0] < 0 ? -a[0] : a[0];
        if (IS("sqrt")) return a[0] < 0 ? 0 : f_sqrt(a[0]);
        if (IS("pow")) return f_pow(a[0], a[1]);
        if (IS("if")) return a[0] != 0 ? a[1] : a[2];
        if (IS("clamp")) return a[0] < a[1] ? a[1] : a[0] > a[2] ? a[2] : a[0];
#undef IS
        c->err = 1;
        return 0;
    }
    c->err = 1;
    return 0;
}

static double unary(struct ctx *c) {
    ws(c);
    if (*c->p == '-') { c->p++; return -unary(c); }
    if (*c->p == '+') { c->p++; return unary(c); }
    if (*c->p == '!') { c->p++; return unary(c) == 0; }
    return prim(c);
}

static double power(struct ctx *c) {
    double v = unary(c);
    ws(c);
    if (*c->p == '^') { c->p++; return f_pow(v, power(c)); }
    return v;
}

static double mul(struct ctx *c) {
    double v = power(c);
    for (;;) {
        ws(c);
        char op = *c->p;
        if (op != '*' && op != '/' && op != '%') return v;
        c->p++;
        double r = power(c);
        if (op == '*') v *= r;
        else if (r == 0) { c->err = 1; v = 0; }
        else if (op == '/') v /= r;
        else v = v - f_floor(v / r) * r;
    }
}

static double add(struct ctx *c) {
    double v = mul(c);
    for (;;) {
        ws(c);
        if (*c->p == '+') { c->p++; v += mul(c); }
        else if (*c->p == '-') { c->p++; v -= mul(c); }
        else return v;
    }
}

static double cmp(struct ctx *c) {
    double v = add(c);
    ws(c);
    const char *p = c->p;
    if (p[0] == '<' && p[1] == '=') { c->p += 2; return v <= add(c); }
    if (p[0] == '>' && p[1] == '=') { c->p += 2; return v >= add(c); }
    if (p[0] == '=' && p[1] == '=') { c->p += 2; return v == add(c); }
    if (p[0] == '!' && p[1] == '=') { c->p += 2; return v != add(c); }
    if (p[0] == '<') { c->p++; return v < add(c); }
    if (p[0] == '>') { c->p++; return v > add(c); }
    return v;
}

static double and_(struct ctx *c) {
    double v = cmp(c);
    for (;;) {
        ws(c);
        if (c->p[0] == '&' && c->p[1] == '&') { c->p += 2; double r = cmp(c); v = v != 0 && r != 0; }
        else return v;
    }
}

static double or_(struct ctx *c) {
    double v = and_(c);
    for (;;) {
        ws(c);
        if (c->p[0] == '|' && c->p[1] == '|') { c->p += 2; double r = and_(c); v = v != 0 || r != 0; }
        else return v;
    }
}

static double cond(struct ctx *c) {
    double v = or_(c);
    ws(c);
    if (*c->p == '?') {
        c->p++;
        double a = cond(c);
        ws(c);
        if (*c->p == ':') c->p++; else c->err = 1;
        double b = cond(c);
        return v != 0 ? a : b;
    }
    return v;
}

/* Numero en formato espanol: 1.234.567,89 */
static void format(double v, int dec, char *out, int n) {
    if (v != v || v > 1e15 || v < -1e15) { strlcpy(out, "--", (size_t)n); return; }
    if (dec < 0) dec = (v - f_floor(v) > 1e-9 && f_floor(v) - v < -1e-9) ? 2 : 0;
    if (dec > 6) dec = 6;
    int neg = v < 0;
    if (neg) v = -v;
    v = f_round(v, dec);
    double ip = f_floor(v);
    int fr = (int)f_floor((v - ip) * f_pow(10, dec) + 0.5);
    int hi = (int)f_floor(ip / 1e9);
    int lo = (int)(ip - (double)hi * 1e9);
    char digits[24];
    int nd;
    if (hi) nd = ksnprintf(digits, sizeof digits, "%d%09d", hi, lo);
    else nd = ksnprintf(digits, sizeof digits, "%d", lo);
    int k = 0;
    if (neg && (ip > 0 || fr > 0)) out[k++] = '-';
    for (int i = 0; i < nd && k < n - 2; i++) {
        out[k++] = digits[i];
        int left = nd - i - 1;
        if (left && left % 3 == 0 && k < n - 2) out[k++] = '.';
    }
    if (dec && k < n - dec - 2) {
        out[k++] = ',';
        char f[8];
        for (int i = dec - 1; i >= 0; i--) { f[i] = (char)('0' + fr % 10); fr /= 10; }
        for (int i = 0; i < dec && k < n - 1; i++) out[k++] = f[i];
    }
    out[k] = 0;
}

int calc_eval(const char *expr, calc_lookup_fn lookup, void *user, int dec, char *out, int outlen) {
    struct ctx c = { expr, lookup, user, 0, 0 };
    double v = cond(&c);
    ws(&c);
    if (c.err || *c.p) { strlcpy(out, "--", (size_t)outlen); return -1; }
    format(v, dec, out, outlen);
    return 0;
}

int calc_truth(const char *expr, calc_lookup_fn lookup, void *user) {
    struct ctx c = { expr, lookup, user, 0, 0 };
    double v = cond(&c);
    return c.err ? 0 : v != 0;
}


