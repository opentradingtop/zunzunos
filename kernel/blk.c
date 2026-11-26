/* Capa de bloques, particiones y drivers IDE, AHCI y NVMe */
#include "kernel.h"
blkdev_t *blkdevs[MAXBLK]; int blk_count;
#define CN 1024
typedef struct { blkdev_t *d; u64 lba; u8 *data; } centry_t;
static centry_t cache[CN]; static mutex_t cache_lock;
#define BOUNCE (128 * 1024)

static blkdev_t *root(blkdev_t *d, u64 *lba) { while (d->parent) { *lba += d->offset; d = d->parent; } return d; }
static centry_t *cslot(blkdev_t *d, u64 lba) { return &cache[(lba * 31 + ((u64)d >> 6)) % CN]; }
blkdev_t *blk_new(const char *prefix) {
    static struct { char p[8]; int n; } cnt[8]; int i;
    for (i = 0; i < 8; i++) if (!cnt[i].p[0] || !strcmp(cnt[i].p, prefix)) break;
    if (i == 8) i = 7;
    strncpy0(cnt[i].p, prefix, 8);
    blkdev_t *d = kzalloc(sizeof *d); snprintf(d->name, 16, "%s%d", prefix, cnt[i].n++); d->present = 1;
    return d;
}
static int raw(blkdev_t *d, u64 lba, u32 n, void *b, int wr) {
    if (!d->present) return E_IO;
    if (lba + n > d->sectors) return E_INVAL;
    u8 *p = b; u32 max = MIN(d->maxsect ? d->maxsect : 256, BOUNCE / d->ssize);
    mutex_lock(&d->lock); int r = 0;
    while (n && !r) {
        u32 c = MIN(n, max); u64 len = (u64)c * d->ssize;
        if (wr) { memcpy(d->bounce, p, len); r = d->write(d, lba, c, d->bounce); }
        else { r = d->read(d, lba, c, d->bounce); if (!r) memcpy(p, d->bounce, len); }
        p += len; lba += c; n -= c;
    }
    mutex_unlock(&d->lock);
    return r ? E_IO : 0;
}
int blk_read(blkdev_t *dv, u64 lba, u32 n, void *b) {
    blkdev_t *d = root(dv, &lba);
    if (n != 1 || d->ssize > 4096) return raw(d, lba, n, b, 0);
    mutex_lock(&cache_lock); centry_t *e = cslot(d, lba);
    if (e->d == d && e->lba == lba && e->data) { memcpy(b, e->data, d->ssize); mutex_unlock(&cache_lock); return 0; }
    mutex_unlock(&cache_lock);
    int r = raw(d, lba, 1, b, 0);
    if (!r) { mutex_lock(&cache_lock); if (!e->data) e->data = kmalloc(4096); if (e->data) { memcpy(e->data, b, d->ssize); e->d = d; e->lba = lba; } mutex_unlock(&cache_lock); }
    return r;
}
int blk_write(blkdev_t *dv, u64 lba, u32 n, const void *b) {
    blkdev_t *d = root(dv, &lba); d->wcount++;
    mutex_lock(&cache_lock);
    if (n > CN) { for (int i = 0; i < CN; i++) if (cache[i].d == d) cache[i].d = 0; }
    else for (u32 i = 0; i < n; i++) { centry_t *e = cslot(d, lba + i); if (e->d == d && e->lba == lba + i) { if (d->ssize <= 4096 && e->data) memcpy(e->data, (const u8 *)b + (u64)i * d->ssize, d->ssize); else e->d = 0; } } /* la cache se actualiza con lo escrito */
    mutex_unlock(&cache_lock);
    return raw(d, lba, n, (void *)b, 1);
}
void blk_sync(void) { for (int i = 0; i < blk_count; i++) { blkdev_t *d = blkdevs[i]; if (d && !d->parent && d->present && d->flush) { mutex_lock(&d->lock); d->flush(d); mutex_unlock(&d->lock); } } }
int blk_eject(const char *n) { blkdev_t *d = 0; for (int i = 0; i < blk_count; i++) if (blkdevs[i] && !strcmp(blkdevs[i]->name, n)) d = blkdevs[i]; if (!d) return E_NOENT; while (d->parent) d = d->parent; if (!d->removable) return E_INVAL; vfs_sync(); blk_sync(); blk_remove(d); return 0; }
blkdev_t *blk_find(const char *n) { for (int i = 0; i < blk_count; i++) if (blkdevs[i] && !strcmp(blkdevs[i]->name, n)) return blkdevs[i]; return 0; }
void blk_remove(blkdev_t *d) {
    kprintf("disco retirado: %s\n", d->name); d->present = 0; vfs_umount_dev(d);
    mutex_lock(&cache_lock); for (int i = 0; i < CN; i++) if (cache[i].d == d) cache[i].d = 0; mutex_unlock(&cache_lock);
    for (int i = 0; i < blk_count; i++) if (blkdevs[i] == d || (blkdevs[i] && blkdevs[i]->parent == d)) blkdevs[i] = 0;
}
static void add_list(blkdev_t *d) { for (int i = 0; i < blk_count; i++) if (!blkdevs[i]) { blkdevs[i] = d; return; } if (blk_count < MAXBLK) blkdevs[blk_count++] = d; }
static void add_part(blkdev_t *d, int n, u64 start, u64 cnt) {
    if (!cnt || start + cnt > d->sectors) return;
    blkdev_t *p = kzalloc(sizeof *p); snprintf(p->name, 16, "%sp%d", d->name, n);
    p->ssize = d->ssize; p->sectors = cnt; p->offset = start; p->parent = d; p->present = 1; p->removable = d->removable;
    add_list(p); fs_probe(p);
}
void fs_probe(blkdev_t *d) {
    if (fat_probe(d, d->name) == 0) return;
    if (ntfs_probe(d, d->name) == 0) return;
    if (exfat_probe(d, d->name) == 0) return;
    if (ext_probe(d, d->name) == 0) return;
    iso_probe(d, d->name);
}
static void scan_parts(blkdev_t *d);
void blk_register(blkdev_t *d) {
    d->bounce = dma_alloc(BOUNCE); add_list(d);
    kprintf("disco %s: %s, %lu MB, sector %u\n", d->name, d->model, d->sectors * d->ssize >> 20, d->ssize);
    scan_parts(d);
}
int blk_add_part(blkdev_t *d, u64 st, u64 cnt) { if (d->parent || !cnt || st + cnt > d->sectors) return E_INVAL; for (int n = 1; n < 64; n++) { char nm[16]; snprintf(nm, 16, "%sp%d", d->name, n); if (!blk_find(nm)) { add_part(d, n, st, cnt); return 0; } } return E_NOMEM; }
int blk_rescan(blkdev_t *d) {
    if (d->parent) { vfs_umount_dev(d); fs_probe(d); return 0; }
    vfs_umount_dev(d);
    for (int i = 0; i < blk_count; i++) if (blkdevs[i] && blkdevs[i]->parent == d) blkdevs[i] = 0;
    mutex_lock(&cache_lock); for (int i = 0; i < CN; i++) if (cache[i].d == d) cache[i].d = 0; mutex_unlock(&cache_lock);
    scan_parts(d); return 0;
}
static void scan_parts(blkdev_t *d) {
    u8 *s = kmalloc(MAX(d->ssize, 4096U)); int parts = 0;
    if (d->ssize == 2048 || blk_read(d, 0, 1, s)) { if (d->ssize == 2048) fs_probe(d); kfree(s); return; }
    if (s[510] == 0x55 && s[511] == 0xAA) {
        u8 *e = s + 446; int gpt = 0; u8 tmp[64]; memcpy(tmp, e, 64);
        for (int i = 0; i < 4; i++) if (tmp[i * 16 + 4] == 0xEE) gpt = 1;
        if (gpt && !blk_read(d, 1, 1, s) && !memcmp(s, "EFI PART", 8)) {
            u64 lba = *(u64 *)(s + 72); u32 num = *(u32 *)(s + 80), esz = *(u32 *)(s + 84); u8 *t = kmalloc(d->ssize); u32 per = d->ssize / esz;
            for (u32 i = 0; i < MIN(num, 128U); i++) {
                if (i % per == 0 && blk_read(d, lba + i / per, 1, t)) break;
                u8 *x = t + (i % per) * esz; u64 a = *(u64 *)(x + 32), b = *(u64 *)(x + 40), z = 0;
                if (a && memcmp(x, &z, 8)) { add_part(d, ++parts, a, b - a + 1); }
            }
            kfree(t);
        } else if (!gpt) for (int i = 0; i < 4; i++) {
            u8 *x = tmp + i * 16; u8 t = x[4]; u32 a = *(u32 *)(x + 8), n = *(u32 *)(x + 12);
            if (t && t != 5 && t != 0xF && t != 0x85 && (x[0] & 0x7F) == 0 && a) { add_part(d, ++parts, a, n); }
        }
    }
    if (!parts) fs_probe(d);
    kfree(s);
}

