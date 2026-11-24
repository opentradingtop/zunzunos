#include "kernel.h"
#define UCHK(p, n) do { if (!user_ok((void *)(p), (n))) return E_INVAL; } while (0)
#define PATHW(var, uptr, w) char var[256]; if (user_str((const char *)(uptr), var, 256) < 0) return E_INVAL; { int pe_ = sess_path(var, (w)); if (pe_) return pe_; }
#define PATH(var, uptr) PATHW(var, uptr, 0)
/* portapapeles: texto o imagen ("ZIMG" + ancho + alto + pixeles), hasta 32 MB */
static u8 *clip; static u32 clip_n, clip_cap;

typedef struct { u64 mem_total, mem_free, uptime_ms; u32 scr_w, scr_h, nproc, boot_mode; char cpu[49]; char pad[7]; u64 tsc_khz; int layout; u8 hwmac[6], pad2[2]; } zsys_t;
typedef struct { u8 bus, dev, fn, cls, sub, pif; u16 vendor, device; } zpci_t;
typedef struct { char name[16]; char model[41]; u64 sectors; u32 ssize; u32 removable; } zdisk_t;
typedef struct { char name[16]; u64 start, sectors; } zpart_t;

int fd_alloc(file_t *f) { proc_t *p = cur->proc; for (int i = 0; i < MAXFD; i++) if (!p->fds[i]) { p->fds[i] = f; return i; } return E_NOMEM; }
static file_t *getfd(u64 i) { return i < MAXFD ? cur->proc->fds[i] : 0; }
#define FD(v, n) file_t *v = getfd(n); if (!v) return E_BADF

