/* Impresion: un documento es una lista de dibujo (texto, rectangulos, lineas, imagenes) en puntos (1/72").
   Salidas: PDF, PostScript, PCL 5, PWG Raster (IPP Everywhere) y texto. Envio: USB, red (9100), IPP o archivo. */
#include "z.h"
#include "zp_metrics.h"

enum { IT_PAGE = 1, IT_TEXT, IT_RECT, IT_LINE, IT_IMG };
typedef struct { u8 t, font; float x, y, w, h, size, lw; u32 c, c2; u32 *px; int iw, ih; char *s; } zpi;
struct zpdoc { zpi *it; int n, cap, pages; double pw, ph; };

/* ---------- documento ---------- */
static zpi *add(zpdoc *d, int t) { if (d->n == d->cap) { d->cap = d->cap ? d->cap * 2 : 256; d->it = realloc(d->it, d->cap * sizeof(zpi)); } zpi *i = &d->it[d->n++]; memset(i, 0, sizeof *i); i->t = t; return i; }
zpdoc *zp_new(const zpset *s) { zpdoc *d = calloc(1, sizeof *d); d->pw = s->land ? s->ph : s->pw; d->ph = s->land ? s->pw : s->ph; return d; }
void zp_page(zpdoc *d) { add(d, IT_PAGE); d->pages++; }
static void need_page(zpdoc *d) { if (!d->pages) zp_page(d); }
int zp_npages(zpdoc *d) { return d->pages; }
double zp_pw(zpdoc *d) { return d->pw; }
double zp_ph(zpdoc *d) { return d->ph; }
void zp_free(zpdoc *d) { if (!d) return; for (int i = 0; i < d->n; i++) { free(d->it[i].s); free(d->it[i].px); } free(d->it); free(d); }
void zp_text(zpdoc *d, double x, double y, int font, double size, u32 col, const char *s) { need_page(d); zpi *i = add(d, IT_TEXT); i->x = x; i->y = y; i->font = font; i->size = size; i->c = col; i->s = strdup(s); }
void zp_rect(zpdoc *d, double x, double y, double w, double h, u32 fill, u32 stroke, double lw) { need_page(d); zpi *i = add(d, IT_RECT); i->x = x; i->y = y; i->w = w; i->h = h; i->c = fill; i->c2 = stroke; i->lw = lw; }
void zp_line(zpdoc *d, double x0, double y0, double x1, double y1, u32 col, double lw) { need_page(d); zpi *i = add(d, IT_LINE); i->x = x0; i->y = y0; i->w = x1; i->h = y1; i->c = col; i->lw = lw; }
void zp_image(zpdoc *d, double x, double y, double w, double h, const u32 *px, int iw, int ih) { need_page(d); zpi *i = add(d, IT_IMG); i->x = x; i->y = y; i->w = w; i->h = h; i->iw = iw; i->ih = ih; i->px = malloc((long)iw * ih * 4); memcpy(i->px, px, (long)iw * ih * 4); }

/* ---------- texto: UTF-8 -> WinAnsi y anchuras ---------- */
static int winansi(u32 c) {
    if (c < 0x80 || (c >= 0xA0 && c <= 0xFF)) return c;
    static const u16 m[][2] = {{0x20AC, 0x80}, {0x201A, 0x82}, {0x0192, 0x83}, {0x201E, 0x84}, {0x2026, 0x85}, {0x2020, 0x86}, {0x2021, 0x87}, {0x02C6, 0x88}, {0x2030, 0x89}, {0x0160, 0x8A}, {0x2039, 0x8B}, {0x0152, 0x8C}, {0x017D, 0x8E}, {0x2018, 0x91}, {0x2019, 0x92}, {0x201C, 0x93}, {0x201D, 0x94}, {0x2022, 0x95}, {0x2013, 0x96}, {0x2014, 0x97}, {0x02DC, 0x98}, {0x2122, 0x99}, {0x0161, 0x9A}, {0x203A, 0x9B}, {0x0153, 0x9C}, {0x017E, 0x9E}, {0x0178, 0x9F}};
    for (unsigned i = 0; i < sizeof m / sizeof m[0]; i++) if (m[i][0] == c) return m[i][1];
    return '?';
}
static int to_win(const char *s, u8 *o, int cap) { int n = 0, c; while (n < cap - 1 && (c = utf8_next(&s))) { if (c == '\t') c = ' '; if (c < 32) continue; o[n++] = winansi(c); } o[n] = 0; return n; }
static int cw(int font, int c) { if (c < 32) return 0; if (font >= ZPF_MONO) return 600; return ZPW[font & 3][c - 32]; }
double zp_textw(int font, double size, const char *s) { u8 b[1024]; int n = to_win(s, b, sizeof b); long w = 0; for (int i = 0; i < n; i++) w += cw(font, b[i]); return w * size / 1000.0; }

/* flujo de parrafos con salto de linea por palabras y de pagina automatico */
void zp_flow_init(zpflow *f, zpdoc *d, double margin) { f->d = d; f->x0 = margin; f->x1 = d->pw - margin; f->ytop = margin; f->ybot = d->ph - margin; f->y = f->ytop; f->started = 0; f->num = 0; }
void zp_flow_newpage(zpflow *f) { zp_page(f->d); f->y = f->ytop; f->started = 1; if (f->num) { char t[16]; snprintf(t, 16, "%d", zp_npages(f->d)); zp_text(f->d, (f->x0 + f->x1) / 2 - zp_textw(ZPF_SERIF, 10, t) / 2, f->ybot + 30, ZPF_SERIF, 10, 0x444444, t); } }
void zp_para(zpflow *f, int font, double size, u32 col, const char *text, int align, double after) {
    if (!f->started) zp_flow_newpage(f);
    double lh = size * 1.3, maxw = f->x1 - f->x0; const char *p = text; char line[1024];
    if (!*p) { f->y += lh; if (f->y > f->ybot) zp_flow_newpage(f); return; }
    while (*p) {
        int n = 0, last_sp = -1; const char *q = p, *cut = 0;
        while (*q && *q != '\n' && n < 1000) { const char *q0 = q; int c = utf8_next(&q); int l = q - q0; memcpy(line + n, q0, l); n += l; line[n] = 0; if (c == ' ') { last_sp = n - 1; cut = q; } if (zp_textw(font, size, line) > maxw) { if (last_sp > 0) { n = last_sp; q = cut; } else { n -= l; q = q0; } break; } }
        line[n] = 0; if (f->y + size > f->ybot) zp_flow_newpage(f);
        double x = f->x0, w = zp_textw(font, size, line); if (align == 1) x = f->x0 + (maxw - w) / 2; else if (align == 2) x = f->x1 - w;
        zp_text(f->d, x, f->y + size, font, size, col, line); f->y += lh;
        if (*q == '\n') q++; while (*q == ' ' && q != p) q++; if (q == p) break; p = q;
    }
    f->y += after;
}

/* ---------- buffer de salida ---------- */
typedef struct { u8 *p; long n, cap; } ob_t;
static void ob_add(ob_t *b, const void *s, long n) { if (b->n + n > b->cap) { b->cap = MAX(b->cap * 2, b->n + n + 65536); b->p = realloc(b->p, b->cap); } memcpy(b->p + b->n, s, n); b->n += n; }
static void ob_s(ob_t *b, const char *s) { ob_add(b, s, strlen(s)); }
static void ob_f(ob_t *b, const char *f, ...) { char t[512]; va_list a; va_start(a, f); int n = vsnprintf(t, sizeof t, f, a); va_end(a); ob_add(b, t, MIN(n, (int)sizeof t - 1)); }
static void ob_c(ob_t *b, int c) { u8 x = c; ob_add(b, &x, 1); }
static void ob_be32(ob_t *b, u32 v) { u8 x[4] = {v >> 24, v >> 16, v >> 8, v}; ob_add(b, x, 4); }
static void ob_be16(ob_t *b, u32 v) { u8 x[2] = {v >> 8, v}; ob_add(b, x, 2); }
static double R(u32 c) { return ((c >> 16) & 255) / 255.0; } static double G(u32 c) { return ((c >> 8) & 255) / 255.0; } static double B(u32 c) { return (c & 255) / 255.0; }
static u32 grayc(u32 c) { int g = (((c >> 16) & 255) * 30 + ((c >> 8) & 255) * 59 + (c & 255) * 11) / 100; return g << 16 | g << 8 | g; }
/* imprime un numero con 2 decimales sin depender de printf %f */
static void ob_num(ob_t *b, double v) { long x = (long)(v * 100 + (v < 0 ? -0.5 : 0.5)); if (x < 0) { ob_c(b, '-'); x = -x; } ob_f(b, "%ld.%02ld ", x / 100, x % 100); }
static int sel_page(const zpset *s, int pg) { return (s->from <= 0 || pg >= s->from) && (s->to <= 0 || pg <= s->to); }
/* recorre las paginas elegidas: devuelve el indice del primer elemento de la pagina o -1 */
static int page_start(zpdoc *d, int pg) { int p = 0; for (int i = 0; i < d->n; i++) if (d->it[i].t == IT_PAGE && ++p == pg) return i + 1; return -1; }

