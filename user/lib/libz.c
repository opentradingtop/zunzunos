#include "z.h"
extern int main(const char *args);
static char g_args[1024]; ztheme TH; int z_joy_raw;
static struct { int id; char key[96]; } WT[16];
void z_title_key(int id, const char *k) { int f = -1; for (int i = 0; i < 16; i++) if (WT[i].id == id || (!WT[i].id && f < 0)) { if (WT[i].id == id) { f = i; break; } f = i; } if (f >= 0) { WT[f].id = id; strncpy0(WT[f].key, k, 96); } }
static zevent jq[16]; static int jqn; static u32 jbtn; static int jdx, jdy, jmap[10] = {-1};
static void jpush(u32 key, u32 ch, int down) { if (jqn < 16) { zevent e = {down ? EV_KEY : EV_KEYUP, 0, 0, 0, key, down ? ch : 0, 0, 0}; jq[jqn++] = e; } }
int z_event0(zevent *e, long ms);
static void jload(void) { static const char *K[] = {"mando_a", "mando_b", "mando_x", "mando_y", "mando_select", "mando_start", "mando_l", "mando_r"}; static const int D[] = {0, 1, 2, 3, 8, 9, 4, 5}; for (int i = 0; i < 8; i++) jmap[i] = z_cfg_get(K[i], D[i]); }
static void joy_keys(zevent *e) { if (jmap[0] < 0) jload(); int dx = e->x, dy = e->y; u32 b = e->btn; if (dx != jdx) { if (jdx) jpush(jdx < 0 ? 0x50 : 0x4F, 0, 0); if (dx) jpush(dx < 0 ? 0x50 : 0x4F, 0, 1); } if (dy != jdy) { if (jdy) jpush(jdy < 0 ? 0x52 : 0x51, 0, 0); if (dy) jpush(dy < 0 ? 0x52 : 0x51, 0, 1); } static const u32 KK[8] = {0x2C, 0x28, 0x52, 0x1B, 0x29, 0x13, 0x4B, 0x4E}, CH[8] = {' ', '\n', 0, 'x', 27, 'p', 0, 0}; for (int i = 0; i < 8; i++) { int bit = jmap[i]; if (bit < 0 || bit > 31) continue; int was = jbtn >> bit & 1, now = b >> bit & 1; if (was != now) jpush(KK[i], CH[i], now); } jbtn = b; jdx = dx; jdy = dy; }
zevent z_last_down;   /* ultima pulsacion del raton (ui_input_click la usa si el programa no le pasa el evento) */
int z_event(zevent *e, long ms) {
    int r0 = z_event0(e, ms); if (r0 && e->type == EV_DOWN) z_last_down = *e; return r0; }
int z_event0(zevent *e, long ms) {
    for (;;) {
        if (jqn) { *e = jq[0]; memmove(jq, jq + 1, --jqn * sizeof *jq); e->win = WT[0].id; return 1; }
        int r = z_event_raw(e, ms); if (!r) return 0;
        if (e->type == EV_THEME) { z_theme_get(&TH); for (int i = 0; i < 16; i++) if (WT[i].id) SC2(28, WT[i].id, tr(WT[i].key)); e->type = EV_REDRAW; e->win = WT[0].id; return 1; }
        if (e->type == EV_JOY && !z_joy_raw) { int w = e->win; joy_keys(e); for (int i = 0; i < jqn; i++) jq[i].win = w; if (!jqn) continue; ms = 0; continue; }
        return 1;
    }
}
void z_start(void) { SC1(102, &TH); SC1(11, g_args); z_exit(main(g_args)); }

