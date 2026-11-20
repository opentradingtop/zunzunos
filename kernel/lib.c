#include "kernel.h"

void *memcpy(void *d, const void *s, size_t n) {
    void *r = d;
    __asm__ volatile("mov %%rcx,%%rdx; shr $3,%%rcx; rep movsq; mov %%rdx,%%rcx; and $7,%%rcx; rep movsb"
                     : "+D"(d), "+S"(s), "+c"(n) : : "rdx", "memory");
    return r;
}
void *memset(void *d, int v, size_t n) {
    void *r = d;
    __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(v) : "memory");
    return r;
}
void *memmove(void *d, const void *s, size_t n) {
    u8 *a = d; const u8 *b = s;
    if (a == b || !n) return d;
    if (a < b || a >= b + n) return memcpy(d, s, n);
    while (n--) a[n] = b[n];
    return d;
}
int memcmp(const void *x, const void *y, size_t n) {
    const u8 *a = x, *b = y;
    for (; n; n--, a++, b++) if (*a != *b) return *a - *b;
    return 0;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
int strcmp(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return (u8)*a - (u8)*b; }
int strncmp(const char *a, const char *b, size_t n) {
    for (; n; n--, a++, b++) { if (*a != *b) return (u8)*a - (u8)*b; if (!*a) break; }
    return 0;
}
static int lc(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
int strcasecmp(const char *a, const char *b) {
    while (*a && lc((u8)*a) == lc((u8)*b)) a++, b++;
    return lc((u8)*a) - lc((u8)*b);
}
char *strncpy0(char *d, const char *s, size_t n) {
    size_t i = 0;
    if (!n) return d;
    for (; i + 1 < n && s[i]; i++) d[i] = s[i];
    d[i] = 0;
    return d;
}
char *strchr(const char *s, int c) { for (;; s++) { if (*s == (char)c) return (char *)s; if (!*s) return 0; } }
char *strrchr(const char *s, int c) { char *r = 0; for (;; s++) { if (*s == (char)c) r = (char *)s; if (!*s) return r; } }

int utf8_decode(const char **ps) {
    const u8 *s = (const u8 *)*ps; u32 c = *s; int n = 0;
    if (!c) return 0;
    if (c < 0x80) n = 0;
    else if ((c >> 5) == 6) { c &= 31; n = 1; }
    else if ((c >> 4) == 14) { c &= 15; n = 2; }
    else if ((c >> 3) == 30) { c &= 7; n = 3; }
    else { *ps = (const char *)s + 1; return '?'; }
    s++;
    while (n--) { if ((*s & 0xC0) != 0x80) { *ps = (const char *)s; return '?'; } c = (c << 6) | (*s++ & 63); }
    *ps = (const char *)s;
    return c;
}
int utf8_encode(u32 c, char *o) {
    if (c < 0x80) { o[0] = c; return 1; }
    if (c < 0x800) { o[0] = 0xC0 | c >> 6; o[1] = 0x80 | (c & 63); return 2; }
    if (c < 0x10000) { o[0] = 0xE0 | c >> 12; o[1] = 0x80 | ((c >> 6) & 63); o[2] = 0x80 | (c & 63); return 3; }
    o[0] = 0xF0 | c >> 18; o[1] = 0x80 | ((c >> 12) & 63); o[2] = 0x80 | ((c >> 6) & 63); o[3] = 0x80 | (c & 63); return 4;
}

int vsnprintf(char *out, size_t max, const char *f, va_list ap) {
    size_t n = 0;
#define PUT(c) do { if (n + 1 < max) out[n] = (c); n++; } while (0)
    for (; *f; f++) {
        if (*f != '%') { PUT(*f); continue; }
        f++;
        int zero = 0, width = 0, left = 0, lng = 0;
        if (*f == '-') { left = 1; f++; }
        if (*f == '0') { zero = 1; f++; }
        while (*f >= '0' && *f <= '9') width = width * 10 + *f++ - '0';
        while (*f == 'l' || *f == 'z') { lng = 1; f++; }
        char b[24]; int i = 0; const char *s = b; int len;
        switch (*f) {
        case 'c': b[0] = (char)va_arg(ap, int); len = 1; break;
        case 's': s = va_arg(ap, const char *); if (!s) s = "(null)"; len = strlen(s); break;
        case 'd': case 'u': case 'x': case 'X': case 'p': {
            u64 v; int neg = 0; int base = (*f == 'd' || *f == 'u') ? 10 : 16;
            if (*f == 'p') { v = (u64)va_arg(ap, void *); lng = 1; }
            else if (*f == 'd') { i64 x = lng ? va_arg(ap, i64) : va_arg(ap, int); neg = x < 0; v = neg ? -(u64)x : (u64)x; }
            else v = lng ? va_arg(ap, u64) : va_arg(ap, u32);
            char t[24]; int k = 0;
            do { int d = v % base; t[k++] = d < 10 ? '0' + d : (*f == 'X' ? 'A' : 'a') + d - 10; v /= base; } while (v);
            if (neg) b[i++] = '-';
            while (k) b[i++] = t[--k];
            len = i; break; }
        case '%': b[0] = '%'; len = 1; break;
        default: b[0] = *f; len = 1; if (!*f) { f--; len = 0; }
        }
        if (!left) for (int p = len; p < width; p++) PUT(zero ? '0' : ' ');
        for (int p = 0; p < len; p++) PUT(s[p]);
        if (left) for (int p = len; p < width; p++) PUT(' ');
    }
    if (max) out[n < max ? n : max - 1] = 0;
    return n;
}
int snprintf(char *o, size_t m, const char *f, ...) { va_list a; va_start(a, f); int r = vsnprintf(o, m, f, a); va_end(a); return r; }

#define COM1 0x3F8
#define DMESG 65536
static char dmesg[DMESG]; static u32 dm_pos, dm_total; static int ser_ok;
void serial_init(void) {
    outb(COM1 + 1, 0); outb(COM1 + 3, 0x80); outb(COM1, 1); outb(COM1 + 1, 0); outb(COM1 + 3, 3); outb(COM1 + 2, 0xC7); outb(COM1 + 4, 3);
    ser_ok = inb(COM1 + 5) != 0xFF;
}
static void sput(char c) {
    if (ser_ok) { int t = 20000; while (!(inb(COM1 + 5) & 0x20) && --t); outb(COM1, c); }
    dmesg[dm_pos] = c; dm_pos = (dm_pos + 1) % DMESG; dm_total++;
}
void kprintf(const char *f, ...) {
    char b[512]; va_list a; va_start(a, f); vsnprintf(b, sizeof b, f, a); va_end(a);
    u64 fl = irq_save();
    for (char *p = b; *p; p++) { if (*p == '\n') sput('\r'); sput(*p); }
    irq_restore(fl);
}
int dmesg_read(char *o, int max) {
    u64 fl = irq_save();
    u32 n = dm_total < DMESG ? dm_total : DMESG; u32 start = dm_total < DMESG ? 0 : dm_pos;
    if (n > (u32)max) { start = (start + n - max) % DMESG; n = max; }
    for (u32 i = 0; i < n; i++) o[i] = dmesg[(start + i) % DMESG];
    irq_restore(fl);
    return n;
}
void panic(const char *f, ...) {
    char b[256]; va_list a; va_start(a, f); vsnprintf(b, sizeof b, f, a); va_end(a);
    __asm__("cli");
    kprintf("\n*** PANICO: %s\n", b);
    fb_early_panic(b);
    for (;;) __asm__("hlt");
}
