/* Impresoras: anadir (USB o red), elegir lenguaje, predeterminada y pagina de prueba */
#include "z.h"
static zwin *W; static zprinter P[12]; static int n, sel = -1; static char msg[200];
static const char *LANGS[] = {"pdf", "pwg", "ps", "pcl", "txt", "qpdl"};
#define NLANG 6
static const char *LNAME(const char *l) { return !strcmp(l, "pdf") ? "PDF" : !strcmp(l, "pwg") ? "IPP Everywhere (PWG)" : !strcmp(l, "ps") ? "PostScript" : !strcmp(l, "pcl") ? "PCL 5" : !strcmp(l, "qpdl") ? "Samsung SPL2 (QPDL)" : _("Texto (agujas/tickets)"); }
static const char *INFO[] = {
    "Funcionan con ZunzunOS (pulsa Compatibles para ver marcas y modelos):",
    "- De red o Wi-Fi con AirPrint / IPP Everywhere: casi todas",
    "  desde 2013 (HP, Brother, Canon, Epson, Kyocera, Ricoh...).",
    "- Laser con PCL 5/6 o PostScript, por USB o por red (9100):",
    "  HP LaserJet, Brother (PCL o BR-Script), Kyocera, Lexmark,",
    "  Ricoh, Xerox, OKI.",
    "- Impresoras de agujas o de tickets en modo texto.",
    "- Samsung SPL2 por USB (ML-1640, ML-2240): en pruebas.",
    "No funcionan (lenguaje propio sin documentar, 'GDI'):",
    "- HP LaserJet 1018/1020/P1102, Canon CAPT (LBP),",
    "  Brother HL-1110, Samsung ML-2160 y Xpress.",
};
/* lista orientativa de modelos: 1 funciona, 0 no funciona */
typedef struct { const char *marca, *modelo, *conex, *lenguaje; int ok; } compat_t;
static const compat_t CP[] = {
    {"HP", "LaserJet Pro M404dn / M404dw / M428fdw", "USB y red", "PCL 5 / PostScript / IPP", 1},
    {"HP", "LaserJet Pro M203dw / M227fdw", "USB y red", "PCL 5 / PostScript / IPP", 1},
    {"HP", "LaserJet P2035 / P2055dn / P3015", "USB y red", "PCL 5", 1},
    {"HP", "LaserJet 1320 / 2015 / 4250", "USB y red", "PCL 5 / PostScript", 1},
    {"HP", "Color LaserJet Pro M255dw / M454dw / M479fdw", "Red / Wi-Fi", "IPP Everywhere", 1},
    {"HP", "LaserJet Pro M15w / M28w", "Red / Wi-Fi", "AirPrint (IPP)", 1},
    {"HP", "DeskJet 2700 / 4100, ENVY 6000 / 6400", "Red / Wi-Fi", "AirPrint (IPP)", 1},
    {"HP", "OfficeJet Pro 8020 / 9010 / 9020", "Red / Wi-Fi", "AirPrint (IPP)", 1},
    {"HP", "LaserJet 1018 / 1020 / 1022, P1005 / P1006 / P1102, M1132", "USB", "ZjStream (propio)", 0},
    {"Brother", "HL-L2350DW / HL-L2370DW / HL-L2390DW", "Red / Wi-Fi", "AirPrint (IPP)", 1},
    {"Brother", "DCP-L2550DW, MFC-L2710DW / MFC-L2750DW", "Red / Wi-Fi", "AirPrint (IPP)", 1},
    {"Brother", "HL-L5100DN / HL-L6200DW / HL-5450DN / HL-5250DN", "USB y red", "PCL / BR-Script", 1},
    {"Brother", "HL-L3270CDW, MFC-L3750CDW", "Red / Wi-Fi", "AirPrint (IPP)", 1},
    {"Brother", "HL-1110 / HL-1210W / HL-L2300D, DCP-1510", "USB", "GDI (propio)", 0},
    {"Canon", "PIXMA TS5150 / TS5350 / TS6350 / TS8350", "Red / Wi-Fi", "AirPrint (IPP)", 1},
    {"Canon", "PIXMA G3570 / G4570, MAXIFY GX6050", "Red / Wi-Fi", "AirPrint (IPP)", 1},
    {"Canon", "i-SENSYS LBP233dw / LBP623Cdw / MF443dw", "USB y red", "PCL / IPP", 1},
    {"Canon", "LBP2900 / LBP3010 / LBP6000 / LBP6030", "USB", "CAPT (propio)", 0},
    {"Epson", "EcoTank ET-2720 / ET-2810 / ET-2850 / ET-4800", "Red / Wi-Fi", "AirPrint (IPP)", 1},
    {"Epson", "Expression Home XP-2200 / XP-3200, WorkForce WF-2930", "Red / Wi-Fi", "AirPrint (IPP)", 1},
    {"Epson", "LX-350 / FX-890 / LQ-590 (agujas)", "USB", "Texto", 1},
    {"Epson", "TM-T20 / TM-T88 (tickets)", "USB y red", "Texto", 1},
    {"Epson", "Inyección de tinta solo por USB", "USB", "ESC/P-R (propio)", 0},
    {"Samsung", "ML-2851ND / ML-3310ND / ML-3710ND", "USB y red", "PCL / PostScript", 1},
    {"Samsung", "ML-1640 / ML-2240", "USB", "SPL2 / QPDL (en pruebas)", 1},
    {"Samsung", "ML-1660 / ML-1865 / ML-2160 / ML-2165", "USB", "SPL (propio)", 0},
    {"Samsung", "Xpress M2020 / M2070", "USB", "SPL (propio)", 0},
    {"Kyocera", "ECOSYS P2040dw / P2135d / P2235dn / M2540dn", "USB y red", "PCL / KPDL (PostScript)", 1},
    {"Kyocera", "ECOSYS P5026cdw, FS-1370DN", "USB y red", "PCL / KPDL (PostScript)", 1},
    {"Lexmark", "B2236dw / MB2236adw / MS310dn / MS421dn", "USB y red", "PCL / PostScript", 1},
    {"Lexmark", "C3224dw / CS421dn", "USB y red", "PCL / PostScript", 1},
    {"Xerox", "B210 / B230 / C230, Phaser 3260", "USB y red", "PCL / IPP", 1},
    {"Xerox", "Phaser 3020, WorkCentre 3025", "USB", "GDI (propio)", 0},
    {"Ricoh", "SP 230DNw / P 311 / SP C261DNw", "USB y red", "PCL / PostScript", 1},
    {"Ricoh", "SP 111 / SP 150", "USB", "GDI (propio)", 0},
    {"OKI", "B412dn / B432dn / C332dn", "USB y red", "PCL / PostScript", 1},
    {"Pantum", "P2502W", "Red / Wi-Fi", "AirPrint (IPP)", 1},
    {"Pantum", "P2500", "USB", "GDI (propio)", 0},
};
#define NCP ((int)(sizeof CP / sizeof CP[0]))
static void compat_win(void) {
    static const char *MK[] = {"Todas", "HP", "Brother", "Canon", "Epson", "Samsung", "Kyocera", "Lexmark", "Xerox", "Ricoh", "OKI", "Pantum"}; int nm = 12, mk = 0, top = 0;
    zsys sy; z_sysinfo(&sy); zwin *w = zw_new("Impresoras compatibles", -1, 0, MIN(980, (int)sy.scr_w - 40), MIN(600, (int)sy.scr_h - 80), WF_DECOR | WF_RESIZE); zevent e;
    for (;;) {
        g_clear(w, UI_WHITE); g_rect(w, 0, 0, 120, w->h, UI_PANEL); for (int i = 0; i < nm; i++) { if (i == mk) g_rect(w, 4, 8 + i * 28, 112, 26, UI_ACC); g_text(w, 14, 13 + i * 28, _(MK[i]), i == mk ? 0xFFFFFF : UI_TEXT); }
        int x0 = 132, y = 10; g_textb(w, x0, y, _("Modelo"), UI_TEXT); g_textb(w, w->w - 400, y, _("Conexión"), UI_TEXT); g_textb(w, w->w - 270, y, _("Lenguaje"), UI_TEXT); y += 24; int vis = (w->h - 100) / 24, k = 0;
        for (int i = 0; i < NCP; i++) { if (mk && strcmp(CP[i].marca, MK[mk])) continue; if (k++ < top) continue; if (y > w->h - 80) break; const compat_t *c = &CP[i];
            if (!c->ok) g_rect(w, x0 - 4, y - 2, w->w - x0, 22, 0xFEE2E2); g_round(w, x0, y + 3, 12, 12, 3, c->ok ? 0x16A34A : 0xDC2626); char t[160]; snprintf(t, 160, "%s %s", c->marca, _(c->modelo)); zw_clip(w, x0 + 18, y, w->w - 420 - x0 - 18, 22); g_text(w, x0 + 18, y + 2, t, UI_TEXT); zw_noclip(w); g_text(w, w->w - 400, y + 2, _(c->conex), UI_MUTED); g_text(w, w->w - 270, y + 2, _(c->lenguaje), c->ok ? UI_MUTED : 0xB91C1C); y += 24; }
        (void)vis; g_rect(w, 120, w->h - 64, w->w - 120, 64, UI_BG);
        g_text(w, x0, w->h - 58, _("Verde: debería funcionar. Rojo: no funciona (lenguaje propio sin documentar)."), UI_TEXT);
        g_text(w, x0, w->h - 38, _("Regla general: funciona cualquier impresora con AirPrint, IPP Everywhere, PCL 5 o PostScript."), UI_MUTED);
        g_text(w, x0, w->h - 20, _("Las que solo dicen 'GDI', 'host-based' o 'solo Windows' no funcionan."), UI_MUTED); zw_present(w);
        if (!z_event(&e, -1)) continue; if (e.type == EV_REDRAW) continue; if (e.win != (u32)w->id) continue; if (e.type == EV_CLOSE || (e.type == EV_KEY && e.ch == 27)) break; if (e.type == EV_RESIZE) { zw_resized(w, e.x, e.y); continue; }
        if (e.type == EV_DOWN && e.x < 120) { int i = (e.y - 8) / 28; if (i >= 0 && i < nm) { mk = i; top = 0; } } else if (e.type == EV_WHEEL) top = MAX(0, MIN(top - (int)e.key * 3, NCP - 1)); }
    zw_close(w); z_log("[prueba] lista de compatibles cerrada");
}
static void draw(void) {
    g_clear(W, UI_WHITE); g_rect(W, 0, 0, W->w, 40, UI_PANEL); static const char *B[] = {"Añadir USB", "Añadir de red", "Buscar en la red", "Página de prueba", "Predeterminada", "Quitar", "Compatibles"}; int x = 8; for (int i = 0; i < 7; i++) { int w = g_textw(_(B[i])) + 16; ui_button(W, x, 7, w, 26, B[i], 0); x += w + 4; }
    g_textb(W, 12, 52, _("Impresoras instaladas"), UI_TEXT); if (!n) { g_text(W, 12, 78, _("Todavía no hay ninguna."), UI_MUTED); g_text(W, 12, 98, _("Añade una con los botones de arriba."), UI_MUTED); }
    for (int i = 0; i < n; i++) { int y = 74 + i * 46; if (i == sel) g_rect(W, 0, y, W->w * 2 / 5, 44, UI_ACC2); g_rect(W, 12, y + 8, 22, 18, 0x334155); g_rect(W, 16, y + 4, 14, 6, 0xF8FAFC); g_text(W, 44, y + 4, P[i].name, UI_TEXT); char t[240]; snprintf(t, 240, "%s  %s", P[i].uri, P[i].def ? _("(predeterminada)") : ""); zw_clip(W, 44, y, W->w * 2 / 5 - 50, 44); g_text(W, 44, y + 22, t, UI_MUTED); zw_noclip(W); }
    int px = W->w * 2 / 5 + 20; g_rect(W, px - 8, 44, 1, W->h - 76, UI_LINE);
    if (sel >= 0 && sel < n) { zprinter *p = &P[sel]; g_textb(W, px, 52, _("Ajustes de la impresora"), UI_TEXT); int y = 80; char t[120];
        g_text(W, px, y + 5, _("Lenguaje"), UI_TEXT); snprintf(t, 120, "%s", LNAME(p->lang)); ui_button(W, px + 100, y, 230, 26, t, 0); y += 34;
        g_text(W, px, y + 5, _("Papel"), UI_TEXT); ui_button(W, px + 100, y, 112, 26, "A4", p->paper == 0 ? 2 : 0); ui_button(W, px + 218, y, 112, 26, "Carta (EE. UU.)", p->paper == 1 ? 2 : 0); y += 34;
        g_text(W, px, y + 5, _("Color"), UI_TEXT); ui_button(W, px + 100, y, 112, 26, "Color", p->color ? 2 : 0); ui_button(W, px + 218, y, 112, 26, "Escala de grises", p->color ? 0 : 2); y += 34;
        g_text(W, px, y + 5, _("Resolución"), UI_TEXT); ui_button(W, px + 100, y, 74, 26, "150", p->dpi == 150 ? 2 : 0); ui_button(W, px + 178, y, 74, 26, "300", p->dpi == 300 ? 2 : 0); ui_button(W, px + 256, y, 74, 26, "600", p->dpi == 600 ? 2 : 0); y += 40;
        if (p->model[0]) { snprintf(t, 120, "%s: %s", _("Modelo"), p->model); g_text(W, px, y, t, UI_MUTED); } }
    else for (int i = 0; i < (int)(sizeof INFO / sizeof INFO[0]); i++) g_text(W, px, 52 + i * 20, INFO[i], i == 0 || i == 8 ? UI_TEXT : UI_MUTED);
    g_rect(W, 0, W->h - 24, W->w, 24, UI_PANEL); g_text(W, 8, W->h - 20, msg, UI_TEXT); zw_present(W);
}
static void save(void) { int e = zp_save_printers(P, n); if (e) snprintf(msg, 200, "Error: %s", z_errstr(e)); }
static void add(const char *name, const char *uri, const char *lang, const char *model, int color) { if (n >= 12) return; zprinter *p = &P[n]; memset(p, 0, sizeof *p); strncpy0(p->name, name, 64); strncpy0(p->uri, uri, 160); strncpy0(p->lang, lang, 8); strncpy0(p->model, model ? model : "", 64); p->color = color; p->dpi = strcmp(lang, "qpdl") ? 300 : 600; p->def = n == 0; zsys s; (void)s; sel = n++; save(); char m[300]; snprintf(m, 300, "[prueba] impresora anadida %s %s %s", name, uri, lang); z_log(m); snprintf(msg, 200, "%s: %s", _("Impresora añadida"), name); }
static void field(const char *id, const char *k, char *o, int cap) { const char *p = strstr(id, k); o[0] = 0; if (!p) return; p += strlen(k); int i = 0; while (p[i] && p[i] != ';' && i < cap - 1) { o[i] = p[i]; i++; } o[i] = 0; }
static void add_usb(void) {
    zusbprn l[8]; int c = zp_usb_list(l, 8); if (!c) { strcpy(msg, _("No hay ninguna impresora USB conectada.")); return; }
    const char *it[8]; char lab[8][120]; for (int i = 0; i < c; i++) { char mf[48], md[64]; field(l[i].devid, "MFG:", mf, 48); if (!mf[0]) field(l[i].devid, "MANUFACTURER:", mf, 48); field(l[i].devid, "MDL:", md, 64); if (!md[0]) field(l[i].devid, "MODEL:", md, 64); snprintf(lab[i], 120, "%s %s (%s)%s", mf, md, l[i].id, zp_guess_lang(l[i].devid) ? "" : _(" - no compatible")); it[i] = lab[i]; }
    zwininfo wi; zw_info(W->id, &wi); int r = ui_menu(wi.x + 8, wi.y + 40, it, c); if (r < 0) return;
    const char *lang = zp_guess_lang(l[r].devid); char cmd[120]; field(l[r].devid, "CMD:", cmd, 120); if (!cmd[0]) field(l[r].devid, "COMMAND SET:", cmd, 120);
    if (!lang) { char q[300]; snprintf(q, 300, "%s (%s). %s", _("Esta impresora usa un lenguaje propio sin documentar"), cmd[0] ? cmd : "?", _("No se puede usar desde ZunzunOS.")); ui_confirm("Impresora no compatible", q); char m[300]; snprintf(m, 300, "[prueba] impresora usb no compatible: %s", l[r].devid); z_log(m); return; }
    char u[40]; snprintf(u, 40, "usb://%s", l[r].id); char md[64]; field(l[r].devid, "MDL:", md, 64); add(md[0] ? md : lab[r], u, lang, l[r].devid, 0);
}
static int probe(const char *host, int quiet) {
    char uri[160], model[64], fm[600], hb[80]; int color, port = 631; strncpy0(hb, host, 80); { char *c = strchr(hb, ':'); if (c) { *c = 0; port = atoi(c + 1); } } host = hb; snprintf(uri, 160, "%s:%d/ipp/print", host, port);
    int rawp = port >= 9100 && port <= 9199 ? port : 9100; /* host:91xx = puerto RAW (JetDirect) indicado */
    if (!quiet) { snprintf(msg, 200, "%s %s...", _("Preguntando a"), host); draw(); }
    if (port == rawp) { } else if (!zp_ipp_query(uri, model, 64, fm, 600, &color)) { const char *lang = strstr(fm, "application/pdf") ? "pdf" : strstr(fm, "image/pwg-raster") ? "pwg" : strstr(fm, "application/postscript") ? "ps" : strstr(fm, "vnd.hp-PCL") ? "pcl" : "pwg"; char u[180]; snprintf(u, 180, "ipp://%s", uri); add(model[0] ? model : host, u, lang, model, color); return 1; }
    u32 ip; if (z_parse_ip(host, &ip) && z_dns(host, &ip)) { if (!quiet) snprintf(msg, 200, "%s: %s", _("No se encuentra"), host); return 0; }
    int s = z_socket(1); int e = s < 0 ? s : z_connect(s, ip, rawp, quiet ? 400 : 4000); if (s >= 0) z_close(s);
    if (e) { if (!quiet) snprintf(msg, 200, "%s %s", host, _("no responde ni por IPP (631) ni por el puerto 9100.")); return 0; }
    if (quiet) { char u[180]; snprintf(u, 180, "socket://%s:%d", host, rawp); add(host, u, "pcl", "", 0); return 1; }
    static const char *it[] = {"PCL 5 (láser HP, Brother, Kyocera...)", "PostScript", "PDF", "Texto (agujas/tickets)"}; static const char *lg[] = {"pcl", "ps", "pdf", "txt"}; zwininfo wi; zw_info(W->id, &wi); int r = ui_menu(wi.x + 120, wi.y + 40, it, 4); if (r < 0) return 0;
    char u[180]; snprintf(u, 180, "socket://%s:%d", host, rawp); add(host, u, lg[r], "", 0); return 1;
}
static void scan(void) {
    znet ni; if (z_netinfo(&ni) || !ni.ip) { strcpy(msg, _("No hay conexión de red.")); return; } u32 base = ni.ip & 0x00FFFFFFu; int found = 0;
    for (int h = 1; h < 255; h++) { u32 ip = base | (u32)h << 24; if (ip == ni.ip) continue; char a[20]; z_fmt_ip(ip, a); if (h % 8 == 1) { snprintf(msg, 200, "%s %s...  (%d %s)", _("Buscando"), a, found, _("encontradas")); draw(); zevent e; while (z_event(&e, 0)) if (e.type == EV_KEY && e.ch == 27) { strcpy(msg, _("Búsqueda cancelada")); return; } }
        int ports[2] = {631, 9100}; for (int k = 0; k < 2; k++) { int s = z_socket(1); if (s < 0) break; int e = z_connect(s, ip, ports[k], 150); z_close(s); if (!e) { if (probe(a, 1)) found++; break; } } }
    snprintf(msg, 200, "%d %s", found, _("impresoras encontradas en la red")); }