extern void z_thread_entry(void);
int z_thread(void (*fn)(void *), void *arg, int stack) {
    if (!stack) stack = 65536;
    u64 *m = malloc(stack); if (!m) return -12;
    u64 *t = (u64 *)(((u64)m + stack - 64) & ~15ULL), *a = malloc(16);
    a[0] = (u64)fn; a[1] = (u64)arg;
    return sc5(10, (long)z_thread_entry, (long)t, (long)a, 0, 0);
}
#ifndef __arm__   /* en ARM estan en user/lib/arm/mates.c */
void *memcpy(void *d, const void *s, size_t n) { void *r = d; __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory"); return r; }
void *memset(void *d, int v, size_t n) { void *r = d; __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(v) : "memory"); return r; }
#endif
void *memmove(void *d, const void *s, size_t n) { u8 *a = d; const u8 *b = s; if (a <= b || a >= b + n) return memcpy(d, s, n); while (n--) a[n] = b[n]; return d; }
int memcmp(const void *x, const void *y, size_t n) { const u8 *a = x, *b = y; for (; n; n--, a++, b++) if (*a != *b) return *a - *b; return 0; }
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
int strcmp(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return (u8)*a - (u8)*b; }
int strncmp(const char *a, const char *b, size_t n) { for (; n; n--, a++, b++) { if (*a != *b) return (u8)*a - (u8)*b; if (!*a) break; } return 0; }
int tolower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
int toupper(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
int isspace(int c) { return c == ' ' || (c >= 9 && c <= 13); }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isalpha(int c) { return (c | 32) >= 'a' && (c | 32) <= 'z'; }
int strcasecmp(const char *a, const char *b) { while (*a && tolower((u8)*a) == tolower((u8)*b)) a++, b++; return tolower((u8)*a) - tolower((u8)*b); }
char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)); return r; }
char *strcat(char *d, const char *s) { strcpy(d + strlen(d), s); return d; }
char *strncpy0(char *d, const char *s, size_t n) { size_t i = 0; if (!n) return d; for (; i + 1 < n && s[i]; i++) d[i] = s[i]; d[i] = 0; return d; }
char *strchr(const char *s, int c) { for (;; s++) { if (*s == (char)c) return (char *)s; if (!*s) return 0; } }
char *strrchr(const char *s, int c) { char *r = 0; for (;; s++) { if (*s == (char)c) r = (char *)s; if (!*s) return r; } }
char *strstr(const char *h, const char *n) { size_t l = strlen(n); for (; *h; h++) if (!strncmp(h, n, l)) return (char *)h; return l ? 0 : (char *)h; }
char *strcasestr(const char *h, const char *n) { size_t l = strlen(n); for (; *h; h++) { size_t i = 0; while (i < l && tolower((u8)h[i]) == tolower((u8)n[i])) i++; if (i == l) return (char *)h; } return 0; }
int atoi(const char *s) { int n = 0, g = 0; while (isspace(*s)) s++; if (*s == '-') { g = 1; s++; } else if (*s == '+') s++; while (isdigit(*s)) n = n * 10 + *s++ - '0'; return g ? -n : n; }
double atof(const char *s) {
    double v = 0, f = 0.1; int g = 0; while (isspace(*s)) s++; if (*s == '-') { g = 1; s++; }
    while (isdigit(*s)) v = v * 10 + *s++ - '0';
    if (*s == '.' || *s == ',') { s++; while (isdigit(*s)) { v += (*s++ - '0') * f; f /= 10; } }
    return g ? -v : v;
}
static u64 rs = 0x9E3779B97F4A7C15ULL;
u32 rnd(void) { if (rs == 0x9E3779B97F4A7C15ULL) rs ^= z_cycles(); rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs >> 16; }
int utf8_next(const char **ps) {
    const u8 *s = (const u8 *)*ps; u32 c = *s; int n = 0;
    if (!c) return 0;
    if (c < 0x80) n = 0; else if ((c >> 5) == 6) { c &= 31; n = 1; } else if ((c >> 4) == 14) { c &= 15; n = 2; } else if ((c >> 3) == 30) { c &= 7; n = 3; } else { *ps = (char *)s + 1; return '?'; }
    s++; while (n--) { if ((*s & 0xC0) != 0x80) { *ps = (char *)s; return '?'; } c = c << 6 | (*s++ & 63); }
    *ps = (char *)s; return c;
}
int utf8_put(u32 c, char *o) {
    if (c < 0x80) { o[0] = c; return 1; } if (c < 0x800) { o[0] = 0xC0 | c >> 6; o[1] = 0x80 | (c & 63); return 2; }
    o[0] = 0xE0 | c >> 12; o[1] = 0x80 | ((c >> 6) & 63); o[2] = 0x80 | (c & 63); return 3;
}
/* malloc: lista libre con cabecera de 16 bytes */
typedef struct blk { size_t size; struct blk *next; } blk;
static blk *fl;
void *malloc(size_t n) {
    n = (n + 15) & ~15ULL; if (!n) n = 16;
    blk **p = &fl;
    for (blk *b = fl; b; p = &b->next, b = b->next) if (b->size >= n) {
        if (b->size >= n + 64) { blk *r = (blk *)((u8 *)(b + 1) + n); r->size = b->size - n - sizeof(blk); r->next = b->next; *p = r; b->size = n; } else *p = b->next;
        return b + 1;
    }
    size_t t = n + sizeof(blk), chunk = t < 65536 ? 65536 : t;
    blk *b = z_sbrk(chunk); if (!b) return 0;
    b->size = n;
    if (chunk >= t + 64) { blk *r = (blk *)((u8 *)b + t); r->size = chunk - t - sizeof(blk); r->next = fl; fl = r; } else b->size = chunk - sizeof(blk);
    return b + 1;
}
void free(void *v) {
    if (!v) return;
    blk *b = (blk *)v - 1, **p = &fl;
    while (*p && *p < b) p = &(*p)->next;
    b->next = *p; *p = b;
    if (b->next && (u8 *)(b + 1) + b->size == (u8 *)b->next) { b->size += sizeof(blk) + b->next->size; b->next = b->next->next; }
    if (p != &fl) { blk *pr = (blk *)((u8 *)p - __builtin_offsetof(blk, next)); if ((u8 *)(pr + 1) + pr->size == (u8 *)b) { pr->size += sizeof(blk) + b->size; pr->next = b->next; } }
}
size_t z_msize(void *v) { return v ? ((blk *)v - 1)->size : 0; }   /* tamano util de un bloque de malloc */
void *calloc(size_t a, size_t b) { void *p = malloc(a * b); if (p) memset(p, 0, a * b); return p; }
void *realloc(void *v, size_t n) { if (!v) return malloc(n); blk *b = (blk *)v - 1; if (b->size >= n) return v; void *p = malloc(n); if (p) { memcpy(p, v, b->size); free(v); } return p; }
char *strdup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
static void qs(u8 *b, long lo, long hi, size_t w, int (*c)(const void *, const void *), u8 *t, u8 *pv) {
    while (lo < hi) {
        memcpy(pv, b + (lo + (hi - lo) / 2) * w, w); long i = lo, j = hi;
        while (i <= j) { while (c(b + i * w, pv) < 0) i++; while (c(b + j * w, pv) > 0) j--; if (i <= j) { memcpy(t, b + i * w, w); memcpy(b + i * w, b + j * w, w); memcpy(b + j * w, t, w); i++; j--; } }
        if (j - lo < hi - i) { qs(b, lo, j, w, c, t, pv); lo = i; } else { qs(b, i, hi, w, c, t, pv); hi = j; }
    }
}
void qsort(void *b, size_t n, size_t w, int (*c)(const void *, const void *)) { if (n < 2) return; u8 *t = malloc(w * 2); qs(b, 0, n - 1, w, c, t, t + w); free(t); }

