#include "kernel.h"
pci_t pci_devs[MAXPCI]; int pci_count;
static u32 cfg(int b, int d, int f, int r) { outl(0xCF8, 0x80000000u | b << 16 | d << 11 | f << 8 | (r & 0xFC)); return inl(0xCFC); }
u32 pci_read(const pci_t *p, int r) { return cfg(p->bus, p->dev, p->fn, r); }
void pci_write(const pci_t *p, int r, u32 v) { outl(0xCF8, 0x80000000u | p->bus << 16 | p->dev << 11 | p->fn << 8 | (r & 0xFC)); outl(0xCFC, v); }
u16 pci_read16(const pci_t *p, int r) { return pci_read(p, r) >> ((r & 2) * 8); }
void pci_write16(const pci_t *p, int r, u16 v) { u32 x = pci_read(p, r); int s = (r & 2) * 8; x = (x & ~(0xFFFFu << s)) | (u32)v << s; pci_write(p, r, x); }
void pci_enable(const pci_t *p, int mem, int io, int master) {
    u32 c = pci_read(p, 4) & 0xFFFF; c |= (mem ? 2 : 0) | (io ? 1 : 0) | (master ? 4 : 0); c &= ~0x400u; pci_write(p, 4, c);
}
u64 pci_bar(const pci_t *p, int i) {
    u32 v = p->bar[i];
    if (v & 1) return v & ~3u;
    u64 a = v & ~15u;
    if ((v & 6) == 4) a |= (u64)p->bar[i + 1] << 32;
    return a;
}
static void scan_bus(int b, int depth);
static void add(int b, int d, int f) {
    u32 id = cfg(b, d, f, 0); u32 cl = cfg(b, d, f, 8); u32 h = (cfg(b, d, f, 12) >> 16) & 0x7F;
    if (pci_count < MAXPCI) {
        pci_t *p = &pci_devs[pci_count++];
        p->bus = b; p->dev = d; p->fn = f; p->vendor = id; p->device = id >> 16;
        p->cls = cl >> 24; p->sub = cl >> 16; p->pif = cl >> 8; p->irq = cfg(b, d, f, 0x3C);
        if (h == 0) for (int i = 0; i < 6; i++) p->bar[i] = cfg(b, d, f, 0x10 + i * 4);
    }
    if (h == 1) { int sec = (cfg(b, d, f, 0x18) >> 8) & 0xFF; if (sec) scan_bus(sec, 1); }
}
static void scan_bus(int b, int depth) {
    for (int d = 0; d < 32; d++) {
        if ((cfg(b, d, 0, 0) & 0xFFFF) == 0xFFFF) continue;
        add(b, d, 0);
        if (cfg(b, d, 0, 12) & 0x800000)
            for (int f = 1; f < 8; f++) if ((cfg(b, d, f, 0) & 0xFFFF) != 0xFFFF) add(b, d, f);
    }
}
void pci_init(void) {
    scan_bus(0, 0);
    for (int i = 0; i < pci_count; i++) { pci_t *p = &pci_devs[i]; kprintf("pci %02x:%02x.%d %04x:%04x clase %02x.%02x.%02x\n", p->bus, p->dev, p->fn, p->vendor, p->device, p->cls, p->sub, p->pif); }
}
