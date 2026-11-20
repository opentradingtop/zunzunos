/* exFAT con lectura y escritura: archivos, carpetas, borrar y renombrar.
   Mapa de bits de asignacion, cadena FAT o NoFatChain, hash de nombre con la tabla de mayusculas y suma del grupo de entradas. */
#include "kernel.h"
extern void vfs_set_size(int, u64, u64);
typedef struct { blkdev_t *d; u32 bps, spc, fat, heap, count, root; u64 csz; u8 *bm; u32 bmcl; int bmnc; u64 bmlen; u16 *up; u32 hint; u64 nfree; mount_t *m; } xf_t;
/* entrada resuelta: su posicion en el directorio que la contiene y sus datos */
typedef struct { u32 pfirst; int pnc; u64 psize; int proot; u64 epos; int nsec; u8 set[20 * 32]; int dir, root; u32 first; int nc; u64 size; char name[256]; } xe_t;
#define EOC 0xFFFFFFFFu
static int xrd(xf_t *f, u64 off, void *buf, u64 n) {
    u32 ss = f->d->ssize; u8 *p = buf, *t = 0;
    while (n) { u64 l = off / ss; u32 o = off % ss;
        if (o || n < ss) { if (!t) t = kmalloc(ss); if (blk_read(f->d, l, 1, t)) { kfree(t); return E_IO; } u32 c = MIN(ss - o, n); memcpy(p, t + o, c); p += c; off += c; n -= c; }
        else { u64 c = MIN(n / ss, 2048UL); if (blk_read(f->d, l, c, p)) { kfree(t); return E_IO; } p += c * ss; off += c * ss; n -= c * ss; } }
    kfree(t); return 0; }
static int xwr(xf_t *f, u64 off, const void *buf, u64 n) {
    u32 ss = f->d->ssize; const u8 *p = buf; u8 *t = 0;
    while (n) { u64 l = off / ss; u32 o = off % ss;
        if (o || n < ss) { if (!t) t = kmalloc(ss); if (blk_read(f->d, l, 1, t)) { kfree(t); return E_IO; } u32 c = MIN(ss - o, n); memcpy(t + o, p, c); if (blk_write(f->d, l, 1, t)) { kfree(t); return E_IO; } p += c; off += c; n -= c; }
        else { u64 c = MIN(n / ss, 2048UL); if (blk_write(f->d, l, c, p)) { kfree(t); return E_IO; } p += c * ss; off += c * ss; n -= c * ss; } }
    kfree(t); return 0; }
