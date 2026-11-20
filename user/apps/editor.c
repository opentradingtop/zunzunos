/* Editor de texto */
#include "z.h"
static zwin *W; static char *T; static long len, cap, cur, sel = -1, top; static char path[256]; static int dirty; static char st[100];
#define TB 34
#define SB 20
#define CW (8 * (int)TH.text_sc)
#define CH (16 * (int)TH.text_sc)
static void title(void) { char t[300]; snprintf(t, 300, _("%s%s - Editor"), dirty ? "*" : "", path[0] ? path : _("sin titulo")); zw_title(W->id, t); }
static void ins(const char *s, long n) { if (len + n + 1 > cap) { cap = (len + n) * 2 + 4096; T = realloc(T, cap); } memmove(T + cur + n, T + cur, len - cur + 1); memcpy(T + cur, s, n); cur += n; len += n; if (!dirty) { dirty = 1; title(); } }
static void del(long a, long b) { memmove(T + a, T + b, len - b + 1); len -= b - a; if (cur >= b) cur -= b - a; else if (cur > a) cur = a; if (!dirty) { dirty = 1; title(); } }
static long ls(long p) { while (p > 0 && T[p - 1] != '\n') p--; return p; }
static long le(long p) { while (p < len && T[p] != '\n') p++; return p; }
static int colof(long p) { int c = 0; for (long i = ls(p); i < p; i++) if ((T[i] & 0xC0) != 0x80) c++; return c; }
static long atcol(long s, int c) { long i = s; while (i < len && T[i] != '\n' && c > 0) { i++; while (i < len && (T[i] & 0xC0) == 0x80) i++; c--; } return i; }
static int vis(void) { return (W->h - TB - SB) / CH; }
static void follow(void) { long l = ls(cur); if (cur < top || l < top) top = l; int v = vis(), k = 0; long p = l; while (p > top) { p = ls(p - 1); k++; if (k >= v) { top = p; long q = top; (void)q; k = v - 1; top = le(top) + 1; break; } } if (k >= v) top = le(top) + 1; }
static const char *bt[] = {"Nuevo", "Abrir", "Guardar", "Como...", "Imprimir"};
static void pbuild(zpdoc *d, const zpset *s, void *c) { zpflow f; zp_flow_init(&f, d, 56); f.num = 1; char *ln = malloc(len + 2); long p = 0; while (p <= len) { long e = le(p); memcpy(ln, T + p, e - p); ln[e - p] = 0; zp_para(&f, ZPF_MONO, 10, 0, ln, 0, 0); p = e + 1; } free(ln); if (!f.started) zp_flow_newpage(&f); }
static void print_(void) { zp_dialog(path[0] ? strrchr(path, '/') + 1 : "sin titulo", pbuild, 0); }
static void draw(void) {
    g_clear(W, UI_WHITE); g_rect(W, 0, 0, W->w, TB, UI_PANEL); int x = 6; for (int i = 0; i < 5; i++) { ui_button(W, x, 4, 72, 26, bt[i], 0); x += 76; }
    long a = sel < 0 ? -1 : MIN(sel, cur), b = sel < 0 ? -1 : MAX(sel, cur); int y = TB + 2, v = vis(); long p = top; int cols = (W->w - 10) / CW; zw_clip(W, 0, TB, W->w, W->h - TB - SB);
    for (int r = 0; r < v && p <= len; r++) { long e = le(p); int c = 0; for (long i = p; i <= e; c++) { const char *q = T + i; int ch = i < e ? utf8_next(&q) : 0; long nx = i < e ? q - T : e + 1; int X = 5 + c * CW; if (i >= a && i < b && a >= 0) g_rect(W, X, y, CW, CH, UI_ACC2); if (i == cur) g_rect(W, X, y, 2, CH, UI_ACC); if (ch && ch != '\t' && c < cols + 1) { char u[4]; int n = utf8_put(ch, u); u[n] = 0; if (TH.text_sc > 1) g_text2(W, X, y, u, UI_TEXT, TH.text_sc); else g_text(W, X, y, u, UI_TEXT); } i = nx; } p = e + 1; y += CH; }
    zw_noclip(W); g_rect(W, 0, W->h - SB, W->w, SB, UI_PANEL); int ln = 1; for (long i = 0; i < cur; i++) if (T[i] == '\n') ln++; char s[160]; snprintf(s, 160, _("Linea %d, col %d   %ld bytes   %s"), ln, colof(cur) + 1, len, st); g_text(W, 8, W->h - SB + 2, s, UI_TEXT); zw_present(W);
}
static void load(const char *p) { zdirent de; if (!z_stat(p, &de) && de.size > 8 * 1024 * 1024) { snprintf(st, 100, "%s", _("Archivo demasiado grande para el Editor")); return; } long n; char *d = z_readfile(p, &n); if (!d) { snprintf(st, 100, _("No se pudo abrir")); return; }
    { long bad = 0; for (long i = 0; i < MIN(n, 65536L); i++) if (!d[i] || ((u8)d[i] < 32 && d[i] != '\n' && d[i] != '\r' && d[i] != '\t' && d[i] != 12)) bad++; if (bad > MIN(n, 65536L) / 50 + 1) { free(d); snprintf(st, 100, "%s", _("No es un archivo de texto (es un programa o datos)")); return; } } free(T); T = realloc(d, n + 4096); len = n; cap = n + 4096; T[len] = 0; cur = top = 0; sel = -1; strncpy0(path, p, 256); dirty = 0; title(); st[0] = 0; }
