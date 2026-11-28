/* Alquimia: comprimir, extraer y convertir archivos en un solo programa. Junta las web apps PRESSA / PRESSA PRO
   (compresor con contrasena AES-256) y ALQUIMIA (conversor multiformato) de tonyfluxa.top, reescritas en C para
   ZunzunOS: todo se hace en el equipo, sin red. El motor esta en user/apps/alquimia/ y se prueba en el Mac o en
   Linux con tools/alquimia_test.sh.
   Argumentos: una ruta (un archivo comprimido se abre para extraer; cualquier otro, para convertir),
   "--comprimir LISTA" o "--convertir LISTA" (LISTA: archivo con una ruta por linea; lo deja Archivos). */
#include "z.h"
#include "zcrypto.h"
#include "alquimia/motor.c"

static zwin *W; static int tab, busy, cancel, quit_after; static char msg[240], msg2[240]; static u32 msgc;
static long p_done, p_total; static char p_what[120]; static u64 p_last;
static const u32 TABC[3] = {0xD97706, 0x0D9488, 0x7C3AED};   /* cobre, verde azulado y violeta: vivos en tema claro y oscuro */
/* zonas pulsables: se apuntan al dibujar */
typedef struct { int x, y, w, h, id; } hot_t; static hot_t HOT[160]; static int nhot;
static void hot(int x, int y, int w, int h, int id) { if (nhot < 160) HOT[nhot++] = (hot_t){x, y, w, h, id}; }
static int hit(int x, int y) { for (int i = nhot - 1; i >= 0; i--) if (ui_in(x, y, HOT[i].x, HOT[i].y, HOT[i].w, HOT[i].h)) return HOT[i].id; return -1; }
static void btn(int x, int y, int w, int h, const char *s, int st, int id) { ui_button(W, x, y, w, h, s, st); hot(x, y, w, h, id); }
/* en pantallas bajas (la de 3,5" de la Raspberry Pi) el contenido se dibuja como si midiera VH y se desplaza con la
   rueda o con los botones de arriba a la derecha; la barra de abajo (mensaje y boton principal) queda fija */
static int VH, scy;
static void pbtn(int x, int y, int w, int h, const char *s, u32 col, int id) { g_round(W, x, y, w, h, 6, col); s = tr(s); g_textb(W, x + (w - g_textw(s)) / 2, y + (h - 16) / 2, s, 0xFFFFFF); hot(x, y, w, h, id); }
static int chip(int x, int y, const char *s, int on, u32 col, int id) { s = tr(s); int w = g_textw(s) + 20; g_round(W, x, y, w, 26, 12, on ? col : UI_LINE); g_round(W, x + 1, y + 1, w - 2, 24, 11, on ? col : UI_WHITE); g_text(W, x + 10, y + 5, s, on ? 0xFFFFFF : UI_TEXT); hot(x, y, w, 26, id); return w + 6; }
static void hum(long b, char *o) { if (b >= 1L << 30) snprintf(o, 24, "%ld.%ld GB", b >> 30, ((b >> 20) & 1023) * 10 / 1024); else if (b >= 1L << 20) snprintf(o, 24, "%ld.%ld MB", b >> 20, ((b >> 10) & 1023) * 10 / 1024); else if (b >= 1024) snprintf(o, 24, "%ld KB", b >> 10); else snprintf(o, 24, "%ld B", b); }
static void say(u32 c, const char *a, const char *b) { msgc = c; strncpy0(msg, a ? a : "", 240); strncpy0(msg2, b ? b : "", 240); }
static void plog(const char *f, const char *a) { char m[400]; snprintf(m, 400, f, a); z_log(m); }
/* carpeta donde se puede escribir: la del archivo si su unidad lo permite; si no, Documentos */
static int dir_rw(const char *p) { if (!strncmp(p, "/rd/bin", 7)) return 0; zmount M[16]; int n = z_mounts(M, 16), best = -1, bl = 0; for (int i = 0; i < n; i++) { int l = strlen(M[i].name); if (!strncmp(p, M[i].name, l) && (p[l] == '/' || !p[l]) && l > bl) { best = i; bl = l; } } return best >= 0 && !M[best].ro; }
static void parent_of(const char *p, char *o, int cap) { strncpy0(o, p, cap); char *s = strrchr(o, '/'); if (s && s != o) *s = 0; else strcpy(o, "/"); }
static void dest_for(const char *src, char *o, int cap) { char d[400]; parent_of(src, d, 400); if (dir_rw(d)) strncpy0(o, d, cap); else z_docdir(o); }

/* ---------- campos de texto ---------- */
enum { F_NONE, F_CPW, F_CNAME, F_XPW, F_VPW, F_VW };
static char cpw[128], cname[160], xpw[128], vw[8]; static int focus, fcur, showpw;
static cvopt cvo;
static char *fbuf(int f, int *cap) { *cap = f == F_CNAME ? 150 : f == F_VW ? 6 : 120; return f == F_CPW ? cpw : f == F_CNAME ? cname : f == F_XPW ? xpw : f == F_VPW ? cvo.pw : f == F_VW ? vw : 0; }
static void field(int x, int y, int w, int f, int secret) { int cap; char *b = fbuf(f, &cap); if (secret && !showpw) { char m[130]; int k = 0, cur = 0; for (const char *p = b; *p && k < 128;) { if (p - b < fcur) cur++; utf8_next(&p); m[k++] = '*'; } m[k] = 0; ui_input(W, x, y, w, 26, m, focus == f ? cur : 0, focus == f); }
    else ui_input(W, x, y, w, 26, b, focus == f ? fcur : 0, focus == f); hot(x, y, w, 26, 900 + f); }

