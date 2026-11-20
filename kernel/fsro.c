/* Sistemas de archivos de solo lectura: ISO9660 (Joliet), exFAT y ext2/3/4 */
#include "kernel.h"
extern void vfs_set_size(int, u64, u64);
static int rd_bytes(blkdev_t *d, u64 off, void *buf, u64 n) {
    u8 *p = buf; u32 ss = d->ssize; u8 *t = 0;
    while (n) {
        u64 l = off / ss; u32 o = off % ss;
        if (o || n < ss) { if (!t) t = kmalloc(ss); if (blk_read(d, l, 1, t)) { kfree(t); return E_IO; } u32 c = MIN(ss - o, n); memcpy(p, t + o, c); p += c; off += c; n -= c; }
        else { u64 c = n / ss; if (c > 2048) c = 2048; if (blk_read(d, l, c, p)) { kfree(t); return E_IO; } p += c * ss; off += c * ss; n -= c * ss; }
    }
    kfree(t); return 0;
}
typedef struct { char name[256]; u64 size, loc; int dir; u32 extra; } rent_t;
/* resuelve la ruta con un iterador de directorio propio de cada FS */
typedef int (*iter_t)(void *fs, const rent_t *dir, int idx, rent_t *o);
static int ro_walk(void *fs, iter_t it, const rent_t *root, const char *p, rent_t *o) {
    *o = *root; char c[256];
    for (;;) {
        while (*p == '/') p++; if (!*p) return 0;
        int k = 0; while (*p && *p != '/' && k < 255) c[k++] = *p++; c[k] = 0; if (!o->dir) return E_NOTDIR;
        rent_t d = *o, *e = kmalloc(sizeof *e); int found = 0;
        for (int i = 0; !it(fs, &d, i, e); i++) if (!strcasecmp(e->name, c)) { *o = *e; found = 1; break; }
        kfree(e); if (!found) return E_NOENT;
    }
}
static int ro_stat(void *fs, iter_t it, const rent_t *root, const char *p, zdirent_t *d) { rent_t *e = kmalloc(sizeof *e); int r = ro_walk(fs, it, root, p, e); if (!r) { memset(d, 0, sizeof *d); strncpy0(d->name, e->name, 256); d->size = e->size; d->is_dir = e->dir; } kfree(e); return r; }
static int ro_rdir(void *fs, iter_t it, const rent_t *root, const char *p, int idx, zdirent_t *d) { rent_t *e = kmalloc(sizeof *e), *x = kmalloc(sizeof *x); int r = ro_walk(fs, it, root, p, e); if (!r) { r = e->dir ? it(fs, e, idx, x) : E_NOTDIR; if (!r) { memset(d, 0, sizeof *d); strncpy0(d->name, x->name, 256); d->size = x->size; d->is_dir = x->dir; } } kfree(e); kfree(x); return r; }

