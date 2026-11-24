/* Lector propio de colmenas del registro de Windows (formato regf), solo lectura, para Forense.
   Claves 'nk', listas de subclaves 'lf'/'lh'/'li'/'ri', valores 'vk' y datos grandes 'db'.
   Las colmenas de un Windows averiado pueden estar danadas: cada desplazamiento se comprueba antes de usarlo. */
#include "z.h"
#include "zntfs.h"

struct zreg { const u8 *b; u32 n, root; };
const u8 *zr_cell(zreg *r, u32 off, u32 *len);
#define RD16(p) ((u16)((p)[0] | (p)[1] << 8))
#define RD32(p) ((u32)((p)[0] | (p)[1] << 8 | (p)[2] << 16 | (u32)(p)[3] << 24))

zreg *zr_open(const u8 *d, long len) {
    if (len < 4096 + 32 || memcmp(d, "regf", 4)) return 0;
    zreg *r = calloc(1, sizeof *r); if (!r) return 0;
    r->b = d + 4096; u32 hb = RD32(d + 0x28); r->n = (u32)(len - 4096); if (hb && hb < r->n) r->n = hb;
    r->root = RD32(d + 0x24);
    const u8 *c; u32 cl;
    if (!(c = zr_cell(r, r->root, &cl)) || cl < 0x4C || memcmp(c, "nk", 2)) { free(r); return 0; }
    return r; }
void zr_close(zreg *r) { free(r); }

/* datos de la celda off (sin su tamano) y su longitud; 0 si no es valida */
const u8 *zr_cell(zreg *r, u32 off, u32 *len) {
    if (off == 0xFFFFFFFF || off > r->n - 8 || off & 3) return 0;
    i32 s = (i32)RD32(r->b + off); u32 sz = s < 0 ? (u32)-s : (u32)s;
    if (sz < 8 || off + sz > r->n) return 0;
    *len = sz - 4; return r->b + off + 4; }

static int nk_ok(zreg *r, u32 off, const u8 **k, u32 *kl) { return (*k = zr_cell(r, off, kl)) && *kl >= 0x4C && !memcmp(*k, "nk", 2) && 0x4C + RD16(*k + 0x48) <= *kl; }

/* nombre de una clave o valor a UTF-8 (ASCII/latin1 si 'comp', si no UTF-16) */
static void name_of(const u8 *p, int len, int comp, char *o, int cap) {
    if (!comp) { zn_utf8(p, len / 2, o, cap); return; }
    int k = 0; for (int i = 0; i < len && k + 2 < cap; i++) { u8 c = p[i]; if (c < 0x80) o[k++] = c; else { o[k++] = 0xC0 | c >> 6; o[k++] = 0x80 | (c & 63); } }
    o[k] = 0; }

static int same(const char *a, const char *b, int bn) {
    for (int i = 0; i < bn; i++) { char x = a[i], y = b[i]; if (!x) return 0; if (x >= 'a' && x <= 'z') x -= 32; if (y >= 'a' && y <= 'z') y -= 32; if (x != y) return 0; }
    return !a[bn]; }

/* subclave i de una lista (lf/lh/li, o ri de listas); devuelve su celda o -1 */
static int list_at(zreg *r, u32 off, int i, int depth) {
    u32 l; const u8 *c = zr_cell(r, off, &l); if (!c || l < 4 || depth > 2) return -1;
    int n = RD16(c + 2);
    if (!memcmp(c, "lf", 2) || !memcmp(c, "lh", 2)) { if (i >= n || 4 + (u32)(i + 1) * 8 > l) return -1; return RD32(c + 4 + i * 8); }
    if (!memcmp(c, "li", 2)) { if (i >= n || 4 + (u32)(i + 1) * 4 > l) return -1; return RD32(c + 4 + i * 4); }
    if (!memcmp(c, "ri", 2)) {
        for (int j = 0; j < n && 4 + (u32)(j + 1) * 4 <= l; j++) {
            u32 sl; const u8 *s = zr_cell(r, RD32(c + 4 + j * 4), &sl); if (!s || sl < 4) continue;
            int m = RD16(s + 2); if (i < m) return list_at(r, RD32(c + 4 + j * 4), i, depth + 1); i -= m; } }
    return -1; }

int zr_subkey(zreg *r, int key, int i, char *name, int cap) {
    const u8 *k; u32 kl; if (!nk_ok(r, key, &k, &kl) || i < 0 || (u32)i >= RD32(k + 0x14)) return -1;
    int s = list_at(r, RD32(k + 0x1C), i, 0); const u8 *sk; u32 sl;
    if (s < 0 || !nk_ok(r, s, &sk, &sl)) return -1;
    if (name) name_of(sk + 0x4C, RD16(sk + 0x48), RD16(sk + 2) & 0x20, name, cap);
    return s; }

int zr_key(zreg *r, const char *path) {
    int key = r->root; const char *p = path;
    while (*p) {
        while (*p == '\\' || *p == '/') p++; if (!*p) break;
        const char *q = p; while (*q && *q != '\\' && *q != '/') q++;
        int found = -1; char nm[256];
        for (int i = 0; i < 100000; i++) { int s = zr_subkey(r, key, i, nm, sizeof nm); if (s < 0) break; if (same(nm, p, q - p)) { found = s; break; } }
        if (found < 0) return -1; key = found; p = q; }
    return key; }