/* ---------- progreso: lo llama el motor; pinta, atiende la ventana y dice si hay que cancelar ---------- */
static void draw(void);
static int prog(long d, long t, const char *what) { p_done = d; p_total = t; if (what && what[0]) strncpy0(p_what, what, 120); u64 now = z_ms(); if (now - p_last < 90) return cancel; p_last = now; draw();
    zevent e; while (z_event(&e, 0)) { if (e.type == EV_REDRAW) draw(); if (e.win != (u32)W->id) continue; if (e.type == EV_CLOSE) { cancel = 1; quit_after = 1; } else if (e.type == EV_RESIZE) { zw_resized(W, e.x, e.y); draw(); } else if (e.type == EV_DOWN && hit(e.x, e.y) == 99) cancel = 1; else if (e.type == EV_KEY && e.ch == 27) cancel = 1; }
    return cancel; }

/* ================= Comprimir (PRESSA) ================= */
typedef struct { char p[400]; long size; int dir; } citem;
static citem CI[256]; static int nci, csel = -1, ctop, cfmt, clevel = 2, cly; static char cdest[400]; static long c_in, c_out; static int c_done;
static long tree_size(const char *p, int depth) { zdirent d; if (z_stat(p, &d)) return 0; if (!d.is_dir) return d.size; long s = 0; if (depth > 24) return 0; for (int i = 0; !z_readdir(p, i, &d); i++) { char q[600]; snprintf(q, 600, "%s/%s", p, d.name); s += d.is_dir ? tree_size(q, depth + 1) : (long)d.size; } return s; }
static void c_add(const char *p) { if (nci >= 256) return; for (int i = 0; i < nci; i++) if (!strcmp(CI[i].p, p)) return; zdirent d; if (z_stat(p, &d)) return; citem *c = &CI[nci++]; strncpy0(c->p, p, 400); c->dir = d.is_dir; c->size = d.is_dir ? tree_size(p, 0) : (long)d.size;
    if (nci == 1) { dest_for(p, cdest, 400); char bn[160]; strncpy0(bn, base_name(p), 160); char *dot = strrchr(bn, '.'); if (dot && dot != bn && !c->dir) *dot = 0; strncpy0(cname, bn, 150); }
    else if (nci == 2) { u32 t[6]; z_time(t); snprintf(cname, 150, "%s-%u%02u%02u", tr("comprimido"), t[5], t[4], t[3]); } c_done = 0; }
static void c_run(void) {
    if (!nci) { say(0xB91C1C, tr("Añade primero archivos o carpetas."), 0); return; } if (!cname[0]) { say(0xB91C1C, tr("Escribe el nombre del archivo."), 0); return; }
    if (cfmt == AF_GZ && (nci != 1 || CI[0].dir)) { say(0xB91C1C, tr("GZ guarda un solo archivo: elige TAR.GZ o ZIP para varios o para carpetas."), 0); return; }
    if (cpw[0] && cfmt != AF_ZIP) { say(0xB91C1C, tr("La contraseña solo vale para ZIP."), 0); return; }
    if (cpw[0] && strlen(cpw) < 8 && !ui_confirm("Contraseña corta", "La contraseña tiene menos de 8 letras y se adivina pronto. ¿Seguir igualmente?")) return;
    char out[620]; snprintf(out, 620, "%s/%s%s", cdest, cname, AF_EXT[cfmt]); zdirent st; if (!z_stat(out, &st)) { char q[700]; snprintf(q, 700, "%s %s", tr("Ya existe y se sustituirá:"), out); if (!ui_confirm("Comprimir", q)) return; }
    alist L = {0}; for (int i = 0; i < nci; i++) al_path(&L, CI[i].p); busy = 1; cancel = 0; p_done = 0; p_total = L.total; say(UI_MUTED, tr("Comprimiendo..."), 0); draw();
    astat s; u64 t0 = z_ms(); int r = arc_create(cfmt, &L, out, clevel, cpw[0] ? cpw : 0, prog, &s); busy = 0; al_free(&L);
    if (r) { say(0xB91C1C, s.err, 0); plog("[prueba] alquimia comprimir error %s", s.err); return; }
    c_in = s.in; c_out = s.out; c_done = 1; char a[24], b[24], m[240]; hum(s.in, a); hum(s.out, b); snprintf(m, 240, "%s %d %s: %s -> %s (%lu ms)%s", tr("Hecho:"), s.files, tr("archivos"), a, b, (unsigned long)(z_ms() - t0), cpw[0] ? tr(", cifrado AES-256") : "");
    say(0x15803D, m, out); plog("[prueba] alquimia comprimido %s", out); }