static u64 cl_off(xf_t *f, u32 c) { return ((u64)f->heap << f->bps) + (u64)(c - 2) * f->csz; }
static u32 fat_get(xf_t *f, u32 c) { u32 v; if (xrd(f, ((u64)f->fat << f->bps) + c * 4UL, &v, 4)) return EOC; return v; }
static int fat_set(xf_t *f, u32 c, u32 v) { return xwr(f, ((u64)f->fat << f->bps) + c * 4UL, &v, 4); }
/* cluster numero idx de una cadena */
static u32 nth(xf_t *f, u32 first, int nc, u64 idx) { if (nc) return first + idx; u32 c = first; for (u64 i = 0; i < idx; i++) { c = fat_get(f, c); if (c < 2 || c >= 0xFFFFFFF7u) return 0; } return c; }
/* ---- mapa de bits ---- */
static int bm_get(xf_t *f, u32 c) { if (!f->bm) return 1; u32 i = c - 2; return f->bm[i / 8] >> (i % 8) & 1; }
static void bm_put(xf_t *f, u32 c, int v) { if (!f->bm) return; u32 i = c - 2; if (v) f->bm[i / 8] |= 1 << (i % 8); else f->bm[i / 8] &= ~(1 << (i % 8)); u64 bi = i / 8; u32 cl = nth(f, f->bmcl, f->bmnc, bi / f->csz); if (cl) xwr(f, cl_off(f, cl) + bi % f->csz, &f->bm[bi], 1); if (v) f->nfree--; else f->nfree++; if (f->m) f->m->free_b = f->nfree * f->csz; }
static u32 alloc1(xf_t *f, u32 want) { if (want >= 2 && want < f->count + 2 && !bm_get(f, want)) { bm_put(f, want, 1); return want; } for (u32 k = 0; k < f->count; k++) { u32 c = 2 + (f->hint + k) % f->count; if (!bm_get(f, c)) { bm_put(f, c, 1); f->hint = c - 1; return c; } } return 0; }
static void free_chain(xf_t *f, u32 first, int nc, u64 size) { if (first < 2) return; u64 n = (size + f->csz - 1) / f->csz; u32 c = first; for (u64 i = 0; i < n && c >= 2 && c < f->count + 2; i++) { u32 nx = nc ? c + 1 : fat_get(f, c); if (bm_get(f, c)) bm_put(f, c, 0); if (!nc) fat_set(f, c, 0); c = nx; } }
static int zero_cl(xf_t *f, u32 c) { u8 *z = kzalloc(f->csz); int e = xwr(f, cl_off(f, c), z, f->csz); kfree(z); return e; }
/* ---- nombres ---- */
static u16 upc(xf_t *f, u16 c) { if (f->up) return f->up[c]; if (c >= 'a' && c <= 'z') return c - 32; if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32; return c; }
static int to16(const char *s, u16 *o) { int n = 0, c; while (n < 255 && (c = utf8_decode(&s))) o[n++] = c > 0xFFFF ? '?' : c; return n; }
static u16 namehash(xf_t *f, const u16 *n, int l) { u16 h = 0; for (int i = 0; i < l; i++) { u16 c = upc(f, n[i]); h = ((h & 1) ? 0x8000 : 0) + (h >> 1) + (c & 0xFF); h = ((h & 1) ? 0x8000 : 0) + (h >> 1) + (c >> 8); } return h; }
static u16 setsum(const u8 *s, int nsec) { u16 h = 0; for (int i = 0; i < (nsec + 1) * 32; i++) { if (i == 2 || i == 3) continue; h = ((h & 1) ? 0x8000 : 0) + (h >> 1) + s[i]; } return h; }
static u32 now_ts(void) { u32 r[6]; rtc_read(r); return (r[5] - 1980) << 25 | r[4] << 21 | r[3] << 16 | r[2] << 11 | r[1] << 5 | r[0] / 2; }
/* ---- directorios ---- */
static int dpos(xf_t *f, u32 first, int nc, u64 p, u64 *abs) { u32 c = nth(f, first, nc, p / f->csz); if (!c) return E_IO; *abs = cl_off(f, c) + p % f->csz; return 0; }
static void dir_of(const xe_t *e, u32 *first, int *nc, u64 *size, int *root) { *first = e->first; *nc = e->nc; *size = e->size; *root = e->root; }
static int dir_limit(xf_t *f, u32 first, int nc, u64 size, int root, u64 *lim) { if (!root) { *lim = size; return 0; } u64 n = 1; u32 c = first; while (n < 1 << 20) { c = fat_get(f, c); if (c < 2 || c >= 0xFFFFFFF7u) break; n++; } *lim = n * f->csz; return 0; }
/* lee el grupo de entradas en la posicion p; devuelve 1 si es un archivo valido */
static int read_set(xf_t *f, u32 df, int dnc, u64 p, u64 lim, xe_t *e) {
    u64 a; if (dpos(f, df, dnc, p, &a) || xrd(f, a, e->set, 32)) return -1; if (e->set[0] == 0) return -1; if (e->set[0] != 0x85) return 0;
    int n = e->set[1]; if (n < 2 || n > 18 || p + (n + 1) * 32 > lim) return 0; for (int i = 1; i <= n; i++) { if (dpos(f, df, dnc, p + i * 32, &a) || xrd(f, a, e->set + i * 32, 32)) return 0; } if (e->set[32] != 0xC0) return 0;
    e->nsec = n; e->epos = p; e->dir = *(u16 *)(e->set + 4) & 0x10; e->nc = e->set[33] & 2; e->first = *(u32 *)(e->set + 52); e->size = *(u64 *)(e->set + 56); e->root = 0; int nl = e->set[35], k = 0;
    for (int i = 2; i <= n && nl > 0; i++) { u8 *x = e->set + i * 32; if (x[0] != 0xC1) break; for (int j = 0; j < 15 && nl > 0 && k < 250; j++, nl--) k += utf8_encode(*(u16 *)(x + 2 + j * 2), e->name + k); } e->name[k] = 0; return 1; }
