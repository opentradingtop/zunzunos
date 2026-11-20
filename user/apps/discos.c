/* Discos: ver unidades y formatear en distintos formatos */
#include "z.h"
static zwin *W; static zdisk D[16]; static int nd, sd; static zpart P[16]; static int np, sp = -1, fs; static char label[16] = "DATOS", sysd[16], msgs[120]; static int lfoc, lcur = 5, pct = -1;
static const char *FN[] = {"FAT32", "FAT16", "exFAT", "ext2"};
static const char *FD[] = {"Windows, macOS, Linux y ZunzunOS", "Unidades de hasta 2 GB", "Archivos grandes (aqui solo lectura)", "Linux (aqui solo lectura)"};
static void hum(u64 b, char *o) { if (b >> 30) snprintf(o, 16, "%lu.%lu GB", b >> 30, ((b >> 20) & 1023) * 10 / 1024); else snprintf(o, 16, "%lu MB", b >> 20); }
static void load(void) { nd = z_disks(D, 16); dk_sys_disk(sysd); if (sd >= nd) sd = 0; np = nd ? z_parts(D[sd].name, P, 16) : 0; if (sp >= np) sp = -1; }
static const char *fsof(const char *dev, char *lab) { static zmount m[32]; int n = z_mounts(m, 32); lab[0] = 0; for (int i = 0; i < n; i++) if (!strcmp(m[i].dev, dev)) { strcpy(lab, m[i].label); return m[i].fstype; } return "sin formato"; }
#define LW 210
static void draw(void) {
    char a[16], b[160], l[20]; g_clear(W, UI_BG); g_rect(W, 0, 0, LW, W->h, UI_PANEL); g_textb(W, 12, 10, "Unidades", UI_TEXT);
    for (int i = 0; i < nd; i++) { int y = 34 + i * 44; if (i == sd) g_rect(W, 0, y, LW, 42, UI_ACC2); hum(D[i].sectors * D[i].ssize, a); g_textb(W, 12, y + 4, D[i].name, UI_TEXT); g_text(W, 100, y + 4, a, UI_MUTED); zw_clip(W, 12, y, LW - 18, 42); g_text(W, 12, y + 22, D[i].model, UI_MUTED); zw_noclip(W); }
    int x = LW + 14, y = 10; if (!nd) { g_text(W, x, y, "No hay discos", UI_TEXT); zw_present(W); return; }
    g_textb(W, x, y, "Particiones", UI_TEXT); y += 22; ui_button(W, x, y, W->w - x - 12, 26, "Disco completo (crea una particion nueva)", sp < 0 ? 2 : 0); y += 30;
    for (int i = 0; i < np; i++) { hum(P[i].sectors * D[sd].ssize, a); const char *t = fsof(P[i].name, l); snprintf(b, 160, "%s   %s   %s   %s", P[i].name, a, t, l); ui_button(W, x, y, W->w - x - 12, 26, b, sp == i ? 2 : 0); y += 30; }
    y = 240; g_textb(W, x, y, "Formato", UI_TEXT); y += 22; for (int i = 0; i < 4; i++) { ui_button(W, x, y, 76, 26, FN[i], fs == i ? 2 : 0); g_text(W, x + 86, y + 5, FD[i], UI_MUTED); y += 30; }
    y += 6; g_text(W, x, y + 5, "Etiqueta", UI_TEXT); ui_input(W, x + 76, y, 150, 26, label, lcur, lfoc); y += 38; int pr = !strcmp(D[sd].name, sysd); ui_button(W, x, y, 130, 30, "Formatear", pr ? 0 : 2); if (pr) g_text(W, x + 142, y + 7, "Disco del sistema: protegido", 0xB91C1C);
    y += 38; if (pct >= 0) { g_rect(W, x, y, W->w - x - 12, 10, UI_LINE); g_rect(W, x, y, (W->w - x - 12) * pct / 100, 10, UI_ACC); } g_text(W, x, y + 14, msgs, UI_TEXT); zw_present(W);
}
static void prog(int p, const char *m) { pct = p; strncpy0(msgs, m, 120); draw(); }
static void format_(void) {
    if (!strcmp(D[sd].name, sysd)) return; char q[140], c[16] = ""; snprintf(q, 140, _("Se borrara %s (%s). Escribe BORRAR:"), sp < 0 ? D[sd].name : P[sp].name, FN[fs]); if (!ui_prompt("Formatear", q, c, 16) || strcmp(c, _("BORRAR"))) { strcpy(msgs, _("Cancelado")); return; }
    int e = sp < 0 ? dk_format_disk(D[sd].name, D[sd].sectors, D[sd].ssize, fs, label, prog) : dk_format_part(D[sd].name, &P[sp], D[sd].ssize, fs, label, prog); pct = -1; if (e) snprintf(msgs, 120, "Error: %s", e == -28 ? _("tamano no valido para este formato") : z_errstr(e)); else strcpy(msgs, _("Formato terminado")); z_sleep(300); if (!e) { const char *mn = sp < 0 ? 0 : P[sp].name; char t[20]; if (mn) strcpy(t, mn); else snprintf(t, 20, "%sp1", D[sd].name); z_set_ro(t, 0); } load(); char m[120]; snprintf(m, 120, "[prueba] formato %s %s: %s", D[sd].name, FN[fs], e ? z_errstr(e) : "OK"); z_log(m);
}
int main(const char *a) {
    W = zw_new("Discos", -1, 0, 680, 520, WF_DECOR); load(); draw(); zevent e; int X = LW + 14;
    for (;;) { if (!z_event(&e, 3000)) { load(); draw(); continue; } if (e.type == EV_REDRAW) { draw(); continue; } if (e.win != (u32)W->id) continue; if (e.type == EV_CLOSE) return 0;
        if (e.type == EV_DOWN) { lfoc = 0; if (e.x < LW) { int i = (e.y - 34) / 44; if (e.y >= 34 && i < nd) { sd = i; sp = -1; load(); } } else if (e.y >= 32 && e.y < 58) sp = -1; else if (e.y >= 62 && e.y < 62 + np * 30) sp = (e.y - 62) / 30; else if (e.y >= 262 && e.y < 382) { if (e.x < X + 76) fs = (e.y - 262) / 30; } else if (e.y >= 388 && e.y < 414) lfoc = 1; else if (e.y >= 426 && e.y < 456 && e.x < X + 130) format_(); draw(); }
        else if (e.type == EV_KEY && lfoc) { ui_input_key(label, 12, &lcur, &e); draw(); } }
}