/* ---------- PDF ---------- */
static const char *PDFN[] = {"Helvetica", "Helvetica-Bold", "Times-Roman", "Times-Bold", "Courier", "Courier-Bold"};
static void pdf_str(ob_t *b, const char *s) { u8 t[1024]; int n = to_win(s, t, sizeof t); ob_c(b, '('); for (int i = 0; i < n; i++) { if (t[i] == '(' || t[i] == ')' || t[i] == '\\') ob_c(b, '\\'); ob_c(b, t[i]); } ob_c(b, ')'); }
long zp_render_pdf(zpdoc *d, const zpset *s, u8 **out) {
    ob_t b = {0}; long off[4096]; int no = 0; ob_s(&b, "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n");
    int pages[1024], np = 0; for (int c = 0; c < MAX(1, s->copies) && c < 99; c++) for (int p = 1; p <= d->pages && np < 1024; p++) if (sel_page(s, p)) pages[np++] = p;
    /* objetos: 1 catalogo, 2 paginas, 3..8 fuentes, luego por pagina: pagina, contenido e imagenes */
    int next = 9; int pobj[1024]; ob_t cont = {0};
    #define OBJ(N_) do { off[N_] = b.n; ob_f(&b, "%d 0 obj\n", N_); no = MAX(no, N_); } while (0)
    OBJ(1); ob_s(&b, "<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    for (int f = 0; f < 6; f++) { OBJ(3 + f); ob_f(&b, "<< /Type /Font /Subtype /Type1 /BaseFont /%s /Encoding /WinAnsiEncoding >>\nendobj\n", PDFN[f]); }
    for (int k = 0; k < np; k++) {
        int st = page_start(d, pages[k]); int imgs[256], ni = 0; cont.n = 0;
        for (int i = st; i >= 0 && i < d->n && d->it[i].t != IT_PAGE; i++) { zpi *e = &d->it[i]; u32 c = s->color ? e->c : grayc(e->c);
            if (e->t == IT_TEXT) { ob_num(&cont, R(c)); ob_num(&cont, G(c)); ob_num(&cont, B(c)); ob_f(&cont, "rg BT /F%d ", e->font + 1); ob_num(&cont, e->size); ob_s(&cont, "Tf 1 0 0 1 "); ob_num(&cont, e->x); ob_num(&cont, d->ph - e->y); ob_s(&cont, "Tm "); pdf_str(&cont, e->s); ob_s(&cont, " Tj ET\n"); }
            else if (e->t == IT_RECT) { if (e->c != ZP_NONE) { ob_num(&cont, R(c)); ob_num(&cont, G(c)); ob_num(&cont, B(c)); ob_s(&cont, "rg "); ob_num(&cont, e->x); ob_num(&cont, d->ph - e->y - e->h); ob_num(&cont, e->w); ob_num(&cont, e->h); ob_s(&cont, "re f\n"); } if (e->c2 != ZP_NONE) { u32 c2 = s->color ? e->c2 : grayc(e->c2); ob_num(&cont, R(c2)); ob_num(&cont, G(c2)); ob_num(&cont, B(c2)); ob_s(&cont, "RG "); ob_num(&cont, e->lw); ob_s(&cont, "w "); ob_num(&cont, e->x); ob_num(&cont, d->ph - e->y - e->h); ob_num(&cont, e->w); ob_num(&cont, e->h); ob_s(&cont, "re S\n"); } }
            else if (e->t == IT_LINE) { ob_num(&cont, R(c)); ob_num(&cont, G(c)); ob_num(&cont, B(c)); ob_s(&cont, "RG "); ob_num(&cont, e->lw); ob_s(&cont, "w "); ob_num(&cont, e->x); ob_num(&cont, d->ph - e->y); ob_s(&cont, "m "); ob_num(&cont, e->w); ob_num(&cont, d->ph - e->h); ob_s(&cont, "l S\n"); }
            else if (e->t == IT_IMG && ni < 256) { imgs[ni] = i; ob_s(&cont, "q "); ob_num(&cont, e->w); ob_s(&cont, "0 0 "); ob_num(&cont, e->h); ob_num(&cont, e->x); ob_num(&cont, d->ph - e->y - e->h); ob_f(&cont, "cm /Im%d Do Q\n", ni); ni++; } }
        pobj[k] = next; int cobj = next + 1, iobj = next + 2; next += 2 + ni;
        OBJ(pobj[k]); ob_s(&b, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "); ob_num(&b, d->pw); ob_num(&b, d->ph); ob_s(&b, "] /Resources << /Font << "); for (int f = 0; f < 6; f++) ob_f(&b, "/F%d %d 0 R ", f + 1, 3 + f); ob_s(&b, ">> /XObject << "); for (int j = 0; j < ni; j++) ob_f(&b, "/Im%d %d 0 R ", j, iobj + j); ob_f(&b, ">> >> /Contents %d 0 R >>\nendobj\n", cobj);
        OBJ(cobj); ob_f(&b, "<< /Length %ld >>\nstream\n", cont.n); ob_add(&b, cont.p, cont.n); ob_s(&b, "\nendstream\nendobj\n");
        for (int j = 0; j < ni; j++) { zpi *e = &d->it[imgs[j]]; long sz = (long)e->iw * e->ih * (s->color ? 3 : 1); OBJ(iobj + j); ob_f(&b, "<< /Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /%s /BitsPerComponent 8 /Length %ld >>\nstream\n", e->iw, e->ih, s->color ? "DeviceRGB" : "DeviceGray", sz);
            for (long p = 0; p < (long)e->iw * e->ih; p++) { u32 c = e->px[p]; if (s->color) { u8 x[3] = {c >> 16, c >> 8, c}; ob_add(&b, x, 3); } else ob_c(&b, grayc(c) & 255); } ob_s(&b, "\nendstream\nendobj\n"); }
    }
    OBJ(2); ob_s(&b, "<< /Type /Pages /Kids ["); for (int k = 0; k < np; k++) ob_f(&b, "%d 0 R ", pobj[k]); ob_f(&b, "] /Count %d >>\nendobj\n", np);
    long xr = b.n; ob_f(&b, "xref\n0 %d\n0000000000 65535 f \n", no + 1); for (int i = 1; i <= no; i++) ob_f(&b, "%010ld 00000 n \n", off[i]);
    ob_f(&b, "trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%ld\n%%%%EOF\n", no + 1, xr); free(cont.p); *out = b.p; return b.n;
    #undef OBJ
}

/* ---------- PostScript ---------- */
static const char *PSN[] = {"Helvetica", "Helvetica-Bold", "Times-Roman", "Times-Bold", "Courier", "Courier-Bold"};
static void ps_str(ob_t *b, const char *s) { u8 t[1024]; int n = to_win(s, t, sizeof t); ob_c(b, '('); for (int i = 0; i < n; i++) { int c = t[i]; if (c >= 0x80 && c < 0xA0) c = c == 0x80 ? 'E' : c == 0x91 || c == 0x92 ? '\'' : c == 0x93 || c == 0x94 ? '"' : c == 0x96 || c == 0x97 ? '-' : c == 0x95 ? 0xB7 : '?'; if (c == '(' || c == ')' || c == '\\') ob_c(b, '\\'); if (c >= 0x80) ob_f(b, "\\%03o", c); else ob_c(b, c); } ob_c(b, ')'); }
long zp_render_ps(zpdoc *d, const zpset *s, u8 **out) {
    ob_t b = {0}; int np = 0; for (int p = 1; p <= d->pages; p++) np += sel_page(s, p);
    ob_s(&b, "%!PS-Adobe-3.0\n%%Creator: ZunzunOS\n"); ob_f(&b, "%%%%Pages: %d\n%%%%BoundingBox: 0 0 %d %d\n%%%%EndComments\n", np, (int)d->pw, (int)d->ph);
    ob_s(&b, "/ISOF { exch findfont dup length dict begin { 1 index /FID ne { def } { pop pop } ifelse } forall /Encoding ISOLatin1Encoding def currentdict end definefont pop } def\n");
    for (int f = 0; f < 6; f++) ob_f(&b, "/%s /ZF%d ISOF\n", PSN[f], f);
    ob_s(&b, "<< /PageSize ["); ob_num(&b, d->pw); ob_num(&b, d->ph); ob_f(&b, "] /NumCopies %d >> setpagedevice\n", MAX(1, s->copies));
    int k = 0;
    for (int p = 1; p <= d->pages; p++) { if (!sel_page(s, p)) continue; ob_f(&b, "%%%%Page: %d %d\n", p, ++k);
        for (int i = page_start(d, p); i >= 0 && i < d->n && d->it[i].t != IT_PAGE; i++) { zpi *e = &d->it[i]; u32 c = s->color ? e->c : grayc(e->c);
            if (e->t == IT_TEXT) { ob_num(&b, R(c)); ob_num(&b, G(c)); ob_num(&b, B(c)); ob_f(&b, "setrgbcolor /ZF%d ", e->font); ob_num(&b, e->size); ob_s(&b, "selectfont "); ob_num(&b, e->x); ob_num(&b, d->ph - e->y); ob_s(&b, "moveto "); ps_str(&b, e->s); ob_s(&b, " show\n"); }
            else if (e->t == IT_RECT) { if (e->c != ZP_NONE) { ob_num(&b, R(c)); ob_num(&b, G(c)); ob_num(&b, B(c)); ob_s(&b, "setrgbcolor "); ob_num(&b, e->x); ob_num(&b, d->ph - e->y - e->h); ob_num(&b, e->w); ob_num(&b, e->h); ob_s(&b, "rectfill\n"); } if (e->c2 != ZP_NONE) { u32 c2 = s->color ? e->c2 : grayc(e->c2); ob_num(&b, R(c2)); ob_num(&b, G(c2)); ob_num(&b, B(c2)); ob_s(&b, "setrgbcolor "); ob_num(&b, e->lw); ob_s(&b, "setlinewidth "); ob_num(&b, e->x); ob_num(&b, d->ph - e->y - e->h); ob_num(&b, e->w); ob_num(&b, e->h); ob_s(&b, "rectstroke\n"); } }
            else if (e->t == IT_LINE) { ob_num(&b, R(c)); ob_num(&b, G(c)); ob_num(&b, B(c)); ob_s(&b, "setrgbcolor "); ob_num(&b, e->lw); ob_s(&b, "setlinewidth newpath "); ob_num(&b, e->x); ob_num(&b, d->ph - e->y); ob_s(&b, "moveto "); ob_num(&b, e->w); ob_num(&b, d->ph - e->h); ob_s(&b, "lineto stroke\n"); }
            else if (e->t == IT_IMG) { int col = s->color; ob_s(&b, "gsave "); ob_num(&b, e->x); ob_num(&b, d->ph - e->y - e->h); ob_s(&b, "translate "); ob_num(&b, e->w); ob_num(&b, e->h); ob_f(&b, "scale\n%d %d 8 [%d 0 0 -%d 0 %d] currentfile /ASCIIHexDecode filter %s\n", e->iw, e->ih, e->iw, e->ih, e->ih, col ? "false 3 colorimage" : "image");
                static const char hx[] = "0123456789ABCDEF"; int lc = 0; for (long q = 0; q < (long)e->iw * e->ih; q++) { u32 v = e->px[q]; u8 x[3] = {v >> 16, v >> 8, v}; if (!col) x[0] = grayc(v) & 255; for (int j = 0; j < (col ? 3 : 1); j++) { ob_c(&b, hx[x[j] >> 4]); ob_c(&b, hx[x[j] & 15]); } if ((lc += col ? 6 : 2) >= 72) { ob_c(&b, '\n'); lc = 0; } } ob_s(&b, ">\ngrestore\n"); } }
        ob_s(&b, "showpage\n"); }
    ob_s(&b, "%%EOF\n"); *out = b.p; return b.n;
}

/* ---------- rasterizado (vista previa, PWG y PCL) ---------- */
/* RY: primera fila de la pagina que hay en px (rasterizado por bandas); H es la fila final (excluida) */
static int RY;
static void fill(u32 *px, int W, int H, int x0, int y0, int x1, int y1, u32 c) { x0 = MAX(0, x0); y0 = MAX(RY, y0); x1 = MIN(W, x1); y1 = MIN(H, y1); for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) px[(long)(y - RY) * W + x] = c; }
static void glyph(u32 *px, int W, int H, int cp, double x, double y, double gw, double gh, u32 c) {
    const u8 *g = z_glyph(cp); int X0 = (int)x, Y0 = (int)y, X1 = (int)(x + gw), Y1 = (int)(y + gh); if (X1 <= X0 || Y1 <= Y0) return;
    for (int Y = MAX(RY, Y0); Y < MIN(H, Y1); Y++) { double fy = (Y - y + 0.5) / gh * 16 - 0.5; int iy = (int)z_floor(fy); double ty = fy - iy;
        for (int X = MAX(0, X0); X < MIN(W, X1); X++) { double fx = (X - x + 0.5) / gw * 8 - 0.5; int ix = (int)z_floor(fx); double tx = fx - ix;
            #define BIT(a, b) ((a) >= 0 && (a) < 8 && (b) >= 0 && (b) < 16 && (g[b] & (0x80 >> (a))) ? 1.0 : 0.0)
            double v = BIT(ix, iy) * (1 - tx) * (1 - ty) + BIT(ix + 1, iy) * tx * (1 - ty) + BIT(ix, iy + 1) * (1 - tx) * ty + BIT(ix + 1, iy + 1) * tx * ty;
            #undef BIT
            if (v >= 0.45) px[(long)(Y - RY) * W + X] = c; } }
}
static int cp_of_win(int c) { if (c >= 0x80 && c < 0xA0) { static const u16 m[] = {0x20AC, 0, 0x201A, 0x192, 0x201E, 0x2026, 0x2020, 0x2021, 0x2C6, 0x2030, 0x160, 0x2039, 0x152, 0, 0x17D, 0, 0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x2DC, 0x2122, 0x161, 0x203A, 0x153, 0, 0x17E, 0x178}; return m[c - 0x80] ? m[c - 0x80] : '?'; } return c; }
static u32 *raster(zpdoc *d, const zpset *s, int pg, int dpi, int ry, int rows, int *ow, int *oh) {
    double k = dpi / 72.0; int W = (int)(d->pw * k), HP = (int)(d->ph * k); if (rows < 0) rows = HP; int H = ry + rows; RY = ry; u32 *px = malloc((long)W * rows * 4); if (!px) return 0; for (long i = 0; i < (long)W * rows; i++) px[i] = 0xFFFFFF;
    for (int i = page_start(d, pg); i >= 0 && i < d->n && d->it[i].t != IT_PAGE; i++) { zpi *e = &d->it[i]; u32 c = s && !s->color ? grayc(e->c) : e->c;
        if (e->t == IT_RECT) { if (e->c != ZP_NONE) fill(px, W, H, e->x * k, e->y * k, (e->x + e->w) * k, (e->y + e->h) * k, c); if (e->c2 != ZP_NONE) { u32 c2 = s && !s->color ? grayc(e->c2) : e->c2; int t = MAX(1, (int)(e->lw * k)); int x0 = e->x * k, y0 = e->y * k, x1 = (e->x + e->w) * k, y1 = (e->y + e->h) * k; fill(px, W, H, x0, y0, x1, y0 + t, c2); fill(px, W, H, x0, y1 - t, x1, y1, c2); fill(px, W, H, x0, y0, x0 + t, y1, c2); fill(px, W, H, x1 - t, y0, x1, y1, c2); } }
        else if (e->t == IT_LINE) { int t = MAX(1, (int)(e->lw * k)); double x0 = e->x * k, y0 = e->y * k, x1 = e->w * k, y1 = e->h * k, dx = x1 - x0, dy = y1 - y0; int n = (int)MAX(z_fabs(dx), z_fabs(dy)) + 1; for (int j = 0; j <= n; j++) { int x = x0 + dx * j / n, y = y0 + dy * j / n; fill(px, W, H, x - t / 2, y - t / 2, x - t / 2 + t, y - t / 2 + t, c); } }
        else if (e->t == IT_IMG) { int x0 = e->x * k, y0 = e->y * k, w = MAX(1, (int)(e->w * k)), h = MAX(1, (int)(e->h * k)); for (int y = MAX(RY, y0); y < MIN(H, y0 + h); y++) { u32 *r = e->px + (long)(y - y0) * e->ih / h * e->iw; for (int x = MAX(0, x0); x < MIN(W, x0 + w); x++) { u32 v = r[(long)(x - x0) * e->iw / w]; px[(long)(y - RY) * W + x] = s && !s->color ? grayc(v) : v & 0xFFFFFF; } } }
        else if (e->t == IT_TEXT) { u8 t[1024]; int n = to_win(e->s, t, sizeof t); double x = e->x * k, sz = e->size * k; for (int j = 0; j < n; j++) { double adv = cw(e->font, t[j]) * sz / 1000.0, gw = MIN(adv * 0.95, sz * 0.62), gh = sz * 1.15; if (e->font >= ZPF_MONO) gw = adv * 0.9; if (t[j] != ' ') { glyph(px, W, H, cp_of_win(t[j]), x + (adv - gw) / 2, e->y * k - sz * 0.92, gw, gh, c); if (e->font & 1 && e->font != ZPF_MONO) glyph(px, W, H, cp_of_win(t[j]), x + (adv - gw) / 2 + MAX(1, sz / 30), e->y * k - sz * 0.92, gw, gh, c); } x += adv; } } }
    *ow = W; if (oh) *oh = HP; RY = 0; return px;
}
u32 *zp_raster(zpdoc *d, const zpset *s, int pg, int dpi, int *ow, int *oh) { return raster(d, s, pg, dpi, 0, -1, ow, oh); }
/* solo las filas ry..ry+rows-1 de la pagina (las que pasan del final quedan en blanco) */
u32 *zp_raster_rows(zpdoc *d, const zpset *s, int pg, int dpi, int ry, int rows, int *ow) { return raster(d, s, pg, dpi, ry, rows, ow, 0); }