static int name_eq(xf_t *f, const char *a, const char *b) { u16 x[256], y[256]; int n = to16(a, x), m = to16(b, y); if (n != m) return 0; for (int i = 0; i < n; i++) if (upc(f, x[i]) != upc(f, y[i])) return 0; return 1; }
static int lookup(xf_t *f, const xe_t *d, const char *name, xe_t *o) { u32 df; int dnc, root; u64 ds, lim; dir_of(d, &df, &dnc, &ds, &root); dir_limit(f, df, dnc, ds, root, &lim);
    for (u64 p = 0; p < lim; p += 32) { int r = read_set(f, df, dnc, p, lim, o); if (r < 0) break; if (r == 1) { if (name_eq(f, o->name, name)) { o->pfirst = df; o->pnc = dnc; o->psize = ds; o->proot = root; return 0; } p += o->nsec * 32; } } return E_NOENT; }
static void root_e(xf_t *f, xe_t *e) { memset(e, 0, sizeof *e); e->dir = 1; e->root = 1; e->first = f->root; e->nc = 0; }
static int walk(xf_t *f, const char *p, xe_t *e) { root_e(f, e); char part[256]; while (*p) { while (*p == '/') p++; if (!*p) break; int i = 0; while (*p && *p != '/' && i < 255) part[i++] = *p++; part[i] = 0; if (!e->dir) return E_NOTDIR; xe_t d = *e; if (lookup(f, &d, part, e)) return E_NOENT; } return 0; }
static int split(const char *p, char *par, char *nm) { const char *s = strrchr(p, '/'); if (!s) { par[0] = 0; strncpy0(nm, p, 256); } else { int l = s - p; memcpy(par, p, l); par[l] = 0; strncpy0(nm, s + 1, 256); } return nm[0] ? 0 : E_INVAL; }
static int write_set(xf_t *f, xe_t *e) { *(u16 *)(e->set + 2) = setsum(e->set, e->nsec); for (int i = 0; i <= e->nsec; i++) { u64 a; if (dpos(f, e->pfirst, e->pnc, e->epos + i * 32, &a) || xwr(f, a, e->set + i * 32, 32)) return E_IO; } return 0; }
static void stream_put(xe_t *e) { e->set[33] = 1 | (e->nc ? 2 : 0); *(u64 *)(e->set + 40) = e->size; *(u32 *)(e->set + 52) = e->first; *(u64 *)(e->set + 56) = e->size; }
/* asegura que la cadena (first, nc) tenga al menos n clusters; puede convertir NoFatChain en cadena */
static int grow(xf_t *f, u32 *first, int *nc, u64 have, u64 need, int zero) {
    u32 last = have ? nth(f, *first, *nc, have - 1) : 0;
    for (u64 k = have; k < need; k++) {
        u32 c = alloc1(f, last ? last + 1 : f->hint + 2); if (!c) return E_NOSPC; if (zero) zero_cl(f, c);
        if (!last) { *first = c; *nc = 1; }
        else if (*nc && c == last + 1) {}
        else { if (*nc) { for (u64 i = 0; i + 1 < k; i++) fat_set(f, *first + i, *first + i + 1); *nc = 0; } fat_set(f, last, c); }
        if (!*nc) fat_set(f, c, EOC); last = c; }
    return 0; }
