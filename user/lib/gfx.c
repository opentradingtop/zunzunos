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
    static const u16 m[][2] = {{0xE1,160},{0xE9,130},{0xED,161},{0xF3,162},{0xFA,163},{0xF1,164},{0xD1,165},{0xFC,129},{0xDC,154},{0xC9,144},{0xBF,168},{0xA1,173},{0xBA,167},{0xAA,166},{0xE7,135},{0xC7,128},{0xE0,133},{0xE8,138},{0xF2,149},{0xEC,141},{0xF9,151},{0xE2,131},{0xEA,136},{0xEE,140},{0xF4,147},{0xFB,150},{0xE4,132},{0xEB,137},{0xEF,139},{0xF6,148},{0xC4,142},{0xD6,153},{0xC1,65},{0xCD,73},{0xD3,79},{0xDA,85},{0xB0,248},{0xB7,250},{0xAB,174},{0xBB,175},{0x20AC,'E'},{0x2022,7},{0x2192,26},{0x2190,27},{0x2191,24},{0x2193,25},{0x2588,219},{0x2013,'-'},{0x2014,'-'},{0x201C,'"'},{0x201D,'"'},{0x2018,'\''},{0x2019,'\''},{0xA0,' '},{0xB4,'\''},{0xA8,'"'},
        {0x2500,196},{0x2502,179},{0x250C,218},{0x2510,191},{0x2514,192},{0x2518,217},{0x251C,195},{0x2524,180},{0x252C,194},{0x2534,193},{0x253C,197},{0x2550,205},{0x2551,186},{0x2554,201},{0x2557,187},{0x255A,200},{0x255D,188},{0x2591,176},{0x2592,177},{0x2593,178},{0x2580,223},{0x2584,220},{0x258C,221},{0x2590,222},{0x25A0,254},
        {0x25BA,16},{0x25B6,16},{0x25C4,17},{0x25C0,17},{0x25B2,30},{0x25BC,31},{0x2195,18},{0x2194,29},{0x2665,3},{0x2666,4},{0x2663,5},{0x2660,6},{0x263A,1},{0x263B,2},{0x263C,15},{0x266A,13},{0x266B,14},{0x25CB,9},{0x25D8,8},{0x25D9,10},{0x2219,249},{0x221A,251},{0x221E,236},{0x2248,247},{0x2261,240},{0x2264,243},{0x2265,242},{0x2229,239},{0x207F,252},
        {0xB1,241},{0xB2,253},{0xBD,171},{0xBC,172},{0xF7,246},{0xAC,170},{0xA3,156},{0xA5,157},{0xA2,155},{0x192,159},{0xB5,230},{0xB6,20},{0xA7,21},{0xDF,225},{0xC5,143},{0xE5,134},{0xC6,146},{0xE6,145},{0xFF,152},{0xE3,'a'},{0xF5,'o'},{0xC3,'A'},{0xD5,'O'},{0xC0,'A'},{0xC8,'E'},{0xCC,'I'},{0xD2,'O'},{0xD9,'U'},{0xC2,'A'},{0xCA,'E'},{0xCE,'I'},{0xD4,'O'},{0xDB,'U'},{0xCB,'E'},{0xCF,'I'},{0xF8,237},{0xD8,237},
        {0x3B1,224},{0x393,226},{0x3C0,227},{0x3A3,228},{0x3C3,229},{0x3C4,231},{0x3A6,232},{0x398,233},{0x3A9,234},{0x3B4,235},{0x3C6,237},{0x3B5,238},{0x3B2,225},{0x3BC,230},{0x2030,'%'},{0x2039,'<'},{0x203A,'>'},{0x2044,'/'},{0x2215,'/'},{0x2217,'*'},{0x22C5,250},{0x2027,250},{0x2010,'-'},{0x2011,'-'},{0x2012,'-'},{0x2015,'-'},{0x2212,'-'},{0x201A,','},{0x201E,'"'},{0x2032,'\''},{0x2033,'"'},{0xD7,'x'},{0x2713,251},{0x2714,251}};
    for (unsigned i = 0; i < sizeof m / sizeof m[0]; i++) if (m[i][0] == c) return m[i][1];
    return '?';
}
void g_glyph(zwin *w, int x, int y, int cp, u32 c) {
    const u8 *g = font8x16 + (cp & 255) * 16;
    for (int j = 0; j < 16; j++) { u8 b = g[j]; if (b) for (int i = 0; i < 8; i++) if (b & (0x80 >> i)) g_pixel(w, x + i, y + j, c); }
}
int g_text(zwin *w, int x, int y, const char *s, u32 c) { s = trl(s); if (w->zs) zs_rec(w, x, y, 1, s); int x0 = x, ch; while ((ch = utf8_next(&s))) { g_glyph(w, x, y, cp437(ch), c); x += 8; } return x - x0; }
int g_textb(zwin *w, int x, int y, const char *s, u32 c) { void *z = w->zs; w->zs = 0; g_text(w, x + 1, y, s, c); w->zs = z; return g_text(w, x, y, s, c); }
int g_text2(zwin *w, int x, int y, const char *s, u32 c, int sc) { s = trl(s); if (w->zs) zs_rec(w, x, y, sc, s);
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
/* casillas de una linea: el ancla de la seleccion se guarda aparte, por direccion del texto, para no cambiar
   la forma de llamar a ui_input/ui_input_key en todos los programas */
static struct { const char *b; int a; } ANC[24];
static int anc_get(const char *b) { for (int i = 0; i < 24; i++) if (ANC[i].b == b) return ANC[i].a; return -1; }
static void anc_set(const char *b, int a) { int f = -1; for (int i = 0; i < 24; i++) { if (ANC[i].b == b) { if (a < 0) ANC[i].b = 0; else ANC[i].a = a; return; } if (!ANC[i].b && f < 0) f = i; }
    if (a < 0) return; if (f < 0) { f = 0; memmove(ANC, ANC + 1, 23 * sizeof ANC[0]); f = 23; } ANC[f].b = b; ANC[f].a = a; }
static int colx(const char *t, int n) { int cx = 0; for (int i = 0; t[i] && i < n; ) { const char *q = t + i; utf8_next(&q); i = q - t; cx += 8; } return cx; }
/* casillas dibujadas hace poco: para saber en que letra cae un clic */
typedef struct { const char *b; zwin *w; int x, y, a, h, sx; } fld_t; static fld_t FL[32]; static int nfl;
static void fld_note(zwin *w, const char *b, int x, int y, int a, int h, int sx) {
    int k = -1; for (int i = 0; i < nfl; i++) if (FL[i].b == b && FL[i].w == w) k = i;
    if (k < 0) { if (nfl < 32) k = nfl++; else { memmove(FL, FL + 1, 31 * sizeof FL[0]); k = 31; } }
    FL[k] = (fld_t){ b, w, x, y, a, h, sx }; }
/* clics seguidos en el mismo sitio (doble y triple clic): 1, 2 o 3 */
int ui_clicks(zevent *e) {
    static u64 t0; static int x0, y0, n; u64 t = z_ms();
    int dx = e->x - x0, dy = e->y - y0; if (t - t0 < 450 && dx > -5 && dx < 5 && dy > -5 && dy < 5) n = n >= 3 ? 3 : n + 1; else n = 1;
    t0 = t; x0 = e->x; y0 = e->y; return n; }
/* posicion (en bytes) de la letra mas cercana a la x de pantalla px, con la casilla empezando en sx */
static int pos_at(const char *t, int sx, int px) { int i = 0, cx = 0;
    while (t[i]) { const char *q = t + i; utf8_next(&q); if (px < sx + cx + 4) break; i = q - t; cx += 8; } return i; }
static int wordc(const char *t, int i) { u8 c = t[i]; return c >= 0x80 || (c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'z') || c == '_'; }
/* clic en una casilla: 1 clic pone el cursor donde se pulsa, 2 eligen la palabra, 3 todo el texto */
extern zevent z_last_down;
int ui_input_click(zwin *w, zevent *e, const char *b, int *cur) {
    if (!e) e = &z_last_down; int n = ui_clicks(e), l = strlen(b), p = l; fld_t *f = 0;
    for (int i = 0; i < nfl; i++) if (FL[i].b == b && FL[i].w == w) f = &FL[i];
    if (f) p = pos_at(b, f->sx, e->x);
    if (n == 3) { anc_set(b, 0); *cur = l; }
    else if (n == 2 && l) { int s = MIN(p, l), q = s; if (s == l || !wordc(b, s)) { if (s > 0) s--; q = s; }
        if (wordc(b, s)) { while (s > 0 && wordc(b, s - 1)) s--; q = s; while (q < l && wordc(b, q)) q++; } else q = s + 1;
        anc_set(b, s); *cur = q; }
    else { anc_set(b, -1); *cur = p; }
    return n; }
/* elige todo el texto de la casilla (Ctrl+A, o al entrar en la barra de direcciones) */
void ui_input_selall(const char *b, int *cur) { anc_set(b, 0); *cur = strlen(b); }
int ui_input_hassel(const char *b, int cur) { int a = anc_get(b); return a >= 0 && a != cur; }
void ui_input(zwin *w, int x, int y, int a, int b, const char *t, int cur, int focus) {
    g_rect(w, x, y, a, b, UI_WHITE); g_frame(w, x, y, a, b, focus ? UI_ACC : UI_LINE);
    int ty = y + (b - 16) / 2, len = g_textw(t), sx = x + 5, cx = colx(t, cur);
    if (cx > a - 14) sx -= cx - (a - 14); (void)len; fld_note(w, t, x, y, a, b, sx);
    zw_clip(w, x + 2, y + 1, a - 4, b - 2); int an = focus ? anc_get(t) : -1;
    if (an >= 0 && an != cur) { int p = colx(t, MIN(an, cur)), q = colx(t, MAX(an, cur)); g_rect(w, sx + p, ty, q - p, 16, 0xBFDBFE); }
    g_text(w, sx, ty, t, UI_TEXT);
    if (focus) g_rect(w, sx + cx, ty, 1, 16, UI_TEXT);
    zw_noclip(w);
}
/* borra lo seleccionado (si hay) y deja el cursor en su lugar */
static int del_sel(char *b, int *c) { int an = anc_get(b), l = strlen(b); anc_set(b, -1); if (an < 0 || an == *c) return 0; an = MIN(an, l); int p = MIN(an, *c), q = MAX(an, *c); memmove(b + p, b + q, l - q + 1); *c = p; return 1; }
int ui_input_key(char *b, int cap, int *cur, zevent *e) {
    int l = strlen(b), c = MIN(*cur, l), sh = e->mods & M_SHIFT, an = anc_get(b); if (an > l) { an = -1; anc_set(b, -1); }
    int mv = e->key == K_LEFT || e->key == K_RIGHT || e->key == K_HOME || e->key == K_END;
    if (mv) { if (sh) { if (an < 0) anc_set(b, c); } else anc_set(b, -1);
        if (e->key == K_LEFT) { if (!sh && an >= 0 && an != c) c = MIN(an, c); else while (c > 0) { c--; if ((b[c] & 0xC0) != 0x80) break; } }
        else if (e->key == K_RIGHT) { if (!sh && an >= 0 && an != c) c = MAX(an, c); else if (c < l) { c++; while (c < l && (b[c] & 0xC0) == 0x80) c++; } }
        else if (e->key == K_HOME) c = 0; else c = l; }
    else if (e->ch == 1) { anc_set(b, 0); c = l; }                                   /* Ctrl+A: todo */
    else if (e->ch == 3 || e->ch == 24) { int p = an >= 0 && an != c ? MIN(an, c) : 0, q = an >= 0 && an != c ? MAX(an, c) : l;   /* Ctrl+C / Ctrl+X: lo seleccionado o todo */
        if (q > p) z_clip_set(b + p, q - p); if (e->ch == 24) { memmove(b + p, b + q, l - q + 1); c = p; anc_set(b, -1); } }
    else if (e->ch == 22) { char t[512]; del_sel(b, &c); l = strlen(b); int n = z_clip_get(t, MIN(cap - l - 1, 511)), k = 0;   /* Ctrl+V */
        for (int i = 0; i < n; i++) { if (t[i] == '\n' || t[i] == '\r' || t[i] == '\t') t[k++] = ' '; else if ((u8)t[i] >= 32) t[k++] = t[i]; } while (k && (t[k - 1] & 0xC0) == 0x80) { int s = k - 1; while (s && (t[s] & 0xC0) == 0x80) s--; if (k - s < ((u8)t[s] >= 0xF0 ? 4 : (u8)t[s] >= 0xE0 ? 3 : 2)) k = s; else break; }
        if (k > 0 && l + k < cap) { memmove(b + c + k, b + c, l - c + 1); memcpy(b + c, t, k); c += k; } }
    else if (e->ch == 8) { if (!del_sel(b, &c) && c > 0) { int s = c; while (--s > 0 && (b[s] & 0xC0) == 0x80); memmove(b + s, b + c, l - c + 1); c = s; } }
    else if (e->ch == 127) { if (!del_sel(b, &c) && c < l) { int t = c + 1; while (t < l && (b[t] & 0xC0) == 0x80) t++; memmove(b + c, b + t, l - t + 1); } }
    else if (e->ch >= 32) { char u[4]; int n = utf8_put(e->ch, u); del_sel(b, &c); l = strlen(b); if (l + n >= cap) { *cur = c; return 1; } memmove(b + c + n, b + c, l - c + 1); memcpy(b + c, u, n); c += n; }
    else return 0;
    *cur = c; return 1;
}
/* boton derecho en una casilla: cortar, copiar, pegar y seleccionar todo */
int ui_input_menu(zwin *w, char *b, int cap, int *cur, zevent *e) { static const char *it[] = {"Cortar", "Copiar", "Pegar", "Seleccionar todo"}; int sx, sy; zw_screen(w, e->x, e->y, &sx, &sy);
    int r = ui_menu(sx, sy, it, 4); if (r < 0) return 0; static const int ch[] = {24, 3, 22, 1}; zevent k = {0}; k.type = EV_KEY; k.ch = ch[r]; k.mods = M_CTRL; return ui_input_key(b, cap, cur, &k); }
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
        else if (e.type == EV_DOWN) { if (ui_in(e.x, e.y, w - 100, by, 84, 28)) res = 1; else if (ui_in(e.x, e.y, w - 196, by, 84, 28)) res = 0;
            else if (buf && ui_in(e.x, e.y, 16, 40, w - 32, 26)) { if (e.btn & 2) ui_input_menu(d, buf, cap, &cur, &e); else ui_input_click(d, &e, buf, &cur); redraw = 1; } }
    }
    zw_close(d); return res;
}
int ui_prompt(const char *t, const char *m, char *b, int c) { return dialog(t, m, b, c); }
int ui_confirm(const char *t, const char *m) { return dialog(t, m, 0, 0); }
const u8 *z_glyph(int cp) { return font8x16 + cp437(cp) * 16; }
