/* Gestor de ventanas y compositor */
#include "kernel.h"
#include "font8x16.h"
#define MAXW 64
#define TITLE (T.title_sc > 1 ? 40 : 26)
#define BS ((int)T.title_sc)
#define WF_DECOR 1
#define WF_RESIZE 2
#define WF_DESKTOP 4
#define WF_PANEL 8
#define WF_POPUP 16
#define WF_LOCK 32 /* pantalla de bloqueo: encima de todo y recibe toda la entrada (solo el escritorio) */
#define ST_MIN 1
#define ST_MAX 2
typedef struct win {
    int used, id, x, y, w, h, cw, ch; u32 flags, state; char title[64];
    u32 *px; u64 px_phys, px_pages; task_t *owner; proc_t *proc;
    int sx, sy, sw, sh; u64 close_t; /* cuando se pidio cerrar (para cerrar a la fuerza si no responde) */
} win_t;
static win_t wins[MAXW]; static win_t *z[MAXW]; static int nz; static win_t *focus, *grab, *hover, *dragw;
static int drag_mode, drag_ox, drag_oy, rw, rh, rx, ry, redge, cur_kind; /* 1 mover, 2 redimensionar */
static mutex_t wm; static u32 *bb; static volatile u8 *fb; u32 scr_w, scr_h; static u32 pitch, bpp, is_rgb;
static int mx, my, mbtn, mods, ready, cascade, dim = 256; static u32 *lbuf;
static int dx0 = 1 << 30, dy0 = 1 << 30, dx1 = -1, dy1 = -1;
static ztheme_t T = {0x0F766E, 0x3B4252, 0x111827, 0xFFFFFF, 0xC8CDD6, 0xC0392B, 0x0B3B3C, 0x134E4A, 0x1F2937, 0x111827, 0x0F766E, 0xCCFBF1, 0xF3F4F6, 0xE5E7EB, 0xC4C9D2, 0x1F2937, 0x6B7280, 0xFFFFFF, 1, 1, 0, 0, 1, 1, 1, 0};
#define C_ACT T.acc
#define C_INACT T.inact
#define C_BORDER T.border
#define C_TXT T.ttxt
#define C_TXT2 T.ttxt2