static void c_draw(int y0) {
    int w = W->w, lw = w - 32 - 178, lh = MAX(88, VH - y0 - scy - 270), LH = 22; g_textb(W, 16, y0, tr("Qué comprimir"), UI_TEXT); y0 += 22; cly = y0;
    g_rect(W, 16, y0, lw, lh, UI_WHITE); g_frame(W, 16, y0, lw, lh, UI_LINE); hot(16, y0, lw, lh, 50);
    if (!nci) g_text(W, 28, y0 + 10, tr("Añade archivos o carpetas, o elígelos en Archivos con el botón derecho."), UI_MUTED);
    int vis = lh / LH; for (int i = ctop; i < nci && i < ctop + vis; i++) { int y = y0 + (i - ctop) * LH; if (i == csel) g_rect(W, 17, y + 1, lw - 2, LH, UI_ACC2); char a[24]; hum(CI[i].size, a); zw_clip(W, 18, y, lw - 100, LH); g_text(W, 24, y + 3, CI[i].p, CI[i].dir ? TABC[0] : UI_TEXT); zw_noclip(W); g_text(W, 16 + lw - 10 - g_textw(a), y + 3, a, UI_MUTED); }
    int bx = w - 170; btn(bx, y0, 154, 28, "Añadir archivo", 0, 51); btn(bx, y0 + 34, 154, 28, "Añadir carpeta", 0, 52); btn(bx, y0 + 68, 154, 28, "Quitar", 0, 53); btn(bx, y0 + 102, 154, 28, "Vaciar", 0, 54);
    long tot = 0; for (int i = 0; i < nci; i++) tot += CI[i].size; { char a[24], m[80]; hum(tot, a); snprintf(m, 80, "%d %s, %s", nci, tr("elementos"), a); g_text(W, bx, y0 + 136, m, UI_MUTED); }
    int y = y0 + lh + 12, x = 16; g_text(W, x, y + 5, tr("Formato"), UI_MUTED); x += 96; static const char *FN[4] = {"ZIP", "TAR.GZ", "TAR", "GZ"}; for (int i = 0; i < 4; i++) x += chip(x, y, FN[i], cfmt == i, TABC[0], 60 + i);
    static const char *LV[3] = {"Rápida", "Normal", "Máxima"}; int lw2 = g_textw(tr("Compresión")) + 10; for (int i = 0; i < 3; i++) lw2 += g_textw(tr(LV[i])) + 26;
    if (x + 20 + lw2 > w - 180) { y += 32; x = 16; } else x += 20; g_text(W, x, y + 5, tr("Compresión"), UI_MUTED); x = MAX(x + g_textw(tr("Compresión")) + 10, x == 16 ? 112 : 0); for (int i = 0; i < 3; i++) x += chip(x, y, LV[i], clevel == i + 1, TABC[0], 64 + i);
    int fw = MIN(220, w - 112 - 70 - 16); y += 36; g_text(W, 16, y + 5, tr("Contraseña"), cfmt == AF_ZIP ? UI_MUTED : UI_LINE); if (cfmt == AF_ZIP) { field(112, y, fw, F_CPW, 1); btn(118 + fw, y, 54, 26, showpw ? "Ocultar" : "Ver", 0, 67); zw_clip(W, 182 + fw, y, MAX(0, w - 182 - fw - 16), 26); g_text(W, 182 + fw, y + 5, cpw[0] ? tr("ZIP cifrado WinZip AES-256: se abre con 7-Zip, WinRAR y WinZip") : tr("opcional (ZIP cifrado AES-256)"), cpw[0] ? 0x15803D : UI_MUTED); zw_noclip(W); }
    else g_text(W, 112, y + 5, tr("solo para ZIP"), UI_LINE);
    y += 36; g_text(W, 16, y + 5, tr("Guardar en"), UI_MUTED); zw_clip(W, 112, y, w - 112 - 112, 26); g_text(W, 112, y + 5, cdest[0] ? cdest : tr("(elige una carpeta)"), UI_TEXT); zw_noclip(W); btn(w - 106, y, 90, 26, "Cambiar", 0, 68);
    y += 34; g_text(W, 16, y + 5, tr("Nombre"), UI_MUTED); field(112, y, w - 112 - 90, F_CNAME, 0); g_text(W, w - 72, y + 5, AF_EXT[cfmt], UI_TEXT);
    if (c_done && c_in > 0 && lh >= 212) {   /* el manometro de PRESSA: cuanto se ahorra */
        int pct = (int)(100 - c_out * 100 / MAX(1L, c_in)); if (pct < 0) pct = 0; int gx = w - 170, gy = cly + 158, gw = 154; char m[24]; snprintf(m, 24, "%d %%", pct);
        g_round(W, gx, gy, gw, 54, 6, 0x1F2937); g_text2(W, gx + 8, gy + 2, m, 0xFBBF24, 2); zw_clip(W, gx, gy, gw - 4, 54); g_text(W, gx + 8, gy + 34, tr("de espacio ahorrado"), 0xE5E7EB); zw_noclip(W); for (int k = 0; k < 10; k++) g_rect(W, gx + 96 + k * 5, gy + 30 - k * 3, 4, 3 + k * 3, k * 10 < pct ? 0xF59E0B : 0x4B5563); }
}

/* ================= Extraer ================= */
static arch AR; static int ar_ok; static u8 *xsel; static int xtop, xly; static char xdest[400];
static void x_open(const char *p) { if (ar_ok) arch_close(&AR); free(xsel); xsel = 0; xtop = 0; ar_ok = !arch_open(&AR, p); if (!ar_ok) { say(0xB91C1C, AR.err, p); arch_close(&AR); return; }
    xsel = malloc(AR.n + 1); memset(xsel, 1, AR.n + 1); char d[400]; dest_for(p, d, 400); char bn[200]; strncpy0(bn, base_name(p), 200); static const char *E[] = {".tar.gz", ".tgz", ".zip", ".tar", ".gz"}; for (int i = 0; i < 5; i++) if (z_ends(bn, E[i])) { bn[strlen(bn) - strlen(E[i])] = 0; break; }
    if (AR.kind == AF_GZ) strncpy0(xdest, d, 400); else snprintf(xdest, 400, "%s/%s", d, bn); say(UI_MUTED, AR.encrypted ? tr("Archivo cifrado: escribe la contraseña para extraerlo.") : 0, 0); plog("[prueba] alquimia abierto %s", p); }