/* ===== ISO9660 ===== */
typedef struct { blkdev_t *d; rent_t root; int joliet; } iso_t;
static int iso_iter(void *v, const rent_t *dir, int idx, rent_t *o) {
    iso_t *f = v; u8 *s = kmalloc(2048); int n = 0, r = E_NOENT;
    for (u64 off = 0; off < dir->size; off += 2048) {
        if (rd_bytes(f->d, dir->loc * 2048 + off, s, 2048)) break;
        for (u32 i = 0; i < 2048 && s[i];) {
            u8 *e = s + i; int nl = e[32]; u8 *nm = e + 33;
            if (!(nl == 1 && nm[0] <= 1)) {
                if (n++ == idx) {
                    int k = 0;
                    if (f->joliet) { for (int j = 0; j + 1 < nl && k < 250; j += 2) { u16 c = nm[j] << 8 | nm[j + 1]; if (c == ';') break; k += utf8_encode(c, o->name + k); } }
                    else { for (int j = 0; j < nl && nm[j] != ';'; j++) o->name[k++] = nm[j]; if (k && o->name[k - 1] == '.') k--; }
                    o->name[k] = 0; o->loc = *(u32 *)(e + 2); o->size = *(u32 *)(e + 10); o->dir = !!(e[25] & 2); r = 0; goto out;
                }
            }
            i += e[0];
        }
    }
out: kfree(s); return r;
}
static int iso_open(mount_t *m, const char *p, int fl, file_t *f) { if (fl & (O_WRITE | O_CREATE)) return E_ROFS; iso_t *s = m->priv; rent_t *e = kmalloc(sizeof *e); int r = ro_walk(s, iso_iter, &s->root, p, e); if (!r && e->dir) r = E_ISDIR; if (!r) { f->priv = s; f->size = e->size; f->a = e->loc; } kfree(e); return r; }
static i64 iso_read(file_t *f, void *b, u64 n) { iso_t *s = f->priv; return rd_bytes(s->d, f->a * 2048 + f->pos, b, n) ? E_IO : (i64)n; }
static int iso_stat(mount_t *m, const char *p, zdirent_t *d) { iso_t *s = m->priv; return ro_stat(s, iso_iter, &s->root, p, d); }
static int iso_rd(mount_t *m, const char *p, int i, zdirent_t *d) { iso_t *s = m->priv; return ro_rdir(s, iso_iter, &s->root, p, i, d); }
static const fsops_t isoops = { iso_open, iso_read, 0, 0, iso_stat, iso_rd };
int iso_probe(blkdev_t *d, const char *name) {
    u8 *b = kmalloc(2048); iso_t *f = 0; char label[17] = "";
    for (int i = 16; i < 40; i++) {
        if (rd_bytes(d, i * 2048UL, b, 2048) || memcmp(b + 1, "CD001", 5)) break;
        if (b[0] == 255) break;
        int jol = b[0] == 2 && b[88] == 0x25 && b[89] == 0x2F;
        if (b[0] == 1 || jol) { if (!f) f = kzalloc(sizeof *f); f->d = d; f->joliet = jol; f->root.dir = 1; f->root.loc = *(u32 *)(b + 156 + 2); f->root.size = *(u32 *)(b + 156 + 10); if (b[0] == 1) { memcpy(label, b + 40, 16); for (int k = 15; k >= 0 && label[k] == ' '; k--) label[k] = 0; } }
    }
    kfree(b); if (!f) return -1;
    vfs_mount(name, &isoops, f, d, "iso9660", label); return 0;
}