int wm_ready(void) { return ready; }
static int lay(win_t *w) { return w->flags & WF_LOCK ? 4 : w->flags & WF_DESKTOP ? 0 : w->flags & WF_POPUP ? 3 : w->flags & WF_PANEL ? 2 : 1; }
static int ox(win_t *w) { return w->x - (w->flags & WF_DECOR ? 1 : 0); }
static int oy(win_t *w) { return w->y - (w->flags & WF_DECOR ? TITLE : 0); }
static int ow(win_t *w) { return w->w + (w->flags & WF_DECOR ? 2 : 0); }
static int oh(win_t *w) { return w->h + (w->flags & WF_DECOR ? TITLE + 1 : 0); }
static void dirty(int x, int y, int w, int h) {
    int a = MAX(x, 0), b = MAX(y, 0), c = MIN(x + w, (int)scr_w), d = MIN(y + h, (int)scr_h);
    if (a >= c || b >= d) return;
    u64 f = irq_save();
    dx0 = MIN(dx0, a); dy0 = MIN(dy0, b); dx1 = MAX(dx1, c); dy1 = MAX(dy1, d);
    irq_restore(f);
}
static void dirty_win(win_t *w) { dirty(ox(w), oy(w), ow(w), oh(w)); }
static int cx0, cy0, cx1, cy1;
static void rect(int x, int y, int w, int h, u32 c) {
    int a = MAX(x, cx0), b = MAX(y, cy0), e = MIN(x + w, cx1), f = MIN(y + h, cy1);
    for (int j = b; j < f; j++) { u32 *p = bb + j * scr_w + a; for (int i = a; i < e; i++) *p++ = c; }
}
static int cp437(int c) {
    if (c < 128) return c;
    static const u16 m[][2] = {{0xE1,160},{0xE9,130},{0xED,161},{0xF3,162},{0xFA,163},{0xF1,164},{0xD1,165},{0xFC,129},{0xDC,154},{0xC9,144},{0xBF,168},{0xA1,173},{0xBA,167},{0xAA,166},{0xE7,135},{0xC7,128},{0xE0,133},{0xE8,138},{0xF2,149},{0xC1,65},{0xCD,73},{0xD3,79},{0xDA,85},{0xB7,250},{0xAC,170},{0xB4,39},{0xA8,34}};
    for (unsigned i = 0; i < sizeof m / sizeof m[0]; i++) if (m[i][0] == c) return m[i][1];
    return '?';
}
static void text_s(int x, int y, const char *s, u32 col, int maxw, int sc) {
    int x0 = x;
    while (*s) {
        int c = utf8_decode(&s); if (x - x0 + 8 * sc > maxw) break;
        const u8 *g = font8x16 + cp437(c) * 16;
        for (int j = 0; j < 16; j++) for (int i = 0; i < 8; i++) if (g[j] & (0x80 >> i)) rect(x + i * sc, y + j * sc, sc, sc, col);
        x += 8 * sc;
    }
}
static void text(int x, int y, const char *s, u32 col, int maxw) { text_s(x, y, s, col, maxw, 1); }
static const char *arrow[] = {"X","XX","X.X","X..X","X...X","X....X","X.....X","X......X","X.......X","X........X","X.....XXXXX","X..X..X","X.X X..X","XX  X..X","X    X..X","     X..X","      XX"};
static void cline(int x0, int y0, int dx, int dy, int n, u32 c) { for (int i = 0; i < n; i++) rect(x0 + i * dx, y0 + i * dy, 1, 1, c); }
static void rarrow(int ax, int ay, int k, u32 c, int o) { int dx = k == 1 || k == 3 || k == 4 ? 1 : 0, dy = k == 2 || k == 3 ? 1 : k == 4 ? -1 : 0; for (int t = -o; t <= o; t++) { int px = dy && !dx ? t : 0, py = dx && !dy ? t : 0; if (dx && dy) { px = t; py = 0; } cline(ax - 7 * dx + px, ay - 7 * dy + py, dx, dy, 15, c); for (int i = 0; i < 4; i++) { int a1 = i + 1; if (k == 1) { rect(ax - 7 + i + t * 0, ay - a1 + t, 1, 2 * a1 - 2 * t * 0 > 0 ? 2 * a1 : 1, c); rect(ax + 7 - i, ay - a1 + t, 1, 2 * a1, c); } else if (k == 2) { rect(ax - a1 + t, ay - 7 + i, 2 * a1, 1, c); rect(ax - a1 + t, ay + 7 - i, 2 * a1, 1, c); } else { int sg = k == 3 ? 1 : -1; rect(ax - 7 + t, ay - 7 * sg + i * sg, 4 - i, 1, c); rect(ax + 4 + i + t, ay + 7 * sg - i * sg, 4 - i, 1, c); } } } }
static int no_cursor;
static void draw_cursor(void) {
    if (no_cursor) return;
    if (cur_kind) { rarrow(mx, my, cur_kind, 0, 1); rarrow(mx, my, cur_kind, 0xFFFFFF, 0); return; }
    for (int j = 0; j < 17; j++) for (int i = 0; arrow[j][i]; i++) {
        char c = arrow[j][i]; int x = mx + i, y = my + j;
        if (c == ' ' || x < cx0 || x >= cx1 || y < cy0 || y >= cy1) continue;
        bb[y * scr_w + x] = c == 'X' ? 0 : 0xFFFFFF;
    }
}
static void draw_btn(int x, int y, int kind, int active) {
    int S = BS; rect(x, y, 20 * S, 18 * S, kind == 2 ? T.close : active ? (T.acc & 0xFEFEFE) >> 1 : T.inact + 0x101010);
    if (kind == 0) rect(x + 5 * S, y + 12 * S, 10 * S, 2 * S, C_TXT);
    else if (kind == 1) { rect(x + 5 * S, y + 4 * S, 10 * S, 2 * S, C_TXT); rect(x + 5 * S, y + 12 * S, 10 * S, S, C_TXT); rect(x + 5 * S, y + 4 * S, S, 9 * S, C_TXT); rect(x + 14 * S, y + 4 * S, S, 9 * S, C_TXT); }
    else for (int i = 0; i < 9; i++) { rect(x + (5 + i) * S, y + (4 + i) * S, 2 * S, S, C_TXT); rect(x + (13 - i) * S, y + (4 + i) * S, 2 * S, S, C_TXT); }
}
static void draw_win(win_t *w) {
    if (w->flags & WF_DECOR) {
        int X = ox(w), Y = oy(w), W = ow(w), H = oh(w), act = w == focus; u32 c = act ? C_ACT : C_INACT;
        rect(X, Y, W, 1, C_BORDER); rect(X, Y + H - 1, W, 1, C_BORDER); rect(X, Y, 1, H, C_BORDER); rect(X + W - 1, Y, 1, H, C_BORDER);
        rect(X + 1, Y + 1, W - 2, TITLE - 1, c);
        int bx = X + W - 23 * BS, by = Y + (TITLE - 18 * BS) / 2 + 1, st = 22 * BS;
        draw_btn(bx, by, 2, act); bx -= st;
        draw_btn(bx, by, 1, act); bx -= st;
        draw_btn(bx, by, 0, act);
        text_s(X + 8, Y + (TITLE - 16 * BS) / 2 + 1, w->title, act ? C_TXT : C_TXT2, bx - X - 14, BS);
    }
    int a = MAX(w->x, cx0), b = MAX(w->y, cy0), e = MIN(w->x + w->w, cx1), f = MIN(w->y + w->h, cy1);
    if (a >= e) return;
    if (w->cw == w->w && w->ch == w->h) { for (int j = b; j < f; j++) memcpy(bb + j * scr_w + a, w->px + (j - w->y) * w->cw + (a - w->x), (e - a) * 4); return; }
    for (int j = b; j < f; j++) { u32 *s = w->px + (u64)(j - w->y) * w->ch / w->h * w->cw, *d = bb + j * scr_w; for (int i = a; i < e; i++) d[i] = s[(u64)(i - w->x) * w->cw / w->w]; }
}
#ifdef ZZ_TACTIL
/* ===== Teclado en pantalla (Raspberry Pi con pantalla tactil): lo dibuja el gestor de ventanas encima de todo,
   asi sirve tambien en la pantalla de inicio. Las teclas son codigos HID: las etiquetas salen de la distribucion
   (ES/US) con key_char, y al pulsar se generan las mismas teclas que con un teclado de verdad. ===== */