static void x_run(int test) {
    if (!ar_ok) { say(0xB91C1C, tr("Abre primero un archivo comprimido."), 0); return; } int any = 0; for (int i = 0; i < AR.n; i++) any |= xsel[i]; if (!any) { say(0xB91C1C, tr("No hay nada marcado."), 0); return; }
    busy = 1; cancel = 0; say(UI_MUTED, test ? tr("Comprobando...") : tr("Extrayendo..."), 0); draw(); astat s; int r = arch_extract(&AR, xdest, xpw, xsel, test, prog, &s); busy = 0;
    if (r) { say(0xB91C1C, s.err, 0); plog("[prueba] alquimia extraer error %s", s.err); return; } char a[24], m[240]; hum(s.out, a); snprintf(m, 240, test ? "%s %d %s (%s): %s" : "%s %d %s (%s) %s", test ? tr("Todo bien:") : tr("Extraídos"), s.files, tr("archivos"), a, test ? tr("los CRC coinciden") : tr("en"));
    say(0x15803D, m, test ? 0 : xdest); plog(test ? "[prueba] alquimia comprobado %s" : "[prueba] alquimia extraido %s", test ? AR.path : xdest); }
static void x_draw(int y0) {
    int w = W->w; btn(16, y0, 170, 28, "Abrir comprimido...", 0, 70); if (ar_ok) { zw_clip(W, 196, y0, w - 212, 28); g_text(W, 196, y0 + 6, AR.path, UI_TEXT); zw_noclip(W); } else g_text(W, 196, y0 + 6, tr("ZIP (también cifrados), TAR, TAR.GZ, TGZ y GZ"), UI_MUTED);
    y0 += 36; xly = y0; int lh = MAX(88, VH - y0 - scy - 200), LH = 22, vis = lh / LH;
    g_rect(W, 16, y0, w - 32, lh, UI_WHITE); g_frame(W, 16, y0, w - 32, lh, UI_LINE); hot(16, y0, w - 32, lh, 71);
    if (ar_ok) { long tot = 0; for (int i = 0; i < AR.n; i++) tot += AR.e[i].size;
        for (int i = xtop; i < AR.n && i < xtop + vis; i++) { aent *x = &AR.e[i]; int y = y0 + (i - xtop) * LH; g_frame(W, 24, y + 5, 12, 12, UI_MUTED); if (xsel[i]) g_rect(W, 27, y + 8, 6, 6, TABC[1]);
            zw_clip(W, 42, y, w - 230, LH); g_text(W, 44, y + 3, x->name, x->dir ? TABC[1] : UI_TEXT); zw_noclip(W); char a[24]; if (!x->dir) { hum(x->size, a); g_text(W, w - 150 - g_textw(a), y + 3, a, UI_MUTED); }
            const char *m = x->enc == 2 ? "AES" : x->enc == 1 ? "ZipCrypto" : x->dir ? "" : AR.kind != AF_ZIP ? "" : x->meth == 8 ? "deflate" : x->meth == 0 ? tr("guardado") : "?"; g_text(W, w - 134, y + 3, m, x->enc ? 0xB45309 : UI_MUTED); }
        char a[24], m[200]; hum(tot, a); static const char *KN[] = {"ZIP", "TAR.GZ", "TAR", "GZ"}; snprintf(m, 200, "%s · %d %s · %s%s", KN[AR.kind], AR.n, tr("elementos"), a, AR.encrypted ? tr(" · cifrado") : ""); g_text(W, 16, y0 + lh + 6, m, UI_MUTED);
        btn(w - 196, y0 + lh + 4, 86, 24, "Todo", 0, 72); btn(w - 104, y0 + lh + 4, 88, 24, "Nada", 0, 73); }
    int fw = MIN(220, w - 112 - 70 - 16), y = y0 + lh + 40; g_text(W, 16, y + 5, tr("Contraseña"), UI_MUTED); field(112, y, fw, F_XPW, 1); btn(118 + fw, y, 54, 26, showpw ? "Ocultar" : "Ver", 0, 67); zw_clip(W, 182 + fw, y, MAX(0, w - 198 - fw), 26); g_text(W, 182 + fw, y + 5, tr("solo si está cifrado"), UI_MUTED); zw_noclip(W);
    y += 36; g_text(W, 16, y + 5, tr("Extraer en"), UI_MUTED); zw_clip(W, 112, y, w - 112 - 112, 26); g_text(W, 112, y + 5, xdest[0] ? xdest : "-", UI_TEXT); zw_noclip(W); btn(w - 106, y, 90, 26, "Cambiar", 0, 74); }

/* ================= Convertir (ALQUIMIA) ================= */
typedef struct { char p[400]; int kind; char res[160]; int ok; } vitem;
static vitem VI[128]; static int nvi, vsel = -1, vtop, vly; static char vdest[400]; static const char *vfmt[CK_N];
static void v_add(const char *p) { if (nvi >= 128) return; zdirent d; if (z_stat(p, &d) || d.is_dir) return; for (int i = 0; i < nvi; i++) if (!strcmp(VI[i].p, p)) return; vitem *v = &VI[nvi++]; memset(v, 0, sizeof *v); strncpy0(v->p, p, 400);
    u8 h[16] = {0}; int fd = z_open(p, O_READ); if (fd >= 0) { z_read(fd, h, 16); z_close(fd); } v->kind = cv_kind(p, h, 16); vsel = nvi - 1; if (nvi == 1) dest_for(p, vdest, 400);
    if (!vfmt[v->kind]) { const char *o[40]; cv_outputs(v->kind, o, 40); vfmt[v->kind] = o[0]; } }
