/* libz - biblioteca de las aplicaciones de ZunzunOS */
#pragma once
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned long u64;
typedef int i32; typedef long i64; typedef unsigned long size_t;
typedef __builtin_va_list va_list;
#define va_start __builtin_va_start
#define va_end __builtin_va_end
#define va_arg __builtin_va_arg
#define NULL ((void *)0)
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
const char *tr(const char *s); const char *trl(const char *s); const unsigned char *z_glyph(int cp);
#define _(s) tr(s)

static inline long sc5(long n, long a, long b, long c, long d, long e) {
    long r; __asm__ volatile("int $0x40" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e) : "memory", "cc"); return r;
}
#define SC0(n) sc5(n, 0, 0, 0, 0, 0)
#define SC1(n, a) sc5(n, (long)(a), 0, 0, 0, 0)
#define SC2(n, a, b) sc5(n, (long)(a), (long)(b), 0, 0, 0)
#define SC3(n, a, b, c) sc5(n, (long)(a), (long)(b), (long)(c), 0, 0)
#define SC4(n, a, b, c, d) sc5(n, (long)(a), (long)(b), (long)(c), (long)(d), 0)

typedef struct { u32 acc, inact, border, ttxt, ttxt2, close, d1, d2, panel1, panel2, ui_acc, ui_acc2, ui_bg, ui_panel, ui_line, ui_text, ui_muted, ui_white; u32 title_sc, text_sc, panel_top, menu_grid, icon_sc, show_icons, motif, lang, gen; } ztheme;
extern ztheme TH;
typedef struct { u32 type, win; i32 x, y; u32 key, ch, mods, btn; } zevent;
enum { EV_NONE, EV_KEY, EV_KEYUP, EV_MOVE, EV_DOWN, EV_UP, EV_WHEEL, EV_CLOSE, EV_RESIZE, EV_FOCUS, EV_BLUR, EV_WINLIST, EV_ENTER, EV_LEAVE, EV_THEME, EV_JOY, EV_REDRAW };
enum { K_ENTER = 0x28, K_ESC = 0x29, K_BS = 0x2A, K_TAB = 0x2B, K_F1 = 0x3A, K_F5 = 0x3E, K_INS = 0x49, K_HOME = 0x4A, K_PGUP = 0x4B, K_DEL = 0x4C, K_END = 0x4D, K_PGDN = 0x4E, K_RIGHT = 0x4F, K_LEFT = 0x50, K_DOWN = 0x51, K_UP = 0x52 };
#define M_SHIFT 1
#define M_CTRL 2
#define M_ALT 4
#define WF_DECOR 1
#define WF_RESIZE 2
#define WF_DESKTOP 4
#define WF_PANEL 8
#define WF_POPUP 16
#define WF_LOCK 32
#define O_READ 1
#define O_WRITE 2
#define O_CREATE 4
#define O_TRUNC 8
#define O_APPEND 16
typedef struct { i32 x, y, w, h; u32 flags; u64 title; } zwincfg;
typedef struct { u32 id; i32 x, y, w, h; u32 flags, state; u64 canvas; char title[64]; int pid; } zwininfo;
typedef struct { u64 size; u32 is_dir; u32 mtime; char name[256]; } zdirent;
typedef struct { int pid; int threads; char name[32]; u64 mem_kb; u64 cpu; } zproc;
typedef struct { u64 mem_total, mem_free, uptime_ms; u32 scr_w, scr_h, nproc, boot_mode; char cpu[49]; char pad[7]; u64 tsc_khz; int layout; } zsys;
typedef struct { char name[16]; char fstype[8]; char label[16]; u64 total, free_b; char dev[16]; u32 ro, pad; } zmount;
typedef struct { u8 mac[6]; u8 up, dhcp; u32 ip, mask, gw, dns; u64 rxp, txp, rxb, txb; char name[8]; char kind[16]; } znet;
typedef struct { u8 bus, dev, fn, cls, sub, pif; u16 vendor, device; } zpci;
typedef struct { char name[16]; char model[41]; u64 sectors; u32 ssize; u32 removable; } zdisk;
typedef struct { char name[16]; u64 start, sectors; } zpart;

