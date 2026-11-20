#include "kernel.h"

typedef struct PACKED { u16 lo, sel; u8 ist, type; u16 mid; u32 hi, zero; } idte_t;
typedef struct PACKED { u32 r0; u64 rsp[3]; u64 r1; u64 ist[7]; u64 r2; u16 r3, iomap; } tss_t;
static idte_t idt[256]; static tss_t tss; static u64 gdt[9];
static struct PACKED { u16 lim; u64 base; } gdtr, idtr;
extern char isr_table[]; extern void load_gdt(void *);
volatile u64 ticks; u64 tsc_khz; char cpu_brand[49]; static volatile u8 *lapic;
static u8 fx_default[512] __attribute__((aligned(16)));
#define LAPIC(r) (*(volatile u32 *)(lapic + (r)))

void tss_set_rsp0(u64 v) { tss.rsp[0] = v; }
void fpu_save(void *p) { __asm__ volatile("fxsave64 (%0)" : : "r"(p) : "memory"); }
void fpu_load(void *p) { __asm__ volatile("fxrstor64 (%0)" : : "r"(p)); }
void fpu_init_state(void *p) { memcpy(p, fx_default, 512); }

static void set_gate(int n, int dpl) {
    u64 a = (u64)isr_table + n * 16;
    idt[n] = (idte_t){ a & 0xFFFF, 0x08, 0, (u8)(0x8E | dpl << 5), (a >> 16) & 0xFFFF, a >> 32, 0 };
}

void cpu_init(void) {
    gdt[1] = 0x00AF9A000000FFFF; gdt[2] = 0x00CF92000000FFFF;
    gdt[4] = 0x00CFF2000000FFFF; gdt[5] = 0x00AFFA000000FFFF;
    u64 t = (u64)&tss; u64 l = sizeof tss - 1;
    gdt[6] = (l & 0xFFFF) | (t & 0xFFFFFF) << 16 | 0x89UL << 40 | (t >> 24 & 0xFF) << 56;
    gdt[7] = t >> 32;
    tss.iomap = sizeof tss;
    gdtr.lim = sizeof gdt - 1; gdtr.base = (u64)gdt;
    load_gdt(&gdtr);
    for (int i = 0; i < 256; i++) set_gate(i, 0);
    set_gate(0x40, 3);
    idtr.lim = sizeof idt - 1; idtr.base = (u64)idt;
    __asm__ volatile("lidt %0" : : "m"(idtr));
    /* FPU/SSE */
    u64 c;
    __asm__ volatile("mov %%cr0,%0" : "=r"(c)); c &= ~4UL; c |= 2 | 32; __asm__ volatile("mov %0,%%cr0" : : "r"(c));
    __asm__ volatile("mov %%cr4,%0" : "=r"(c)); c |= (3 << 9); __asm__ volatile("mov %0,%%cr4" : : "r"(c));
    __asm__ volatile("fninit"); fpu_save(fx_default);
    /* PAT: entrada 1 = write-combining */
    u64 pat = rdmsr(0x277); pat = (pat & ~0xFF00UL) | (1UL << 8); wrmsr(0x277, pat);
    u32 r[12];
    for (u32 i = 0; i < 3; i++)
        __asm__ volatile("cpuid" : "=a"(r[i*4]), "=b"(r[i*4+1]), "=c"(r[i*4+2]), "=d"(r[i*4+3]) : "a"(0x80000002 + i), "c"(0));
    memcpy(cpu_brand, r, 48); cpu_brand[48] = 0;
    char *p = cpu_brand; while (*p == ' ') p++;
    memmove(cpu_brand, p, strlen(p) + 1);
}

void udelay(u64 us) { u64 s = rdtsc(), n = us * (tsc_khz ? tsc_khz : 1000000) / 1000; while (rdtsc() - s < n) __asm__("pause"); }
void mdelay_busy(u64 ms) { udelay(ms * 1000); }

