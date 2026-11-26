/* ZunzunOS - memoria virtual ARMv7 (descriptores cortos). Cada proceso tiene su tabla de primer nivel de 8 KB
   (TTBR0, 0-2 GB); el kernel usa tabla_kernel (TTBR1), que es la misma para todos.
   Las tablas de segundo nivel ocupan una pagina: 1 KB de descriptores para el hardware y detras, en 512 bytes,
   las banderas de vm_map de cada entrada (PTE_U, PTE_W, PTE_NOFREE...). */
#include "../kernel.h"

extern char __kstart[], __kend[]; extern u32 tabla_kernel[];
extern u8 *pmm_bitmap; extern u64 pmm_max_pfn, pmm_nfree, pmm_ntotal;
u64 kernel_cr3; u32 arm_mem_size; static u32 cur_ttbr0; static u32 io_next = IOVA_BASE;

#define D_SMALL 2
#define D_XN 1
#define D_B 4
#define D_C 8
#define D_AP_K (1 << 4)
#define D_AP_U (3 << 4)
#define D_TEX1 (1 << 6)
#define D_AP2 (1 << 9)
#define D_S (1 << 10)
#define D_NG (1 << 11)

static inline void pte_sync(void *p) { __asm__ volatile("dsb; mcr p15, 0, %0, c7, c11, 1; dsb" : : "r"(p) : "memory"); }
static inline void tlb_va(u32 va) { __asm__ volatile("dsb; mcr p15, 0, %0, c8, c7, 1; mcr p15, 0, %1, c7, c5, 6; dsb; isb" : : "r"(va & ~0xFFFu), "r"(0) : "memory"); }
static inline void tlb_all(void) { __asm__ volatile("dsb; mcr p15, 0, %0, c8, c7, 0; mcr p15, 0, %0, c7, c5, 6; dsb; isb" : : "r"(0) : "memory"); }

u64 read_cr3(void) { return cur_ttbr0; }
void write_cr3(u64 v) {
    cur_ttbr0 = (u32)v;
    __asm__ volatile("dsb; mcr p15, 0, %0, c2, c0, 0; isb" : : "r"((u32)v | 0x4A) : "memory");
    tlb_all();
}

u64 V2P(const void *v) {
    u32 a = (u32)v;
    if (a >= DMAP && a < IOVA_BASE) return a - DMAP;
    if (a >= IOVA_BASE && a < IOVA_END) { u64 fl; u64 p = vm_lookup(0, a, &fl); if (p) return p | (a & 0xFFF); }
    panic("V2P: direccion no directa %p", v);
}

static u32 descr(u64 fl) {
    u32 d = D_SMALL;
    if (fl & PTE_U) d |= D_AP_U | D_NG | D_TEX1 | D_C | D_B | D_S;
    else if ((fl & PTE_PCD)) d |= D_AP_K | D_XN | D_B;              /* dispositivo */
    else if ((fl & PTE_PWT)) d |= D_AP_K | D_XN | D_TEX1 | D_S;     /* normal sin cache (framebuffer) */
    else d |= D_AP_K | D_TEX1 | D_C | D_B | D_S;
    if (!(fl & PTE_W)) d |= D_AP2;
    return d;
}
static u32 *l1_of(u64 cr3, u32 va) { return va >= 0x80000000u ? tabla_kernel : (u32 *)P2V(cr3); }
static u32 *pte_of(u64 cr3, u32 va, int create, u16 **flags) {
    u32 *l1 = l1_of(cr3, va), *e1 = &l1[va >> 20];
    if ((*e1 & 3) != 1) {
        if (*e1 & 3) return 0;   /* seccion de 1 MB */
        if (!create) return 0;
        u64 p = pmm_alloc(); if (!p) return 0;
        memset(P2V(p), 0, PAGE);
        *e1 = (u32)p | 1; pte_sync(e1);
    }
    u8 *l2 = P2V(*e1 & ~0xFFFu); int i = (va >> 12) & 255;
    if (flags) *flags = (u16 *)(l2 + 1024) + i;
    return (u32 *)l2 + i;
}
int vm_map(u64 cr3, u64 va, u64 pa, u64 fl) {
    u16 *f; u32 *e = pte_of(cr3, (u32)va, 1, &f);
    if (!e) return -1;
    *e = ((u32)pa & ~0xFFFu) | descr(fl); *f = (u16)(fl | PTE_P); pte_sync(e);
    if ((u32)va >= 0x80000000u || cur_ttbr0 == (u32)cr3) tlb_va((u32)va);
    return 0;
}
void vm_unmap(u64 cr3, u64 va) {
    u16 *f; u32 *e = pte_of(cr3, (u32)va, 0, &f);
    if (e) { *e = 0; *f = 0; pte_sync(e); if ((u32)va >= 0x80000000u || cur_ttbr0 == (u32)cr3) tlb_va((u32)va); }
}
u64 vm_lookup(u64 cr3, u64 va, u64 *fl) {
    u16 *f; u32 *e = pte_of(cr3, (u32)va, 0, &f);
    if (!e || !(*e & 2)) return 0;
    if (fl) *fl = *f;
    return *e & ~0xFFFu;
}
u64 vm_new_space(void) {
    u64 p = pmm_alloc_contig_al(2, 0, 2);   /* 8 KB alineados a 8 KB */
    if (!p) return 0;
    memset(P2V(p), 0, 2 * PAGE); dma_sync(P2V(p), 2 * PAGE);
    return p;
}
void vm_free_space(u64 cr3) {
    u32 *l1 = P2V(cr3);
    for (int i = 0; i < 2048; i++) {
        if ((l1[i] & 3) != 1) continue;
        u8 *l2 = P2V(l1[i] & ~0xFFFu); u32 *e = (u32 *)l2; u16 *f = (u16 *)(l2 + 1024);
        for (int j = 0; j < 256; j++) if ((e[j] & 2) && !(f[j] & PTE_NOFREE)) pmm_free(e[j] & ~0xFFFu);
        pmm_free(l1[i] & ~0xFFFu);
    }
    pmm_free_contig(cr3, 2);
}