static void v_run(void) {
    if (!nvi) { say(0xB91C1C, tr("Añade primero los archivos que quieres convertir."), 0); return; } busy = 1; cancel = 0; int ok = 0, bad = 0; if (vw[0]) cvo.maxw = cvo.maxh = atoi(vw); else cvo.maxw = cvo.maxh = 0;
    for (int i = 0; i < nvi && !cancel; i++) { vitem *v = &VI[i]; const char *f = vfmt[v->kind]; if (!f) continue; char out[620]; cv_outname(v->p, f, vdest, out, 620); char err[200]; p_done = 0; p_total = 1; strncpy0(p_what, base_name(v->p), 120); draw();
        int r = cv_convert(v->p, f, out, &cvo, prog, err); v->ok = r ? -1 : 1; if (r) { strncpy0(v->res, err, 160); bad++; plog("[prueba] alquimia convertir error %s", err); } else { snprintf(v->res, 160, "-> %s", base_name(out)); ok++; plog("[prueba] alquimia convertido %s", out); } }
    busy = 0; char m[200]; snprintf(m, 200, "%s %d%s", tr("Convertidos:"), ok, bad ? "" : "."); if (bad) { char b[80]; snprintf(b, 80, ", %s %d.", tr("con error:"), bad); strcat(m, b); } say(bad ? 0xB45309 : 0x15803D, m, vdest); }
/* opciones en fila que salta de linea si no cabe (pantallas estrechas) */
static int fx, fy;
static void fl_fit(int wd) { if (fx > 16 && fx + wd > W->w - 16) { fx = 16; fy += 32; } }
static void opt_step(const char *lab, const char *val, int idm, int idp) { int vw2 = MAX(64, g_textw(val) + 12), wd = g_textw(lab) + 8 + 52 + vw2; fl_fit(wd); int x = fx, y = fy;
    g_text(W, x, y + 5, lab, UI_MUTED); x += g_textw(lab) + 8; btn(x, y, 26, 26, "-", 0, idm); g_text(W, x + 26 + (vw2 - g_textw(val)) / 2, y + 5, val, UI_TEXT); btn(x + 26 + vw2, y, 26, 26, "+", 0, idp); fx += wd + 24; }
static void opt_chk(const char *lab, int on, int id) { int wd = g_textw(lab) + 24; fl_fit(wd); g_frame(W, fx, fy + 6, 14, 14, UI_MUTED); if (on) g_rect(W, fx + 3, fy + 9, 8, 8, TABC[2]); g_text(W, fx + 20, fy + 5, lab, UI_TEXT); hot(fx, fy, wd, 26, id); fx += wd + 24; }
static void opt_field(const char *lab, int fw, int f, int secret) { int wd = g_textw(lab) + 8 + fw; fl_fit(wd); g_text(W, fx, fy + 5, lab, UI_MUTED); field(fx + g_textw(lab) + 8, fy, fw, f, secret); fx += wd + 24; }
static void v_draw(int y0) {
    int w = W->w, lw = w - 32 - 178, lh = MAX(88, VH - y0 - scy - 290), LH = 22, vis = lh / LH; g_textb(W, 16, y0, tr("Qué convertir"), UI_TEXT); y0 += 22; vly = y0;
    g_rect(W, 16, y0, lw, lh, UI_WHITE); g_frame(W, 16, y0, lw, lh, UI_LINE); hot(16, y0, lw, lh, 80);
    if (!nvi) g_text(W, 28, y0 + 10, tr("Imágenes, sonido, vídeo AVI, documentos, tablas y comprimidos."), UI_MUTED);
    for (int i = vtop; i < nvi && i < vtop + vis; i++) { vitem *v = &VI[i]; int y = y0 + (i - vtop) * LH; if (i == vsel) g_rect(W, 17, y + 1, lw - 2, LH, UI_ACC2); zw_clip(W, 18, y, lw / 2, LH); g_text(W, 24, y + 3, base_name(v->p), UI_TEXT); zw_noclip(W);
        zw_clip(W, 24 + lw / 2, y, lw / 2 - 16, LH); if (v->ok) g_text(W, 24 + lw / 2, y + 3, v->res, v->ok > 0 ? 0x15803D : 0xB91C1C); else { char m[80]; snprintf(m, 80, "%s -> %s", tr(CVK_NAME[v->kind]), vfmt[v->kind] ? cv_label(vfmt[v->kind]) : "?"); g_text(W, 24 + lw / 2, y + 3, m, UI_MUTED); } zw_noclip(W); }
    int bx = w - 170; btn(bx, y0, 154, 28, "Añadir archivo", 0, 81); btn(bx, y0 + 34, 154, 28, "Quitar", 0, 82); btn(bx, y0 + 68, 154, 28, "Vaciar", 0, 83);
    int y = y0 + lh + 10, k = vsel >= 0 && vsel < nvi ? VI[vsel].kind : nvi ? VI[0].kind : -1;
    if (k >= 0) { char lab[80]; snprintf(lab, 80, "%s, %s:", tr(CVK_NAME[k]), tr("formato de salida")); g_text(W, 16, y + 5, lab, UI_MUTED); int x = 24 + g_textw(lab); const char *o[40]; int n = cv_outputs(k, o, 40);
        for (int i = 0; i < n; i++) { int cw = g_textw(cv_label(o[i])) + 26; if (x + cw > w - 16) { x = 24 + g_textw(lab); y += 30; } x += chip(x, y, cv_label(o[i]), vfmt[k] && !strcmp(vfmt[k], o[i]), TABC[2], 200 + i); }
        fx = 16; fy = y + 36; char v[40]; const char *f = vfmt[k] ? vfmt[k] : "";
        if (k == CK_IMG) { snprintf(v, 40, "%d %%", cvo.pct); opt_step(tr("Tamaño"), v, 101, 102); opt_field(tr("Lado máx."), 60, F_VW, 0); if (!strcmp(f, "jpg")) { snprintf(v, 40, "%d", cvo.quality); opt_step(tr("Calidad"), v, 103, 104); }
            snprintf(v, 40, "%d\xC2\xB0", cvo.rot); opt_step(tr("Girar"), v, 105, 106); opt_chk(tr("Escala de grises"), cvo.gray, 107); if (!strcmp(f, "pdf")) opt_chk(tr("Carta (EE. UU.)"), cvo.paper, 120); }
        else if (k == CK_AUD || (k == CK_VID && (!strcmp(f, "wav") || !strcmp(f, "mp3")))) { static const char *CH[3] = {"Igual", "Mono", "Estéreo"}; opt_step(tr("Canales"), tr(CH[cvo.ch]), 108, 109); if (cvo.rate) snprintf(v, 40, "%d Hz", cvo.rate); else snprintf(v, 40, "%s", tr("igual")); opt_step(tr("Frecuencia"), v, 110, 111);
            if (!strcmp(f, "mp3")) { snprintf(v, 40, "%d kbps", cvo.kbps); opt_step("MP3", v, 112, 113); } opt_chk(tr("Normalizar el volumen"), cvo.norm, 114); }
        else if (k == CK_VID && !strcmp(f, "gif")) { snprintf(v, 40, "%d", cvo.gif_fps); opt_step(tr("GIF: fotogramas/s"), v, 115, 116); snprintf(v, 40, "%d px", cvo.gif_w); opt_step(tr("ancho"), v, 117, 118); }
        else if (k == CK_DOC) { if (!strcmp(f, "pdf")) { opt_chk(tr("Papel Carta (EE. UU.) en el PDF"), cvo.paper, 120); snprintf(v, 40, "%d pt", cvo.fsize); opt_step(tr("Letra"), v, 121, 122); } }
        else if (k == CK_DATA) { static const char *SP[5] = {"auto", ",", ";", "tab", "|"}; int si = cvo.delim == ',' ? 1 : cvo.delim == ';' ? 2 : cvo.delim == '\t' ? 3 : cvo.delim == '|' ? 4 : 0; opt_step(tr("Separador"), tr(SP[si]), 123, 124); opt_chk(tr("La primera fila es la cabecera"), cvo.header, 125); }
        else if (k == CK_ARC) opt_field(tr("Contraseña para abrirlo"), MIN(200, W->w - 220), F_VPW, 1);
        y = fy; }
    y = MAX(y + 40, VH - scy - 100); g_text(W, 16, y + 5, tr("Guardar en"), UI_MUTED); zw_clip(W, 112, y, w - 112 - 112, 26); g_text(W, 112, y + 5, vdest[0] ? vdest : "-", UI_TEXT); zw_noclip(W); btn(w - 106, y, 90, 26, "Cambiar", 0, 84); }

