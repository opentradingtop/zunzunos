/* Seleccionar texto con el raton en cualquier programa.
   El programa marca la zona de texto al empezar a dibujar (zs_area); desde ese momento g_text y g_text2
   anotan donde cae cada letra dentro de la zona. Con eso, arrastrar el raton selecciona en orden de lectura,
   zs_show tine lo seleccionado, Ctrl+C lo copia y el boton derecho ofrece Copiar y Seleccionar todo.
   Las posiciones se guardan en coordenadas del documento (y + desplazamiento), asi la seleccion aguanta
   el desplazamiento mientras el texto siga a la vista. */
#include "z.h"

typedef struct { int x, y, cw, h, off, n, line; short *cx; } zrun;
typedef struct { int ax, ay, aw, ah, scroll, rec, nr, cr, nt, ct, down, drag, sel, sx, sy, fx, fy, all; zrun *r; char *t; } zsel_t;

static zsel_t *S(zwin *w) { if (!w->zs) w->zs = calloc(1, sizeof(zsel_t)); return w->zs; }

void zs_area(zwin *w, int x, int y, int a, int b, int scroll) {
    zsel_t *s = S(w); for (int i = 0; i < s->nr; i++) free(s->r[i].cx);
    s->ax = x; s->ay = y; s->aw = a; s->ah = b; s->scroll = scroll; s->nr = 0; s->nt = 0; s->rec = 1; }
void zs_stop(zwin *w) { if (w->zs) ((zsel_t *)w->zs)->rec = 0; }
int zs_has(zwin *w) { zsel_t *s = w->zs; return s && (s->sel || s->all); }
void zs_clear(zwin *w) { zsel_t *s = w->zs; if (s) { s->sel = s->all = s->drag = s->down = 0; } }
void zs_all(zwin *w) { zsel_t *s = S(w); s->all = 1; s->sel = 0; }

/* anota un trozo de texto; cx (opcional) da la x de cada letra para tipografias proporcionales */
static void add(zwin *w, int x, int y, int cw, int h, const char *txt, int bytes, const short *cx, int n) {
    zsel_t *s = w->zs; if (!s || !s->rec || !n) return;
    int x0 = MAX(s->ax, w->cx0), y0 = MAX(s->ay, w->cy0), x1 = MIN(s->ax + s->aw, w->cx1), y1 = MIN(s->ay + s->ah, w->cy1);
    if (y + h <= y0 || y >= y1) return; int last = cx ? cx[n - 1] + cw : n * cw; if (x + last <= x0 || x >= x1) return;
    if (s->nr == s->cr) { s->cr = s->cr ? s->cr * 2 : 64; s->r = realloc(s->r, s->cr * sizeof *s->r); }
    if (s->nt + bytes + 1 > s->ct) { s->ct = MAX(s->ct * 2, s->nt + bytes + 4096); s->t = realloc(s->t, s->ct); }
    zrun *r = &s->r[s->nr++]; r->x = x; r->y = y + s->scroll; r->cw = cw; r->h = h; r->off = s->nt; r->n = n; r->line = 0; r->cx = 0;
    if (cx) { r->cx = malloc(n * sizeof(short)); memcpy(r->cx, cx, n * sizeof(short)); }
    memcpy(s->t + s->nt, txt, bytes); s->nt += bytes; s->t[s->nt++] = 0; }
void zs_rec(zwin *w, int x, int y, int sc, const char *txt) {
    if (!w->zs || !((zsel_t *)w->zs)->rec) return; const char *p = txt; int n = 0; while (utf8_next(&p)) n++; add(w, x, y, 8 * sc, 16 * sc, txt, p - txt, 0, n); }
void zs_add(zwin *w, int x, int y, int h, const char *txt, const short *cx, int n, int cw) {
    if (!w->zs) return; int b = strlen(txt); add(w, x, y, cw, h, txt, b, cx, n); }

/* orden de lectura: los trozos se agrupan en lineas por su altura y dentro de cada linea por x */
static int cmp_y(const void *a, const void *b) { const zrun *p = a, *q = b; return p->y != q->y ? p->y - q->y : p->x - q->x; }
static int cmp_l(const void *a, const void *b) { const zrun *p = a, *q = b; return p->line != q->line ? p->line - q->line : p->x - q->x; }
static void order(zsel_t *s) { if (!s->nr) return; qsort(s->r, s->nr, sizeof *s->r, cmp_y); int line = 0, bot = s->r[0].y + s->r[0].h;
    for (int i = 0; i < s->nr; i++) { zrun *r = &s->r[i]; if (r->y + r->h / 2 >= bot) { line++; bot = r->y + r->h; } else bot = MAX(bot, r->y + r->h); r->line = line; }
    qsort(s->r, s->nr, sizeof *s->r, cmp_l); }
static int cxof(zrun *r, int i) { return r->x + (r->cx ? r->cx[i] : i * r->cw); }
static int cwof(zrun *r, int i) { return r->cx ? (i + 1 < r->n ? r->cx[i + 1] - r->cx[i] : r->cw) : r->cw; }
/* indice de cursor (letra global) para un punto en coordenadas del documento */
static int caret(zsel_t *s, int px, int py) { int k = 0;
    for (int i = 0; i < s->nr; i++) { zrun *r = &s->r[i]; int top = r->y, bot = r->y + r->h;
        /* la linea entera ocupa de la parte de arriba del trozo mas alto a la de abajo del mas bajo */
        for (int j = i - 1; j >= 0 && s->r[j].line == r->line; j--) { top = MIN(top, s->r[j].y); bot = MAX(bot, s->r[j].y + s->r[j].h); }
        for (int j = i + 1; j < s->nr && s->r[j].line == r->line; j++) { top = MIN(top, s->r[j].y); bot = MAX(bot, s->r[j].y + s->r[j].h); }
        for (int c = 0; c < r->n; c++, k++) if (py < top || (py < bot && px < cxof(r, c) + cwof(r, c) / 2)) return k; }
    return k; }
