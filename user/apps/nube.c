/* Nube: archivos de pCloud (API HTTP JSON de pCloud, siempre por HTTPS con certificados verificados).
   Se entra una vez con el correo y la contrasena de pCloud; pCloud devuelve una llave de sesion ("auth") y en la
   memoria se guarda solo esa llave, nunca la contrasena (/sys/ZUNZUN/NUBE.TXT). Las cuentas europeas usan
   eapi.pcloud.com y las de EE. UU. api.pcloud.com: al entrar se prueba primero la europea.
   Navegar por las carpetas, abrir (se descarga a /rd/tmp/nube), guardar en Documentos/Nube, subir, nueva carpeta,
   cambiar nombre y borrar. Para las pruebas, "servidor=" en NUBE.TXT fija otro servidor (tools/fake_pcloud.py). */
#include "z.h"
#define CFG "/sys/ZUNZUN/NUBE.TXT"
#define TB 40
#define SB 22
#define MAXE 2000
static zwin *W; static char msg[300], srv[128], auth[80], email[128], fixed_srv[128];
typedef struct { char name[200]; long long id; long size, mtime; int dir; } ent_t;
static ent_t *E; static int ne, top, sel = -1;
static struct { long long id; char name[120]; } ST[32]; static int nst;   /* carpetas abiertas, desde la raiz */
static long quota, used; static int busy;
/* formulario de entrada */
static char f_mail[128], f_pass[128]; static int f_foc, f_cur;

