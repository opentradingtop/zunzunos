/* ZunzunOS - cabecera comun del kernel */
#pragma once
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
typedef unsigned long u64; typedef signed char i8; typedef short i16; typedef int i32; typedef long i64;
typedef unsigned long size_t;
typedef __builtin_va_list va_list;
#define va_start __builtin_va_start
#define va_end __builtin_va_end
#define va_arg __builtin_va_arg
#define NULL ((void *)0)
#define PACKED __attribute__((packed))
#define ALIGN(x, a) (((x) + (a) - 1) & ~((u64)(a) - 1))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define HZ 1000

/* --- puertos y CPU --- */
static inline void outb(u16 p, u8 v) { __asm__ volatile("outb %0,%1" : : "a"(v), "Nd"(p)); }
static inline void outw(u16 p, u16 v) { __asm__ volatile("outw %0,%1" : : "a"(v), "Nd"(p)); }
static inline void outl(u16 p, u32 v) { __asm__ volatile("outl %0,%1" : : "a"(v), "Nd"(p)); }
static inline u8 inb(u16 p) { u8 v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline u16 inw(u16 p) { u16 v; __asm__ volatile("inw %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline u32 inl(u16 p) { u32 v; __asm__ volatile("inl %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline u64 rdmsr(u32 m) { u32 a, d; __asm__ volatile("rdmsr" : "=a"(a), "=d"(d) : "c"(m)); return ((u64)d << 32) | a; }
static inline void wrmsr(u32 m, u64 v) { __asm__ volatile("wrmsr" : : "c"(m), "a"((u32)v), "d"((u32)(v >> 32))); }
static inline u64 rdtsc(void) { u32 a, d; __asm__ volatile("rdtsc" : "=a"(a), "=d"(d)); return ((u64)d << 32) | a; }
static inline u64 read_cr2(void) { u64 v; __asm__ volatile("mov %%cr2,%0" : "=r"(v)); return v; }
static inline u64 read_cr3(void) { u64 v; __asm__ volatile("mov %%cr3,%0" : "=r"(v)); return v; }
static inline void write_cr3(u64 v) { __asm__ volatile("mov %0,%%cr3" : : "r"(v)); }
static inline void invlpg(u64 a) { __asm__ volatile("invlpg (%0)" : : "r"(a)); }
static inline u64 irq_save(void) { u64 f; __asm__ volatile("pushfq; pop %0; cli" : "=r"(f)); return f; }
static inline void irq_restore(u64 f) { if (f & 0x200) __asm__ volatile("sti"); }
static inline void dma_sync(const void *p, u64 n) { u64 a = (u64)p & ~63UL, e = (u64)p + n; __asm__ volatile("mfence"); for (; a < e; a += 64) __asm__ volatile("clflush (%0)" : : "r"(a)); __asm__ volatile("mfence"); }
static inline void mb(void) { __asm__ volatile("mfence" : : : "memory"); }
#define R8(a) (*(volatile u8 *)(a))
#define R16(a) (*(volatile u16 *)(a))
#define R32(a) (*(volatile u32 *)(a))
#define R64(a) (*(volatile u64 *)(a))

/* --- arranque --- */
typedef struct PACKED {
    u64 magic, fb_addr; u32 fb_w, fb_h, fb_pitch, fb_bpp;
    u64 mmap; u32 mmap_count, boot_mode;
    u64 kernel_phys, kernel_size, initrd_phys, initrd_size, rsdp, arena_phys, arena_size;
    u32 fb_rgb, boot_serial;
} bootinfo_t;
typedef struct PACKED { u64 base, len; u32 type, acpi; } mment_t;
extern bootinfo_t bi;

/* --- lib --- */
void *memcpy(void *, const void *, size_t); void *memset(void *, int, size_t);
void *memmove(void *, const void *, size_t); int memcmp(const void *, const void *, size_t);
size_t strlen(const char *); int strcmp(const char *, const char *); int strncmp(const char *, const char *, size_t);
int strcasecmp(const char *, const char *); char *strncpy0(char *, const char *, size_t);
char *strchr(const char *, int); char *strrchr(const char *, int);
int vsnprintf(char *, size_t, const char *, va_list); int snprintf(char *, size_t, const char *, ...);
void kprintf(const char *, ...); void panic(const char *, ...) __attribute__((noreturn));
void serial_init(void); int dmesg_read(char *, int);
int utf8_decode(const char **); int utf8_encode(u32, char *);

/* --- cpu --- */
typedef struct { u64 r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx, rax, vec, err, rip, cs, rflags, rsp, ss; } regs_t;
void cpu_init(void); void timer_init(void); void tss_set_rsp0(u64); void udelay(u64); void mdelay_busy(u64);
extern volatile u64 ticks; extern u64 tsc_khz; extern char cpu_brand[49];
void fpu_save(void *); void fpu_load(void *); void fpu_init_state(void *);

/* --- memoria --- */
#define DMAP 0xFFFF800000000000UL
#define KVMA 0xFFFFFFFF80000000UL
#define P2V(p) ((void *)((u64)(p) + DMAP))
#define PAGE 4096UL
#define PTE_P 1UL
#define PTE_W 2UL
#define PTE_U 4UL
#define PTE_PWT 8UL
#define PTE_PCD 16UL
#define PTE_PS 128UL
#define PTE_NOFREE (1UL << 9)
#define PTE_ADDR 0x000FFFFFFFFFF000UL
u64 V2P(const void *);
void mm_init(void);
u64 pmm_alloc(void); void pmm_free(u64); u64 pmm_alloc_contig(u64 n, u64 maxphys); void pmm_free_contig(u64, u64);
u64 pmm_free_pages(void); u64 pmm_total_pages(void);
void *kmalloc(size_t); void *kzalloc(size_t); void kfree(void *);
void *dma_alloc(size_t); void dma_free(void *, size_t);
void *ioremap(u64 phys, u64 size); void *ioremap_wc(u64 phys, u64 size);
u64 vm_new_space(void); void vm_free_space(u64 pml4); int vm_map(u64 pml4, u64 va, u64 pa, u64 flags);
void vm_unmap(u64 pml4, u64 va); u64 vm_lookup(u64 pml4, u64 va, u64 *flags);
extern u64 kernel_cr3;

/* --- tareas --- */
typedef struct { u32 type, win; i32 x, y; u32 key, ch, mods, btn; } zevent_t;
enum { EV_NONE, EV_KEY, EV_KEYUP, EV_MOVE, EV_DOWN, EV_UP, EV_WHEEL, EV_CLOSE, EV_RESIZE, EV_FOCUS, EV_BLUR, EV_WINLIST, EV_ENTER, EV_LEAVE, EV_THEME, EV_JOY };
#define MAXFD 32
#define EVQ 128
struct file; struct proc;
enum { T_READY, T_BLOCKED, T_ZOMBIE };
typedef struct task {
    u64 rsp; u32 tid; volatile int state; char name[32];
    struct proc *proc; u64 wake_at; u8 *kstack; u64 kstack_top;
    u8 fx_raw[512 + 16]; u8 *fx;
    struct task *next;
    zevent_t evq[EVQ]; volatile u32 evh, evt; volatile int ev_wait;
    u64 cpu_ticks;
} task_t;
typedef struct proc {
    int pid; char name[32]; u64 cr3; u64 brk, brk_base; u64 stack_lo, stack_hi;
    struct file *fds[MAXFD]; volatile int killed; int nthreads; char args[1024]; u64 mem_pages;
    int exit_code;
} proc_t;
extern task_t *cur;
void sched_init(void); void schedule(void); void yield(void); void msleep(u64);
task_t *kthread(const char *, void (*)(void *), void *);
void task_block(u64 timeout_ms); void task_wake(task_t *); void timer_tick(void);
int proc_spawn(const char *path, const char *args); int proc_kill(int pid); void proc_exit(int code) __attribute__((noreturn));
void task_exit(void) __attribute__((noreturn)); int thread_create(u64 entry, u64 stack, u64 arg);
int vm_fault(u64 addr); int user_ok(const void *p, u64 n); int user_str(const char *p, char *out, int max);
typedef struct { int pid; int threads; char name[32]; u64 mem_kb; u64 cpu; } zproc_t;
int proc_list(zproc_t *, int);
void ev_push(task_t *, const zevent_t *); int ev_pop(zevent_t *, i64 timeout_ms);
typedef struct { volatile task_t *owner; int depth; } mutex_t;
void mutex_lock(mutex_t *); void mutex_unlock(mutex_t *);

/* --- pci --- */
typedef struct { u8 bus, dev, fn, cls, sub, pif, irq; u16 vendor, device; u32 bar[6]; } pci_t;
#define MAXPCI 64
extern pci_t pci_devs[MAXPCI]; extern int pci_count;
void pci_init(void); u32 pci_read(const pci_t *, int); void pci_write(const pci_t *, int, u32);
void pci_enable(const pci_t *, int mem, int io, int master); u64 pci_bar(const pci_t *, int);
u16 pci_read16(const pci_t *, int); void pci_write16(const pci_t *, int, u16);

/* --- acpi --- */
void acpi_init(void); void acpi_poweroff(void); void acpi_reboot(void);

/* --- bloques --- */
typedef struct blkdev {
    char name[16]; u32 ssize; u64 sectors; void *drv; u64 offset; struct blkdev *parent;
    int (*read)(struct blkdev *, u64 lba, u32 n, void *); int (*write)(struct blkdev *, u64 lba, u32 n, const void *);
    int removable;
    char model[41]; mutex_t lock; int present; u8 *bounce; u32 maxsect; int (*flush)(struct blkdev *);
} blkdev_t;
#define MAXBLK 32
extern blkdev_t *blkdevs[MAXBLK]; extern int blk_count;
blkdev_t *blk_new(const char *prefix); void blk_register(blkdev_t *); int blk_read(blkdev_t *, u64, u32, void *); int blk_write(blkdev_t *, u64, u32, const void *);
void blk_remove(blkdev_t *); int blk_eject(const char *); void blk_sync(void); blkdev_t *blk_find(const char *); int blk_rescan(blkdev_t *); int blk_add_part(blkdev_t *, u64, u64);
void ata_init(void); void ahci_init(void); void nvme_init(void); void sdhci_init(void);

/* --- vfs --- */
#define O_READ 1
#define O_WRITE 2
#define O_CREATE 4
#define O_TRUNC 8
#define O_APPEND 16
enum { E_OK = 0, E_NOENT = -2, E_IO = -5, E_BADF = -9, E_NOMEM = -12, E_EXIST = -17, E_NOTDIR = -20, E_ISDIR = -21, E_ACCES = -13, E_INVAL = -22, E_NOSPC = -28, E_ROFS = -30, E_NOTEMPTY = -39, E_NOSYS = -38, E_TIMEOUT = -110, E_CONN = -111 };
typedef struct { u64 size; u32 is_dir; u32 mtime; char name[256]; } zdirent_t;
struct mount;
typedef struct file {
    struct mount *m; u64 pos, size; int flags, refs, is_sock; void *priv; u64 a, b, c, d;
} file_t;
typedef struct fsops {
    int (*open)(struct mount *, const char *, int, file_t *);
    i64 (*read)(file_t *, void *, u64); i64 (*write)(file_t *, const void *, u64);
    void (*close)(file_t *); int (*stat)(struct mount *, const char *, zdirent_t *);
    int (*readdir)(struct mount *, const char *, int, zdirent_t *);
    int (*mkdir)(struct mount *, const char *); int (*unlink)(struct mount *, const char *);
    int (*rename)(struct mount *, const char *, const char *); void (*sync)(struct mount *); int (*contig)(struct mount *, const char *, u64, u64 *);
} fsops_t;
typedef struct mount { char name[16]; const fsops_t *ops; void *priv; blkdev_t *dev; char fstype[8]; char label[16]; u64 total, free_b; volatile i64 *free_src; u32 free_mul; int dead, ro; } mount_t;
void vfs_init(void); int vfs_mount(const char *, const fsops_t *, void *, blkdev_t *, const char *, const char *);
void vfs_umount_dev(blkdev_t *); void fs_probe(blkdev_t *);
int vfs_open(const char *, int, file_t **); i64 vfs_read(file_t *, void *, u64); i64 vfs_write(file_t *, const void *, u64);
i64 vfs_seek(file_t *, i64, int); void vfs_close(file_t *); int vfs_stat(const char *, zdirent_t *);
int vfs_readdir(const char *, int, zdirent_t *); int vfs_mkdir(const char *); int vfs_unlink(const char *); int vfs_rename(const char *, const char *);
void *vfs_slurp(const char *, u64 *); mount_t *vfs_resolve(const char *, const char **);
/* sesion.c */
int sess_path(char *p, int write); int sess_spawn_ok(const char *path); int sess_guest(void); int sess_has_pin(void); long sess_syscall(u64 op, const char *b, const char *c);
 int vfs_contig(const char *, u64, u64 *); void vfs_sync(void);
typedef struct { char name[16]; char fstype[8]; char label[16]; u64 total, free_b; char dev[16]; u32 ro, pad; } zmount_t;
int vfs_set_ro(const char *, int);
int vfs_mounts(zmount_t *, int);
int fat_probe(blkdev_t *, const char *); int iso_probe(blkdev_t *, const char *); int exfat_probe(blkdev_t *, const char *); int ext_probe(blkdev_t *, const char *); int ntfs_probe(blkdev_t *, const char *);

/* --- entrada --- */
#define M_SHIFT 1
#define M_CTRL 2
#define M_ALT 4
#define M_ALTGR 8
#define M_GUI 16
#define M_NUM 32
#define M_CAPS 64
void input_joy(u32 btn, int dx, int dy, u32 id); extern volatile u8 kbd_leds; extern volatile u32 kbd_ledgen; void kbd_set_leds(u8); void rtc_write(const u32 *);
void input_init(void); void ps2_poll(void); void input_key(u8 usage, int down);
void input_mouse(int dx, int dy, int btn, int wheel); void input_abs(int x, int y, int maxx, int maxy, int btn, int wheel);
typedef struct { int kind; int a, b, c, d; } inev_t;
int input_pop(inev_t *); u32 key_translate(u8 usage, int mods); extern int key_layout; extern task_t *wm_task;

/* --- ventanas --- */
typedef struct { u32 acc, inact, border, ttxt, ttxt2, close, d1, d2, panel1, panel2, ui_acc, ui_acc2, ui_bg, ui_panel, ui_line, ui_text, ui_muted, ui_white; u32 title_sc, text_sc, panel_top, menu_grid, icon_sc, show_icons, motif, lang, gen; } ztheme_t;
void wm_theme_get(ztheme_t *); void wm_theme_set(const ztheme_t *);
void wm_init(void); int wm_ready(void);
typedef struct { i32 x, y, w, h; u32 flags; u64 title; } zwincfg_t;
typedef struct { u32 id; i32 x, y, w, h; u32 flags, state; u64 canvas; char title[64]; int pid; } zwininfo_t;
int wm_create(const zwincfg_t *, const char *title); int wm_destroy(int id); void wm_proc_gone(proc_t *);
int wm_present(int id, int x, int y, int w, int h); int wm_info(int id, zwininfo_t *); int wm_list(zwininfo_t *, int);
int wm_op(int id, int op, i64 a, i64 b); int wm_resize(int id, int w, int h); int wm_set_title(int id, const char *);
extern u32 scr_w, scr_h;
void fb_early_panic(const char *);

/* --- usb --- */
void xhci_init(void); void ehci_init(void);

/* --- red --- */
typedef struct netdev {
    char name[8]; u8 mac[6]; void *priv; int up; const char *kind;
    int (*tx)(struct netdev *, const void *, u32); int (*rx)(struct netdev *, void *);
    int (*link)(struct netdev *); void (*dbg)(struct netdev *); volatile int *gone;
    u64 rxp, txp, rxb, txb;
} netdev_t;
void net_register(netdev_t *); void netdrv_init(void); void net_init(void);
typedef struct { u8 mac[6]; u8 up, dhcp; u32 ip, mask, gw, dns; u64 rxp, txp, rxb, txb; char name[8]; char kind[16]; } znet_t;
int net_info(znet_t *); int net_dns(const char *, u32 *); int net_ping(u32 ip, int timeout, int seq);
int sock_new(int type); int sock_connect(file_t *, u32 ip, int port, int timeout); i64 sock_send(file_t *, const void *, u64);
i64 sock_recv(file_t *, void *, u64, int timeout); int sock_bind(file_t *, int port); int sock_accept(file_t *, int timeout, file_t **);
void sock_close(file_t *); i64 sock_sendto(file_t *, u32, int, const void *, u64); file_t *sock_file(int type);

/* --- sonido --- */
void snd_init(void); i64 snd_write(const void *, u64); int snd_info(char *, int); int snd_volume(int); int snd_space(void); int rec_start(void); i64 rec_read(void *, u64); void rec_stop(void); void rec_gain(int);
void backlight_init(void); int backlight_get(void); int backlight_set(int);

int fd_alloc(file_t *);
u64 syscall_dispatch(regs_t *);
void rtc_read(u32 *out);
void usbprn_add(u16 vid, u16 pid, const u8 *rawid, int (*wr)(void *, const void *, u32), void *ctx, volatile int *gone);
