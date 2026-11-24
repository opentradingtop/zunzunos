/* Lector NTFS propio (solo lectura), para Forense: abrir archivos y carpetas por su ruta.
   - Registros del MFT con sus correcciones (fixups); el $MFT puede estar fragmentado.
   - Atributos residentes y no residentes (listas de trozos, trozos dispersos), y $ATTRIBUTE_LIST
     para los archivos cuyos trozos no caben en un registro.
   - Carpetas: arbol de indices $I30 ($INDEX_ROOT + bloques INDX de $INDEX_ALLOCATION), con la tabla
     $UpCase del propio volumen para comparar nombres sin distinguir mayusculas.
   Pensado para discos danados: comprueba todos los limites y nunca sigue un dato imposible. */
#include "z.h"
#include "zntfs.h"

typedef struct { u64 vcn, len; i64 lcn; } run_t;                  /* lcn -1: trozo disperso (ceros) */
typedef struct { int res; u8 *val; u32 vlen; run_t *r; int nr; u64 size, init; int comp; } attr_t;
struct zntfs { zn_rd rd; void *ctx; u64 lba0, nclus; u32 cs, rs, ibs; attr_t mft; u16 *up; u8 *rec, *rec2; };

#define RD16(p) ((u16)((p)[0] | (p)[1] << 8))
#define RD32(p) ((u32)((p)[0] | (p)[1] << 8 | (p)[2] << 16 | (u32)(p)[3] << 24))
#define RD64(p) ((u64)RD32(p) | (u64)RD32((p) + 4) << 32)

static void attr_free(attr_t *a) { free(a->val); free(a->r); memset(a, 0, sizeof *a); }

/* bytes de la particion (desplazamiento en bytes) */
static int rd_bytes(zntfs *z, u64 off, void *buf, u32 len) {
    static u8 sec[4096]; u8 *o = buf;
    while (len) {
        u64 lba = off / 512; u32 in = off % 512;
        if (!in && len >= 512) { u32 n = len / 512; if (n > 64) n = 64; if (z->rd(z->ctx, z->lba0 + lba, n, o)) return -5; off += n * 512; o += n * 512; len -= n * 512; continue; }
        if (z->rd(z->ctx, z->lba0 + lba, 1, sec)) return -5;
        u32 k = 512 - in; if (k > len) k = len; memcpy(o, sec + in, k); off += k; o += k; len -= k; }
    return 0; }

static int do_fixup(u8 *b, u32 size) {
    u32 uo = RD16(b + 4), un = RD16(b + 6);
    if (uo + un * 2 > size || un < 1 || (un - 1) * 512 > size) return -1;
    for (u32 i = 1; i < un; i++) { u8 *e = b + i * 512 - 2; if (e[0] != b[uo] || e[1] != b[uo + 1]) return -1; e[0] = b[uo + i * 2]; e[1] = b[uo + i * 2 + 1]; }
    return 0; }

/* lista de trozos de un atributo no residente; se anade a la que ya haya (extensiones de $ATTRIBUTE_LIST) */
static int decode_runs(const u8 *a, u32 alen, attr_t *at) {
    u32 ro = RD16(a + 32); if (ro >= alen) return -1;
    u64 vcn = RD64(a + 16); i64 lcn = 0; const u8 *p = a + ro, *end = a + alen;
    while (p < end && *p) {
        int nl = *p & 15, no = *p >> 4; p++;
        if (!nl || nl > 8 || no > 8 || p + nl + no > end) return -1;
        u64 len = 0; for (int i = 0; i < nl; i++) len |= (u64)p[i] << (8 * i); p += nl;
        i64 d = 0; if (no) { for (int i = 0; i < no; i++) d |= (i64)p[i] << (8 * i); if (p[no - 1] & 0x80) d -= (i64)1 << (8 * no); p += no; }
        if (!len || len > (1ULL << 40)) return -1;
        run_t *nr = realloc(at->r, (at->nr + 1) * sizeof(run_t)); if (!nr) return -12; at->r = nr;
        if (no) lcn += d;
        at->r[at->nr].vcn = vcn; at->r[at->nr].len = len; at->r[at->nr].lcn = no ? lcn : -1; at->nr++; vcn += len; }
    return 0; }