/* ---------- utilidades ---------- */
static int isalnum(int c) { return isalpha(c) || isdigit(c); }
static void strncat0(char *d, const char *s, int cap) { int l = strlen(d); if (l < cap - 1) strncpy0(d + l, s, cap - l); }
static void kv(const char *t, const char *k, char *o, int cap) { int l = strlen(k); o[0] = 0; for (const char *p = t; p && *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : 0) if (!strncmp(p, k, l) && p[l] == '=') { int i = 0; p += l + 1; while (p[i] && p[i] != '\n' && i < cap - 1) { o[i] = p[i]; i++; } o[i] = 0; return; } }
static void cfg_load(void) { char *t = z_readfile(CFG, 0); kv(t, "servidor", srv, 128); kv(t, "llave", auth, 80); kv(t, "correo", email, 128); kv(t, "servidor_fijo", fixed_srv, 128); free(t); }
static int cfg_save(void) { sb_t b = {0}; sb_f(&b, "servidor=%s\nllave=%s\ncorreo=%s\n", srv, auth, email); if (fixed_srv[0]) sb_f(&b, "servidor_fijo=%s\n", fixed_srv); z_mkdir("/sys/ZUNZUN"); int e = z_writefile(CFG, b.p, b.n); free(b.p); z_sync(); return e; }
static void enc(sb_t *b, const char *s) { static const char hx[] = "0123456789ABCDEF"; for (const u8 *p = (const u8 *)s; *p; p++) { if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~') sb_add(b, (const char *)p, 1); else { char t[3] = {'%', hx[*p >> 4], hx[*p & 15]}; sb_add(b, t, 3); } } }
static long long jint(const char *v, const char *e) { if (!v) return 0; while (v < e && (*v == ' ' || *v == '"')) v++; int neg = v < e && *v == '-'; if (neg) v++; long long n = 0; while (v < e && *v >= '0' && *v <= '9') n = n * 10 + *v++ - '0'; return neg ? -n : n; }
static void human(long n, char *o, int cap) { if (n < 1024) snprintf(o, cap, "%ld B", n); else if (n < 1048576) snprintf(o, cap, "%ld KB", (n + 1023) / 1024); else if (n < 1073741824L) snprintf(o, cap, "%ld.%ld MB", n / 1048576, n % 1048576 * 10 / 1048576); else snprintf(o, cap, "%ld.%ld GB", n / 1073741824L, n % 1073741824L * 10 / 1073741824L); }
void draw(void);
static int pump(void *c) { zevent e; while (z_event(&e, 0)) { if (e.type == EV_REDRAW) draw(); if (e.type == EV_CLOSE && e.win == (u32)W->id) return 1; } return 0; }

/* ---------- llamadas a la API: GET {srv}/metodo?parametros; respuesta JSON con "result" (0 = bien) ---------- */
typedef struct { char *b; long n; long long result; char err[160]; } resp_t;
static void resp_free(resp_t *r) { free(r->b); r->b = 0; }
/* q: parametros ya codificados ("a=1&b=2"), sin la llave; la llave se anade si hay sesion */
static int api(const char *base, const char *method, const char *q, int with_auth, resp_t *r) {
    memset(r, 0, sizeof *r); sb_t u = {0}; sb_f(&u, "%s/%s?", base, method); if (with_auth) { sb_s(&u, "auth="); enc(&u, auth); sb_s(&u, "&"); } sb_s(&u, q ? q : "");
    zhttp_opt o = {0, 0, pump, 0, 60000}; zhttp_info inf; int e = z_http_ex("GET", u.p, 0, 0, 0, &o, &r->b, &r->n, &inf); free(u.p);
    if (e) { snprintf(r->err, 160, "%s", e == -125 ? tr("Cancelado") : z_http_err(e)); r->result = -1; return -1; }
    const char *end = r->b + r->n, *v = zj_get(r->b, end, "result"); if (!v) { snprintf(r->err, 160, "%s (HTTP %d)", tr("Respuesta no válida de pCloud"), inf.status); r->result = -1; return -1; }
    r->result = jint(v, end); if (r->result) { const char *m = zj_get(r->b, end, "error"); char t[120] = ""; if (m) zj_str(m, end, t, 120); snprintf(r->err, 160, "pCloud %lld: %s", r->result, t[0] ? t : tr("error")); return -1; } return 0; }
static int call(const char *method, const char *q, resp_t *r) { int e = api(srv, method, q, 1, r);
    if (e && (r->result == 1000 || r->result == 2000)) { auth[0] = 0; cfg_save(); strcpy(msg, tr("La sesión de pCloud ha caducado: vuelve a entrar.")); } else if (e) snprintf(msg, 300, "%s", r->err); return e; }

/* ---------- entrar y salir ---------- */
static void after_login(resp_t *r) { const char *end = r->b + r->n, *av = zj_get(r->b, end, "auth"); if (av) zj_str(av, end, auth, 80); quota = (long)jint(zj_get(r->b, end, "quota"), end); used = (long)jint(zj_get(r->b, end, "usedquota"), end); }
static void list_dir(void);
static void login(void) {
    if (!f_mail[0] || !f_pass[0]) { strcpy(msg, tr("Escribe el correo y la contraseña de pCloud")); return; }
    sb_t q = {0}; sb_s(&q, "getauth=1&logout=1&timeformat=timestamp&username="); enc(&q, f_mail); sb_s(&q, "&password="); enc(&q, f_pass);
    const char *hosts[2] = {"https://eapi.pcloud.com", "https://api.pcloud.com"}; int nh = 2; if (fixed_srv[0]) { hosts[0] = fixed_srv; nh = 1; }
    resp_t r = {0}; char first[160] = ""; int ok = 0;
    for (int i = 0; i < nh && !ok; i++) { snprintf(msg, 300, "%s %s...", tr("Conectando con"), hosts[i]); draw(); busy = 1;
        int e = api(hosts[i], "userinfo", q.p, 0, &r); busy = 0;
        if (!e) { strncpy0(srv, hosts[i], 128); ok = 1; break; }
        if (r.result == 2297) {   /* verificacion en dos pasos: pCloud da un "token" y pide el codigo */
            char tok[200] = "", code[16] = ""; const char *end = r.b + r.n; { const char *tv = zj_get(r.b, end, "token"); if (tv) zj_str(tv, end, tok, 200); }
            if (tok[0] && ui_prompt("pCloud", "Código de verificación en dos pasos:", code, 12) && code[0]) { sb_t t = {0}; sb_s(&t, "getauth=1&timeformat=timestamp&token="); enc(&t, tok); sb_s(&t, "&code="); enc(&t, code);
                resp_t r2; if (!api(hosts[i], "tfa_login", t.p, 0, &r2)) { resp_free(&r); r = r2; strncpy0(srv, hosts[i], 128); ok = 1; } else { snprintf(first, 160, "%s", r2.err); resp_free(&r2); } free(t.p); }
            else snprintf(first, 160, "%s", tr("Falta el código de verificación")); break; }
        if (!first[0] || r.result == 2000) snprintf(first, 160, "%s", r.result == 2000 ? tr("pCloud no aceptó el correo o la contraseña") : r.err); resp_free(&r); }
    free(q.p); memset(f_pass, 0, sizeof f_pass); f_cur = 0;
    if (!ok) { snprintf(msg, 300, "%s", first); char lg[200]; snprintf(lg, 200, "[prueba] nube entrar: fallo (%s)", first); z_log(lg); return; }
    after_login(&r); resp_free(&r); strncpy0(email, f_mail, 128); int e = cfg_save(); if (e) snprintf(msg, 300, "%s: %s", tr("No se pudo guardar la sesión"), z_errstr(e));
    char lg[200]; snprintf(lg, 200, "[prueba] nube entrar: bien en %s", srv); z_log(lg); nst = 1; ST[0].id = 0; strcpy(ST[0].name, "pCloud"); list_dir(); }
static void logout(void) { if (!ui_confirm("Salir de pCloud", "¿Cerrar la sesión de pCloud en este equipo? Tus archivos siguen en pCloud.")) return; resp_t r; api(srv, "logout", "", 1, &r); resp_free(&r);
    auth[0] = 0; ne = 0; nst = 0; cfg_save(); strcpy(msg, tr("Sesión de pCloud cerrada")); z_log("[prueba] nube salir"); }

/* ---------- carpetas ---------- */
static int cmp(const void *a, const void *b) { const ent_t *x = a, *y = b; if (x->dir != y->dir) return y->dir - x->dir; return strcasecmp(x->name, y->name); }
static void list_dir(void) { if (!nst) { nst = 1; ST[0].id = 0; strcpy(ST[0].name, "pCloud"); } char q[64]; snprintf(q, 64, "folderid=%lld&timeformat=timestamp", ST[nst - 1].id);
    strcpy(msg, tr("Leyendo la carpeta...")); draw(); busy = 1; resp_t r; int e = call("listfolder", q, &r); busy = 0; if (e) { resp_free(&r); return; }
    ne = 0; top = 0; sel = -1; const char *end = r.b + r.n, *md = zj_get(r.b, end, "metadata"), *ct = md ? zj_get(md, end, "contents") : 0;
    for (int i = 0; ct && ne < MAXE; i++) { const char *it = zj_item(ct, end, i); if (!it) break; ent_t *x = &E[ne]; memset(x, 0, sizeof *x); const char *nv = zj_get(it, end, "name"); if (nv) zj_str(nv, end, x->name, 200); x->dir = zj_true(zj_get(it, end, "isfolder"), end);
        x->id = jint(zj_get(it, end, x->dir ? "folderid" : "fileid"), end); x->size = (long)jint(zj_get(it, end, "size"), end); x->mtime = (long)jint(zj_get(it, end, "modified"), end); if (x->name[0]) ne++; }
    resp_free(&r); qsort(E, ne, sizeof *E, cmp); snprintf(msg, 300, tr("%d elementos"), ne);
    char lg[300]; snprintf(lg, 300, "[prueba] nube carpeta %s: %d", ST[nst - 1].name, ne); z_log(lg); }

/* ---------- bajar y subir ---------- */
typedef struct { int fd; long got, tot; } dl_t;
static int dl_cb(const char *d, long n, void *c) { dl_t *k = c; if (z_write(k->fd, d, n) != n) return 1; k->got += n; static u64 t; if (z_ms() - t > 300) { t = z_ms(); char a[24], b[24]; human(k->got, a, 24); human(k->tot, b, 24); snprintf(msg, 300, "%s %s / %s", tr("Descargando"), a, b); draw(); } return 0; }
static int download(ent_t *x, const char *dst) { char q[64]; snprintf(q, 64, "fileid=%lld", x->id); resp_t r; if (call("getfilelink", q, &r)) { resp_free(&r); return -1; }
    const char *end = r.b + r.n, *hs = zj_get(r.b, end, "hosts"); char host[128] = "", path[1024] = ""; const char *h0 = hs ? zj_item(hs, end, 0) : 0, *pv = zj_get(r.b, end, "path"); if (h0) zj_str(h0, end, host, 128); if (pv) zj_str(pv, end, path, 1024); resp_free(&r);
    if (!host[0] || !path[0]) { strcpy(msg, tr("pCloud no dio el enlace de descarga")); return -1; }
    char url[1300]; snprintf(url, sizeof url, "%s://%s%s", strncmp(srv, "http://", 7) ? "https" : "http", host, path);
    int fd = z_open(dst, O_WRITE | O_CREATE | O_TRUNC); if (fd < 0) { snprintf(msg, 300, "%s: %s", tr("No se pudo guardar"), z_errstr(fd)); return -1; }
    dl_t k = {fd, 0, x->size}; zhttp_opt o = {0, dl_cb, pump, &k, 60000}; zhttp_info inf; char *rest = 0; long rl = 0; busy = 1; int e = z_http_ex("GET", url, 0, 0, 0, &o, &rest, &rl, &inf); busy = 0; z_close(fd); z_sync();
    if (!e && (inf.status < 200 || inf.status > 299)) e = -5; free(rest);
    if (e || k.got != x->size) { z_unlink(dst); z_sync(); snprintf(msg, 300, "%s: %s", tr("No se pudo descargar"), e ? z_http_err(e) : tr("llegó incompleto")); return -1; }
    char lg[400]; snprintf(lg, 400, "[prueba] nube descargado %s: %ld bytes", dst, k.got); z_log(lg); return 0; }
static void save_doc(ent_t *x) { char d[200], p[400]; z_docdir(d); strcat(d, "/Nube"); z_mkdir(d); snprintf(p, 400, "%s/%s", d, x->name);
    zdirent st; for (int n = 2; !z_stat(p, &st) && n < 100; n++) { const char *dot = strrchr(x->name, '.'); int bl = dot ? (int)(dot - x->name) : (int)strlen(x->name); snprintf(p, 400, "%s/%.*s (%d)%s", d, bl, x->name, n, dot ? dot : ""); }
    if (!download(x, p)) snprintf(msg, 300, "%s %s", tr("Guardado en"), p); }
static void open_file(ent_t *x) { char p[400]; z_mkdir("/rd/tmp/nube"); snprintf(p, 400, "/rd/tmp/nube/%s", x->name); if (download(x, p)) return; if (z_open_path(p) < 0) strcpy(msg, tr("No hay ningún programa para abrir ese archivo")); else snprintf(msg, 300, "%s %s", tr("Abierto"), x->name); }
static void upload(void) { char p[260]; if (!z_pick_file(tr("Subir a pCloud"), 0, p, 260)) return; long n; char *d = z_readfile(p, &n); if (!d) { strcpy(msg, tr("No se pudo leer")); return; }
    const char *nm = strrchr(p, '/'); nm = nm ? nm + 1 : p; sb_t u = {0}; sb_f(&u, "%s/uploadfile?auth=", srv); enc(&u, auth); sb_f(&u, "&folderid=%lld&nopartial=1&renameifexists=1&filename=", ST[nst - 1].id); enc(&u, nm);
    char a[24]; human(n, a, 24); snprintf(msg, 300, "%s %s (%s)...", tr("Subiendo"), nm, a); draw();
    zhttp_opt o = {0, 0, pump, 0, 120000}; zhttp_info inf; char *b = 0; long bl = 0; busy = 1; int e = z_http_ex("PUT", u.p, d, n, "application/octet-stream", &o, &b, &bl, &inf); busy = 0; free(u.p); free(d);
    long long res = -1; if (!e && b) { const char *v = zj_get(b, b + bl, "result"); if (v) res = jint(v, b + bl); }
    if (e || res) { snprintf(msg, 300, "%s: %s", tr("No se pudo subir"), e ? z_http_err(e) : b ? b : "?"); free(b); return; } free(b);
    char lg[400]; snprintf(lg, 400, "[prueba] nube subido %s: %ld bytes", nm, n); z_log(lg); list_dir(); snprintf(msg, 300, "%s %s", tr("Subido"), nm); }
static void mkdir_(void) { char n[120] = ""; if (!ui_prompt("Nueva carpeta", "Nombre:", n, 110) || !n[0]) return; sb_t q = {0}; sb_f(&q, "folderid=%lld&name=", ST[nst - 1].id); enc(&q, n); resp_t r; int e = call("createfolderifnotexists", q.p, &r); free(q.p); resp_free(&r); if (!e) { char lg[200]; snprintf(lg, 200, "[prueba] nube carpeta creada %s", n); z_log(lg); list_dir(); } }
static void rename_(ent_t *x) { char n[200]; strcpy(n, x->name); if (!ui_prompt("Cambiar nombre", "Nombre nuevo:", n, 190) || !n[0] || !strcmp(n, x->name)) return; sb_t q = {0}; sb_f(&q, "%s=%lld&toname=", x->dir ? "folderid" : "fileid", x->id); enc(&q, n);
    resp_t r; int e = call(x->dir ? "renamefolder" : "renamefile", q.p, &r); free(q.p); resp_free(&r); if (!e) list_dir(); }
static void delete_(ent_t *x) { char t[300]; snprintf(t, 300, tr(x->dir ? "¿Borrar la carpeta %s y todo lo que tiene? pCloud la guarda un tiempo en su papelera." : "¿Borrar %s? pCloud lo guarda un tiempo en su papelera."), x->name); if (!ui_confirm("Borrar", t)) return;
    char q[64]; snprintf(q, 64, "%s=%lld", x->dir ? "folderid" : "fileid", x->id); resp_t r; int e = call(x->dir ? "deletefolderrecursive" : "deletefile", q, &r); resp_free(&r); if (!e) { char lg[300]; snprintf(lg, 300, "[prueba] nube borrado %s", x->name); z_log(lg); list_dir(); } }

/* ---------- ventana ---------- */
static const char *BT[] = {"Subir de nivel", "Actualizar", "Subir un archivo...", "Nueva carpeta", "Salir de pCloud"};
static int bw(int i) { return g_textw(tr(BT[i])) + 16; }
static int bx(int i) { int x = 8; for (int k = 0; k < i; k++) x += bw(k) + 6; return x; }
#define RH 24
static int list_y(void) { return TB + 28; }
static void draw_login(void) { int y = TB + 30, X = 30; g_text2(W, X, y, "pCloud", UI_ACC, 2); y += 44;
    g_text(W, X, y, tr("Entra con tu cuenta de pCloud. En esta memoria solo se guarda la llave de la sesión, nunca la contraseña."), UI_MUTED); y += 34;
    g_text(W, X, y + 6, tr("Correo"), UI_TEXT); ui_input(W, X + 130, y, 380, 26, f_mail, f_foc == 0 ? f_cur : 0, f_foc == 0); y += 38;
    char mk[130]; int n = 0; for (const char *q = f_pass; *q && n < 128; q++) if (((u8)*q & 0xC0) != 0x80) mk[n++] = '*'; mk[n] = 0;
    g_text(W, X, y + 6, tr("Contraseña"), UI_TEXT); ui_input(W, X + 130, y, 380, 26, mk, f_foc == 1 ? n : 0, f_foc == 1); y += 44; ui_button(W, X + 130, y, 120, 30, "Entrar", 2);
    static int lg; if (!lg) { lg = 1; int a, b, c, d, e2, f; zw_screen(W, X + 300, TB + 30 + 44 + 34 + 13, &a, &b); zw_screen(W, X + 300, TB + 30 + 44 + 34 + 38 + 13, &c, &d); zw_screen(W, X + 190, TB + 30 + 44 + 34 + 38 + 44 + 15, &e2, &f); char m[200]; snprintf(m, 200, "[prueba] nube formulario (correo en %d,%d) (clave en %d,%d) (entrar en %d,%d)", a, b, c, d, e2, f); z_log(m); } }
void draw(void) { { static char last[300]; if (strcmp(msg, last) && strncmp(msg, tr("Descargando"), strlen(tr("Descargando")))) { strcpy(last, msg); if (msg[0]) { char lg[340]; snprintf(lg, 340, "[prueba] nube estado: %s", msg); z_log(lg); } } }
    g_clear(W, UI_WHITE); g_rect(W, 0, 0, W->w, TB, UI_PANEL);
    if (!auth[0]) draw_login();
    else { for (int i = 0; i < 5; i++) ui_button(W, bx(i), 7, bw(i), 26, BT[i], 0);
        char p[600] = ""; for (int i = 0; i < nst; i++) { if (i) strncat0(p, " / ", 600); strncat0(p, ST[i].name, 600); } int y = TB + 6; g_textb(W, 12, y, p, UI_TEXT);
        if (quota) { char a[24], b[24], t[80]; human(used, a, 24); human(quota, b, 24); snprintf(t, 80, "%s %s / %s", tr("Ocupado"), a, b); g_text(W, W->w - g_textw(t) - 12, y, t, UI_MUTED); }
        y = list_y(); g_rect(W, 0, y - 2, W->w, 1, UI_LINE); int rows = (W->h - SB - y) / RH; top = MAX(0, MIN(top, ne - rows));
        for (int i = top; i < ne && i < top + rows; i++) { ent_t *x = &E[i]; int Y = y + (i - top) * RH; if (i == sel) g_rect(W, 0, Y, W->w, RH, UI_ACC2);
            if (x->dir) { g_round(W, 14, Y + 6, 18, 13, 2, 0xD97706); g_rect(W, 14, Y + 4, 8, 4, 0xD97706); } else { g_rect(W, 16, Y + 3, 13, 17, 0xF8FAFC); g_frame(W, 16, Y + 3, 13, 17, 0x94A3B8); }
            zw_clip(W, 40, Y, W->w - 300, RH); g_text(W, 40, Y + 4, x->name, x->dir ? UI_ACC : UI_TEXT); zw_noclip(W);
            if (!x->dir) { char a[24]; human(x->size, a, 24); g_text(W, W->w - 250, Y + 4, a, UI_MUTED); }
            if (x->mtime) { char d[40]; long s = x->mtime; long days = s / 86400; long y0 = 1970; for (;;) { int ly = (y0 % 4 == 0 && y0 % 100) || y0 % 400 == 0; if (days < 365 + ly) break; days -= 365 + ly; y0++; }
                static const int md[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}; int m = 0; for (; m < 12; m++) { int l = md[m] + (m == 1 && ((y0 % 4 == 0 && y0 % 100) || y0 % 400 == 0)); if (days < l) break; days -= l; }
                snprintf(d, 40, "%02ld/%02d/%04ld", days + 1, m + 1, y0); g_text(W, W->w - 130, Y + 4, d, UI_MUTED); } }
        if (!ne) g_text(W, 40, y + 6, tr("Carpeta vacía."), UI_MUTED); }
    g_rect(W, 0, W->h - SB, W->w, SB, UI_PANEL); zw_clip(W, 0, W->h - SB, W->w, SB); g_text(W, 8, W->h - SB + 3, msg, UI_TEXT); zw_noclip(W); zw_present(W); }
static void item_menu(int i, zevent *e) { ent_t *x = &E[i]; int sx, sy; zw_screen(W, e->x, e->y, &sx, &sy);
    if (x->dir) { static const char *it[] = {"Abrir", "Cambiar nombre", "Borrar"}; int r = ui_menu(sx, sy, it, 3); if (r == 0) { if (nst < 32) { ST[nst].id = x->id; strncpy0(ST[nst].name, x->name, 120); nst++; } list_dir(); } else if (r == 1) rename_(x); else if (r == 2) delete_(x); return; }
    static const char *it[] = {"Abrir", "Guardar en Documentos", "Cambiar nombre", "Borrar"}; int r = ui_menu(sx, sy, it, 4); if (r == 0) open_file(x); else if (r == 1) save_doc(x); else if (r == 2) rename_(x); else if (r == 3) delete_(x); }
int main(const char *a) { if ((z_sess(0, 0, 0) & 7) == 2) { ui_confirm("Nube", "La nube solo se puede usar en la sesión del administrador."); return 0; }
    zsys s; z_sysinfo(&s); W = zw_new("Nube", -1, 0, MIN(900, (int)s.scr_w - 40), MIN(620, (int)s.scr_h - 80), WF_DECOR | WF_RESIZE); if (!W) return 1; E = malloc(MAXE * sizeof *E);
    cfg_load(); z_log("[prueba] nube abierta"); if (auth[0]) { strcpy(f_mail, email); list_dir(); if (auth[0]) { resp_t r; if (!call("userinfo", "", &r)) after_login(&r); resp_free(&r); } } else { strcpy(f_mail, email); f_cur = strlen(f_mail); }
    draw(); { int sx, sy; char lg[300]; int k = 0; for (int i = 0; i < 5; i++) { zw_screen(W, bx(i) + bw(i) / 2, 20, &sx, &sy); k += snprintf(lg + k, 300 - k, "%s%d,%d", i ? " " : "[prueba] nube botones ", sx, sy); } zw_screen(W, 40, list_y() + RH / 2, &sx, &sy); snprintf(lg + k, 300 - k, " (fila0 en %d,%d)", sx, sy); z_log(lg); }
    zevent e; for (;;) { if (!z_event(&e, -1)) continue; if (e.type == EV_REDRAW) { draw(); continue; } if (e.win != (u32)W->id) continue; if (e.type == EV_CLOSE) return 0;
        if (e.type == EV_RESIZE) { zw_resized(W, e.x, e.y); draw(); continue; } if (e.type == EV_DOWN) msg[0] = 0;
        if (!auth[0]) {
            if (e.type == EV_KEY) { if (e.key == K_TAB) { f_foc = !f_foc; f_cur = strlen(f_foc ? f_pass : f_mail); } else if (e.ch == '\n') { if (f_foc == 0) { f_foc = 1; f_cur = strlen(f_pass); } else login(); } else ui_input_key(f_foc ? f_pass : f_mail, 120, &f_cur, &e); }
            else if (e.type == EV_DOWN) { int y = TB + 30 + 44 + 34; if (e.y >= y && e.y < y + 26) { f_foc = 0; ui_input_click(W, &e, f_mail, &f_cur); } else if (e.y >= y + 38 && e.y < y + 64) { f_foc = 1; f_cur = strlen(f_pass); } else if (e.y >= y + 82 && e.y < y + 112 && e.x >= 160 && e.x < 280) login(); }
            draw(); continue; }
        if (e.type == EV_WHEEL) { top -= (int)e.key * 3; draw(); continue; }
        if (e.type == EV_KEY) { if (e.key == K_F5) list_dir(); else if (e.key == K_BS && nst > 1) { nst--; list_dir(); } draw(); continue; }
        if (e.type == EV_DOWN && e.y < TB) { for (int i = 4; i >= 0; i--) if (e.x >= bx(i)) { if (e.x < bx(i) + bw(i)) { if (i == 0 && nst > 1) { nst--; list_dir(); } else if (i == 1) list_dir(); else if (i == 2) upload(); else if (i == 3) mkdir_(); else if (i == 4) logout(); } break; } draw(); continue; }
        if (e.type == EV_DOWN && e.y >= list_y() && e.y < W->h - SB) { int i = top + (e.y - list_y()) / RH; if (i < ne) { sel = i; draw(); if (E[i].dir && !(e.btn & 2) && ui_clicks(&e) >= 2) { if (nst < 32) { ST[nst].id = E[i].id; strncpy0(ST[nst].name, E[i].name, 120); nst++; } list_dir(); } else if (e.btn & 2 || !E[i].dir) item_menu(i, &e); } draw(); continue; } }
}
