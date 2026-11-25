/* Escritorio: fondo, iconos, panel, menu, idioma, reloj, brillo y volumen */
#include "z.h"
#define NAPP z_napps
#define S_ ((int)TH.text_sc)
#define PH (36 + (S_ - 1) * 16)
#define MI (28 * S_)
#define IS (TH.icon_sc == 0 ? 32 : TH.icon_sc == 2 ? 64 : 44)
#define IW (IS + 64 + (S_ - 1) * 92)
#define IH (IS + 40 + (S_ - 1) * 16)
#define TRAY (76 * S_)
#define FLAGW 40
static zwin *bg, *bar, *menu, *qs, *cal; static zsys si; static zwininfo wl[32]; static int nw, menu_hover = -1, icon_sel = -1, last_icon = -1, bright = 100, vol = 85, qs_drag, tb_x[32], tb_w, saved_diag, vis[64], nvis; static u64 menu_closed, qs_closed, cal_closed, last_click; static char clk[48];
static u32 *wall; static int wall_w, wall_h; static char wall_p[160]; static int h24 = 1, secs, dfmt = 1;
static int sess(void) { return z_sess(0, 0, 0); }
static int guest(void) { return (sess() & 7) == 2; }
/* programas que el invitado no puede abrir (el nucleo tambien lo impide) */
static int admin_only(const char *b) { static const char *L[] = {"config", "instalar", "discos", "rescate", "restaurar", "forense", "nube"}; for (int i = 0; i < 7; i++) if (!strcmp(b, L[i])) return 1; return 0; }
static int bar_y(void) { return TH.panel_top ? 0 : si.scr_h - PH; }
static void T_(zwin *w, int x, int y, const char *s, u32 c) { if (S_ > 1) g_text2(w, x, y, s, c, S_); else g_text(w, x, y, s, c); }
static void visible(void) { nvis = 0; int g = guest(); for (int i = 0; i < NAPP && nvis < 64; i++) if (!z_hidden(z_apps[i].bin) && !(g && admin_only(z_apps[i].bin))) vis[nvis++] = i; }
static void load_wall(void) { char p[160]; z_cfg_gets("fondo", p, 160); if (!strcmp(p, wall_p) && (wall || !p[0])) return; strcpy(wall_p, p); free(wall); wall = 0; if (p[0] && z_img_load(p, &wall, &wall_w, &wall_h)) wall = 0; }
static void load_clock(void) { h24 = z_cfg_get("reloj24", 1); secs = z_cfg_get("segundos", 0); dfmt = z_cfg_get("fecha", 1); }
static int cfg_load(void) { zdirent st; if (z_stat("/sys", &st)) return 0; ztheme t; z_theme_get(&t); if (!z_theme_load(&t)) { t.lang = z_cfg_get("idioma", 0); z_theme_set(&t); } else { t.lang = z_cfg_get("idioma", 0); if (t.lang != TH.lang) z_theme_set(&t); } bright = z_cfg_get("brillo", z_bright_get() & 255); z_bright_set(bright); vol = z_cfg_get("volumen", z_audio_vol(-1)); z_audio_vol(vol); z_setlayout(z_cfg_get("teclado", 1)); return 1; }
static void diag(void) { zdirent d; if (z_stat("/sys", &d)) return; char *b = malloc(90000); int n = snprintf(b, 2000, "ZunzunOS diagnostico\nPCI:\n"); zpci p[64]; int k = z_pci(p, 64); for (int i = 0; i < k; i++) n += snprintf(b + n, 80, "%02x:%02x.%d %04x:%04x clase %02x.%02x.%02x\n", p[i].bus, p[i].dev, p[i].fn, p[i].vendor, p[i].device, p[i].cls, p[i].sub, p[i].pif); n += snprintf(b + n, 40, "\nREGISTRO:\n"); n += z_dmesg(b + n, 65536); z_writefile("/sys/DIAGNOST.TXT", b, n); free(b); }
static void launch(const char *b, const char *arg) { char p[64]; snprintf(p, 64, "/rd/bin/%s", b); int r = z_spawn(p, arg ? arg : ""); if (r < 0) { char m[120]; snprintf(m, 120, "%s %s: %s", _("No se pudo abrir"), b, z_errstr(r)); ui_confirm("Error", m); } }
static void bird(zwin *w, int x, int y, int s, u32 c, u32 d) { for (int i = 0; i < s; i++) { int h = i * 3 / 5; g_rect(w, x + i, y - h / 2 + s / 6, 1, h + 1, c); } g_circle(w, x + s + s / 8, y + s / 8, s / 4, d, 1); g_line(w, x + s + s / 3, y + s / 8, x + s * 2, y, d); for (int i = 0; i < s / 2; i++) g_line(w, x + s / 2 + i, y - i / 8, x + s / 4 + i / 2, y - s / 2 - i / 3, d); }
static void draw_wall(zwin *w) { int m = z_cfg_get("fondo_modo", 0), W = w->w, H = w->h; g_rect(w, 0, 0, W, H, TH.d1); if (m == 3) { for (int y = 0; y < H; y += wall_h) for (int x = 0; x < W; x += wall_w) g_image(w, x, y, wall_w, wall_h, wall); return; } if (m == 2) { g_image(w, (W - wall_w) / 2, (H - wall_h) / 2, wall_w, wall_h, wall); return; } long sw = W * 1024L / wall_w, sh = H * 1024L / wall_h, s = m == 0 ? MAX(sw, sh) : MIN(sw, sh); int dw = m == 4 ? W : wall_w * s / 1024, dh = m == 4 ? H : wall_h * s / 1024, ox = (W - dw) / 2, oy = (H - dh) / 2; for (int y = MAX(0, oy); y < MIN(H, oy + dh); y++) { u32 *o = w->px + y * W, *r = wall + (long)(y - oy) * wall_h / dh * wall_w; for (int x = MAX(0, ox); x < MIN(W, ox + dw); x++) o[x] = r[(long)(x - ox) * wall_w / dw]; } }
/* iconos del escritorio: apps por familias (una columna nueva por familia) y lo que hay en la carpeta Escritorio */
typedef struct { short kind, idx, x, y; } dic_t; static dic_t IC[160]; static int nic; static char DESK[48];
typedef struct { char name[64]; u8 dir; } dent_t; static dent_t DE[64]; static int nde;
static void desk_scan(void) { nde = 0; zdirent d; for (int i = 0; nde < 64 && !z_readdir(DESK, i, &d); i++) { if (d.name[0] == '.') continue; strncpy0(DE[nde].name, d.name, 64); DE[nde].dir = d.is_dir; nde++; } }
/* categorias plegables: al arrancar solo se ven los encabezados (la carpeta Escritorio, abierta); un clic en el
   encabezado abre o cierra su columna. Las posiciones no cambian al abrir o cerrar */
static u32 fam_open = 1u << 31;   /* bit f: familia f abierta; el bit 31 es la carpeta Escritorio */
static int fam_is_open(int f) { return f >= z_nfams ? (fam_open >> 31) & 1 : (fam_open >> f) & 1; }
static void layout(int W, int by, int ty) { nic = 0; int HD = 18 * S_, col = -1, row = 0, rows = MAX(1, (by - ty - 20 - HD) / IH);
    for (int f = 0; f <= z_nfams; f++) { int any = 0; int n = f < z_nfams ? nvis : nde; for (int k = 0; k < n; k++) { if (f < z_nfams && z_apps[vis[k]].fam != f) continue; if (!any) { col++; row = 0; any = 1; IC[nic] = (dic_t){-1 - f, 0, 12 + col * IW, ty + 6, }; nic++; } else if (row >= rows) { col++; row = 0; } if (nic >= 160) return; if (fam_is_open(f)) IC[nic++] = (dic_t){f < z_nfams ? 0 : 1, k, 12 + col * IW, ty + 6 + HD + row * IH}; row++; } } }
