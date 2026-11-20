#include "kernel.h"

extern char __kstart[], __kend[];
u64 kernel_cr3; static u64 *kpml4;
static u8 *bitmap; static u64 max_pfn, free_pages, total_pages, hint;
static u64 early_cur, early_end;
#define IOBASE 0xFFFFC00000000000UL
static u64 io_next = IOBASE;

u64 V2P(const void *v) {
    u64 a = (u64)v;
    if (a >= KVMA) return a - KVMA + bi.kernel_phys;
    if (a >= DMAP && a < IOBASE) return a - DMAP;
    panic("V2P: direccion no directa %p", v);
}

static struct { u64 a, b; } resv[8]; static int nresv;
static void reserve(u64 a, u64 len) { if (len) { resv[nresv].a = a & ~(PAGE - 1); resv[nresv].b = ALIGN(a + len, PAGE); nresv++; } }
static int is_resv(u64 a, u64 len) { for (int i = 0; i < nresv; i++) if (a < resv[i].b && a + len > resv[i].a) return 1; return 0; }

static u64 early_alloc(void) {
    if (early_cur >= early_end) panic("sin memoria temprana");
    u64 p = early_cur; early_cur += PAGE;
    memset((void *)p, 0, PAGE);
    return p;
}

void mm_init(void) {
    mment_t *mm = (mment_t *)bi.mmap;
    u64 top = 4UL << 30, ram_top = 0;
    bi.kernel_size = (u64)__kend - (u64)__kstart;
    reserve(0, 1UL << 20); reserve(bi.kernel_phys, bi.kernel_size + (bi.boot_mode == 2 ? (16UL << 20) : 0));
    reserve(bi.initrd_phys, bi.initrd_size); reserve(bi.arena_phys, bi.arena_size);
    u64 total = 0;
    for (u32 i = 0; i < bi.mmap_count; i++) {
        mment_t *e = &mm[i]; u64 end = e->base + e->len;
        if (e->type == 1) { total += e->len; if (end > ram_top) ram_top = end; }
        if ((e->type == 1 || e->type == 3 || e->type == 4) && end > top) top = end;
    }
    if (top > (512UL << 30)) top = 512UL << 30;
    if (ram_top > top) ram_top = top;
    max_pfn = ram_top >> 12;
    u64 bm_pages = (max_pfn / 8 + PAGE) / PAGE;
    u64 pt_pages = (top >> 30) + 64 + 16;
    u64 need = (bm_pages + pt_pages) * PAGE;
    /* hueco libre por debajo de 4 GB (accesible con el mapa de identidad) */
    for (u32 i = 0; i < bi.mmap_count && !early_end; i++) {
        if (mm[i].type != 1) continue;
        u64 a = ALIGN(MAX(mm[i].base, 1UL << 20), PAGE), e = MIN(mm[i].base + mm[i].len, 4UL << 30);
        while (a + need <= e) {
            int hit = 0;
            for (int r = 0; r < nresv; r++) if (a < resv[r].b && a + need > resv[r].a) { a = resv[r].b; hit = 1; break; }
            if (!hit) { early_cur = a; early_end = a + need; break; }
        }
    }
    if (!early_end) panic("no hay memoria para arrancar");
    u64 early_base = early_cur;
    u64 bm_phys = early_cur; early_cur += bm_pages * PAGE;

    u64 *p4 = (u64 *)early_alloc();
    kernel_cr3 = (u64)p4;
    u64 *dpt = (u64 *)early_alloc(), *iopt = (u64 *)early_alloc(), *kpt = (u64 *)early_alloc(), *kpd = (u64 *)early_alloc();
    p4[256] = (u64)dpt | 3; p4[384] = (u64)iopt | 3; p4[511] = (u64)kpt | 3; kpt[510] = (u64)kpd | 3;
    for (int i = 0; i < 16; i++) kpd[i] = (bi.kernel_phys + i * (2UL << 20)) | PTE_P | PTE_W | PTE_PS;
    for (u64 g = 0; g < (top >> 30) + 1 && (g << 30) < top; g++) {
        u64 *pd = (u64 *)early_alloc();
        dpt[g] = (u64)pd | 3;
        for (int j = 0; j < 512; j++) {
            u64 a = (g << 30) + ((u64)j << 21); int ok = a == 0;
            for (u32 i = 0; i < bi.mmap_count && !ok; i++) {
                mment_t *e = &mm[i];
                if ((e->type == 1 || e->type == 3 || e->type == 4) && a < e->base + e->len && a + (2UL << 20) > e->base) ok = 1;
            }
            if (ok) pd[j] = a | PTE_P | PTE_W | PTE_PS;
        }
    }
    /* copiar mapa de memoria antes de perder la identidad */
    static mment_t saved[512]; u32 cnt = MIN(bi.mmap_count, 512);
    memcpy(saved, mm, cnt * sizeof(mment_t));
    write_cr3(kernel_cr3);
    kpml4 = P2V(kernel_cr3);
    bitmap = P2V(bm_phys);
    memset(bitmap, 0xFF, bm_pages * PAGE);
    for (u32 i = 0; i < cnt; i++) {
        if (saved[i].type != 1) continue;
        u64 a = ALIGN(saved[i].base, PAGE), e = (saved[i].base + saved[i].len) & ~(PAGE - 1);
        for (; a < e && (a >> 12) < max_pfn; a += PAGE) {
            if (is_resv(a, PAGE) || (a >= early_base && a < early_end)) continue;
            bitmap[a >> 15] &= ~(1 << ((a >> 12) & 7)); free_pages++;
        }
    }
    total_pages = total >> 12;
    kprintf("memoria: %lu MB en total, %lu MB libres\n", total >> 20, free_pages >> 8);
}

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
u64 pmm_alloc_contig(u64 n, u64 maxphys) {
    u64 f = irq_save(), lim = maxphys ? MIN(maxphys >> 12, max_pfn) : max_pfn, run = 0;
    for (u64 p = 256; p < lim; p++) {
        if (run == 0 && (p & 7) == 0 && bitmap[p >> 3] == 0xFF) { p += 7; continue; }
        if (USED(p)) { run = 0; continue; }
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
    u64 pg = ALIGN(n, PAGE) >> 12, p = pmm_alloc_contig(pg, 4UL << 30);
    if (!p) panic("sin memoria DMA");
    void *v = P2V(p); memset(v, 0, pg * PAGE);
    return v;
}
void dma_free(void *v, size_t n) { pmm_free_contig(V2P(v), ALIGN(n, PAGE) >> 12); }

/* --- tablas de paginas --- */
static u64 *pt_next(u64 *t, int i, int create, u64 fl) {
    if (t[i] & PTE_P) { if (fl & PTE_U) t[i] |= PTE_U; return P2V(t[i] & PTE_ADDR); }
    if (!create) return 0;
    u64 p = pmm_alloc();
    if (!p) return 0;
    memset(P2V(p), 0, PAGE);
    t[i] = p | PTE_P | PTE_W | (fl & PTE_U);
    return P2V(p);
}
static u64 *pte_of(u64 cr3, u64 va, int create, u64 fl) {
    u64 *t = P2V(cr3);
    for (int sh = 39; sh > 12; sh -= 9) { t = pt_next(t, (va >> sh) & 511, create, fl); if (!t) return 0; }
    return &t[(va >> 12) & 511];
}
int vm_map(u64 cr3, u64 va, u64 pa, u64 fl) {
    u64 *e = pte_of(cr3, va, 1, fl);
    if (!e) return -1;
    *e = (pa & PTE_ADDR) | fl | PTE_P;
    if (read_cr3() == cr3) invlpg(va);
    return 0;
}
void vm_unmap(u64 cr3, u64 va) { u64 *e = pte_of(cr3, va, 0, 0); if (e) { *e = 0; if (read_cr3() == cr3) invlpg(va); } }
u64 vm_lookup(u64 cr3, u64 va, u64 *fl) {
    u64 *e = pte_of(cr3, va, 0, 0);
    if (!e || !(*e & PTE_P)) return 0;
    if (fl) *fl = *e;
    return *e & PTE_ADDR;
}
u64 vm_new_space(void) {
    u64 p = pmm_alloc();
    if (!p) return 0;
    u64 *t = P2V(p); memset(t, 0, 2048); memcpy(t + 256, kpml4 + 256, 2048);
    return p;
}
static void free_level(u64 tp, int lvl) {
    u64 *t = P2V(tp);
    for (int i = 0; i < 512; i++) {
        if (!(t[i] & PTE_P)) continue;
        if (lvl == 1) { if (!(t[i] & PTE_NOFREE)) pmm_free(t[i] & PTE_ADDR); }
        else free_level(t[i] & PTE_ADDR, lvl - 1);
    }
    pmm_free(tp);
}
void vm_free_space(u64 cr3) {
    u64 *t = P2V(cr3);
    for (int i = 0; i < 256; i++) if (t[i] & PTE_P) free_level(t[i] & PTE_ADDR, 3);
    pmm_free(cr3);
}
static void *io_map(u64 phys, u64 size, u64 fl) {
    u64 off = phys & (PAGE - 1), n = ALIGN(size + off, PAGE);
    u64 f = irq_save(); u64 va = io_next; io_next += n + PAGE; irq_restore(f);
    for (u64 i = 0; i < n; i += PAGE) if (vm_map(kernel_cr3, va + i, (phys & ~(PAGE - 1)) + i, PTE_W | fl)) panic("ioremap");
    for (u64 i = 0; i < n; i += PAGE) invlpg(va + i);
    return (void *)(va + off);
}
void *ioremap(u64 p, u64 s) { return io_map(p, s, PTE_PCD | PTE_PWT); }
void *ioremap_wc(u64 p, u64 s) { return io_map(p, s, PTE_PWT); }
