/* ZunzunOS - CPU ARMv7: excepciones, temporizador generico, hilos, FPU y reloj (Raspberry Pi 2) */
#include "../kernel.h"

volatile u64 ticks; u64 tsc_khz; char cpu_brand[49];
static u32 cnt_frq, cnt_step; static u64 cnt_next, cnt_base;
extern void kthread_tramp(void); extern void isr_return(void);

#define CORE0_TIMER_CTL R32(LOCAL(0x40))
#define CORE0_IRQ_SRC R32(LOCAL(0x60))

void cpu_init(void) {
    u32 midr; __asm__ volatile("mrc p15, 0, %0, c0, c0, 0" : "=r"(midr));
    u32 part = (midr >> 4) & 0xFFF;
    const char *n = part == 0xC07 ? "ARM Cortex-A7" : part == 0xD03 ? "ARM Cortex-A53" : part == 0xC0F ? "ARM Cortex-A15" : "ARM";
    snprintf(cpu_brand, sizeof cpu_brand, "%s r%up%u (Raspberry Pi)", n, (midr >> 20) & 15, midr & 15);
}
void fpu_init_state(void *p) { memset(p, 0, 264); }
/* codigo recien copiado por la cache de datos (al cargar un programa): se lleva hasta el punto de unificacion y se
   invalida la cache de instrucciones, que no ve lo escrito por la otra. En QEMU no hace falta; en la Pi, si. */
void arch_code_sync(void *va, u32 n) {
    u32 a = (u32)va & ~63u, e = (u32)va + n;
    __asm__ volatile("dsb" : : : "memory");
    for (; a < e; a += 64) __asm__ volatile("mcr p15, 0, %0, c7, c11, 1" : : "r"(a) : "memory");   /* DCCMVAU */
    __asm__ volatile("dsb; mcr p15, 0, %0, c7, c5, 0; mcr p15, 0, %0, c7, c5, 6; dsb; isb" : : "r"(0) : "memory");   /* ICIALLU, BPIALL */
}

static inline u64 cnt_now(void) { return rdtsc(); }
static inline void cntv_cval(u64 v) { __asm__ volatile("mcrr p15, 3, %0, %1, c14" : : "r"((u32)v), "r"((u32)(v >> 32))); }
static inline void cntv_ctl(u32 v) { __asm__ volatile("mcr p15, 0, %0, c14, c3, 1; isb" : : "r"(v)); }

void udelay(u64 us) { u64 s = cnt_now(), n = us * (cnt_frq ? cnt_frq : 19200000) / 1000000; while (cnt_now() - s < n); }
void mdelay_busy(u64 ms) { udelay(ms * 1000); }

void timer_init(void) {
    __asm__ volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(cnt_frq));
    if (!cnt_frq) cnt_frq = 19200000;
    tsc_khz = cnt_frq / 1000; cnt_step = cnt_frq / HZ;
    cnt_base = cnt_now(); cnt_next = cnt_base + cnt_step; cntv_cval(cnt_next); cntv_ctl(1);
    CORE0_TIMER_CTL = 1 << 3;   /* nCNTVIRQ -> IRQ del nucleo 0 */
    __asm__ volatile("mcr p15, 0, %0, c14, c1, 0" : : "r"(2));   /* CNTKCTL.PL0VCTEN: los programas leen el contador */
    kprintf("timer: generico a %u Hz, %u interrupciones por segundo\n", cnt_frq, HZ);
}

static void user_check(regs_t *r) {
    if ((r->cpsr & 0x1F) == 0x10 && cur && cur->proc && cur->proc->killed) { irq_on(); proc_exit(-9); }
}
void irq_entrada(regs_t *r) {
    u32 src = CORE0_IRQ_SRC;
    if (src & (1 << 3)) {
        /* ticks se calcula con el contador: si se pierden interrupciones (QEMU va a saltos) el reloj no se atrasa */
        u64 now = cnt_now();
        ticks = (now - cnt_base) / cnt_step;
        cnt_next = cnt_base + (ticks + 1) * cnt_step;
        cntv_cval(cnt_next);
        timer_tick();
    }
    user_check(r);
}
void svc_entrada(regs_t *r) {
    irq_on();
    u64 v = syscall_dispatch(r);
    r->r[0] = (u32)v; r->r[1] = (u32)(v >> 32);   /* los programas leen 64 bits en r0:r1 */
    irq_off();
    user_check(r);
}
static const char *kinds[] = {"?", "acceso a datos", "lectura de instruccion", "instruccion no definida"};
void abort_entrada(regs_t *r, int kind) {
    u32 st = 0, addr = 0;
    if (kind == 1) { __asm__ volatile("mrc p15, 0, %0, c5, c0, 0; mrc p15, 0, %1, c6, c0, 0" : "=r"(st), "=r"(addr)); }
    else if (kind == 2) { __asm__ volatile("mrc p15, 0, %0, c5, c0, 1; mrc p15, 0, %1, c6, c0, 2" : "=r"(st), "=r"(addr)); }
    u32 fs = ((st >> 6) & 0x10) | (st & 0xF);
    if (kind != 3 && (fs == 5 || fs == 7) && vm_fault(addr)) return;   /* pagina del monticulo o de la pila: se crea */
    int user = (r->cpsr & 0x1F) == 0x10;
    if (user) {
        kprintf("[%s] fallo de %s en %p pc=%p estado=%x: proceso terminado\n", cur->name, kinds[kind], (void *)addr, (void *)r->pc, st);
        irq_on(); proc_exit(-11);
    }
    panic("fallo de %s en el kernel addr=%p pc=%p lr=%p estado=%x tarea=%s", kinds[kind], (void *)addr, (void *)r->pc, (void *)r->klr, st, cur ? cur->name : "?");
}

