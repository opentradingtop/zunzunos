/* Navegador: HTTP y HTTPS (TLS 1.3 y 1.2 con certificados verificados), HTML y CSS, imagenes,
   formularios GET y POST, historial, marcadores y descargas. Sin JavaScript. */
#include "../web/kernel.h"
#include "font8x16.h"
static zwin *G;
void strlcpy(char *d, const char *s, size_t n) { strncpy0(d, s, n); }
void gfx_fill(int x, int y, int w, int h, u32 c) { g_rect(G, x, y, w, h, c & 0xFFFFFF); }
void gfx_char(int x, int y, u8 c, u32 k) { g_glyph(G, x, y, c, k & 0xFFFFFF); }
void gfx_text_big(int x, int y, const char *s, u32 k, int sc) { for (; *s; s++, x += 8 * sc) { const u8 *g = font8x16 + (u8)*s * 16; for (int j = 0; j < 16; j++) for (int i = 0; i < 8; i++) if (g[j] & (0x80 >> i)) g_rect(G, x + i * sc, y + j * sc, sc, sc, k & 0xFFFFFF); } }
void gfx_image(int x, int y, int w, int h, const u32 *px, int iw, int ih) { int y0 = MAX(y, G->cy0), y1 = MIN(y + h, G->cy1), x0 = MAX(x, G->cx0), x1 = MIN(x + w, G->cx1); for (int j = y0; j < y1; j++) { const u32 *r = px + (long)(j - y) * ih / h * iw; u32 *o = G->px + (long)j * G->w; for (int i = x0; i < x1; i++) o[i] = r[(long)(i - x) * iw / w] & 0xFFFFFF; } }
const char *clip_get(int *l) { static char b[4096]; *l = z_clip_get(b, 4095); b[*l] = 0; return b; }
#define TB 38
#define SB 22
static void strncat0(char *d, const char *s, int cap) { int l = strlen(d); strncpy0(d + l, s, cap - l); }
static int isal(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }
static char url[1100], addr[1100], msg[200], title[200]; static int asel, acur, afoc, scroll, loading, secure, certst;
static struct html_doc *D; static char *src; static long srcl;
static char *hb[64], *hf[64]; static int nb, nf;
/* ---------- cache de imagenes ---------- */
typedef struct { char *url; u32 *px; int w, h, state; } img_t; /* state 1 lista, 2 fallo */
static img_t IM[160]; static int nim; static long impx;
static void resolve(const char *base, const char *rel, char *out, int cap) {
    while (*rel == ' ' || *rel == '\t' || *rel == '\n' || *rel == '\r') rel++;
    if (!strncasecmp(rel, "http://", 7) || !strncasecmp(rel, "https://", 8) || !strncasecmp(rel, "zunzun:", 7)) { strncpy0(out, rel, cap); return; }
    if (base[0] == '/') { /* archivo local */ if (rel[0] == '/') { strncpy0(out, rel, cap); return; } strncpy0(out, base, cap); char *sl = strrchr(out, '/'); if (sl) strncpy0(sl + 1, rel, cap - (sl + 1 - out)); return; }
    const char *p = strstr(base, "://"); int hl = p ? (int)(strchr(p + 3, '/') ? strchr(p + 3, '/') - base : (long)strlen(base)) : 0;
    if (rel[0] == '/' && rel[1] == '/') { int sl = (int)(p - base) + 1; char sc[16]; memcpy(sc, base, MIN(sl, 15)); sc[MIN(sl, 15)] = 0; snprintf(out, cap, "%s%s", sc, rel); return; }
    if (rel[0] == '/') { char hb2[600]; memcpy(hb2, base, MIN(hl, 599)); hb2[MIN(hl, 599)] = 0; snprintf(out, cap, "%s%s", hb2, rel); return; }
    if (rel[0] == '#') { strncpy0(out, base, cap); char *h = strchr(out, '#'); if (h) *h = 0; strncat0(out, rel, cap); return; }
    char b[1100]; strncpy0(b, base, sizeof b); char *h = strchr(b, '#'); if (h) *h = 0; if (rel[0] == '?') { char *q = strchr(b, '?'); if (q) *q = 0; snprintf(out, cap, "%s%s", b, rel); return; }
    char *q = strchr(b, '?'); if (q) *q = 0; char *sl = strrchr(b, '/'); if (sl && sl - b >= hl) sl[1] = 0; else strncat0(b, "/", sizeof b);
    char t[1400]; snprintf(t, sizeof t, "%s%s", b, rel); /* quita ./ y ../ */
    char *r = t + hl; char o[1400]; int k = 0; memcpy(o, t, hl); k = hl;
    while (*r) { if (!strncmp(r, "/./", 3)) { r += 2; continue; } if (!strncmp(r, "/../", 4)) { r += 3; while (k > hl && o[k - 1] != '/') k--; if (k > hl) k--; continue; } o[k++] = *r++; } o[k] = 0; strncpy0(out, o, cap); }
