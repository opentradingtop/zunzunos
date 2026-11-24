/* Ajustes, papelera, menus emergentes, fechas e idioma */
#include "z.h"
#define CFG "/sys/ZUNZUN/AJUSTES.TXT"
static char *cfg_read(void) { char *d = z_readfile(CFG, 0); return d ? d : strdup(""); }
static const char *cfg_find(const char *d, const char *k) { int l = strlen(k); for (const char *p = d; *p;) { if (!strncmp(p, k, l) && p[l] == '=') return p + l + 1; p = strchr(p, '\n'); if (!p) break; p++; } return 0; }
int z_cfg_get(const char *k, int def) { char *d = cfg_read(); const char *v = cfg_find(d, k); int r = v ? atoi(v) : def; free(d); return r; }
void z_cfg_gets(const char *k, char *o, int cap) { char *d = cfg_read(); const char *v = cfg_find(d, k); o[0] = 0; if (v) { int i = 0; while (v[i] && v[i] != '\n' && i < cap - 1) { o[i] = v[i]; i++; } o[i] = 0; } free(d); }
void z_cfg_sets(const char *k, const char *val) { char *d = cfg_read(); sb_t b = {0}; sb_s(&b, ""); int l = strlen(k), done = 0; for (char *p = d; *p;) { char *e = strchr(p, '\n'); int n = e ? e - p : (int)strlen(p); if (!strncmp(p, k, l) && p[l] == '=') { sb_f(&b, "%s=%s\n", k, val); done = 1; } else if (n) { sb_add(&b, p, n); sb_s(&b, "\n"); } if (!e) break; p = e + 1; } if (!done) sb_f(&b, "%s=%s\n", k, val); z_writefile(CFG, b.p, b.n); free(b.p); free(d); }
void z_cfg_set(const char *k, int v) { char t[16]; snprintf(t, 16, "%d", v); z_cfg_sets(k, t); }
int z_trash(const char *path) {
    if (path[0] != '/') return -22; const char *s = strchr(path + 1, '/'); if (!s) return -22; char vol[40]; int vl = MIN((int)(s - path), 39); memcpy(vol, path, vl); vol[vl] = 0;
    char td[64], dst[160], inf[170]; snprintf(td, 64, "%s/.PAPELERA", vol); z_mkdir(td); u32 t[6]; z_time(t);
    snprintf(dst, 160, "%s/%u%02u%02u%02u%02u%02u-%04x", td, t[5], t[4], t[3], t[2], t[1], t[0], rnd() & 0xFFFF);
    int e = z_rename(path, dst); if (e) return e; snprintf(inf, 170, "%s.info", dst); char b[600]; int n = snprintf(b, 600, "%s\n%02u/%02u/%u %02u:%02u\n", path, t[3], t[4], t[5], t[2], t[1]); z_writefile(inf, b, n); return 0;
}
void zw_screen(zwin *w, int x, int y, int *sx, int *sy) { zwininfo i; zw_info(w->id, &i); *sx = i.x + x; *sy = i.y + y; }
int ui_menu(int sx, int sy, const char **it, int n) {
    int w = 60; for (int i = 0; i < n; i++) w = MAX(w, g_textw(tr(it[i])) + 32); zsys s; z_sysinfo(&s); int h = n * 26 + 4; sx = MIN(sx, (int)s.scr_w - w - 2); sy = MIN(sy, (int)s.scr_h - h - 2);
    zwin *m = zw_new("menu", sx, sy, w, h, WF_POPUP); int hv = -1, r = -1, go = 1; zevent e;
    while (go) { g_clear(m, UI_WHITE); g_frame(m, 0, 0, m->w, m->h, UI_LINE); for (int i = 0; i < n; i++) { if (i == hv) g_rect(m, 2, 2 + i * 26, m->w - 4, 26, UI_ACC2); g_text(m, 14, 7 + i * 26, tr(it[i]), UI_TEXT); } zw_present(m);
        if (!z_event(&e, -1) || e.win != (u32)m->id) continue; if (e.type == EV_MOVE) { hv = e.y >= 2 ? (e.y - 2) / 26 : -1; if (hv >= n) hv = -1; } else if (e.type == EV_DOWN) { r = e.y >= 2 ? (e.y - 2) / 26 : -1; if (r >= n) r = -1; go = 0; } else if (e.type == EV_BLUR || (e.type == EV_KEY && e.ch == 27)) go = 0; else if (e.type == EV_KEY && (e.key == 0x51 || e.key == 0x52)) { hv = (hv + (e.key == 0x51 ? 1 : n - 1)) % n; } else if (e.type == EV_KEY && e.ch == '\n' && hv >= 0) { r = hv; go = 0; } }
    zw_close(m); return r;
}
int ui_edit_menu(zwin *w, zevent *e) { if (e->type != EV_DOWN || !(e->btn & 2) || e->win != (u32)w->id) return 0; static const char *it[] = {"Copiar", "Cortar", "Pegar", "Seleccionar todo"}; static const u8 ch[] = {3, 24, 22, 1}, key[] = {0x06, 0x1B, 0x19, 0x04}; int sx, sy; zw_screen(w, e->x, e->y, &sx, &sy); int r = ui_menu(sx, sy, it, 4); if (r < 0) { e->type = EV_NONE; return 1; } e->type = EV_KEY; e->ch = ch[r]; e->key = key[r]; e->mods = M_CTRL; return 1; }
int z_weekday(int y, int m, int d) { static const u8 t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4}; if (m < 3) y--; return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d + 6) % 7; }
const char *z_month(int m) { static const char *M[] = {"enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre"}; return tr(M[(m - 1) % 12]); }
void z_fmt_time(const u32 *t, int h24, int secs, char *o, int cap) { int h = t[2]; if (h24) { if (secs) snprintf(o, cap, "%02d:%02u:%02u", h, t[1], t[0]); else snprintf(o, cap, "%02d:%02u", h, t[1]); } else { const char *ap = h < 12 ? "AM" : "PM"; h %= 12; if (!h) h = 12; if (secs) snprintf(o, cap, "%d:%02u:%02u %s", h, t[1], t[0], ap); else snprintf(o, cap, "%d:%02u %s", h, t[1], ap); } }
void z_fmt_date(const u32 *t, int f, char *o, int cap) { static const char *WD[] = {"lun", "mar", "mie", "jue", "vie", "sab", "dom"}; int en = TH.lang == 1; const char *wd = tr(WD[z_weekday(t[5], t[4], t[3])]);
    if (f == 0) o[0] = 0; else if (f == 2) { if (en) snprintf(o, cap, "%s %u", z_month(t[4]), t[3]); else snprintf(o, cap, "%u %s", t[3], z_month(t[4])); } else if (f == 3) snprintf(o, cap, "%u-%02u-%02u", t[5], t[4], t[3]); else if (f == 4) { if (en) snprintf(o, cap, "%s, %s %u", wd, z_month(t[4]), t[3]); else snprintf(o, cap, "%s %u %s", wd, t[3], z_month(t[4])); } else if (en) snprintf(o, cap, "%02u/%02u/%u", t[4], t[3], t[5]); else snprintf(o, cap, "%02u/%02u/%u", t[3], t[4], t[5]); }
