#include "z.h"
#include "font8x16.h"

static void attach(zwin *w) { zwininfo i; zw_info(w->id, &i); w->w = i.w; w->h = i.h; w->px = (u32 *)i.canvas; zw_noclip(w); }
zwin *zw_new(const char *title, int x, int y, int cw, int ch, u32 fl) {
    extern void z_title_key(int, const char *); zwincfg c = {x, y, cw, ch, fl, (u64)tr(title)}; int id = SC1(20, &c);
    if (id < 0) return 0; z_title_key(id, title);
    zwin *w = calloc(1, sizeof *w); w->id = id; attach(w);
    return w;
}
void zw_close(zwin *w) { SC1(21, w->id); free(w); }
void zw_present(zwin *w) { sc5(22, w->id, 0, 0, 0, 0); }
void zw_present_rect(zwin *w, int x, int y, int a, int b) { sc5(22, w->id, x, y, a, b); }
void zw_resized(zwin *w, int nw, int nh) { SC3(27, w->id, nw, nh); attach(w); }
void zw_clip(zwin *w, int x, int y, int a, int b) { w->cx0 = MAX(x, 0); w->cy0 = MAX(y, 0); w->cx1 = MIN(x + a, w->w); w->cy1 = MIN(y + b, w->h); }
void zw_noclip(zwin *w) { w->cx0 = w->cy0 = 0; w->cx1 = w->w; w->cy1 = w->h; }
void g_rect(zwin *w, int x, int y, int a, int b, u32 c) {
    int x0 = MAX(x, w->cx0), y0 = MAX(y, w->cy0), x1 = MIN(x + a, w->cx1), y1 = MIN(y + b, w->cy1);
    for (int j = y0; j < y1; j++) { u32 *p = w->px + j * w->w + x0; for (int i = x0; i < x1; i++) *p++ = c; }
}
void g_clear(zwin *w, u32 c) { g_rect(w, 0, 0, w->w, w->h, c); }
void g_frame(zwin *w, int x, int y, int a, int b, u32 c) { g_rect(w, x, y, a, 1, c); g_rect(w, x, y + b - 1, a, 1, c); g_rect(w, x, y, 1, b, c); g_rect(w, x + a - 1, y, 1, b, c); }
void g_pixel(zwin *w, int x, int y, u32 c) { if (x >= w->cx0 && y >= w->cy0 && x < w->cx1 && y < w->cy1) w->px[y * w->w + x] = c; }
void g_line(zwin *w, int x0, int y0, int x1, int y1, u32 c) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1, dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) { g_pixel(w, x0, y0, c); if (x0 == x1 && y0 == y1) break; int t = 2 * e; if (t >= dy) { e += dy; x0 += sx; } if (t <= dx) { e += dx; y0 += sy; } }
}
void g_circle(zwin *w, int cx, int cy, int r, u32 c, int fill) {
    int x = r, y = 0, e = 1 - r;
    while (x >= y) {
        if (fill) { g_rect(w, cx - x, cy + y, 2 * x + 1, 1, c); g_rect(w, cx - x, cy - y, 2 * x + 1, 1, c); g_rect(w, cx - y, cy + x, 2 * y + 1, 1, c); g_rect(w, cx - y, cy - x, 2 * y + 1, 1, c); }
        else { int p[8][2] = {{x,y},{y,x},{-y,x},{-x,y},{-x,-y},{-y,-x},{y,-x},{x,-y}}; for (int i = 0; i < 8; i++) g_pixel(w, cx + p[i][0], cy + p[i][1], c); }
        y++; if (e < 0) e += 2 * y + 1; else { x--; e += 2 * (y - x) + 1; }
    }
}
static u32 mix(u32 a, u32 b, int t, int n) {
    int ar = a >> 16 & 255, ag = a >> 8 & 255, ab = a & 255, br = b >> 16 & 255, bg = b >> 8 & 255, bb = b & 255;
    int r = ar + (br - ar) * t / n, g = ag + (bg - ag) * t / n, l = ab + (bb - ab) * t / n;
    return r << 16 | g << 8 | l;
}
void g_grad(zwin *w, int x, int y, int a, int b, u32 c0, u32 c1) { for (int j = 0; j < b; j++) g_rect(w, x, y + j, a, 1, mix(c0, c1, j, b ? b : 1)); }
void g_round(zwin *w, int x, int y, int a, int b, int r, u32 c) {
    g_rect(w, x + r, y, a - 2 * r, b, c); g_rect(w, x, y + r, r, b - 2 * r, c); g_rect(w, x + a - r, y + r, r, b - 2 * r, c);
    g_circle(w, x + r, y + r, r, c, 1); g_circle(w, x + a - r - 1, y + r, r, c, 1); g_circle(w, x + r, y + b - r - 1, r, c, 1); g_circle(w, x + a - r - 1, y + b - r - 1, r, c, 1);
}
static int cp437(int c) {
    if (c < 128) return c;
    static const u16 m[][2] = {{0xE1,160},{0xE9,130},{0xED,161},{0xF3,162},{0xFA,163},{0xF1,164},{0xD1,165},{0xFC,129},{0xDC,154},{0xC9,144},{0xBF,168},{0xA1,173},{0xBA,167},{0xAA,166},{0xE7,135},{0xC7,128},{0xE0,133},{0xE8,138},{0xF2,149},{0xEC,141},{0xF9,151},{0xE2,131},{0xEA,136},{0xEE,140},{0xF4,147},{0xFB,150},{0xE4,132},{0xEB,137},{0xEF,139},{0xF6,148},{0xC4,142},{0xD6,153},{0xC1,65},{0xCD,73},{0xD3,79},{0xDA,85},{0xB0,248},{0xB7,250},{0xAB,174},{0xBB,175},{0x20AC,'E'},{0x2022,7},{0x2192,26},{0x2190,27},{0x2191,24},{0x2193,25},{0x2588,219},{0x2013,'-'},{0x2014,'-'},{0x201C,'"'},{0x201D,'"'},{0x2018,'\''},{0x2019,'\''},{0xA0,' '},{0xB4,'\''},{0xA8,'"'}};
    for (unsigned i = 0; i < sizeof m / sizeof m[0]; i++) if (m[i][0] == c) return m[i][1];
    return '?';
}
void g_glyph(zwin *w, int x, int y, int cp, u32 c) {
    const u8 *g = font8x16 + (cp & 255) * 16;
    for (int j = 0; j < 16; j++) { u8 b = g[j]; if (b) for (int i = 0; i < 8; i++) if (b & (0x80 >> i)) g_pixel(w, x + i, y + j, c); }
}
int g_text(zwin *w, int x, int y, const char *s, u32 c) { s = trl(s); int x0 = x, ch; while ((ch = utf8_next(&s))) { g_glyph(w, x, y, cp437(ch), c); x += 8; } return x - x0; }
int g_textb(zwin *w, int x, int y, const char *s, u32 c) { g_text(w, x + 1, y, s, c); return g_text(w, x, y, s, c); }
int g_text2(zwin *w, int x, int y, const char *s, u32 c, int sc) { s = trl(s);
    int x0 = x, ch;
    while ((ch = utf8_next(&s))) {
        const u8 *g = font8x16 + cp437(ch) * 16;
        for (int j = 0; j < 16; j++) for (int i = 0; i < 8; i++) if (g[j] & (0x80 >> i)) g_rect(w, x + i * sc, y + j * sc, sc, sc, c);
        x += 8 * sc;
    }
    return x - x0;
}
int g_textw(const char *s) { s = trl(s); int n = 0; while (utf8_next(&s)) n += 8; return n; }
void g_image(zwin *w, int x, int y, int a, int b, const u32 *p) {
    int x0 = MAX(x, w->cx0), y0 = MAX(y, w->cy0), x1 = MIN(x + a, w->cx1), y1 = MIN(y + b, w->cy1);
    if (x1 > x0) for (int j = y0; j < y1; j++) memcpy(w->px + j * w->w + x0, p + (j - y) * a + (x0 - x), (x1 - x0) * 4);
}
int ui_in(int mx, int my, int x, int y, int w, int h) { return mx >= x && my >= y && mx < x + w && my < y + h; }
void ui_button(zwin *w, int x, int y, int a, int b, const char *s, int st) {
    u32 bg = st == 2 ? UI_ACC : st == 1 ? UI_ACC2 : UI_WHITE, fg = st == 2 ? UI_WHITE : UI_TEXT;
    g_round(w, x, y, a, b, 3, UI_LINE); g_round(w, x + 1, y + 1, a - 2, b - 2, 2, bg); s = tr(s);
    g_text(w, x + (a - g_textw(s)) / 2, y + (b - 16) / 2, s, fg);
}
void ui_input(zwin *w, int x, int y, int a, int b, const char *t, int cur, int focus) {
    g_rect(w, x, y, a, b, UI_WHITE); g_frame(w, x, y, a, b, focus ? UI_ACC : UI_LINE);
    int ty = y + (b - 16) / 2, len = g_textw(t), sx = x + 5; const char *p = t; int cx = 0; for (int i = 0; p[i] && i < cur; ) { const char *q = p + i; utf8_next(&q); i = q - p; cx += 8; }
    if (cx > a - 14) sx -= cx - (a - 14); (void)len;
    zw_clip(w, x + 2, y + 1, a - 4, b - 2); g_text(w, sx, ty, t, UI_TEXT);
    if (focus) g_rect(w, sx + cx, ty, 1, 16, UI_TEXT);
    zw_noclip(w);
}
int ui_input_key(char *b, int cap, int *cur, zevent *e) {
    int l = strlen(b), c = *cur;
    if (e->key == K_LEFT) { while (c > 0) { c--; if ((b[c] & 0xC0) != 0x80) break; } }
    else if (e->key == K_RIGHT) { if (c < l) { c++; while (c < l && (b[c] & 0xC0) == 0x80) c++; } }
    else if (e->key == K_HOME) c = 0;
    else if (e->key == K_END) c = l;
    else if (e->ch == 22) { char t[512]; int n = z_clip_get(t, MIN(cap - l - 1, 511)); int k = 0; for (int i = 0; i < n; i++) if ((u8)t[i] >= 32) t[k++] = t[i]; if (k > 0 && l + k < cap) { memmove(b + c + k, b + c, l - c + 1); memcpy(b + c, t, k); c += k; } }
    else if (e->ch == 3) { z_clip_set(b, l); }
    else if (e->ch == 24) { z_clip_set(b, l); b[0] = 0; c = 0; }
    else if (e->ch == 8) { if (c > 0) { int s = c; while (--s > 0 && (b[s] & 0xC0) == 0x80); memmove(b + s, b + c, l - c + 1); c = s; } }
    else if (e->ch == 127) { if (c < l) { int t = c + 1; while (t < l && (b[t] & 0xC0) == 0x80) t++; memmove(b + c, b + t, l - t + 1); } }
    else if (e->ch == 22) { char t[256]; int n = z_clip_get(t, MIN(255, cap - l - 1)); for (int i = 0; i < n; i++) if (t[i] == '\n' || t[i] == '\r') t[i] = ' '; memmove(b + c + n, b + c, l - c + 1); memcpy(b + c, t, n); c += n; }
    else if (e->ch >= 32) { char u[4]; int n = utf8_put(e->ch, u); if (l + n >= cap) return 0; memmove(b + c + n, b + c, l - c + 1); memcpy(b + c, u, n); c += n; }
    else return 0;
    *cur = c; return 1;
}
/* dialogos modales: ventana propia con su bucle */
static int dialog(const char *title, const char *msg, char *buf, int cap) {
    msg = tr(msg);
    int w = MAX(340, g_textw(msg) + 40), h = buf ? 130 : 104; zsys s; z_sysinfo(&s);
    zwin *d = zw_new(title, (s.scr_w - w) / 2, (s.scr_h - h) / 2, w, h, WF_DECOR); if (!d) return 0;
    int cur = buf ? strlen(buf) : 0, res = -1, redraw = 1, by = h - 38; zevent e;
    while (res < 0) {
        if (redraw) {
            g_clear(d, UI_BG); g_text(d, 16, 14, msg, UI_TEXT);
            if (buf) ui_input(d, 16, 40, w - 32, 26, buf, cur, 1);
            ui_button(d, w - 196, by, 84, 28, "Cancelar", 0); ui_button(d, w - 100, by, 84, 28, "Aceptar", 2); zw_present(d); redraw = 0;
        }
        if (!z_event(&e, -1) || e.win != (u32)d->id) continue;
        if (e.type == EV_CLOSE) res = 0;
        else if (e.type == EV_KEY) { if (e.ch == '\n') res = 1; else if (e.ch == 27) res = 0; else if (buf && ui_input_key(buf, cap, &cur, &e)) redraw = 1; }
        else if (e.type == EV_DOWN) { if (ui_in(e.x, e.y, w - 100, by, 84, 28)) res = 1; else if (ui_in(e.x, e.y, w - 196, by, 84, 28)) res = 0; }
    }
    zw_close(d); return res;
}
int ui_prompt(const char *t, const char *m, char *b, int c) { return dialog(t, m, b, c); }
int ui_confirm(const char *t, const char *m) { return dialog(t, m, 0, 0); }
const u8 *z_glyph(int cp) { return font8x16 + cp437(cp) * 16; }
