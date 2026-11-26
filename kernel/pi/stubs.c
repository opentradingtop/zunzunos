/* ZunzunOS - lo que la Raspberry Pi 2 no tiene (todavia): PCI, ACPI, tarjeta de sonido y red por PCI.
   La red Ethernet de la Pi 2 (LAN9514) y el teclado/raton USB van por el controlador DWC OTG: pendiente. */
#include "../kernel.h"
pci_t pci_devs[MAXPCI]; int pci_count;
void pci_init(void) {}
u32 pci_read(const pci_t *p, int o) { return ~0u; } void pci_write(const pci_t *p, int o, u32 v) {}
u16 pci_read16(const pci_t *p, int o) { return 0xFFFF; } void pci_write16(const pci_t *p, int o, u16 v) {}
void pci_enable(const pci_t *p, int m, int io, int bm) {} u64 pci_bar(const pci_t *p, int i) { return 0; }
void acpi_init(void) {}
int acpi_battery(int *o) { o[0] = 0; return 0; }
void netdrv_init(void) {}
void xhci_init(void) {} void ehci_init(void) {}
/* sonido: pendiente (la salida analogica de la Pi es PWM en GPIO40/45) */
void snd_init(void) {}
i64 snd_write(const void *b, u64 n) { return E_NOSYS; }
int snd_info(char *o, int n) { strncpy0(o, "sin tarjeta de sonido", n); return 0; }
int snd_volume(int v) { return 0; }
int snd_space(void) { return 0; }
int rec_start(void) { return E_NOSYS; } void rec_stop(void) {} i64 rec_read(void *b, u64 n) { return 0; } void rec_gain(int g) {}
