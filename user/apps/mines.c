/* Campo minado */
#include "z.h"
#define GW 16
#define GH 16
#define NM 40
#define CS 26
#define TOP 34
static zwin *W; static u8 mine[GH][GW], open_[GH][GW], flag[GH][GW]; static int started, over, win, flags; static u64 t0, t1;
static int nb(int x, int y) { int n = 0; for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) { int a = x + i, b = y + j; if (a >= 0 && b >= 0 && a < GW && b < GH && mine[b][a]) n++; } return n; }
static void reset(void) { memset(mine, 0, sizeof mine); memset(open_, 0, sizeof open_); memset(flag, 0, sizeof flag); started = over = win = flags = 0; t0 = t1 = 0; }
static void seed(int fx, int fy) { int n = 0; while (n < NM) { int x = rnd() % GW, y = rnd() % GH; if (mine[y][x] || (x >= fx - 1 && x <= fx + 1 && y >= fy - 1 && y <= fy + 1)) continue; mine[y][x] = 1; n++; } started = 1; t0 = z_ms(); }
static void reveal(int x, int y) { if (x < 0 || y < 0 || x >= GW || y >= GH || open_[y][x] || flag[y][x]) return; open_[y][x] = 1; if (!nb(x, y)) for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) if (i || j) reveal(x + i, y + j); }
static void check(void) { int c = 0; for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) if (open_[y][x]) c++; if (c == GW * GH - NM) { over = win = 1; t1 = z_ms(); } }
static void draw(void) { static const u32 nc[] = {0,0x2563EB,0x16A34A,0xDC2626,0x1E3A8A,0x7F1D1D,0x0E7490,0x111827,0x6B7280}; g_clear(W, 0xD6D3D1); char b[64]; u64 s = started ? ((over ? t1 : z_ms()) - t0) / 1000 : 0; snprintf(b, 64, _("Minas: %d   Tiempo: %lu"), NM - flags, s); g_text(W, 8, 9, b, UI_TEXT); ui_button(W, W->w - 80, 4, 74, 26, over ? (win ? "Bien!" : "Otra") : "Nueva", over ? 2 : 0); for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) { int X = x * CS, Y = TOP + y * CS; if (open_[y][x] || (over && mine[y][x])) { g_rect(W, X, Y, CS, CS, 0xE7E5E4); g_frame(W, X, Y, CS, CS, 0xA8A29E); if (mine[y][x]) { g_rect(W, X, Y, CS, CS, open_[y][x] ? 0xEF4444 : 0xE7E5E4); g_circle(W, X + CS / 2, Y + CS / 2, 7, 0x111827, 1); } else { int n = nb(x, y); if (n) { char t[2] = {'0' + n, 0}; g_textb(W, X + 9, Y + 5, t, nc[n]); } } } else { g_rect(W, X, Y, CS, CS, 0xA8A29E); g_rect(W, X + 1, Y + 1, CS - 3, CS - 3, 0xC7C2BE); if (flag[y][x]) { g_rect(W, X + 9, Y + 5, 2, 16, 0x111827); g_rect(W, X + 11, Y + 5, 9, 7, 0xDC2626); } } } zw_present(W); }
int main(const char *a) { W = zw_new("Campo minado", -1, 0, GW * CS, TOP + GH * CS, WF_DECOR); reset(); draw(); zevent e; for (;;) { int g = z_event(&e, 1000); if (g) { if (e.type == EV_CLOSE) return 0; if (e.type == EV_REDRAW) draw(); if (e.type == EV_DOWN) { if (e.y < TOP) { if (e.x > W->w - 80) reset(); } else if (!over) { int x = e.x / CS, y = (e.y - TOP) / CS; if (x < GW && y < GH) { if (e.btn & 2) { if (!open_[y][x]) { flag[y][x] ^= 1; flags += flag[y][x] ? 1 : -1; } } else if (!flag[y][x]) { if (!started) seed(x, y); if (mine[y][x]) { open_[y][x] = 1; over = 1; t1 = z_ms(); } else { reveal(x, y); check(); } } } } draw(); } } else if (started && !over) draw(); } }