/* cancelar: el escritorio crea /rd/tmp/IMPRESION-CANCELAR/<trabajo>; se mira en cada banda al preparar y entre trozos de 64 KB al enviar */
#define ZP_CANCELADO (-125)
static char q_name[64];
static int q_cancelled(void) { if (!q_name[0]) return 0; char f[120]; zdirent d; snprintf(f, 120, "/rd/tmp/IMPRESION-CANCELAR/%s", q_name); return !z_stat(f, &d); }
/* ---------- PWG Raster (IPP Everywhere) ---------- */
static void pwg_line(ob_t *b, const u8 *l, int npx, int bpp) {
    int i = 0;
    while (i < npx) { int r = 1; while (i + r < npx && r < 128 && !memcmp(l + (i + r) * bpp, l + i * bpp, bpp)) r++;
        if (r > 1) { ob_c(b, r - 1); ob_add(b, l + i * bpp, bpp); i += r; continue; }
        int n = 1; while (i + n < npx && n < 128 && (i + n + 1 >= npx || memcmp(l + (i + n) * bpp, l + (i + n + 1) * bpp, bpp))) n++;
        if (n == 1) { ob_c(b, 0); ob_add(b, l + i * bpp, bpp); } else { ob_c(b, 257 - n); ob_add(b, l + i * bpp, n * bpp); } i += n; }
}
long zp_render_pwg(zpdoc *d, const zpset *s, u8 **out) {
    ob_t b = {0}; ob_s(&b, "RaS2"); int dpi = s->dpi > 0 ? s->dpi : 300, col = s->color, bpp = col ? 3 : 1, total = 0; for (int c = 0; c < MAX(1, s->copies); c++) for (int p = 1; p <= d->pages; p++) total += sel_page(s, p);
    for (int c = 0; c < MAX(1, s->copies); c++) for (int p = 1; p <= d->pages; p++) { if (!sel_page(s, p)) continue; if (q_cancelled()) { free(b.p); *out = 0; return ZP_CANCELADO; } int W, H; u32 *px = zp_raster(d, s, p, dpi, &W, &H); if (!px) break;
        u8 h[1796]; memset(h, 0, sizeof h); strcpy((char *)h, "PwgRaster"); strcpy((char *)h + 192, "");
        #define P32(o, v) do { u32 v_ = (v); h[o] = v_ >> 24; h[(o) + 1] = v_ >> 16; h[(o) + 2] = v_ >> 8; h[(o) + 3] = v_; } while (0)
        P32(276, dpi); P32(280, dpi); P32(340, 1); P32(352, (u32)d->pw); P32(356, (u32)d->ph); P32(372, W); P32(376, H); P32(384, 8); P32(388, bpp * 8); P32(392, W * bpp); P32(396, 0); P32(400, col ? 19 : 18); P32(420, bpp); P32(452, total); P32(456, 1); P32(460, 1); P32(484, 0);
        strcpy((char *)h + 1732, s->paper == 1 ? "na_letter_8.5x11in" : "iso_a4_210x297mm");
        #undef P32
        ob_add(&b, h, sizeof h); u8 *ln = malloc((long)W * bpp), *pv = malloc((long)W * bpp);
        for (int y = 0; y < H; ) { for (int x = 0; x < W; x++) { u32 v = px[(long)y * W + x]; if (col) { ln[x * 3] = v >> 16; ln[x * 3 + 1] = v >> 8; ln[x * 3 + 2] = v; } else ln[x] = grayc(v) & 255; }
            int rep = 1; while (y + rep < H && rep < 256) { int same = 1; for (int x = 0; x < W && same; x++) { u32 v = px[(long)(y + rep) * W + x]; if (col) same = ln[x * 3] == (u8)(v >> 16) && ln[x * 3 + 1] == (u8)(v >> 8) && ln[x * 3 + 2] == (u8)v; else same = ln[x] == (u8)(grayc(v) & 255); } if (!same) break; rep++; }
            ob_c(&b, rep - 1); pwg_line(&b, ln, W, bpp); y += rep; }
        free(ln); free(pv); free(px); }
    *out = b.p; return b.n;
}