/* sistema */
static inline void z_exit(int c) { SC1(0, c); for (;;); }
static inline void z_yield(void) { SC0(1); }
static inline void z_sleep(int ms) { SC1(2, ms); }
static inline u64 z_ms(void) { return SC0(3); }
static inline void z_time(u32 *t) { SC1(4, t); }
static inline int z_spawn(const char *p, const char *a) { return SC2(5, p, a); }
static inline int z_kill(int p) { return SC1(6, p); }
static inline int z_procs(zproc *o, int n) { return SC2(7, o, n); }
static inline void z_sysinfo(zsys *s) { SC1(8, s); }
static inline void *z_sbrk(long n) { return (void *)SC1(9, n); }
static inline void z_log(const char *s) { SC1(12, s); }
static inline int z_dmesg(char *b, int n) { return SC2(13, b, n); }
static inline void z_setlayout(int l) { SC1(14, l); }
static inline void z_clip_set(const char *b, int n) { SC2(15, b, n); }
static inline int z_clip_get(char *b, int n) { return SC2(16, b, n); }
static inline void z_reboot(void) { SC1(90, 1); }
static inline void z_poweroff(void) { SC1(90, 0); }
/* "--imprimir RUTA": abrir el archivo y mostrar enseguida el dialogo Imprimir (lo usa Archivos) */
static inline const char *z_print_arg(const char *a, int *p) { const char *k = "--imprimir "; int i = 0; while (k[i] && a[i] == k[i]) i++; *p = !k[i]; return *p ? a + 11 : a; }
/* sesion: 0 consulta (bits 0-2 modo 1 admin 2 invitado, 8 hay PIN, 16 hay /sys), 1 entrar como admin, 2 invitado, 3 cerrar, 4 cambiar PIN, 5 comprobar PIN */
static inline int z_sess(int op, const char *a, const char *b) { return SC3(109, op, a, b); }
static inline long z_idle_ms(void) { return SC0(110); } /* tiempo sin tocar teclado ni raton */
static inline int z_screen(int on) { return SC1(111, on); }
static inline int z_capture(u32 *px, long npix) { return SC2(112, px, npix); } /* devuelve ancho << 16 | alto */
int z_png_save(const char *path, const u32 *px, int w, int h);
/* video: JPEG y AVI (MJPEG + PCM) */
long z_jpeg_encode(const u32 *px, int w, int h, int quality, u8 **out);
typedef struct { int fd, w, h, fps, arate, frames, nidx, cap; long movi, pos, asamples; u8 *idx; } zavi_t;
typedef struct { int fd; long n; int w, h, nf, capf, na, capa, vstream, astream, achan, arate, abits; u32 us_frame; long *fo, *fl, *ao, *al; } zavi_r;
int zavi_open(zavi_t *a, const char *path, int w, int h, int fps, int arate); int zavi_frame(zavi_t *a, const u8 *jpg, long n); int zavi_audio(zavi_t *a, const short *pcm, long samples); int zavi_close(zavi_t *a);
int zavi_load(zavi_r *r, const char *path); u8 *zavi_read(zavi_r *r, int i, long *n); u8 *zavi_read_audio(zavi_r *r, int i, long *n); void zavi_free(zavi_r *r);
/* imagen en el portapapeles */
int z_clip_set_img(const u32 *px, int w, int h); u32 *z_clip_get_img(int *w, int *h);
int z_shortcut_target(const char *path, char *out, int cap); int z_make_shortcut(const char *target, const char *dir, char *made, int cap);
static inline int z_pci(zpci *o, int n) { return SC2(91, o, n); }
static inline int z_disks(zdisk *o, int n) { return SC2(92, o, n); }
static inline int z_bright_set(int p) { return SC1(93, p); }
static inline int z_bright_get(void) { return SC0(94); }
static inline int z_disk_read(const char *n, u64 l, int c, void *b) { return SC4(95, n, l, c, b); }
static inline int z_disk_write(const char *n, u64 l, int c, const void *b) { return SC4(96, n, l, c, b); }
static inline int z_disk_rescan(const char *n) { return SC1(97, n); }
static inline int z_parts(const char *n, zpart *o, int m) { return SC3(98, n, o, m); }
static inline int z_fat_contig(const char *p, u64 size, u64 *lba) { return SC3(101, p, size, lba); }
static inline void z_theme_get(ztheme *t) { SC1(102, t); }
static inline void z_rtc_set(const u32 *t) { SC1(104, t); }
static inline void z_joy_inject(u32 b, int dx, int dy) { SC3(105, b, dx, dy); }
static inline int z_eject(const char *dev) { return SC1(106, dev); }
static inline void z_theme_set(const ztheme *t) { SC1(103, t); }
static inline int z_set_ro(const char *mnt, int ro) { return SC2(99, mnt, ro); }
static inline int z_add_part(const char *d, u64 st, u64 n) { return SC3(100, d, st, n); }
int z_thread(void (*fn)(void *), void *arg, int stack);