/* ===== IDE / ATAPI (PIO) ===== */
#ifdef __arm__
/* la Raspberry Pi no tiene ATA, AHCI ni NVMe: solo la tarjeta SD (sdhci.c) */
void ata_init(void) {} void ahci_init(void) {} void nvme_init(void) {}
#else
typedef struct { u16 io, ctl; int slave, atapi, lba48; } ata_t;
static int ata_wait(ata_t *a, u8 mask, u8 val, int ms) {
    u64 end = ticks + ms;
    for (;;) { u8 s = inb(a->io + 7); if (s != 0xFF && !(s & 0x80) && (s & 1) && val) return -1; if ((s & mask) == val) return 0; if (ticks > end) return -1; yield(); }
}
static void ata_sel(ata_t *a, u32 hi) { outb(a->io + 6, 0xE0 | a->slave << 4 | hi); udelay(2); }
static int ata_rw(blkdev_t *d, u64 lba, u32 n, void *buf, int wr) {
    ata_t *a = d->drv; u16 *p = buf;
    if (ata_wait(a, 0x80, 0, 3000)) return -1;
    ata_sel(a, a->lba48 ? 0 : (lba >> 24) & 15);
    if (a->lba48) { outb(a->io + 2, n >> 8); outb(a->io + 3, lba >> 24); outb(a->io + 4, lba >> 32); outb(a->io + 5, lba >> 40); }
    outb(a->io + 2, n); outb(a->io + 3, lba); outb(a->io + 4, lba >> 8); outb(a->io + 5, lba >> 16);
    outb(a->io + 7, a->lba48 ? (wr ? 0x34 : 0x24) : (wr ? 0x30 : 0x20));
    for (u32 i = 0; i < n; i++) {
        if (ata_wait(a, 0x88, 8, 5000)) return -1;
        if (wr) __asm__ volatile("rep outsw" : "+S"(p), "=c"((int){0}) : "c"(256), "d"(a->io));
        else __asm__ volatile("rep insw" : "+D"(p), "=c"((int){0}) : "c"(256), "d"(a->io) : "memory");
        p += 0; if (!wr) {}
    }
    if (wr) { if (ata_wait(a, 0x80, 0, 5000)) return -1; outb(a->io + 7, a->lba48 ? 0xEA : 0xE7); ata_wait(a, 0x80, 0, 5000); }
    return 0;
}
static void ata_srst(ata_t *a) { outb(a->ctl, 6); udelay(20); outb(a->ctl, 2); msleep(5); ata_wait(a, 0x80, 0, 3000); }
static int ata_read(blkdev_t *d, u64 l, u32 n, void *b) { int r = ata_rw(d, l, n, b, 0); if (r) ata_srst(d->drv); return r; }
static int ata_write(blkdev_t *d, u64 l, u32 n, const void *b) { int r = ata_rw(d, l, n, (void *)b, 1); if (r) ata_srst(d->drv); return r; }
static int atapi_cmd(ata_t *a, const u8 *pkt, void *buf, u32 len) {
    if (ata_wait(a, 0x80, 0, 3000)) return -1;
    ata_sel(a, 0); outb(a->io + 1, 0); outb(a->io + 4, len & 0xFF); outb(a->io + 5, len >> 8); outb(a->io + 7, 0xA0);
    if (ata_wait(a, 0x88, 8, 3000)) return -1;
    for (int i = 0; i < 6; i++) outw(a->io, pkt[i * 2] | pkt[i * 2 + 1] << 8);
    u8 *p = buf; u32 got = 0;
    while (got < len) {
        udelay(1); if (ata_wait(a, 0x80, 0, 8000)) return -1;
        u8 s = inb(a->io + 7); if (s & 1) return -1; if (!(s & 8)) break;
        u32 c = inb(a->io + 4) | inb(a->io + 5) << 8;
        for (u32 i = 0; i < c / 2; i++) { u16 v = inw(a->io); if (got + 1 < len) { p[got] = v; p[got + 1] = v >> 8; } got += 2; }
    }
    return ata_wait(a, 0x80, 0, 3000);
}
static int atapi_read(blkdev_t *d, u64 lba, u32 n, void *b) {
    u8 k[12] = {0x28, 0, lba >> 24, lba >> 16, lba >> 8, lba, 0, n >> 8, n, 0, 0, 0};
    return atapi_cmd(d->drv, k, b, n * 2048);
}
static int no_write(blkdev_t *d, u64 l, u32 n, const void *b) { return -1; }
static void swapstr(char *o, u16 *w, int n) { for (int i = 0; i < n; i++) { o[i * 2] = w[i] >> 8; o[i * 2 + 1] = w[i]; } o[n * 2] = 0; for (int i = n * 2 - 1; i >= 0 && o[i] == ' '; i--) o[i] = 0; }
static void ata_probe(u16 io, u16 ctl, int slave) {
    ata_t t = {io, ctl, slave, 0, 0}; static u16 id[256];
    outb(ctl, 2); outb(io + 6, 0xA0 | slave << 4); udelay(10);
    if (inb(io + 7) == 0xFF) return;
    outb(io + 2, 0); outb(io + 3, 0); outb(io + 4, 0); outb(io + 5, 0); outb(io + 7, 0xEC); udelay(50);
    if (inb(io + 7) == 0) return;
    u64 end = ticks + 500; int cd = 0;
    while (inb(io + 7) & 0x80) if (ticks > end) return;
    if (inb(io + 4) == 0x14 && inb(io + 5) == 0xEB) { cd = 1; outb(io + 7, 0xA1); udelay(50); }
    end = ticks + 500;
    for (;;) { u8 s = inb(io + 7); if (s & 1) return; if (s & 8) break; if (ticks > end) return; }
    for (int i = 0; i < 256; i++) id[i] = inw(io);
    ata_t *a = kmalloc(sizeof *a); *a = t; a->atapi = cd; blkdev_t *d = blk_new(cd ? "cd" : "ata"); d->drv = a; swapstr(d->model, id + 27, 20); d->maxsect = 128;
    if (cd) {
        static u8 cap[8]; u8 k[12] = {0x25}; d->ssize = 2048; d->read = atapi_read; d->write = no_write; d->removable = 1;
        if (atapi_cmd(a, k, cap, 8)) { d->sectors = 0; kprintf("%s: unidad sin disco\n", d->name); return; }
        d->sectors = (u64)(cap[0] << 24 | cap[1] << 16 | cap[2] << 8 | cap[3]) + 1;
    } else {
        a->lba48 = !!(id[83] & 0x400); d->ssize = 512; d->read = ata_read; d->write = ata_write;
        d->sectors = a->lba48 ? *(u64 *)(id + 100) : *(u32 *)(id + 60);
    }
    blk_register(d);
}
void ata_init(void) {
    for (int i = 0; i < pci_count; i++) {
        pci_t *p = &pci_devs[i]; if (p->cls != 1 || p->sub != 1) continue;
        pci_enable(p, 0, 1, 1);
        u16 a = (p->pif & 1) && p->bar[0] > 1 ? p->bar[0] & ~3 : 0x1F0, ac = (p->pif & 1) && p->bar[1] > 1 ? (p->bar[1] & ~3) + 2 : 0x3F6;
        u16 b = (p->pif & 4) && p->bar[2] > 1 ? p->bar[2] & ~3 : 0x170, bc = (p->pif & 4) && p->bar[3] > 1 ? (p->bar[3] & ~3) + 2 : 0x376;
        for (int s = 0; s < 2; s++) { ata_probe(a, ac, s); ata_probe(b, bc, s); }
    }
}