static int img_get(const char *s, u32 **px, int *w, int *h) { char u[1100]; resolve(url, s, u, sizeof u); for (int i = 0; i < nim; i++) if (!strcmp(IM[i].url, u)) { if (IM[i].state != 1) return 0; *px = IM[i].px; *w = IM[i].w; *h = IM[i].h; return 1; } return 0; }
static void img_clear(void) { for (int i = 0; i < nim; i++) { free(IM[i].url); free(IM[i].px); } nim = 0; impx = 0; }
/* ---------- utilidades ---------- */
static void dl_dir(char *o) { char d[160]; z_docdir(d); snprintf(o, 200, "%s/Descargas", d); z_mkdir(d); z_mkdir(o); }
static char *latin1_to_utf8(const char *s, long n, long *on) { char *o = malloc(n * 2 + 1); long k = 0; for (long i = 0; i < n; i++) { u8 c = s[i]; if (c < 128) o[k++] = c; else { o[k++] = 0xC0 | c >> 6; o[k++] = 0x80 | (c & 63); } } o[k] = 0; *on = k; return o; }
static int sa_i, sb_i, nsi; static void set_page(char *html, long n) { sa_i = sb_i = -1; nsi = 0; if (D) html_free(D); free(src); src = html; srcl = n; D = html_load(src, srcl, G->w - 18); scroll = 0; const char *t = html_title(D); if (t && *t) { u32 tl; char *u8t = cp437_to_utf8(t, strlen(t), &tl); strncpy0(title, u8t ? u8t : t, sizeof title); free(u8t); } else strncpy0(title, url, sizeof title); char wt[260]; snprintf(wt, sizeof wt, "%s - %s", title, _("Navegador")); zw_title(G->id, wt); }
static void page_text(const char *fmt, const char *a, const char *b) { char *h = malloc(4096); long n = snprintf(h, 4096, fmt, a, b); set_page(h, n); }
static void esc(const char *s, char *o, int cap) { int k = 0; for (; *s && k < cap - 8; s++) { if (*s == '<') { memcpy(o + k, "&lt;", 4); k += 4; } else if (*s == '&') { memcpy(o + k, "&amp;", 5); k += 5; } else if (*s == '"') { memcpy(o + k, "&quot;", 6); k += 6; } else o[k++] = *s; } o[k] = 0; }
static void draw(void);
static void navigate(const char *u, const char *post, int push);
static int pending_imgs(void) { if (!D) return 0; int n = html_nimages(D); int c = 0; for (int i = 0; i < n; i++) { char u[1100]; resolve(url, html_image_src(D, i), u, sizeof u); int f = 0; for (int k = 0; k < nim; k++) if (!strcmp(IM[k].url, u)) f = 1; if (!f) c++; } return c; }
static void load_one_img(void) { int n = html_nimages(D); for (int i = 0; i < n && nim < 160; i++) { char u[1100]; resolve(url, html_image_src(D, i), u, sizeof u); int f = 0; for (int k = 0; k < nim; k++) if (!strcmp(IM[k].url, u)) f = 1; if (f) continue;
        img_t *m = &IM[nim++]; memset(m, 0, sizeof *m); m->url = strdup(u); m->state = 2; if (strncasecmp(u, "http", 4) && u[0] != '/') return; if (impx > 24L << 20) return;
        char *b = 0; long l = 0; int e; snprintf(msg, sizeof msg, "%s %s", _("Imagen"), u); draw();
        if (u[0] == '/') { b = z_readfile(u, &l); e = b ? 0 : -2; } else { zhttp_info inf; e = z_http("GET", u, 0, 0, 0, &b, &l, &inf); if (!e && inf.status != 200) e = -5; }
        if (!e && b) { u32 *px = 0; int w = 0, h = 0; u8 *d = (u8 *)b; int r = z_png_decode(d, l, &px, &w, &h); if (r) r = z_jpeg_decode(d, l, &px, &w, &h); if (r) r = z_gif_decode(d, l, &px, &w, &h); if (r) r = z_bmp_decode(d, l, &px, &w, &h);
            if (!r && w > 0 && h > 0) { if (w > 1400) { int nw = 1400, nh = MAX(1, h * 1400 / w); u32 *s2 = malloc((long)nw * nh * 4); for (int y = 0; y < nh; y++) for (int x = 0; x < nw; x++) s2[(long)y * nw + x] = px[(long)(y * h / nh) * w + x * w / nw]; free(px); px = s2; w = nw; h = nh; } m->px = px; m->w = w; m->h = h; m->state = 1; impx += (long)w * h; } }
        free(b); return; } }
