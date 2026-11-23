/* Pintar: lapiz, linea, rectangulo, circulo, relleno y goma. Guarda BMP */
#include "z.h"
#define TW 96
#define CW_ 640
#define CH_ 440
static zwin *W; static u32 *C; static int tool, sz = 3, down, sx, sy, lx, ly; static u32 color = 0x111827; static char path[256] = "/sys/dibujo.bmp"; static u32 *undo;
static const u32 pal[] = {0x111827,0xFFFFFF,0xDC2626,0xEA580C,0xF59E0B,0x16A34A,0x0D9488,0x2563EB,0x7C3AED,0xDB2777,0x78350F,0x9CA3AF};
static const char *tn[] = {"Lapiz","Linea","Rect","Circulo","Relleno","Goma"};
static zwin CV;
static void dot(int x, int y, u32 c) { g_circle(&CV, x, y, sz, c, 1); }
static void line(int a, int b, int c, int d, u32 k) { int dx = c > a ? c - a : a - c, sx2 = a < c ? 1 : -1, dy = d > b ? b - d : d - b, sy2 = b < d ? 1 : -1, e = dx + dy; for (;;) { dot(a, b, k); if (a == c && b == d) break; int t = 2 * e; if (t >= dy) { e += dy; a += sx2; } if (t <= dx) { e += dx; b += sy2; } } }
static void fill(int x, int y, u32 k) { u32 o = C[y * CW_ + x]; if (o == k) return; int *st = malloc(CW_ * CH_ * 8), n = 0; st[n++] = x; st[n++] = y; while (n) { y = st[--n]; x = st[--n]; if (x < 0 || y < 0 || x >= CW_ || y >= CH_ || C[y * CW_ + x] != o) continue; C[y * CW_ + x] = k; if (n + 8 < CW_ * CH_ * 2) { st[n++] = x + 1; st[n++] = y; st[n++] = x - 1; st[n++] = y; st[n++] = x; st[n++] = y + 1; st[n++] = x; st[n++] = y - 1; } } free(st); }
static void shape(int x, int y) { memcpy(C, undo, CW_ * CH_ * 4); if (tool == 1) line(sx, sy, x, y, color); else if (tool == 2) { int a = MIN(sx, x), b = MIN(sy, y), w = MAX(sx, x) - a, h = MAX(sy, y) - b; line(a, b, a + w, b, color); line(a, b + h, a + w, b + h, color); line(a, b, a, b + h, color); line(a + w, b, a + w, b + h, color); } else if (tool == 3) { int dx = x - sx, dy = y - sy; int r = (int)z_sqrt(dx * dx + dy * dy); for (int i = 0; i < sz; i++) g_circle(&CV, sx, sy, r + i, color, 0); } }
static void ui(void) {
    g_rect(W, 0, 0, TW, W->h, UI_PANEL); for (int i = 0; i < 6; i++) ui_button(W, 6, 6 + i * 30, TW - 12, 26, tn[i], tool == i ? 2 : 0);
    for (int i = 0; i < 12; i++) { int x = 6 + (i % 4) * 21, y = 192 + (i / 4) * 21; g_rect(W, x, y, 19, 19, pal[i]); g_frame(W, x, y, 19, 19, pal[i] == color ? UI_ACC : UI_LINE); }
    g_rect(W, 6, 260, TW - 12, 20, color); g_frame(W, 6, 260, TW - 12, 20, UI_LINE); char s[20]; snprintf(s, 20, _("Grosor %d"), sz); g_text(W, 8, 288, s, UI_TEXT); ui_button(W, 6, 306, 40, 24, "-", 0); ui_button(W, 50, 306, 40, 24, "+", 0); ui_button(W, 6, 330, TW - 12, 26, "Deshacer", 0); ui_button(W, 6, 358, TW - 12, 26, "Limpiar", 0); ui_button(W, 6, 386, TW - 12, 26, "Guardar", 0); ui_button(W, 6, 414, TW - 12, 24, "Imprimir", 0);
}
static void blit(void) { g_image(W, TW, 0, CW_, CH_, C); }
static void save(void) {
    if (!ui_prompt("Guardar imagen", "Ruta del archivo BMP:", path, 240)) return; int st = CW_ * 3 + (CW_ % 4), sz2 = 54 + st * CH_; u8 *b = calloc(1, sz2); b[0] = 'B'; b[1] = 'M'; *(u32 *)(b + 2) = sz2; *(u32 *)(b + 10) = 54; *(u32 *)(b + 14) = 40; *(int *)(b + 18) = CW_; *(int *)(b + 22) = CH_; *(u16 *)(b + 26) = 1; *(u16 *)(b + 28) = 24;
    for (int y = 0; y < CH_; y++) { u8 *d = b + 54 + (CH_ - 1 - y) * st; for (int x = 0; x < CW_; x++) { u32 c = C[y * CW_ + x]; d[x * 3] = c; d[x * 3 + 1] = c >> 8; d[x * 3 + 2] = c >> 16; } } int e = z_writefile(path, b, sz2); free(b); char m[128]; snprintf(m, 128, "[prueba] paint guardo: %s", e ? z_errstr(e) : "OK"); z_log(m); if (e) ui_confirm("Error", z_errstr(e));
}
/* portapapeles: pegar una imagen (ajustada al lienzo, centrada) y copiar el dibujo */
static void paste_img(void) { int w, h; u32 *px = z_clip_get_img(&w, &h); if (!px) { ui_confirm("Pegar", "El portapapeles no tiene ninguna imagen."); return; } memcpy(undo, C, CW_ * CH_ * 4); long k = MIN(1024L, MIN(CW_ * 1024L / w, CH_ * 1024L / h)); int dw = MAX(1, w * k / 1024), dh = MAX(1, h * k / 1024), x0 = (CW_ - dw) / 2, y0 = (CH_ - dh) / 2;
    for (int y = 0; y < dh; y++) for (int x = 0; x < dw; x++) C[(y0 + y) * CW_ + x0 + x] = px[(long)(y * 1024 / k) * w + x * 1024 / k] & 0xFFFFFF; free(px); char m[80]; snprintf(m, 80, "[prueba] paint pego imagen %dx%d", w, h); z_log(m); }
