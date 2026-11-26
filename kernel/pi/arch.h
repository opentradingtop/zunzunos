/* ZunzunOS - arquitectura ARMv7-A (Raspberry Pi 2: BCM2836, 4x Cortex-A7, 32 bits)
   Mapa de memoria virtual (TTBCR.N = 1):
     0x00000000-0x7FFFFFFF  procesos (TTBR0, una tabla por proceso)
       0x00400000           programa (APP_BASE), detras el monticulo (brk)
       0x30000000           lienzos de las ventanas (CANVAS_BASE + id * CANVAS_STEP)
       0x7FF00000           tope de la pila
     0x80000000-0xBFFFFFFF  RAM fisica 0-1 GB vista entera (DMAP); el kernel esta en 0x80008000
     0xC0000000-0xEFFFFFFF  ioremap (framebuffer, etc.), en paginas de 4 KB
     0xF2000000-0xF2FFFFFF  perifericos del SoC (fisica 0x3F000000)
     0xF3000000-0xF30FFFFF  perifericos locales de los nucleos (fisica 0x40000000) */
#pragma once

/* --- registros guardados al entrar al kernel (pi/start.S) --- */
typedef struct { u32 r[13]; u32 usp, ulr, klr, pc, cpsr; } regs_t;
#define SC_NUM(r) ((r)->r[7])
/* los argumentos llegan en 32 bits: se extiende el signo (los procesos pasan -1 como "sin limite") */
#define SC_ARGS(r, a, b, c, d, e) u64 a = (u64)(i64)(i32)(r)->r[0], b = (u64)(i64)(i32)(r)->r[1], c = (u64)(i64)(i32)(r)->r[2], \
    d = (u64)(i64)(i32)(r)->r[3], e = (u64)(i64)(i32)(r)->r[4]

#define DMAP 0x80000000UL
#define KVMA 0x80000000UL
#define P2V(p) ((void *)(u32)((u64)(p) + DMAP))
#define IOVA_BASE 0xC0000000UL
#define IOVA_END 0xF0000000UL
#define PERI_PHYS 0x3F000000UL
#define PERI_VA 0xF2000000UL
#define LOCAL_PHYS 0x40000000UL
#define LOCAL_VA 0xF3000000UL
#define PERI(off) (PERI_VA + (off))
#define LOCAL(off) (LOCAL_VA + (off))

#define APP_BASE 0x400000UL
#define STACK_TOP 0x7FF00000UL
#define STACK_SIZE (8UL << 20)
#define USER_TOP 0x80000000ULL
#define USER_BRK_MAX 0x30000000ULL
#define CANVAS_BASE 0x30000000ULL
#define CANVAS_STEP 0x800000ULL   /* 8 MB por ventana: hasta 1920x1080 */

/* banderas de vm_map (las mismas que en x86; pi/mm.c las traduce a descriptores ARM) */
#define PTE_P 1UL
#define PTE_W 2UL
#define PTE_U 4UL
#define PTE_PWT 8UL
#define PTE_PCD 16UL
#define PTE_PS 128UL
#define PTE_NOFREE (1UL << 9)

static inline u64 irq_save(void) { u32 f; __asm__ volatile("mrs %0, cpsr; cpsid i" : "=r"(f) : : "memory"); return f; }
static inline void irq_restore(u64 f) { if (!(f & 0x80)) __asm__ volatile("cpsie i" : : : "memory"); }
static inline void irq_on(void) { __asm__ volatile("cpsie i" : : : "memory"); }
static inline void irq_off(void) { __asm__ volatile("cpsid i" : : : "memory"); }
static inline void cpu_idle(void) { __asm__ volatile("dsb; cpsie i; wfi" : : : "memory"); }
static inline void cpu_halt(void) { __asm__ volatile("cpsid i; dsb; wfi" : : : "memory"); }
static inline void mb(void) { __asm__ volatile("dsb" : : : "memory"); }
/* contador del temporizador generico (sustituye al TSC) */
static inline u64 rdtsc(void) { u32 lo, hi; __asm__ volatile("isb; mrrc p15, 1, %0, %1, c14" : "=r"(lo), "=r"(hi)); return (u64)hi << 32 | lo; }
/* limpia e invalida la cache de datos sobre [p, p+n): antes y despues de que otro bus (GPU, DMA) lea o escriba */
static inline void dma_sync(const void *p, u64 n) {
    u32 a = (u32)p & ~63u, e = (u32)p + (u32)n;
    __asm__ volatile("dsb" : : : "memory");
    for (; a < e; a += 64) __asm__ volatile("mcr p15, 0, %0, c7, c14, 1" : : "r"(a) : "memory");
    __asm__ volatile("dsb" : : : "memory");
}
/* "cr3" = tabla de paginas de primer nivel de los procesos (TTBR0) */
u64 read_cr3(void); void write_cr3(u64);
static inline void tss_set_rsp0(u64 v) { (void)v; }   /* en ARM la pila del kernel de cada tarea es la sp del modo SVC */

struct task;
void arch_kthread_init(struct task *t, void (*fn)(void *), void *arg);
void arch_user_init(struct task *t, u64 pc, u64 sp, u64 arg);
void arch_code_sync(void *va, u32 n);   /* tras copiar codigo: caches de datos e instrucciones de acuerdo */

/* placa (pi/placa.c) */
int uart_init(void); void uart_putc(char c); int uart_getc(void);
int mbox_call(u32 *buf);   /* canal 8 (propiedades); buf alineado a 16 */
void gpio_func(int pin, int fn); void gpio_set(int pin, int on); int gpio_get(int pin); void gpio_pull(int pin, int mode);
extern u32 arm_mem_size;
extern int osk_on;   /* teclado en pantalla (wm.c) */
void lcd_dirty(int x0, int y0, int x1, int y1);   /* la pantalla SPI copia esta zona del framebuffer */