/* ---------- carga ---------- */
static void show_error(int e, const char *u, zhttp_info *inf) {
    char eu[1200]; esc(u, eu, sizeof eu);
    if (e <= -1001 && e >= -1003) { char *h = malloc(6000); const char *why = e == -1001 ? _("El certificado no lo ha firmado ninguna autoridad de confianza (puede ser autofirmado o falso).") : e == -1002 ? _("El certificado pertenece a otro sitio web: alguien podría estar suplantándolo.") : _("El certificado ha caducado o todavía no es válido. Comprueba también la fecha del equipo.");
        long n = snprintf(h, 6000, "<html><head><title>%s</title></head><body><h1 style=\"color:#b91c1c\">%s</h1><p>%s</p><p><b>%s</b></p><p>%s</p><p><a href=\"zunzun:volver\">%s</a></p><p><a href=\"zunzun:continuar\">%s</a></p></body></html>",
            _("Conexión no segura"), _("Conexión no segura"), eu, why, _("Si sigues, lo que envíes o recibas podría verse o cambiarse por el camino."), _("Volver (recomendado)"), _("Continuar de todos modos"));
        set_page(h, n); certst = -e - 1000; secure = 0; return; }
    page_text("<html><head><title>Error</title></head><body><h1>%s</h1><p>%s</p></body></html>", _("No se puede abrir la página"), e == -1004 ? inf->err : z_http_err(e)); }
static void navigate(const char *u0, const char *post, int push) {
    char u[1100]; strncpy0(u, u0, sizeof u);
    if (!strcmp(u, "zunzun:volver")) { if (nb) { char *p = hb[--nb]; if (nf < 64) hf[nf++] = strdup(url); navigate(p, 0, 0); free(p); } return; }
    if (!strcmp(u, "zunzun:continuar")) { char host[128]; const char *p = strstr(url, "://"); int i = 0; if (p) { p += 3; while (*p && *p != '/' && *p != ':' && i < 127) host[i++] = *p++; } host[i] = 0; z_https_allow(host); navigate(url, 0, 0); return; }
    if (push && url[0]) { if (nb == 64) { free(hb[0]); memmove(hb, hb + 1, 63 * sizeof *hb); nb--; } hb[nb++] = strdup(url); for (int i = 0; i < nf; i++) free(hf[i]); nf = 0; }
    char *h = strchr(u, '#'); char frag[200] = ""; if (h && u[0] != '/') { strncpy0(frag, h + 1, 200); }
    strcpy(url, u); strcpy(addr, u); acur = strlen(addr); afoc = 0; loading = 1; img_clear(); snprintf(msg, sizeof msg, "%s %s", _("Cargando"), u); draw();
    if (u[0] == '/') { long n; char *b = z_readfile(u, &n); secure = 0; certst = 0; loading = 0; { char m[300]; snprintf(m, 300, "[prueba] navegador %s: local %ld bytes", u, b ? n : -1L); z_log(m); } if (!b) { page_text("<html><body><h1>%s</h1><p>%s</p></body></html>", _("No se puede abrir"), u); return; } set_page(b, n); msg[0] = 0; return; }
    if (strncasecmp(u, "http://", 7) && strncasecmp(u, "https://", 8)) { loading = 0; page_text("<html><body><h1>%s</h1><p>%s</p></body></html>", _("Dirección no válida"), u); return; }
    char *b = 0; long l = 0; zhttp_info inf; u64 t0 = z_ms(); int e = post ? z_http("POST", u, post, strlen(post), 0, &b, &l, &inf) : z_http("GET", u, 0, 0, 0, &b, &l, &inf); loading = 0;
    char m[300]; snprintf(m, 300, "[prueba] navegador %s: %d estado %d cert %d tipo %s %ld bytes", u, e, inf.status, inf.cert, inf.ctype, l); z_log(m);
    if (e) { show_error(e, u, &inf); snprintf(msg, sizeof msg, "%s", e == -1004 ? inf.err : z_http_err(e)); return; }
    if (inf.final[0]) { strcpy(url, inf.final); strcpy(addr, url); acur = strlen(addr); } secure = inf.tls && !inf.cert; certst = inf.cert;
    const char *ct = inf.ctype; int html = !ct[0] || strstr(ct, "html") || strstr(ct, "xml");
    if (strstr(ct, "image/")) { char *pg = malloc(2400); char eu[1200]; esc(url, eu, sizeof eu); long n = snprintf(pg, 2400, "<html><head><title>%s</title></head><body><img src=\"%s\"></body></html>", eu, eu); free(b); set_page(pg, n); }
    else if (strstr(ct, "text/plain")) { char *pg = malloc(l * 6 + 200); long n = snprintf(pg, 40, "<html><body><pre>"); for (long i = 0; i < l; i++) { char c = b[i]; if (c == '<') { memcpy(pg + n, "&lt;", 4); n += 4; } else if (c == '&') { memcpy(pg + n, "&amp;", 5); n += 5; } else pg[n++] = c; } n += snprintf(pg + n, 40, "</pre></body></html>"); free(b); set_page(pg, n); }
    else if (!html) { char d[200], name[160]; dl_dir(d); const char *s = strrchr(url, '/'); strncpy0(name, s && s[1] ? s + 1 : "descarga", 160); char *q = strchr(name, '?'); if (q) *q = 0; if (!name[0]) strcpy(name, "descarga"); char p[400]; snprintf(p, 400, "%s/%s", d, name); int we = z_writefile(p, b, l); free(b);
        page_text("<html><body><h1>%s</h1><p>%s</p></body></html>", we ? _("No se pudo guardar el archivo") : _("Archivo descargado"), p); snprintf(m, 300, "[prueba] navegador descarga %s: %d", p, we); z_log(m); if (!we && z_ends(p, ".pdf")) z_spawn("/rd/bin/pdf", p); return; }
    else { if ((strstr(ct, "iso-8859") || strstr(ct, "windows-1252")) || ((!strstr(ct, "utf-8")) && (strstr(b, "charset=iso-8859") || strstr(b, "charset=ISO-8859") || strstr(b, "charset=windows-1252")))) { long n2; char *u8b = latin1_to_utf8(b, l, &n2); free(b); b = u8b; l = n2; } set_page(b, l); }
    if (frag[0] && D) { int a = html_anchor_y(D, frag); if (a >= 0) scroll = a; }
    snprintf(msg, sizeof msg, "%s (%lu ms)%s", _("Listo"), (unsigned long)(z_ms() - t0), D && html_has_script(D) ? _("  - esta página usa JavaScript, que no está disponible") : ""); }