static void canvas_menu(int x, int y) { int sx, sy; zw_screen(W, x, y, &sx, &sy); static const char *it[] = {"Pegar imagen", "Copiar el dibujo", "Deshacer"}; int r = ui_menu(sx, sy, it, 3); if (r == 0) paste_img(); else if (r == 1) z_clip_set_img(C, CW_, CH_); else if (r == 2) memcpy(C, undo, CW_ * CH_ * 4); }
static void print_(void) { const char *n = strrchr(path, '/'); zp_print_image(n ? n + 1 : "Pintar", C, CW_, CH_); }
int main(const char *a) {
    W = zw_new("Pintar", -1, 0, TW + CW_, CH_, WF_DECOR); C = malloc(CW_ * CH_ * 4); undo = malloc(CW_ * CH_ * 4); memset(C, 255, CW_ * CH_ * 4); memcpy(undo, C, CW_ * CH_ * 4);
    CV.w = CW_; CV.h = CH_; CV.px = C; zw_noclip(&CV); ui(); blit(); zw_present(W); zevent e;
    for (;;) { if (!z_event(&e, -1)) continue; if (e.type == EV_REDRAW) { ui(); blit(); zw_present(W); continue; } if (e.type == EV_CLOSE) return 0; int cx = e.x - TW, cy = e.y;
        if (e.type == EV_DOWN) {
            if (e.x < TW) { int y = e.y; if (y < 186) { int t = (y - 6) / 30; if (t >= 0 && t < 6) tool = t; } else if (y >= 192 && y < 255) { int i = (e.x - 6) / 21 + ((y - 192) / 21) * 4; if (i >= 0 && i < 12) color = pal[i]; } else if (y >= 306 && y < 330) { if (e.x < 48) sz = MAX(1, sz - 1); else sz = MIN(24, sz + 1); } else if (y >= 330 && y < 356) memcpy(C, undo, CW_ * CH_ * 4); else if (y >= 358 && y < 384) { memcpy(undo, C, CW_ * CH_ * 4); memset(C, 255, CW_ * CH_ * 4); } else if (y >= 386 && y < 412) save(); else if (y >= 414) print_(); ui(); }
            else if (e.btn & 2) canvas_menu(e.x, e.y);
            else { memcpy(undo, C, CW_ * CH_ * 4); down = 1; sx = lx = cx; sy = ly = cy; if (tool == 0) dot(cx, cy, color); else if (tool == 5) dot(cx, cy, 0xFFFFFF); else if (tool == 4) { fill(cx, cy, color); down = 0; } }
            blit(); zw_present(W);
        } else if (e.type == EV_MOVE && down) { cx = MAX(0, MIN(cx, CW_ - 1)); cy = MAX(0, MIN(cy, CH_ - 1)); if (tool == 0) line(lx, ly, cx, cy, color); else if (tool == 5) line(lx, ly, cx, cy, 0xFFFFFF); else shape(cx, cy); lx = cx; ly = cy; blit(); zw_present(W); }
        else if (e.type == EV_UP) down = 0;
        else if (e.type == EV_KEY && e.ch == 22) { paste_img(); blit(); zw_present(W); } else if (e.type == EV_KEY && e.ch == 3) z_clip_set_img(C, CW_, CH_); else if (e.type == EV_KEY && e.ch == 16) { print_(); ui(); blit(); zw_present(W); } else if (e.type == EV_KEY && e.ch == 19) { save(); ui(); blit(); zw_present(W); } else if (e.type == EV_KEY && e.ch == 26) { memcpy(C, undo, CW_ * CH_ * 4); blit(); zw_present(W); } }
}