/* registro n del MFT, ya corregido; 0 si va bien */
static int read_rec(zntfs *z, u64 n, u8 *out) {
    if (!z->mft.nr) {                                         /* arrancando: el MFT empieza donde dice el sector de arranque */
        if (rd_bytes(z, z->mft.size + n * z->rs, out, z->rs)) return -5; }
    else {
        u64 off = n * z->rs; u32 done = 0;
        while (done < z->rs) {
            u64 vcn = (off + done) / z->cs; u32 in = (off + done) % z->cs; u32 k = z->cs - in; if (k > z->rs - done) k = z->rs - done;
            run_t *r = 0; for (int i = 0; i < z->mft.nr; i++) if (vcn >= z->mft.r[i].vcn && vcn < z->mft.r[i].vcn + z->mft.r[i].len) { r = &z->mft.r[i]; break; }
            if (!r || r->lcn < 0) return -2;
            if (rd_bytes(z, (u64)(r->lcn + (vcn - r->vcn)) * z->cs + in, out + done, k)) return -5;
            done += k; } }
    if (memcmp(out, "FILE", 4) || do_fixup(out, z->rs)) return -2;
    return 0; }

/* recorre los atributos de un registro: devuelve el siguiente atributo valido despues de p (o el primero si p es 0) */
static u8 *next_attr(zntfs *z, u8 *rec, u8 *p) {
    u32 used = RD32(rec + 24); if (used > z->rs) used = z->rs;
    u32 o = p ? (u32)(p - rec) + RD32(p + 4) : RD16(rec + 20);
    if (o < 24 || o + 16 > used) return 0;
    u8 *a = rec + o; u32 t = RD32(a), l = RD32(a + 4);
    if (t == 0xFFFFFFFF || l < 16 || o + l > used) return 0;
    return a; }

/* valor de un atributo residente, solo si cabe entero dentro del atributo (y este dentro del registro) */
static u8 *resv(u8 *a, u32 need) { u32 l = RD32(a + 4), vl = RD32(a + 16), vo = RD16(a + 20); if (a[8] || vo > l || vl > l - vo || vl < need) return 0; return a + vo; }
static int name_is(const u8 *a, const char *name) {
    int nl = a[9]; const u8 *nm = a + RD16(a + 10); int k = name ? (int)strlen(name) : 0;
    if (nl != k || RD16(a + 10) + nl * 2 > RD32(a + 4)) return 0; for (int i = 0; i < k; i++) if (RD16(nm + 2 * i) != (u8)name[i]) return 0; return 1; }

static int take_attr(zntfs *z, const u8 *a, attr_t *at, int first) {
    u32 l = RD32(a + 4);
    if (!a[8]) { u32 vl = RD32(a + 16), vo = RD16(a + 20); if (vo + vl > l) return -1; at->res = 1; at->val = malloc(vl + 1); if (!at->val) return -12; memcpy(at->val, a + vo, vl); at->vlen = vl; at->size = at->init = vl; return 0; }
    if (l < 64) return -1;
    if (first) { at->size = RD64(a + 48); at->init = RD64(a + 56); at->comp = RD16(a + 12) & 0x00FF; }
    return decode_runs(a, l, at); }

static int cmp_run(const void *x, const void *y) { const run_t *a = x, *b = y; return a->vcn < b->vcn ? -1 : a->vcn > b->vcn; }