#define K_SHIFT 0xE1
#define K_CTRL 0xE0
#define K_ALTGR 0xE6
#define K_HIDE 0xFF
typedef struct { u8 u, w; } okey_t;
static const okey_t ORow0[] = {{0x29,2},{0x1E,2},{0x1F,2},{0x20,2},{0x21,2},{0x22,2},{0x23,2},{0x24,2},{0x25,2},{0x26,2},{0x27,2},{0x2D,2},{0x2A,3},{0}};
static const okey_t ORow1[] = {{0x2B,3},{0x14,2},{0x1A,2},{0x08,2},{0x15,2},{0x17,2},{0x1C,2},{0x18,2},{0x0C,2},{0x12,2},{0x13,2},{0x2F,2},{0x30,2},{0}};
static const okey_t ORow2[] = {{0x04,2},{0x16,2},{0x07,2},{0x09,2},{0x0A,2},{0x0B,2},{0x0D,2},{0x0E,2},{0x0F,2},{0x33,2},{0x34,2},{0x28,4},{0}};
static const okey_t ORow3[] = {{K_SHIFT,3},{0x64,2},{0x1D,2},{0x1B,2},{0x06,2},{0x19,2},{0x05,2},{0x11,2},{0x10,2},{0x36,2},{0x37,2},{0x38,2},{0x2E,2},{0}};
static const okey_t ORow4[] = {{K_CTRL,3},{K_ALTGR,3},{0x2C,9},{0x50,2},{0x52,2},{0x51,2},{0x4F,2},{0x4C,3},{K_HIDE,3},{0}};
static const okey_t *const ORows[5] = {ORow0, ORow1, ORow2, ORow3, ORow4};
#define OSK_KH 28
#define OSK_H (5 * (OSK_KH + 2) + 2)
int osk_on;   /* la placa lo activa: hay pantalla tactil */
static int osk_vis, osk_shift, osk_ctrl, osk_altgr, osk_hl = -1; static u64 osk_hl_t;
static int panel_h(void) { int ph = 0; for (int i = 0; i < nz; i++) if (z[i]->flags & WF_PANEL) ph = z[i]->h; return ph; }
static int osk_y0(void) { return scr_h - OSK_H - (T.panel_top ? 0 : panel_h()); }   /* encima de la barra de tareas */
static int osk_key_at(int r, int k, int *x, int *y, int *w) {   /* posicion de la tecla k de la fila r */
    int tot = 0, before = 0; for (int i = 0; ORows[r][i].u; i++) { if (i == k) before = tot; tot += ORows[r][i].w; }
    *x = 2 + before * ((int)scr_w - 4) / tot; *w = (before + ORows[r][k].w) * ((int)scr_w - 4) / tot - (*x - 2) - 2;
    *y = osk_y0() + 2 + r * (OSK_KH + 2); return 0;
}
static void glyph(int x, int y, int idx, u32 col) { const u8 *g = font8x16 + idx * 16; for (int j = 0; j < 16; j++) for (int i = 0; i < 8; i++) if (g[j] & (0x80 >> i)) rect(x + i, y + j, 1, 1, col); }
static void osk_label(int u, char *o, int *gl) {
    *gl = 0; o[0] = 0; int es = T.lang == 0;
    switch (u) {
    case K_SHIFT: strncpy0(o, es ? "May" : "Shift", 16); return; case K_CTRL: strncpy0(o, "Ctrl", 16); return; case K_ALTGR: strncpy0(o, "AltGr", 16); return;
    case K_HIDE: *gl = 0x1F; return; case 0x29: strncpy0(o, "Esc", 16); return; case 0x2B: strncpy0(o, "Tab", 16); return;
    case 0x28: strncpy0(o, es ? "Intro" : "Enter", 16); return; case 0x2A: *gl = 0x1B; return; case 0x4C: strncpy0(o, es ? "Supr" : "Del", 16); return;
    case 0x2C: strncpy0(o, es ? "espacio" : "space", 16); return;
    case 0x50: *gl = 0x1B; return; case 0x4F: *gl = 0x1A; return; case 0x52: *gl = 0x18; return; case 0x51: *gl = 0x19; return;
    }
    int m = (osk_shift ? M_SHIFT : 0) | (osk_altgr ? M_ALTGR : 0); u32 c = key_char(u, m);
    if (c >= 0xE001 && c <= 0xE004) c = c == 0xE001 ? 0xB4 : c == 0xE002 ? '`' : c == 0xE003 ? '^' : 0xA8;
    if (c >= 'a' && c <= 'z' && osk_shift) c -= 32;
    if (c) o[utf8_encode(c, o)] = 0;
}
static void osk_draw(void) {
    if (!osk_vis) return;
    int y0 = osk_y0(); rect(0, y0, scr_w, OSK_H, T.panel2); rect(0, y0, scr_w, 1, T.acc);
    for (int r = 0; r < 5; r++) for (int k = 0; ORows[r][k].u; k++) {
        int x, y, w, u = ORows[r][k].u; osk_key_at(r, k, &x, &y, &w);
        int on = (u == K_SHIFT && osk_shift) || (u == K_CTRL && osk_ctrl) || (u == K_ALTGR && osk_altgr) || osk_hl == r * 32 + k;
        u32 bg = on ? T.acc : (u >= 0xE0 || u < 4 || (u >= 0x28 && u <= 0x2B) || u >= 0x4C) ? T.inact : T.panel1;
        rect(x, y, w, OSK_KH, bg); rect(x, y + OSK_KH - 1, w, 1, 0);
        if (u == K_SHIFT && osk_shift == 2) rect(x + 4, y + OSK_KH - 5, w - 8, 2, 0xFFFFFF);   /* mayusculas fijas */
        char l[16]; int gl; osk_label(u, l, &gl);
        if (gl) glyph(x + (w - 8) / 2, y + (OSK_KH - 16) / 2, gl, 0xFFFFFF);
        else { int n = 0; for (const char *q = l; utf8_decode(&q); ) n++; text(x + (w - n * 8) / 2, y + (OSK_KH - 16) / 2, l, 0xFFFFFF, w); }
    }
}
static void on_key2(int u, int down); static int osk_set(int v);
static void osk_press(int px, int py) {
    for (int r = 0; r < 5; r++) for (int k = 0; ORows[r][k].u; k++) {
        int x, y, w, u = ORows[r][k].u; osk_key_at(r, k, &x, &y, &w);
        if (px < x || px >= x + w + 2 || py < y || py >= y + OSK_KH + 2) continue;
        osk_hl = r * 32 + k; osk_hl_t = ticks + 150 * HZ / 1000;
        if (u == K_SHIFT) osk_shift = (osk_shift + 1) % 3;
        else if (u == K_CTRL) osk_ctrl = !osk_ctrl;
        else if (u == K_ALTGR) osk_altgr = !osk_altgr;
        else if (u == K_HIDE) { osk_hl = -1; osk_set(0); return; }
        else {
            if (osk_shift) on_key2(K_SHIFT, 1); if (osk_ctrl) on_key2(K_CTRL, 1); if (osk_altgr) on_key2(K_ALTGR, 1);
            on_key2(u, 1); on_key2(u, 0);
            if (osk_altgr) on_key2(K_ALTGR, 0); if (osk_ctrl) on_key2(K_CTRL, 0); if (osk_shift) on_key2(K_SHIFT, 0);
            if (osk_shift == 1) osk_shift = 0; osk_ctrl = osk_altgr = 0;
        }
        dirty(0, osk_y0(), scr_w, OSK_H); return;
    }
}
/* calibracion del tactil (pi/tactil.c): cruz a pantalla completa, encima de todo */
static int cal_n, cal_x, cal_y;
void wm_calib(int step, int x, int y) { u64 f = irq_save(); cal_n = step; cal_x = x; cal_y = y; irq_restore(f); dirty(0, 0, scr_w, scr_h); if (wm_task) task_wake(wm_task); }
static void cal_draw(void) {
    if (!cal_n) return;
    rect(0, 0, scr_w, scr_h, 0x111827);
    char m[80]; snprintf(m, sizeof m, T.lang == 0 ? "Toca el centro de la cruz (%d de 3)" : "Touch the center of the cross (%d of 3)", cal_n);
    int n = 0; for (const char *q = m; utf8_decode(&q); ) n++;
    text((scr_w - n * 8) / 2, scr_h / 2 - 40, m, 0xFFFFFF, scr_w);
    const char *t = T.lang == 0 ? "Calibrar la pantalla táctil" : "Touch screen calibration"; n = 0; for (const char *q = t; utf8_decode(&q); ) n++;
    text((scr_w - n * 8) / 2, scr_h / 2 - 64, t, T.ui_acc2, scr_w);
    rect(cal_x - 14, cal_y, 29, 1, 0xFFFFFF); rect(cal_x, cal_y - 14, 1, 29, 0xFFFFFF); rect(cal_x - 3, cal_y - 3, 7, 7, 0xF87171);
}
u32 *wm_fb_line(int y) { return (u32 *)(fb + y * pitch); }   /* la pantalla SPI lee de aqui */
static void notify_list(void);
static int osk_set(int v) { int n = v < 0 ? !osk_vis : !!v; if (n != osk_vis) { osk_vis = n; osk_shift = osk_ctrl = osk_altgr = 0; dirty(0, 0, scr_w, scr_h); notify_list(); kprintf("wm: teclado en pantalla %s\n", n ? "visible" : "oculto"); } return osk_vis; }
static void osk_tick(void) { if (osk_hl >= 0 && ticks > osk_hl_t) { osk_hl = -1; if (osk_vis) dirty(0, osk_y0(), scr_w, OSK_H); } }
#endif
static void compose(void) {
    u64 fl = irq_save(); int a = dx0, b = dy0, c = dx1, d = dy1; dx0 = dy0 = 1 << 30; dx1 = dy1 = -1; irq_restore(fl);
    if (c <= a || d <= b) return;
    cx0 = a; cy0 = b; cx1 = c; cy1 = d;
    rect(a, b, c - a, d - b, T.d1);
    for (int i = 0; i < nz; i++) { win_t *w = z[i]; if (!(w->state & ST_MIN)) draw_win(w); }
    if (drag_mode == 2 && dragw) {
        int X = rx - 1, Y = ry - TITLE, W = rw + 2, H = rh + TITLE + 1; u32 k = T.ui_acc2;
        rect(X, Y, W, 2, k); rect(X, Y + H - 2, W, 2, k); rect(X, Y, 2, H, k); rect(X + W - 2, Y, 2, H, k);
    }
#ifdef ZZ_TACTIL
    osk_draw(); cal_draw();
#endif
    draw_cursor();
    for (int j = b; j < d; j++) {
        u32 *s = bb + j * scr_w + a;
        if (dim < 256) { for (int i = 0; i < c - a; i++) { u32 v = s[i]; lbuf[i] = (((v & 0xFF00FF) * dim >> 8) & 0xFF00FF) | (((v & 0xFF00) * dim >> 8) & 0xFF00); } s = lbuf; }
        volatile u8 *o = fb + j * pitch + a * (bpp / 8); int n = c - a;
        if (bpp == 32 && !is_rgb) memcpy((void *)o, s, n * 4);
        else if (bpp == 32) { volatile u32 *q = (volatile u32 *)o; for (int i = 0; i < n; i++) { u32 v = s[i]; q[i] = (v & 0xFF00) | (v >> 16 & 0xFF) | (v & 0xFF) << 16; } }
        else for (int i = 0; i < n; i++) { u32 v = s[i]; o[i*3] = v; o[i*3+1] = v >> 8; o[i*3+2] = v >> 16; }
    }
#ifdef __arm__
    lcd_dirty(a, b, c, d);   /* la pantalla SPI copia esta zona */
#endif
}
static void send(win_t *w, u32 type, int x, int y, u32 key, u32 ch, u32 btn) {
    if (w->w != w->cw && (type == EV_MOVE || type == EV_DOWN || type == EV_UP || type == EV_WHEEL) && w->w > 0) x = x * w->cw / w->w;
    if (w->h != w->ch && (type == EV_MOVE || type == EV_DOWN || type == EV_UP || type == EV_WHEEL) && w->h > 0) y = y * w->ch / w->h;
    zevent_t e = {type, w->id, x, y, key, ch, mods, btn}; ev_push(w->owner, &e);
}
static void notify_list(void) { for (int i = 0; i < nz; i++) if (z[i]->flags & WF_PANEL) send(z[i], EV_WINLIST, 0, 0, 0, 0, 0); }
static void z_remove(win_t *w) { for (int i = 0; i < nz; i++) if (z[i] == w) { memmove(&z[i], &z[i + 1], (nz - i - 1) * sizeof z[0]); nz--; return; } }
static void z_insert(win_t *w) { int i = nz; while (i > 0 && lay(z[i - 1]) > lay(w)) i--; memmove(&z[i + 1], &z[i], (nz - i) * sizeof z[0]); z[i] = w; nz++; }
static void set_focus(win_t *w) {
    if (w == focus) return;
    win_t *o = focus; focus = w;
    if (o) { send(o, EV_BLUR, 0, 0, 0, 0, 0); dirty_win(o); }
    if (w) { send(w, EV_FOCUS, 0, 0, 0, 0, 0); dirty_win(w); }
    notify_list();
}
static void raise_win(win_t *w) { z_remove(w); z_insert(w); dirty_win(w); }
static win_t *lockw(void);
static void focus_top(void) {
    { win_t *l = lockw(); if (l) { set_focus(l); return; } }
    for (int i = nz - 1; i >= 0; i--) { win_t *w = z[i]; if (lay(w) == 1 && !(w->state & ST_MIN)) { set_focus(w); return; } }
    set_focus(0);
}
static void set_size(win_t *w, int nw, int nh) { if (w->flags & WF_RESIZE) send(w, EV_RESIZE, nw, nh, 0, 0, 0); else { dirty_win(w); w->w = nw; w->h = nh; dirty_win(w); } }
static void do_max(win_t *w) {
    if (!(w->flags & WF_DECOR)) return;
    dirty_win(w);
    if (w->state & ST_MAX) { w->state &= ~ST_MAX; w->x = w->sx; w->y = w->sy; set_size(w, w->sw, w->sh); }
    else {
        int ph = 0; for (int i = 0; i < nz; i++) if (z[i]->flags & WF_PANEL) ph = z[i]->h;
        w->sx = w->x; w->sy = w->y; w->sw = w->w; w->sh = w->h; w->state |= ST_MAX; w->x = 1; w->y = TITLE + (T.panel_top ? ph : 0);
        int mw = scr_w - 2, mh = scr_h - ph - TITLE - 1; if (!(w->flags & WF_RESIZE)) { if ((u64)mw * w->ch > (u64)mh * w->cw) mw = mh * w->cw / w->ch; else mh = mw * w->ch / w->cw; w->x = 1 + (scr_w - 2 - mw) / 2; }
        set_size(w, mw, mh);
    }
    dirty_win(w);
}
/* zona de redimensionado: 1 izquierda, 2 derecha, 4 arriba, 8 abajo */
static int zone(win_t *w, int x, int y) {
    if (!(w->flags & WF_DECOR) || (w->state & (ST_MAX | ST_MIN))) return 0;
    int X = ox(w), Y = oy(w), R = X + ow(w), B = Y + oh(w), o = 5, in = 3, c = 14, e = 0;
    if (x < X - o || x >= R + o || y < Y - o || y >= B + o) return 0;
    if (x < X + in) e |= 1; else if (x >= R - in) e |= 2; if (y < Y + in) e |= 4; else if (y >= B - in) e |= 8;
    if (!e) return 0;
    if (e & 3) { if (y < Y + c) e |= 4; else if (y >= B - c) e |= 8; } if (e & 12) { if (x < X + c) e |= 1; else if (x >= R - c) e |= 2; }
    return e;
}
static win_t *lockw(void) { for (int i = nz - 1; i >= 0; i--) if (lay(z[i]) == 4) return z[i]; return 0; }
static win_t *hit0(int x, int y);
static win_t *hit(int x, int y) { win_t *l = lockw(); return l ? l : hit0(x, y); }
static win_t *hit0(int x, int y) {
    for (int i = nz - 1; i >= 0; i--) { win_t *w = z[i]; if (w->state & ST_MIN) continue; if ((x >= ox(w) && y >= oy(w) && x < ox(w) + ow(w) && y < oy(w) + oh(w)) || zone(w, x, y)) return w; }
    return 0;
}
static int kind_of(int e) { if (!e) return 0; if ((e & 3) && !(e & 12)) return 1; if ((e & 12) && !(e & 3)) return 2; return ((e & 1) && (e & 4)) || ((e & 2) && (e & 8)) ? 3 : 4; }
static void cursor_dirty(void) { dirty(mx - 10, my - 10, 28, 28); }
static void rb_dirty(void) { dirty(rx - 3, ry - TITLE - 2, rw + 6, rh + TITLE + 6); }
static void on_mouse(int nx, int ny, int b, int wheel) {
    nx = MAX(0, MIN(nx, (int)scr_w - 1)); ny = MAX(0, MIN(ny, (int)scr_h - 1));
    int moved = nx != mx || ny != my, pressed = b & ~mbtn, released = mbtn & ~b;
    if (moved) { cursor_dirty(); mx = nx; my = ny; cursor_dirty(); }
#ifdef ZZ_TACTIL
    if (osk_vis && my >= osk_y0() && my < osk_y0() + OSK_H && !grab && !drag_mode) { if (b & ~mbtn & 1) osk_press(mx, my); mbtn = b; return; }
#endif
    if (moved) {
        if (drag_mode == 1 && dragw) { dirty_win(dragw); dragw->x = MAX(drag_ox + mx, 1 - dragw->w + 60); dragw->y = MAX(drag_oy + my, TITLE); dirty_win(dragw); }
        else if (drag_mode == 2 && dragw) {
            rb_dirty(); win_t *w = dragw; int L = w->x, Tp = w->y, R = w->x + w->w, Bo = w->y + w->h;
            int mnw = w->flags & WF_RESIZE ? 320 : 80, mnh = w->flags & WF_RESIZE ? 200 : 60; if (redge & 1) L = MIN(mx + 1, R - mnw); if (redge & 2) R = MAX(mx, L + mnw); if (redge & 4) Tp = MAX(MIN(my + TITLE, Bo - mnh), TITLE); if (redge & 8) Bo = MAX(my, Tp + mnh);
            rx = L; ry = Tp; rw = MIN(R - L, (int)scr_w); rh = MIN(Bo - Tp, (int)scr_h); if (!(w->flags & WF_RESIZE)) { rw = MAX(rw, w->cw / 4); rh = rw * w->ch / w->cw; if (redge & 1) rx = R - rw; if (redge & 4) ry = Bo - rh; } rb_dirty();
        } else {
            win_t *w = grab ? grab : hit(mx, my);
            if (w != hover) { if (hover) send(hover, EV_LEAVE, 0, 0, 0, 0, 0); hover = w; if (w) send(w, EV_ENTER, 0, 0, 0, 0, 0); }
            int k = !grab && w ? kind_of(zone(w, mx, my)) : 0; if (k != cur_kind) { cur_kind = k; cursor_dirty(); }
            if (w && !k) send(w, EV_MOVE, mx - w->x, my - w->y, 0, 0, b);
        }
    }
    if (wheel) { win_t *w = grab ? grab : hit(mx, my); if (w) send(w, EV_WHEEL, mx - w->x, my - w->y, wheel, 0, b); }
    if (pressed) {
        win_t *w = hit(mx, my);
        if (!w) { set_focus(0); }
        else {
            if (lay(w) == 1 || lay(w) == 3 || lay(w) == 4) { raise_win(w); set_focus(w); }
            else if (lay(w) == 0) set_focus(w);
            int X = ox(w), Y = oy(w), W = ow(w), e = zone(w, mx, my), in_client = mx >= w->x && my >= w->y && mx < w->x + w->w && my < w->y + w->h;
            if (e && (pressed & 1)) { drag_mode = 2; dragw = w; redge = e; rx = w->x; ry = w->y; rw = w->w; rh = w->h; rb_dirty(); }
            else if (in_client) { grab = w; send(w, EV_DOWN, mx - w->x, my - w->y, 0, 0, pressed); }
            else if ((w->flags & WF_DECOR) && my < Y + TITLE && (pressed & 1)) {
                int bx = X + W - 23 * BS, st = 22 * BS;
                if (mx >= bx) { /* segunda X despues de 3 s sin cerrarse: el programa no responde, se termina */
                    if (w->close_t && ticks - w->close_t > 3 * HZ && w->proc && w->proc->pid != 1) { kprintf("wm: %s no responde; se cierra a la fuerza\n", w->proc->name); proc_kill(w->proc->pid); }
                    else { w->close_t = ticks; send(w, EV_CLOSE, 0, 0, 0, 0, 0); } }
                else if (mx >= bx - st) do_max(w);
                else if (mx >= bx - 2 * st) { w->state |= ST_MIN; dirty_win(w); focus_top(); notify_list(); }
                else if (!(w->state & ST_MAX)) { drag_mode = 1; dragw = w; drag_ox = w->x - mx; drag_oy = w->y - my; }
            }
        }
    }
    if (released) {
        if (grab) { send(grab, EV_UP, mx - grab->x, my - grab->y, 0, 0, released); if (!(b & 7)) grab = 0; }
        if (!(b & 1)) {
            if (drag_mode == 2 && dragw) { rb_dirty(); win_t *w = dragw; dirty_win(w); int chg = rw != w->w || rh != w->h; w->x = rx; w->y = ry; dirty_win(w); if (chg) set_size(w, rw, rh); }
            drag_mode = 0; dragw = 0;
        }
    }
    mbtn = b;
}
static void cycle(void) {
    int n = 0; win_t *l[MAXW];
    for (int i = 0; i < nz; i++) if (lay(z[i]) == 1) l[n++] = z[i];
    if (n < 2) return;
    win_t *w = l[0]; w->state &= ~ST_MIN; raise_win(w); set_focus(w);
}
static const u8 kpnav[10] = {0x49, 0x4D, 0x51, 0x4E, 0x50, 0, 0x4F, 0x4A, 0x52, 0x4B};
/* repeticion de teclas: los teclados USB no repiten solos y los PS/2 lo hacen a su ritmo; aqui se hace igual
   para todos. Tras 450 ms pulsada, la tecla se repite 30 veces por segundo hasta que se suelta. No se repiten
   los modificadores ni Bloq Mayus, Bloq Num, Impr Pant ni Esc. */