static void go_addr(void) { char u[1100]; const char *a = addr; while (*a == ' ') a++; if (!*a) return;
    if (strstr(a, "://") || a[0] == '/' || !strncmp(a, "zunzun:", 7)) strncpy0(u, a, sizeof u);
    else if (strchr(a, '.') && !strchr(a, ' ')) snprintf(u, sizeof u, "https://%s", a);
    else { char q[900] = ""; int k = 0; static const char hx[] = "0123456789ABCDEF"; for (const u8 *p = (const u8 *)a; *p && k < 880; p++) { if (isal(*p)) q[k++] = *p; else if (*p == ' ') q[k++] = '+'; else { q[k++] = '%'; q[k++] = hx[*p >> 4]; q[k++] = hx[*p & 15]; } } q[k] = 0; snprintf(u, sizeof u, "https://html.duckduckgo.com/html/?q=%s", q); }
    navigate(u, 0, 1); }
/* ---------- marcadores ---------- */
#define MARC "/sys/ZUNZUN/MARCADORES.TXT"
static void add_mark(void) { long n; char *t = z_readfile(MARC, &n); sb_t s = {0}; if (t) sb_add(&s, t, n); sb_f(&s, "%s\t%s\n", url, title); int e = z_writefile(MARC, s.p, s.n); free(s.p); free(t); z_sync(); strcpy(msg, e ? z_errstr(e) : _("Página añadida a marcadores")); }
static void marks_menu(int sx, int sy) { char *t = z_readfile(MARC, 0); const char *it[24]; char *us[24]; int n = 0; if (t) { char *p = t; while (*p && n < 24) { char *e = strchr(p, '\n'); if (e) *e = 0; char *tab = strchr(p, '\t'); if (tab) { *tab = 0; us[n] = p; it[n] = tab[1] ? tab + 1 : p; n++; } if (!e) break; p = e + 1; } }
    if (!n) { strcpy(msg, _("Aún no hay marcadores: pulsa la estrella para guardar la página.")); free(t); return; } int r = ui_menu(sx, sy, it, n); char u[1100] = ""; if (r >= 0) strncpy0(u, us[r], sizeof u); free(t); if (u[0]) navigate(u, 0, 1); }
