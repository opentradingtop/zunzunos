#pragma once
/* entorno minimo para probar en el Mac el AES-256 y el PBKDF2 de kernel/cifrado.c */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64; typedef int64_t i64;
#define MIN(a, b) ((a) < (b) ? (a) : (b))
enum { E_ACCES = -13, E_INVAL = -22, E_EXIST = -17, E_IO = -5, E_NOMEM = -12 };
enum { O_READ = 1, O_WRITE = 2, O_CREATE = 4, O_TRUNC = 8, O_APPEND = 16 };
typedef struct { u32 h[8]; u64 len; u8 buf[64]; int n; } sha_t;
void sha_init(sha_t *c); void sha_up(sha_t *c, const void *d, long n); void sha_end(sha_t *c, u8 *o);
typedef struct fsops { i64 (*read)(void *, void *, u64); i64 (*write)(void *, const void *, u64); } fsops_t;
typedef struct mount { const fsops_t *ops; } mount_t;
typedef struct file { mount_t *m; u64 pos, size; int enc; u8 nonce[8]; } file_t;
typedef struct { u64 size; u32 is_dir; } zdirent_t;
extern volatile u64 ticks;
static inline u64 rdtsc(void) { static u64 t = 12345; return t += 977; }
static inline int vfs_stat(const char *p, zdirent_t *d) { (void)p; (void)d; return -2; }
static inline void *vfs_slurp(const char *p, u64 *n) { (void)p; (void)n; return 0; }
static inline int vfs_open(const char *p, int f, file_t **o) { (void)p; (void)f; (void)o; return -1; }
static inline i64 vfs_write(file_t *f, const void *b, u64 n) { (void)f; (void)b; return n; }
static inline void vfs_close(file_t *f) { (void)f; } static inline void vfs_sync(void) {} static inline int vfs_unlink(const char *p) { (void)p; return 0; }
static inline int sess_guest(void) { return 0; }
#define kprintf printf
#define kmalloc malloc
#define kfree free
static inline void strncpy0(char *d, const char *s, int n) { strncpy(d, s, n - 1); d[n - 1] = 0; }