/* ventanas */
static inline int zw_op(int id, int op, long a, long b) { return SC4(23, id, op, a, b); }
static inline int zw_info(int id, zwininfo *i) { return SC2(24, id, i); }
static inline int zw_list(zwininfo *o, int n) { return SC2(25, o, n); }
static inline int z_event_raw(zevent *e, long ms) { return SC2(26, e, ms); }
int z_event(zevent *e, long ms); extern int z_joy_raw;
static inline int zw_title(int id, const char *t) { extern void z_title_key(int, const char *); z_title_key(id, t); return SC2(28, id, tr(t)); }

/* archivos */
static inline int z_open(const char *p, int f) { return SC2(40, p, f); }
static inline int z_close(int f) { return SC1(41, f); }
static inline long z_read(int f, void *b, long n) { return SC3(42, f, b, n); }
static inline long z_write(int f, const void *b, long n) { return SC3(43, f, b, n); }
static inline long z_seek(int f, long o, int w) { return SC3(44, f, o, w); }
static inline int z_stat(const char *p, zdirent *d) { return SC2(45, p, d); }
static inline int z_readdir(const char *p, int i, zdirent *d) { return SC3(46, p, i, d); }
static inline int z_mkdir(const char *p) { return SC1(47, p); }
static inline int z_unlink(const char *p) { return SC1(48, p); }
static inline int z_rename(const char *a, const char *b) { return SC2(49, a, b); }
static inline int z_mounts(zmount *o, int n) { return SC2(50, o, n); }
static inline void z_sync(void) { SC0(51); }
void *z_readfile(const char *p, long *size);
int z_writefile(const char *p, const void *b, long n);
const char *z_errstr(int e);

/* red */
static inline int z_socket(int t) { return SC1(60, t); }
static inline int z_connect(int s, u32 ip, int port, int to) { return SC4(61, s, ip, port, to); }
static inline long z_send(int s, const void *b, long n) { return SC3(62, s, b, n); }
static inline long z_recv(int s, void *b, long n, int to) { return SC4(63, s, b, n, to); }
static inline int z_dns(const char *h, u32 *ip) { return SC2(64, h, ip); }
static inline int z_ping(u32 ip, int to, int seq) { return SC3(65, ip, to, seq); }
static inline int z_netinfo(znet *n) { return SC1(66, n); }
static inline int z_bind(int s, int p) { return SC2(67, s, p); }
static inline int z_accept(int s, int to) { return SC2(68, s, to); }
int z_parse_ip(const char *s, u32 *ip); void z_fmt_ip(u32 ip, char *o);

/* sonido */
static inline long z_audio(const void *b, long n) { return SC2(80, b, n); }
static inline int z_audio_info(char *b, int n) { return SC2(81, b, n); }
static inline int z_audio_vol(int v) { return SC1(82, v); }
static inline int z_audio_space(void) { return SC0(83); }
static inline int z_rec_start(void) { return SC0(84); }
static inline long z_rec_read(void *b, long n) { return SC2(85, b, n); }
static inline void z_rec_stop(void) { SC0(86); }
static inline void z_rec_gain(int g) { SC1(87, g); }

/* C */
void *memcpy(void *, const void *, size_t); void *memset(void *, int, size_t); void *memmove(void *, const void *, size_t);
int memcmp(const void *, const void *, size_t); size_t strlen(const char *); int strcmp(const char *, const char *);
int strncmp(const char *, const char *, size_t); int strncasecmp(const char *, const char *, size_t); int strcasecmp(const char *, const char *); char *strcpy(char *, const char *);
char *strncpy0(char *, const char *, size_t); char *strchr(const char *, int); char *strrchr(const char *, int); char *strstr(const char *, const char *);
char *strcasestr(const char *, const char *); char *strcat(char *, const char *); int atoi(const char *); double atof(const char *);
void *malloc(size_t); void *calloc(size_t, size_t); void *realloc(void *, size_t); void free(void *); char *strdup(const char *);
int vsnprintf(char *, size_t, const char *, va_list); int snprintf(char *, size_t, const char *, ...);
int utf8_next(const char **); int utf8_put(u32, char *); int tolower(int); int toupper(int); int isspace(int); int isdigit(int); int isalpha(int);
void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
u32 rnd(void);
double z_sqrt(double); double z_pow(double, double); double z_sin(double); double z_cos(double); double z_tan(double); double z_atan(double); double z_ln(double); double z_exp(double); double z_floor(double); double z_fabs(double);

