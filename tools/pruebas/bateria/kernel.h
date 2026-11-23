#pragma once
/* entorno minimo para compilar kernel/acpi.c en el Mac y probar la lectura de la bateria */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64; typedef int8_t i8; typedef int64_t i64;
#define PACKED __attribute__((packed))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define HZ 1000
enum { E_NOENT = -2, E_IO = -5 };
typedef struct { u64 rsdp; } bi_t; extern bi_t bi; extern volatile u64 ticks;
static inline void *P2V(u64 a) { (void)a; static u8 z[4096]; return z; }
void *ioremap(u64 pa, u64 n); u8 inb(u16 p); u16 inw(u16 p); void outb(u16 p, u8 v); void outw(u16 p, u16 v);
static inline void udelay(int u) { (void)u; } static inline void mdelay_busy(int m) { (void)m; } static inline void vfs_sync(void) {} static inline void blk_sync(void) {}
static inline u64 irq_save(void) { return 0; } static inline void irq_restore(u64 f) { (void)f; }
#define kprintf printf
#define __asm__(...) ((void)0)   /* el Mac de la prueba puede no ser x86 */
