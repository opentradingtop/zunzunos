/* Impresoras USB (clase 7): lista con su IEEE 1284 Device ID y envio de datos crudos */
#include "kernel.h"
typedef struct { char id[16], devid[240]; } zusbprn_t;
typedef struct { zusbprn_t info; int (*wr)(void *, const void *, u32); void *ctx; volatile int *gone; int used; } uprn_t;
static uprn_t P[8]; static mutex_t plock; static u8 *kbuf;
void usbprn_add(u16 vid, u16 pid, const u8 *rawid, int (*wr)(void *, const void *, u32), void *ctx, volatile int *gone) {
    mutex_lock(&plock); int k = -1; for (int i = 0; i < 8; i++) if (!P[i].used || *P[i].gone) { k = i; break; }
    if (k >= 0) { uprn_t *p = &P[k]; memset(p, 0, sizeof *p); snprintf(p->info.id, 16, "%04x:%04x", vid, pid); int l = rawid ? (rawid[0] << 8 | rawid[1]) - 2 : 0; if (l > 0) { l = MIN(l, 239); memcpy(p->info.devid, rawid + 2, l); p->info.devid[l] = 0; } p->wr = wr; p->ctx = ctx; p->gone = gone; p->used = 1; kprintf("usb: impresora %s %s\n", p->info.id, p->info.devid); }
    mutex_unlock(&plock);
}
int usbprn_list(zusbprn_t *o, int max) { int n = 0; mutex_lock(&plock); for (int i = 0; i < 8 && n < max; i++) if (P[i].used && !*P[i].gone) o[n++] = P[i].info; mutex_unlock(&plock); return n; }
long usbprn_write(int idx, const void *data, long len) {
    mutex_lock(&plock); uprn_t *p = 0; int n = 0; for (int i = 0; i < 8; i++) if (P[i].used && !*P[i].gone && n++ == idx) p = &P[i];
    if (!p) { mutex_unlock(&plock); return E_NOENT; } if (!kbuf) kbuf = kmalloc(16384);
    long done = 0; while (done < len) { u32 c = MIN(len - done, 16384L); memcpy(kbuf, (const u8 *)data + done, c); int r = p->wr(p->ctx, kbuf, c); if (r < 0) { mutex_unlock(&plock); return done ? done : r; } done += c; }
    mutex_unlock(&plock); return done;
}