u64 syscall_dispatch(regs_t *r) {
    u64 a = r->rbx, b = r->rcx, c = r->rdx, d = r->rsi, e = r->rdi;
    if (r->rax >= 95 && r->rax <= 101 && sess_guest()) return E_ACCES; /* el invitado no accede a los discos en bruto */
    switch (r->rax) {
    case 0: proc_exit((int)a);
    case 1: yield(); return 0;
    case 2: msleep(a); return 0;
    case 3: return ticks * 1000 / HZ;
    case 4: { UCHK(a, 24); rtc_read((u32 *)a); return 0; }
    case 5: { PATH(p, a); if (!sess_spawn_ok(p)) return E_ACCES; char ar[1024] = ""; if (b && user_str((char *)b, ar, 1024) < 0) return E_INVAL; return proc_spawn(p, ar); }
    case 6: if (a == 1 && cur->proc->pid != 1) return E_ACCES; return proc_kill(a);
    case 7: { UCHK(a, b * sizeof(zproc_t)); return proc_list((zproc_t *)a, b); }
    case 8: { UCHK(a, sizeof(zsys_t)); zsys_t *s = (zsys_t *)a; zproc_t t[64];
        { extern u8 net_first_mac[6]; memcpy(s->hwmac, net_first_mac, 6); } s->mem_total = pmm_total_pages() * 4096; s->mem_free = pmm_free_pages() * 4096; s->uptime_ms = ticks * 1000 / HZ; s->scr_w = scr_w; s->scr_h = scr_h;
        s->nproc = proc_list(t, 64); s->boot_mode = bi.boot_mode; memcpy(s->cpu, cpu_brand, 49); s->tsc_khz = tsc_khz; s->layout = key_layout; return 0; }
    case 9: { proc_t *p = cur->proc; u64 o = p->brk; i64 inc = a; if (inc < 0 || o + inc > 0x1000000000UL) return 0; p->brk = o + ALIGN(inc, 16); return o; }
    case 10: UCHK(b - 8, 8); return thread_create(a, b, c);
    case 11: UCHK(a, 1024); memcpy((void *)a, cur->proc->args, 1024); return 0;
    case 12: { char m[256]; if (user_str((char *)a, m, 256) < 0) return E_INVAL; kprintf("[%s] %s\n", cur->proc->name, m); return 0; }
    case 13: { UCHK(a, b); return dmesg_read((char *)a, b); }
    case 14: key_layout = a ? 1 : 0; return 0;
    case 15: { UCHK(a, b); u32 n = MIN(b, 32u << 20); if (n > clip_cap) { u8 *nb = kmalloc(n); if (!nb) return E_NOMEM; kfree(clip); clip = nb; clip_cap = n; } memcpy(clip, (void *)a, n); clip_n = n; return n; }
    case 16: { if (!a) return clip_n; UCHK(a, b); u32 n = MIN(b, clip_n); if (n) memcpy((void *)a, clip, n); return n; } /* a = 0: tamano */

    case 20: { UCHK(a, sizeof(zwincfg_t)); zwincfg_t cf = *(zwincfg_t *)a; char t[64] = ""; if (cf.title && user_str((char *)cf.title, t, 64) < 0) return E_INVAL; return wm_create(&cf, t); }
    case 21: return wm_destroy(a);
    case 22: return wm_present(a, b, c, d, e);
    case 23: return wm_op(a, b, c, d);
    case 24: UCHK(b, sizeof(zwininfo_t)); return wm_info(a, (zwininfo_t *)b);
    case 25: UCHK(a, b * sizeof(zwininfo_t)); return wm_list((zwininfo_t *)a, b);
    case 26: UCHK(a, sizeof(zevent_t)); return ev_pop((zevent_t *)a, (i64)b);
    case 27: return wm_resize(a, b, c);
    case 28: { char t[64]; if (user_str((char *)b, t, 64) < 0) return E_INVAL; return wm_set_title(a, t); }

    case 40: { PATHW(p, a, b & (O_WRITE | O_CREATE | O_TRUNC | O_APPEND)); file_t *f; int x = vfs_open(p, b, &f); if (x) return x; x = fd_alloc(f); if (x < 0) vfs_close(f); return x; }
    case 41: { FD(f, a); cur->proc->fds[a] = 0; vfs_close(f); return 0; }
    case 42: { FD(f, a); UCHK(b, c); return vfs_read(f, (void *)b, c); }
    case 43: { FD(f, a); UCHK(b, c); return vfs_write(f, (void *)b, c); }
    case 44: { FD(f, a); return vfs_seek(f, b, c); }
    case 45: { PATH(p, a); UCHK(b, sizeof(zdirent_t)); return vfs_stat(p, (zdirent_t *)b); }
    case 46: { PATH(p, a); UCHK(c, sizeof(zdirent_t)); return vfs_readdir(p, b, (zdirent_t *)c); }
    case 47: { PATHW(p, a, 1); return vfs_mkdir(p); }
    case 48: { PATHW(p, a, 1); return vfs_unlink(p); }
    case 49: { PATHW(p, a, 1); PATHW(q, b, 1); return vfs_rename(p, q); }
    case 50: UCHK(a, b * sizeof(zmount_t)); return vfs_mounts((zmount_t *)a, b);
    case 51: vfs_sync(); blk_sync(); return 0;

    case 60: { file_t *f = sock_file(a); if (!f) return E_NOMEM; int x = fd_alloc(f); if (x < 0) sock_close(f); return x; }
    case 61: { FD(f, a); return sock_connect(f, b, c, d); }
    case 62: { FD(f, a); UCHK(b, c); return sock_send(f, (void *)b, c); }
    case 63: { FD(f, a); UCHK(b, c); return sock_recv(f, (void *)b, c, (int)d); }
    case 64: { PATH(p, a); UCHK(b, 4); return net_dns(p, (u32 *)b); }
    case 65: return net_ping(a, b, c);
    case 66: UCHK(a, sizeof(znet_t)); return net_info((znet_t *)a);
    case 67: { FD(f, a); return sock_bind(f, b); }
    case 68: { FD(f, a); file_t *n; int x = sock_accept(f, (int)b, &n); if (x) return x; x = fd_alloc(n); if (x < 0) sock_close(n); return x; }
    case 69: { FD(f, a); UCHK(d, e); return sock_sendto(f, b, c, (void *)d, e); }
    case 70: { FD(f, a); UCHK(b, c); UCHK(e, 8); i64 r = sock_recv(f, (void *)b, c, (int)d); if (r >= 0) { u32 ip; u16 pt; sock_from(f, &ip, &pt); ((u32 *)e)[0] = ip; ((u32 *)e)[1] = pt; } return r; }

    case 80: UCHK(a, b); return snd_write((void *)a, b);
    case 81: UCHK(a, b); return snd_info((char *)a, b);
    case 82: return snd_volume((int)a);
    case 83: return snd_space();
    case 84: return rec_start();
    case 85: UCHK(a, b); return rec_read((void *)a, b);
    case 86: rec_stop(); return 0;
    case 87: rec_gain(a); return 0;

    case 93: return backlight_set(a);
    case 94: return backlight_get();
    case 95: case 96: { char n[16]; if (user_str((char *)a, n, 16) < 0) return E_INVAL; blkdev_t *k = blk_find(n); if (!k) return E_NOENT; if (c > 2048 || b + c > k->sectors) return E_INVAL; UCHK(d, c * k->ssize); return r->rax == 95 ? blk_read(k, b, c, (void *)d) : blk_write(k, b, c, (void *)d); }
    case 97: { char n[16]; if (user_str((char *)a, n, 16) < 0) return E_INVAL; blkdev_t *k = blk_find(n); if (!k) return E_NOENT; vfs_sync(); blk_sync(); return blk_rescan(k); }
    case 98: { char n[16]; if (user_str((char *)a, n, 16) < 0) return E_INVAL; UCHK(b, c * sizeof(zpart_t)); blkdev_t *k = blk_find(n); if (!k) return E_NOENT; zpart_t *o = (zpart_t *)b; int m = 0; for (int i = 0; i < blk_count && m < (int)c; i++) { blkdev_t *p = blkdevs[i]; if (p && p->parent == k) { memcpy(o[m].name, p->name, 16); o[m].start = p->offset; o[m].sectors = p->sectors; m++; } } return m; }
    case 99: { char n[16]; if (user_str((char *)a, n, 16) < 0) return E_INVAL; return vfs_set_ro(n, b); }
    case 100: { char n[16]; if (user_str((char *)a, n, 16) < 0) return E_INVAL; blkdev_t *k = blk_find(n); return k ? blk_add_part(k, b, c) : E_NOENT; }
    case 101: { PATHW(p, a, 1); UCHK(c, 8); return vfs_contig(p, b, (u64 *)c); }
    case 102: UCHK(a, sizeof(ztheme_t)); wm_theme_get((ztheme_t *)a); return 0;
    case 103: UCHK(a, sizeof(ztheme_t)); wm_theme_set((ztheme_t *)a); return 0;
    case 104: UCHK(a, 24); rtc_write((u32 *)a); return 0;
    case 105: input_joy(a, (int)b, (int)c, 0x5A5A0001); return 0;
    case 107: { UCHK(a, b * 256); extern int usbprn_list(void *, int); return usbprn_list((void *)a, b); }
    case 110: { extern volatile u64 last_input; return (ticks - last_input) * 1000 / HZ; }
    case 112: { UCHK(a, b * 4); extern int wm_capture(u32 *, u64); return wm_capture((u32 *)a, b); }
    case 111: { extern int screen_power(int); return screen_power(a); }
    case 114: { if (a == 0) { char n[24]; if (user_str((char *)b, n, 24) < 0) return E_INVAL; return guard_syscall(0, (u64)n, c); }
        if (a == 1) { UCHK(b, c * sizeof(zrule_t)); } else if (a == 2) { UCHK(b, c * sizeof(gl_t)); } else if (a == 3) { UCHK(b, sizeof(zask_t)); }
        if ((a == 0 || a == 4 || a == 5) && cur->proc && cur->proc->pid != 1 && !guard_admin()) return E_ACCES;   /* cambiar reglas: escritorio o Configuracion */
        return guard_syscall(a, b, c); }
    case 115: { char pw[200] = ""; if (b && user_str((char *)b, pw, 200) < 0) return E_INVAL; long r = crypt_syscall((int)a, pw); memset(pw, 0, sizeof pw); return r; }
    case 113: { int o[5]; int r = acpi_battery(o); if (r) return r; UCHK(a, sizeof o); memcpy((void *)a, o, sizeof o); return 0; }
    case 109: { char pb[8] = "", pc[8] = ""; if (b && user_str((char *)b, pb, 8) < 0) return E_INVAL; if (c && user_str((char *)c, pc, 8) < 0) return E_INVAL; return sess_syscall(a, b ? pb : 0, c ? pc : 0); }
    case 108: { UCHK(b, c); extern long usbprn_write(int, const void *, long); return usbprn_write(a, (const void *)b, c); }
    case 106: { char n[16]; if (user_str((char *)a, n, 16) < 0) return E_INVAL; return blk_eject(n); }
    case 90: if (a == 1) acpi_reboot(); else acpi_poweroff(); return 0;
    case 91: { UCHK(a, b * sizeof(zpci_t)); zpci_t *o = (zpci_t *)a; int n = MIN((int)b, pci_count);
        for (int i = 0; i < n; i++) { pci_t *p = &pci_devs[i]; o[i] = (zpci_t){p->bus, p->dev, p->fn, p->cls, p->sub, p->pif, p->vendor, p->device}; } return n; }
    case 92: { UCHK(a, b * sizeof(zdisk_t)); zdisk_t *o = (zdisk_t *)a; int n = 0;
        for (int i = 0; i < blk_count && n < (int)b; i++) { blkdev_t *d2 = blkdevs[i]; if (!d2 || d2->parent) continue; memcpy(o[n].name, d2->name, 16); memcpy(o[n].model, d2->model, 41); o[n].sectors = d2->sectors; o[n].ssize = d2->ssize; o[n].removable = d2->removable; n++; } return n; }
    }
    return E_NOSYS;
}