/* atributo de tipo t y nombre name (0: sin nombre) del registro n, juntando sus extensiones */
static int get_attr(zntfs *z, u64 n, u32 t, const char *name, attr_t *at) {
    memset(at, 0, sizeof *at); u8 *rec = z->rec;
    if (read_rec(z, n, rec)) return -2;
    u8 *a = 0, *list = 0; int found = 0;
    while ((a = next_attr(z, rec, a))) {
        if (RD32(a) == 0x20) list = a;
        if (RD32(a) == t && name_is(a, name) && (a[8] ? RD64(a + 16) == 0 : 1)) { if (take_attr(z, a, at, 1)) { attr_free(at); return -1; } found = 1; break; } }
    if (!list) return found ? 0 : -2;
    /* $ATTRIBUTE_LIST: las demas piezas del atributo estan en otros registros */
    attr_t L; memset(&L, 0, sizeof L); if (take_attr(z, list, &L, 1)) { attr_free(&L); return found ? 0 : -2; }
    u8 *lb = L.val; u32 ll = L.vlen;
    if (!L.res) { ll = L.size > 65536 ? 65536 : (u32)L.size; lb = malloc(ll); if (!lb || zn_attr_read_raw(z, &L, 0, lb, ll) != (long)ll) { free(lb); attr_free(&L); return found ? 0 : -2; } }
    u8 *rec2 = z->rec2;
    for (u32 o = 0; o + 26 <= ll; ) {
        u8 *e = lb + o; u32 el = RD16(e + 4); if (el < 26 || o + el > ll) break;
        u64 ref = RD64(e + 16) & 0xFFFFFFFFFFFFULL, svcn = RD64(e + 8);
        int match = RD32(e) == t; if (match) { int nl = e[6]; const char *nm = name ? name : ""; if (nl != (int)strlen(nm)) match = 0; for (int i = 0; match && i < nl; i++) if (RD16(e + e[7] + 2 * i) != (u8)nm[i]) match = 0; }
        if (match && ref != n && !read_rec(z, ref, rec2)) {
            u8 *b = 0; while ((b = next_attr(z, rec2, b))) if (RD32(b) == t && name_is(b, name) && b[8] && RD64(b + 16) == svcn) { take_attr(z, b, at, !found); found = 1; break; } }
        o += el; }
    if (lb != L.val) free(lb); attr_free(&L);
    if (at->nr > 1) qsort(at->r, at->nr, sizeof(run_t), cmp_run);
    return found ? 0 : -2; }

/* lee len bytes de los datos de un atributo desde off; devuelve los leidos o <0 */
long zn_attr_read_raw(zntfs *z, void *av, u64 off, void *buf, u32 len) {
    attr_t *a = av; u8 *o = buf;
    if (off >= a->size) return 0; if (off + len > a->size) len = (u32)(a->size - off);
    if (a->res) { memcpy(o, a->val + off, len); return len; }
    if (a->comp) return -95;                                   /* comprimido: no se lee (aun) */
    u32 done = 0;
    while (done < len) {
        u64 p = off + done, vcn = p / z->cs; u32 in = p % z->cs, k = z->cs - in; if (k > len - done) k = len - done;
        run_t *r = 0; for (int i = 0; i < a->nr; i++) if (vcn >= a->r[i].vcn && vcn < a->r[i].vcn + a->r[i].len) { r = &a->r[i]; break; }
        if (p >= a->init || !r || r->lcn < 0) memset(o + done, 0, k);
        else { u64 run_left = (r->vcn + r->len - vcn) * z->cs - in; if (k < run_left && len - done > k) { k = len - done < run_left ? len - done : (u32)run_left; }
            if ((u64)r->lcn + (vcn - r->vcn) >= z->nclus) return -5;
            if (rd_bytes(z, (u64)(r->lcn + (vcn - r->vcn)) * z->cs + in, o + done, k)) return -5;
            if (p + k > a->init) memset(o + done + (a->init - p), 0, p + k - a->init); }
        done += k; }
    return done; }

zntfs *zn_open(zn_rd rd, void *ctx, u64 lba0) {
    u8 b[512]; if (rd(ctx, lba0, 1, b) || memcmp(b + 3, "NTFS    ", 8)) return 0;
    u32 bps = RD16(b + 11), spc = b[13]; if (bps < 256 || bps > 4096 || !spc || (spc & (spc - 1))) return 0;
    zntfs *z = calloc(1, sizeof *z); if (!z) return 0;
    z->rd = rd; z->ctx = ctx; z->lba0 = lba0; z->cs = bps * spc; z->nclus = RD64(b + 40) / spc;
    signed char c = b[64]; z->rs = c > 0 ? c * z->cs : 1u << -c; c = b[68]; z->ibs = c > 0 ? c * z->cs : 1u << -c;
    if (z->rs < 512 || z->rs > 65536) { free(z); return 0; }
    z->rec = malloc(z->rs); z->rec2 = malloc(z->rs);
    z->mft.size = RD64(b + 48) * z->cs;                         /* provisional: donde empieza el MFT */
    attr_t m; if (!z->rec || !z->rec2 || get_attr(z, 0, 0x80, 0, &m) || m.res) { zn_close(z); return 0; }
    z->mft = m;
    attr_t u; if (!get_attr(z, 10, 0x80, 0, &u) && u.size >= 131072) { z->up = malloc(131072); if (z->up && zn_attr_read_raw(z, &u, 0, z->up, 131072) != 131072) { free(z->up); z->up = 0; } }
    attr_free(&u);
    return z; }