/* valor i de la clave: celda del vk o -1 */
static int vk_at(zreg *r, int key, int i) {
    const u8 *k; u32 kl; if (!nk_ok(r, key, &k, &kl) || i < 0 || (u32)i >= RD32(k + 0x24)) return -1;
    u32 ll; const u8 *l = zr_cell(r, RD32(k + 0x28), &ll); if (!l || (u32)(i + 1) * 4 > ll) return -1;
    u32 v = RD32(l + i * 4), vl; const u8 *vk = zr_cell(r, v, &vl);
    if (!vk || vl < 0x14 || memcmp(vk, "vk", 2) || 0x14 + RD16(vk + 2) > vl) return -1;
    return v; }

int zr_value_at(zreg *r, int key, int i, char *name, int cap, int *type) {
    int v = vk_at(r, key, i); if (v < 0) return -1; u32 vl; const u8 *vk = zr_cell(r, v, &vl);
    if (name) name_of(vk + 0x14, RD16(vk + 2), RD16(vk + 0x10) & 1, name, cap);
    if (type) *type = RD32(vk + 0xC);
    return v; }

/* datos del valor (hasta cap bytes); devuelve su longitud total o -1 */
static int vk_data(zreg *r, int v, int *type, u8 *out, int cap) {
    u32 vl; const u8 *vk = zr_cell(r, v, &vl); if (!vk) return -1;
    u32 sz = RD32(vk + 4), off = RD32(vk + 8); if (type) *type = RD32(vk + 0xC);
    if (sz & 0x80000000) { sz &= 0x7FFFFFFF; if (sz > 4) sz = 4; if (out) memcpy(out, vk + 8, sz < (u32)cap ? sz : (u32)cap); return sz; }
    u32 dl; const u8 *d = zr_cell(r, off, &dl); if (!d) return -1;
    if (sz > 16344 && dl >= 8 && !memcmp(d, "db", 2)) {                         /* datos grandes en segmentos */
        int ns = RD16(d + 2); u32 sl; const u8 *s = zr_cell(r, RD32(d + 4), &sl); if (!s) return -1;
        u32 done = 0;
        for (int i = 0; i < ns && (u32)(i + 1) * 4 <= sl && done < sz; i++) {
            u32 gl; const u8 *g = zr_cell(r, RD32(s + i * 4), &gl); if (!g) return -1;
            u32 k = gl < 16344 ? gl : 16344; if (k > sz - done) k = sz - done;
            if (out && done < (u32)cap) memcpy(out + done, g, k < (u32)cap - done ? k : (u32)cap - done);
            done += k; }
        return done; }
    if (sz > dl) sz = dl;
    if (out) memcpy(out, d, sz < (u32)cap ? sz : (u32)cap);
    return sz; }

int zr_value(zreg *r, int key, const char *name, int *type, u8 *out, int cap) {
    char nm[256];
    for (int i = 0; i < 100000; i++) { int v = zr_value_at(r, key, i, nm, sizeof nm, 0); if (v < 0) return -1;
        if (same(nm, name, strlen(name)) || (!nm[0] && !strcmp(name, "@"))) return vk_data(r, v, type, out, cap); }
    return -1; }

int zr_str(zreg *r, const char *path, const char *value, char *out, int cap) {
    out[0] = 0; int key = zr_key(r, path); if (key < 0) return -1;
    u8 buf[2048]; int t, n = zr_value(r, key, value, &t, buf, sizeof buf); if (n < 0) return -1; if (n > (int)sizeof buf) n = sizeof buf;
    if (t == 1 || t == 2) { int w = n / 2; for (int i = 0; i < w; i++) if (!RD16(buf + 2 * i)) { w = i; break; } zn_utf8(buf, w, out, cap); }
    else if (t == 4 && n >= 4) snprintf(out, cap, "%u", RD32(buf));
    else if (t == 11 && n >= 8) snprintf(out, cap, "%lu", (u64)RD32(buf) | (u64)RD32(buf + 4) << 32);
    else if (t == 7) { int k = 0; for (int i = 0; i + 1 < n && k < cap - 3; ) { int j = i; while (j + 1 < n && RD16(buf + j)) j += 2; if (j == i) break;
            if (k) { out[k++] = ';'; out[k++] = ' '; } zn_utf8(buf + i, (j - i) / 2, out + k, cap - k); k += strlen(out + k); i = j + 2; } }
    else { int k = 0; for (int i = 0; i < n && i < 32 && k + 3 < cap; i++) k += snprintf(out + k, cap - k, "%02x", buf[i]); }
    return t; }

int zr_u32(zreg *r, const char *path, const char *value, u32 *o) {
    int key = zr_key(r, path); if (key < 0) return -1; u8 b[8] = {0}; int t; if (zr_value(r, key, value, &t, b, 8) < 4) return -1; *o = RD32(b); return 0; }
int zr_u64(zreg *r, const char *path, const char *value, u64 *o) {
    int key = zr_key(r, path); if (key < 0) return -1; u8 b[8] = {0}; int t; if (zr_value(r, key, value, &t, b, 8) < 8) return -1; *o = (u64)RD32(b) | (u64)RD32(b + 4) << 32; return 0; }