/* ===== AHCI ===== */
typedef struct { volatile u8 *hba, *port; u32 *cl; u8 *fis, *ct; } ahci_t;
#define PR(r) R32(a->port + (r))
static int ahci_cmd(ahci_t *a, const u8 *cfis, void *buf, u32 len, int wr) {
    u64 end = ticks + 3000; while (PR(0x20) & 0x88) { if (ticks > end) return -1; yield(); }
    memcpy(a->ct, cfis, 20); u64 ph = len ? V2P(buf) : 0;
    u32 *prd = (u32 *)(a->ct + 0x80); prd[0] = ph; prd[1] = ph >> 32; prd[2] = 0; prd[3] = len - 1;
    a->cl[0] = 5 | (wr ? 1 << 6 : 0) | (len ? 1u << 16 : 0); a->cl[1] = 0; PR(0x10) = 0xFFFFFFFF; mb(); PR(0x38) = 1;
    end = ticks + 15000;
    int bad = 0; while (PR(0x38) & 1) { if (PR(0x10) & (1 << 30)) { bad = 1; break; } if (ticks > end) { bad = 1; break; } yield(); }
    if (PR(0x10) & (1 << 30)) bad = 1;
    if (bad) { PR(0x18) &= ~1u; for (int i = 0; i < 100 && (PR(0x18) & 0x8000); i++) msleep(5); PR(0x30) = 0xFFFFFFFF; PR(0x10) = 0xFFFFFFFF; if (PR(0x20) & 0x88) { PR(0x18) |= 8; for (int i = 0; i < 100 && (PR(0x18) & 8); i++) msleep(5); } PR(0x18) |= 1; return -1; }
    return 0;
}
static void h2d(u8 *f, u8 cmd, u64 lba, u32 n) { memset(f, 0, 20); f[0] = 0x27; f[1] = 0x80; f[2] = cmd; f[4] = lba; f[5] = lba >> 8; f[6] = lba >> 16; f[7] = 0x40; f[8] = lba >> 24; f[9] = lba >> 32; f[10] = lba >> 40; f[12] = n; f[13] = n >> 8; }
static int ahci_rd(blkdev_t *d, u64 l, u32 n, void *b) { u8 f[20]; h2d(f, 0x25, l, n); return ahci_cmd(d->drv, f, b, n * d->ssize, 0); }
static int ahci_wr(blkdev_t *d, u64 l, u32 n, const void *b) { u8 f[20]; h2d(f, 0x35, l, n); return ahci_cmd(d->drv, f, (void *)b, n * d->ssize, 1); }
static int ahci_fl(blkdev_t *d) { u8 f[20]; h2d(f, 0xEA, 0, 0); return ahci_cmd(d->drv, f, 0, 0, 0); }
void ahci_init(void) {
    for (int i = 0; i < pci_count; i++) {
        pci_t *p = &pci_devs[i]; if (p->cls != 1 || p->sub != 6) continue;
        pci_enable(p, 1, 0, 1);
        volatile u8 *h = ioremap(pci_bar(p, 5), 0x1100 + 0x80 * 32);
        if (R32(h + 0x24) & 1) { R32(h + 0x28) |= 2; for (int t = 0; t < 200 && (R32(h + 0x28) & 1); t++) msleep(5); }
        R32(h + 4) |= 1u << 31;
        u32 pi = R32(h + 0x0C);
        for (int n = 0; n < 32; n++) {
            if (!(pi & (1u << n))) continue;
            volatile u8 *port = h + 0x100 + n * 0x80; ahci_t *a = kzalloc(sizeof *a); a->hba = h; a->port = port;
            PR(0x18) &= ~1u; PR(0x18) &= ~0x10u;
            for (int t = 0; t < 100 && (PR(0x18) & 0xC000); t++) msleep(5);
            u8 *m = dma_alloc(8192); a->cl = (u32 *)m; a->fis = m + 1024; a->ct = m + 4096;
            u64 ph = V2P(m); PR(0) = ph; PR(4) = ph >> 32; PR(8) = ph + 1024; PR(0xC) = (ph + 1024) >> 32;
            a->cl[2] = ph + 4096; a->cl[3] = (ph + 4096) >> 32;
            PR(0x18) |= 0x10; PR(0x30) = 0xFFFFFFFF; PR(0x10) = 0xFFFFFFFF;
            PR(0x18) |= 2 | 4;
            for (int t = 0; t < 100 && (PR(0x28) & 15) != 3; t++) msleep(2);
            if ((PR(0x28) & 15) != 3) { dma_free(m, 8192); kfree(a); continue; }
            for (int t = 0; t < 200 && (PR(0x20) & 0x88); t++) msleep(5);
            u32 sig = PR(0x24); PR(0x18) |= 1;
            if (sig == 0xEB140101) { kprintf("ahci%d: unidad ATAPI ignorada\n", n); continue; }
            u16 *id = dma_alloc(4096); u8 f[20]; h2d(f, 0xEC, 0, 0); f[7] = 0;
            if (ahci_cmd(a, f, id, 512, 0)) { kprintf("ahci: identify fallo puerto %d\n", n); continue; }
            blkdev_t *d = blk_new("ahci"); d->drv = a; d->ssize = 512; d->sectors = *(u64 *)(id + 100); if (!d->sectors) d->sectors = *(u32 *)(id + 60);
            swapstr(d->model, id + 27, 20); d->read = ahci_rd; d->write = ahci_wr; d->flush = ahci_fl; d->maxsect = 256;
            dma_free(id, 4096); blk_register(d);
        }
    }
}