static void draw_icon(zwin *w, int x, int y, u32 col, const char *sym, const char *n, int sel, int kind) { int cx = x + IW / 2 - 2; if (sel) g_round(w, x, y, IW - 4, IH - 4, 6, TH.acc); if (kind == 1) { g_round(w, cx - IS / 2, y + 6 + IS / 6, IS, IS * 5 / 6, IS / 10, col); g_rect(w, cx - IS / 2, y + 6 + IS / 12, IS / 2, IS / 6, col); } else if (kind == 2) { g_rect(w, cx - IS / 3, y + 6, IS * 2 / 3, IS, 0xF8FAFC); g_frame(w, cx - IS / 3, y + 6, IS * 2 / 3, IS, 0x94A3B8); for (int i = 0; i < 4; i++) g_rect(w, cx - IS / 4, y + 14 + i * IS / 5, IS / 2, 2, 0x94A3B8); } else { g_round(w, cx - IS / 2, y + 6, IS, IS, IS / 6, col); g_text(w, cx - g_textw(sym) / 2, y + 6 + IS / 2 - 8, sym, 0xFFFFFF); } char t[24]; strncpy0(t, n, 15); int tw = g_textw(t) * S_; T_(w, cx - tw / 2 + 1, y + IS + 11, t, 0); T_(w, cx - tw / 2, y + IS + 10, t, 0xFFFFFF); }
static void draw_bg(void) { zwin *w = bg; int by = TH.panel_top ? w->h : w->h - PH, ty = TH.panel_top ? PH : 0; if (wall) draw_wall(w); else { g_grad(w, 0, 0, w->w, w->h, TH.d1, TH.d2); if (TH.motif) { for (int i = 0; i < 90; i++) g_circle(w, w->w - 80, w->h + 40, 40 + i * 6, TH.d2 + 0x030303 * (i & 3), 0); bird(w, w->w - 320, by - 200, 120, TH.acc, TH.ui_acc2); g_text2(w, w->w - 300, by - 110, "ZunzunOS", TH.ui_acc2, 3); } }
    if (TH.show_icons) { layout(w->w, by, ty); for (int i = 0; i < nic; i++) { dic_t *c = &IC[i]; if (c->kind < 0) { int f = -1 - c->kind; const char *h = f < z_nfams ? _(z_fams[f]) : _("Escritorio"); char t[28]; strncpy0(t, h, 14); int tx = c->x + 5, ty2 = c->y + 4 * S_; if (fam_is_open(f)) for (int k = 0; k < 5; k++) g_line(w, tx + k, ty2 + 2 + k, tx + 8 - k, ty2 + 2 + k, 0xE5E7EB); else for (int k = 0; k < 5; k++) g_line(w, tx + 1 + k, ty2 + k, tx + 1 + k, ty2 + 8 - k, 0xE5E7EB);
                tx += 13; T_(w, tx + 1, c->y + 1, t, 0); T_(w, tx, c->y, t, 0xE5E7EB); g_rect(w, c->x + 5, c->y + 16 * S_ + 1, IW - 16, 1, f < z_nfams ? z_famcol[f] : 0xE5E7EB); continue; } if (c->kind == 0) { const zapp *a = &z_apps[vis[c->idx]]; draw_icon(w, c->x, c->y, a->col, a->sym, _(a->name), i == icon_sel, 0); } else { char lab[64]; strncpy0(lab, DE[c->idx].name, 64); int kd = DE[c->idx].dir ? 1 : 2, lnk = z_ends(lab, ".acceso"); if (lnk) { char p[220], t[400]; zdirent st; snprintf(p, 220, "%s/%s", DESK, DE[c->idx].name); lab[strlen(lab) - 7] = 0; if (!z_shortcut_target(p, t, 400) && !z_stat(t, &st)) kd = st.is_dir ? 1 : 2; } draw_icon(w, c->x, c->y, 0xD97706, "", lab, i == icon_sel, kd); if (lnk) { int ax = c->x + IW / 2 - IS / 2 - 2, ay = c->y + 6 + IS - 14; g_rect(w, ax, ay, 14, 14, 0xFFFFFF); g_frame(w, ax, ay, 14, 14, 0x334155); g_line(w, ax + 3, ay + 10, ax + 10, ay + 3, 0x1D4ED8); g_line(w, ax + 6, ay + 3, ax + 10, ay + 3, 0x1D4ED8); g_line(w, ax + 10, ay + 3, ax + 10, ay + 7, 0x1D4ED8); } } } } zw_present(w); }
static int head_at(int x, int y) { for (int i = 0; i < nic; i++) if (IC[i].kind < 0 && x >= IC[i].x && x < IC[i].x + IW - 4 && y >= IC[i].y - 3 && y < IC[i].y + 16 * S_ + 6) return -1 - IC[i].kind; return -1; }
static int icon_at(int x, int y) { for (int i = 0; i < nic; i++) if (IC[i].kind >= 0 && x >= IC[i].x && x < IC[i].x + IW - 4 && y >= IC[i].y && y < IC[i].y + IH - 4) return i; return -1; }
static int clock_w(void) { return g_textw(clk) * S_; }
static int tray_x(void) { return bar->w - clock_w() - 16 - TRAY - FLAGW; }
/* iconos de red e impresora, a la izquierda de brillo/volumen */
#define ICW (26 * S_)
/* bateria (solo si el sistema la encuentra): icono y porcentaje a la derecha de la impresora */
static int bat[5], bat_ok; static u64 bat_t;
#define BATW (bat_ok ? 62 * S_ : 0)
static int icons_x(void) { return tray_x() - 2 * ICW - 8 - BATW; }
static int net_st, prn_st; /* red: 0 sin tarjeta 1 sin cable 2 sin direccion 3 conectada; impresora: 0 ninguna 1 lista 2 imprimiendo 3 error */
static char rj_name[24][64];
static int read_jobs(char (*o)[400], int max) { zdirent e; char nm[24][64]; int c = 0; for (int i = 0; c < 24 && !z_readdir("/rd/tmp/IMPRESION", i, &e); i++) if (e.name[0] != '.') strncpy0(nm[c++], e.name, 64);
    for (int i = 0; i < c; i++) for (int j = i + 1; j < c; j++) if (strcmp(nm[j], nm[i]) > 0) { char x[64]; strcpy(x, nm[i]); strcpy(nm[i], nm[j]); strcpy(nm[j], x); }
    int k = 0; for (int i = 0; i < c && k < max; i++) { char f[120]; snprintf(f, 120, "/rd/tmp/IMPRESION/%s", nm[i]); char *t = z_readfile(f, 0); if (t) { strncpy0(rj_name[k], nm[i], 64); strncpy0(o[k++], t, 400); free(t); } } return k; }
static u64 toast_until; static char toast[160];
static int poll_bat(void) { u64 now = z_ms(); if (bat_t && now - bat_t < 10000) return 0; bat_t = now; int o[5]; int was = bat_ok, op = bat[1], oc = bat[2], oa = bat[3];
    if (z_battery(o) || !o[0]) { bat_ok = 0; return was; } memcpy(bat, o, sizeof o); bat_ok = 1;
    static int warned = 101; if (bat[4] && bat[1] <= 10 && bat[1] < warned - 4) { warned = bat[1]; snprintf(toast, 160, _("Batería baja: %d %%. Conecta el cargador."), bat[1]); toast_until = now + 8000; } if (!bat[4]) warned = 101;
    if (!was || op != bat[1] || oc != bat[2] || oa != bat[3]) { char lg[80]; snprintf(lg, 80, "[prueba] bateria %d%% cargando %d cargador %d", bat[1], bat[2], bat[3]); z_log(lg); return 1; } return 0; }
static const char *bat_text(char *b) { snprintf(b, 120, "%s: %d %%, %s", _("Batería"), bat[1], _(bat[2] ? "cargando" : bat[4] ? "con la batería" : bat[1] >= 99 ? "cargada y con el cargador" : "con el cargador")); return b; }
static int poll_status(void) { int o1 = net_st, o2 = prn_st; int ob = poll_bat(); znet ni; net_st = z_netinfo(&ni) ? 0 : !ni.up ? 1 : !ni.dhcp && !ni.local ? 2 : 3;
    zprinter pr[12]; int np = zp_printers(pr, 12); char j[1][400]; int nj = read_jobs(j, 1); prn_st = !np && !nj ? 0 : 1; if (nj) { int st = atoi(j[0]); if (st < 2) prn_st = 2; else if (st == 3) prn_st = 3; }
    return ob || o1 != net_st || o2 != prn_st; }
static void draw_icons(zwin *w) { int x = icons_x(), cy = PH / 2;
    u32 nc = net_st == 3 ? 0x4ADE80 : net_st == 2 ? 0xFBBF24 : 0x6B7280; for (int i = 0; i < 3; i++) g_rect(w, x + 5 + i * 6, cy + 6 - (6 + i * 4), 4, 6 + i * 4, nc); if (net_st < 2) g_line(w, x + 3, cy + 7, x + 22, cy - 9, 0xF87171);
    if (bat_ok) { int bx = icons_x() + 2 * ICW + 6, by = cy - 6, pc = bat[1]; u32 c = bat[2] || bat[3] ? 0x4ADE80 : pc <= 10 ? 0xEF4444 : pc <= 25 ? 0xFBBF24 : 0xE5E7EB;
        g_frame(w, bx, by, 22, 12, 0xE5E7EB); g_rect(w, bx + 22, by + 3, 2, 6, 0xE5E7EB); g_rect(w, bx + 2, by + 2, MAX(1, 18 * pc / 100), 8, c);
        if (bat[2]) { g_line(w, bx + 12, by + 1, bx + 9, by + 6, 0x111827); g_line(w, bx + 9, by + 6, bx + 14, by + 6, 0x111827); g_line(w, bx + 14, by + 6, bx + 11, by + 11, 0x111827); }
        char t[8]; snprintf(t, 8, "%d%%", pc); g_text(w, bx + 27, cy - 8, t, 0xFFFFFF); }
    if (!prn_st) return; x += ICW + 4; u32 pc = prn_st == 3 ? 0xFCA5A5 : 0xE5E7EB; g_rect(w, x + 7, cy - 9, 12, 6, 0xF8FAFC); g_round(w, x + 3, cy - 4, 20, 10, 2, pc); g_rect(w, x + 7, cy + 4, 12, 5, 0xF8FAFC); if (prn_st == 2) g_circle(w, x + 21, cy - 7, 4, 0x38BDF8, 1); if (prn_st == 3) g_circle(w, x + 21, cy - 7, 4, 0xEF4444, 1); }