int vsnprintf(char *out, size_t max, const char *f, va_list ap) {
    size_t n = 0;
#define PUT(c) do { if (n + 1 < max) out[n] = (c); n++; } while (0)
    for (; *f; f++) {
        if (*f != '%') { PUT(*f); continue; }
        f++; int zero = 0, width = 0, left = 0, lng = 0, prec = -1;
        if (*f == '-') { left = 1; f++; }
        if (*f == '0') { zero = 1; f++; }
        while (isdigit(*f)) width = width * 10 + *f++ - '0';
        if (*f == '.') { f++; prec = 0; if (*f == '*') { prec = va_arg(ap, int); f++; if (prec < 0) prec = -1; } else while (isdigit(*f)) prec = prec * 10 + *f++ - '0'; }   /* %.*s: precision como argumento */
        while (*f == 'l' || *f == 'z') { lng = 1; f++; }
        char b[64]; int i = 0, len; const char *s = b;
        switch (*f) {
        case 'c': b[0] = va_arg(ap, int); len = 1; break;
        case 's': s = va_arg(ap, const char *); if (!s) s = "(null)"; len = strlen(s); if (prec >= 0 && len > prec) len = prec; break;
        case 'f': {
            double v = va_arg(ap, double); if (prec < 0) prec = 6; if (prec > 12) prec = 12;
            if (v < 0) { b[i++] = '-'; v = -v; }
            if (v > 1e15) { memcpy(b + i, "inf", 3); i += 3; len = i; break; }
            double r = 0.5; for (int k = 0; k < prec; k++) r /= 10; v += r;
            u64 ip = (u64)v; double fp = v - ip; char t[24]; int k = 0; do { t[k++] = '0' + ip % 10; ip /= 10; } while (ip); while (k) b[i++] = t[--k];
            if (prec) { b[i++] = '.'; for (int q = 0; q < prec; q++) { fp *= 10; int d = (int)fp; b[i++] = '0' + d; fp -= d; } }
            len = i; break; }
        case 'd': case 'u': case 'x': case 'X': {
            u64 v; int neg = 0, base = (*f == 'x' || *f == 'X') ? 16 : 10;
            if (*f == 'd') { long x = lng ? va_arg(ap, long) : va_arg(ap, int); neg = x < 0; v = neg ? -(u64)x : (u64)x; } else v = lng ? va_arg(ap, u64) : va_arg(ap, u32);
            char t[24]; int k = 0; do { int d = v % base; t[k++] = d < 10 ? '0' + d : (*f == 'X' ? 'A' : 'a') + d - 10; v /= base; } while (v);
            if (neg) b[i++] = '-'; while (k) b[i++] = t[--k]; len = i; break; }
        case '%': b[0] = '%'; len = 1; break;
        default: b[0] = *f; len = 1;
        }
        if (!left) for (int p = len; p < width; p++) PUT(zero ? '0' : ' ');
        for (int p = 0; p < len; p++) PUT(s[p]);
        if (left) for (int p = len; p < width; p++) PUT(' ');
    }
    if (max) out[n < max ? n : max - 1] = 0;
    return n;
}
int snprintf(char *o, size_t m, const char *f, ...) { va_list a; va_start(a, f); int r = vsnprintf(o, m, f, a); va_end(a); return r; }

