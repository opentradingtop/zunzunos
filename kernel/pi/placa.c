/* ZunzunOS - placa Raspberry Pi 2 (BCM2836): UART, GPIO, buzon del firmware, framebuffer y arranque */
#include "../kernel.h"
#include "../../user/lib/version.h"

bootinfo_t bi; int fb_in_ram;

/* --- UART PL011 (GPIO14 TX, GPIO15 RX; 115200 baudios con el reloj de 48 MHz de config.txt) --- */
#define UART(o) R32(PERI(0x201000 + (o)))
int uart_init(void) {
    UART(0x30) = 0; gpio_func(14, 4); gpio_func(15, 4); gpio_pull(14, 0); gpio_pull(15, 2);
    UART(0x44) = 0x7FF; UART(0x24) = 26; UART(0x28) = 3; UART(0x2C) = 3 << 5 | 1 << 4; UART(0x38) = 0;
    UART(0x30) = 0x301;   /* UARTEN | TXE | RXE (QEMU no transmite sin TXE) */
    return 1;
}
void uart_putc(char c) { for (int t = 0; t < 100000 && (UART(0x18) & 0x20); t++) continue; UART(0) = (u8)c; }
int uart_getc(void) { if (UART(0x18) & 0x10) return -1; return UART(0) & 0xFF; }

/* --- GPIO --- */
#define GPIO(o) R32(PERI(0x200000 + (o)))
void gpio_func(int pin, int fn) { u32 r = 0x00 + (pin / 10) * 4, sh = (pin % 10) * 3; u32 v = GPIO(r); v &= ~(7u << sh); v |= (u32)fn << sh; GPIO(r) = v; }
void gpio_set(int pin, int on) { GPIO((on ? 0x1C : 0x28) + (pin / 32) * 4) = 1u << (pin % 32); }
int gpio_get(int pin) { return (GPIO(0x34 + (pin / 32) * 4) >> (pin % 32)) & 1; }
static void spin(int n) { for (volatile int i = 0; i < n; i++); }
void gpio_pull(int pin, int mode) {   /* 0 sin resistencia, 1 a masa, 2 a 3,3 V */
    GPIO(0x94) = mode; spin(300); GPIO(0x98 + (pin / 32) * 4) = 1u << (pin % 32); spin(300); GPIO(0x94) = 0; GPIO(0x98 + (pin / 32) * 4) = 0;
}