static void draw_bar(void) { zwin *w = bar; g_grad(w, 0, 0, w->w, PH, TH.panel1, TH.panel2); g_rect(w, 0, TH.panel_top ? PH - 1 : 0, w->w, 1, TH.acc); int th = 16 * S_, ty = (PH - th) / 2, bw = 96 * S_; g_round(w, 6, 5, bw, PH - 10, 4, TH.acc); bird(w, 12, PH / 2 + 1, 12, 0xFFFFFF, TH.ui_acc2); T_(w, 40, ty, _("Inicio"), 0xFFFFFF); nw = zw_list(wl, 32); int x = bw + 16, tx = icons_x(); tb_w = nw ? MIN(170 * S_, (tx - x - 6) / nw) : 170; for (int i = 0; i < nw; i++) { int foc = wl[i].state & 0x100, min = wl[i].state & 1; tb_x[i] = x; g_round(w, x, 5, tb_w - 4, PH - 10, 3, foc ? TH.acc : min ? TH.panel2 : TH.inact); zw_clip(w, x + 6, 5, tb_w - 16, PH - 10); T_(w, x + 8, ty, wl[i].title, min ? 0x9CA3AF : 0xFFFFFF); zw_noclip(w); x += tb_w; } draw_icons(w); if (toast[0] && z_ms() < toast_until) { int tw = g_textw(toast) + 20, x0 = MAX(bw + 16, icons_x() - tw - 8); g_round(w, x0, 4, tw, PH - 8, 4, 0x065F46); g_text(w, x0 + 10, (PH - 16) / 2, toast, 0xFFFFFF); } tx = tray_x(); g_round(w, tx, 5, TRAY - 8, PH - 10, 4, qs ? TH.acc : TH.inact); g_glyph(w, tx + 10 * S_, PH / 2 - 8, 15, 0xFDE68A); g_glyph(w, tx + 40 * S_, PH / 2 - 8, 14, 0xBFDBFE); z_flag(w, tx + TRAY + 2, PH / 2 - 10, TH.lang); T_(w, w->w - clock_w() - 12, ty, clk, 0xE5E7EB); zw_present(w); }
#define QW 260
#define QH 104
static void draw_qs(void) { zwin *w = qs; g_clear(w, TH.panel1); g_frame(w, 0, 0, w->w, w->h, TH.acc); char b[48]; for (int k = 0; k < 2; k++) { int y = 12 + k * 46, v = k ? vol : bright; snprintf(b, 48, "%s  %d%%", k ? _("Volumen") : _("Brillo"), v); g_text(w, 14, y, b, 0xFFFFFF); g_round(w, 14, y + 20, QW - 28, 8, 3, 0x4B5563); g_round(w, 14, y + 20, MAX(8, (QW - 28) * v / 100), 8, 3, k ? 0x38BDF8 : 0xFBBF24); g_circle(w, 14 + (QW - 28) * v / 100, y + 24, 7, 0xFFFFFF, 1); } zw_present(w); }
static void qs_set(int x, int y) { int v = MAX(0, MIN((x - 14) * 100 / (QW - 28), 100)); if (y < 54) { bright = MAX(5, v); z_bright_set(bright); } else { vol = v; z_audio_vol(vol); } draw_qs(); }
static void close_qs(void) { if (qs) { zw_close(qs); qs = 0; qs_closed = z_ms(); z_cfg_set("brillo", bright); z_cfg_set("volumen", vol); draw_bar(); } }
/* aviso breve en la barra (capturas): toast y toast_until */
/* captura de pantalla: Impr Pant = pantalla entera, Alt+Impr Pant = ventana activa. Se guarda en Documentos/Capturas */
static u64 shot_at;
static void screenshot(int win_only) { long np = (long)si.scr_w * si.scr_h; u32 *px = malloc(np * 4); if (!px) return; int r = z_capture(px, np); if (r < 0) { free(px); return; }
    int W = r >> 16, H = r & 0xFFFF, x0 = 0, y0 = 0, w = W, h = H; if (win_only) { nw = zw_list(wl, 32); for (int i = 0; i < nw; i++) if ((wl[i].state & 0x100) && !(wl[i].state & 1)) { x0 = MAX(0, wl[i].x); y0 = MAX(0, wl[i].y); w = MIN(wl[i].w, W - x0); h = MIN(wl[i].h, H - y0); } }
    if (x0 || y0 || w != W) for (int y = 0; y < h; y++) memmove(px + (long)y * w, px + (long)(y0 + y) * W + x0, w * 4);
    char dd[120], dir[160], p[220]; z_docdir(dd); snprintf(dir, 160, "%s/Capturas", dd); z_mkdir(dir); u32 t[6]; z_time(t); snprintf(p, 220, "%s/captura-%04u%02u%02u-%02u%02u%02u.png", dir, t[5], t[4], t[3], t[2], t[1], t[0]);
    int e = z_png_save(p, px, w, h); free(px); z_sync(); if (e) snprintf(toast, 160, "%s: %s", _("No se pudo guardar la captura"), z_errstr(e)); else snprintf(toast, 160, "%s: %s", _("Captura guardada en Capturas"), strrchr(p, '/') + 1); toast_until = z_ms() + 5000;
    char m[300]; snprintf(m, 300, "[prueba] captura %s %dx%d: %d", p, w, h, e); z_log(m); }
/* ventanas de la red y de la impresora (al pulsar sus iconos) */
static zwin *nwin, *pwin; static u64 nwin_closed, pwin_closed;
static void draw_net(void) { zwin *w = nwin; g_clear(w, TH.panel1); g_frame(w, 0, 0, w->w, w->h, TH.acc); znet ni; int r = z_netinfo(&ni); char a[16], b[200]; int y = 12;
    g_textb(w, 14, y, _("Red"), 0xFFFFFF); y += 26; static const char *ST[] = {"No hay tarjeta de red compatible", "Sin cable", "Esperando una dirección (DHCP)", "Conectada"};
    g_circle(w, 20, y + 7, 5, net_st == 3 ? 0x4ADE80 : net_st == 2 ? 0xFBBF24 : 0xF87171, 1); g_text(w, 32, y, _(ST[net_st]), 0xFFFFFF); y += 24; if (r) { zw_present(w); return; }
    #define LN(k, v) do { g_text(w, 14, y, _(k), 0x9CA3AF); g_text(w, 160, y, v, 0xFFFFFF); y += 20; } while (0)
    snprintf(b, 200, "%s (%s)", ni.name, ni.kind); LN("Conexión", b); if (ni.dhcp) { z_fmt_ip(ni.ip, a); LN("Dirección IP", a); z_fmt_ip(ni.mask, a); LN("Máscara", a); z_fmt_ip(ni.gw, a); LN("Puerta de enlace", a); z_fmt_ip(ni.dns, a); LN("DNS", a); }
    else if (ni.local) { z_fmt_ip(ni.ip, a); LN("Dirección local", a); LN("Tipo", _("cable directo, sin router")); }
    snprintf(b, 200, "%02x:%02x:%02x:%02x:%02x:%02x", ni.mac[0], ni.mac[1], ni.mac[2], ni.mac[3], ni.mac[4], ni.mac[5]); LN("MAC", b); snprintf(b, 200, "%lu / %lu", (unsigned long)ni.rxp, (unsigned long)ni.txp); LN("Paquetes rx/tx", b);
    #undef LN
    zw_present(w); }