void *z_readfile(const char *p, long *size) {
    int f = z_open(p, O_READ); if (f < 0) return 0;
    long n = z_seek(f, 0, 2); z_seek(f, 0, 0);
    char *b = malloc(n + 1);
    if (!b) { z_close(f); return 0; }
    long got = 0; while (got < n) { long r = z_read(f, b + got, n - got); if (r <= 0) break; got += r; }
    b[got] = 0; z_close(f); if (size) *size = got;
    return b;
}
static int write_raw(const char *p, const void *b, long n) {
    int f = z_open(p, O_WRITE | O_CREATE | O_TRUNC); if (f < 0) return f;
    long r = z_write(f, b, n); z_close(f); z_sync();
    return r == n ? 0 : (r < 0 ? (int)r : -5);
}
/* escritura segura en la memoria o el disco (/sys): se escribe entera en NOMBRE.zztmp, se guarda, y solo entonces
   sustituye al archivo. Si se va la luz a medias, queda el archivo viejo entero o el nuevo entero; nunca uno a medias.
   z_recover_tmp (al arrancar) termina un cambio que se quedo a mitad. En /rd (memoria) se escribe directamente. */
int z_writefile(const char *p, const void *b, long n) {
    if (strncmp(p, "/sys/", 5) || strlen(p) > 280) return write_raw(p, b, n);
    char t[300]; snprintf(t, 300, "%s.zztmp", p); int e = write_raw(t, b, n); if (e) { z_unlink(t); z_sync(); return e; }
    zdirent d; if (!z_stat(p, &d)) { if (d.is_dir) { z_unlink(t); return -21; } e = z_unlink(p); if (e) { z_unlink(t); z_sync(); return e; } z_sync(); }
    e = z_rename(t, p); z_sync(); return e; }