void zn_close(zntfs *z) { if (!z) return; attr_free(&z->mft); free(z->up); free(z->rec); free(z->rec2); free(z); }

static u16 upc(zntfs *z, u16 c) { if (z->up) return z->up[c]; return c >= 'a' && c <= 'z' ? c - 32 : c; }
static int cmp_name(zntfs *z, const u16 *a, int na, const u8 *b, int nb) {
    for (int i = 0; i < na && i < nb; i++) { u16 x = upc(z, a[i]), y = upc(z, RD16(b + 2 * i)); if (x != y) return x < y ? -1 : 1; }
    return na < nb ? -1 : na > nb; }

/* UTF-8 <-> UTF-16 */
static int u8_to_16(const char *s, int n, u16 *o, int cap) {
    int k = 0; for (int i = 0; i < n && k < cap; ) { u32 c = (u8)s[i]; int l = 1;
        if (c >= 0xF0 && i + 3 < n) { c = (c & 7) << 18 | (s[i + 1] & 63) << 12 | (s[i + 2] & 63) << 6 | (s[i + 3] & 63); l = 4; }
        else if (c >= 0xE0 && i + 2 < n) { c = (c & 15) << 12 | (s[i + 1] & 63) << 6 | (s[i + 2] & 63); l = 3; }
        else if (c >= 0xC0 && i + 1 < n) { c = (c & 31) << 6 | (s[i + 1] & 63); l = 2; }
        if (c >= 0x10000) { if (k + 1 >= cap) break; c -= 0x10000; o[k++] = 0xD800 | c >> 10; o[k++] = 0xDC00 | (c & 1023); } else o[k++] = c; i += l; }
    return k; }
void zn_utf8(const u8 *w, int n, char *o, int cap) {
    int k = 0; for (int i = 0; i < n; i++) { u32 c = RD16(w + 2 * i);
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < n) { u32 d = RD16(w + 2 * i + 2); if (d >= 0xDC00 && d < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00); i++; } }
        char t[4]; int l = c < 0x80 ? (t[0] = c, 1) : c < 0x800 ? (t[0] = 0xC0 | c >> 6, t[1] = 0x80 | (c & 63), 2) : c < 0x10000 ? (t[0] = 0xE0 | c >> 12, t[1] = 0x80 | (c >> 6 & 63), t[2] = 0x80 | (c & 63), 3) : (t[0] = 0xF0 | c >> 18, t[1] = 0x80 | (c >> 12 & 63), t[2] = 0x80 | (c >> 6 & 63), t[3] = 0x80 | (c & 63), 4);
        if (k + l >= cap) break; memcpy(o + k, t, l); k += l; }
    o[k] = 0; }

/* indice de una carpeta: raiz + bloques; cb por cada entrada (en orden) o busqueda de un nombre */
typedef struct { attr_t root, alloc; u8 *blk; int ok; } dir_t;
static int dir_open(zntfs *z, u64 n, dir_t *d) {
    memset(d, 0, sizeof *d);
    if (get_attr(z, n, 0x90, "$I30", &d->root) || !d->root.res || d->root.vlen < 32) { attr_free(&d->root); return -20; }
    get_attr(z, n, 0xA0, "$I30", &d->alloc);
    d->blk = malloc(z->ibs); d->ok = d->blk != 0; return d->ok ? 0 : -12; }