/* --- buzon del firmware (canal 8: propiedades) --- */
#define MBOX(o) R32(PERI(0xB880 + (o)))
int mbox_call(u32 *buf) {
    u32 bus = (u32)V2P(buf) | 0xC0000000u;
    dma_sync(buf, buf[0]);
    for (int t = 0; MBOX(0x38) & 0x80000000u; t++) if (t > 1000000) return 0;
    MBOX(0x20) = bus | 8;
    for (int t = 0; t < 4000000; t++) {
        if (MBOX(0x18) & 0x40000000u) continue;
        u32 r = MBOX(0x00);
        if ((r & 0xF) == 8 && (r & ~0xFu) == bus) { dma_sync(buf, buf[0]); return buf[1] == 0x80000000u; }
    }
    return 0;
}
static u32 mbx[64] __attribute__((aligned(16)));
static u32 board_rev;
static void query_board(void) {
    u32 i = 0; mbx[i++] = 0; mbx[i++] = 0;
    mbx[i++] = 0x10005; mbx[i++] = 8; mbx[i++] = 0; mbx[i++] = 0; mbx[i++] = 0;   /* memoria del ARM */
    mbx[i++] = 0x10002; mbx[i++] = 4; mbx[i++] = 0; mbx[i++] = 0;                 /* revision de la placa */
    mbx[i++] = 0x10004; mbx[i++] = 8; mbx[i++] = 0; mbx[i++] = 0; mbx[i++] = 0;   /* numero de serie */
    mbx[i++] = 0; mbx[0] = i * 4;
    if (mbox_call(mbx)) {
        arm_mem_size = mbx[6]; board_rev = mbx[10];
        /* la Pi 2 no tiene red propia (la Ethernet va por USB): el numero de serie hace de identidad del equipo
           (Red local, archivos por equipo), con forma de direccion MAC administrada localmente */
        extern u8 net_first_mac[6]; u32 lo = mbx[14], hi = mbx[15];
        u8 m[6] = {0x02, 0x52, (u8)(hi ^ (lo >> 24)), (u8)(lo >> 16), (u8)(lo >> 8), (u8)lo}; memcpy(net_first_mac, m, 6);
    }
}
/* framebuffer del firmware a 480x320 y 32 bits; en QEMU es la pantalla, en la Pi la salida HDMI */
static int fb_setup(void) {
    u32 i = 0; mbx[i++] = 0; mbx[i++] = 0;
    mbx[i++] = 0x48003; mbx[i++] = 8; mbx[i++] = 0; mbx[i++] = 480; mbx[i++] = 320;   /* fisico */
    mbx[i++] = 0x48004; mbx[i++] = 8; mbx[i++] = 0; mbx[i++] = 480; mbx[i++] = 320;   /* virtual */
    mbx[i++] = 0x48009; mbx[i++] = 8; mbx[i++] = 0; mbx[i++] = 0; mbx[i++] = 0;       /* desplazamiento */
    mbx[i++] = 0x48005; mbx[i++] = 4; mbx[i++] = 0; mbx[i++] = 32;                   /* bits por pixel */
    mbx[i++] = 0x48006; mbx[i++] = 4; mbx[i++] = 0; mbx[i++] = 0;                    /* orden BGR: 0x00RRGGBB */
    u32 alloc = i; mbx[i++] = 0x40001; mbx[i++] = 8; mbx[i++] = 0; mbx[i++] = 4096; mbx[i++] = 0;
    u32 pit = i; mbx[i++] = 0x40008; mbx[i++] = 4; mbx[i++] = 0; mbx[i++] = 0;
    mbx[i++] = 0; mbx[0] = i * 4;
    if (!mbox_call(mbx) || !mbx[alloc + 3] || mbx[5] != 480 || mbx[6] != 320 || mbx[20] != 32) return 0;
    bi.fb_addr = mbx[alloc + 3] & 0x3FFFFFFF; bi.fb_w = 480; bi.fb_h = 320; bi.fb_bpp = 32; bi.fb_pitch = mbx[pit + 3] ? mbx[pit + 3] : 480 * 4;
    bi.fb_rgb = mbx[24] == 1;
    return 1;
}

/* --- reinicio y apagado con el watchdog (PM). "Apagar" deja el firmware parado en la particion 63 --- */
#define PM(o) R32(PERI(0x100000 + (o)))
#define PM_PASS 0x5A000000u
static void pm_reset(void) { PM(0x24) = PM_PASS | 10; PM(0x1C) = PM_PASS | (PM(0x1C) & ~0x30u) | 0x20; for (;;) cpu_halt(); }
void acpi_reboot(void) { kprintf("reiniciando\n"); vfs_sync(); blk_sync(); irq_off(); pm_reset(); }
void acpi_poweroff(void) {
    kprintf("apagando: ya se puede desenchufar la Raspberry Pi\n"); vfs_sync(); blk_sync(); irq_off();
    extern void lcd_off(void); lcd_off();
    u32 r = PM(0x20); r &= ~0xFFFFFAAAu; r |= 0x555; PM(0x20) = PM_PASS | r; pm_reset();
}

/* --- ordenes por el puerto serie: las usan las pruebas de QEMU (y sirven con un cable USB-TTL)
     "!T x y" toque en (x, y), "!U x y" levantar el dedo, "!M x y" mover, "!K uso 1|0" tecla HID,
     "!R x y 1|0" lectura cruda del tactil (pasa por la calibracion), "!C" calibrar el tactil --- */