const char *const z_fams[] = {"Rescate", "Oficina", "Internet", "Multimedia", "Juegos", "Sistema"};
const u32 z_famcol[] = {0xB91C1C, 0x15803D, 0x0284C7, 0xDB2777, 0xCA8A04, 0x475569};
const int z_nfams = 6;
const zapp z_apps[] = {
    {"Rescate","rescate",0xB91C1C,"+",0},{"Archivos","files",0xD97706,"[]",0},{"Discos","discos",0xB45309,"(=)",0},{"Papelera","papelera",0x64748B,"\\_/",0},{"Restaurar","restaurar",0x1D4ED8,"<<",0},{"Instalar","instalar",0x0D9488,"->",0},
    {"Escritor","escritor",0x7C2D12,"Es",1},{"Hoja","hoja",0x15803D,"##",1},{"Presentar","diapos",0xC2410C,"|>",1},{"Agenda","agenda",0x0369A1,"31",1},{"Editor","editor",0x2563EB,"Aa",1},{"Calculadora","calc",0x7C3AED,"+=",1},{"Lector PDF","pdf",0xB91C1C,"PDF",1},
    {"Navegador","browser",0x0284C7,"@",2},{"Apps web","apps",0x6D28D9,"</>",2},
    {"Imágenes","viewer",0x059669,"::",3},{"Música","player",0xE11D48,"d|",3},{"Grabadora","grabadora",0xBE123C,"(o)",3},{"Pintar","paint",0xDB2777,"~*",3},
    {"Serpiente","snake",0x16A34A,"S~",4},{"Campo minado","mines",0x57534E,"#",4},{"Bloques","blocks",0x9333EA,"==",4},{"Enjambre","enjambre",0xCA8A04,"\\/",4},
    {"Configuración","config",0x475569,"*",5},{"Impresoras","impresoras",0x334155,"[=]",5},{"Terminal","term",0x1F2937,">_",5},{"Monitor","sysmon",0x0F766E,"%",5},{"Acerca de","acerca",0x0F766E,"i",5},
    {"Correo","correo",0x1D4ED8,"@>",2},{"LUMEN","lumen",0x7C3AED,"IA",2},{"Red local","redlocal",0x0E7490,"<>",2},
    {"Grabar pantalla","pantalla",0xDC2626,"(*)",3},{"Vídeos","video",0x7C3AED,"|>|",3},
    {"Navegador JS","navjs",0xCA8A04,"JS",2},{"Consola JavaScript","jsconsola",0x854D0E,"{}",2},
};
const int z_napps = sizeof z_apps / sizeof z_apps[0];
int z_hidden(const char *bin) { char h[400], k[24]; z_cfg_gets("ocultos", h, 400); snprintf(k, 24, ",%s,", bin); char w[404]; snprintf(w, 404, ",%s,", h); return strstr(w, k) != 0; }
void z_set_hidden(const char *bin, int hide) { char h[400], o[420] = ""; z_cfg_gets("ocultos", h, 400); char *p = h; while (*p) { char *c = strchr(p, ','); if (c) *c = 0; if (*p && strcmp(p, bin)) { if (o[0]) strcat(o, ","); strcat(o, p); } if (!c) break; p = c + 1; } if (hide) { if (o[0]) strcat(o, ","); strcat(o, bin); } z_cfg_sets("ocultos", o); }
static int z_iext(const char *p, const char *e) { return z_ends(p, e); }
const char *z_app_for(const char *p) { if (z_iext(p, ".png") || z_iext(p, ".bmp") || z_iext(p, ".jpg") || z_iext(p, ".jpeg") || z_iext(p, ".gif")) return "viewer"; if (z_iext(p, ".wav") || z_iext(p, ".mp3")) return "player"; if (z_iext(p, ".avi")) return "video"; if (z_iext(p, ".htm") || z_iext(p, ".html")) return "apps"; if (z_iext(p, ".zes") || z_iext(p, ".docx") || z_iext(p, ".odt") || z_iext(p, ".rtf")) return "escritor"; if (z_iext(p, ".xlsx") || z_iext(p, ".csv")) return "hoja"; if (z_iext(p, ".zdp")) return "diapos"; if (z_iext(p, ".pdf")) return "pdf"; return "editor"; }
/* acceso directo: archivo NOMBRE.acceso con "ZACCESO1\n<ruta>\n" */
int z_shortcut_target(const char *p, char *o, int cap) { o[0] = 0; if (!z_ends(p, ".acceso")) return -22; char *t = z_readfile(p, 0); if (!t) return -2; int r = -22; if (!strncmp(t, "ZACCESO1\n", 9)) { char *e = strchr(t + 9, '\n'); if (e) *e = 0; strncpy0(o, t + 9, cap); r = 0; } free(t); return r; }
int z_make_shortcut(const char *target, const char *dir, char *made, int cap) { const char *n = strrchr(target, '/'); n = n && n[1] ? n + 1 : target; char p[400]; snprintf(p, 400, "%s/%s.acceso", dir, n); zdirent d; for (int k = 2; !z_stat(p, &d) && k < 100; k++) snprintf(p, 400, "%s/%s (%d).acceso", dir, n, k); char b[600]; int l = snprintf(b, 600, "ZACCESO1\n%s\n", target); int e = z_writefile(p, b, l); z_sync(); if (made) strncpy0(made, p, cap); return e; }
int z_open_path(const char *p) { zdirent d; if (z_stat(p, &d)) return -2; { char t[400]; if (!z_shortcut_target(p, t, 400)) { if (z_stat(t, &d)) return -2; p = t; char a[64]; if (d.is_dir) return z_spawn("/rd/bin/files", p); snprintf(a, 64, "/rd/bin/%s", z_app_for(p)); return z_spawn(a, p); } } char a[64]; if (d.is_dir) { snprintf(a, 64, "/rd/bin/files"); return z_spawn(a, p); } snprintf(a, 64, "/rd/bin/%s", z_app_for(p)); return z_spawn(a, p); }
void z_notify(void) { ztheme t; z_theme_get(&t); z_theme_set(&t); }
void z_flag(zwin *w, int x, int y, int lang) { if (!lang) { g_rect(w, x, y, 30, 20, 0xAA151B); g_rect(w, x, y + 5, 30, 10, 0xF1BF00); } else { for (int i = 0; i < 13; i++) g_rect(w, x, y + i * 20 / 13, 30, (i + 1) * 20 / 13 - i * 20 / 13, i & 1 ? 0xFFFFFF : 0xB22234); g_rect(w, x, y, 13, 11, 0x3C3B6E); for (int j = 0; j < 3; j++) for (int i = 0; i < 4; i++) g_rect(w, x + 2 + i * 3, y + 2 + j * 3, 1, 1, 0xFFFFFF); } g_frame(w, x, y, 30, 20, 0x111827); }
/* ---------- PNG (capturas de pantalla): deflate con codigos Huffman fijos y LZ77 con tabla hash ---------- */
typedef struct { u8 *p; long n, cap; u32 bb; int bc; } bw_t;
static void bw_byte(bw_t *w, u8 b) { if (w->n == w->cap) { w->cap = w->cap * 2 + 65536; w->p = realloc(w->p, w->cap); } w->p[w->n++] = b; }
static void bw_bits(bw_t *w, u32 v, int n) { w->bb |= v << w->bc; w->bc += n; while (w->bc >= 8) { bw_byte(w, w->bb); w->bb >>= 8; w->bc -= 8; } }
static void bw_huff(bw_t *w, u32 code, int n) { u32 r = 0; for (int i = 0; i < n; i++) r |= (code >> i & 1) << (n - 1 - i); bw_bits(w, r, n); } /* los codigos Huffman van con el bit alto primero */
static void lit(bw_t *w, int c) { if (c < 144) bw_huff(w, 0x30 + c, 8); else if (c < 256) bw_huff(w, 0x190 + c - 144, 9); else if (c < 280) bw_huff(w, c - 256, 7); else bw_huff(w, 0xC0 + c - 280, 8); }
static const u16 LB[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258}; static const u8 LE[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const u16 DB[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577}; static const u8 DE[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
static void match(bw_t *w, int len, int dist) { int i = 28; while (LB[i] > len) i--; lit(w, 257 + i); if (LE[i]) bw_bits(w, len - LB[i], LE[i]); int d = 29; while (DB[d] > dist) d--; bw_huff(w, d, 5); if (DE[d]) bw_bits(w, dist - DB[d], DE[d]); }
static void deflate_fixed(bw_t *w, const u8 *d, long n) {
    bw_bits(w, 1, 1); bw_bits(w, 1, 2); int *head = malloc(65536 * sizeof(int)), *prev = malloc(32768 * sizeof(int)); for (int i = 0; i < 65536; i++) head[i] = -1;
    #define HSH(i) ((((u32)d[i] << 10) ^ ((u32)d[(i) + 1] << 5) ^ d[(i) + 2]) & 0xFFFF)
    long i = 0; while (i < n) { int best = 0, bd = 0; if (i + 2 < n) { int h = HSH(i), c = head[h], tries = 16; while (c >= 0 && i - c <= 32768 && tries--) { int l = 0; while (l < 258 && i + l < n && d[c + l] == d[i + l]) l++; if (l > best) { best = l; bd = i - c; if (l == 258) break; } int pc = prev[c & 32767]; if (pc >= c) break; c = pc; } }
        long adv = best >= 3 ? best : 1; if (best >= 3) match(w, best, bd); else lit(w, d[i]);
        for (long k = 0; k < adv; k++, i++) if (i + 2 < n) { int h = HSH(i); prev[i & 32767] = head[h]; head[h] = i; } }
    #undef HSH
    lit(w, 256); if (w->bc) bw_bits(w, 0, 8 - w->bc); free(head); free(prev); }
static void be32(bw_t *w, u32 v) { bw_byte(w, v >> 24); bw_byte(w, v >> 16); bw_byte(w, v >> 8); bw_byte(w, v); }
static void png_chunk(bw_t *o, const char *t, const u8 *d, long n) { be32(o, n); long s = o->n; for (int i = 0; i < 4; i++) bw_byte(o, t[i]); for (long i = 0; i < n; i++) bw_byte(o, d[i]); be32(o, z_crc32(o->p + s, o->n - s)); }
int z_png_save(const char *path, const u32 *px, int w, int h) {
    long rl = (long)w * 3 + 1, n = rl * h; u8 *raw = malloc(n); if (!raw) return -12; for (int y = 0; y < h; y++) { u8 *r = raw + y * rl; r[0] = 0; for (int x = 0; x < w; x++) { u32 c = px[(long)y * w + x]; r[1 + x * 3] = c >> 16; r[2 + x * 3] = c >> 8; r[3 + x * 3] = c; } }
    bw_t z = {0}; bw_byte(&z, 0x78); bw_byte(&z, 0x01); deflate_fixed(&z, raw, n); u32 a = 1, b = 0; for (long i = 0; i < n; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; } be32(&z, b << 16 | a); free(raw);
    bw_t o = {0}; static const u8 sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10}; for (int i = 0; i < 8; i++) bw_byte(&o, sig[i]); u8 ih[13] = {w >> 24, w >> 16, w >> 8, w, h >> 24, h >> 16, h >> 8, h, 8, 2, 0, 0, 0};
    png_chunk(&o, "IHDR", ih, 13); png_chunk(&o, "IDAT", z.p, z.n); png_chunk(&o, "IEND", 0, 0); free(z.p); int e = z_writefile(path, (char *)o.p, o.n); free(o.p); return e; }
/* ---------- imagen en el portapapeles ---------- */
int z_clip_set_img(const u32 *px, int w, int h) { long n = 12 + (long)w * h * 4; u8 *b = malloc(n); if (!b) return -12; memcpy(b, "ZIMG", 4); memcpy(b + 4, &w, 4); memcpy(b + 8, &h, 4); memcpy(b + 12, px, (long)w * h * 4); int r = SC2(15, b, n); free(b); return r < 0 ? r : 0; }
u32 *z_clip_get_img(int *w, int *h) { long n = SC2(16, 0, 0); if (n < 12) return 0; u8 *b = malloc(n); if (!b) return 0; SC2(16, b, n); if (memcmp(b, "ZIMG", 4)) { free(b); return 0; } memcpy(w, b + 4, 4); memcpy(h, b + 8, 4); if ((long)*w * *h * 4 + 12 > n || *w <= 0 || *h <= 0) { free(b); return 0; } u32 *px = malloc((long)*w * *h * 4); memcpy(px, b + 12, (long)*w * *h * 4); free(b); return px; }

/* reglas de Internet por programa, guardadas en /sys/ZUNZUN/PERMISOS-RED.TXT ("programa=0|1|2").
   Si no existe el archivo: el navegador, el correo, LUMEN y las apps web pueden salir; los demas preguntan. */
#define RULES "/sys/ZUNZUN/PERMISOS-RED.TXT"
int z_rules_load(void) { char *t = z_readfile(RULES, 0); int n = 0; z_guard(0, "*", 0);
    if (!t) { static const char *def[] = {"browser", "correo", "lumen", "apps"}; for (int i = 0; i < 4; i++) { z_guard(0, def[i], 1); n++; } return n; }
    for (char *p = t; p && *p; ) { char *e = strchr(p, '\n'); if (e) *e = 0; char *q = strchr(p, '='); if (q) { *q = 0; z_guard(0, p, atoi(q + 1)); n++; } p = e ? e + 1 : 0; } free(t); return n; }
int z_rules_save(void) { zrule r[64]; int n = z_guard(1, r, 64); if (n < 0) return n; char b[64 * 32]; int l = 0; for (int i = 0; i < n; i++) l += snprintf(b + l, sizeof b - l, "%s=%u\n", r[i].name, r[i].pol); int e = z_writefile(RULES, b, l); z_sync(); return e; }
const char *z_app_name(const char *bin) { for (int i = 0; i < z_napps; i++) if (!strcmp(z_apps[i].bin, bin)) return tr(z_apps[i].name); return bin; }

/* pide una frase sin mostrarla (puntos en lugar de letras). 1 si se acepto */
int z_pass_dialog(const char *title, const char *msg, char *buf, int cap) { zsys s; z_sysinfo(&s); int w = 480, h = 150; zwin *d = zw_new(title, (s.scr_w - w) / 2, (s.scr_h - h) / 2, w, h, WF_DECOR); if (!d) return 0;
    int cur = 0, r = -1; buf[0] = 0; zevent e; char mk[200];
    while (r < 0) { int n = 0; for (const char *q = buf; *q; ) { utf8_next(&q); if (n < 190) mk[n++] = '*'; } mk[n] = 0; g_clear(d, UI_BG); g_text(d, 16, 16, tr(msg), UI_TEXT); ui_input(d, 16, 44, w - 32, 28, mk, n, 1);
        ui_button(d, w - 230, h - 46, 100, 30, "Cancelar", 0); ui_button(d, w - 120, h - 46, 104, 30, "Aceptar", 2); zw_present(d);
        if (!z_event(&e, -1) || e.win != (u32)d->id) continue; if (e.type == EV_CLOSE) r = 0;
        else if (e.type == EV_KEY) { if (e.ch == 27) r = 0; else if (e.ch == '\n') r = 1; else ui_input_key(buf, cap, &cur, &e); }
        else if (e.type == EV_DOWN && e.y >= h - 46 && e.y < h - 16) { if (e.x >= w - 120) r = 1; else if (e.x >= w - 230) r = 0; } }
    zw_close(d); if (!r) { memset(buf, 0, cap); buf[0] = 0; } return r; }
/* reescribe los datos protegidos (al activar el cifrado se cifran; al desactivarlo se guardan sin cifrar) */
static int rw_dir(const char *dir, int depth, void (*pg)(int, void *), void *ctx, int *n) { zdirent e; char names[128][256]; int k = 0, dirs[128];
    for (int i = 0; k < 128 && !z_readdir(dir, i, &e); i++) { if (e.name[0] == '.') continue; dirs[k] = e.is_dir; strncpy0(names[k++], e.name, 256); }
    for (int i = 0; i < k; i++) { char p[512]; snprintf(p, 512, "%s/%s", dir, names[i]); if (dirs[i]) { if (depth > 0) rw_dir(p, depth - 1, pg, ctx, n); continue; }
        static const char *X[] = {"KERNEL.BIN", "INITRD.BIN", "CIFRADO.TXT", "SESION.TXT", "TEMA.TXT", "AJUSTES.TXT"}; int skip = 0; for (unsigned j = 0; j < sizeof X / sizeof *X; j++) if (!strcmp(names[i], X[j])) skip = 1; if (skip) continue;   /* el sistema y lo que no se cifra */
        if (z_ends(p, ".zztmp")) continue; long len; char *d = z_readfile(p, &len); if (!d) continue; z_writefile(p, d, len); free(d); (*n)++; if (pg && *n % 10 == 0) pg(*n, ctx); }
    return 0; }
int z_crypt_rewrite(void (*progress)(int, void *), void *ctx) { int n = 0; static const char *D[] = {"/sys/DOCS", "/sys/ESCRITORIO", "/sys/ZUNZUN", "/sys/ZUNZUN-PUNTOS"};
    for (int i = 0; i < 4; i++) rw_dir(D[i], 8, progress, ctx, &n); z_sync(); return n; }
