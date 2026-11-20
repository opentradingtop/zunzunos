/* Terminal con interprete de comandos */
#include "z.h"
int z_http_get(const char *, char **, long *, char *, int, int *);
#define COLS 200
#define TS ((int)TH.text_sc)
#define CW_ (8 * TS)
#define CH_ (16 * TS)
#define ROWS 1500
static zwin *W; static u16 *scr; static u8 *attr; static int cx, top, cols = 88, vis = 26, view; static u32 col = 0; static char cwd[256] = "/rd"; static char line[512]; static int ll, lc; static char hist[32][512]; static int nh, hi; static int cy;
static const u32 pal[] = {0xD1D5DB, 0x5EEAD4, 0xFCA5A5, 0xFDE68A, 0x93C5FD};
#define L(y) (scr + ((y) % ROWS) * COLS)
#define A(y) (attr + ((y) % ROWS) * COLS)
/* Imprimir: todo el texto de la terminal (lo que hay desde el principio del historial) */
static void print_(void) { sb_t b = {0}; sb_s(&b, ""); for (int y = top; y <= cy; y++) { u16 *r = L(y); int n = COLS; while (n > 0 && (!r[n - 1] || r[n - 1] == ' ')) n--; for (int x = 0; x < n; x++) { u32 c = r[x] ? r[x] : ' '; char u[4]; int k = 0; if (c < 0x80) u[k++] = c; else if (c < 0x800) { u[k++] = 0xC0 | c >> 6; u[k++] = 0x80 | (c & 63); } else { u[k++] = 0xE0 | c >> 12; u[k++] = 0x80 | (c >> 6 & 63); u[k++] = 0x80 | (c & 63); } sb_add(&b, u, k); } sb_s(&b, "\n"); }
    char sub_[300]; snprintf(sub_, 300, "%s: %s", _("Carpeta"), cwd); zp_print_text("Terminal", sub_, b.p, 1); free(b.p); }