static void dir_close(dir_t *d) { attr_free(&d->root); attr_free(&d->alloc); free(d->blk); }
/* entradas de un nodo: devuelve puntero y longitud (raiz si vcn < 0) */
static u8 *node(zntfs *z, dir_t *d, i64 vcn, u32 *len) {
    if (vcn < 0) { u8 *h = d->root.val + 16; u32 eo = RD32(h), tot = RD32(h + 4); if (16 + tot > d->root.vlen || eo > tot) return 0; *len = tot - eo; return h + eo; }
    if (!d->alloc.nr && !d->alloc.res) return 0;
    u64 unit = z->ibs >= z->cs ? z->cs : 512;
    if (zn_attr_read_raw(z, &d->alloc, (u64)vcn * unit, d->blk, z->ibs) != (long)z->ibs) return 0;
    if (memcmp(d->blk, "INDX", 4) || do_fixup(d->blk, z->ibs)) return 0;
    u8 *h = d->blk + 24; u32 eo = RD32(h), tot = RD32(h + 4); if (24 + tot > z->ibs || eo > tot) return 0;
    *len = tot - eo; return h + eo; }

/* busca name en la carpeta n: numero de registro o -1 */
static i64 dir_find(zntfs *z, u64 n, const u16 *nm, int nl) {
    dir_t d; if (dir_open(z, n, &d)) return -1; i64 vcn = -1, res = -1;
    for (int depth = 0; depth < 32; depth++) {
        u32 len; u8 *e = node(z, &d, vcn, &len); if (!e) break; u8 *end = e + len; i64 next = -2;
        while (e + 16 <= end) {
            u32 el = RD16(e + 8), kl = RD16(e + 10), fl = RD32(e + 12); if (el < 16 || e + el > end) break;
            if (fl & 2) { if (fl & 1) next = RD64(e + el - 8); break; }
            if (kl >= 66 && 66 + e[16 + 64] * 2 <= kl) {
                int c = cmp_name(z, nm, nl, e + 16 + 66, e[16 + 64]);
                if (!c) { res = RD64(e) & 0xFFFFFFFFFFFFULL; break; }
                if (c < 0) { if (fl & 1) next = RD64(e + el - 8); break; } }
            e += el; }
        if (res >= 0 || next < 0) break; vcn = next; }
    dir_close(&d); return res; }

i64 zn_lookup(zntfs *z, const char *path) {
    u64 n = 5; const char *p = path;
    while (*p) {
        while (*p == '/' || *p == '\\') p++; if (!*p) break;
        const char *q = p; while (*q && *q != '/' && *q != '\\') q++;
        if (q - p == 2 && p[0] == 'C' && p[1] == ':') { p = q; continue; }   /* "C:\..." */
        u16 w[256]; int k = u8_to_16(p, q - p, w, 256); i64 r = dir_find(z, n, w, k); if (r < 0) return -1; n = r; p = q; }
    return n; }

/* datos del registro: tamano, fechas, si es carpeta */
static int rec_info(zntfs *z, u64 n, zn_ent *e) {
    memset(e, 0, sizeof *e); e->ref = n; u8 *rec = z->rec; if (read_rec(z, n, rec)) return -2;
    e->dir = RD16(rec + 22) & 2; u8 *a = 0; int ns = -1;
    while ((a = next_attr(z, rec, a))) {
        u32 t = RD32(a);
        u8 *v;
        if (t == 0x10 && (v = resv(a, 36))) { e->ctime = RD64(v); e->mtime = RD64(v + 8); e->atime = RD64(v + 24); e->attrs = RD32(v + 32); }
        if (t == 0x30 && (v = resv(a, 66)) && 66 + v[64] * 2 <= RD32(a + 16)) { int fns = v[65]; if (ns < 0 || (ns == 2 && fns != 2)) { ns = fns; zn_utf8(v + 66, v[64], e->name, sizeof e->name); } }
        if (t == 0x80 && !a[9]) e->size = a[8] ? (RD32(a + 4) >= 64 ? RD64(a + 48) : 0) : RD32(a + 16) <= RD32(a + 4) ? RD32(a + 16) : 0; }
    if (e->dir) e->size = 0;
    else if (!e->size) { attr_t d; if (!get_attr(z, n, 0x80, 0, &d)) { e->size = d.size; attr_free(&d); } }   /* $DATA en un registro de extension */
    return 0; }