static const char *job_state(int st) { return st == 0 ? "Preparando" : st == 1 ? "Enviando" : st == 2 ? "Enviado" : st == 4 ? "Cancelado" : "Error"; }
static char jname[6][64]; static int njv, jst[6], jobs_y0; /* trabajos que se ven en la ventana, para Cancelar */
#define PWW 470
static void draw_prn(void) { zwin *w = pwin; g_clear(w, TH.panel1); g_frame(w, 0, 0, w->w, w->h, TH.acc); int y = 12; char b[300]; g_textb(w, 14, y, _("Impresoras"), 0xFFFFFF); y += 24;
    zprinter pr[12]; int np = zp_printers(pr, 12); zusbprn ul[8]; int nu = zp_usb_list(ul, 8); if (!np) { g_text(w, 14, y, _("Todavía no hay ninguna."), 0x9CA3AF); y += 20; }
    for (int i = 0; i < np && i < 4; i++) { int usb = !strncmp(pr[i].uri, "usb://", 6), on = !usb; for (int k = 0; usb && k < nu; k++) if (!strcmp(ul[k].id, pr[i].uri + 6)) on = 1;
        g_circle(w, 20, y + 7, 5, on ? 0x4ADE80 : 0x6B7280, 1); snprintf(b, 300, "%.40s%s", pr[i].name, pr[i].def ? " *" : ""); zw_clip(w, 32, y, 222, 18); g_text(w, 32, y, b, 0xFFFFFF); zw_noclip(w); g_text(w, 262, y, usb ? (on ? _("USB, conectada") : _("USB, no conectada")) : _("En red"), 0x9CA3AF); y += 20; }
    y += 6; g_rect(w, 10, y, w->w - 20, 1, 0x4B5563); y += 8; g_textb(w, 14, y, _("Trabajos de impresión"), 0xFFFFFF); y += 24; jobs_y0 = y - 3; char j[6][400]; int nj = read_jobs(j, 6); njv = nj; for (int i = 0; i < nj; i++) { strcpy(jname[i], rj_name[i]); jst[i] = atoi(j[i]); } if (!nj) { g_text(w, 14, y, _("No hay trabajos recientes."), 0x9CA3AF); y += 20; }
    for (int i = 0; i < nj; i++) { char *f[6] = {0}; char *p = j[i]; for (int k = 0; k < 6 && p; k++) { f[k] = p; p = strchr(p, '|'); if (p) *p++ = 0; } if (!f[4]) continue; int st = atoi(f[0]); char *nl = strchr(f[5] ? f[5] : f[4], '\n'); if (nl) *nl = 0;
        u32 c = st == 3 ? 0xF87171 : st == 2 ? 0x4ADE80 : st == 4 ? 0x9CA3AF : 0x38BDF8; if (st < 2) { ui_button(w, w->w - 90, y - 3, 80, 22, "Cancelar", 0); } g_text(w, 14, y, f[1], 0x9CA3AF); zw_clip(w, 64, y, 200, 18); g_text(w, 64, y, f[3][0] ? f[3] : "-", 0xFFFFFF); zw_noclip(w); long kb = atoi(f[4]) / 1024; if (st == 3 && f[5] && atoi(f[5])) snprintf(b, 300, "%s: %s", _(job_state(st)), z_errstr(atoi(f[5]))); else if (st == 2) snprintf(b, 300, "%s (%ld KB)", _(job_state(st)), kb); else snprintf(b, 300, "%s", _(job_state(st))); g_text(w, 270, y, b, c); y += 20; }
    ui_button(w, 14, w->h - 36, 150, 26, "Abrir Impresoras", 0); ui_button(w, w->w - 134, w->h - 36, 120, 26, "Borrar lista", 0); zw_present(w); }
static int pop_y(int h) { return TH.panel_top ? PH + 2 : (int)si.scr_h - PH - h - 2; }
static void close_net(void) { if (nwin) { zw_close(nwin); nwin = 0; nwin_closed = z_ms(); } }
static void close_prn(void) { if (pwin) { zw_close(pwin); pwin = 0; pwin_closed = z_ms(); } }
static void open_net(void) { int h = 230; nwin = zw_new("Red", MIN(icons_x(), (int)si.scr_w - 354), pop_y(h), 350, h, WF_POPUP); draw_net(); z_log("[prueba] ventana de red"); }
static void open_prn(void) { int h = 330; pwin = zw_new("Impresoras", MIN(icons_x(), (int)si.scr_w - PWW - 4), pop_y(h), PWW, h, WF_POPUP); draw_prn(); z_log("[prueba] ventana de impresion"); }
static void prn_click(int x, int y) { if (y < pwin->h - 40) { if (x < pwin->w - 90 || y < jobs_y0) return; int i = (y - jobs_y0) / 20; if (i >= 0 && i < njv && jst[i] < 2) { z_mkdir("/rd/tmp/IMPRESION-CANCELAR"); char f[140]; snprintf(f, 140, "/rd/tmp/IMPRESION-CANCELAR/%s", jname[i]); z_writefile(f, "1", 1); char m[200]; snprintf(m, 200, "[prueba] cancelar trabajo %s", jname[i]); z_log(m); draw_prn(); } return; } if (x < 170) { close_prn(); launch("impresoras", 0); } else if (x >= pwin->w - 134) { zdirent e; char f[120]; while (!z_readdir("/rd/tmp/IMPRESION", 0, &e)) { snprintf(f, 120, "/rd/tmp/IMPRESION/%s", e.name); if (z_unlink(f)) break; } poll_status(); draw_prn(); draw_bar(); } }
/* calendario del reloj */
static int cy_, cm_;
static void draw_cal(void) { zwin *w = cal; g_clear(w, TH.panel1); g_frame(w, 0, 0, w->w, w->h, TH.acc); char b[48]; snprintf(b, 48, "%s %d", z_month(cm_), cy_); g_textb(w, 40, 10, b, 0xFFFFFF); g_text(w, 10, 10, "<", 0xFFFFFF); g_text(w, w->w - 18, 10, ">", 0xFFFFFF); static const char *D[] = {"lun", "mar", "mie", "jue", "vie", "sab", "dom"}; for (int i = 0; i < 7; i++) { char s[4]; strncpy0(s, _(D[i]), 3); g_text(w, 10 + i * 34, 36, s, 0x9CA3AF); } u32 t[6]; z_time(t); static const u8 dm[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}; int n = dm[cm_ - 1] + (cm_ == 2 && cy_ % 4 == 0), f = z_weekday(cy_, cm_, 1); for (int d = 1; d <= n; d++) { int c = f + d - 1, x = 10 + (c % 7) * 34, y = 58 + (c / 7) * 24; if (d == (int)t[3] && cm_ == (int)t[4] && cy_ == (int)t[5]) g_round(w, x - 4, y - 2, 28, 20, 4, TH.acc); snprintf(b, 8, "%2d", d); g_text(w, x, y, b, 0xFFFFFF); } zw_present(w); }
static void open_cal(void) { u32 t[6]; z_time(t); cy_ = t[5]; cm_ = t[4]; cal = zw_new("Calendario", si.scr_w - 252, TH.panel_top ? PH + 2 : (int)si.scr_h - PH - 210, 248, 206, WF_POPUP); draw_cal(); }
static void close_cal(void) { if (cal) { zw_close(cal); cal = 0; cal_closed = z_ms(); } }
/* menu Inicio: familias a la izquierda y, al pasar por encima, su subpersiana con los programas */
#define GW (112 + (S_ - 1) * 56)
#define GH (84 + (S_ - 1) * 16)
#define MW1 (230 * S_)
#define MW2 (TH.menu_grid ? 3 * GW + 8 : 250 * S_)
static int fsel = -1, mh_, mlast_x, fpend = -1; static u64 fpend_t;
static int menu_h(void) { return 42 + (z_nfams + 3) * MI + 14; }
static int fam_apps(int f, int *o) { int n = 0; for (int k = 0; k < nvis; k++) if (z_apps[vis[k]].fam == f) o[n++] = k; return n; }
static int sub_y0(int f, int n) { int y = 42 + f * MI, h = TH.menu_grid ? (n + 2) / 3 * GH : n * MI; return MAX(42, MIN(y, mh_ - 6 - h)); }
static int menu_at(int x, int y) { if (y < 42) return -1; if (x < MW1) { int r = (y - 42) / MI; if (r < z_nfams) return r; int q = (y - 42 - z_nfams * MI - 8) / MI; if (y >= 42 + z_nfams * MI + 8 && q >= 0 && q < 3) return 100 + q; return -1; } if (fsel < 0) return -1; int o[32], n = fam_apps(fsel, o), y0 = sub_y0(fsel, n), i; if (TH.menu_grid) { int c = (x - MW1 - 4) / GW, r = (y - y0) / GH; if (y < y0 || c < 0 || c > 2) return -1; i = r * 3 + c; } else { if (y < y0) return -1; i = (y - y0) / MI; } return i >= 0 && i < n ? 200 + o[i] : -1; }
static void draw_menu(void) { zwin *w = menu; g_clear(w, TH.panel1); g_frame(w, 0, 0, w->w, w->h, TH.acc); g_grad(w, 1, 1, w->w - 2, 38, TH.acc, TH.panel1); g_textb(w, 14, 12, "ZunzunOS", 0xFFFFFF); { const char *u = _(guest() ? "Invitado" : "Administrador"); int tw = g_textw(u); g_round(w, MW1 - tw - 22, 8, tw + 14, 24, 6, TH.panel2); g_text(w, MW1 - tw - 15, 12, u, 0xFFFFFF); }
    for (int f = 0; f < z_nfams; f++) { int y = 42 + f * MI; if (f == fsel) g_rect(w, 2, y, MW1 - 4, MI, TH.acc); else if (menu_hover == f) g_rect(w, 2, y, MW1 - 4, MI, TH.panel2); g_round(w, 12, y + 7, 14, 14, 3, z_famcol[f]); T_(w, 36, y + (MI - 16 * S_) / 2, _(z_fams[f]), 0xFFFFFF); g_text(w, MW1 - 20, y + (MI - 16) / 2, ">", 0xFFFFFF); }
    int sy = 42 + z_nfams * MI + 4; g_rect(w, 10, sy, MW1 - 20, 1, 0x4B5563); static const char *SY[] = {"Registro del sistema", "Reiniciar", "Apagar"}; for (int k = 0; k < 3; k++) { int y = sy + 4 + k * MI; if (menu_hover == 100 + k) g_rect(w, 2, y, MW1 - 4, MI, TH.acc); g_rect(w, 12, y + 7, 14, 14, 0x6B7280); T_(w, 36, y + (MI - 16 * S_) / 2, _(SY[k]), 0xFFFFFF); }
    if (fsel >= 0 && w->w > MW1) { g_rect(w, MW1, 1, 1, w->h - 2, TH.acc); int o[32], n = fam_apps(fsel, o), y0 = sub_y0(fsel, n); for (int i = 0; i < n; i++) { const zapp *a = &z_apps[vis[o[i]]]; int hv = menu_hover == 200 + o[i]; if (TH.menu_grid) { int x = MW1 + 4 + (i % 3) * GW, y = y0 + (i / 3) * GH, cx = x + GW / 2; if (hv) g_round(w, x + 2, y + 2, GW - 4, GH - 4, 5, TH.acc); g_round(w, cx - 19, y + 8, 38, 38, 7, a->col); g_text(w, cx - g_textw(a->sym) / 2, y + 19, a->sym, 0xFFFFFF); char t[16]; strncpy0(t, _(a->name), 14); int tw = g_textw(t) * S_; T_(w, cx - tw / 2, y + 54, t, 0xFFFFFF); } else { int y = y0 + i * MI; if (hv) g_rect(w, MW1 + 2, y, MW2 - 4, MI, TH.acc); g_rect(w, MW1 + 12, y + 7, 14, 14, a->col); T_(w, MW1 + 36, y + (MI - 16 * S_) / 2, _(a->name), 0xFFFFFF); } } }
    zw_present(w); }