/* ================= ventana ================= */
static void draw(void) {
    nhot = 0; int w = W->w; g_clear(W, UI_BG); u32 c = TABC[tab]; VH = MAX(W->h, 600); scy = MAX(0, MIN(scy, VH - W->h));
    /* en pantallas bajas la cabecera es una sola franja de 34 puntos */
    int sm = W->h < 420, hh = sm ? 34 : 56, ty = sm ? 38 : 64, th = sm ? 28 : 32, hb = ty + th + 4;
    int y0 = hb + 10 - scy; if (tab == 0) c_draw(y0); else if (tab == 1) x_draw(y0); else v_draw(y0);
    /* cabecera y pestanas encima del contenido desplazado; tapan sus zonas pulsables */
    g_rect(W, 0, hh, w, hb - hh, UI_BG); hot(0, 0, w, hb, 0); g_grad(W, 0, 0, w, hh, 0x111827, c);
    if (sm) { g_textb(W, 12, 9, "Alquimia", 0xFFFFFF); zw_clip(W, 100, 0, MAX(0, w - 110), hh); g_text(W, 104, 9, tr("Comprimir, extraer y convertir"), 0xF3F4F6); zw_noclip(W); }
    else { g_text2(W, 16, 10, "Alquimia", 0xFFFFFF, 2); zw_clip(W, 170, 0, MAX(0, w - 186), 56); g_text(W, 170, 12, tr("Comprimir, extraer y convertir"), 0xF3F4F6); g_text(W, 170, 30, tr("Todo en este equipo, sin Internet"), 0xD1D5DB); zw_noclip(W); }
    static const char *TN[3] = {"Comprimir", "Extraer", "Convertir"}; int x = sm ? 8 : 16; for (int i = 0; i < 3; i++) { int tw = g_textw(tr(TN[i])) + (sm ? 20 : 36); g_round(W, x, ty, tw, th, 6, i == tab ? TABC[i] : UI_PANEL); g_text(W, x + (tw - g_textw(tr(TN[i]))) / 2, ty + (th - 16) / 2, tr(TN[i]), i == tab ? 0xFFFFFF : UI_TEXT); hot(x, ty, tw, th, 10 + i); x += tw + (sm ? 4 : 8); }
    if (VH > W->h) { btn(w - 70, ty, 28, th, "\xE2\x96\xB2", 0, 20); btn(w - 38, ty, 28, th, "\xE2\x96\xBC", 0, 21); }
    g_rect(W, 0, hb - 2, w, 2, c);
    /* barra de abajo: mensaje o progreso, y los botones de la pestana */
    g_rect(W, 0, W->h - 66, w, 66, UI_BG); g_rect(W, 0, W->h - 66, w, 1, UI_LINE); hot(0, W->h - 66, w, 66, 0);
    if (tab == 0) pbtn(w - 170, W->h - 58, 154, 40, busy ? "Comprimiendo..." : "Comprimir", c, 69);
    else if (tab == 1) { btn(w - 296, W->h - 58, 118, 40, "Comprobar", 0, 75); pbtn(w - 170, W->h - 58, 154, 40, busy ? "Extrayendo..." : "Extraer", c, 76); }
    else pbtn(w - 170, W->h - 58, 154, 40, busy ? "Convirtiendo..." : "Convertir", c, 85);
    int my = W->h - 58, mw = w - (tab == 1 ? 312 : 186) - 16;
    if (busy) { g_rect(W, 16, my + 2, mw, 14, UI_PANEL); g_frame(W, 16, my + 2, mw, 14, UI_LINE); long pc = p_total > 0 ? MIN(1000L, p_done * 1000 / p_total) : 0; g_rect(W, 17, my + 3, (int)((mw - 2) * pc / 1000), 12, c);
        char m[200]; snprintf(m, 200, "%ld %% · %s", pc / 10, p_what); zw_clip(W, 16, my + 20, mw - 100, 18); g_text(W, 16, my + 20, m, UI_MUTED); zw_noclip(W); btn(16 + mw - 90, my + 18, 90, 22, "Cancelar", 0, 99); }
    else { zw_clip(W, 16, my, mw, 40); g_text(W, 16, my + 2, msg, msgc ? msgc : UI_MUTED); g_text(W, 16, my + 20, msg2, UI_MUTED); zw_noclip(W); }
    g_text(W, w - 16 - g_textw("tonyfluxa.top"), W->h - 16, "tonyfluxa.top", UI_LINE);
    zw_present(W); }