static int num(const char **p) { while (**p == ' ') (*p)++; int n = 0, neg = **p == '-'; if (neg) (*p)++; while (**p >= '0' && **p <= '9') n = n * 10 + *(*p)++ - '0'; return neg ? -n : n; }
static void serial_cmd(const char *l) {
    if (l[0] != '!') return;
    char c = l[1]; const char *p = l + 2; int a = num(&p), b = num(&p);
    if (c == 'T' || c == 'U' || c == 'M') input_abs(a, b, scr_w - 1, scr_h - 1, c == 'T', 0);
    else if (c == 'K') input_key((u8)a, b);
    else if (c == 'R') { int pr = num(&p); extern void touch_raw(int, int, int); touch_raw(a, b, pr); }   /* lectura cruda del tactil */
    else if (c == 'C') { extern void touch_calibrate(void); touch_calibrate(); }
}
static void serial_th(void *x) {
    char l[64]; int n = 0;
    for (;;) {
        int c;
        while ((c = uart_getc()) >= 0) { if (c == '\r' || c == '\n') { l[n] = 0; if (n) serial_cmd(l); n = 0; } else if (n < 63) l[n++] = c; }
        msleep(10);
    }
}

/* la hora se guarda al cambiarla y al apagar; al arrancar se lee (la Pi no tiene reloj con pila) */
extern i64 clock_now(void); extern void clock_set(i64);
void clock_save(void) { file_t *f; char b[32]; int n = snprintf(b, sizeof b, "%lld\n", clock_now()); if (!vfs_open("/sys/ZUNZUN/RELOJ.TXT", O_WRITE | O_CREATE | O_TRUNC, &f)) { vfs_write(f, b, n); vfs_close(f); } }
static void clock_load(void) {
    u64 n; char *s = vfs_slurp("/sys/ZUNZUN/RELOJ.TXT", &n); if (!s) return;
    i64 t = 0; for (u64 i = 0; i < n && s[i] >= '0' && s[i] <= '9'; i++) t = t * 10 + s[i] - '0';
    kfree(s); if (t > 1700000000LL && t > clock_now()) { clock_set(t); kprintf("reloj: hora guardada en /sys\n"); }
}
extern void lcd_init(void); extern void touch_init(void);
static void sys_wait(void *a) {   /* cuando aparece /sys (la tarjeta SD) se lee la hora guardada; DIAGNOST.TXT lo escribe el escritorio */
    for (int i = 0; i < 100; i++) { zdirent_t d; if (!vfs_stat("/sys", &d)) { clock_load(); return; } msleep(100); }
}

void pi_main(void) {
    serial_init();
    extern char __kend[];
    u32 kend = (u32)__kend - DMAP; u32 *ih = P2V(kend);
    bi.magic = 0x31544F424F4E555AUL; bi.boot_mode = 3;
    if (!memcmp(ih, "ZZINITRD", 8)) { bi.initrd_phys = kend + 16; bi.initrd_size = ih[2]; }
    kprintf("\nZunzunOS %s arrancando (Raspberry Pi)\n", ZZ_VERSION);
    R32(PERI(0xB21C)) = 0xFFFFFFFF; R32(PERI(0xB220)) = 0xFFFFFFFF; R32(PERI(0xB224)) = 0xFFFFFFFF;   /* sin interrupciones de la GPU */
    query_board();
    kprintf("placa: revision %x, %u MB para el ARM, initrd %u KB\n", board_rev, arm_mem_size >> 20, (u32)(bi.initrd_size >> 10));
    cpu_init(); mm_init(); timer_init(); sched_init();
    irq_on();
    kprintf("cpu: %s\n", cpu_brand);
    if (!fb_setup()) {   /* sin framebuffer del firmware: uno en RAM, que solo ve la pantalla SPI */
        void *p = kzalloc(480 * 320 * 4); fb_in_ram = 1;
        bi.fb_addr = V2P(p); bi.fb_w = 480; bi.fb_h = 320; bi.fb_bpp = 32; bi.fb_pitch = 480 * 4; bi.fb_rgb = 0;
        kprintf("pantalla: el firmware no da framebuffer; se usa uno en memoria\n");
    } else kprintf("pantalla: framebuffer del firmware en %p\n", (void *)(u32)bi.fb_addr);
    vfs_init(); input_init(); wm_init(); backlight_init();
    osk_on = 1; lcd_init(); touch_init();
    sdhci_init(); netdrv_init(); net_init(); snd_init();
    kthread("serie", serial_th, 0);
    kthread("sys", sys_wait, 0);
    if (wm_ready()) {
        int r = proc_spawn("/rd/bin/desktop", "");
        if (r < 0) kprintf("no se pudo iniciar el escritorio: %d\n", r);
    }
    kprintf("[zz] sistema listo\n");
    for (;;) msleep(1000);
}