static void range(zsel_t *s, int *a, int *b) { if (s->all) { *a = 0; *b = 1 << 30; return; } int p = caret(s, s->sx, s->sy), q = caret(s, s->fx, s->fy); *a = MIN(p, q); *b = MAX(p, q); }

void zs_show(zwin *w) { zsel_t *s = w->zs; if (!s) return; s->rec = 0; if (!s->sel && !s->all) return; order(s); int a, b; range(s, &a, &b); if (a >= b) return;
    int x0 = MAX(s->ax, 0), y0 = MAX(s->ay, 0), x1 = MIN(s->ax + s->aw, w->w), y1 = MIN(s->ay + s->ah, w->h), k = 0;
    for (int i = 0; i < s->nr; i++) { zrun *r = &s->r[i]; for (int c = 0; c < r->n; c++, k++) { if (k < a || k >= b) continue;
        int cx = cxof(r, c), cw = cwof(r, c), cy = r->y - s->scroll;
        for (int y = MAX(cy, y0); y < MIN(cy + r->h, y1); y++) { u32 *p = w->px + y * w->w; for (int x = MAX(cx, x0); x < MIN(cx + cw, x1); x++) {
            u32 v = p[x]; p[x] = ((((v >> 16) & 255) * 5 + 0x25 * 3) / 8) << 16 | ((((v >> 8) & 255) * 5 + 0x63 * 3) / 8) << 8 | (((v & 255) * 5 + 0xEB * 3) / 8); } } } } }

/* texto seleccionado: salto de linea entre lineas y un espacio donde hay hueco entre trozos */
char *zs_text(zwin *w) { zsel_t *s = w->zs; if (!s || (!s->sel && !s->all)) return 0; order(s); int a, b; range(s, &a, &b); sb_t o = {0}; sb_s(&o, ""); int k = 0, pl = -1, pend = -1000000;
    for (int i = 0; i < s->nr; i++) { zrun *r = &s->r[i]; const char *p = s->t + r->off;
        for (int c = 0; c < r->n; c++, k++) { const char *q = p; utf8_next(&q); if (k >= a && k < b) {
            int cx = cxof(r, c); if (pl >= 0 && r->line != pl) { while (o.n && o.p[o.n - 1] == ' ') o.n--; sb_s(&o, "\n"); } else if (pl >= 0 && cx - pend > r->cw / 2 && o.n && o.p[o.n - 1] != ' ') sb_s(&o, " ");
            sb_add(&o, p, q - p); pl = r->line; pend = cx + cwof(r, c); } p = q; } }
    while (o.n && (o.p[o.n - 1] == ' ' || o.p[o.n - 1] == '\n')) o.n--; o.p[o.n] = 0; return o.p; }
int zs_copy(zwin *w) { char *t = zs_text(w); if (!t) return 0; int n = strlen(t); if (n) z_clip_set(t, n); free(t); return n; }

static int inside(zsel_t *s, int x, int y) { return x >= s->ax && y >= s->ay && x < s->ax + s->aw && y < s->ay + s->ah; }
/* raton y teclado. Devuelve 1 si el evento era de la seleccion (el programa solo tiene que redibujar),
   0 si el programa debe atenderlo como siempre. Pulsar sin arrastrar llega al programa (enlaces, botones). */
int zs_event(zwin *w, zevent *e) { zsel_t *s = S(w);
    if (e->type == EV_DOWN && (e->btn & 1) && inside(s, e->x, e->y)) { s->down = 1; s->drag = 0; s->sx = s->fx = e->x; s->sy = s->fy = e->y + s->scroll; s->sel = s->all = 0; return 0; }
    if (e->type == EV_DOWN && (e->btn & 2) && (s->sel || s->all) && inside(s, e->x, e->y)) {
        static const char *it[] = {"Copiar", "Seleccionar todo"}; int sx, sy; zw_screen(w, e->x, e->y, &sx, &sy); int r = ui_menu(sx, sy, it, 2);
        if (r == 0) zs_copy(w); else if (r == 1) zs_all(w); return 1; }
    if (e->type == EV_MOVE && s->down) { if (!e->btn) { s->down = s->drag = 0; return 0; } int y = e->y + s->scroll;
        if (!s->drag && (e->x - s->sx) * (e->x - s->sx) + (y - s->sy) * (y - s->sy) < 16) return 0;
        s->drag = 1; s->sel = 1; s->fx = e->x; s->fy = y; return 1; }
    if (e->type == EV_UP && s->down) { s->down = 0; if (s->drag) { s->drag = 0; s->fx = e->x; s->fy = e->y + s->scroll; char lg[48]; char *t = zs_text(w); snprintf(lg, 48, "[prueba] seleccion %d letras", t ? (int)strlen(t) : 0); free(t); z_log(lg); return 1; } return 0; }
    if (e->type == EV_KEY && e->ch == 3 && (s->sel || s->all)) { int n = zs_copy(w); char lg[48]; snprintf(lg, 48, "[prueba] copiado %d letras", n); z_log(lg); return 1; }
    if (e->type == EV_KEY && e->key == K_ESC) s->sel = s->all = 0;   /* Esc quita la seleccion y sigue llegando al programa */
    return 0; }
/* desplazamiento con la rueda o al arrastrar fuera de la zona: el programa llama a esto al cambiar scroll */
int zs_dragging(zwin *w) { zsel_t *s = w->zs; return s && s->drag; }