int zn_stat(zntfs *z, const char *path, zn_ent *e) { i64 n = zn_lookup(z, path); if (n < 0) return -2; return rec_info(z, n, e); }

/* lee hasta cap bytes del archivo (desde off); devuelve los leidos o <0 */
long zn_read(zntfs *z, const char *path, u64 off, void *buf, long cap) {
    i64 n = zn_lookup(z, path); if (n < 0) return -2;
    attr_t a; if (get_attr(z, n, 0x80, 0, &a)) return -2;
    long r = cap > 0x7FFFFFFF ? -22 : zn_attr_read_raw(z, &a, off, buf, (u32)cap); attr_free(&a); return r; }

u8 *zn_readall(zntfs *z, const char *path, long max, long *len) {
    i64 n = zn_lookup(z, path); *len = 0; if (n < 0) return 0;
    attr_t a; if (get_attr(z, n, 0x80, 0, &a)) return 0;
    long sz = a.size > (u64)max ? max : (long)a.size; u8 *b = malloc(sz + 1);
    if (b && zn_attr_read_raw(z, &a, 0, b, sz) != sz) { free(b); b = 0; }
    if (b) { b[sz] = 0; *len = sz; } attr_free(&a); return b; }

/* recorre la carpeta en orden; los nombres cortos (8.3) se saltan */
static int walk(zntfs *z, dir_t *d, i64 vcn, int depth, int (*cb)(const zn_ent *, void *), void *ctx, int *count) {
    if (depth > 32) return 0; u32 len; u8 *e0 = node(z, d, vcn, &len); if (!e0) return 0;
    u8 *copy = malloc(len); if (!copy) return -12; memcpy(copy, e0, len); u8 *e = copy, *end = copy + len; int r = 0;
    while (e + 16 <= end && !r) {
        u32 el = RD16(e + 8), kl = RD16(e + 10), fl = RD32(e + 12); if (el < 16 || e + el > end) break;
        if (fl & 1) r = walk(z, d, RD64(e + el - 8), depth + 1, cb, ctx, count);
        if (r || (fl & 2)) break;
        if (kl >= 66 && 66 + e[16 + 64] * 2 <= kl && e[16 + 65] != 2) {
            u8 *f = e + 16; zn_ent x; memset(&x, 0, sizeof x); x.ref = RD64(e) & 0xFFFFFFFFFFFFULL; x.dir = (RD32(f + 56) & 0x10000000) != 0;
            x.ctime = RD64(f + 8); x.mtime = RD64(f + 16); x.atime = RD64(f + 32); x.size = x.dir ? 0 : RD64(f + 48); x.attrs = RD32(f + 56);
            zn_utf8(f + 66, f[64], x.name, sizeof x.name); (*count)++; r = cb(&x, ctx); }
        e += el; }
    free(copy); return r; }

int zn_list(zntfs *z, const char *path, int (*cb)(const zn_ent *, void *), void *ctx) {
    i64 n = zn_lookup(z, path); if (n < 0) return -2;
    dir_t d; if (dir_open(z, n, &d)) return -20; int count = 0, r = walk(z, &d, -1, 0, cb, ctx, &count); dir_close(&d); return r < 0 ? r : count; }

int zn_volume(zntfs *z, char *label, int cap, int *major, int *minor, int *flags) {
    attr_t a; label[0] = 0; *major = *minor = *flags = 0;
    if (!get_attr(z, 3, 0x60, 0, &a)) { if (a.res) zn_utf8(a.val, a.vlen / 2, label, cap); attr_free(&a); }
    if (get_attr(z, 3, 0x70, 0, &a)) return -2;
    if (a.res && a.vlen >= 12) { *major = a.val[8]; *minor = a.val[9]; *flags = RD16(a.val + 10); }
    attr_free(&a); return 0; }

u64 zn_clusters(zntfs *z, u32 *csize) { *csize = z->cs; return z->nclus; }
