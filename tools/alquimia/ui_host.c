/* Capturas de la interfaz de Alquimia sin arrancar ZunzunOS: el mismo alquimia.c, con las ventanas en memoria.
   tools/alquimia/ui_host.sh CARPETA_DE_PRUEBAS SALIDA  ->  PNG de cada pestana, en espanol, en ingles y en 480x320 */
#include "z.h"
#include "zcrypto.h"
#define main alq_main
#include "../../user/apps/alquimia.c"
#undef main
int printf(const char *, ...); char *getenv(const char *);
ztheme TH = {0x0F766E, 0x3B4252, 0x111827, 0xFFFFFF, 0xC8CDD6, 0xC0392B, 0x0B3B3C, 0x134E4A, 0x1F2937, 0x111827, 0x0F766E, 0xCCFBF1, 0xF3F4F6, 0xE5E7EB, 0xC4C9D2, 0x1F2937, 0x6B7280, 0xFFFFFF, 1, 1, 0, 0, 1, 1, 1, 0};
zevent z_last_down; int z_joy_raw;
static int SW = 1366, SH = 768;
zwin *zw_new(const char *t, int x, int y, int w, int h, u32 f) { zwin *z = calloc(1, sizeof *z); z->w = w; z->h = h; z->px = calloc((long)w * h, 4); zw_noclip(z); (void)t; (void)x; (void)y; (void)f; return z; }
void zw_close(zwin *w) { free(w->px); free(w); }
void zw_present(zwin *w) { (void)w; }
void zw_resized(zwin *w, int nw, int nh) { free(w->px); w->w = nw; w->h = nh; w->px = calloc((long)nw * nh, 4); zw_noclip(w); }
int z_event(zevent *e, long ms) { (void)e; (void)ms; return 0; }
void z_sysinfo(zsys *s) { memset(s, 0, sizeof *s); s->scr_w = SW; s->scr_h = SH; }
int z_mounts(zmount *o, int n) { (void)n; memset(o, 0, 2 * sizeof *o); strcpy(o[0].name, "/tmp"); strcpy(o[1].name, "/home"); o[1].ro = 1; return 2; }
static char CFG[2048]; int z_cfg_get(const char *k, int d) { (void)k; return d; } void z_cfg_set(const char *k, int v) { (void)k; (void)v; } void z_cfg_gets(const char *k, char *o, int c) { (void)k; (void)c; o[0] = 0; (void)CFG; } void z_cfg_sets(const char *k, const char *v) { (void)k; (void)v; }
int z_pick_file(const char *t, const char *e, char *o, int c) { (void)t; (void)e; (void)o; (void)c; return 0; } int z_pick_dir(const char *t, char *o, int c) { (void)t; (void)o; (void)c; return 0; }
int ui_confirm(const char *t, const char *m) { (void)t; (void)m; return 1; } int ui_menu(int x, int y, const char **i, int n) { (void)x; (void)y; (void)i; (void)n; return -1; }
int z_spawn(const char *p, const char *a) { (void)p; (void)a; return 0; } void z_clip_set(const char *b, int n) { (void)b; (void)n; } int z_clip_get(char *b, int n) { (void)n; b[0] = 0; return 0; }
void zs_rec(zwin *w, int x, int y, int sc, const char *s) { (void)w; (void)x; (void)y; (void)sc; (void)s; }
const char *trl(const char *s) { return tr(s); }
static const char *OUT;
static void shot(const char *n) { draw(); u8 *o; long len; if (z_png_encode(W->px, W->w, W->h, &o, &len)) return; char p[600]; snprintf(p, 600, "%s/%s.png", OUT, n); z_writefile(p, o, len); free(o); printf("captura %s %dx%d\n", p, W->w, W->h); }
int main(int argc, char **argv) {
    if (argc < 3) return 2; const char *T = argv[1]; OUT = argv[2]; char p[600];
    zsys s; z_sysinfo(&s); W = zw_new("Alquimia", 0, 0, 860, 620, 0); cv_defaults(&cvo); z_docdir(cdest); z_docdir(vdest);
    snprintf(p, 600, "%s/origen/Carpeta ñandú", T); c_add(p); snprintf(p, 600, "%s/conv/foto.png", T); c_add(p); snprintf(p, 600, "%s/conv/tono.wav", T); c_add(p);
    strcpy(cpw, "clave-larga-2026"); snprintf(cdest, 400, "%s/ui", T); strcpy(cname, "copia-de-seguridad"); tab = 0; shot("1-comprimir");
    c_run(); shot("2-comprimido");
    snprintf(p, 600, "%s/ui/copia-de-seguridad.zip", T); x_open(p); tab = 1; shot("3-extraer-cifrado");
    strcpy(xpw, "clave-larga-2026"); snprintf(xdest, 400, "%s/ui/extraido", T); x_run(0); shot("4-extraido");
    tab = 2; const char *F[] = {"conv/base.png", "conv/tono.mp3", "conv/clip.avi", "conv/libro.md", "conv/tabla.csv", "a.zip", "conv/blob.bin"}; for (int i = 0; i < 7; i++) { snprintf(p, 600, "%s/%s", T, F[i]); v_add(p); }
    snprintf(vdest, 400, "%s/ui", T); static const char *N[] = {"5-convertir-imagen", "6-convertir-sonido", "7-convertir-video", "8-convertir-documento", "9-convertir-datos", "10-convertir-comprimido", "11-convertir-otro"};
    for (int i = 0; i < 7; i++) { vsel = i; shot(N[i]); }
    vfmt[CK_IMG] = "jpg"; vfmt[CK_DOC] = "pdf"; vfmt[CK_DATA] = "xlsx"; vfmt[CK_VID] = "gif"; vfmt[CK_AUD] = "mp3"; vfmt[CK_ARC] = "tgz"; vfmt[CK_NONE] = "sha256"; v_run(); vsel = 0; shot("12-convertido");
    TH.lang = 1; tab = 0; shot("13-en-compress"); tab = 2; vsel = 1; shot("14-en-convert"); TH.lang = 0;
    SW = 480; SH = 320; zw_resized(W, 480, 300); tab = 0; shot("15-pantalla-pequena-comprimir"); tab = 1; shot("16-pantalla-pequena-extraer"); tab = 2; vsel = 3; shot("17-pantalla-pequena-convertir");
    return 0; }