/* ---------- Samsung SPL2 / QPDL (ML-1640, ML-2240 y otras "GDI" de Samsung) ----------
   Escrito desde la descripcion publica del formato (SpliX, doc/specs-en): bandas de 128 lineas
   guardadas por columnas, bits invertidos (1 = blanco) y compresion 0x11. Solo se usa la
   copia con desplazamiento 1 (repeticion del byte anterior) y bloques literales de hasta 64 bytes:
   todas las entradas de la tabla de punteros valen 1, asi cualquier indice significa lo mismo. */
#define QB 128
static void qp_band(ob_t *b, const u8 *d, long n, int num, int wpx) {
    ob_t c = {0}; u32 v = 0x09ABCDEF; ob_add(&c, &v, 4); v = QB; ob_add(&c, &v, 4); for (int i = 0; i < 64; i++) { u16 p = 1; ob_add(&c, &p, 2); }
    ob_add(&c, d, QB); long i = QB, lit = -1;
    #define FLUSH() do { while (lit >= 0 && lit < i) { int k = (int)MIN(64, i - lit); ob_c(&c, k - 1); ob_add(&c, d + lit, k); lit += k; } lit = -1; } while (0)
    while (i < n) { int r = 0; while (i + r < n && r < 514 && d[i + r] == d[i - 1]) r++;
        if (r >= 3) { FLUSH(); ob_c(&c, 0x80 | ((r - 3) & 0x7F)); ob_c(&c, ((r - 3) >> 7 & 3) << 6 | 1); i += r; }
        else { if (lit < 0) lit = i; i++; } }
    FLUSH();
    #undef FLUSH
    u32 sum = 0; for (long k = 0; k < c.n; k++) sum += c.p[k];
    ob_c(b, 0x0C); ob_c(b, num); ob_be16(b, wpx); ob_be16(b, QB); ob_c(b, 0x11); ob_be32(b, c.n + 4); ob_add(b, c.p, c.n); ob_be32(b, sum); free(c.p);
}
long zp_render_qpdl(zpdoc *d, const zpset *s, u8 **out) {
    static const u8 bayer[8][8] = {{0,32,8,40,2,34,10,42},{48,16,56,24,50,18,58,26},{12,44,4,36,14,46,6,38},{60,28,52,20,62,30,54,22},{3,35,11,43,1,33,9,41},{51,19,59,27,49,17,57,25},{15,47,7,39,13,45,5,37},{63,31,55,23,61,29,53,21}};
    ob_t b = {0}; const int dpi = 600; int cp = MAX(1, s->copies);
    ob_s(&b, "\x1B%-12345X@PJL ENTER LANGUAGE = QPDL\n");
    for (int p = 1; p <= d->pages; p++) { if (!sel_page(s, p)) continue;
        int W, H = (int)(d->ph * dpi / 72.0), wb; u8 ph[17] = {0, dpi / 100, cp >> 8, cp, s->paper == 1 ? 0 : 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0}; ob_add(&b, ph, 17);
        /* solo bandas completas dentro de la hoja: una banda que pasa del final hace que la ML-1640
           imprima "INTERNAL ERROR - FALSE" (se pierden como mucho 127 lineas, dentro del margen) */
        for (int y0 = 0, num = 0; y0 + QB <= H; y0 += QB, num++) {
            if (q_cancelled()) { free(b.p); *out = 0; return ZP_CANCELADO; } u32 *px = zp_raster_rows(d, s, p, dpi, y0, QB, &W); if (!px) break; wb = (W + 7) / 8; u8 *band = malloc((long)wb * QB); memset(band, 0xFF, (long)wb * QB);
            for (int r = 0; r < QB && y0 + r < H; r++) { const u32 *l = px + (long)r * W; for (int x = 0; x < W; x++) { int g = grayc(l[x]) & 255; if (g * 64 / 256 < bayer[(y0 + r) & 7][x & 7] || g < 8) band[(long)(x >> 3) * QB + r] &= ~(0x80 >> (x & 7)); } }
            free(px); qp_band(&b, band, (long)wb * QB, num & 255, wb * 8); free(band); }
        ob_c(&b, 1); ob_be16(&b, cp); }
    /* fin del trabajo: tabulador (fin de QPDL) y despues la marca JCL; sin el tabulador la ML-1640
       imprime una hoja "INTERNAL ERROR - FALSE" al final de cada trabajo */
    ob_s(&b, "\t\x1B%-12345X"); *out = b.p; return b.n;
}

