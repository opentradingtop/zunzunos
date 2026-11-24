/* Consola JavaScript (motor Colibri, propio de ZunzunOS): escribe una linea y pulsa Intro para ejecutarla. Con un archivo .js como
   argumento lo ejecuta al abrir (y "jsconsola --prueba ARCHIVO" anota cada linea de salida en el registro). */
#include "z.h"
#include "cbhost.h"
#define NL 600
static zwin *W; static char *L[NL]; static u32 C[NL]; static int nl, top; static char in[1024]; static int il, prueba; static cb_rt *ctx;
static char *hist[40]; static int nh, hi;
static const char *mchr(const char *s, int c, long n) { for (; n-- > 0; s++) if (*s == c) return s; return 0; }
static void add(const char *s, int n, u32 col) {
    while (n >= 0) { const char *e = mchr(s, '\n', n); int k = e ? (int)(e - s) : n;
        if (nl == NL) { free(L[0]); memmove(L, L + 1, (NL - 1) * sizeof *L); memmove(C, C + 1, (NL - 1) * sizeof *C); nl--; }
        L[nl] = malloc(k + 1); memcpy(L[nl], s, k); L[nl][k] = 0; C[nl] = col; nl++;
        if (prueba) { char b[600]; snprintf(b, 600, "[js] %.*s", k > 560 ? 560 : k, s); z_log(b); }
        if (!e) break; s = e + 1; n -= k + 1; }
    top = -1; }
static void out(void *u, int level, const char *s, int n) { add(s, n, level == 2 ? 0xF87171 : level == 1 ? 0xFBBF24 : 0xE5E7EB); }
static void draw(void) {
    g_clear(W, 0x0B1120); int lh = 18, vis = (W->h - 44) / lh; int first = top < 0 ? MAX(0, nl - vis) : top;
    for (int i = 0; i < vis && first + i < nl; i++) g_text(W, 10, 8 + i * lh, L[first + i], C[first + i]);
    g_rect(W, 0, W->h - 34, W->w, 34, 0x1E293B); g_text(W, 10, W->h - 25, ">", 0x5EEAD4); g_text(W, 26, W->h - 25, in, 0xFFFFFF);
    g_rect(W, 26 + g_textw(in), W->h - 27, 2, 18, 0x5EEAD4); zw_present(W); }
static void run(const char *src, long n, const char *name, int show) {
    char *res = malloc(8192), *err = malloc(4096);
    if (cbh_eval(ctx, src, n, name, res, 8192, err, 4096)) add(err, strlen(err), 0xF87171); else if (show && res[0]) add(res, strlen(res), 0x93C5FD);
    free(res); free(err); }
int main(const char *a) {
    W = zw_new("Consola JavaScript", -1, 0, 680, 440, WF_DECOR | WF_RESIZE);
    ctx = cbh_new(out, 0); if (!ctx) { add(_("No hay memoria para JavaScript"), strlen(_("No hay memoria para JavaScript")), 0xF87171); }
    char m[120]; add(_("JavaScript (Colibrí). Escribe y pulsa Intro."), strlen(_("JavaScript (Colibrí). Escribe y pulsa Intro.")), 0x5EEAD4);
    if (!strncmp(a, "--prueba ", 9)) { prueba = 1; a += 9; }
    if (a[0] && ctx) { long n; char *s = z_readfile(a, &n); if (!s) add(_("No se puede abrir"), strlen(_("No se puede abrir")), 0xF87171); else { u64 t0 = z_ms(); if (!strncmp(s, "// zz-prueba", 12)) prueba = 1; run(s, n, a, 0); free(s);
            for (long w; (w = cbh_tick(ctx)) >= 0 && w < 3000 && z_ms() - t0 < 20000;) { zevent e; z_event(&e, (int)MAX(w, 1)); }
            if (prueba) { snprintf(m, 120, "[prueba] js terminado %d lineas en %lu ms", nl, z_ms() - t0); z_log(m); } } }
    draw(); zevent e;
    for (;;) {
        long w = ctx ? cbh_tick(ctx) : -1; int g = z_event(&e, w < 0 ? -1 : (int)MAX(w, 1));
        if (!g) { draw(); continue; } if (e.type == EV_REDRAW) { draw(); continue; } if (e.win != (u32)W->id) continue;
        if (e.type == EV_CLOSE) { cbh_free(ctx); return 0; }
        if (e.type == EV_RESIZE) zw_resized(W, e.x, e.y);
        else if (e.type == EV_WHEEL) { int vis = (W->h - 44) / 18; int f = top < 0 ? MAX(0, nl - vis) : top; f -= (int)e.key * 3; top = MAX(0, MIN(f, MAX(0, nl - vis))); if (top >= MAX(0, nl - vis)) top = -1; }
        else if (e.type == EV_KEY) {
            if (e.ch == '\n' || e.ch == '\r') { if (!il) continue; char b[1100]; snprintf(b, 1100, "> %s", in); add(b, strlen(b), 0x5EEAD4);
                if (nh == 40) { free(hist[0]); memmove(hist, hist + 1, 39 * sizeof *hist); nh--; } hist[nh++] = strdup(in); hi = nh;
                if (ctx) run(in, il, "<consola>", 1); il = 0; in[0] = 0; }
            else if (e.ch == 8) { if (il) { while (il > 0 && ((u8)in[il - 1] & 0xC0) == 0x80) il--; if (il) il--; in[il] = 0; } }
            else if (e.key == 0x100 && hi > 0) { hi--; strncpy0(in, hist[hi], sizeof in); il = strlen(in); }
            else if (e.key == 0x101 && hi < nh) { hi++; if (hi < nh) strncpy0(in, hist[hi], sizeof in); else in[0] = 0; il = strlen(in); }
            else if (e.ch >= 32 && il < (int)sizeof in - 5) { il += utf8_put(e.ch, in + il); in[il] = 0; }
            else continue; }
        else continue;
        draw(); }
}
