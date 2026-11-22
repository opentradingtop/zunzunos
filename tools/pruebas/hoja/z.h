#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <time.h>
typedef uint8_t u8; typedef uint32_t u32; typedef uint64_t u64;
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
typedef struct zwin zwin; typedef struct { char *p; long n, cap; } sb_t;
static inline void strncpy0(char *d, const char *s, size_t n) { if (!n) return; strncpy(d, s, n - 1); d[n - 1] = 0; }
#define z_sqrt sqrt
#define z_pow pow
#define z_sin sin
#define z_cos cos
#define z_tan tan
#define z_atan atan
#define z_ln log
#define z_exp exp
#define z_floor floor
#define z_fabs fabs
static inline void z_time(u32 *t) { t[0]=0;t[1]=30;t[2]=12;t[3]=22;t[4]=11;t[5]=2026; }
static inline u64 z_ms(void) { return 12345; }
typedef struct { int lang; } ztheme_t; static ztheme_t TH;