/* tras un corte: NOMBRE.zztmp con NOMBRE presente = escritura a medias (se borra); sin NOMBRE = el nuevo ya estaba
   entero y faltaba ponerle el nombre. Devuelve cuantos arreglo */
int z_recover_tmp(const char *dir, int depth) { zdirent e; int fixed = 0; char names[64][256]; int nn = 0;
    for (int i = 0; nn < 64 && !z_readdir(dir, i, &e); i++) { if (e.name[0] == '.') continue; if (e.is_dir) { if (depth > 0) { char sub[400]; snprintf(sub, 400, "%s/%s", dir, e.name); fixed += z_recover_tmp(sub, depth - 1); } continue; } if (z_ends(e.name, ".zztmp")) strncpy0(names[nn++], e.name, 256); }
    for (int i = 0; i < nn; i++) { char t[400], p[400]; snprintf(t, 400, "%s/%s", dir, names[i]); strcpy(p, t); p[strlen(p) - 6] = 0; zdirent d; if (!z_stat(p, &d)) z_unlink(t); else z_rename(t, p); fixed++; char lg[460]; snprintf(lg, 460, "[prueba] escritura recuperada %s", p); z_log(lg); }
    if (fixed) z_sync(); return fixed; }
const char *z_errstr(int e) {
    switch (e) { case -2: return "no existe"; case -5: return "error de E/S"; case -13: return "acceso denegado"; case -12: return "sin memoria"; case -17: return "ya existe"; case -20: return "no es una carpeta"; case -21: return "es una carpeta"; case -22: return "parametro no valido"; case -28: return "disco lleno"; case -30: return "solo lectura"; case -39: return "carpeta no vacia"; case -38: return "no disponible"; case -110: return "tiempo agotado"; case -111: return "conexion rechazada"; }
    return "error";
}
int z_parse_ip(const char *s, u32 *ip) {
    u32 r = 0; for (int i = 0; i < 4; i++) { if (!isdigit(*s)) return 0; int v = 0; while (isdigit(*s)) v = v * 10 + *s++ - '0'; if (v > 255) return 0; r |= (u32)v << (i * 8); if (i < 3) { if (*s != '.') return 0; s++; } }
    if (*s) return 0; *ip = r; return 1;
}
void z_fmt_ip(u32 ip, char *o) { snprintf(o, 16, "%u.%u.%u.%u", ip & 255, ip >> 8 & 255, ip >> 16 & 255, ip >> 24); }

#ifndef __arm__
/* matematicas con x87 */
double z_sqrt(double x) { __asm__("fsqrt" : "+t"(x)); return x; }
double z_sin(double x) { __asm__("fsin" : "+t"(x)); return x; }
double z_cos(double x) { __asm__("fcos" : "+t"(x)); return x; }
double z_tan(double x) { double o; __asm__("fptan; fstp %%st(0)" : "=t"(o) : "0"(x)); return o; }
double z_atan(double x) { double o; __asm__("fld1; fpatan" : "=t"(o) : "0"(x)); return o; }
double z_ln(double x) { double o; __asm__("fldln2; fxch; fyl2x" : "=t"(o) : "0"(x)); return o; }
double z_exp(double x) { double o; __asm__("fldln2; fdivrp; fld %%st(0); frndint; fsub %%st, %%st(1); fxch; f2xm1; fld1; faddp; fscale; fstp %%st(1)" : "=t"(o) : "0"(x)); return o; }
double z_pow(double a, double b) { if (a == 0) return b == 0 ? 1 : 0; if (a < 0) { long i = (long)b; if ((double)i != b) return 0; double r = z_exp(z_ln(-a) * b); return i & 1 ? -r : r; } return z_exp(z_ln(a) * b); }
double z_floor(double x) { long i = (long)x; double d = i; return d > x ? d - 1 : d; }
#endif
double z_fabs(double x) { return x < 0 ? -x : x; }
int strncasecmp(const char *a, const char *b, size_t n) { for (; n; n--, a++, b++) { int x = tolower((u8)*a), y = tolower((u8)*b); if (x != y) return x - y; if (!x) break; } return 0; }