static void close_menu(void) { if (menu) { zw_close(menu); menu = 0; menu_closed = z_ms(); menu_hover = -1; fsel = -1; draw_bar(); } }
static void open_menu(void) { mh_ = menu_h(); fsel = -1; fpend = -1; menu = zw_new("Inicio", 4, TH.panel_top ? PH + 2 : (int)si.scr_h - PH - mh_ - 2, MW1, mh_, WF_POPUP); draw_menu(); draw_bar(); }
static void set_fam(int f) { if (f == fsel) return; fsel = f; int ww = f >= 0 ? MW1 + MW2 : MW1; if (menu->w != ww) zw_resized(menu, ww, mh_); draw_menu(); }
static void menu_act(int i) { if (i < 0) return; if (i < z_nfams) { set_fam(i); return; } close_menu(); if (i >= 200) launch(z_apps[vis[i - 200]].bin, 0); else if (i == 100) z_spawn("/rd/bin/editor", "--dmesg"); else if (i == 101) { if (ui_confirm("Reiniciar", "¿Reiniciar el equipo?")) { diag(); z_reboot(); } } else if (ui_confirm("Apagar", "¿Apagar el equipo?")) { diag(); z_poweroff(); } }
static void app_ctx(int k, int sx, int sy) { static const char *it[] = {"Abrir", "Ocultar (desinstalar del menú)", "Desinstalar programas..."}; int r = ui_menu(sx, sy, it, guest() ? 2 : 3); if (r == 2) { launch("config", "programas"); return; } if (r == 0) launch(z_apps[vis[k]].bin, 0); else if (r == 1) { z_set_hidden(z_apps[vis[k]].bin, 1); visible(); draw_bg(); z_log("[prueba] icono oculto"); } }
static void desk_path(int k, char *o) { snprintf(o, 160, "%s/%s", DESK, DE[k].name); }
static void new_folder(void) { char n[64] = ""; if (!ui_prompt("Nueva carpeta", "Nombre:", n, 60) || !n[0]) return; char p[160]; snprintf(p, 160, "%s/%s", DESK, n); int e = z_mkdir(p); z_sync(); if (e) { ui_confirm("Error", z_errstr(e)); return; } char m[200]; snprintf(m, 200, "[prueba] carpeta creada en el escritorio: %s", n); z_log(m); desk_scan(); draw_bg(); }
static void new_doc(void) { char n[64] = ""; if (!ui_prompt("Nuevo documento de texto", "Nombre:", n, 56) || !n[0]) return; if (!z_ends(n, ".txt")) strcat(n, ".txt"); char p[160]; snprintf(p, 160, "%s/%s", DESK, n); int f = z_open(p, O_WRITE | O_CREATE); if (f >= 0) z_close(f); z_sync(); desk_scan(); draw_bg(); }
static void desk_ctx(int k, int sx, int sy) { static const char *it[] = {"Abrir", "Renombrar", "Mover a la papelera", "Copiar", "Cortar"}; int r = ui_menu(sx, sy, it, 5); char p[160]; desk_path(k, p); if (r == 3 || r == 4) { char c[200]; int l = snprintf(c, 200, "ZFILES1 %c\n%s\n", r == 4 ? 'X' : 'C', p); z_clip_set(c, l); return; } if (r == 0) z_open_path(p); else if (r == 1) { char n[64]; strncpy0(n, DE[k].name, 64); if (ui_prompt("Renombrar", "Nuevo nombre:", n, 60) && n[0]) { char q[160]; snprintf(q, 160, "%s/%s", DESK, n); z_rename(p, q); z_sync(); } } else if (r == 2) { z_trash(p); z_sync(); } desk_scan(); draw_bg(); }
static void tick_clock(void) { u32 t[6]; z_time(t); char a[24], b[40], n[48]; z_fmt_time(t, h24, secs, a, 24); z_fmt_date(t, dfmt, b, 40); if (b[0]) snprintf(n, 48, "%s  %s", a, b); else strcpy(n, a); if (strcmp(n, clk)) { strcpy(clk, n); draw_bar(); } }
static void make_bar(void) { if (bar) zw_close(bar); bar = zw_new("Panel", 0, bar_y(), si.scr_w, PH, WF_PANEL); }
static void power_cfg(void);
static void reload(void) { power_cfg(); z_theme_get(&TH); visible(); desk_scan(); load_wall(); load_clock(); close_menu(); close_cal(); make_bar(); clk[0] = 0; tick_clock(); draw_bg(); draw_bar(); }
static void set_lang(int l) { ztheme t; z_theme_get(&t); t.lang = l; z_theme_set(&t); z_cfg_set("idioma", l); char m[40]; snprintf(m, 40, "[prueba] idioma %d", l); z_log(m); }
/* ===== Sesiones: pantalla de inicio, PIN de 4 cifras, bloqueo e invitado ===== */
static zwin *lk; static int lst, unlocking, lsel, lfoc2; /* lsel: 0 administrador, 1 invitado (teclado); lfoc2: boton secundario con Tab */ static char pin[5], pin1[5], pin_old[5], lmsg[96]; static u64 lwait_t, lk_open_t;
enum { L_PICK, L_PIN, L_NEW1, L_NEW2, L_WAIT, L_OLD, L_CHG1, L_CHG2, L_CONT };
static int pin_state(void) { return lst == L_PIN || lst == L_NEW1 || lst == L_NEW2 || lst == L_OLD || lst == L_CHG1 || lst == L_CHG2; }
static void ctext(zwin *w, int cx, int y, const char *s, u32 c, int sc) { int tw = g_textw(s) * sc; if (sc > 1) g_text2(w, cx - tw / 2, y, s, c, sc); else g_text(w, cx - tw / 2, y, s, c); }
static void lbtn(zwin *w, int x, int y, int bw, int bh, const char *s, u32 bgc) { g_round(w, x, y, bw, bh, 8, bgc); ctext(w, x + bw / 2, y + bh / 2 - 8, s, 0xFFFFFF, 1); }
#define LCX ((int)lk->w / 2)
#define LCY ((int)lk->h / 2)
static const char *ltitle(void) { switch (lst) { case L_PIN: return unlocking ? "Pantalla bloqueada: introduce tu PIN" : "Introduce tu PIN"; case L_NEW1: return "Crea un PIN de 4 cifras para el administrador"; case L_NEW2: case L_CHG2: return "Repite el PIN"; case L_OLD: return "PIN actual"; case L_CHG1: return "PIN nuevo"; } return ""; }
static const char *lsecond(void) { return lst == L_NEW1 || lst == L_NEW2 ? "Ahora no" : lst >= L_OLD && lst <= L_CHG2 ? "Cancelar" : 0; }
static void draw_lock(void) {
    zwin *w = lk; int cx = LCX, cy = LCY; if (wall) draw_wall(w); else g_grad(w, 0, 0, w->w, w->h, TH.d1, TH.d2);
    u32 t[6]; z_time(t); char a[24], b[40]; z_fmt_time(t, h24, 0, a, 24); z_fmt_date(t, 1, b, 40); ctext(w, cx + 2, 42, a, 0, 4); ctext(w, cx, 40, a, 0xFFFFFF, 4); ctext(w, cx + 1, 111, b, 0, 1); ctext(w, cx, 110, b, 0xE5E7EB, 1);
    if (lst == L_PICK || lst == L_CONT) {
        g_round(w, cx - 250, cy - 170, 500, 330, 14, TH.panel1); ctext(w, cx, cy - 145, _(lst == L_PICK ? "¿Quién va a usar el equipo?" : "Pantalla bloqueada"), 0xFFFFFF, 1); if (lst == L_PICK) ctext(w, cx, cy + 130, _("Flechas o Tab para elegir, Intro para entrar"), 0x9CA3AF, 1);
        if (lst == L_CONT) { lbtn(w, cx - 100, cy - 40, 200, 80, _("Continuar"), TH.acc); }
        else for (int k = 0; k < 2; k++) { int x = cx - 220 + k * 240, y = cy - 100; if (k == lsel) g_round(w, x - 5, y - 5, 210, 210, 15, 0xFFFFFF); g_round(w, x, y, 200, 200, 12, k ? TH.panel2 : TH.acc); { u32 ic = k ? TH.panel2 : TH.acc; g_circle(w, x + 100, y + 70, 40, 0xFFFFFF, 1); g_circle(w, x + 100, y + 58, 14, ic, 1); g_round(w, x + 78, y + 76, 44, 22, 10, ic); if (!k) { g_circle(w, x + 128, y + 96, 11, TH.ui_acc2, 1); g_text(w, x + 125, y + 89, "*", 0xFFFFFF); } }
            ctext(w, x + 100, y + 128, _(k ? "Invitado" : "Administrador"), 0xFFFFFF, 1); int st = sess(); ctext(w, x + 100, y + 154, _(k ? "Se borra al salir" : (st & 16) && !(st & 8) ? "Sin PIN" : "Con PIN"), 0xCBD5E1, 1); }
    } else if (lst == L_WAIT) { g_round(w, cx - 250, cy - 60, 500, 120, 14, TH.panel1); ctext(w, cx, cy - 8, _("Esperando la memoria del sistema..."), 0xFFFFFF, 1); }
    else {
        g_round(w, cx - 200, cy - 230, 400, 470, 14, TH.panel1); ctext(w, cx, cy - 205, _(ltitle()), 0xFFFFFF, 1);
        for (int i = 0; i < 4; i++) g_circle(w, cx - 54 + i * 36, cy - 150, 10, 0xFFFFFF, i < (int)strlen(pin));
        static const char *KP[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "Borrar", "0", "Volver"};
        for (int i = 0; i < 12; i++) { int r = i / 3, c = i % 3; if (i == 11 && (unlocking && lst == L_PIN)) continue; lbtn(w, cx - 120 + c * 84, cy - 100 + r * 68, 72, 56, i == 9 || i == 11 ? _(KP[i]) : KP[i], i == 9 || i == 11 ? TH.panel2 : TH.inact); }
        if (lmsg[0]) ctext(w, cx, cy + 166, lmsg, 0xFCA5A5, 1);
        const char *s2 = lsecond(); if (s2) { if (lfoc2) g_round(w, cx - 94, cy + 182, 188, 44, 10, 0xFFFFFF); lbtn(w, cx - 90, cy + 186, 180, 36, _(s2), TH.panel2); }
    }
    zw_present(w);
}
/* Red local: su servicio corre durante la sesion del administrador (con la del invitado no, sus datos son otros) */
static void red_stop(void) { zproc p[64]; int n = z_procs(p, 64); for (int i = 0; i < n; i++) if (!strcmp(p[i].name, "redlocal")) z_kill(p[i].pid); }
static void red_start(void) { if (guest()) return; zproc p[64]; int n = z_procs(p, 64); for (int i = 0; i < n; i++) if (!strcmp(p[i].name, "redlocal")) return; z_spawn("/rd/bin/redlocal", "--servicio"); }
/* guardia de red: un programa sin permiso quiere salir a Internet; el nucleo espera la respuesta (60 s como mucho) */
static void guard_ask(void) { zask q; if (z_guard(3, &q, 0) != 1) return; char dest[80], b[200]; if (q.host[0]) strncpy0(dest, q.host, 80); else { char ip[20]; z_fmt_ip(q.ip, ip); if (q.port) snprintf(dest, 80, "%s:%u", ip, q.port); else snprintf(dest, 80, "%s", ip); }
    char lg[160]; snprintf(lg, 160, "[prueba] guardia pregunta: %s -> %s", q.name, dest); z_log(lg);
    int w = 560, h = 210; zwin *d = zw_new("Permiso de Internet", (si.scr_w - w) / 2, (si.scr_h - h) / 2, w, h, WF_DECOR); if (!d) { z_guard(4, (void *)(long)q.id, 3); return; }
    int r = 0; u64 t0 = z_ms(); zevent e; static const char *B[] = {"Permitir siempre", "Solo esta vez", "Bloquear"}; int bx[3], bw[3], x = w - 16; for (int i = 2; i >= 0; i--) { bw[i] = g_textw(_(B[i])) + 26; x -= bw[i]; bx[i] = x; x -= 8; }
    while (!r && z_ms() - t0 < 58000) { g_clear(d, UI_BG); snprintf(b, 200, _("%s quiere conectarse a Internet:"), z_app_name(q.name)); g_textb(d, 20, 20, b, UI_TEXT); g_text2(d, 20, 50, dest, UI_ACC, 2);
        g_text(d, 20, 96, _("Si no lo esperabas, bloquéalo."), UI_MUTED); g_text(d, 20, 114, _("Lo puedes cambiar en Configuración, Conexiones."), UI_MUTED);
        for (int i = 0; i < 3; i++) ui_button(d, bx[i], h - 50, bw[i], 32, B[i], i == 1 ? 2 : 0); zw_present(d);
        if (!z_event(&e, 500) || e.win != (u32)d->id) continue; if (e.type == EV_CLOSE || (e.type == EV_KEY && e.ch == 27)) r = 3;
        else if (e.type == EV_DOWN && e.y >= h - 50 && e.y < h - 18) for (int i = 0; i < 3; i++) if (e.x >= bx[i] && e.x < bx[i] + bw[i]) r = i == 0 ? 2 : i == 1 ? 1 : 4; }
    zw_close(d); if (!r) r = 3; z_guard(4, (void *)(long)q.id, r); if (r == 2 || r == 4) z_rules_save(); snprintf(lg, 160, "[prueba] guardia respuesta %d", r); z_log(lg); }