/* seleccion con el raton: filas absolutas (y) y columnas */
static int tsa_y = -1, tsa_x, tsb_y = -1, tsb_x, tdrag;
static int first_row(void) { return MAX(top, cy - vis + 1 - view); }
static void cell_at(int px, int py, int *y, int *x) { *y = MIN(cy, first_row() + MAX(0, (py - 3) / CH_)); *x = MAX(0, MIN(cols, (px - 4 + CW_ / 2) / CW_)); }
static int tsel_has(void) { return tsa_y >= 0 && (tsa_y != tsb_y || tsa_x != tsb_x); }
static void tsel_ord(int *y0, int *x0, int *y1, int *x1) { if (tsa_y < tsb_y || (tsa_y == tsb_y && tsa_x <= tsb_x)) { *y0 = tsa_y; *x0 = tsa_x; *y1 = tsb_y; *x1 = tsb_x; } else { *y0 = tsb_y; *x0 = tsb_x; *y1 = tsa_y; *x1 = tsa_x; } }
static void tsel_copy(void) { if (!tsel_has()) return; int y0, x0, y1, x1; tsel_ord(&y0, &x0, &y1, &x1); sb_t b = {0}; sb_s(&b, ""); for (int y = y0; y <= y1; y++) { u16 *r = L(y); int a = y == y0 ? x0 : 0, e = y == y1 ? x1 : COLS; if (y != y1) { while (e > a && (!r[e - 1] || r[e - 1] == ' ')) e--; } for (int x = a; x < e; x++) { char u[4]; int k = utf8_put(r[x] ? r[x] : ' ', u); sb_add(&b, u, k); } if (y != y1) sb_s(&b, "\n"); } z_clip_set(b.p, b.n); char m[80]; snprintf(m, 80, "[prueba] term copio %ld bytes", b.n); z_log(m); free(b.p); }
static void newline(void) { cy++; cx = 0; memset(L(cy), 0, COLS * 2); memset(A(cy), 0, COLS); if (cy - top >= ROWS - 1) top++; }
static void draw(void) {
    g_clear(W, 0x0F172A); int first = MAX(top, cy - vis + 1 - view);
    if (tsel_has()) { int y0, x0, y1, x1; tsel_ord(&y0, &x0, &y1, &x1); for (int y = MAX(y0, first); y <= y1 && y < first + vis; y++) { int a = y == y0 ? x0 : 0, e = y == y1 ? x1 : cols; if (e > a) g_rect(W, 4 + a * CW_, 3 + (y - first) * CH_, (e - a) * CW_, CH_, 0x1E40AF); } }
    for (int r = 0; r < vis && first + r <= cy; r++) { int y = first + r; for (int x = 0; x < cols; x++) { u16 c = L(y)[x]; if (c) { char u[4]; int n = utf8_put(c, u); u[n] = 0; if (TS > 1) g_text2(W, 4 + x * CW_, 3 + r * CH_, u, pal[A(y)[x]], TS); else g_text(W, 4 + x * 8, 3 + r * 16, u, pal[A(y)[x]]); } } }
    if (!view) g_rect(W, 4 + cx * CW_, 3 + (cy - first) * CH_ + CH_ - 3, CW_, 2, 0x5EEAD4);
    zw_present(W);
}
static void putc2(int c) { if (c == '\n') { newline(); return; } if (c == '\r') return; if (c == '\t') { do putc2(' '); while (cx % 8); return; } if (cx >= cols) newline(); L(cy)[cx] = c; A(cy)[cx] = col; cx++; }
static void puts2(const char *s) { int c; while ((c = utf8_next(&s))) putc2(c); }
static void say(const char *f, ...) { char b[1024]; va_list a; va_start(a, f); vsnprintf(b, sizeof b, tr(f), a); va_end(a); puts2(b); }
static void flush2(void) { draw(); }
static void path_of(const char *a, char *o) {
    char t[512]; if (a[0] == '/') strncpy0(t, a, 512); else snprintf(t, 512, "%s/%s", cwd, a);
    char *p = t; int n = 0; o[0] = 0; char st[32][128]; int d = 0; (void)n;
    while (*p) { while (*p == '/') p++; if (!*p) break; char *e = p; while (*e && *e != '/') e++; int l = MIN(e - p, 127); char c[128]; memcpy(c, p, l); c[l] = 0; p = e; if (!strcmp(c, ".")) continue; if (!strcmp(c, "..")) { if (d) d--; continue; } if (d < 32) strcpy(st[d++], c); }
    int k = 0; for (int i = 0; i < d; i++) k += snprintf(o + k, 256 - k, "/%s", st[i]); if (!d) strcpy(o, "/");
}
static int argv_split(char *s, char **v) { int n = 0; while (*s && n < 16) { while (isspace(*s)) *s++ = 0; if (!*s) break; if (*s == '"') { v[n++] = ++s; while (*s && *s != '"') s++; if (*s) *s++ = 0; } else { v[n++] = s; while (*s && !isspace(*s)) s++; } } return n; }
static void err(const char *w, int e) { col = 2; say("%s: %s\n", w, z_errstr(e)); col = 0; }
static void human(u64 n, char *o) { if (n >= 1UL << 30) snprintf(o, 16, "%lu.%lu GB", n >> 30, ((n >> 20) & 1023) * 10 / 1024); else if (n >= 1 << 20) snprintf(o, 16, "%lu MB", n >> 20); else if (n >= 1024) snprintf(o, 16, "%lu KB", n >> 10); else snprintf(o, 16, "%lu B", n); }
static int copy(const char *a, const char *b) {
    int f = z_open(a, O_READ); if (f < 0) return f; int g = z_open(b, O_WRITE | O_CREATE | O_TRUNC); if (g < 0) { z_close(f); return g; }
    char *buf = malloc(65536); long r; int e = 0; while ((r = z_read(f, buf, 65536)) > 0) { long w = z_write(g, buf, r); if (w != r) { e = w < 0 ? (int)w : -28; break; } } if (r < 0) e = r; free(buf); z_close(f); z_close(g); return e;
}
static void cmd_ls(const char *p) { zdirent d; int n = 0, i = 0; while (!z_readdir(p, i++, &d)) { char s[16]; human(d.size, s); if (d.is_dir) { col = 4; say(_("%-30s  <carpeta>\n"), d.name); col = 0; } else say("%-30s  %s\n", d.name, s); n++; } if (!n) { zdirent q; int e = z_stat(p, &q); if (e) err(p, e); } }
/* prueba completa de un volumen: escribe, relee, nombres largos, renombra, borra */
static int t_ok, t_bad; static void chk(const char *n, int ok) { char m[128]; snprintf(m, 128, "[prueba] %s: %s", n, ok ? "OK" : "FALLO"); z_log(m); col = ok ? 1 : 2; say("%s\n", m); col = 0; flush2(); if (ok) t_ok++; else t_bad++; }
static void test_fs(const char *root) {
    char dir[256], f[320], g[320]; snprintf(dir, 256, "%s/Prueba Zunzun", root); t_ok = t_bad = 0; zdirent st;
    z_unlink(dir); chk("crear carpeta", z_mkdir(dir) == 0); snprintf(f, 320, "%s/Archivo con nombre muy largo y acentos áé ñ.dat", dir); long sz = 300000; u8 *a = malloc(sz), *b = malloc(sz);
    for (long i = 0; i < sz; i++) a[i] = (i * 7 + (i >> 8)) & 255; chk("escribir 300 KB", z_writefile(f, a, sz) == 0);
    int h = z_open(f, O_READ); long got = 0, r; while (h >= 0 && (r = z_read(h, b + got, MIN(7777, sz - got))) > 0) got += r; if (h >= 0) z_close(h); { long bad = -1; for (long i = 0; i < got && i < sz; i++) if (a[i] != b[i]) { bad = i; break; } char m[100]; if (got != sz || bad >= 0) { snprintf(m, 100, "[prueba] detalle: leidos %ld de %ld, primer error en %ld", got, sz, bad); z_log(m); } } chk("releer y comparar", got == sz && !memcmp(a, b, sz));
    chk("stat tamano", !z_stat(f, &st) && st.size == (u64)sz);
    h = z_open(f, O_WRITE); z_seek(h, 123456, 0); z_write(h, "ZUNZUN", 6); z_close(h); memcpy(a + 123456, "ZUNZUN", 6); long n2; u8 *c = z_readfile(f, &n2); chk("escritura parcial", c && n2 == sz && !memcmp(a, c, sz)); free(c);
    int cnt = 0, i = 0; while (i < 40) { snprintf(g, 320, "%s/f%d.txt", dir, i); if (z_writefile(g, "hola", 4)) break; i++; cnt++; } chk("40 archivos (directorio crece)", cnt == 40); zdirent d; int seen = 0; while (!z_readdir(dir, seen, &d)) seen++; chk("listar 41 entradas", seen == 41);
    for (i = 0; i < 40; i++) { snprintf(g, 320, "%s/f%d.txt", dir, i); z_unlink(g); } snprintf(g, 320, "%s/renombrado.bin", dir); chk("renombrar", !z_rename(f, g) && !z_stat(g, &st) && st.size == (u64)sz); chk("carpeta no vacia protegida", z_unlink(dir) == -39); chk("borrar archivo", !z_unlink(g)); chk("borrar carpeta", !z_unlink(dir) && z_stat(dir, &st) == -2);
    free(a); free(b); char m[80]; snprintf(m, 80, "[prueba] resultado fs %s: %d ok, %d fallos", root, t_ok, t_bad); z_log(m); say("%s\n", m);
}
static void test_ro(const char *p) { long n; char *d = z_readfile(p, &n); char m[320]; snprintf(m, 320, "[prueba] lectura %s: %s (%ld bytes) [%.20s]", p, d ? "OK" : "FALLO", d ? n : 0, d ? d : ""); z_log(m); say("%s\n", m); free(d); }
static void tone(int hz, int ms) { int n = 48 * ms; short *b = malloc(n * 4); double ph = 0, st = 6.283185307 * hz / 48000; for (int i = 0; i < n; i++) { short v = z_sin(ph) * 9000; b[i * 2] = v; b[i * 2 + 1] = v; ph += st; if (ph > 6.283185307) ph -= 6.283185307; } z_audio(b, n * 4); free(b); }
static void ping(const char *h) { u32 ip; int e = z_dns(h, &ip); if (e) { err(h, e); return; } char s[16]; z_fmt_ip(ip, s); say("PING %s (%s)\n", h, s); int ok = 0; for (int i = 1; i <= 4; i++) { int r = z_ping(ip, 1500, i); if (r >= 0) { say(_("respuesta %d: %d ms\n"), i, r); ok++; } else say(_("respuesta %d: %s\n"), i, z_errstr(r)); flush2(); z_sleep(200); } char m[80]; snprintf(m, 80, "[prueba] ping %s: %d/4", h, ok); z_log(m); }
static void run(char *ln) {
    char *v[16]; int n = argv_split(ln, v); if (!n) return; char p[256], q[256]; const char *c = v[0]; { static const char *al[][2] = {{"disks", "discos"}, {"date", "fecha"}, {"keyboard", "teclado"}, {"open", "abrir"}, {"net", "red"}, {"server", "servidor"}, {"unlock", "desbloquear"}, {"tone", "tono"}, {"sound", "sonido"}, {"poweroff", "apagar"}, {"shutdown", "apagar"}, {"reboot", "reiniciar"}, {"restart", "reiniciar"}, {"brightness", "brillo"}, {"quit", "salir"}}; for (int i = 0; i < 15; i++) if (!strcmp(c, al[i][0])) c = al[i][1]; } char *a1 = n > 1 ? v[1] : 0, *a2 = n > 2 ? v[2] : 0; int e;
    if (!strcmp(c, "ayuda") || !strcmp(c, "help")) { col = 1; say(_("Archivos: ls cd pwd cat cp mv rm mkdir touch df discos\nSistema:  ps kill mem info lspci dmesg fecha teclado es|us abrir APP\nRed:      red ping HOST dns HOST wget URL [ARCHIVO] servidor PUERTO\nDiscos:   desbloquear VOLUMEN (permite escribir en un volumen protegido)\nOtros:    echo TXT [> ARCHIVO] tono [HZ] limpiar sonido salir apagar reiniciar\nPruebas:  prueba-fs CARPETA  prueba-lee ARCHIVO\n")); col = 0; }
    else if (!strcmp(c, "ls") || !strcmp(c, "dir")) { path_of(a1 ? a1 : ".", p); cmd_ls(p); }
    else if (!strcmp(c, "cd")) { path_of(a1 ? a1 : "/", p); zdirent d; e = z_stat(p, &d); if (e) err(p, e); else if (!d.is_dir) err(p, -20); else strcpy(cwd, p); }
    else if (!strcmp(c, "pwd")) say("%s\n", cwd);
    else if (!strcmp(c, "limpiar") || !strcmp(c, "clear")) { top = cy = 0; cx = 0; memset(scr, 0, COLS * ROWS * 2); }
    else if (!strcmp(c, "cat") && a1) { path_of(a1, p); long l; char *d = z_readfile(p, &l); if (!d) err(p, -2); else { puts2(d); if (l && d[l - 1] != '\n') say("\n"); free(d); } }
    else if (!strcmp(c, "cp") && a2) { path_of(a1, p); path_of(a2, q); zdirent d; if (!z_stat(q, &d) && d.is_dir) { char *b = strrchr(p, '/'); strcat(q, b); } e = copy(p, q); if (e) err(q, e); z_sync(); }
    else if (!strcmp(c, "mv") && a2) { path_of(a1, p); path_of(a2, q); e = z_rename(p, q); if (e == -22) { e = copy(p, q); if (!e) e = z_unlink(p); } if (e) err(p, e); }
    else if (!strcmp(c, "rm") && a1) { path_of(a1, p); e = z_unlink(p); if (e) err(p, e); }
    else if (!strcmp(c, "mkdir") && a1) { path_of(a1, p); e = z_mkdir(p); if (e) err(p, e); }
    else if (!strcmp(c, "touch") && a1) { path_of(a1, p); int f = z_open(p, O_WRITE | O_CREATE); if (f < 0) err(p, f); else z_close(f); }
    else if (!strcmp(c, "echo")) { int to = -1; for (int i = 1; i < n; i++) if (!strcmp(v[i], ">")) to = i; char b[512] = ""; for (int i = 1; i < (to > 0 ? to : n); i++) { strcat(b, v[i]); if (i + 1 < (to > 0 ? to : n)) strcat(b, " "); } strcat(b, "\n"); if (to > 0 && to + 1 < n) { path_of(v[to + 1], p); e = z_writefile(p, b, strlen(b)); if (e) err(p, e); } else puts2(b); }
    else if (!strcmp(c, "df")) { zmount m[24]; int k = z_mounts(m, 24); say("%-10s %-8s %-12s %10s %10s\n", _("volumen"), _("tipo"), _("etiqueta"), "total", _("libre")); for (int i = 0; i < k; i++) { char a[16], b[16]; human(m[i].total, a); human(m[i].free_b, b); say("/%-9s %-8s %-12s %10s %10s\n", m[i].name, m[i].fstype, m[i].label, a, b); } }
    else if (!strcmp(c, "discos")) { zdisk d[16]; int k = z_disks(d, 16); for (int i = 0; i < k; i++) { char a[16]; human(d[i].sectors * d[i].ssize, a); say("%-8s %-10s %s%s\n", d[i].name, a, d[i].model, d[i].removable ? _(" (extraible)") : ""); } }
    else if (!strcmp(c, "ps")) { zproc t[64]; int k = z_procs(t, 64); say("%5s %-20s %5s %9s %8s\n", "PID", _("nombre"), _("hilos"), _("memoria"), "cpu(ms)"); for (int i = 0; i < k; i++) say("%5d %-20s %5d %6lu KB %8lu\n", t[i].pid, t[i].name, t[i].threads, t[i].mem_kb, t[i].cpu); }
    else if (!strcmp(c, "kill") && a1) { e = z_kill(atoi(a1)); if (e) err(a1, e); }
    else if (!strcmp(c, "mem") || !strcmp(c, "info")) { zsys s; z_sysinfo(&s); say(_("CPU: %s\nMemoria: %lu MB libres de %lu MB\nPantalla: %ux%u\nArranque: %s\nFuncionando: %lu s\n"), s.cpu, s.mem_free >> 20, s.mem_total >> 20, s.scr_w, s.scr_h, s.boot_mode == 2 ? "UEFI" : "BIOS", s.uptime_ms / 1000); char m[80]; snprintf(m, 80, "[prueba] mem libre %lu KB de %lu MB", s.mem_free >> 10, s.mem_total >> 20); z_log(m); }
    else if (!strcmp(c, "lspci")) { zpci d[64]; int k = z_pci(d, 64); for (int i = 0; i < k; i++) say("%02x:%02x.%d  %04x:%04x  clase %02x.%02x.%02x\n", d[i].bus, d[i].dev, d[i].fn, d[i].vendor, d[i].device, d[i].cls, d[i].sub, d[i].pif); }
    else if (!strcmp(c, "dmesg")) { char *b = malloc(65536); int k = z_dmesg(b, 65535); b[k] = 0; puts2(b); free(b); }
    else if (!strcmp(c, "fecha")) { u32 t[6]; z_time(t); say("%02u/%02u/%u %02u:%02u:%02u\n", t[3], t[4], t[5], t[2], t[1], t[0]); }
    else if (!strcmp(c, "teclado") && a1) z_setlayout(!strcmp(a1, "es"));
    else if (!strcmp(c, "abrir") && a1) { snprintf(p, 256, "/rd/bin/%s", a1); e = z_spawn(p, a2 ? a2 : ""); if (e < 0) err(a1, e); }
    else if (!strcmp(c, "red")) { znet i; if (z_netinfo(&i)) say(_("no hay tarjeta de red compatible\n")); else { char a[16], m[16], g[16], d[16]; z_fmt_ip(i.ip, a); z_fmt_ip(i.mask, m); z_fmt_ip(i.gw, g); z_fmt_ip(i.dns, d); say(_("%s (%s) %02x:%02x:%02x:%02x:%02x:%02x enlace=%s\nIP %s  mascara %s\npuerta %s  dns %s\nrecibidos %lu paq.  enviados %lu paq.\n"), i.name, i.kind, i.mac[0], i.mac[1], i.mac[2], i.mac[3], i.mac[4], i.mac[5], i.up ? _("si") : _("no"), a, m, g, d, i.rxp, i.txp); char l[96]; snprintf(l, 96, "[prueba] red: %s ip=%s", i.kind, a); z_log(l); } }
    else if (!strcmp(c, "ping") && a1) ping(a1);
    else if (!strcmp(c, "dns") && a1) { u32 ip; e = z_dns(a1, &ip); if (e) err(a1, e); else { z_fmt_ip(ip, p); say("%s\n", p); } }
    else if (!strcmp(c, "wget") && a1) { char *b; long l; int st = 0; e = z_http_get(a1, &b, &l, 0, 0, &st); if (e) say("%s: %s\n", a1, z_http_err(e)); else { say("HTTP %d, %ld bytes\n", st, l); long s = 0; for (long i = 0; i < l; i++) s = (s * 31 + (u8)b[i]) & 0xFFFFFFF; char m[120]; snprintf(m, 120, "[prueba] http %d %ld bytes suma=%ld", st, l, s); z_log(m); if (a2) { path_of(a2, p); e = z_writefile(p, b, l); if (e) err(p, e); } free(b); } }
    else if (!strcmp(c, "servidor") && a1) { int s = z_socket(1); z_bind(s, atoi(a1)); say(_("escuchando en el puerto %s (20 s)\n"), a1); flush2(); z_log("[prueba] servidor listo"); int k = z_accept(s, 20000); if (k >= 0) { char b[512]; long r = z_recv(k, b, 511, 3000); static const char re[] = "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\nHola desde ZunzunOS\n"; z_send(k, re, sizeof re - 1); z_sleep(200); z_close(k); say(_("cliente atendido (%ld bytes)\n"), r); z_log("[prueba] servidor atendio"); } z_close(s); }
    else if (!strcmp(c, "tono")) { char m[64]; z_audio_info(m, 64); say(_("sonido: %s\n"), m); tone(a1 ? atoi(a1) : 440, 1500); z_log("[prueba] tono enviado"); }
    else if (!strcmp(c, "sonido")) { char m[64]; z_audio_info(m, 64); say("%s\n", m); }
    else if (!strcmp(c, "prueba-fs") && a1) { path_of(a1, p); test_fs(p); }
    else if (!strcmp(c, "prueba-mando")) { say("mando simulado en 7 s\n"); draw(); z_sleep(7000); z_joy_inject(0, 0, 1); z_sleep(200); z_joy_inject(0, 0, 0); for (int b = 0; b < 8; b++) { z_sleep(200); z_joy_inject(1u << b, 0, 0); z_sleep(200); z_joy_inject(0, 0, 0); } }
    else if (!strcmp(c, "prueba-lee") && a1) { path_of(a1, p); test_ro(p); }
    else if (!strcmp(c, "brillo")) { if (a1) z_bright_set(atoi(a1)); int b = z_bright_get(); say(_("brillo %d%% (%s)\n"), b & 255, b & 256 ? "hardware" : "software"); char m[64]; snprintf(m, 64, "[prueba] brillo %d", b & 255); z_log(m); }
    else if (!strcmp(c, "desbloquear") && a1) { e = z_set_ro(a1[0] == '/' ? a1 + 1 : a1, 0); if (e) err(a1, e); else say(_("%s admite escritura\n"), a1); }
    else if (!strcmp(c, "fallo")) { z_log("[prueba] provocando fallo"); *(volatile int *)8 = 1; }
    else if (!strcmp(c, "tarea")) { for (int i = 0; i < 4; i++) { z_spawn("/rd/bin/sysmon", ""); } say(_("abiertos\n")); }
    else if (!strcmp(c, "salir") || !strcmp(c, "exit")) z_exit(0);
    else if (!strcmp(c, "apagar")) z_poweroff();
    else if (!strcmp(c, "reiniciar")) z_reboot();
    else { snprintf(p, 256, "/rd/bin/%s", c); zdirent d; if (!z_stat(p, &d)) { e = z_spawn(p, a1 ? a1 : ""); if (e < 0) err(c, e); } else { col = 2; say(_("%s: orden desconocida (prueba con ayuda)\n"), c); col = 0; } }
}
static int ps_len;
static void prompt(void) { col = 1; say("%s", cwd); col = 3; say(" $ "); col = 0; ps_len = cx; }
static void redraw_line(void) { for (int i = ps_len; i < COLS; i++) L(cy)[i] = 0; cx = ps_len; const char *s = line; int c, k = 0, pos = ps_len; while ((c = utf8_next(&s))) { if (ps_len + k < COLS) L(cy)[ps_len + k] = c; k++; if (s - line <= lc) pos = ps_len + k; } cx = pos; }
int main(const char *a) {
    zsys sy; z_sysinfo(&sy); cols = MIN(cols, ((int)sy.scr_w - 60) / CW_); vis = MIN(vis, ((int)sy.scr_h - 110) / CH_); W = zw_new("Terminal", -1, 0, cols * CW_ + 8, vis * CH_ + 6, WF_DECOR | WF_RESIZE); scr = calloc(COLS * ROWS, 2); attr = calloc(COLS, ROWS);
    col = 1; say(_("ZunzunOS - terminal. Escribe ayuda para ver las ordenes.\n")); col = 0; prompt(); draw(); zevent e;
    for (;;) {
        if (!z_event(&e, -1)) continue;
        if (e.type == EV_REDRAW) { draw(); continue; }
        if (e.type == EV_DOWN && (e.btn & 2)) { static const char *it[] = {"Pegar", "Copiar la línea", "Imprimir", "Copiar"}; int sx, sy; zw_screen(W, e.x, e.y, &sx, &sy); int r = ui_menu(sx, sy, it, tsel_has() ? 4 : 3); if (r == 1) { z_clip_set(line, strlen(line)); continue; } if (r == 2) { print_(); draw(); continue; } if (r == 3) { tsel_copy(); continue; } if (r != 0) continue; e.type = EV_KEY; e.ch = 22; e.key = 0x19; e.mods = M_CTRL; }
        if (e.type == EV_CLOSE) z_exit(0);
        if (e.type == EV_RESIZE) { zw_resized(W, e.x, e.y); cols = MIN((W->w - 8) / CW_, COLS); vis = (W->h - 6) / CH_; { char m[50]; snprintf(m, 50, "[prueba] term %dx%d", W->w, W->h); z_log(m); } draw(); }
        if (e.type == EV_WHEEL) { view = MAX(0, MIN(view - (int)e.key * 3, MAX(0, cy - top - vis + 1))); draw(); }
        if (e.type == EV_DOWN && !(e.btn & 2)) { cell_at(e.x, e.y, &tsa_y, &tsa_x); tsb_y = tsa_y; tsb_x = tsa_x; tdrag = 1; draw(); continue; }
        if (e.type == EV_MOVE && tdrag) { cell_at(e.x, e.y, &tsb_y, &tsb_x); draw(); continue; }
        if (e.type == EV_UP && tdrag) { tdrag = 0; if (!tsel_has()) tsa_y = -1; draw(); continue; }
        if (e.type != EV_KEY) continue;
        if (e.ch == 3 && tsel_has() && !ll) { tsel_copy(); tsa_y = -1; draw(); continue; }
        if (e.ch == 16) { print_(); draw(); continue; }
        view = 0;
        if (e.ch == '\n') { cx = ps_len; redraw_line(); putc2('\n'); if (ll) { strcpy(hist[nh % 32], line); nh++; } hi = nh; char c[512]; strcpy(c, line); line[0] = 0; ll = lc = 0; draw(); run(c); if (cx) putc2('\n'); prompt(); }
        else if (e.key == K_UP) { if (hi > 0 && hi > nh - 32) { hi--; strcpy(line, hist[hi % 32]); ll = lc = strlen(line); redraw_line(); } }
        else if (e.key == K_DOWN) { if (hi < nh) { hi++; if (hi == nh) line[0] = 0; else strcpy(line, hist[hi % 32]); ll = lc = strlen(line); redraw_line(); } }
        else if (e.key == K_PGUP) { view = MIN(vis, MAX(0, cy - top - vis + 1)); }
        else if (ui_input_key(line, 500, &lc, &e)) { ll = strlen(line); redraw_line(); }
        draw();
    }
}