/* ---------- dibujo ---------- */
static const char *BT[] = {"<", ">", "R", "Inicio", "*", "Marcadores", "Imprimir"};
static int bw(int i) { return g_textw(_(BT[i])) + 16; }
static int bx(int i) { int x = 6; for (int k = 0; k < i; k++) x += bw(k) + 4; return x; }
static int addr_x(void) { return bx(4) + 26; }
static int addr_w(void) { return G->w - addr_x() - bw(4) - bw(5) - bw(6) - 24; }
/* ---------- seleccionar texto arrastrando; menu del boton derecho (copiar, enlaces, imagenes) ---------- */
typedef struct { int type, x, y, w, h, len, sc; const char *s; const u32 *px; int iw, ih; } sit_t;
static sit_t *SI; static int nsi, capsi, sa_i = -1, sa_c, sb_i = -1, sb_c, seldrag;
static void si_add(void *c, const struct html_item *it) { if (it->type == 0) return; if (nsi == capsi) { capsi = capsi * 2 + 256; SI = realloc(SI, capsi * sizeof *SI); } SI[nsi++] = (sit_t){it->type, it->x, it->y, it->w, it->h, it->len, it->scale ? it->scale : 1, it->str, it->px, it->iw, it->ih}; }
static void si_load(void) { nsi = 0; if (D) html_walk(D, si_add, 0); }
/* texto mas cercano al punto (coordenadas de la pagina) */
static void si_hit(int X, int Y, int *oi, int *oc) { int best = -1; long bd = 1L << 60; for (int i = 0; i < nsi; i++) { sit_t *t = &SI[i]; if (t->type != 1) continue; long dy = Y < t->y ? t->y - Y : Y >= t->y + t->h ? Y - t->y - t->h + 1 : 0, dx = X < t->x ? t->x - X : X >= t->x + t->w ? X - t->x - t->w + 1 : 0, d = dy * 100000 + dx; if (d < bd) { bd = d; best = i; } }
    *oi = best; if (best < 0) { *oc = 0; return; } sit_t *t = &SI[best]; int c = (X - t->x + 4 * t->sc) / (8 * t->sc); *oc = c < 0 ? 0 : c > t->len ? t->len : c; }
static int sel_has(void) { return sa_i >= 0 && sb_i >= 0 && (sa_i != sb_i || sa_c != sb_c); }
static void sel_order(int *i0, int *c0, int *i1, int *c1) { if (sa_i < sb_i || (sa_i == sb_i && sa_c <= sb_c)) { *i0 = sa_i; *c0 = sa_c; *i1 = sb_i; *c1 = sb_c; } else { *i0 = sb_i; *c0 = sb_c; *i1 = sa_i; *c1 = sa_c; } }
static void sel_clear(void) { sa_i = sb_i = -1; seldrag = 0; }
static void sel_start(int X, int Y) { si_load(); si_hit(X, Y, &sa_i, &sa_c); sb_i = sa_i; sb_c = sa_c; seldrag = 1; }
static void sel_all(void) { si_load(); sa_i = -1; for (int i = 0; i < nsi; i++) if (SI[i].type == 1) { if (sa_i < 0) { sa_i = i; sa_c = 0; } sb_i = i; sb_c = SI[i].len; } }
static void sel_paint(int vh) { if (!sel_has()) return; si_load(); int i0, c0, i1, c1; sel_order(&i0, &c0, &i1, &c1); for (int i = i0; i <= i1 && i < nsi; i++) { sit_t *t = &SI[i]; if (t->type != 1) continue; int a = i == i0 ? c0 : 0, b = i == i1 ? c1 : t->len; if (b <= a) continue; int y = TB + 4 + t->y - scroll; if (y + t->h < TB || y > TB + vh) continue; { int x0 = 4 + t->x + a * 8 * t->sc, x1 = MIN(G->w - 10, x0 + (b - a) * 8 * t->sc), ya = MAX(TB, y), yb = MIN(TB + vh, y + t->h); for (int yy = ya; yy < yb; yy++) { u32 *r = G->px + (long)yy * G->w; for (int xx = MAX(0, x0); xx < x1; xx++) r[xx] = ((r[xx] >> 1) & 0x7F7F7F) + ((0x93C5FD >> 1) & 0x7F7F7F); } } } } /* tinte azul encima del texto */
static char *sel_text(void) { si_load(); sb_t b = {0}; sb_s(&b, ""); int i0, c0, i1, c1; sel_order(&i0, &c0, &i1, &c1); int py = -1, pend = 0;
    for (int i = i0; i <= i1 && i < nsi; i++) { sit_t *t = &SI[i]; if (t->type != 1) continue; int a = i == i0 ? c0 : 0, e = i == i1 ? c1 : t->len; if (e <= a) continue; if (py >= 0) { if (t->y >= py + 4) sb_s(&b, "\n"); else if (t->x > pend + 2) sb_s(&b, " "); } u32 ul; char *u = cp437_to_utf8(t->s + a, e - a, &ul); if (u) { sb_add(&b, u, ul); free(u); } py = t->y; pend = t->x + e * 8 * t->sc; }
    return b.p; }