static void test_page(zpdoc *d, const zpset *s, void *ctx) {
    zprinter *p = ctx; double W_ = zp_pw(d), m = 50; zp_page(d); zp_rect(d, m, m, W_ - 2 * m, 60, 0x0F766E, ZP_NONE, 0); zp_text(d, m + 16, m + 40, ZPF_SANSB, 26, 0xFFFFFF, "ZunzunOS");
    zp_text(d, m, 150, ZPF_SANSB, 18, 0, _("Página de prueba")); char t[300]; snprintf(t, 300, "%s: %s", _("Impresora"), p->name); zp_text(d, m, 180, ZPF_SANS, 11, 0, t); snprintf(t, 300, "%s: %s   %s: %s   %s: %d ppp", _("Conexión"), p->uri, _("Lenguaje"), LNAME(p->lang), _("Resolución"), p->dpi); zp_text(d, m, 198, ZPF_SANS, 11, 0, t);
    static const u32 C[] = {0x00AEEF, 0xEC008C, 0xFFF200, 0x000000, 0xE11D48, 0x16A34A, 0x2563EB, 0x9CA3AF}; for (int i = 0; i < 8; i++) zp_rect(d, m + i * 60, 230, 54, 54, C[i], ZP_NONE, 0);
    for (int i = 0; i <= 10; i++) { int g = 255 - i * 25; zp_rect(d, m + i * 44, 300, 44, 30, g << 16 | g << 8 | g, ZP_NONE, 0); } zp_rect(d, m, 300, 484, 30, ZP_NONE, 0, 0.5);
    double y = 370; static const int F[] = {ZPF_SANS, ZPF_SANSB, ZPF_SERIF, ZPF_SERIFB, ZPF_MONO}; for (int i = 0; i < 5; i++) { zp_text(d, m, y, F[i], 12, 0, "El veloz murciélago hindú comía feliz cardillo y kiwi. 0123456789 €"); y += 22; }
    for (int i = 0; i < 6; i++) zp_line(d, m, 500 + i * 12, W_ - m, 500 + i * 12 + 40, 0x334155, 0.5 + i * 0.5);
    zp_text(d, m, zp_ph(d) - 40, ZPF_SANS, 9, 0x6B7280, _("Si ves los textos nítidos y las barras de color, la impresora está bien configurada."));
}
static void do_test(void) { if (sel < 0) { strcpy(msg, _("Elige primero una impresora.")); return; } zpset s = {0}; s.paper = P[sel].paper; s.pw = s.paper ? 612 : 595.28; s.ph = s.paper ? 792 : 841.89; s.copies = 1; s.color = P[sel].color; s.dpi = P[sel].dpi; zpdoc *d = zp_new(&s); test_page(d, &s, &P[sel]); snprintf(msg, 200, "%s", _("Enviando a la impresora...")); draw(); int e = zp_print(d, &P[sel], &s, _("Página de prueba")); zp_free(d); if (e) snprintf(msg, 200, "%s: %s", _("No se pudo imprimir"), e <= -1000 ? _("la impresora rechazó el trabajo") : z_errstr(e)); else strcpy(msg, _("Página de prueba enviada")); }
int main(const char *a) {
    W = zw_new("Impresoras", -1, 0, 900, 520, WF_DECOR | WF_RESIZE); n = zp_printers(P, 12); if (n) sel = 0; zusbprn l[8]; int c = zp_usb_list(l, 8); snprintf(msg, 200, "%d %s, %d %s", n, _("instaladas"), c, _("impresoras USB conectadas")); draw(); zevent e;
    if (a[0] == '-' && a[1] == '-') { if (!strncmp(a, "--red ", 6)) probe(a + 6, 0); else if (!strcmp(a, "--usb")) add_usb(); draw(); }
    for (;;) { if (!z_event(&e, -1)) continue; if (e.type == EV_REDRAW) { draw(); continue; } if (e.win != (u32)W->id) continue; if (e.type == EV_CLOSE) return 0; if (e.type == EV_RESIZE) { zw_resized(W, e.x, e.y); draw(); continue; } if (e.type != EV_DOWN) continue;
        if (e.y < 40) { static const char *B[] = {"Añadir USB", "Añadir de red", "Buscar en la red", "Página de prueba", "Predeterminada", "Quitar", "Compatibles"}; int x = 8, b = -1; for (int i = 0; i < 7; i++) { int w = g_textw(_(B[i])) + 16; if (e.x >= x && e.x < x + w) b = i; x += w + 4; }
            if (b == 6) compat_win(); else if (b == 0) add_usb(); else if (b == 1) { char h[80] = ""; if (ui_prompt("Añadir de red", "Dirección IP o nombre de la impresora:", h, 78) && h[0]) probe(h, 0); } else if (b == 2) scan(); else if (b == 3) do_test();
            else if (b == 4 && sel >= 0) { for (int i = 0; i < n; i++) P[i].def = i == sel; save(); } else if (b == 5 && sel >= 0 && ui_confirm("Quitar", "¿Quitar esta impresora?")) { memmove(&P[sel], &P[sel + 1], (n - sel - 1) * sizeof *P); n--; sel = n ? 0 : -1; if (n && !P[0].def) { int any = 0; for (int i = 0; i < n; i++) any |= P[i].def; if (!any) P[0].def = 1; } save(); } }
        else if (e.x < W->w * 2 / 5) { int i = (e.y - 74) / 46; sel = e.y >= 74 && i < n ? i : -1; }
        else if (sel >= 0) { zprinter *p = &P[sel]; int px = W->w * 2 / 5 + 20, y = 80; zevent *E = &e;
            #define IN(x0, y0, w0, h0) (E->x >= (x0) && E->x < (x0) + (w0) && E->y >= (y0) && E->y < (y0) + (h0))
            if (IN(px + 100, y, 230, 26)) { int k = 0; for (int i = 0; i < NLANG; i++) if (!strcmp(p->lang, LANGS[i])) k = i; strcpy(p->lang, LANGS[(k + 1) % NLANG]); if (!strcmp(p->lang, "qpdl")) p->dpi = 600; save(); }
            else if (IN(px + 100, y + 34, 230, 26)) { p->paper = e.x >= px + 218; save(); } else if (IN(px + 100, y + 68, 230, 26)) { p->color = e.x < px + 218; save(); }
            else if (IN(px + 100, y + 102, 230, 26)) { p->dpi = e.x < px + 178 ? 150 : e.x < px + 256 ? 300 : 600; save(); }
            #undef IN
        }
        draw(); }
}