/* busca n entradas libres seguidas en el directorio d (ampliandolo si hace falta) */
static int find_slot(xf_t *f, xe_t *d, int n, u64 *pos) {
    u32 df; int dnc, root; u64 ds, lim; dir_of(d, &df, &dnc, &ds, &root); dir_limit(f, df, dnc, ds, root, &lim); u64 run = 0, st = 0; u8 t;
    for (u64 p = 0; p < lim; p += 32) { u64 a; if (dpos(f, df, dnc, p, &a) || xrd(f, a, &t, 1)) return E_IO; if (t < 0x80) { if (!run) st = p; if (++run >= (u64)n) { *pos = st; return 0; } } else run = 0; }
    u64 have = lim / f->csz; int e = grow(f, &d->first, &d->nc, have, have + 1, 1); if (e) return e;
    if (!root) { d->size += f->csz; stream_put(d); *(u32 *)(d->set + 12) = now_ts(); write_set(f, d); }
    *pos = run ? st : lim; return 0; }
static int create(xf_t *f, const char *path, int dir, xe_t *o) {
    char par[512], nm[256]; if (split(path, par, nm)) return E_INVAL; xe_t d; int e = walk(f, par, &d); if (e) return e; if (!d.dir) return E_NOTDIR; if (!lookup(f, &d, nm, o)) return E_EXIST;
    u16 n16[256]; int nl = to16(nm, n16); if (!nl) return E_INVAL; int nn = (nl + 14) / 15; u64 pos; if ((e = find_slot(f, &d, 2 + nn, &pos))) return e;
    memset(o, 0, sizeof *o); o->nsec = 1 + nn; o->epos = pos; o->pfirst = d.first; o->pnc = d.nc; o->psize = d.size; o->proot = d.root; o->dir = dir; strncpy0(o->name, nm, 256);
    u8 *s = o->set; s[0] = 0x85; s[1] = o->nsec; *(u16 *)(s + 4) = dir ? 0x10 : 0x20; u32 ts = now_ts(); *(u32 *)(s + 8) = ts; *(u32 *)(s + 12) = ts; *(u32 *)(s + 16) = ts;
    s[32] = 0xC0; s[35] = nl; *(u16 *)(s + 36) = namehash(f, n16, nl);
    for (int i = 0; i < nn; i++) { u8 *x = s + (2 + i) * 32; x[0] = 0xC1; for (int j = 0; j < 15; j++) { int k = i * 15 + j; *(u16 *)(x + 2 + j * 2) = k < nl ? n16[k] : 0; } }
    if (dir) { if ((e = grow(f, &o->first, &o->nc, 0, 1, 1))) return e; o->size = f->csz; }
    stream_put(o); return write_set(f, o); }
static void del_set(xf_t *f, xe_t *e) { for (int i = 0; i <= e->nsec; i++) e->set[i * 32] &= 0x7F; for (int i = 0; i <= e->nsec; i++) { u64 a; if (!dpos(f, e->pfirst, e->pnc, e->epos + i * 32, &a)) xwr(f, a, e->set + i * 32, 1); } }
static int dir_empty(xf_t *f, xe_t *d) { u64 lim; dir_limit(f, d->first, d->nc, d->size, 0, &lim); xe_t x; for (u64 p = 0; p < lim; p += 32) { int r = read_set(f, d->first, d->nc, p, lim, &x); if (r < 0) break; if (r == 1) return 0; } return 1; }
/* ---- operaciones de la VFS ---- */
static int xo_open(mount_t *m, const char *p, int fl, file_t *fi) {
    xf_t *f = m->priv; xe_t *e = kmalloc(sizeof *e); int r = walk(f, p, e);
    if (r == E_NOENT && (fl & O_CREATE)) r = create(f, p, 0, e);
    if (!r && e->dir) r = E_ISDIR;
    if (!r && (fl & O_TRUNC) && (fl & O_WRITE) && e->size) { free_chain(f, e->first, e->nc, e->size); e->first = 0; e->nc = 0; e->size = 0; stream_put(e); *(u32 *)(e->set + 12) = now_ts(); r = write_set(f, e); }
    if (r) { kfree(e); return r; } fi->priv = e; fi->size = e->size; return 0; }