static void sel_copy(void) { char *t = sel_text(); if (!t) return; z_clip_set(t, strlen(t)); snprintf(msg, sizeof msg, "%s (%d %s)", _("Texto copiado"), (int)strlen(t), _("caracteres")); char m[120]; snprintf(m, 120, "[prueba] navegador copio %d caracteres", (int)strlen(t)); z_log(m); free(t); }
static void save_img(const sit_t *t) { char d[200], p[300]; dl_dir(d); u32 tm[6]; z_time(tm); snprintf(p, 300, "%s/imagen-%02u%02u%02u.png", d, tm[2], tm[1], tm[0]); int e = z_png_save(p, t->px, t->iw, t->ih); z_sync(); snprintf(msg, sizeof msg, "%s %s", e ? z_errstr(e) : _("Imagen guardada en"), e ? "" : p); }
static void print_(void);
static void page_menu(int X, int Y, int wx, int wy) { si_load(); int sx, sy; zw_screen(G, wx, wy, &sx, &sy); const char *l = html_link_at(D, X, Y); const sit_t *im = 0; for (int i = 0; i < nsi; i++) if (SI[i].type == 2 && SI[i].px && X >= SI[i].x && X < SI[i].x + SI[i].w && Y >= SI[i].y && Y < SI[i].y + SI[i].h) im = &SI[i];
    const char *it[8]; int id[8], k = 0; if (sel_has()) { it[k] = "Copiar"; id[k++] = 0; } if (l) { it[k] = "Abrir el enlace"; id[k++] = 1; it[k] = "Copiar el enlace"; id[k++] = 2; } if (im) { it[k] = "Copiar la imagen"; id[k++] = 3; it[k] = "Guardar la imagen"; id[k++] = 4; } it[k] = "Seleccionar todo"; id[k++] = 5; it[k] = "Imprimir"; id[k++] = 6;
    int r = ui_menu(sx, sy, it, k); if (r < 0) return; char nx[1100]; switch (id[r]) { case 0: sel_copy(); break; case 1: resolve(url, l, nx, sizeof nx); navigate(nx, 0, 1); break; case 2: resolve(url, l, nx, sizeof nx); z_clip_set(nx, strlen(nx)); strcpy(msg, _("Enlace copiado")); break; case 3: z_clip_set_img(im->px, im->iw, im->ih); strcpy(msg, _("Imagen copiada")); break; case 4: save_img(im); break; case 5: sel_all(); break; case 6: print_(); break; } }
/* ---------- Imprimir: la pagina se vuelve a maquetar a 760 px y se pasa al documento de impresion
   como textos, lineas e imagenes (texto nitido a la resolucion de la impresora). Sin fondos, como hacen los navegadores */
#define PW_ 760
typedef struct { zpdoc *d; double k, top, pagepx; int page; } pctx_t;
static void p_item(void *c, const struct html_item *it) { pctx_t *q = c; int pg = it->y / (int)q->pagepx; while (q->page < pg) { zp_page(q->d); q->page++; } double y = q->top + (it->y - pg * q->pagepx) * q->k, x = 36 + it->x * q->k;
    if (it->type == 0) { if (it->h <= 3 && it->w > 0) zp_rect(q->d, x, y, it->w * q->k, MAX(0.5, it->h * q->k), it->color & 0xFFFFFF, ZP_NONE, 0); return; }
    if (it->type == 2) { if (it->px && it->iw > 0 && it->ih > 0) zp_image(q->d, x, y, it->w * q->k, it->h * q->k, it->px, it->iw, it->ih); return; }
    u32 col = it->color & 0xFFFFFF; int r = col >> 16 & 255, g = col >> 8 & 255, b = col & 255; if (r * 3 + g * 6 + b > 2000) col = 0; /* texto muy claro: negro */
    u32 ul; char *u = cp437_to_utf8(it->str, it->len, &ul); if (!u) return; double sz = 8 * q->k * it->scale / 0.6; zp_text(q->d, x, y + 13 * it->scale * q->k, it->bold ? ZPF_MONOB : ZPF_MONO, sz, col, u); free(u);
    if (it->underline) zp_rect(q->d, x, y + 14.5 * it->scale * q->k, it->w * q->k, 0.6, col, ZP_NONE, 0); }
static void p_build(zpdoc *d, const zpset *s, void *c) { struct html_doc *P = html_load(src, srcl, PW_); if (!P) return; for (int i = 0; i < 40 && pending_imgs(); i++) load_one_img(); html_relayout(P);
    pctx_t q = {d, (zp_pw(d) - 72) / PW_, 54, 0, 0}; q.pagepx = (zp_ph(d) - 54 - 40) / q.k; zp_page(d); zp_text(d, 36, 30, ZPF_SANS, 7, 0x6B7280, url); html_walk(P, p_item, &q); html_free(P); }