void timer_init(void) {
    outb(0x20, 0x11); outb(0xA0, 0x11); outb(0x21, 0x50); outb(0xA1, 0x58);
    outb(0x21, 4); outb(0xA1, 2); outb(0x21, 1); outb(0xA1, 1); outb(0x21, 0xFF); outb(0xA1, 0xFF);
    u64 base = rdmsr(0x1B); wrmsr(0x1B, base | 0x800);
    lapic = ioremap(base & 0xFFFFF000, 4096);
    LAPIC(0xF0) = 0x1FF; LAPIC(0x80) = 0; LAPIC(0x320) = 0x10000; LAPIC(0x3E0) = 3;
    /* calibrar con el canal 2 del PIT: 10 ms */
    u8 g = inb(0x61); outb(0x61, (g & 0xFD) | 1); outb(0x43, 0xB0);
    outb(0x42, 11932 & 0xFF); outb(0x42, 11932 >> 8);
    (void)inb(0x61); outb(0x61, (inb(0x61) & 0xFD)); outb(0x61, inb(0x61) | 1);
    extern u32 acpi_pm_tmr;
    LAPIC(0x380) = 0xFFFFFFFF; u64 t0 = rdtsc(); u64 guard = 0; int ok = 1; const char *src = "PIT";
    while (!(inb(0x61) & 0x20)) if (++guard > 2000000) { ok = 0; break; }
    if (!ok && acpi_pm_tmr) {          /* PCs recientes sin PIT: temporizador ACPI */
        src = "ACPI"; LAPIC(0x380) = 0xFFFFFFFF; t0 = rdtsc(); u32 a = inl(acpi_pm_tmr) & 0xFFFFFF; ok = 1; guard = 0;
        while (((inl(acpi_pm_tmr) - a) & 0xFFFFFF) < 35795) if (++guard > 5000000) { ok = 0; break; }
    }
    u64 t1 = rdtsc(); u32 left = LAPIC(0x390); LAPIC(0x380) = 0;
    u64 per10 = 0xFFFFFFFFUL - left;
    if (!ok || per10 < 1000) { per10 = 625000; tsc_khz = 2000000; src = "valores por defecto"; }
    else tsc_khz = (t1 - t0) / 10;
    kprintf("timer: calibrado con %s\n", src);
    LAPIC(0x320) = 0x20 | (1 << 17);
    LAPIC(0x380) = (u32)(per10 / 10 * 1000 / HZ);
    kprintf("timer: LAPIC %lu ticks/10ms, TSC %lu MHz\n", per10, tsc_khz / 1000);
}

static const char *exc_names[] = {"division","debug","NMI","breakpoint","overflow","bound","opcode invalido","sin FPU","doble fallo","?","TSS","segmento","pila","proteccion general","fallo de pagina","?","FPU","alineacion","machine check","SIMD"};

void isr_dispatch(regs_t *r) {
    u64 v = r->vec;
    if (v == 0x20) {
        ticks++; if (lapic) LAPIC(0xB0) = 0;
        timer_tick();
    } else if (v == 0x40) {
        __asm__("sti");
        r->rax = syscall_dispatch(r);
        __asm__("cli");
    } else if (v == 14) {
        u64 a = read_cr2();
        if (!(r->err & 1) && vm_fault(a)) return;
        if (r->cs & 3) {
            kprintf("[%s] fallo de pagina en %p rip=%p err=%lx: proceso terminado\n", cur->name, (void *)a, (void *)r->rip, r->err);
            __asm__("sti"); proc_exit(-11);
        }
        panic("fallo de pagina kernel addr=%p rip=%p err=%lx tarea=%s", (void *)a, (void *)r->rip, r->err, cur ? cur->name : "?");
    } else if (v < 32) {
        if (r->cs & 3) {
            kprintf("[%s] excepcion %s rip=%p: proceso terminado\n", cur->name, v < 20 ? exc_names[v] : "?", (void *)r->rip);
            __asm__("sti"); proc_exit(-(int)v);
        }
        panic("excepcion %s (%lu) rip=%p err=%lx rsp=%p tarea=%s", v < 20 ? exc_names[v] : "?", v, (void *)r->rip, r->err, (void *)r->rsp, cur ? cur->name : "?");
    } else if (v != 0xFF) { if (lapic) LAPIC(0xB0) = 0; }
    if ((r->cs & 3) && cur->proc && cur->proc->killed) { __asm__("sti"); proc_exit(-9); }
}