static i64 xo_read(file_t *fi, void *b, u64 n) { xf_t *f = fi->m->priv; xe_t *e = fi->priv; u8 *p = b; u64 pos = fi->pos, left = n;
    while (left) { u32 c = nth(f, e->first, e->nc, pos / f->csz); if (!c) return E_IO; u64 o = pos % f->csz, l = MIN(f->csz - o, left); if (xrd(f, cl_off(f, c) + o, p, l)) return E_IO; p += l; pos += l; left -= l; } return n; }
static i64 xo_write(file_t *fi, const void *b, u64 n) {
    xf_t *f = fi->m->priv; xe_t *e = fi->priv; u64 pos = fi->pos, end = pos + n; if (!n) return 0;
    u64 have = (e->size + f->csz - 1) / f->csz, need = (end + f->csz - 1) / f->csz; int er = grow(f, &e->first, &e->nc, have, need, 0); if (er) return er;
    if (pos > e->size) { u8 *z = kzalloc(4096); u64 q = e->size; while (q < pos) { u32 c = nth(f, e->first, e->nc, q / f->csz); u64 o = q % f->csz, l = MIN(MIN(f->csz - o, pos - q), 4096UL); xwr(f, cl_off(f, c) + o, z, l); q += l; } kfree(z); }
    const u8 *p = b; u64 left = n; while (left) { u32 c = nth(f, e->first, e->nc, pos / f->csz); if (!c) return E_IO; u64 o = pos % f->csz, l = MIN(f->csz - o, left); if (xwr(f, cl_off(f, c) + o, p, l)) return E_IO; p += l; pos += l; left -= l; }
    if (end > e->size) e->size = end; stream_put(e); *(u32 *)(e->set + 12) = now_ts(); if (write_set(f, e)) return E_IO; return n; }
static void xo_close(file_t *fi) { kfree(fi->priv); }
static int xo_stat(mount_t *m, const char *p, zdirent_t *d) { xf_t *f = m->priv; xe_t *e = kmalloc(sizeof *e); int r = walk(f, p, e); if (!r) { memset(d, 0, sizeof *d); strncpy0(d->name, e->root ? "" : e->name, 256); d->size = e->size; d->is_dir = e->dir; } kfree(e); return r; }
static int xo_readdir(mount_t *m, const char *p, int idx, zdirent_t *d) {
    xf_t *f = m->priv; xe_t *e = kmalloc(sizeof *e), *x = kmalloc(sizeof *x); int r = walk(f, p, e); if (!r && !e->dir) r = E_NOTDIR;
    if (!r) { u64 lim; dir_limit(f, e->first, e->nc, e->size, e->root, &lim); int k = 0; r = E_NOENT; for (u64 q = 0; q < lim; q += 32) { int s = read_set(f, e->first, e->nc, q, lim, x); if (s < 0) break; if (s == 1) { if (k++ == idx) { memset(d, 0, sizeof *d); strncpy0(d->name, x->name, 256); d->size = x->size; d->is_dir = x->dir; r = 0; break; } q += x->nsec * 32; } } }
    kfree(e); kfree(x); return r; }
static int xo_mkdir(mount_t *m, const char *p) { xf_t *f = m->priv; xe_t *e = kmalloc(sizeof *e); int r = create(f, p, 1, e); kfree(e); return r; }
static int xo_unlink(mount_t *m, const char *p) { xf_t *f = m->priv; xe_t *e = kmalloc(sizeof *e); int r = walk(f, p, e); if (!r && e->root) r = E_INVAL; if (!r && e->dir && !dir_empty(f, e)) r = E_NOTEMPTY; if (!r) { free_chain(f, e->first, e->nc, e->size); del_set(f, e); } kfree(e); return r; }
static int xo_rename(mount_t *m, const char *a, const char *b) {
    xf_t *f = m->priv; xe_t *e = kmalloc(sizeof *e), *n = kmalloc(sizeof *n), *t = kmalloc(sizeof *t); int r = walk(f, a, e); if (!r && e->root) r = E_INVAL;
    int caseonly = !r && name_eq(f, a, b);
    if (!r && !caseonly && !walk(f, b, t)) r = E_EXIST;
    if (!r) { xe_t old = *e; if (caseonly) del_set(f, e); r = create(f, b, 0, n);
        if (!r) { *(u16 *)(n->set + 4) = *(u16 *)(old.set + 4); memcpy(n->set + 8, old.set + 8, 12); n->first = old.first; n->nc = old.nc; n->size = old.size; n->dir = old.dir; stream_put(n); r = write_set(f, n); if (!r && !caseonly && !walk(f, a, t) && t->epos == old.epos && t->pfirst == old.pfirst) del_set(f, t); }
        else if (caseonly) { for (int k = 0; k <= old.nsec; k++) old.set[k * 32] |= 0x80; write_set(f, &old); } }
    kfree(e); kfree(n); kfree(t); return r; }