static void read_list(const char *lf, void (*add)(const char *)) { long n; char *d = z_readfile(lf, &n); if (!d) { add(lf); return; } for (char *p = d; *p;) { char *e = strchr(p, '\n'); if (e) *e = 0; if (*p) add(p); if (!e) break; p = e + 1; } free(d); if (!strncmp(lf, "/rd/tmp/", 8)) z_unlink(lf); }
static int is_archive(const char *p) { return z_ends(p, ".zip") || z_ends(p, ".tar") || z_ends(p, ".gz") || z_ends(p, ".tgz"); }
static void step(int *v, int d, int lo, int hi) { *v = MAX(lo, MIN(hi, *v + d)); }
static void click(zevent *e) {
    int id = hit(e->x, e->y); if (id < 0) { focus = F_NONE; return; }
    if (id >= 900) { focus = id - 900; int cap; char *b = fbuf(focus, &cap); fcur = strlen(b); if (!(focus == F_CPW || focus == F_XPW || focus == F_VPW) || showpw) ui_input_click(W, e, b, &fcur); return; }
    focus = F_NONE; char p[400];
    if (id == 0) return; if (id >= 10 && id <= 12) { if (tab != id - 10) { msg[0] = msg2[0] = 0; scy = 0; } tab = id - 10; z_cfg_set("alquimia_pestana", tab); return; }
    if (id == 20 || id == 21) { scy += id == 20 ? -120 : 120; return; }
    if (busy) return;
    switch (id) {
    case 51: if (z_pick_file(tr("Añadir a Alquimia"), 0, p, 400)) c_add(p); break;
    case 52: if (z_pick_dir(tr("Carpeta para comprimir"), p, 400)) c_add(p); break;
    case 53: if (csel >= 0 && csel < nci) { memmove(CI + csel, CI + csel + 1, (nci - csel - 1) * sizeof *CI); nci--; csel = MIN(csel, nci - 1); } break;
    case 54: nci = 0; csel = -1; ctop = 0; c_done = 0; break;
    case 50: { int i = ctop + (e->y - cly) / 22; csel = i < nci ? i : -1; } break;
    case 60: case 61: case 62: case 63: cfmt = id - 60; z_cfg_set("alquimia_formato", cfmt); break;
    case 64: case 65: case 66: clevel = id - 63; z_cfg_set("alquimia_nivel", clevel); break;
    case 67: showpw = !showpw; break;
    case 68: if (z_pick_dir(tr("Dónde guardar el comprimido"), p, 400)) strncpy0(cdest, p, 400); break;
    case 69: c_run(); break;
    case 70: if (z_pick_file(tr("Abrir comprimido"), "zip,tar,gz,tgz", p, 400)) x_open(p); break;
    case 71: if (ar_ok) { int i = xtop + (e->y - xly) / 22; if (i >= 0 && i < AR.n) { xsel[i] = !xsel[i]; if (AR.e[i].dir) { int L = strlen(AR.e[i].name); for (int k = 0; k < AR.n; k++) if (!strncmp(AR.e[k].name, AR.e[i].name, L)) xsel[k] = xsel[i]; } } } break;
    case 72: if (ar_ok) memset(xsel, 1, AR.n); break;
    case 73: if (ar_ok) memset(xsel, 0, AR.n); break;
    case 74: if (z_pick_dir(tr("Dónde extraer"), p, 400)) strncpy0(xdest, p, 400); break;
    case 75: x_run(1); break;
    case 76: x_run(0); break;
    case 80: { int i = vtop + (e->y - vly) / 22; vsel = i < nvi ? i : -1; } break;
    case 81: if (z_pick_file(tr("Añadir a Alquimia"), 0, p, 400)) v_add(p); break;
    case 82: if (vsel >= 0 && vsel < nvi) { memmove(VI + vsel, VI + vsel + 1, (nvi - vsel - 1) * sizeof *VI); nvi--; vsel = MIN(vsel, nvi - 1); } break;
    case 83: nvi = 0; vsel = -1; vtop = 0; break;
    case 84: if (z_pick_dir(tr("Dónde guardar lo convertido"), p, 400)) strncpy0(vdest, p, 400); break;
    case 85: v_run(); break;
    case 101: step(&cvo.pct, -25, 25, 400); break; case 102: step(&cvo.pct, 25, 25, 400); break; case 103: step(&cvo.quality, -5, 10, 100); break; case 104: step(&cvo.quality, 5, 10, 100); break;
    case 105: cvo.rot = (cvo.rot + 270) % 360; break; case 106: cvo.rot = (cvo.rot + 90) % 360; break; case 107: cvo.gray = !cvo.gray; break;
    case 108: cvo.ch = (cvo.ch + 2) % 3; break; case 109: cvo.ch = (cvo.ch + 1) % 3; break;
    case 110: case 111: { static const int R[6] = {0, 22050, 32000, 44100, 48000, 16000}; int i = 0; while (i < 6 && R[i] != cvo.rate) i++; i = (i + (id == 111 ? 1 : 5)) % 6; cvo.rate = R[i]; } break;
    case 112: case 113: { static const int K[8] = {64, 96, 128, 160, 192, 256, 320, 32}; int i = 0; while (i < 8 && K[i] != cvo.kbps) i++; i = (i + (id == 113 ? 1 : 7)) % 8; cvo.kbps = K[i]; } break;
    case 114: cvo.norm = !cvo.norm; break; case 115: step(&cvo.gif_fps, -1, 1, 25); break; case 116: step(&cvo.gif_fps, 1, 1, 25); break; case 117: step(&cvo.gif_w, -40, 80, 1280); break; case 118: step(&cvo.gif_w, 40, 80, 1280); break;
    case 120: cvo.paper = !cvo.paper; break; case 121: step(&cvo.fsize, -1, 7, 20); break; case 122: step(&cvo.fsize, 1, 7, 20); break;
    case 123: case 124: { static const char S[5] = {0, ',', ';', '\t', '|'}; int i = 0; while (i < 5 && S[i] != cvo.delim) i++; i = (i + (id == 124 ? 1 : 4)) % 5; cvo.delim = S[i]; } break;
    case 125: cvo.header = !cvo.header; break;
    default: if (id >= 200 && id < 240) { int k = vsel >= 0 && vsel < nvi ? VI[vsel].kind : nvi ? VI[0].kind : -1; const char *o[40]; int n = k >= 0 ? cv_outputs(k, o, 40) : 0; if (id - 200 < n) { vfmt[k] = o[id - 200]; for (int i = 0; i < nvi; i++) if (VI[i].kind == k) VI[i].ok = 0; } } }
}
int main(const char *a) {
    zsys s; z_sysinfo(&s); int ww = MIN(860, (int)s.scr_w - 20), wh = MIN(620, (int)s.scr_h - 50);
    W = zw_new("Alquimia", -1, 0, MAX(ww, 400), MAX(wh, 300), WF_DECOR | WF_RESIZE); if (!W) return 1;
    cv_defaults(&cvo); tab = z_cfg_get("alquimia_pestana", 0) % 3; cfmt = z_cfg_get("alquimia_formato", AF_ZIP) % 4; clevel = MAX(1, MIN(3, z_cfg_get("alquimia_nivel", 2))); z_docdir(cdest); z_docdir(vdest);
    if (a && a[0]) { if (!strncmp(a, "--comprimir ", 12)) { read_list(a + 12, c_add); tab = 0; } else if (!strncmp(a, "--convertir ", 12)) { read_list(a + 12, v_add); tab = 2; }
        else if (is_archive(a)) { x_open(a); tab = 1; } else { v_add(a); tab = 2; } }
    plog("[prueba] alquimia abierta %s", a ? a : ""); draw(); zevent e;
    for (;;) {
        if (quit_after) return 0; if (!z_event(&e, -1)) continue; if (e.type == EV_REDRAW || e.type == EV_THEME) { draw(); continue; } if (e.win != (u32)W->id) continue;
        if (e.type == EV_CLOSE) return 0;
        if (e.type == EV_RESIZE) zw_resized(W, e.x, e.y);
        else if (e.type == EV_WHEEL) { int d = -(int)e.key * 3, h = hit(e.x, e.y); if (h == 50) ctop = MAX(0, MIN(ctop + d, nci - 1)); else if (h == 71 && ar_ok) xtop = MAX(0, MIN(xtop + d, AR.n - 1)); else if (h == 80) vtop = MAX(0, MIN(vtop + d, nvi - 1)); else scy += d * 12; }
        else if (e.type == EV_DOWN) { if (e.btn & 2) { if (focus) { int cap; char *b = fbuf(focus, &cap); if (ui_input_menu(W, b, cap, &fcur, &e)) { draw(); continue; } } } else click(&e); }
        else if (e.type == EV_KEY) { if (e.ch == 27) focus = F_NONE; else if (e.ch == '\t' && tab == 0) focus = focus == F_CPW ? F_CNAME : F_CPW; else if (focus) { if (e.ch == '\n') { focus = F_NONE; if (tab == 0) c_run(); else if (tab == 1) x_run(0); } else { int cap; char *b = fbuf(focus, &cap); ui_input_key(b, cap, &fcur, &e); } }
            else if (e.ch == '\n') { if (tab == 0) c_run(); else if (tab == 1) x_run(0); else v_run(); } else continue; }
        else continue;
        draw(); }
}
