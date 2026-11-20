/* Red por USB (RNDIS y CDC-ECM): lo que cada controlador debe aportar */
#pragma once
typedef struct {
    void *dev; volatile int *gone; int kind; /* 1 RNDIS, 2 CDC-ECM */ int ctl_if, data_if, data_alt, mac_idx;
    int (*ctrl)(void *dev, u8 rt, u8 rq, u16 val, u16 idx, u16 len, void *data);
    int (*out)(void *dev, const void *p, u32 n);
    int (*in_start)(void *dev, void *buf, u32 n);
    int (*in_poll)(void *dev); /* -1 en curso, >= 0 bytes recibidos, < -1 error */
} usbnet_t;
void usbnet_attach(const usbnet_t *u);
/* interfaz de red del iPhone (protocolo de Apple, "ipheth" en Linux) */
static inline int usbnet_is_ipheth(int cls, int sub, int prot) { return cls == 0xFF && sub == 0xFD && prot == 1; }
/* elige la configuracion USB: la 0 si tiene algo conocido; si no, la primera con red (iPhone, adaptadores RTL8153...) */
static inline int usbnet_pick_cfg(const u8 *c, int tl, int ci, int *known) { int net = 0; *known = 0; for (int i = 0; i + 2 <= tl && c[i]; i += c[i]) if (c[i + 1] == 4) { int cl = c[i + 5], su = c[i + 6], pr = c[i + 7]; if (cl == 3 || cl == 8 || cl == 7 || cl == 9) *known = 1; if ((cl == 2 && (su == 2 || su == 6)) || cl == 0xE0 || cl == 0xEF || usbnet_is_ipheth(cl, su, pr)) net = 1; } (void)ci; return net; }
/* deteccion comun en los descriptores de configuracion */
static inline int usbnet_ctl_kind(int cls, int sub, int prot) {
    if ((cls == 2 && sub == 2 && prot == 0xFF) || (cls == 0xE0 && sub == 1 && prot == 3) || (cls == 0xEF && sub == 4 && prot == 1)) return 1;
    if (cls == 2 && sub == 6) return 2;
    return 0;
}