/* ===== ext2/3/4 ===== */
typedef struct { blkdev_t *d; u32 bs, ipg, isz, bpg, first; u64 gdt; u32 gsz; int bit64; rent_t r; } ext_t;
static int ino_get(ext_t *f, u32 n, u8 *o) {
    u32 g = (n - 1) / f->ipg, i = (n - 1) % f->ipg; u8 gd[64]; if (rd_bytes(f->d, f->gdt + g * (u64)f->gsz, gd, f->gsz)) return E_IO;
    u64 tb = *(u32 *)(gd + 8); if (f->bit64 && f->gsz >= 64) tb |= (u64)*(u32 *)(gd + 40) << 32;
    return rd_bytes(f->d, tb * f->bs + i * (u64)f->isz, o, MIN(f->isz, 256U));
}
/* bloque fisico del bloque logico l */
static u64 ext_map(ext_t *f, const u8 *in, u64 l) {
    u32 fl = *(u32 *)(in + 32); const u8 *ib = in + 40;
    if (fl & 0x80000) {
        u8 *nb = 0; const u8 *h = ib; u64 res = 0;
        for (;;) {
            u16 ne = *(u16 *)(h + 2), dep = *(u16 *)(h + 6); if (*(u16 *)h != 0xF30A) break; const u8 *e = h + 12;
            if (!dep) { for (int i = 0; i < ne; i++, e += 12) { u32 s = *(u32 *)e; u32 len = *(u16 *)(e + 4); if (len > 32768) len -= 32768; if (l >= s && l < s + len) { res = (((u64)*(u16 *)(e + 6) << 32) | *(u32 *)(e + 8)) + (l - s); break; } } break; }
            int k = -1; for (int i = 0; i < ne; i++) if (*(u32 *)(e + i * 12) <= l) k = i; if (k < 0) break; e += k * 12; u64 c = (u64)*(u16 *)(e + 8) << 32 | *(u32 *)(e + 4);
            if (!nb) nb = kmalloc(f->bs); if (rd_bytes(f->d, c * f->bs, nb, f->bs)) break; h = nb;
        }
        kfree(nb); return res;
    }
    u32 p = f->bs / 4, b, *t = 0; u64 r = 0; int lv; u64 idx = l;
    if (l < 12) return *(u32 *)(ib + l * 4);
    idx -= 12; if (idx < p) lv = 1; else if ((idx -= p) < (u64)p * p) lv = 2; else { idx -= (u64)p * p; lv = 3; }
    b = *(u32 *)(ib + (11 + lv) * 4); t = kmalloc(f->bs);
    for (int i = lv - 1; i >= 0 && b; i--) { u64 dv = 1; for (int k = 0; k < i; k++) dv *= p; if (rd_bytes(f->d, (u64)b * f->bs, t, f->bs)) { b = 0; break; } b = t[(idx / dv) % p]; }
    r = b; kfree(t); return r;
}
static u64 in_size(const u8 *in) { return *(u32 *)(in + 4) | (u64)*(u32 *)(in + 108) << 32; }
static int ext_rw(ext_t *f, const u8 *in, u64 pos, u8 *b, u64 n) {
    u8 *t = kmalloc(f->bs); int r = 0;
    while (n) { u64 l = pos / f->bs; u32 o = pos % f->bs; u32 c = MIN(f->bs - o, n); u64 ph = ext_map(f, in, l); if (!ph) memset(b, 0, c); else if (!o && c == f->bs) { if (rd_bytes(f->d, ph * f->bs, b, c)) { r = E_IO; break; } } else { if (rd_bytes(f->d, ph * f->bs, t, f->bs)) { r = E_IO; break; } memcpy(b, t + o, c); } b += c; pos += c; n -= c; }
    kfree(t); return r;
}
static int ext_iter(void *v, const rent_t *d, int idx, rent_t *o) {
    ext_t *f = v; u8 *in = kmalloc(256), *b = kmalloc(f->bs); int r = E_NOENT, n = 0;
    if (!ino_get(f, d->loc, in)) {
        u64 sz = in_size(in);
        for (u64 p = 0; p < sz && r; p += f->bs) {
            if (ext_rw(f, in, p, b, f->bs)) break;
            for (u32 i = 0; i + 8 <= f->bs;) {
                u32 ino = *(u32 *)(b + i); u16 rl = *(u16 *)(b + i + 4); u8 nl = b[i + 6]; if (rl < 8) break;
                if (ino && !(nl == 1 && b[i + 8] == '.') && !(nl == 2 && b[i + 8] == '.' && b[i + 9] == '.')) { if (n++ == idx) { memcpy(o->name, b + i + 8, nl); o->name[nl] = 0; u8 *x = kmalloc(256); if (!ino_get(f, ino, x)) { o->loc = ino; o->size = in_size(x); o->dir = (*(u16 *)x & 0xF000) == 0x4000; r = 0; } kfree(x); break; } }
                i += rl;
            }
        }
    }
    kfree(in); kfree(b); return r;
}
static int ext_open(mount_t *m, const char *p, int fl, file_t *f) { if (fl & (O_WRITE | O_CREATE)) return E_ROFS; ext_t *s = m->priv; rent_t *e = kmalloc(sizeof *e); int r = ro_walk(s, ext_iter, &s->r, p, e); if (!r && e->dir) r = E_ISDIR; if (!r) { u8 *in = kmalloc(256); ino_get(s, e->loc, in); f->priv = s; f->size = e->size; f->a = (u64)in; } kfree(e); return r; }
static i64 ext_read(file_t *f, void *b, u64 n) { return ext_rw(f->priv ? f->priv : 0, (u8 *)f->a, f->pos, b, n) ? E_IO : (i64)n; }
static void ext_close(file_t *f) { kfree((void *)f->a); }
static int ext_stat(mount_t *m, const char *p, zdirent_t *d) { ext_t *s = m->priv; return ro_stat(s, ext_iter, &s->r, p, d); }
static int ext_rd(mount_t *m, const char *p, int i, zdirent_t *d) { ext_t *s = m->priv; return ro_rdir(s, ext_iter, &s->r, p, i, d); }
static const fsops_t extops = { ext_open, ext_read, 0, ext_close, ext_stat, ext_rd };
int ext_probe(blkdev_t *d, const char *name) {
    u8 *b = kmalloc(1024); int r = -1;
    if (!rd_bytes(d, 1024, b, 1024) && *(u16 *)(b + 56) == 0xEF53) {
        ext_t *f = kzalloc(sizeof *f); f->d = d; f->bs = 1024u << *(u32 *)(b + 24); f->bpg = *(u32 *)(b + 32); f->ipg = *(u32 *)(b + 40); f->isz = *(u32 *)(b + 76) ? *(u16 *)(b + 88) : 128; if (!f->isz) f->isz = 128;
        u32 inc = *(u32 *)(b + 96); f->bit64 = !!(inc & 0x80); f->gsz = f->bit64 ? MAX(*(u16 *)(b + 254), (u16)32) : 32; f->first = *(u32 *)(b + 20); f->gdt = (u64)(f->first + 1) * f->bs; f->r.dir = 1; f->r.loc = 2;
        char lab[17]; memcpy(lab, b + 120, 16); lab[16] = 0; u64 cnt = *(u32 *)(b + 4), fr = *(u32 *)(b + 12);
        int mi = vfs_mount(name, &extops, f, d, inc & 0x40 ? "ext4" : "ext2", lab); vfs_set_size(mi, cnt * f->bs, fr * f->bs); r = 0;
    }
    kfree(b); return r;
}