static void save(int as) { if (as || !path[0]) { char n[256]; strcpy(n, path[0] ? path : "/sys/nota.txt"); if (!ui_prompt("Guardar", "Ruta del archivo:", n, 240) || !n[0]) return; strcpy(path, n); } int e = z_writefile(path, T, len); if (e) snprintf(st, 100, "Error: %s", z_errstr(e)); else { dirty = 0; strcpy(st, _("Guardado")); z_log("[prueba] editor guardo"); } title(); }
static void hit(int mx, int my) { int r = (my - TB - 2) / CH; long p = top; while (r-- > 0 && p < len) { long e = le(p); if (e >= len) break; p = e + 1; } cur = atcol(p, (mx - 5 + CW / 2) / CW); }
int main(const char *a) { int wp; a = z_print_arg(a, &wp);
    cap = 65536; T = calloc(1, cap); W = zw_new("Editor", -1, 0, 680, 460, WF_DECOR | WF_RESIZE);
    if (!strcmp(a, "--dmesg")) { len = z_dmesg(T, cap - 1); T[len] = 0; long w = 0; for (long i = 0; i < len; i++) if (T[i] != '\r') T[w++] = T[i]; len = w; T[len] = 0; zw_title(W->id, "Registro del sistema"); } else if (a[0]) load(a); else title();
    draw(); if (wp) { print_(); draw(); } zevent e; int drag = 0;
    for (;;) {
        if (!z_event(&e, -1)) continue; if (e.win != (u32)W->id) continue; ui_edit_menu(W, &e); if (e.type == EV_KEY && e.ch == 16) { print_(); draw(); continue; } if (e.type == EV_REDRAW) { draw(); continue; }
        if (e.type == EV_CLOSE) { if (!dirty || ui_confirm("Salir", "Hay cambios sin guardar. Salir?")) return 0; }
        else if (e.type == EV_RESIZE) zw_resized(W, e.x, e.y);
        else if (e.type == EV_WHEEL) { for (int i = 0; i < 3; i++) { if ((int)e.key > 0) { if (top > 0) top = ls(top - 1); } else { long n = le(top); if (n < len) top = n + 1; } } draw(); continue; }
        else if (e.type == EV_DOWN) { if (e.y < TB) { int i = (e.x - 6) / 76; if (i == 0) { len = cur = top = 0; T[0] = 0; path[0] = 0; dirty = 0; sel = -1; title(); } else if (i == 1) { char n[256]; strcpy(n, path[0] ? path : "/rd/res/ayuda.txt"); if (ui_prompt("Abrir", "Ruta del archivo:", n, 240)) load(n); } else if (i == 2) save(0); else if (i == 3) save(1); else if (i == 4) print_(); } else { hit(e.x, e.y); sel = cur; drag = 1; } }
        else if (e.type == EV_MOVE) { if (!drag) continue; hit(e.x, e.y); }
        else if (e.type == EV_UP) { drag = 0; if (sel == cur) sel = -1; }
        else if (e.type == EV_KEY) {
            int sh = e.mods & M_SHIFT; long c0 = cur; int k = e.key, mv = 1;
            if (k == K_LEFT) { if (cur > 0) { cur--; while (cur > 0 && (T[cur] & 0xC0) == 0x80) cur--; } } else if (k == K_RIGHT) { if (cur < len) { cur++; while (cur < len && (T[cur] & 0xC0) == 0x80) cur++; } } else if (k == K_HOME) cur = ls(cur); else if (k == K_END) cur = le(cur);
            else if (k == K_UP || k == K_PGUP) { int c = colof(cur); for (int i = k == K_UP ? 1 : vis(); i > 0; i--) { long s = ls(cur); if (!s) { cur = 0; break; } cur = ls(s - 1); } cur = atcol(cur, c); } else if (k == K_DOWN || k == K_PGDN) { int c = colof(cur); long s = cur; for (int i = k == K_DOWN ? 1 : vis(); i > 0; i--) { long n = le(s); if (n >= len) break; s = n + 1; } cur = atcol(ls(s), c); } else mv = 0;
            if (mv) { if (sh) { if (sel < 0) sel = c0; } else sel = -1; }
            else if (e.ch == 19) save(0); else if (e.ch == 1) { sel = 0; cur = len; }
            else if (e.ch == 3 || e.ch == 24) { if (sel >= 0) { long a2 = MIN(sel, cur), b2 = MAX(sel, cur); z_clip_set(T + a2, b2 - a2); if (e.ch == 24) { del(a2, b2); sel = -1; } } }
            else if (e.ch == 22 || e.ch == '\n' || e.ch == '\t' || e.ch == 8 || e.ch == 127 || e.ch >= 32) {
                if (sel >= 0 && sel != cur) { long a2 = MIN(sel, cur), b2 = MAX(sel, cur); del(a2, b2); cur = a2; sel = -1; if (e.ch == 8 || e.ch == 127) goto done; } sel = -1;
                if (e.ch == 22) { char *b = malloc(65536); int n = z_clip_get(b, 65536); ins(b, n); free(b); } else if (e.ch == 8) { if (cur > 0) { long s = cur - 1; while (s > 0 && (T[s] & 0xC0) == 0x80) s--; del(s, cur); } } else if (e.ch == 127) { if (cur < len) { long t = cur + 1; while (t < len && (T[t] & 0xC0) == 0x80) t++; del(cur, t); } } else { char u[4]; int n = utf8_put(e.ch, u); ins(u, n); }
            }
done:       follow();
        } else continue;
        draw();
    }
}