/* grafica */
typedef struct { int id, w, h; u32 *px; int cx0, cy0, cx1, cy1; } zwin;
zwin *zw_new(const char *title, int x, int y, int w, int h, u32 flags);
void zw_close(zwin *); void zw_present(zwin *); void zw_present_rect(zwin *, int, int, int, int); void zw_resized(zwin *, int, int);
void zw_clip(zwin *, int, int, int, int); void zw_noclip(zwin *);
void g_clear(zwin *, u32); void g_rect(zwin *, int, int, int, int, u32); void g_frame(zwin *, int, int, int, int, u32);
void g_pixel(zwin *, int, int, u32); void g_line(zwin *, int, int, int, int, u32); void g_circle(zwin *, int, int, int, u32, int fill);
void g_grad(zwin *, int, int, int, int, u32, u32); void g_round(zwin *, int, int, int, int, int, u32);
int g_text(zwin *, int x, int y, const char *, u32); int g_textb(zwin *, int x, int y, const char *, u32); int g_text2(zwin *, int x, int y, const char *, u32, int scale);
int g_textw(const char *); void g_image(zwin *, int, int, int, int, const u32 *);
void g_glyph(zwin *, int x, int y, int cp, u32 c);

/* controles */
#define UI_BG TH.ui_bg
#define UI_PANEL TH.ui_panel
#define UI_LINE TH.ui_line
#define UI_TEXT TH.ui_text
#define UI_MUTED TH.ui_muted
#define UI_ACC TH.ui_acc
#define UI_ACC2 TH.ui_acc2
#define UI_WHITE TH.ui_white
int ui_in(int mx, int my, int x, int y, int w, int h);
void ui_button(zwin *, int x, int y, int w, int h, const char *, int state);
int ui_input_key(char *buf, int cap, int *cur, zevent *e);
void ui_input(zwin *, int x, int y, int w, int h, const char *txt, int cur, int focus);
int ui_prompt(const char *title, const char *msg, char *buf, int cap);
int ui_confirm(const char *title, const char *msg);
int z_png_decode(const u8 *d, long n, u32 **out, int *w, int *h);
int z_bmp_decode(const u8 *d, long n, u32 **out, int *w, int *h);
int z_img_load(const char *p, u32 **out, int *w, int *h);
/* idioma */
/* ajustes guardados en /sys/ZUNZUN/AJUSTES.TXT */
int z_cfg_get(const char *key, int def); void z_cfg_gets(const char *key, char *out, int cap); void z_cfg_set(const char *key, int v); void z_cfg_sets(const char *key, const char *v);
typedef struct { const char *name, *bin; u32 col; const char *sym; int fam; } zapp; extern const char *const z_fams[]; extern const u32 z_famcol[]; extern const int z_nfams; const char *z_app_for(const char *path); int z_open_path(const char *path); extern const zapp z_apps[]; extern const int z_napps; int z_hidden(const char *bin); void z_set_hidden(const char *bin, int hide); void z_notify(void); void z_flag(zwin *w, int x, int y, int lang);
/* papelera, menus y fechas */
int ui_edit_menu(zwin *w, zevent *e); int z_trash(const char *path); int ui_menu(int sx, int sy, const char **items, int n); void zw_screen(zwin *w, int x, int y, int *sx, int *sy);
void z_fmt_date(const u32 *t, int fmt, char *o, int cap); void z_fmt_time(const u32 *t, int h24, int secs, char *o, int cap); int z_weekday(int y, int m, int d); const char *z_month(int m);
int z_jpeg_decode(const u8 *d, long n, u32 **out, int *w, int *h); int z_gif_decode(const u8 *d, long n, u32 **out, int *w, int *h); int z_png_encode(const u32 *px, int w, int h, u8 **out, long *n);
/* ofimatica */
typedef struct { char *p; long n, cap; } sb_t;
void sb_add(sb_t *, const void *, long); void sb_s(sb_t *, const char *); void sb_f(sb_t *, const char *, ...); void sb_esc(sb_t *, const char *, long);
long z_inflate(const u8 *in, long n, u8 *out, long cap); u32 z_crc32(const u8 *, long); u32 z_crc_add(u32, const u8 *, long);
u8 *zip_get(const u8 *z, long n, const char *name, long *out); int zip_save(const char *path, int cnt, const char **names, const char **data, const long *lens);
char *doc_import(const char *path, long *len); int z_theme_save(const ztheme *); int z_theme_load(ztheme *); void z_docdir(char *o); int z_ends(const char *s, const char *e);
typedef struct { char *t; long len, cap, cur, sel; long *ln; int nl, lcap, cols, top, changed; } te_t;
void te_init(te_t *); void te_set(te_t *, const char *, long); void te_layout(te_t *, int cols); int te_key(te_t *, zevent *, int page); void te_draw(zwin *, te_t *, int x, int y, int h, int sc, u32 fg, u32 selc, u32 curc, int center); void te_click(te_t *, int rx, int ry, int sc, int extend); int te_words(const char *, long); int te_line_of(te_t *, long);
/* discos: formateo e instalacion */
enum { DK_FAT32, DK_FAT16, DK_EXFAT, DK_EXT2 };
typedef void (*dk_cb)(int pct, const char *msg);
int dk_mkfs(const char *dev, u64 base, u64 total, u32 ss, int fs, const char *label, u64 hidden, dk_cb cb);
int dk_format_disk(const char *disk, u64 total, u32 ss, int fs, const char *label, dk_cb cb);
int dk_format_part(const char *disk, const zpart *p, u32 ss, int fs, const char *label, dk_cb cb);
int dk_install(const char *disk, u64 total, u32 ss, dk_cb cb);
int dk_sys_disk(char *out);
int dk_update_system(const char *mnt, const char *dev, const char *k, const char *i, const char *e, const char *boot, dk_cb cb); void dk_root(const char *dev, char *o);