static int rep_u; static u64 rep_next;
static int repeatable(int u) { return u >= 4 && u < 0xE0 && u != 0x29 && u != 0x39 && u != 0x53 && u != 0x46 && u != 0x47 && u != 0x48; }
static void on_key2(int u, int down);
/* atajos del sistema que hace el escritorio (proceso 1): llegan a su barra como teclas 0x100 y siguientes */
static void desk_key(int k) { for (int i = 0; i < nz; i++) if ((z[i]->flags & WF_PANEL) && z[i]->proc->pid == 1) { send(z[i], EV_KEY, 0, 0, k, 0, 0); break; } }
static void on_key(int u, int down) {
    if (down && u == rep_u) return;   /* repeticion propia del teclado PS/2: ya la hacemos nosotros */
    if (down && repeatable(u)) { rep_u = u; rep_next = ticks + 450 * HZ / 1000; } else if (!down && u == rep_u) rep_u = 0; else if (down && u < 0xE0) rep_u = 0;
    on_key2(u, down); }
static void key_repeat(void) { for (int n = 0; rep_u && ticks >= rep_next && n < 3; n++) { on_key2(rep_u, 1); rep_next += MAX(1, HZ / 30); } if (rep_u && ticks >= rep_next) rep_next = ticks + HZ / 30; }
static void on_key2(int u, int down) {
    if (down && u == 0x53) { mods ^= M_NUM; kbd_set_leds((mods & M_NUM ? 1 : 0) << 1 | (mods & M_CAPS ? 4 : 0)); return; }
    if (down && u == 0x39) { mods ^= M_CAPS; kbd_set_leds((mods & M_NUM ? 1 : 0) << 1 | (mods & M_CAPS ? 4 : 0)); return; }
    if (!(mods & M_NUM) && u >= 0x59 && u <= 0x63) { if (u == 0x63) u = 0x4C; else { u = kpnav[(u - 0x59 + 1) % 10]; if (!u) return; } }
    int bit = u == 0xE0 || u == 0xE4 ? M_CTRL : u == 0xE1 || u == 0xE5 ? M_SHIFT : u == 0xE2 ? M_ALT : u == 0xE6 ? M_ALTGR : u == 0xE3 || u == 0xE7 ? M_GUI : 0;
    /* tecla Windows sola: menu Inicio (lo hace el escritorio) */
    static int gui_only; if (bit == M_GUI) { if (down) gui_only = 1; else if (gui_only && !lockw()) { gui_only = 0; desk_key(0x100); } } else if (down) gui_only = 0;
    if (bit) { if (down) mods |= bit; else mods &= ~bit; return; }
    if (down && !lockw()) {
        if ((mods & M_GUI) && u == 0x08) { desk_key(0x101); return; }                       /* Win+E: Archivos */
        if ((mods & M_GUI) && u == 0x0F) { desk_key(0x102); return; }                       /* Win+L: bloquear */
        if ((mods & M_CTRL) && (mods & M_SHIFT) && u == 0x29) { desk_key(0x103); return; }  /* Ctrl+Mayus+Esc: Monitor */
        if ((mods & M_GUI) && u == 0x07) { int any = 0; for (int i = 0; i < nz; i++) if (lay(z[i]) == 1 && !(z[i]->state & ST_MIN)) any = 1;   /* Win+D: escritorio */
            for (int i = 0; i < nz; i++) if (lay(z[i]) == 1) { if (any) z[i]->state |= ST_MIN; else z[i]->state &= ~ST_MIN; dirty_win(z[i]); } if (any) focus_top(); else cycle(); notify_list(); return; }
        if ((mods & M_CTRL) && !(mods & (M_ALT | M_SHIFT)) && u == 0x1A && focus && lay(focus) == 1) { send(focus, EV_CLOSE, 0, 0, 0, 0, 0); return; }   /* Ctrl+W: cerrar ventana */
    }
    if (down && u == 0x46) { for (int i = 0; i < nz; i++) if ((z[i]->flags & WF_PANEL) && z[i]->proc->pid == 1) { send(z[i], EV_KEY, 0, 0, u, 0, 0); break; } return; } /* Impr Pant: al escritorio */
    win_t *lk = lockw(); if (lk) { if (focus != lk) set_focus(lk); send(lk, down ? EV_KEY : EV_KEYUP, 0, 0, u, down ? key_translate(u, mods) : 0, 0); return; }
    if (down && (mods & M_ALT) && u == 0x2B) { cycle(); return; }
    if (down && (mods & M_ALT) && u == 0x3D) { if (focus) send(focus, EV_CLOSE, 0, 0, 0, 0, 0); return; }
    win_t *t = focus;
    if (!t) return;
    send(t, down ? EV_KEY : EV_KEYUP, 0, 0, u, down ? key_translate(u, mods) : 0, 0);
}
static void wm_thread(void *a) {
    wm_task = cur; dirty(0, 0, scr_w, scr_h);
    int wheel_acc = 0; (void)wheel_acc;
    for (;;) {
        inev_t e; mutex_lock(&wm);
        while (input_pop(&e)) {
            if (e.kind == 4) { if (focus) { zevent_t j = {EV_JOY, focus->id, e.b, e.c, e.d, 0, mods, e.a}; ev_push(focus->owner, &j); } }
            else if (e.kind == 1) on_key(e.a, e.b);
            else if (e.kind == 2) on_mouse(mx + e.a, my + e.b, e.c, e.d);
            else on_mouse(e.a, e.b, e.c, e.d);
        }
        key_repeat();
#ifdef ZZ_TACTIL
        osk_tick();
#endif
        compose();
        mutex_unlock(&wm);
        task_block(rep_u ? 8 : 12);
    }
}
void wm_theme_get(ztheme_t *o) { *o = T; }
void wm_theme_set(const ztheme_t *n) { mutex_lock(&wm); u32 g = T.gen; T = *n; T.title_sc = T.title_sc == 2 ? 2 : 1; T.gen = g + 1; if (ready) { dirty(0, 0, scr_w, scr_h); notify_list(); task_t *seen[MAXW]; int ns = 0; for (int i = 0; i < MAXW; i++) if (wins[i].used) { int f = 0; for (int k = 0; k < ns; k++) if (seen[k] == wins[i].owner) f = 1; if (!f) { seen[ns++] = wins[i].owner; send(&wins[i], EV_THEME, 0, 0, 0, 0, 0); } } } mutex_unlock(&wm); }
/* captura de pantalla: vuelve a componer todo sin el puntero y copia la imagen */
int wm_capture(u32 *dst, u64 max) { if ((u64)scr_w * scr_h > max) return E_INVAL; mutex_lock(&wm); no_cursor = 1; dirty(0, 0, scr_w, scr_h); compose(); memcpy(dst, bb, (u64)scr_w * scr_h * 4); no_cursor = 0; dirty(0, 0, scr_w, scr_h); mutex_unlock(&wm); return scr_w << 16 | scr_h; }
void wm_blank(int on) { int d = on ? 0 : 256; if (d != dim) { dim = d; if (ready) dirty(0, 0, scr_w, scr_h); } }
void wm_set_dim(int p) { int d = p >= 100 ? 256 : 40 + p * 216 / 100; if (d != dim) { dim = d; if (ready) dirty(0, 0, scr_w, scr_h); } }
void fb_early_panic(const char *m) {
    if (!fb) return;
    if (!bb) return;
    cx0 = cy0 = 0; cx1 = scr_w; cy1 = scr_h;
    rect(0, 0, scr_w, 60, 0x8B0000); text(10, 8, "ZunzunOS: error fatal del kernel / fatal kernel error", 0xFFFFFF, scr_w - 20); text(10, 30, m, 0xFFFFFF, scr_w - 20);
    dx0 = dy0 = 0; dx1 = scr_w; dy1 = 60; int a = 0; (void)a;
    for (u32 j = 0; j < 60; j++) { volatile u8 *o = fb + j * pitch; if (bpp == 32) memcpy((void *)o, bb + j * scr_w, scr_w * 4); }
}
void wm_init(void) {
    if (!bi.fb_addr || !bi.fb_w) { kprintf("wm: sin framebuffer\n"); return; }
    scr_w = bi.fb_w; scr_h = bi.fb_h; pitch = bi.fb_pitch; bpp = bi.fb_bpp; is_rgb = bi.fb_rgb;
#ifdef __arm__
    extern int fb_in_ram; if (fb_in_ram) fb = P2V(bi.fb_addr); else
#endif
    fb = ioremap_wc(bi.fb_addr, pitch * scr_h);
    bb = kmalloc(scr_w * scr_h * 4); lbuf = kmalloc(scr_w * 4);
    if (!bb) { kprintf("wm: sin memoria\n"); return; }
    mx = scr_w / 2; my = scr_h / 2; mods = M_NUM;
    kprintf("wm: %ux%u %ubpp pitch=%u rgb=%u\n", scr_w, scr_h, bpp, pitch, is_rgb);
    ready = 1;
    kthread("wm", wm_thread, 0);
}
static win_t *get(int id) { if (id < 1 || id > MAXW) return 0; win_t *w = &wins[id - 1]; return w->used ? w : 0; }
static win_t *mine(int id) { win_t *w = get(id); return w && w->proc == cur->proc ? w : 0; }
static int alloc_canvas(win_t *w, int nw, int nh) {
    u64 pages = ALIGN((u64)nw * nh * 4, PAGE) >> 12, ph = pmm_alloc_contig(pages, 0);
    if (!ph) return E_NOMEM;
    u32 *px = P2V(ph); memset(px, 0xFF, pages * PAGE);
    u64 va = CANVAS_BASE + (w->id - 1) * CANVAS_STEP;
    if (w->px) { for (u64 i = 0; i < w->px_pages; i++) vm_unmap(cur->proc->cr3, va + i * PAGE); pmm_free_contig(w->px_phys, w->px_pages); }
    for (u64 i = 0; i < pages; i++) vm_map(cur->proc->cr3, va + i * PAGE, ph + i * PAGE, PTE_U | PTE_W | PTE_NOFREE);
    w->px = px; w->px_phys = ph; w->px_pages = pages; w->w = w->cw = nw; w->h = w->ch = nh;
    return 0;
}
int wm_create(const zwincfg_t *c, const char *title) {
    if (!ready || c->w < 1 || c->h < 1) return E_INVAL;
    if ((c->flags & WF_LOCK) && cur->proc->pid != 1) return E_ACCES;
    mutex_lock(&wm); int id = 0;
    for (int i = 0; i < MAXW; i++) if (!wins[i].used) { id = i + 1; break; }
    if (!id) { mutex_unlock(&wm); return E_NOMEM; }
    win_t *w = &wins[id - 1]; memset(w, 0, sizeof *w);
    w->id = id; w->flags = c->flags; w->owner = cur; w->proc = cur->proc; strncpy0(w->title, title, 64);
    int cw = MIN(c->w, (int)scr_w), ch = MIN(c->h, (int)scr_h);
#ifdef ZZ_TACTIL
    /* pantalla pequena: una ventana fija que no cabe se muestra reducida (el programa dibuja a su tamano y los toques
       se traducen); una redimensionable se abre del tamano del area libre */
    int fit = 0, ph = panel_h(), aw = scr_w - 2, ah = scr_h - ph - TITLE - 1, small = scr_h < 600 && (c->flags & WF_DECOR);
    if (small) { if (!(c->flags & WF_RESIZE) && (c->w > aw || c->h > ah)) { cw = MIN(c->w, 1600); ch = MIN(c->h, 1200); fit = 1; } else { cw = MIN(cw, aw); ch = MIN(ch, ah); } }
#endif
    int e = alloc_canvas(w, cw, ch);
    if (e) { mutex_unlock(&wm); return e; }
    w->x = c->x; w->y = c->y;
    if (c->x < 0 && (c->flags & WF_DECOR)) { cascade = (cascade + 1) % 8; w->x = 60 + cascade * 28; w->y = 50 + cascade * 26; if (w->x + w->w > (int)scr_w) w->x = 1; if (w->y + w->h + 40 > (int)scr_h) w->y = TITLE; }
#ifdef ZZ_TACTIL
    if (fit) { int mw = aw, mh = ah; if ((u64)mw * ch > (u64)mh * cw) mw = mh * cw / ch; else mh = mw * ch / cw; w->w = mw; w->h = mh; }
    if (small) { int top = TITLE + (T.panel_top ? ph : 0), bot = scr_h - (T.panel_top ? 0 : ph) - 1;
        w->x = MAX(1, MIN(w->x, (int)scr_w - 1 - w->w)); w->y = MAX(top, MIN(w->y, bot - w->h)); }
#endif
    w->used = 1; z_insert(w); dirty_win(w);
    if (lay(w) == 1 || lay(w) == 3 || lay(w) == 4) set_focus(w);
    notify_list(); mutex_unlock(&wm);
    return id;
}
static void destroy(win_t *w) {
    dirty_win(w); z_remove(w);
    if (grab == w) grab = 0;
    if (hover == w) hover = 0;
    if (dragw == w) { dragw = 0; drag_mode = 0; }
    w->used = 0;
    pmm_free_contig(w->px_phys, w->px_pages); w->px = 0;
    if (focus == w) { focus = 0; focus_top(); }
    notify_list();
}
int wm_destroy(int id) {
    mutex_lock(&wm); win_t *w = mine(id); int r = E_INVAL;
    if (w) { u64 va = CANVAS_BASE + (id - 1) * CANVAS_STEP; for (u64 i = 0; i < w->px_pages; i++) vm_unmap(cur->proc->cr3, va + i * PAGE); destroy(w); r = 0; }
    mutex_unlock(&wm); return r;
}
void wm_proc_gone(proc_t *p) {
    if (!ready) return;
    mutex_lock(&wm);
    for (int i = 0; i < MAXW; i++) if (wins[i].used && wins[i].proc == p) {
        u64 va = CANVAS_BASE + i * CANVAS_STEP; for (u64 k = 0; k < wins[i].px_pages; k++) vm_unmap(p->cr3, va + k * PAGE);
        destroy(&wins[i]);
    }
    mutex_unlock(&wm);
}
int wm_present(int id, int x, int y, int w, int h) {
    win_t *t = mine(id); if (!t) return E_INVAL;
    if (w <= 0 || t->w != t->cw || t->h != t->ch) { x = y = 0; w = t->w; h = t->h; }
    if (!(t->state & ST_MIN)) dirty(t->x + x, t->y + y, w, h);
    if (wm_task) task_wake(wm_task);
    return 0;
}
static void info(win_t *w, zwininfo_t *o) {
    o->id = w->id; o->x = w->x; o->y = w->y; o->w = w->cw; o->h = w->ch; o->flags = w->flags; o->state = w->state | (w == focus ? 0x100 : 0);
    o->canvas = CANVAS_BASE + (w->id - 1) * CANVAS_STEP; memcpy(o->title, w->title, 64); o->pid = w->proc->pid;
}
int wm_info(int id, zwininfo_t *o) { mutex_lock(&wm); win_t *w = get(id); int r = E_INVAL; if (w) { info(w, o); r = 0; } mutex_unlock(&wm); return r; }
int wm_list(zwininfo_t *o, int max) { mutex_lock(&wm); int n = 0; for (int i = 0; i < nz && n < max; i++) if (lay(z[i]) == 1) info(z[i], &o[n++]); mutex_unlock(&wm); return n; }
int wm_set_title(int id, const char *t) { mutex_lock(&wm); win_t *w = mine(id); int r = E_INVAL; if (w) { strncpy0(w->title, t, 64); dirty_win(w); notify_list(); r = 0; } mutex_unlock(&wm); return r; }
int wm_resize(int id, int nw, int nh) {
    mutex_lock(&wm); win_t *w = mine(id); int r = E_INVAL;
    if (w && nw > 0 && nh > 0) { dirty_win(w); r = alloc_canvas(w, MIN(nw, (int)scr_w), MIN(nh, (int)scr_h)); dirty_win(w); }
    mutex_unlock(&wm); return r;
}
int wm_op(int id, int op, i64 a, i64 b) {
    mutex_lock(&wm); win_t *w = get(id); int r = 0;
    if (!w) r = E_INVAL;
    else if (lay(w) == 4 && cur->proc->pid != 1) r = E_ACCES; /* nadie mas puede mover ni ocultar la pantalla de bloqueo */
    else switch (op) {
    case 1: w->state &= ~ST_MIN; raise_win(w); set_focus(w); notify_list(); break;
    case 2: w->state |= ST_MIN; dirty_win(w); if (focus == w) focus_top(); notify_list(); break;
    case 3: dirty_win(w); w->x = a; w->y = b; dirty_win(w); break;
    case 4: do_max(w); break;
    case 5: send(w, EV_CLOSE, 0, 0, 0, 0, 0); break;
#ifdef ZZ_TACTIL
    case 20: r = osk_on ? (a == 2 ? osk_vis : osk_set(a)) : E_NOSYS; break;   /* teclado en pantalla: a = -1 cambiar, 0 ocultar, 1 mostrar, 2 consultar */
#endif
    default: r = E_INVAL;
    }
    mutex_unlock(&wm); return r;
}