void arch_kthread_init(task_t *t, void (*fn)(void *), void *arg) {
    /* switch_context saca r4-r11 y lr (9 palabras); despues la pila queda alineada a 8 para llamar a fn */
    u32 *s = (u32 *)(u32)t->kstack_top - 11;
    memset(s, 0, 44); s[0] = (u32)fn; s[1] = (u32)arg; s[8] = (u32)kthread_tramp;
    t->rsp = (u32)s;
}
void arch_user_init(task_t *t, u64 pc, u64 sp, u64 arg) {
    regs_t *r = (regs_t *)((u32)t->kstack_top - sizeof(regs_t));
    memset(r, 0, sizeof *r);
    r->r[0] = (u32)arg; r->usp = (u32)sp; r->pc = (u32)pc & ~1u; r->cpsr = 0x10 | (pc & 1 ? 0x20 : 0);
    u32 *s = (u32 *)r - 9;   /* r4-r11 y lr = isr_return, que saca el marco */
    memset(s, 0, 36); s[8] = (u32)isr_return;
    t->rsp = (u32)s;
}

/* --- reloj: la Pi no tiene reloj con pila; se lleva por software desde la fecha de compilacion
   (o la que ponga el usuario en Configuracion, que se guarda en /sys/ZUNZUN/RELOJ.TXT) --- */
static i64 clock_base; static u64 clock_tick0;
static i64 days_from_civil(int y, int m, int d) { y -= m <= 2; i64 era = (y >= 0 ? y : y - 399) / 400; int yoe = y - era * 400, doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy; return era * 146097 + doe - 719468; }
static void civil_from_days(i64 z, int *y, int *m, int *d) { z += 719468; i64 era = (z >= 0 ? z : z - 146096) / 146097; int doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1; *m = mp + (mp < 10 ? 3 : -9); *y = yoe + era * 400 + (*m <= 2); }
static void clock_default(void) {
    static const char *mon = "JanFebMarAprMayJunJulAugSepOctNovDec"; const char *D = __DATE__, *T = __TIME__; int m = 1;
    for (int i = 0; i < 12; i++) if (!memcmp(D, mon + i * 3, 3)) m = i + 1;
    int d = (D[4] == ' ' ? 0 : D[4] - '0') * 10 + D[5] - '0', y = (D[7] - '0') * 1000 + (D[8] - '0') * 100 + (D[9] - '0') * 10 + D[10] - '0';
    clock_base = days_from_civil(y, m, d) * 86400 + ((T[0] - '0') * 10 + T[1] - '0') * 3600 + ((T[3] - '0') * 10 + T[4] - '0') * 60 + (T[6] - '0') * 10 + T[7] - '0';
    clock_tick0 = 0;
}
i64 clock_now(void) { if (!clock_base) clock_default(); return clock_base + (i64)((ticks - clock_tick0) / HZ); }
void clock_set(i64 t) { clock_base = t; clock_tick0 = ticks; }
void rtc_read(u32 *o) {
    i64 t = clock_now(); int y, m, d; civil_from_days(t / 86400, &y, &m, &d); int s = t % 86400;
    o[0] = s % 60; o[1] = s / 60 % 60; o[2] = s / 3600; o[3] = d; o[4] = m; o[5] = y;
}
void rtc_write(const u32 *t) {
    clock_set(days_from_civil(t[5] < 100 ? t[5] + 2000 : t[5], t[4], t[3]) * 86400 + t[2] * 3600 + t[1] * 60 + t[0]);
    extern void clock_save(void); clock_save();
}