static void xo_sync(mount_t *m) { (void)m; blk_sync(); }
static const fsops_t xops = { xo_open, xo_read, xo_write, xo_close, xo_stat, xo_readdir, xo_mkdir, xo_unlink, xo_rename, xo_sync, 0 };
int exfat_probe(blkdev_t *d, const char *name) {
    u8 *b = kmalloc(4096); int r = -1;
    if (d->ssize <= 4096 && !blk_read(d, 0, 1, b) && !memcmp(b + 3, "EXFAT   ", 8)) {
        xf_t *f = kzalloc(sizeof *f); f->d = d; f->bps = b[108]; f->spc = 1u << b[109]; f->fat = *(u32 *)(b + 80); f->heap = *(u32 *)(b + 88); f->count = *(u32 *)(b + 92); f->root = *(u32 *)(b + 96); f->csz = (u64)f->spc << f->bps;
        u8 e[32]; char label[16] = "";
        for (u64 p = 0; p < f->csz * 4; p += 32) { u32 c = nth(f, f->root, 0, p / f->csz); if (!c || xrd(f, cl_off(f, c) + p % f->csz, e, 32) || !e[0]) break;
            if (e[0] == 0x81 && !f->bm) { f->bmcl = *(u32 *)(e + 20); f->bmlen = *(u64 *)(e + 24); f->bmnc = 0; u64 need = (f->count + 7) / 8; f->bm = kzalloc(MAX(need, f->bmlen) + 8); for (u64 q = 0; q < f->bmlen; q += f->csz) { u32 cl = nth(f, f->bmcl, 0, q / f->csz); if (!cl) { f->bmnc = 1; cl = f->bmcl + q / f->csz; } xrd(f, cl_off(f, cl), f->bm + q, MIN(f->csz, f->bmlen - q)); } }
            else if (e[0] == 0x82 && !f->up) { u32 cl = *(u32 *)(e + 20); u64 len = *(u64 *)(e + 24); u16 *raw = kmalloc(len + 2); if (!xrd(f, cl_off(f, cl), raw, len)) { f->up = kmalloc(65536 * 2); for (u32 i = 0; i < 65536; i++) f->up[i] = i; u32 c = 0; for (u64 i = 0; i < len / 2 && c < 65536; i++) { if (raw[i] == 0xFFFF && i + 1 < len / 2) { c += raw[++i]; continue; } f->up[c++] = raw[i]; } } kfree(raw); }
            else if (e[0] == 0x83) { int n = MIN((int)e[1], 11), k = 0; for (int i = 0; i < n && k < 14; i++) { u16 ch = *(u16 *)(e + 2 + i * 2); label[k++] = ch < 128 ? ch : '_'; } label[k] = 0; } }
        if (f->bm) for (u32 c = 2; c < f->count + 2; c++) if (!bm_get(f, c)) f->nfree++;
        int mi = vfs_mount(name, &xops, f, d, "exfat", label[0] ? label : 0); vfs_set_size(mi, (u64)f->count * f->csz, f->nfree * f->csz);
        extern mount_t *vfs_mount_ptr(int); f->m = vfs_mount_ptr(mi); r = 0;
    }
    kfree(b); return r;
}