static void print_(void) { if (!D || !src) return; const char *t = html_title(D); char tt[200]; if (t && *t) { u32 tl; char *u = cp437_to_utf8(t, strlen(t), &tl); strncpy0(tt, u ? u : t, 200); free(u); } else strncpy0(tt, url, 200); zp_dialog(tt, p_build, 0); char m[300]; snprintf(m, 300, "[prueba] navegador imprimir %s", url); z_log(m); }
static void draw(void) {
    int vh = G->h - TB - SB; g_rect(G, 0, TB, G->w, vh, D ? html_background(D) & 0xFFFFFF : 0xFFFFFF);
    if (D) { int mx = MAX(0, html_height(D) - vh); scroll = MAX(0, MIN(scroll, mx)); zw_clip(G, 0, TB, G->w - 10, vh); html_draw(D, 4, TB + 4, G->w - 18, vh, scroll); sel_paint(vh); zw_noclip(G); if (mx) { int bh = MAX(24, vh * vh / html_height(D)); g_rect(G, G->w - 8, TB + scroll * (vh - bh) / mx, 6, bh, UI_LINE); } }
    g_rect(G, 0, 0, G->w, TB, UI_PANEL); for (int i = 0; i < 4; i++) ui_button(G, bx(i), 6, bw(i), 26, BT[i], 0);
    int ax = addr_x(); u32 lk = secure ? 0x16A34A : certst ? 0xDC2626 : 0x9CA3AF; g_round(G, ax - 20, 13, 12, 10, 2, lk); g_frame(G, ax - 18, 8, 8, 7, lk);
    ui_input(G, ax, 6, addr_w(), 26, addr, acur, afoc); if (afoc && asel && addr[0]) { int tw = MIN(g_textw(addr), addr_w() - 10); g_rect(G, ax + 5, 10, tw, 18, 0xBFDBFE); zw_clip(G, ax + 4, 6, addr_w() - 8, 26); g_text(G, ax + 5, 11, addr, 0x111827); zw_noclip(G); } ui_button(G, ax + addr_w() + 6, 6, bw(4), 26, "*", 0); ui_button(G, ax + addr_w() + 10 + bw(4), 6, bw(5), 26, "Marcadores", 0); ui_button(G, ax + addr_w() + 14 + bw(4) + bw(5), 6, bw(6), 26, "Imprimir", 0);
    g_rect(G, 0, G->h - SB, G->w, SB, UI_PANEL); zw_clip(G, 0, G->h - SB, G->w, SB); g_text(G, 8, G->h - SB + 3, loading ? _("Cargando...") : msg, UI_TEXT); zw_noclip(G); zw_present(G); }