/* ---------- PCL 5 (texto con fuentes de la impresora, rectangulos e imagenes en trama) ---------- */
long zp_render_pcl(zpdoc *d, const zpset *s, u8 **out) {
    ob_t b = {0}; int land = d->pw > d->ph; double U = 600 / 72.0, LX = 0.25 * 600;
    ob_s(&b, "\x1B%-12345X@PJL ENTER LANGUAGE=PCL\r\n\x1B" "E"); ob_f(&b, "\x1B&l%dA\x1B&l%dO\x1B&l0E\x1B&l0L\x1B&u600D\x1B&l%dX\x1B(19U", s->paper == 1 ? 2 : 26, land, MAX(1, s->copies));
    int first = 1;
    for (int p = 1; p <= d->pages; p++) { if (!sel_page(s, p)) continue; if (!first) ob_c(&b, '\f'); first = 0;
        for (int i = page_start(d, p); i >= 0 && i < d->n && d->it[i].t != IT_PAGE; i++) { zpi *e = &d->it[i];
            if (e->t == IT_TEXT) { static const int TF[] = {16602, 16602, 16901, 16901, 4099, 4099}; int sz10 = (int)(e->size * 10 + 0.5); if (e->font >= ZPF_MONO) ob_f(&b, "\x1B(s0p%dh%d.%dv0s%db4099T", (int)(120 / e->size + 0.5), sz10 / 10, sz10 % 10, e->font & 1 ? 3 : 0); else ob_f(&b, "\x1B(s1p%d.%dv0s%db%dT", sz10 / 10, sz10 % 10, e->font & 1 ? 3 : 0, TF[e->font]);
                ob_f(&b, "\x1B*p%dx%dY", (int)(e->x * U - LX), (int)(e->y * U)); u8 t[1024]; int n = to_win(e->s, t, sizeof t); ob_add(&b, t, n); }
            else if (e->t == IT_RECT || e->t == IT_LINE) { double x0 = e->x, y0 = e->y, w = e->w, h = e->h; u32 c = e->c; if (e->t == IT_LINE) { x0 = MIN(e->x, e->w); y0 = MIN(e->y, e->h); w = MAX(z_fabs(e->w - e->x), e->lw); h = MAX(z_fabs(e->h - e->y), e->lw); }
                for (int pass = 0; pass < (e->t == IT_RECT ? 2 : 1); pass++) { u32 cc = pass ? e->c2 : c; if (cc == ZP_NONE) continue; int g = 100 - (grayc(cc) & 255) * 100 / 255; if (g < 3) continue;
                    if (pass) { double t = MAX(e->lw, 0.5); double rr[4][4] = {{x0, y0, w, t}, {x0, y0 + h - t, w, t}, {x0, y0, t, h}, {x0 + w - t, y0, t, h}}; for (int q = 0; q < 4; q++) ob_f(&b, "\x1B*p%dx%dY\x1B*c%da%db%dg%dP", (int)(rr[q][0] * U - LX), (int)(rr[q][1] * U), MAX(1, (int)(rr[q][2] * U)), MAX(1, (int)(rr[q][3] * U)), g, g >= 97 ? 0 : 2); }
                    else ob_f(&b, "\x1B*p%dx%dY\x1B*c%da%db%dg%dP", (int)(x0 * U - LX), (int)(y0 * U), MAX(1, (int)(w * U)), MAX(1, (int)(h * U)), g, g >= 97 ? 0 : 2); } }
            else if (e->t == IT_IMG) { int R3 = 300, w = MAX(1, (int)(e->w * R3 / 72)), h = MAX(1, (int)(e->h * R3 / 72)), bpl = (w + 7) / 8; static const u8 bay[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
                ob_f(&b, "\x1B*p%dx%dY\x1B*t300R\x1B*r%dS\x1B*r1A\x1B*b0M", (int)(e->x * U - LX), (int)(e->y * U), w); u8 *row = malloc(bpl);
                for (int y = 0; y < h; y++) { memset(row, 0, bpl); u32 *src = e->px + (long)y * e->ih / h * e->iw; for (int x = 0; x < w; x++) { int g = grayc(src[(long)x * e->iw / w]) & 255; if (g * 16 / 256 <= bay[y & 3][x & 3]) row[x >> 3] |= 0x80 >> (x & 7); } ob_f(&b, "\x1B*b%dW", bpl); ob_add(&b, row, bpl); }
                free(row); ob_s(&b, "\x1B*rC"); } } }
    ob_s(&b, "\x1B" "E\x1B%-12345X"); *out = b.p; return b.n;
}

/* ---------- texto plano (impresoras de agujas o de lineas) ---------- */
long zp_render_txt(zpdoc *d, const zpset *s, u8 **out) {
    ob_t b = {0}; int first = 1;
    for (int p = 1; p <= d->pages; p++) { if (!sel_page(s, p)) continue; if (!first) ob_c(&b, '\f'); first = 0; static char pg[66][81]; memset(pg, ' ', sizeof pg);
        for (int i = page_start(d, p); i >= 0 && i < d->n && d->it[i].t != IT_PAGE; i++) { zpi *e = &d->it[i]; if (e->t != IT_TEXT) continue; int r = (int)(e->y / d->ph * 66), c = (int)(e->x / d->pw * 80); if (r < 0 || r >= 66) continue; u8 t[1024]; int n = to_win(e->s, t, sizeof t); for (int j = 0; j < n && c + j < 80; j++) if (c + j >= 0) pg[r][c + j] = t[j] < 0x80 ? t[j] : t[j] >= 0xC0 && t[j] <= 0xFF ? "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy"[t[j] - 0xC0] : '?'; }
        int last = 65; while (last > 0) { int e = 0; for (int c = 0; c < 80; c++) if (pg[last][c] != ' ') e = 1; if (e) break; last--; }
        for (int r = 0; r <= last; r++) { int e = 79; while (e >= 0 && pg[r][e] == ' ') e--; ob_add(&b, pg[r], e + 1); ob_s(&b, "\r\n"); } }
    ob_c(&b, '\f'); *out = b.p; return b.n;
}

/* ---------- impresoras guardadas ---------- */
#define PFILE "/sys/ZUNZUN/IMPRESORAS.TXT"
int zp_printers(zprinter *o, int max) { char *t = z_readfile(PFILE, 0); if (!t) return 0; int n = 0; char *p = t;
    while (*p && n < max) { char *e = strchr(p, '\n'); if (e) *e = 0; zprinter *r = &o[n]; memset(r, 0, sizeof *r); char *f[8]; int k = 0; f[k++] = p; for (char *q = p; *q && k < 8; q++) if (*q == '|') { *q = 0; f[k++] = q + 1; }
        if (k >= 6) { strncpy0(r->name, f[0], 64); strncpy0(r->uri, f[1], 160); strncpy0(r->lang, f[2], 8); r->paper = atoi(f[3]); r->color = atoi(f[4]); r->def = atoi(f[5]); r->dpi = k > 6 ? atoi(f[6]) : 300; if (k > 7) strncpy0(r->model, f[7], 64); n++; }
        if (!e) break; p = e + 1; }
    free(t); return n; }
int zp_save_printers(const zprinter *o, int n) { ob_t b = {0}; for (int i = 0; i < n; i++) ob_f(&b, "%s|%s|%s|%d|%d|%d|%d|%s\n", o[i].name, o[i].uri, o[i].lang, o[i].paper, o[i].color, o[i].def, o[i].dpi, o[i].model); int e = z_writefile(PFILE, b.p ? (char *)b.p : "", b.n); free(b.p); z_sync(); return e; }

/* ---------- envio ---------- */
static int host_port(const char *s, u32 *ip, int *port, char *path, int defp) { char h[128]; int i = 0; while (*s && *s != ':' && *s != '/' && i < 127) h[i++] = *s++; h[i] = 0; *port = defp; if (*s == ':') { *port = atoi(s + 1); while (*s && *s != '/') s++; } if (path) strcpy(path, *s ? s : "/ipp/print"); if (z_parse_ip(h, ip)) return z_dns(h, ip); return 0; }
static int send_all(int sk, const u8 *d, long n) { long o = 0; while (o < n) { if (q_cancelled()) return ZP_CANCELADO; long r = z_send(sk, d + o, MIN(65536L, n - o)); if (r <= 0) return r < 0 ? (int)r : -5; o += r; } return 0; }
static const char *mime(const char *lang) { return !strcmp(lang, "pdf") ? "application/pdf" : !strcmp(lang, "ps") ? "application/postscript" : !strcmp(lang, "pcl") ? "application/vnd.hp-PCL" : !strcmp(lang, "pwg") ? "image/pwg-raster" : "text/plain"; }
static void ipp_attr(ob_t *b, int tag, const char *name, const char *val, int vlen) { ob_c(b, tag); ob_be16(b, strlen(name)); ob_s(b, name); ob_be16(b, vlen < 0 ? (int)strlen(val) : vlen); ob_add(b, val, vlen < 0 ? (long)strlen(val) : vlen); }
static int ipp_call(const char *uri, int op, const char *fmt, const char *job, const u8 *doc, long dlen, u8 **resp, long *rlen) {
    u32 ip; int port; char path[128]; if (host_port(uri, &ip, &port, path, 631)) return -2; ob_t b = {0}; ob_be16(&b, 0x0200); ob_be16(&b, op); ob_be32(&b, 1); ob_c(&b, 0x01);
    ipp_attr(&b, 0x47, "attributes-charset", "utf-8", -1); ipp_attr(&b, 0x48, "attributes-natural-language", "en", -1); char pu[200]; snprintf(pu, 200, "ipp://%s", uri); ipp_attr(&b, 0x45, "printer-uri", pu, -1); ipp_attr(&b, 0x42, "requesting-user-name", "zunzun", -1);
    if (op == 2) { ipp_attr(&b, 0x42, "job-name", job, -1); ipp_attr(&b, 0x49, "document-format", fmt, -1); }
    else { ipp_attr(&b, 0x44, "requested-attributes", "printer-make-and-model", -1); static const char *more[] = {"document-format-supported", "color-supported", "printer-state", "media-default", "printer-resolution-default"}; for (int i = 0; i < 5; i++) ipp_attr(&b, 0x44, "", more[i], -1); }
    ob_c(&b, 0x03); if (doc) ob_add(&b, doc, dlen);
    int sk = z_socket(1); if (sk < 0) { free(b.p); return sk; } int e = z_connect(sk, ip, port, 8000); if (e) { z_close(sk); free(b.p); return e; }
    char hp[128]; strncpy0(hp, uri, 128); { char *sl = strchr(hp, '/'); if (sl) *sl = 0; } char hd[300]; int hl = snprintf(hd, 300, "POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/ipp\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n", path, hp, b.n); e = send_all(sk, (u8 *)hd, hl); if (!e) e = send_all(sk, b.p, b.n); free(b.p); if (e) { z_close(sk); return e; }
    long cap = 65536, got = 0; u8 *r = malloc(cap); for (;;) { if (got == cap) { cap *= 2; r = realloc(r, cap); } long n = z_recv(sk, r + got, cap - got, 20000); if (n <= 0) break; got += n; } z_close(sk);
    u8 *body = 0; for (long i = 0; i + 3 < got; i++) if (!memcmp(r + i, "\r\n\r\n", 4)) { body = r + i + 4; break; } if (!body || got < 12 || memcmp(r + 9, "200", 3)) { free(r); return -5; }
    long bl = got - (body - r); if (bl < 8) { free(r); return -5; } int st = body[2] << 8 | body[3]; if (resp) { *resp = malloc(bl); memcpy(*resp, body, bl); *rlen = bl; } free(r); return st < 0x100 ? 0 : -(1000 + st);
}
int zp_ipp_query(const char *uri, char *model, int mcap, char *formats, int fcap, int *color) {
    u8 *r; long n; int e = ipp_call(uri, 0x000B, 0, 0, 0, 0, &r, &n); if (e) return e; if (model) model[0] = 0; if (formats) formats[0] = 0; if (color) *color = 0; char last[64] = "";
    long p = 8; while (p < n) { int tag = r[p++]; if (tag == 3) break; if (tag < 0x10) continue; if (p + 2 > n) break; int nl = r[p] << 8 | r[p + 1]; p += 2; char name[64] = ""; if (nl) { memcpy(name, r + p, MIN(nl, 63)); name[MIN(nl, 63)] = 0; strcpy(last, name); } else strcpy(name, last); p += nl; if (p + 2 > n) break; int vl = r[p] << 8 | r[p + 1]; p += 2; if (p + vl > n) break;
        if (!strcmp(name, "printer-make-and-model") && model) { memcpy(model, r + p, MIN(vl, mcap - 1)); model[MIN(vl, mcap - 1)] = 0; }
        else if (!strcmp(name, "document-format-supported") && formats && (long)strlen(formats) + vl + 2 < fcap) { { int fl = strlen(formats); memcpy(formats + fl, r + p, vl); formats[fl + vl] = ','; formats[fl + vl + 1] = 0; } }
        else if (!strcmp(name, "color-supported") && color && vl == 1) *color = r[p];
        p += vl; }
    free(r); return 0; }
int zp_usb_list(zusbprn *o, int max) { return SC2(107, o, max); }
int zp_send(const zprinter *pr, const u8 *data, long len, const char *job) {
    const char *u = pr->uri;
    if (!strncmp(u, "file:", 5)) return z_writefile(u + 5, (const char *)data, len);
    if (!strncmp(u, "socket://", 9)) { u32 ip; int port; if (host_port(u + 9, &ip, &port, 0, 9100)) return -2; int sk = z_socket(1); if (sk < 0) return sk; int e = z_connect(sk, ip, port, 8000); if (!e) e = send_all(sk, data, len); z_close(sk); return e; }
    if (!strncmp(u, "ipp://", 6)) return ipp_call(u + 6, 0x0002, mime(pr->lang), job, data, len, 0, 0);
    if (!strncmp(u, "usb://", 6)) { zusbprn l[8]; int n = zp_usb_list(l, 8), k = -1; for (int i = 0; i < n; i++) if (!strcmp(l[i].id, u + 6) || (k < 0 && atoi(u + 6) == i && isdigit(u[6]))) k = i; if (k < 0) return -2; long o = 0; while (o < len) { if (q_cancelled()) { SC3(108, k, "\x1B%-12345X", 9); return ZP_CANCELADO; } long r = SC3(108, k, data + o, MIN(65536L, len - o)); if (r <= 0) return r < 0 ? (int)r : -5; o += r; } return 0; }
    return -22;
}
long zp_render(zpdoc *d, const zpset *s, const char *lang, u8 **out) { if (!strcmp(lang, "pdf")) return zp_render_pdf(d, s, out); if (!strcmp(lang, "ps")) return zp_render_ps(d, s, out); if (!strcmp(lang, "pcl")) return zp_render_pcl(d, s, out); if (!strcmp(lang, "pwg")) return zp_render_pwg(d, s, out); if (!strcmp(lang, "qpdl")) return zp_render_qpdl(d, s, out); return zp_render_txt(d, s, out); }
/* cola de impresion visible: un archivo por trabajo en /rd/tmp/IMPRESION con "estado|hora|impresora|titulo|bytes|error"
   (estado 0 preparando, 1 enviando, 2 enviado, 3 error). La barra de tareas lo lee. */
#define QDIR "/rd/tmp/IMPRESION"
static void q_set(const char *f, int st, const char *pr, const char *job, long n, int err) { u32 t[6]; z_time(t); char b[400]; int l = snprintf(b, 400, "%d|%02u:%02u|%s|%s|%ld|%d\n", st, t[2], t[1], pr, job ? job : "", n, err); z_writefile(f, b, l); }
static void q_trim(void) { zdirent e; char nm[40][64]; int c = 0; for (int i = 0; c < 40 && !z_readdir(QDIR, i, &e); i++) if (e.name[0] != '.') strncpy0(nm[c++], e.name, 64); if (c <= 20) return; for (int i = 0; i < c; i++) for (int j = i + 1; j < c; j++) if (strcmp(nm[j], nm[i]) < 0) { char x[64]; strcpy(x, nm[i]); strcpy(nm[i], nm[j]); strcpy(nm[j], x); } for (int i = 0; i < c - 20; i++) { char f[120]; snprintf(f, 120, QDIR "/%s", nm[i]); z_unlink(f); } }
int zp_print(zpdoc *d, const zprinter *p, const zpset *s, const char *job) { z_mkdir("/rd/tmp"); z_mkdir(QDIR); q_trim(); char qf[120]; snprintf(q_name, 64, "%012lu.txt", (unsigned long)z_ms()); snprintf(qf, 120, QDIR "/%s", q_name); q_set(qf, 0, p->name, job, 0, 0);
    u8 *o; long n = zp_render(d, s, p->lang, &o); if (n == ZP_CANCELADO || (n > 0 && q_cancelled())) { if (n > 0) free(o); q_set(qf, 4, p->name, job, 0, 0); q_name[0] = 0; char m[200]; snprintf(m, 200, "[prueba] imprimir %s (%s): cancelado", p->name, p->lang); z_log(m); return ZP_CANCELADO; } if (n <= 0) { q_set(qf, 3, p->name, job, 0, -12); return -12; } q_set(qf, 1, p->name, job, n, 0); int e = zp_send(p, o, n, job); free(o); q_set(qf, e == ZP_CANCELADO ? 4 : e ? 3 : 2, p->name, job, n, e == ZP_CANCELADO ? 0 : e); q_name[0] = 0; char m[200]; snprintf(m, 200, "[prueba] imprimir %s (%s, %ld bytes): %d", p->name, p->lang, n, e); z_log(m); return e; }
/* elige el lenguaje a partir del IEEE 1284 (USB) o de los formatos IPP */
const char *zp_guess_lang(const char *s) {
    if (!s) return 0; char u[600]; int i = 0; for (; s[i] && i < 599; i++) u[i] = toupper(s[i]); u[i] = 0;
    /* Samsung con SPL2/QPDL (lenguaje "GDI" que ZunzunOS genera, en pruebas) */
    if (strstr(u, "SAMSUNG") && (strstr(u, "ML-1640") || strstr(u, "ML-2240") || strstr(u, "QPDL") || strstr(u, "SPL2"))) return "qpdl";
    if (strstr(u, "SPL") || strstr(u, "ZJS") || strstr(u, "CAPT") || strstr(u, "GDI") || strstr(u, "QPDL") || strstr(u, "LIDIL") || strstr(u, "HBP")) { if (!strstr(u, "PCL") && !strstr(u, "POSTSCRIPT") && !strstr(u, "PDF") && !strstr(u, "PWG")) return 0; }
    if (strstr(u, "APPLICATION/PDF") || strstr(u, "CMD:PDF") || strstr(u, ",PDF")) return "pdf";
    if (strstr(u, "PWG") || strstr(u, "IMAGE/PWG-RASTER")) return "pwg";
    if (strstr(u, "POSTSCRIPT") || strstr(u, "BR-SCRIPT") || strstr(u, "PS;") || strstr(u, ",PS,") || strstr(u, "PS3")) return "ps";
    if (strstr(u, "PCL")) return "pcl";
    if (strstr(u, "TEXT/PLAIN") || strstr(u, "ESC/P") || strstr(u, "ESCP") || strstr(u, "EPSON")) return "txt";
    return 0;
}

/* ---------- dialogo Imprimir ---------- */
typedef struct { zwin *w; zpset s; zprinter pr[12]; int npr, sel, page; zpdoc *doc; u32 *pv; int pvw, pvh; char msg[160]; void (*build)(zpdoc *, const zpset *, void *); void *ctx; const char *title; } dlg_t;
#define DX 330
static void dlg_build(dlg_t *g) { zp_free(g->doc); g->doc = zp_new(&g->s); g->build(g->doc, &g->s, g->ctx); if (!zp_npages(g->doc)) zp_page(g->doc); if (g->page > zp_npages(g->doc)) g->page = 1; free(g->pv); g->pv = 0; }
static void dlg_preview(dlg_t *g) { if (g->pv) return; double pw = zp_pw(g->doc), ph = zp_ph(g->doc); int dpi = (int)MIN(296 * 72 / pw, 396 * 72 / ph); zpset s = g->s; s.color = g->s.color; g->pv = zp_raster(g->doc, &s, g->page, MAX(dpi, 8), &g->pvw, &g->pvh); }
static void opt(zwin *w, int x, int y, const char *a, const char *b, int v) { ui_button(w, x, y, 120, 26, a, v == 0 ? 2 : 0); ui_button(w, x + 126, y, 120, 26, b, v == 1 ? 2 : 0); }
static void num(zwin *w, int x, int y, int v, const char *zero) { ui_button(w, x, y, 26, 26, "-", 0); char t[16]; if (!v && zero) strncpy0(t, tr(zero), 16); else snprintf(t, 16, "%d", v); g_rect(w, x + 30, y, 56, 26, UI_WHITE); g_frame(w, x + 30, y, 56, 26, UI_LINE); g_text(w, x + 58 - g_textw(t) / 2, y + 5, t, UI_TEXT); ui_button(w, x + 90, y, 26, 26, "+", 0); }
static void dlg_draw(dlg_t *g) {
    zwin *w = g->w; g_clear(w, UI_BG); dlg_preview(g); g_rect(w, 10, 10, 304, 404, 0x6B7280);
    if (g->pv) { int x = 12 + (300 - g->pvw) / 2, y = 12 + (400 - g->pvh) / 2; g_rect(w, x + 3, y + 3, g->pvw, g->pvh, 0x374151); g_image(w, x, y, g->pvw, g->pvh, g->pv); }
    char t[64]; snprintf(t, 64, "%d / %d", g->page, zp_npages(g->doc)); ui_button(w, 10, 422, 40, 26, "<", 0); g_text(w, 162 - g_textw(t) / 2, 427, t, UI_TEXT); ui_button(w, 274, 422, 40, 26, ">", 0);
    g_textb(w, DX, 10, tr("Impresora"), UI_TEXT); for (int i = 0; i <= g->npr && i < 6; i++) { int y = 32 + i * 26; if (i == g->sel) g_rect(w, DX, y, 410, 26, UI_ACC2); const char *n = i == 0 ? tr("Guardar como PDF") : g->pr[i - 1].name; g_rect(w, DX + 8, y + 7, 12, 12, i == 0 ? 0xB91C1C : 0x334155); g_text(w, DX + 28, y + 5, n, UI_TEXT); if (i && g->pr[i - 1].def) g_text(w, DX + 410 - g_textw(tr("predeterminada")) - 6, y + 5, tr("predeterminada"), UI_MUTED); }
    int y = 196; g_text(w, DX, y + 5, tr("Papel"), UI_TEXT); opt(w, DX + 110, y, "A4", "Carta (EE. UU.)", g->s.paper); y += 34;
    g_text(w, DX, y + 5, tr("Orientación"), UI_TEXT); opt(w, DX + 110, y, "Vertical", "Horizontal", g->s.land); y += 34;
    g_text(w, DX, y + 5, tr("Color"), UI_TEXT); opt(w, DX + 110, y, "Color", "Escala de grises", !g->s.color); y += 34;
    g_text(w, DX, y + 5, tr("Copias"), UI_TEXT); num(w, DX + 110, y, g->s.copies, 0); y += 34;
    g_text(w, DX, y + 5, tr("Desde la página"), UI_TEXT); num(w, DX + 150, y, g->s.from, "1"); y += 34;
    g_text(w, DX, y + 5, tr("Hasta la página"), UI_TEXT); num(w, DX + 150, y, g->s.to, "última");
    g_text(w, DX, 430, g->msg, UI_MUTED); ui_button(w, w->w - 236, w->h - 40, 110, 30, "Cancelar", 0); ui_button(w, w->w - 120, w->h - 40, 110, 30, g->sel ? "Imprimir" : "Guardar", 2); zw_present(w);
}
static int hit(zevent *e, int x, int y, int w, int h) { return e->x >= x && e->x < x + w && e->y >= y && e->y < y + h; }
static void dlg_paper(dlg_t *g) { g->s.pw = g->s.paper == 1 ? 612 : 595.28; g->s.ph = g->s.paper == 1 ? 792 : 841.89; }
int zp_default_land;
int zp_dialog(const char *title, void (*build)(zpdoc *, const zpset *, void *), void *ctx) {
    static dlg_t G; dlg_t *g = &G; memset(g, 0, sizeof *g); g->build = build; g->ctx = ctx; g->title = title; g->npr = zp_printers(g->pr, 12); g->page = 1; g->s.copies = 1; g->s.color = 1; g->s.dpi = 300; g->s.land = zp_default_land; zp_default_land = 0;
    for (int i = 0; i < g->npr; i++) if (g->pr[i].def) g->sel = i + 1; if (g->sel) { g->s.paper = g->pr[g->sel - 1].paper; g->s.color = g->pr[g->sel - 1].color; g->s.dpi = g->pr[g->sel - 1].dpi ? g->pr[g->sel - 1].dpi : 300; }
    dlg_paper(g); zsys sy; z_sysinfo(&sy); char wt[120]; snprintf(wt, 120, "%s - %s", tr("Imprimir"), tr(title)); g->w = zw_new(wt, (sy.scr_w - 760) / 2, MAX(0, ((int)sy.scr_h - 500) / 2), 760, 500, WF_DECOR);
    dlg_build(g); dlg_draw(g); z_log("[prueba] dialogo imprimir abierto"); zevent e; int res = -1;
    for (;;) { if (!z_event(&e, -1)) continue; if (e.type == EV_REDRAW) { dlg_draw(g); continue; } if (e.win != (u32)g->w->id) continue; if (e.type == EV_CLOSE || (e.type == EV_KEY && e.ch == 27)) break;
        if (e.type != EV_DOWN) continue; int rb = 0, y = 196; g->msg[0] = 0;
        if (hit(&e, 10, 422, 40, 26) && g->page > 1) { g->page--; free(g->pv); g->pv = 0; }
        else if (hit(&e, 274, 422, 40, 26) && g->page < zp_npages(g->doc)) { g->page++; free(g->pv); g->pv = 0; }
        else if (hit(&e, DX, 32, 410, 26 * (MIN(g->npr, 5) + 1))) { g->sel = (e.y - 32) / 26; if (g->sel) { zprinter *p = &g->pr[g->sel - 1]; g->s.paper = p->paper; g->s.color = p->color; g->s.dpi = p->dpi ? p->dpi : 300; dlg_paper(g); rb = 1; } }
        else if (hit(&e, DX + 110, y, 246, 26)) { g->s.paper = e.x >= DX + 236; dlg_paper(g); rb = 1; }
        else if (hit(&e, DX + 110, y + 34, 246, 26)) { g->s.land = e.x >= DX + 236; rb = 1; }
        else if (hit(&e, DX + 110, y + 68, 246, 26)) { g->s.color = e.x < DX + 236; free(g->pv); g->pv = 0; }
        else if (hit(&e, DX + 110, y + 102, 26, 26)) g->s.copies = MAX(1, g->s.copies - 1); else if (hit(&e, DX + 200, y + 102, 26, 26)) g->s.copies = MIN(99, g->s.copies + 1);
        else if (hit(&e, DX + 150, y + 136, 26, 26)) g->s.from = MAX(0, g->s.from - 1); else if (hit(&e, DX + 240, y + 136, 26, 26)) g->s.from = MIN(zp_npages(g->doc), g->s.from + 1);
        else if (hit(&e, DX + 150, y + 170, 26, 26)) g->s.to = MAX(0, g->s.to - 1); else if (hit(&e, DX + 240, y + 170, 26, 26)) g->s.to = MIN(zp_npages(g->doc), g->s.to + 1);
        else if (hit(&e, g->w->w - 236, g->w->h - 40, 110, 30)) break;
        else if (hit(&e, g->w->w - 120, g->w->h - 40, 110, 30)) {
            if (!g->sel) { char p[200], d[120]; z_docdir(d); char nm[80]; strncpy0(nm, tr(title), 80); for (char *q = nm; *q; q++) if (strchr("/\\:*?\"<>|", *q)) *q = '_'; snprintf(p, 200, "%s/%s.pdf", d, nm); if (!ui_prompt("Guardar como PDF", "Ruta del archivo PDF:", p, 190) || !p[0]) { dlg_draw(g); continue; } if (!z_ends(p, ".pdf")) strcat(p, ".pdf");
                strcpy(g->msg, tr("Creando el PDF...")); dlg_draw(g); u8 *o; long n = zp_render(g->doc, &g->s, "pdf", &o); int er = z_writefile(p, (char *)o, n); free(o); z_sync(); char m[300]; snprintf(m, 300, "[prueba] pdf guardado %s (%ld bytes): %d", p, n, er); z_log(m); if (er) { snprintf(g->msg, 160, "Error: %s", z_errstr(er)); dlg_draw(g); continue; } res = 0; break; }
            strcpy(g->msg, tr("Enviando a la impresora...")); dlg_draw(g); int er = zp_print(g->doc, &g->pr[g->sel - 1], &g->s, tr(title)); if (er == ZP_CANCELADO) break; if (er) { snprintf(g->msg, 160, "%s: %s", tr("No se pudo imprimir"), er <= -1000 ? tr("la impresora rechazó el trabajo") : z_errstr(er)); dlg_draw(g); continue; } res = 0; break; }
        if (rb) dlg_build(g); dlg_draw(g); }
    zw_close(g->w); zp_free(g->doc); g->doc = 0; free(g->pv); g->pv = 0; return res;
}
/* ---------- ayudas para que cualquier programa pueda imprimir ---------- */
typedef struct { const char *title, *text, *sub; int mono; const u32 *px; int w, h; } zpq_t;
static void q_head(zpflow *f, const zpq_t *q) { zp_para(f, ZPF_SANSB, 15, 0x111827, q->title, 0, q->sub ? 2 : 10); if (q->sub) zp_para(f, ZPF_SANS, 9, 0x6B7280, q->sub, 0, 10); }
static void q_text(zpdoc *d, const zpset *s, void *c) { zpq_t *q = c; zpflow f; zp_flow_init(&f, d, 50); f.num = 1; q_head(&f, q); const char *p = q->text; char ln[1024];
    while (*p) { int n = 0; while (p[n] && p[n] != '\n' && n < 1023) n++; memcpy(ln, p, n); ln[n] = 0; if (q->mono) for (int i = 0; i < n; i++) if (ln[i] == '\t') ln[i] = ' '; zp_para(&f, q->mono ? ZPF_MONO : ZPF_SANS, q->mono ? 9 : 11, 0x111827, ln, 0, q->mono ? 0 : 3); p += n; if (*p == '\n') p++; } }
static void q_img(zpdoc *d, const zpset *s, void *c) { zpq_t *q = c; zpflow f; zp_flow_init(&f, d, 40); q_head(&f, q); double mw = f.x1 - f.x0, mh = f.ybot - f.y, k = MIN(mw / q->w, mh / q->h); if (k > 2.0) k = 2.0; double w = q->w * k, h = q->h * k; zp_image(d, f.x0 + (mw - w) / 2, f.y, w, h, q->px, q->w, q->h); }
/* imprime un texto (mono = letra de ancho fijo para tablas y terminal); sub es una linea gris bajo el titulo */
int zp_print_text(const char *title, const char *sub, const char *text, int mono) { zpq_t q = {title, text, sub, mono, 0, 0, 0}; return zp_dialog(title, q_text, &q); }
/* imprime una imagen ajustada a la hoja (horizontal si la imagen es apaisada) */
int zp_print_image(const char *title, const u32 *px, int w, int h) { zpq_t q = {title, 0, 0, 0, px, w, h}; zp_default_land = w > h * 5 / 4; return zp_dialog(title, q_img, &q); }
/* texto de la primera pagina de un documento, ordenado por lineas (para "Guardar texto" del lector PDF) */
static int cmp_it(const void *a, const void *b) { const zpi *x = *(zpi **)a, *y = *(zpi **)b; if (z_fabs(x->y - y->y) > x->size * 0.4) return x->y < y->y ? -1 : 1; return x->x < y->x ? -1 : x->x > y->x; }
int zp_text_dump(zpdoc *d, sb_t *b) { zpi **v = malloc(d->n * sizeof *v); int n = 0; for (int i = 0; i < d->n; i++) if (d->it[i].t == IT_TEXT) v[n++] = &d->it[i]; qsort(v, n, sizeof *v, cmp_it);
    for (int i = 0; i < n; i++) { if (i && z_fabs(v[i]->y - v[i - 1]->y) > v[i]->size * 0.4) sb_s(b, "\n"); else if (i && v[i]->x > v[i - 1]->x + zp_textw(v[i - 1]->font, v[i - 1]->size, v[i - 1]->s) + v[i]->size * 0.2) sb_s(b, " "); sb_s(b, v[i]->s); } sb_s(b, "\n"); free(v); return n; }
/* para seleccionar texto en pantalla (Lector PDF): anota en la ventana donde cae cada letra de la pagina pg
   dibujada a dpi con su esquina en (ox, oy) */
void zp_sel_runs(zpdoc *d, int pg, int dpi, zwin *w, int ox, int oy) { double k = dpi / 72.0;
    for (int i = page_start(d, pg); i >= 0 && i < d->n && d->it[i].t != IT_PAGE; i++) { zpi *e = &d->it[i]; if (e->t != IT_TEXT || !e->s) continue;
        u8 t[1024]; int n = to_win(e->s, t, sizeof t), nu = 0; const char *p = e->s; while (utf8_next(&p)) nu++; if (!n || n != nu || n > 1000) continue;
        static short cx[1024]; double x = 0, sz = e->size * k, last = 0; for (int j = 0; j < n; j++) { cx[j] = (short)x; last = cw(e->font, t[j]) * sz / 1000.0; x += last; }
        zs_add(w, ox + (int)(e->x * k), oy + (int)(e->y * k - sz * 0.92), MAX(4, (int)(sz * 1.15)), e->s, cx, n, MAX(1, (int)last)); } }