/* ===== NTFS (solo lectura) ===== */
typedef struct { u64 lcn, len; } run_t;
typedef struct { char name[256]; u64 size, ref; int dir; } nent_t;
typedef struct { blkdev_t *d; u32 bps, cs, rs, irs; u64 mft_lcn; run_t *mr; int nmr; rent_t r; nent_t *ce; int nce, cap; u64 cdir; u8 *rec, *rec2; } nt_t;
#define MFTOK(p) (!memcmp((p), "FILE", 4))
static int nt_fix(nt_t *f, u8 *b, u32 size) { u16 uo = *(u16 *)(b + 4), un = *(u16 *)(b + 6); if (uo + un * 2u > size) return -1; u16 sig = *(u16 *)(b + uo); for (u32 i = 1; i < un; i++) { u32 o = i * 512 - 2; if (o + 2 > size) break; if (*(u16 *)(b + o) != sig) return -1; *(u16 *)(b + o) = *(u16 *)(b + uo + i * 2); } return 0; }
static int nt_runs(const u8 *p, const u8 *e, run_t **o, int *n) { int cap = 16, c = 0; run_t *r = kmalloc(cap * sizeof *r); i64 l = 0; while (p < e && *p) { int ls = *p & 15, os = *p >> 4; p++; if (p + ls + os > e || ls > 8 || os > 8) break; u64 len = 0; for (int i = 0; i < ls; i++) len |= (u64)p[i] << (i * 8); p += ls; i64 d = 0; for (int i = 0; i < os; i++) d |= (i64)p[i] << (i * 8); if (os && (p[os - 1] & 0x80)) d |= -(1L << (os * 8)); p += os; if (c == cap) { run_t *nr = kmalloc(cap * 2 * sizeof *nr); memcpy(nr, r, c * sizeof *r); kfree(r); r = nr; cap *= 2; } l += d; r[c].lcn = os ? (u64)l : ~0UL; r[c].len = len; c++; } *o = r; *n = c; return 0; }
static int nt_rr(nt_t *f, const run_t *r, int n, u64 off, u8 *b, u64 len) { u64 vc = off / f->cs; u32 co = off % f->cs; for (int i = 0; i < n && len; i++) { if (vc >= r[i].len) { vc -= r[i].len; continue; } while (vc < r[i].len && len) { u64 c = MIN((r[i].len - vc) * f->cs - co, len); if (r[i].lcn == ~0UL) memset(b, 0, c); else if (rd_bytes(f->d, (r[i].lcn + vc) * f->cs + co, b, c)) return E_IO; b += c; len -= c; vc += (co + c) / f->cs; co = (co + c) % f->cs; } vc = 0; } return len ? E_IO : 0; }
static int nt_rec(nt_t *f, u64 n, u8 *b) { if (nt_rr(f, f->mr, f->nmr, n * f->rs, b, f->rs) || !MFTOK(b)) return E_IO; return nt_fix(f, b, f->rs) ? E_IO : 0; }
/* busca un atributo sin nombre (o $I30); devuelve puntero dentro del registro */
static u8 *nt_attr(nt_t *f, u8 *r, u32 type, int named) { u32 o = *(u16 *)(r + 20); while (o + 8 <= f->rs) { u8 *a = r + o; u32 t = *(u32 *)a, l = *(u32 *)(a + 4); if (t == 0xFFFFFFFF || !l || o + l > f->rs) break; if (t == type && (named || !a[9])) return a; o += l; } return 0; }
typedef struct { nt_t *f; int resident, nr, bad; u64 size; u8 *res; run_t *r; } nd_t;
static void nd_free(nd_t *d) { kfree(d->res); kfree(d->r); }
static void nd_add(nd_t *d, const run_t *r, int n) { run_t *x = kmalloc((d->nr + n + 1) * sizeof *x); if (d->nr) memcpy(x, d->r, d->nr * sizeof *x); memcpy(x + d->nr, r, n * sizeof *x); kfree(d->r); d->r = x; d->nr += n; }
static int nd_from(nt_t *f, u8 *a, nd_t *d, int first) { u16 fl = *(u16 *)(a + 12); if (fl & 0xC001) { d->bad = 1; return E_NOSYS; } if (!a[8]) { u32 vl = *(u32 *)(a + 16); u16 vo = *(u16 *)(a + 20); d->resident = 1; d->size = vl; d->res = kmalloc(vl + 1); memcpy(d->res, a + vo, vl); return 0; } if (first) d->size = *(u64 *)(a + 48); run_t *r; int n; u16 ro = *(u16 *)(a + 32); nt_runs(a + ro, a + *(u32 *)(a + 4), &r, &n); nd_add(d, r, n); kfree(r); return 0; }
static int nt_data(nt_t *f, u64 rec, nd_t *d) {
    memset(d, 0, sizeof *d); d->f = f; u8 *b = kmalloc(f->rs), *c = kmalloc(f->rs); int e = nt_rec(f, rec, b); if (e) goto out; u8 *a = nt_attr(f, b, 0x80, 0); if (a) { e = nd_from(f, a, d, 1); goto out; } a = nt_attr(f, b, 0x20, 1); e = E_NOENT; if (!a) goto out;
    u32 ll; u8 *l; int free_l = 0; if (!a[8]) { ll = *(u32 *)(a + 16); l = a + *(u16 *)(a + 20); } else { run_t *r; int n; nt_runs(a + *(u16 *)(a + 32), a + *(u32 *)(a + 4), &r, &n); ll = MIN(*(u64 *)(a + 48), 1UL << 20); l = kmalloc(ll); free_l = 1; nt_rr(f, r, n, 0, l, ll); kfree(r); }
    int got = 0; for (u32 o = 0; o + 26 <= ll;) { u8 *x = l + o; u16 rl = *(u16 *)(x + 4); if (!rl) break; if (*(u32 *)x == 0x80 && !x[6]) { u64 ref = *(u64 *)(x + 16) & 0xFFFFFFFFFFFFUL; if (!nt_rec(f, ref, c)) { for (u32 ao = *(u16 *)(c + 20); ao + 8 <= f->rs;) { u8 *q = c + ao; if (*(u32 *)q == 0xFFFFFFFF || !*(u32 *)(q + 4)) break; if (*(u32 *)q == 0x80 && !q[9] && *(u64 *)(q + 16) == *(u64 *)(x + 8)) { nd_from(f, q, d, !got); got = 1; break; } ao += *(u32 *)(q + 4); } } } o += rl; } if (free_l) kfree(l); e = got ? 0 : E_NOENT;
out: kfree(b); kfree(c); return e;
}
static void nt_addent(nt_t *f, u8 *e) { u64 ref = *(u64 *)e & 0xFFFFFFFFFFFFUL; u8 *k = e + 16; int ns = k[65], nl = k[64]; if (ns == 2) return; if (ref < 24 && ref != 5) return; if (f->nce == f->cap) { int cp = f->cap ? f->cap * 2 : 128; nent_t *x = kmalloc(cp * sizeof *x); if (f->nce) memcpy(x, f->ce, f->nce * sizeof *x); kfree(f->ce); f->ce = x; f->cap = cp; } nent_t *n = &f->ce[f->nce]; int o = 0; for (int i = 0; i < nl && o < 250; i++) o += utf8_encode(*(u16 *)(k + 66 + i * 2), n->name + o); n->name[o] = 0; if (n->name[0] == '$') return; n->ref = ref; n->size = *(u64 *)(k + 48); n->dir = !!(*(u32 *)(k + 56) & 0x10000000); f->nce++; }
static void nt_node(nt_t *f, u8 *e, u8 *end, const run_t *r, int nr, int depth);
static void nt_child(nt_t *f, u64 vcn, const run_t *r, int nr, int depth) { if (depth > 16 || !r) return; u8 *b = kmalloc(f->irs); u64 off = f->irs >= f->cs ? vcn * f->cs : vcn * f->irs; if (!nt_rr(f, r, nr, off, b, f->irs) && !memcmp(b, "INDX", 4) && !nt_fix(f, b, f->irs)) { u32 s = 24 + *(u32 *)(b + 24), t = 24 + *(u32 *)(b + 28); if (t <= f->irs) nt_node(f, b + s, b + t, r, nr, depth + 1); } kfree(b); }
static void nt_node(nt_t *f, u8 *e, u8 *end, const run_t *r, int nr, int depth) { while (e + 16 <= end) { u16 l = *(u16 *)(e + 8), fl = *(u16 *)(e + 12); if (l < 16 || e + l > end) break; if (fl & 1) nt_child(f, *(u64 *)(e + l - 8), r, nr, depth); if (fl & 2) break; nt_addent(f, e); e += l; } }
static int nt_list(nt_t *f, u64 ref) { if (f->cdir == ref && f->ce) return 0; f->nce = 0; f->cdir = ~0UL; u8 *b = kmalloc(f->rs); int e = nt_rec(f, ref, b); if (!e) { u8 *ir = nt_attr(f, b, 0x90, 1); if (!ir) e = E_NOTDIR; else { u8 *v = ir + *(u16 *)(ir + 20); run_t *r = 0; int n = 0; u8 *ia = nt_attr(f, b, 0xA0, 1); if (ia) nt_runs(ia + *(u16 *)(ia + 32), ia + *(u32 *)(ia + 4), &r, &n); u32 s = 16 + *(u32 *)(v + 16), t = 16 + *(u32 *)(v + 20); if (!f->ce) { f->cap = 128; f->ce = kmalloc(f->cap * sizeof *f->ce); } nt_node(f, v + s, v + t, r, n, 0); kfree(r); f->cdir = ref; } } kfree(b);
    if (!e) { for (int i = 0; i < f->nce; i++) { int dup = 0; if (i && f->ce[i - 1].ref == f->ce[i].ref) dup = 1; if (dup) { memmove(&f->ce[i - 1], &f->ce[i], (f->nce - i) * sizeof *f->ce); f->nce--; i--; } } } return e; }