int main(const char *a) { int wp; a = z_print_arg(a, &wp);
    zsys s; z_sysinfo(&s); G = zw_new("Navegador", -1, 0, MIN(1040, (int)s.scr_w - 40), MIN(720, (int)s.scr_h - 80), WF_DECOR | WF_RESIZE); html_img_get = img_get;
    navigate(a[0] ? a : "/rd/res/inicio.html", 0, 0); draw(); if (wp) { print_(); draw(); } zevent e; int sel_drag = 0; (void)sel_drag;
    for (;;) { int idle = D && pending_imgs() > 0; if (!z_event(&e, idle ? 0 : -1)) { if (idle) { load_one_img(); if (!pending_imgs()) { html_relayout(D); snprintf(msg, sizeof msg, "%s", _("Listo")); } else if (nim % 3 == 0) html_relayout(D); draw(); } continue; }
        if (e.type == EV_REDRAW) { draw(); continue; } if (e.win != (u32)G->id) continue; if (e.type == EV_CLOSE) return 0;
        if (e.type == EV_RESIZE) { zw_resized(G, e.x, e.y); if (D) { char *c = src; src = 0; long n = srcl; set_page(c, n); } draw(); continue; }
        int vh = G->h - TB - SB;
        if (e.type == EV_WHEEL) scroll -= (int)e.key * 60;
        else if (e.type == EV_DOWN) { afoc = 0;
            if (e.y < TB) { int ax = addr_x();
                if (e.x >= ax && e.x < ax + addr_w()) { if (!afoc) asel = 1; afoc = 1; acur = strlen(addr); }
                else if (e.x < bx(1)) navigate("zunzun:volver", 0, 0);
                else if (e.x < bx(2)) { if (nf) { char *p = hf[--nf]; if (nb < 64) hb[nb++] = strdup(url); navigate(p, 0, 0); free(p); } }
                else if (e.x < bx(3)) navigate(url, 0, 0);
                else if (e.x < ax - 22) navigate("/rd/res/inicio.html", 0, 1);
                else if (e.x >= ax + addr_w() + 6 && e.x < ax + addr_w() + 6 + bw(4)) add_mark();
                else if (e.x >= ax + addr_w() + 14 + bw(4) + bw(5)) print_();
                else if (e.x >= ax + addr_w() + 10 + bw(4)) { int sx, sy; zw_screen(G, e.x, e.y + 20, &sx, &sy); marks_menu(sx, sy); } }
            else if (D && e.y < G->h - SB && (e.btn & 2)) page_menu(e.x - 4, e.y - TB - 4 + scroll, e.x, e.y);
            else if (D && e.y < G->h - SB) { int X = e.x - 4, Y = e.y - TB - 4 + scroll; sel_clear(); if (html_click(D, X, Y)) { int post = 0; const char *sub = html_take_submit(D, &post);
                    if (sub) { char act[1100]; const char *sep = post ? strchr(sub, '\n') : strchr(sub, '?'); int al = sep ? sep - sub : (int)strlen(sub); char a2[1100]; { int cl = MIN(al, (int)sizeof a2 - 1); memcpy(a2, sub, cl); a2[cl] = 0; } if (!a2[0]) { strcpy(act, url); char *q = strchr(act, '?'); if (q) *q = 0; } else resolve(url, a2, act, sizeof act);
                        if (post) navigate(act, sep ? sep + 1 : "", 1); else { char full[2200]; snprintf(full, sizeof full, "%s?%s", act, sep ? sep + 1 : ""); navigate(full, 0, 1); } }
                    else html_relayout(D); }
                else { const char *l = html_link_at(D, X, Y); if (l) { if (l[0] == '#') { int y = html_anchor_y(D, l + 1); if (y >= 0) scroll = y; } else { char nx[1100]; resolve(url, l, nx, sizeof nx); if (!strncasecmp(nx, "mailto:", 7) || !strncasecmp(nx, "javascript:", 11)) strcpy(msg, _("Ese enlace necesita un programa que no está disponible.")); else navigate(nx, 0, 1); } } else sel_start(X, Y); } } }
        else if (e.type == EV_MOVE && seldrag && D) { si_hit(e.x - 4, e.y - TB - 4 + scroll, &sb_i, &sb_c); if (e.y > G->h - SB - 10) scroll += 20; else if (e.y < TB + 10) scroll -= 20; }
        else if (e.type == EV_UP && seldrag) { seldrag = 0; if (sel_has()) { char m[80]; snprintf(m, 80, "[prueba] navegador seleccion %d-%d", sa_i, sb_i); z_log(m); } }
        else if (e.type == EV_MOVE && D && e.y > TB && e.y < G->h - SB) { const char *l = html_link_at(D, e.x - 4, e.y - TB - 4 + scroll); static int had; if (l) { char nx[1100]; resolve(url, l, nx, sizeof nx); snprintf(msg, sizeof msg, "%s", nx); had = 1; } else if (had) { had = 0; msg[0] = 0; } else continue; }
        else if (e.type == EV_KEY) {
            if (e.ch == 16) { print_(); draw(); continue; }
            if (!afoc && e.ch == 3 && sel_has()) { sel_copy(); draw(); continue; } if (!afoc && e.ch == 1) { sel_all(); draw(); continue; }
            if (e.ch == 12 && (e.mods & M_CTRL)) { afoc = 1; asel = 1; acur = strlen(addr); }
            else if (afoc) { if (e.ch == '\n') { afoc = 0; asel = 0; go_addr(); } else if (e.ch == 27) { afoc = 0; asel = 0; strcpy(addr, url); } else if (e.ch == 1) { asel = 1; acur = strlen(addr); } else { if (asel && (e.ch >= 32 || e.ch == 8 || e.ch == 22)) { addr[0] = 0; acur = 0; } asel = 0; ui_input_key(addr, 1000, &acur, &e); } }
            else { int k = e.key == K_UP ? KEY_UP : e.key == K_DOWN ? KEY_DOWN : e.key == K_LEFT ? KEY_LEFT : e.key == K_RIGHT ? KEY_RIGHT : e.key == K_HOME ? KEY_HOME : e.key == K_END ? KEY_END : e.key == K_PGUP ? KEY_PGUP : e.key == K_PGDN ? KEY_PGDN : e.key == K_DEL ? KEY_DEL : e.ch < 128 ? (int)e.ch : uni_to_cp437(e.ch);
                if (D && k && html_key(D, k)) { int post = 0; const char *sub = html_take_submit(D, &post); if (sub) { char act[1100]; const char *sep = post ? strchr(sub, '\n') : strchr(sub, '?'); int al = sep ? sep - sub : (int)strlen(sub); char a2[1100]; { int cl = MIN(al, (int)sizeof a2 - 1); memcpy(a2, sub, cl); a2[cl] = 0; } if (!a2[0]) { strcpy(act, url); char *q = strchr(act, '?'); if (q) *q = 0; } else resolve(url, a2, act, sizeof act); if (post) navigate(act, sep ? sep + 1 : "", 1); else { char full[2200]; snprintf(full, sizeof full, "%s?%s", act, sep ? sep + 1 : ""); navigate(full, 0, 1); } } else html_relayout(D); }
                else if (e.key == K_F5) navigate(url, 0, 0); else if (e.key == K_BS) navigate("zunzun:volver", 0, 0);
                else if (e.key == K_DOWN) scroll += 40; else if (e.key == K_UP) scroll -= 40; else if (e.key == K_PGDN || e.ch == ' ') scroll += vh - 40; else if (e.key == K_PGUP) scroll -= vh - 40; else if (e.key == K_HOME) scroll = 0; else if (e.key == K_END) scroll = 1 << 28; else continue; } }
        draw(); }
}
