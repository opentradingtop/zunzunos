/* Selector de archivos comun: lugares (Documentos, Escritorio, Descargas, memorias y discos), carpetas y una casilla para
   escribir la ruta. z_pick_file(titulo, extensiones o 0, salida, tamano): 1 si se eligio un archivo.
   z_pick_dir(titulo, salida, tamano): lo mismo para elegir una carpeta (la abierta o la marcada en la lista). */
#include "z.h"
typedef struct { char name[120]; int dir; u64 size; } pe_t;
static pe_t *E; static int ne;
static int pcmp(const void *a, const void *b) { const pe_t *x = a, *y = b; if (x->dir != y->dir) return y->dir - x->dir; return strcasecmp(x->name, y->name); }
static int okext(const char *n, const char *exts) { if (!exts || !*exts) return 1; const char *d = strrchr(n, '.'); if (!d) return 0; char e[16]; int k = 0; for (d++; *d && k < 14; d++) e[k++] = (char)tolower(*d); e[k] = 0; char pat[20]; snprintf(pat, 20, ",%s,", e); char all[200]; snprintf(all, 200, ",%s,", exts); return strstr(all, pat) != 0; }
static int dmode;
static void load(const char *dir, const char *exts) { ne = 0; zdirent d; for (int i = 0; ne < 400 && !z_readdir(dir, i, &d); i++) { if (d.name[0] == '.') continue; if (!d.is_dir && (dmode || !okext(d.name, exts))) continue; strncpy0(E[ne].name, d.name, 120); E[ne].dir = d.is_dir; E[ne].size = d.size; ne++; } qsort(E, ne, sizeof *E, pcmp); }
static void join(char *o, int cap, const char *dir, const char *n) { snprintf(o, cap, "%s%s%s", dir, strcmp(dir, "/") ? "/" : "", n); }
static int pick(const char *title, const char *exts, char *out, int cap) {
    zsys s; z_sysinfo(&s); int w = 620, h = 440; zwin *d = zw_new(title, ((int)s.scr_w - w) / 2, ((int)s.scr_h - h) / 2, w, h, WF_DECOR); if (!d) return 0;
    E = malloc(400 * sizeof *E); char dir[256], path[256]; z_docdir(dir); path[0] = 0; int cur = 0, sel = -1, top = 0, res = -1; u64 lastc = 0; int lasti = -1;
    /* lugares */
    char pl[12][120], pn[12][40]; int np = 0; zdirent st;
    snprintf(pl[np], 120, "%s", dir); strcpy(pn[np++], _("Documentos"));
    if (!z_stat("/sys/ESCRITORIO", &st)) { strcpy(pl[np], "/sys/ESCRITORIO"); strcpy(pn[np++], _("Escritorio")); }
    snprintf(pl[np], 120, "%s/Descargas", dir); if (!z_stat(pl[np], &st)) strcpy(pn[np++], _("Descargas"));
    zmount M[8]; int nm = z_mounts(M, 8); for (int i = 0; i < nm && np < 12; i++) { if (!strcmp(M[i].name, "/rd") || !strcmp(M[i].name, "/sys")) continue; strncpy0(pl[np], M[i].name, 120); snprintf(pn[np], 40, "%s", M[i].label[0] ? M[i].label : M[i].name); np++; }
    load(dir, exts); { char m[300]; snprintf(m, 300, "[prueba] selector abierto %s %d", dir, ne); z_log(m); }
    zevent e; int redraw = 1, LH = 20, ly = 76, lx = 160;
    while (res < 0) {
        int vis = (h - ly - 50) / LH;
        if (redraw) {
            g_clear(d, UI_BG); g_rect(d, 0, 0, 150, h, UI_PANEL); g_textb(d, 12, 12, _("Lugares"), UI_TEXT);
            for (int i = 0; i < np; i++) { int on = !strcmp(pl[i], dir); if (on) g_rect(d, 4, 36 + i * 24, 142, 22, UI_ACC2); g_text(d, 12, 39 + i * 24, pn[i], UI_TEXT); }
            ui_button(d, lx, 10, 70, 26, "Subir", 0); g_text(d, lx + 80, 16, dir, UI_MUTED);
            ui_input(d, lx, 42, w - lx - 12, 26, path, cur, 1);
            g_rect(d, lx, ly, w - lx - 12, vis * LH, UI_WHITE); g_frame(d, lx, ly, w - lx - 12, vis * LH, UI_LINE);
            if (!ne) g_text(d, lx + 10, ly + 8, _("Carpeta vacía."), UI_MUTED);
            for (int i = top; i < ne && i < top + vis; i++) { int y = ly + (i - top) * LH; if (i == sel) g_rect(d, lx + 1, y, w - lx - 14, LH, UI_ACC2);
                char b[140]; snprintf(b, 140, "%s%s", E[i].name, E[i].dir ? "/" : ""); g_text(d, lx + 8, y + 2, b, E[i].dir ? UI_ACC : UI_TEXT);
                if (!E[i].dir) { char sz[24]; snprintf(sz, 24, "%lu KB", (E[i].size + 1023) >> 10); g_text(d, w - 20 - g_textw(sz), y + 2, sz, UI_MUTED); } }
            ui_button(d, w - 200, h - 40, 90, 28, "Cancelar", 0); ui_button(d, w - 104, h - 40, 90, 28, dmode ? "Elegir" : "Abrir", 2); zw_present(d); redraw = 0; }
        if (!z_event(&e, -1) || e.win != (u32)d->id) continue;
        int pick = -2;   /* -2 nada; -1 la ruta escrita; >= 0 la fila */
        if (e.type == EV_CLOSE) res = 0;
        else if (e.type == EV_WHEEL) { top = MAX(0, MIN(top - (int)e.key * 3, ne - 1)); redraw = 1; }
        else if (e.type == EV_KEY) {
            if (e.ch == 27) res = 0;
            else if (e.ch == '\n') pick = path[0] ? -1 : dmode && sel < 0 ? -3 : dmode ? -4 : sel;
            else if (e.key == K_DOWN && ne) { sel = MIN(sel + 1, ne - 1); if (sel >= top + vis) top = sel - vis + 1; path[0] = 0; cur = 0; redraw = 1; }
            else if (e.key == K_UP && ne) { sel = MAX(sel - 1, 0); if (sel < top) top = sel; path[0] = 0; cur = 0; redraw = 1; }
            else if (ui_input_key(path, MIN(cap, 250), &cur, &e)) redraw = 1; }
        else if (e.type == EV_DOWN) {
            if (e.x < 150) { int i = (e.y - 36) / 24; if (e.y >= 36 && i < np) { strncpy0(dir, pl[i], 256); load(dir, exts); sel = -1; top = 0; redraw = 1; } }
            else if (ui_in(e.x, e.y, lx, 10, 70, 26)) { char *sl = strrchr(dir, '/'); if (sl && sl != dir) *sl = 0; else strcpy(dir, "/"); load(dir, exts); sel = -1; top = 0; redraw = 1; }
            else if (ui_in(e.x, e.y, lx, 42, w - lx - 12, 26)) { ui_input_click(d, &e, path, &cur); redraw = 1; }
            else if (ui_in(e.x, e.y, w - 200, h - 40, 90, 28)) res = 0;
            else if (ui_in(e.x, e.y, w - 104, h - 40, 90, 28)) pick = path[0] ? -1 : dmode && sel < 0 ? -3 : dmode ? -4 : sel;
            else if (e.x >= lx && e.y >= ly && e.y < ly + vis * LH) { int i = top + (e.y - ly) / LH; if (i < ne) { if (i == lasti && z_ms() - lastc < 450) pick = i; else { sel = i; path[0] = 0; cur = 0; redraw = 1; } lasti = i; lastc = z_ms(); } } }
        if (pick == -1) {   /* ruta escrita: absoluta o dentro de la carpeta actual */
            char p[256]; if (path[0] == '/') strncpy0(p, path, 256); else join(p, 256, dir, path);
            if (!z_stat(p, &st)) { if (st.is_dir && dmode) { strncpy0(out, p, cap); res = 1; } else if (st.is_dir) { strncpy0(dir, p, 256); load(dir, exts); path[0] = 0; cur = 0; sel = -1; top = 0; redraw = 1; } else if (!dmode) { strncpy0(out, p, cap); res = 1; } }
            else { path[0] = 0; cur = 0; redraw = 1; } }
        else if (pick == -3) { strncpy0(out, dir, cap); res = 1; }
        else if (pick == -4 && sel >= 0 && sel < ne) { join(out, cap, dir, E[sel].name); res = 1; }
        else if (pick >= 0 && pick < ne) { char p[256]; join(p, 256, dir, E[pick].name); if (E[pick].dir) { strncpy0(dir, p, 256); load(dir, exts); sel = -1; top = 0; redraw = 1; } else { strncpy0(out, p, cap); res = 1; } } }
    zw_close(d); free(E); E = 0; { char m[300]; snprintf(m, 300, "[prueba] selector cerrado %d %s", res, res ? out : ""); z_log(m); } return res; }
int z_pick_file(const char *title, const char *exts, char *out, int cap) { dmode = 0; return pick(title, exts, out, cap); }
int z_pick_dir(const char *title, char *out, int cap) { dmode = 1; int r = pick(title, 0, out, cap); dmode = 0; return r; }
