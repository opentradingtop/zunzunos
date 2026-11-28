/* Banco de pruebas de Alquimia en el anfitrion: el mismo motor que en ZunzunOS (user/apps/alquimia/*.c).
   Lo maneja tools/alquimia/verifica.py, que compara el resultado con unzip, tar, gzip, Python, PIL y ffmpeg. */
#include "z.h"
#include "zcrypto.h"
#define ALQ_HOST 1
#include "../../user/apps/alquimia/motor.c"
int printf(const char *, ...);
static int quiet(long d, long t, const char *w) { (void)d; (void)t; (void)w; return 0; }
static int lvl(const char *s) { return atoi(s); }
int main(int argc, char **argv) {
    if (argc < 2) return 2; const char *c = argv[1];
    if (!strcmp(c, "dfl") && argc == 5) { long n; u8 *d = z_readfile(argv[3], &n); if (!d) return 3; long on; u8 *o = dfl_all(d, n, lvl(argv[2]), &on);
        u8 *back = malloc(n + 1); long r = z_inflate(o, on, back, n); if (r != n || memcmp(back, d, n)) { printf("FALLO ida y vuelta %ld/%ld\n", r, n); return 1; }
        z_writefile(argv[4], o, on); printf("%ld -> %ld\n", n, on); return 0; }
    if ((!strcmp(c, "zip") || !strcmp(c, "tar") || !strcmp(c, "tgz") || !strcmp(c, "gz")) && argc >= 6) {
        alist L = {0}; for (int i = 5; i < argc; i++) al_path(&L, argv[i]); astat st; const char *pw = strcmp(argv[3], "-") ? argv[3] : 0;
        int f = !strcmp(c, "zip") ? AF_ZIP : !strcmp(c, "tar") ? AF_TAR : !strcmp(c, "tgz") ? AF_TGZ : AF_GZ;
        int r = arc_create(f, &L, argv[4], lvl(argv[2]), pw, quiet, &st); if (r) { printf("ERROR %s\n", st.err); return 1; } printf("%d archivos %ld -> %ld\n", st.files, st.in, st.out); al_free(&L); return 0; }
    if (!strcmp(c, "list") && argc == 3) { arch a; if (arch_open(&a, argv[2])) { printf("ERROR %s\n", a.err); return 1; }
        printf("tipo %d, %d entradas%s\n", a.kind, a.n, a.encrypted ? ", cifrado" : ""); for (int i = 0; i < a.n; i++) printf("%s\t%ld\t%ld\t%d\t%d\n", a.e[i].name, a.e[i].size, a.e[i].csize, a.e[i].meth, a.e[i].enc); arch_close(&a); return 0; }
    if ((!strcmp(c, "x") || !strcmp(c, "t")) && argc >= 4) { arch a; if (arch_open(&a, argv[2])) { printf("ERROR %s\n", a.err); return 1; } const char *pw = strcmp(argv[3], "-") ? argv[3] : 0; astat st;
        int r = arch_extract(&a, argc > 4 ? argv[4] : "/tmp", pw, 0, c[0] == 't', quiet, &st); arch_close(&a); if (r) { printf("ERROR %s\n", st.err); return 1; } printf("bien %d archivos %ld bytes\n", st.files, st.out); return 0; }
#ifdef ALQ_CV
    if (!strcmp(c, "conv") && argc >= 5) { cvopt o; cv_defaults(&o); for (int i = 5; i + 1 < argc; i += 2) cv_setopt(&o, argv[i], argv[i + 1]); char err[200] = "";
        int r = cv_convert(argv[2], argv[3], argv[4], &o, quiet, err); if (r) { printf("ERROR %s\n", err); return 1; } printf("bien\n"); return 0; }
    if (!strcmp(c, "salidas") && argc == 3) { int k = cv_kind(argv[2], 0, 0); const char *o[40]; int n = cv_outputs(k, o, 40); printf("%s:", CVK_NAME[k]); for (int i = 0; i < n; i++) printf(" %s", o[i]); printf("\n"); return 0; }
#endif
    return 2; }