/* impresion (zprint.c) */
enum { ZPF_SANS, ZPF_SANSB, ZPF_SERIF, ZPF_SERIFB, ZPF_MONO, ZPF_MONOB };
#define ZP_NONE 0xFF000000u
typedef struct zpdoc zpdoc;
typedef struct { double pw, ph; int paper, land, copies, from, to, color, dpi; } zpset;
typedef struct { zpdoc *d; double x0, x1, y, ytop, ybot; int started, num; } zpflow;
typedef struct { char name[64], uri[160], lang[8], model[64]; int paper, color, def, dpi; } zprinter;
typedef struct { char id[16], devid[240]; } zusbprn;
zpdoc *zp_new(const zpset *s); void zp_free(zpdoc *d); void zp_page(zpdoc *d); int zp_npages(zpdoc *d); double zp_pw(zpdoc *d); double zp_ph(zpdoc *d);
void zp_text(zpdoc *d, double x, double y, int font, double size, u32 col, const char *s);
void zp_rect(zpdoc *d, double x, double y, double w, double h, u32 fill, u32 stroke, double lw);
void zp_line(zpdoc *d, double x0, double y0, double x1, double y1, u32 col, double lw);
void zp_image(zpdoc *d, double x, double y, double w, double h, const u32 *px, int iw, int ih);
double zp_textw(int font, double size, const char *s);
void zp_flow_init(zpflow *f, zpdoc *d, double margin); void zp_flow_newpage(zpflow *f); void zp_para(zpflow *f, int font, double size, u32 col, const char *text, int align, double after);
long zp_render(zpdoc *d, const zpset *s, const char *lang, u8 **out); u32 *zp_raster(zpdoc *d, const zpset *s, int pg, int dpi, int *w, int *h); u32 *zp_raster_rows(zpdoc *d, const zpset *s, int pg, int dpi, int ry, int rows, int *w); long zp_render_qpdl(zpdoc *d, const zpset *s, u8 **out); int zp_print_text(const char *title, const char *sub, const char *text, int mono); int zp_print_image(const char *title, const u32 *px, int w, int h);
int zp_printers(zprinter *o, int max); int zp_save_printers(const zprinter *o, int n); int zp_send(const zprinter *p, const u8 *data, long len, const char *job);
int zp_print(zpdoc *d, const zprinter *p, const zpset *s, const char *job); int zp_ipp_query(const char *uri, char *model, int mcap, char *formats, int fcap, int *color);
int zp_usb_list(zusbprn *o, int max); const char *zp_guess_lang(const char *devid_or_formats);
extern int zp_default_land; int zp_dialog(const char *title, void (*build)(zpdoc *d, const zpset *s, void *ctx), void *ctx);
/* HTTP y HTTPS (zhttp.c) */
typedef struct { int status, tls, cert; char ctype[96]; char final[1100]; char err[96]; } zhttp_info;
int z_http(const char *method, const char *url, const char *body, long bl, const char *ctype, char **out, long *len, zhttp_info *inf);
extern int z_https_allow_bad; void z_https_allow(const char *host); const char *z_http_err(int e);

