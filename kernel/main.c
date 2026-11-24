#include "kernel.h"
#include "../user/lib/version.h"
bootinfo_t bi;

void rtc_read(u32 *o) {
    static const u8 regs[6] = {0, 2, 4, 7, 8, 9}; u8 v[6], w[6];
    #define CM(r) (outb(0x70, r), inb(0x71))
    for (int t = 0; t < 100; t++) {
        while (CM(10) & 0x80);
        for (int i = 0; i < 6; i++) v[i] = CM(regs[i]);
        for (int i = 0; i < 6; i++) w[i] = CM(regs[i]);
        if (!memcmp(v, w, 6)) break;
    }
    u8 b = CM(11);
    for (int i = 0; i < 6; i++) {
        u8 x = v[i]; int pm = i == 2 && (x & 0x80); if (i == 2) x &= 0x7F;
        if (!(b & 4)) x = (x >> 4) * 10 + (x & 15);
        if (i == 2 && !(b & 2)) x = (x % 12) + (pm ? 12 : 0);
        o[i] = x;
    }
    o[5] += 2000;
}

void rtc_write(const u32 *t) {
    #define CW_(r, v) (outb(0x70, r), outb(0x71, v))
    u8 b = (outb(0x70, 11), inb(0x71)); u32 v[6] = {t[0], t[1], t[2], t[3], t[4], t[5] % 100}; static const u8 regs[6] = {0, 2, 4, 7, 8, 9};
    u64 f = irq_save(); CW_(11, b | 0x80);
    for (int i = 0; i < 6; i++) { u8 x = v[i]; if (!(b & 4)) x = (x / 10) << 4 | (x % 10); CW_(regs[i], x); }
    CW_(11, b & 0x7F); irq_restore(f);
}
void kmain(bootinfo_t *p) {
    serial_init();
    memcpy(&bi, p, sizeof bi);
    kprintf("\nZunzunOS %s arrancando (%s)\n", ZZ_VERSION, bi.boot_mode == 2 ? "UEFI" : "BIOS");
    if (bi.magic != 0x31544F424F4E555AUL) panic("bootinfo no valido");
    cpu_init(); mm_init(); acpi_init(); timer_init(); sched_init();
    __asm__("sti");
    kprintf("cpu: %s\n", cpu_brand);
    vfs_init(); pci_init(); input_init(); wm_init(); backlight_init();
    ata_init(); ahci_init(); nvme_init(); sdhci_init(); xhci_init(); ehci_init();
    netdrv_init(); net_init(); snd_init();
    if (wm_ready()) {
        int r = proc_spawn("/rd/bin/desktop", "");
        if (r < 0) kprintf("no se pudo iniciar el escritorio: %d\n", r);
    }
    kprintf("[zz] sistema listo\n");
    for (;;) msleep(1000);
}
