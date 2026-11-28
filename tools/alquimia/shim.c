/* Llamadas al sistema de ZunzunOS hechas con la libc, para probar el motor de Alquimia en Linux o macOS.
   Las rutas son las del anfitrion. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
typedef struct { uint64_t size; uint32_t is_dir; uint32_t mtime; char name[256]; } zdirent;
int z_open(const char *p, int f) { int fl = 0; if ((f & 3) == 3) fl = O_RDWR; else if (f & 2) fl = O_WRONLY; else fl = O_RDONLY; if (f & 4) fl |= O_CREAT; if (f & 8) fl |= O_TRUNC; if (f & 16) fl |= O_APPEND; int d = open(p, fl, 0644); return d < 0 ? -errno : d; }
int z_close(int f) { return close(f); }
long z_read(int f, void *b, long n) { long r = read(f, b, n); return r < 0 ? -errno : r; }
long z_write(int f, const void *b, long n) { long r = write(f, b, n); return r < 0 ? -errno : r; }
long z_seek(int f, long o, int w) { long r = lseek(f, o, w == 1 ? SEEK_CUR : w == 2 ? SEEK_END : SEEK_SET); return r < 0 ? -errno : r; }
int z_stat(const char *p, zdirent *d) { struct stat s; if (stat(p, &s)) return -errno; memset(d, 0, sizeof *d); d->size = s.st_size; d->is_dir = S_ISDIR(s.st_mode); const char *n = strrchr(p, '/'); snprintf(d->name, 256, "%s", n ? n + 1 : p); return 0; }
int z_readdir(const char *p, int i, zdirent *d) { DIR *D = opendir(p); if (!D) return -errno; struct dirent *e; int k = 0; while ((e = readdir(D))) { if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue; if (k++ == i) { char f[1200]; snprintf(f, sizeof f, "%s/%s", p, e->d_name); z_stat(f, d); snprintf(d->name, 256, "%s", e->d_name); closedir(D); return 0; } } closedir(D); return -2; }
int z_mkdir(const char *p) { return mkdir(p, 0755) ? -errno : 0; }
int z_unlink(const char *p) { return unlink(p) ? -errno : 0; }
int z_rename(const char *a, const char *b) { return rename(a, b) ? -errno : 0; }
void z_sync(void) {}
void z_time(uint32_t *t) { time_t x = time(0); struct tm m; localtime_r(&x, &m); t[0] = m.tm_sec; t[1] = m.tm_min; t[2] = m.tm_hour; t[3] = m.tm_mday; t[4] = m.tm_mon + 1; t[5] = m.tm_year + 1900; }
uint64_t z_ms(void) { struct timeval v; gettimeofday(&v, 0); return (uint64_t)v.tv_sec * 1000 + v.tv_usec / 1000; }
void z_log(const char *s) { if (getenv("ALQ_LOG")) fprintf(stderr, "%s\n", s); }
#ifndef UI_HOST
const char *tr(const char *s) { return s; }
#endif
const char *z_errstr(int e) { static char b[64]; snprintf(b, 64, "error %d (%s)", e, strerror(-e)); return b; }
void *z_readfile(const char *p, long *n) { FILE *f = fopen(p, "rb"); if (!f) return 0; fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET); char *b = malloc(l + 1); if (fread(b, 1, l, f) != (size_t)l) { fclose(f); free(b); return 0; } fclose(f); b[l] = 0; if (n) *n = l; return b; }
int z_writefile(const char *p, const void *b, long n) { FILE *f = fopen(p, "wb"); if (!f) return -errno; long w = fwrite(b, 1, n, f); fclose(f); return w == n ? 0 : -5; }

char *strncpy0(char *d, const char *s, size_t n) { if (!n) return d; size_t i = 0; for (; i + 1 < n && s[i]; i++) d[i] = s[i]; d[i] = 0; return d; }
int utf8_put(uint32_t c, char *o) { if (c < 0x80) { o[0] = c; return 1; } if (c < 0x800) { o[0] = 0xC0 | c >> 6; o[1] = 0x80 | (c & 63); return 2; } if (c < 0x10000) { o[0] = 0xE0 | c >> 12; o[1] = 0x80 | ((c >> 6) & 63); o[2] = 0x80 | (c & 63); return 3; } o[0] = 0xF0 | c >> 18; o[1] = 0x80 | ((c >> 12) & 63); o[2] = 0x80 | ((c >> 6) & 63); o[3] = 0x80 | (c & 63); return 4; }
int utf8_next(const char **ps) { const unsigned char *s = (const unsigned char *)*ps; int c = *s, n = 0; if (c < 0x80) n = 0; else if ((c & 0xE0) == 0xC0) { c &= 31; n = 1; } else if ((c & 0xF0) == 0xE0) { c &= 15; n = 2; } else if ((c & 0xF8) == 0xF0) { c &= 7; n = 3; } else { *ps += 1; return 0xFFFD; } s++; for (int i = 0; i < n; i++) { if ((*s & 0xC0) != 0x80) { *ps = (const char *)s; return 0xFFFD; } c = c << 6 | (*s++ & 63); } *ps = (const char *)s; return c; }
uint32_t rnd(void) { return (uint32_t)rand() << 16 ^ rand(); }
double z_sqrt(double x) { extern double sqrt(double); return sqrt(x); }
double z_floor(double x) { extern double floor(double); return floor(x); }
double z_fabs(double x) { return x < 0 ? -x : x; }
double z_pow(double a, double b) { extern double pow(double, double); return pow(a, b); }
double z_ln(double x) { extern double log(double); return log(x); }
double z_exp(double x) { extern double exp(double); return exp(x); }
double z_sin(double x) { extern double sin(double); return sin(x); }
double z_cos(double x) { extern double cos(double); return cos(x); }
size_t z_msize(void *p) { (void)p; return 0; }
int z_thread(void (*fn)(void *), void *a, int s) { (void)s; fn(a); return 0; }
#ifndef UI_HOST
int z_mounts(void *o, int n) { (void)o; (void)n; return 0; }
#endif

int z_netinfo(void *n) { (void)n; return -1; }
double z_tan(double x) { extern double tan(double); return tan(x); }
#ifndef UI_HOST
unsigned int TH[64];
#endif
#ifndef UI_HOST
const unsigned char *z_glyph(int cp) { static unsigned char g[64]; (void)cp; return g; }
#endif
uint64_t zh_cycles(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000u + t.tv_nsec; }