static void *io_map(u64 phys, u64 size, u64 fl) {
    u32 off = (u32)phys & (PAGE - 1), n = ALIGN((u32)size + off, PAGE);
    u64 f = irq_save(); u32 va = io_next; io_next += n + PAGE; irq_restore(f);
    if (io_next > IOVA_END) panic("ioremap: sin espacio");
    for (u32 i = 0; i < n; i += PAGE) if (vm_map(0, va + i, (phys & ~(u64)(PAGE - 1)) + i, PTE_W | fl)) panic("ioremap");
    return (void *)(va + off);
}
void *ioremap(u64 p, u64 s) {
    if (p >= PERI_PHYS && p + s <= PERI_PHYS + (16 << 20)) return (void *)(u32)(PERI_VA + (p - PERI_PHYS));
    if (p >= LOCAL_PHYS && p + s <= LOCAL_PHYS + (1 << 20)) return (void *)(u32)(LOCAL_VA + (p - LOCAL_PHYS));
    return io_map(p, s, PTE_PCD | PTE_PWT);
}
void *ioremap_wc(u64 p, u64 s) { return io_map(p, s, PTE_PWT); }

static struct { u32 a, b; } resv[8]; static int nresv;
static void reserve(u32 a, u32 len) { if (len) { resv[nresv].a = a & ~(PAGE - 1); resv[nresv].b = ALIGN(a + len, PAGE); nresv++; } }
static int is_resv(u32 a) { for (int i = 0; i < nresv; i++) if (a >= resv[i].a && a < resv[i].b) return 1; return 0; }

void mm_init(void) {
    u32 top = arm_mem_size ? arm_mem_size : 0x3C000000;
    if (top > 0x3F000000) top = 0x3F000000;
    /* la RAM del procesador grafico (por encima de top) no se ve con cache: alli va el framebuffer */
    for (u32 i = top >> 20; i < 0x3F0; i++) tabla_kernel[0x800 + i] = 0;
    dma_sync(tabla_kernel + 0x800, 0x3F0 * 4); tlb_all();
    u32 kend = (u32)__kend - DMAP;
    reserve(0, 0x8000); reserve(0x8000, kend - 0x8000); reserve((u32)bi.initrd_phys, (u32)bi.initrd_size);
    u32 after = MAX(kend, (u32)ALIGN(bi.initrd_phys + bi.initrd_size, PAGE));
    pmm_max_pfn = top >> 12;
    u32 bm_bytes = ALIGN((u32)(pmm_max_pfn / 8 + 1), PAGE);
    reserve(after, bm_bytes);
    pmm_bitmap = P2V(after);
    memset(pmm_bitmap, 0xFF, bm_bytes);
    for (u32 a = 0; a < top; a += PAGE) {
        if (is_resv(a)) continue;
        pmm_bitmap[a >> 15] &= ~(1 << ((a >> 12) & 7)); pmm_nfree++;
    }
    pmm_ntotal = top >> 12;
    kernel_cr3 = vm_new_space();
    write_cr3(kernel_cr3);   /* desde aqui ya no hay mapa de identidad */
    bi.kernel_phys = 0x8000; bi.kernel_size = kend - 0x8000;
    kprintf("memoria: %u MB para el ARM, %u MB libres\n", top >> 20, (u32)(pmm_nfree >> 8));
}