/* datos cifrados: se pide la frase al entrar el administrador (el invitado no usa los datos de la memoria) */
static void crypt_unlock(void) { int st = z_crypt(0, 0); if (!(st & 1) || (st & 2) || guest()) return; char p[200]; z_log("[prueba] cifrado pide la frase");
    for (int tries = 0; ; tries++) { if (!z_pass_dialog("Abrir tus datos", tries ? "Frase incorrecta. Vuelve a escribirla:" : "Tus datos están cifrados. Frase de paso:", p, 190)) { z_log("[prueba] cifrado sin abrir"); break; }
        long r = z_crypt(1, p); memset(p, 0, sizeof p); if (!r) { z_log("[prueba] cifrado abierto"); cfg_load(); reload(); break; } } }
static void lock_done(void) { zw_close(lk); lk = 0; unlocking = 0; cfg_load(); reload(); z_log("[zz] sesion: escritorio"); crypt_unlock(); z_recover_tmp("/sys", 6); z_rules_load(); red_start(); }
static void open_lock(int mode) { /* 0 inicio, 1 bloqueo, 2 cambiar PIN */
    pin[0] = lmsg[0] = 0; int st = sess(); unlocking = mode == 1;
    if (mode == 0) lst = L_PICK; else if (mode == 1) lst = (st & 7) == 1 && (st & 8) ? L_PIN : L_CONT; else lst = st & 8 ? L_OLD : L_CHG1;
    if (!lk) lk = zw_new("Sesion", 0, 0, si.scr_w, si.scr_h, WF_LOCK); lk_open_t = z_ms(); draw_lock();
    z_log(mode == 0 ? "[zz] sesion: pantalla de inicio" : mode == 1 ? "[zz] sesion: bloqueada" : "[zz] sesion: cambiar pin");
}
static void admin_enter(void) { int st = sess(); if (!(st & 16) && z_ms() - lwait_t < 30000) { lst = L_WAIT; return; } if (st & 8) lst = L_PIN; else { lst = L_NEW1; z_log("[zz] sesion: crear pin"); } }
static void pin_full(void) {
    int r; char m[96];
    switch (lst) {
    case L_PIN: r = unlocking ? z_sess(5, pin, 0) : z_sess(1, pin, 0); if (!r) { lock_done(); return; } if (r > 0) { snprintf(m, 96, _("Demasiados intentos. Espera %d s"), r); strcpy(lmsg, m); } else strcpy(lmsg, _("PIN incorrecto")); break;
    case L_NEW1: case L_CHG1: strcpy(pin1, pin); lst = lst == L_NEW1 ? L_NEW2 : L_CHG2; lmsg[0] = 0; break;
    case L_NEW2: case L_CHG2: if (strcmp(pin, pin1)) { strcpy(lmsg, _("Los PIN no coinciden")); lst = lst == L_NEW2 ? L_NEW1 : L_CHG1; break; }
        if (lst == L_NEW2) { z_sess(1, 0, 0); r = z_sess(4, 0, pin1); } else r = z_sess(4, pin_old, pin1);
        if (r) { snprintf(m, 96, "%s: %s", _("No se pudo guardar el PIN"), z_errstr(r)); strcpy(lmsg, m); lst = lst == L_NEW2 ? L_NEW1 : L_CHG1; break; }
        z_log("[zz] sesion: pin guardado"); lock_done(); return;
    case L_OLD: r = z_sess(5, pin, 0); if (r) { strcpy(lmsg, r > 0 ? _("Demasiados intentos") : _("PIN incorrecto")); break; } strcpy(pin_old, pin); lst = L_CHG1; lmsg[0] = 0; break;
    }
    pin[0] = 0;
}
static void pin_key(int ch) { int n = strlen(pin); if (ch >= '0' && ch <= '9' && n < 4) { pin[n] = ch; pin[n + 1] = 0; if (n == 3) { draw_lock(); pin_full(); } } else if ((ch == 8 || ch == 127) && n) pin[n - 1] = 0; }
static void lock_back(void) { pin[0] = lmsg[0] = 0; if (lst >= L_OLD && lst <= L_CHG2) lock_done(); else if (lst == L_PIN && unlocking) return; else lst = L_PICK; }
static void lock_second(void) { if (lst == L_NEW1 || lst == L_NEW2) { z_sess(1, 0, 0); lock_done(); } else lock_done(); }
static void lock_ev(zevent *e) {
    int cx = LCX, cy = LCY;
    if (e->type == EV_KEY && lst == L_PICK) { /* elegir usuario sin raton */
        if (e->key == K_LEFT || e->key == K_RIGHT || e->key == K_UP || e->key == K_DOWN || e->key == K_TAB) lsel = !lsel;
        else if (e->ch == '\n' || e->ch == 13 || e->ch == ' ') { if (lsel == 0) { lwait_t = z_ms(); admin_enter(); } else { z_sess(2, 0, 0); lock_done(); return; } }
        if (lk) draw_lock(); return; }
    if (e->type == EV_KEY && pin_state() && lsecond() && (e->key == K_TAB || (lfoc2 && (e->ch == '\n' || e->ch == 13)))) { if (e->key == K_TAB) lfoc2 = !lfoc2; else { lfoc2 = 0; lock_second(); return; } if (lk) draw_lock(); return; }
    if (e->type == EV_KEY) { if (pin_state()) { lfoc2 = 0; if (e->ch == 27) lock_back(); else pin_key(e->ch); } else if (lst == L_CONT && (e->ch == '\n' || e->ch == 13 || e->ch == ' ')) { lock_done(); return; } if (lk) draw_lock(); return; }
    if (e->type != EV_DOWN || z_ms() - lk_open_t < 700) return; int x = e->x, y = e->y; /* el clic que despierta la pantalla no cuenta como tecla */
    if (lst == L_PICK) { if (y >= cy - 100 && y < cy + 100) { if (x >= cx - 220 && x < cx - 20) { lwait_t = z_ms(); admin_enter(); } else if (x >= cx + 20 && x < cx + 220) { z_sess(2, 0, 0); lock_done(); return; } } }
    else if (lst == L_CONT) { if (x >= cx - 100 && x < cx + 100 && y >= cy - 40 && y < cy + 40) { lock_done(); return; } }
    else if (pin_state()) {
        for (int i = 0; i < 12; i++) { int r = i / 3, c = i % 3, bx = cx - 120 + c * 84, by = cy - 100 + r * 68; if (x < bx || x >= bx + 72 || y < by || y >= by + 56) continue; if (i == 9) pin_key(8); else if (i == 11) lock_back(); else pin_key(i == 10 ? '0' : '1' + i); break; }
        if (lk && lsecond() && x >= cx - 90 && x < cx + 90 && y >= cy + 186 && y < cy + 222) { lock_second(); return; }
    }
    if (lk) draw_lock();
}
static void lock_tick(void) { static u64 last; u64 now = z_ms(); if (now - last < 1000) return; last = now; if (lst == L_WAIT) { if ((sess() & 16) || now - lwait_t >= 30000) { cfg_load(); admin_enter(); if (lst == L_WAIT) return; if (!(sess() & 16)) { z_sess(1, 0, 0); lock_done(); return; } } } draw_lock(); }
static void logout(void) { close_menu(); red_stop(); z_sess(3, 0, 0); cfg_load(); reload(); open_lock(0); }
static void sess_menu(int sx, int sy) {
    int g = guest(); static const char *A[] = {"Bloquear", "Cambiar PIN", "Cerrar sesión"}, *G[] = {"Bloquear", "Cerrar sesión"};
    int r = ui_menu(sx, sy, g ? G : A, g ? 2 : 3); if (r < 0) return; if (g && r == 1) r = 2;
    if (r == 0) open_lock(1); else if (r == 1) open_lock(2); else if (r == 2) logout();
}
/* ===== Energia: salvapantallas, pantalla apagada y apagado por inactividad ===== */
static zwin *ss; static u32 *ssi[3]; static int ssw[3], ssh[3], nss, scr_off, ss_min = 10, off_min = 20, off_h = 4, pin_back = 1; static u64 ss_t0, ss_frame, ss_ask;
static void power_cfg(void) { ss_min = z_cfg_get("salvapantallas", 10); off_min = z_cfg_get("pantalla_off", 20); off_h = z_cfg_get("apagar_h", 4); pin_back = z_cfg_get("pin_al_volver", 1); if (z_cfg_get("salvapantallas_ya", 0)) { z_cfg_set("salvapantallas_ya", 0); ss_ask = z_ms() + 1500; } }
static void ss_draw(void) {
    zwin *w = ss; int W = w->w, H = w->h; u64 now = z_ms(), el = now - ss_t0; const u64 D = 20000; int i = nss ? (int)(el / D) % nss : 0; long t = (long)(el % D) * 1024 / D; /* 0..1024 */
    if (!nss) { g_clear(w, 0); } else {
        int iw = ssw[i], ih = ssh[i]; long s0 = MAX((long)W * 65536 / iw, (long)H * 65536 / ih), z = (i & 1) ? 1150 - 150 * t / 1024 : 1000 + 150 * t / 1024, sc = s0 * z / 1000; /* escala 16.16 */
        long vw = (long)W * 65536 / sc, vh = (long)H * 65536 / sc, ox = (iw - vw) * ((i & 1) ? t : 1024 - t) / 1024, oy = (ih - vh) / 2; long step = 65536L * 65536 / sc;
        int f = t < 40 ? (int)t * 256 / 40 : t > 984 ? (int)(1024 - t) * 256 / 40 : 256; /* fundido a negro entre imagenes */
        for (int y = 0; y < H; y++) { long sy = oy + ((long)y * step >> 16); if (sy >= ih) sy = ih - 1; const u32 *r = ssi[i] + sy * iw; u32 *o = w->px + (long)y * W; long sx = (long)ox << 16;
            if (f >= 256) for (int x = 0; x < W; x++, sx += step) o[x] = r[MIN(sx >> 16, iw - 1)];
            else for (int x = 0; x < W; x++, sx += step) { u32 c = r[MIN(sx >> 16, iw - 1)]; o[x] = (((c >> 16 & 255) * f >> 8) << 16) | (((c >> 8 & 255) * f >> 8) << 8) | ((c & 255) * f >> 8); } }
    }
    u32 tm[6]; z_time(tm); char a[24]; z_fmt_time(tm, h24, 0, a, 24); int k = (int)(tm[1] + tm[2] * 7) % 9, cx = W / 6 + (k % 3) * W / 3, cy = H / 6 + (k / 3) * H / 3; /* el reloj cambia de sitio cada minuto */
    ctext(w, cx + 2, cy + 2, a, 0, 3); ctext(w, cx, cy, a, 0xFFFFFF, 3); zw_present(w); ss_frame = now;
}
static void ss_start(void) {
    if (ss) return; static const char *IM[3] = {"/rd/res/fondo-amanecer.png", "/rd/res/fondo-selva.png", "/rd/res/fondo-noche.png"}; nss = 0;
    for (int i = 0; i < 3; i++) if (!z_img_load(IM[i], &ssi[nss], &ssw[nss], &ssh[nss])) nss++;
    close_menu(); ss = zw_new("Salvapantallas", 0, 0, si.scr_w, si.scr_h, WF_LOCK); ss_t0 = z_ms(); ss_draw(); z_log("[zz] salvapantallas");
}
static void ss_stop(void) {
    if (!ss) return; zw_close(ss); ss = 0; for (int i = 0; i < nss; i++) free(ssi[i]); nss = 0; if (scr_off) { z_screen(1); scr_off = 0; } z_log("[zz] salvapantallas: fin");
    int st = sess(); if (!lk && (st & 7) == 1 && (st & 8) && pin_back) open_lock(1);
}
static void power_tick(void) {
    u64 now = z_ms(); long idle = z_idle_ms();
    if (ss) { if (idle + 300 < (long)(now - ss_t0)) { ss_stop(); return; } if (!scr_off && now - ss_frame >= 70) ss_draw(); }
    if (ss_ask && now >= ss_ask) { ss_ask = 0; ss_start(); return; }
    if (!ss && ss_min && idle >= ss_min * 60000L) ss_start();
    if (!scr_off && off_min && idle >= off_min * 60000L) { if (!ss) ss_start(); z_screen(0); scr_off = 1; z_log("[zz] pantalla apagada"); }
    if (off_h && idle >= off_h * 3600000L) { z_log("[zz] apagado por inactividad"); diag(); z_sync(); z_poweroff(); }
}
int main(const char *a) {
    { zdirent d; strcpy(DESK, z_stat("/sys", &d) ? "/rd/tmp/ESCRITORIO" : "/sys/ESCRITORIO"); if (!z_stat("/rd/tmp", &d) || DESK[1] == 's') z_mkdir(DESK); } desk_scan(); z_sysinfo(&si); bg = zw_new("Escritorio", 0, 0, si.scr_w, si.scr_h, WF_DESKTOP); visible(); make_bar(); load_clock(); tick_clock(); draw_bg(); draw_bar(); z_log("[zz] escritorio listo"); zevent e; u64 t0 = z_ms(); int cfgd = cfg_load(); if (cfgd) reload(); open_lock(0);
    for (;;) { int got = z_event(&e, ss ? 40 : menu ? 80 : secs ? 250 : 1000); if (menu && fpend >= 0 && z_ms() - fpend_t > 300) { if (menu_hover == fpend) set_fam(fpend); fpend = -1; } tick_clock(); if (lk) lock_tick(); power_tick(); guard_ask(); u64 now = z_ms(); if (shot_at && now >= shot_at) { shot_at = 0; screenshot(0); } if (toast[0] && now >= toast_until) { toast[0] = 0; draw_bar(); } { static u64 lp; if (now - lp > 1500) { lp = now; if (poll_status()) draw_bar(); if (nwin) draw_net(); if (pwin) draw_prn(); } } if (!cfgd && now - t0 < 30000 && (cfgd = cfg_load())) { strcpy(DESK, "/sys/ESCRITORIO"); z_mkdir(DESK); reload(); if (qs) draw_qs(); } { static u64 ls; if (now - ls > 3000) { ls = now; int o = nde; dent_t sv = nde ? DE[0] : (dent_t){0}; desk_scan(); if (o != nde || (nde && strcmp(sv.name, DE[0].name))) draw_bg(); } } if (!saved_diag && now - t0 > 30000) { saved_diag = 1; diag(); z_log("[zz] diagnostico guardado"); } if (!got) continue; int ty = TH.panel_top ? PH : 0;
        if (ss && e.win == (u32)ss->id) { if (e.type == EV_DOWN || e.type == EV_KEY) ss_stop(); continue; }
        if (lk && e.win == (u32)lk->id) { if (e.type == EV_REDRAW) draw_lock(); else lock_ev(&e); continue; }
        if (e.type == EV_REDRAW) { reload(); if (lk) draw_lock(); continue; } if (e.type == EV_WINLIST) { draw_bar(); continue; }
        if (qs && e.win == (u32)qs->id) { if (e.type == EV_DOWN) { qs_drag = 1; qs_set(e.x, e.y); } else if (e.type == EV_MOVE && qs_drag) qs_set(e.x, e.y); else if (e.type == EV_UP) qs_drag = 0; else if (e.type == EV_BLUR || (e.type == EV_KEY && e.ch == 27)) close_qs(); }
        else if (nwin && e.win == (u32)nwin->id) { if (e.type == EV_BLUR || (e.type == EV_KEY && e.ch == 27)) close_net(); }
        else if (pwin && e.win == (u32)pwin->id) { if (e.type == EV_DOWN) prn_click(e.x, e.y); else if (e.type == EV_BLUR || (e.type == EV_KEY && e.ch == 27)) close_prn(); }
        else if (cal && e.win == (u32)cal->id) { if (e.type == EV_DOWN && e.y < 30) { if (e.x < 30) { if (--cm_ < 1) { cm_ = 12; cy_--; } } else if (e.x > cal->w - 30) { if (++cm_ > 12) { cm_ = 1; cy_++; } } draw_cal(); } else if (e.type == EV_BLUR || (e.type == EV_KEY && e.ch == 27)) close_cal(); }
        else if (menu && e.win == (u32)menu->id) { if (e.type == EV_MOVE) { int h = menu_at(e.x, e.y); if (h >= 0 && h < z_nfams && h != fsel) { if (fsel >= 0 && e.x > mlast_x) { fpend = h; fpend_t = now; } else { set_fam(h); fpend = -1; } } else if (h == fsel || h >= 200) fpend = -1; else if (h >= 100 && h < 200 && fsel >= 0) { set_fam(-1); fpend = -1; } mlast_x = e.x; if (h != menu_hover) { menu_hover = h; draw_menu(); } } else if (e.type == EV_DOWN) { if (e.y < 42 && e.x >= MW1 - 130 && e.x < MW1) { int sx, sy; zw_screen(menu, e.x, 36, &sx, &sy); close_menu(); sess_menu(sx, sy); continue; } int i = menu_at(e.x, e.y); if ((e.btn & 2) && i >= 200) { int sx, sy; zw_screen(menu, e.x, e.y, &sx, &sy); close_menu(); app_ctx(i - 200, sx, sy); } else menu_act(i); } else if (e.type == EV_BLUR) close_menu(); else if (e.type == EV_KEY && e.ch == 27) close_menu(); }
        else if (e.win == (u32)bar->id && e.type == EV_KEY && e.key == 0x46) screenshot(!!(e.mods & M_ALT));
        /* atajos del sistema (el gestor de ventanas los manda a la barra): Win, Win+E, Win+L, Ctrl+Mayus+Esc */
        else if (e.win == (u32)bar->id && e.type == EV_KEY && e.key >= 0x100 && e.key <= 0x103) { if (e.key == 0x100) { if (menu) close_menu(); else open_menu(); } else if (e.key == 0x101) launch("files", ""); else if (e.key == 0x102) open_lock(1); else launch("sysmon", ""); char lg[48]; snprintf(lg, 48, "[prueba] atajo %x", e.key); z_log(lg); }
        else if (e.win == (u32)bar->id && e.type == EV_DOWN) { int tx = tray_x(); if (e.x < 8 + 96 * S_) { if (menu) close_menu(); else if (now - menu_closed > 200) open_menu(); } else if (e.x >= tx && e.x < tx + TRAY - 8) { if (qs) close_qs(); else if (now - qs_closed > 200) { qs = zw_new("Ajustes rápidos", MIN(tx, (int)si.scr_w - QW - 4), TH.panel_top ? PH + 2 : (int)si.scr_h - PH - QH - 2, QW, QH, WF_POPUP); draw_qs(); draw_bar(); } } else if (e.x >= icons_x() && e.x < icons_x() + ICW) { if (nwin) close_net(); else if (now - nwin_closed > 200) open_net(); } else if (bat_ok && e.x >= icons_x() + 2 * ICW + 4 && e.x < tray_x() - 4) { char b[120]; snprintf(toast, 160, "%s", bat_text(b)); toast_until = now + 5000; draw_bar(); } else if (prn_st && e.x >= icons_x() + ICW + 4 && e.x < icons_x() + 2 * ICW + 4) { if (pwin) close_prn(); else if (now - pwin_closed > 200) open_prn(); } else if (e.x >= tx + TRAY && e.x < tx + TRAY + FLAGW) set_lang(!TH.lang); else if (e.x >= bar->w - clock_w() - 16) { if (cal) close_cal(); else if (now - cal_closed > 200) open_cal(); } else for (int i = 0; i < nw; i++) if (e.x >= tb_x[i] && e.x < tb_x[i] + tb_w) { if ((wl[i].state & 0x100) && !(wl[i].state & 1)) zw_op(wl[i].id, 2, 0, 0); else zw_op(wl[i].id, 1, 0, 0); } }
        else if (e.win == (u32)bg->id && e.type == EV_DOWN) { int k = TH.show_icons ? icon_at(e.x, e.y) : -1; (void)ty;
            int hf = TH.show_icons && !(e.btn & 2) ? head_at(e.x, e.y) : -1;
            if (hf >= 0) { u32 bit = hf >= z_nfams ? 1u << 31 : 1u << hf; fam_open ^= bit; icon_sel = last_icon = -1; draw_bg(); char lg[80]; snprintf(lg, 80, "[prueba] escritorio categoria %d %s", hf, fam_open & bit ? "abierta" : "cerrada"); z_log(lg); continue; }
            if (e.btn & 2) { int sx = e.x, sy = e.y; if (k >= 0 && IC[k].kind == 0) app_ctx(IC[k].idx, sx, sy); else if (k >= 0) desk_ctx(IC[k].idx, sx, sy); else { static const char *it[] = {"Nueva carpeta", "Nuevo documento de texto", "Abrir la carpeta Escritorio", "Cambiar el fondo", "Configuración", "Papelera", "Capturar pantalla", "Pegar"}, *ig[] = {"Nueva carpeta", "Nuevo documento de texto", "Abrir la carpeta Escritorio", "Papelera", "Capturar pantalla", "Pegar"}; int gu = guest(); int r = ui_menu(sx, sy, gu ? ig : it, gu ? 6 : 8); if (gu && r >= 3) r = r == 3 ? 5 : r == 4 ? 6 : 7; if (r == 6) { shot_at = z_ms() + 700; continue; } if (r == 7) { char a2[200]; snprintf(a2, 200, "--pegar %s", DESK); z_spawn("/rd/bin/files", a2); continue; } if (r == 0) new_folder(); else if (r == 1) new_doc(); else if (r == 2) z_spawn("/rd/bin/files", DESK); else if (r == 3) launch("config", "fondo"); else if (r == 4) launch("config", 0); else if (r == 5) launch("papelera", 0); } continue; }
            if (k >= 0 && k == last_icon && now - last_click < 500) { if (IC[k].kind == 0) launch(z_apps[vis[IC[k].idx]].bin, 0); else { char p[160]; desk_path(IC[k].idx, p); z_open_path(p); } last_icon = -1; } else { last_icon = k; last_click = now; } if (k != icon_sel) { icon_sel = k; draw_bg(); } }
    }
}