/* ===== NVMe ===== */
typedef struct { volatile u8 *r; u32 dstrd; u32 *asq, *acq, *iosq, *iocq; u16 at, ah, it, ih; u8 ap, ip; u16 cid; u64 *prp; u32 nsid; } nvme_t;
#define NR(o) R32(n->r + (o))
static int nv_wait(nvme_t *n, u32 *cq, u16 *head, u8 *ph, int qid, int qsz) {
    u64 end = ticks + 15000;
    for (;;) {
        u32 *e = cq + *head * 4;
        if (((R32(&e[3]) >> 16) & 1) == *ph) {
            u32 st = (R32(&e[3]) >> 17) & 0x7FFF;
            if (++*head == qsz) { *head = 0; *ph ^= 1; }
            NR(0x1000 + (2 * qid + 1) * (4 << n->dstrd)) = *head;
            return st ? -1 : 0;
        }
        if (ticks > end) return -1;
        yield();
    }
}
static int nv_admin(nvme_t *n, u32 *c) { memcpy(n->asq + n->at * 16, c, 64); n->at = (n->at + 1) % 32; mb(); NR(0x1000) = n->at; return nv_wait(n, n->acq, &n->ah, &n->ap, 0, 32); }
static int nv_io(blkdev_t *d, u64 lba, u32 cnt, void *b, int wr) {
    nvme_t *n = d->drv; u32 c[16] = {0}; u64 ph = V2P(b), len = (u64)cnt * d->ssize;
    c[0] = (wr ? 1 : 2) | (u32)(++n->cid) << 16; c[1] = n->nsid; *(u64 *)&c[6] = ph;
    u64 off = ph & 4095, pages = (off + len + 4095) / 4096;
    if (pages == 2) *(u64 *)&c[8] = (ph & ~4095UL) + 4096;
    else if (pages > 2) { for (u64 i = 1; i < pages; i++) n->prp[i - 1] = (ph & ~4095UL) + i * 4096; *(u64 *)&c[8] = V2P(n->prp); }
    c[10] = lba; c[11] = lba >> 32; c[12] = cnt - 1;
    memcpy(n->iosq + n->it * 16, c, 64); n->it = (n->it + 1) % 64; mb(); NR(0x1000 + 2 * (4 << n->dstrd)) = n->it;
    return nv_wait(n, n->iocq, &n->ih, &n->ip, 1, 64);
}
static int nv_rd(blkdev_t *d, u64 l, u32 c, void *b) { return nv_io(d, l, c, b, 0); }
static int nv_wr(blkdev_t *d, u64 l, u32 c, const void *b) { return nv_io(d, l, c, (void *)b, 1); }
static int nv_fl(blkdev_t *d) { nvme_t *n = d->drv; u32 c[16] = {0}; c[0] = (u32)(++n->cid) << 16; c[1] = n->nsid; memcpy(n->iosq + n->it * 16, c, 64); n->it = (n->it + 1) % 64; mb(); NR(0x1000 + 2 * (4 << n->dstrd)) = n->it; return nv_wait(n, n->iocq, &n->ih, &n->ip, 1, 64); }
void nvme_init(void) {
    for (int i = 0; i < pci_count; i++) {
        pci_t *p = &pci_devs[i]; if (p->cls != 1 || p->sub != 8) continue;
        pci_enable(p, 1, 0, 1);
        nvme_t *n = kzalloc(sizeof *n); n->r = ioremap(pci_bar(p, 0), 0x4000);
        u64 cap = R64(n->r); n->dstrd = (cap >> 32) & 15; u32 to = ((cap >> 24) & 255) * 500 + 500;
        NR(0x14) = 0; for (u32 t = 0; t < to && (NR(0x1C) & 1); t += 5) msleep(5);
        n->asq = dma_alloc(4096); n->acq = dma_alloc(4096); n->iosq = dma_alloc(4096); n->iocq = dma_alloc(4096); n->prp = dma_alloc(4096);
        n->ap = n->ip = 1;
        NR(0x24) = 31 | 31 << 16; R64(n->r + 0x28) = V2P(n->asq); R64(n->r + 0x30) = V2P(n->acq);
        NR(0x14) = 1 | 6 << 16 | 4 << 20;
        for (u32 t = 0; t < to && !(NR(0x1C) & 1); t += 5) msleep(5);
        if (!(NR(0x1C) & 1)) { kprintf("nvme: no se activa\n"); continue; }
        u8 *id = dma_alloc(4096); u32 c[16];
        memset(c, 0, 64); c[0] = 6 | 1 << 16; *(u64 *)&c[6] = V2P(id); c[10] = 1;
        if (nv_admin(n, c)) { kprintf("nvme: identify fallo\n"); continue; }
        char model[41]; memcpy(model, id + 24, 40); model[40] = 0; for (int k = 39; k >= 0 && model[k] == ' '; k--) model[k] = 0;
        memset(c, 0, 64); c[0] = 5 | 2 << 16; *(u64 *)&c[6] = V2P(n->iocq); c[10] = 1 | 63 << 16; c[11] = 1; if (nv_admin(n, c)) continue;
        memset(c, 0, 64); c[0] = 1 | 3 << 16; *(u64 *)&c[6] = V2P(n->iosq); c[10] = 1 | 63 << 16; c[11] = 1 | 1 << 16; if (nv_admin(n, c)) continue;
        memset(c, 0, 64); c[0] = 6 | 4 << 16; c[1] = 1; *(u64 *)&c[6] = V2P(id); c[10] = 0;
        if (nv_admin(n, c)) continue;
        n->nsid = 1; u64 sz = *(u64 *)id; u8 fl = id[26] & 15; u32 lbads = id[128 + fl * 4 + 2];
        blkdev_t *d = blk_new("nvme"); d->drv = n; d->ssize = 1u << lbads; d->sectors = sz; strncpy0(d->model, model, 41); d->read = nv_rd; d->write = nv_wr; d->flush = nv_fl; d->maxsect = 128 * 1024 / d->ssize;
        dma_free(id, 4096); if (sz) blk_register(d);
    }
}
#endif
