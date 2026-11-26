/* Memoria fisica (mapa de bits de paginas) y kmalloc; comun a x86_64 y ARM.
   El codigo de cada arquitectura (mm.c, pi/mm.c) rellena pmm_bitmap/pmm_max_pfn/pmm_nfree/pmm_ntotal. */
#include "kernel.h"
u8 *pmm_bitmap; u64 pmm_max_pfn, pmm_nfree, pmm_ntotal; static u64 hint;
#define bitmap pmm_bitmap
#define max_pfn pmm_max_pfn
#define free_pages pmm_nfree
#define total_pages pmm_ntotal

u64 pmm_free_pages(void) { return free_pages; }
u64 pmm_total_pages(void) { return total_pages; }
#define USED(p) (bitmap[(p) >> 3] & (1 << ((p) & 7)))
#define SET(p) (bitmap[(p) >> 3] |= 1 << ((p) & 7))
#define CLR(p) (bitmap[(p) >> 3] &= ~(1 << ((p) & 7)))

u64 pmm_alloc(void) {
    u64 f = irq_save();
    for (u64 n = 0, p = hint; n < max_pfn; n++, p++) {
        if (p >= max_pfn) p = 0;
        if ((p & 7) == 0 && bitmap[p >> 3] == 0xFF) { n += 7; p += 7; continue; }
        if (!USED(p)) { SET(p); hint = p + 1; free_pages--; irq_restore(f); return p << 12; }
    }
    irq_restore(f);
    return 0;
}
void pmm_free(u64 a) { u64 f = irq_save(), p = a >> 12; if (p < max_pfn && USED(p)) { CLR(p); free_pages++; } irq_restore(f); }
/* n paginas seguidas por debajo de maxphys (0 = sin limite) que empiezan en un multiplo de al paginas */
u64 pmm_alloc_contig_al(u64 n, u64 maxphys, u64 al) {
    u64 f = irq_save(), lim = maxphys ? MIN(maxphys >> 12, max_pfn) : max_pfn, run = 0;
    for (u64 p = 256; p < lim; p++) {
        if (run == 0 && (p & 7) == 0 && bitmap[p >> 3] == 0xFF) { p += 7; continue; }
        if (USED(p)) { run = 0; continue; }
        if (run == 0 && al > 1 && (p % al)) continue;
        if (++run == n) {
            u64 s = p - n + 1;
            for (u64 i = s; i <= p; i++) SET(i);
            free_pages -= n; irq_restore(f);
            return s << 12;
        }
    }
    irq_restore(f);
    return 0;
}
u64 pmm_alloc_contig(u64 n, u64 maxphys) { return pmm_alloc_contig_al(n, maxphys, 1); }
void pmm_free_contig(u64 a, u64 n) { for (u64 i = 0; i < n; i++) pmm_free(a + i * PAGE); }

/* --- kmalloc: clases de 32 a 2048 bytes; lo grande va a paginas contiguas --- */
typedef struct fnode { struct fnode *next; } fnode_t;
#define NCLS 7
static fnode_t *freel[NCLS];
typedef struct { u64 cls; u64 pages; } khdr_t;
void *kmalloc(size_t n) {
    if (!n) n = 1;
    size_t t = n + sizeof(khdr_t);
    int c = -1;
    for (int i = 0; i < NCLS; i++) if (t <= (32UL << i)) { c = i; break; }
    if (c < 0) {
        u64 pg = ALIGN(t, PAGE) >> 12, p = pmm_alloc_contig(pg, 0);
        if (!p) return 0;
        khdr_t *h = P2V(p); h->cls = 99; h->pages = pg;
        return h + 1;
    }
    u64 f = irq_save();
    if (!freel[c]) {
        u64 p = pmm_alloc();
        if (!p) { irq_restore(f); return 0; }
        u8 *b = P2V(p); size_t s = 32UL << c;
        for (size_t o = 0; o + s <= PAGE; o += s) { fnode_t *x = (fnode_t *)(b + o); x->next = freel[c]; freel[c] = x; }
    }
    fnode_t *x = freel[c]; freel[c] = x->next;
    irq_restore(f);
    khdr_t *h = (khdr_t *)x; h->cls = c; h->pages = 0;
    return h + 1;
}
void *kzalloc(size_t n) { void *p = kmalloc(n); if (p) memset(p, 0, n); return p; }
void kfree(void *v) {
    if (!v) return;
    khdr_t *h = (khdr_t *)v - 1;
    if (h->cls == 99) { pmm_free_contig(V2P(h), h->pages); return; }
    if (h->cls >= NCLS) panic("kfree invalido %p", v);
    int c = h->cls; u64 f = irq_save();
    fnode_t *x = (fnode_t *)h; x->next = freel[c]; freel[c] = x;
    irq_restore(f);
}
void *dma_alloc(size_t n) {
    u64 pg = ALIGN(n, PAGE) >> 12, p = pmm_alloc_contig(pg, (u64)4 << 30);
    if (!p) panic("sin memoria DMA");
    void *v = P2V(p); memset(v, 0, pg * PAGE);
    return v;
}
void dma_free(void *v, size_t n) { pmm_free_contig(V2P(v), ALIGN(n, PAGE) >> 12); }