static int nt_iter(void *v, const rent_t *d, int idx, rent_t *o) { nt_t *f = v; int e = nt_list(f, d->loc); if (e) return e; if (idx >= f->nce) return E_NOENT; nent_t *n = &f->ce[idx]; strncpy0(o->name, n->name, 256); o->size = n->size; o->loc = n->ref; o->dir = n->dir; return 0; }
typedef struct { nd_t d; } nf_t;
static int nt_open(mount_t *m, const char *p, int fl, file_t *o) { if (fl & (O_WRITE | O_CREATE)) return E_ROFS; nt_t *f = m->priv; rent_t *e = kmalloc(sizeof *e); int r = ro_walk(f, nt_iter, &f->r, p, e); if (!r && e->dir) r = E_ISDIR; if (!r) { nf_t *n = kzalloc(sizeof *n); r = nt_data(f, e->loc, &n->d); if (r) kfree(n); else { o->priv = n; o->size = n->d.size; } } kfree(e); return r; }
static i64 nt_read(file_t *o, void *b, u64 n) { nf_t *x = o->priv; nd_t *d = &x->d; if (d->resident) { memcpy(b, d->res + o->pos, n); return n; } return nt_rr(d->f, d->r, d->nr, o->pos, b, n) ? E_IO : (i64)n; }
static void nt_close(file_t *o) { nf_t *x = o->priv; nd_free(&x->d); kfree(x); }
static int nt_stat(mount_t *m, const char *p, zdirent_t *d) { nt_t *f = m->priv; return ro_stat(f, nt_iter, &f->r, p, d); }
static int nt_rdir(mount_t *m, const char *p, int i, zdirent_t *d) { nt_t *f = m->priv; return ro_rdir(f, nt_iter, &f->r, p, i, d); }
static const fsops_t ntops = { nt_open, nt_read, 0, nt_close, nt_stat, nt_rdir };
int ntfs_probe(blkdev_t *d, const char *name) {
    u8 *b = kmalloc(4096); int r = -1; if (d->ssize <= 4096 && !blk_read(d, 0, 1, b) && !memcmp(b + 3, "NTFS    ", 8)) {
        nt_t *f = kzalloc(sizeof *f); f->d = d; f->bps = *(u16 *)(b + 11); f->cs = f->bps * b[13]; f->mft_lcn = *(u64 *)(b + 48); i8 c = b[64], ic = b[68]; f->rs = c > 0 ? (u32)c * f->cs : 1u << -c; f->irs = ic > 0 ? (u32)ic * f->cs : 1u << -ic; u64 total = *(u64 *)(b + 40);
        if (f->cs && f->rs >= 512 && f->rs <= 65536) { static run_t boot; u8 *m = kmalloc(f->rs); boot.lcn = f->mft_lcn; boot.len = 16 + f->rs / f->cs; run_t br = boot; f->mr = &br; f->nmr = 1; if (!nt_rr(f, f->mr, 1, 0, m, f->rs) && MFTOK(m) && !nt_fix(f, m, f->rs)) { u8 *a = nt_attr(f, m, 0x80, 0); if (a && a[8]) { nt_runs(a + *(u16 *)(a + 32), a + *(u32 *)(a + 4), &f->mr, &f->nmr); f->r.dir = 1; f->r.loc = 5; f->cdir = ~0UL; int mi = vfs_mount(name, &ntops, f, d, "ntfs", 0); vfs_set_size(mi, total * f->bps, 0); r = 0; } } kfree(m); } }
    kfree(b); return r;
}
