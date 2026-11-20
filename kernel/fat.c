/* FAT12/16/32 con nombres largos: lectura y escritura */
#include "kernel.h"
typedef struct {
    blkdev_t *d; u32 ss, spc, rsvd, nfats, fatsz, rootent, rootclus, fat, rootlba, data, nclus, csize, type, hint; i64 freec; mount_t *m; u32 fsinfo; u8 *sec;
    u8 *fbuf; u32 fsec; int fvalid, fdirty; /* un sector de la FAT en memoria: los cambios se escriben al terminar cada operacion */
} fs_t;
typedef struct { u32 clus, size; u8 attr; u64 lba; u32 off; u32 first_idx, idx, dir; char name[256]; } ent_t;
#define EOC 0x0FFFFFF8
static u64 c2l(fs_t *f, u32 c) { return f->data + (u64)(c - 2) * f->spc; }
static int ffl(fs_t *f) { if (!f->fdirty) return 0; f->fdirty = 0; for (u32 n = 0; n < f->nfats; n++) if (blk_write(f->d, f->fat + n * f->fatsz + f->fsec, 1, f->fbuf)) return E_IO; return 0; }
static u8 *fsec_get(fs_t *f, u32 s) { if (f->fvalid && f->fsec == s) return f->fbuf; if (ffl(f)) return 0; if (!f->fbuf) f->fbuf = kmalloc(4096); if (!f->fbuf || blk_read(f->d, f->fat + s, 1, f->fbuf)) { f->fvalid = 0; return 0; } f->fsec = s; f->fvalid = 1; return f->fbuf; }
static u32 fat_get(fs_t *f, u32 c) {
    u8 b[4096]; u32 o, v;
    if (f->type == 12) {
        o = c + c / 2; u32 s = o / f->ss; if (blk_read(f->d, f->fat + s, 1, b)) return EOC; u32 i = o % f->ss; u8 l = b[i], h;
        if (i + 1 == f->ss) { if (blk_read(f->d, f->fat + s + 1, 1, b)) return EOC; h = b[0]; } else h = b[i + 1];
        v = l | h << 8; v = c & 1 ? v >> 4 : v & 0xFFF; return v >= 0xFF8 ? EOC : v;
    }
    o = c * (f->type == 16 ? 2 : 4); (void)b;
    u8 *fb = fsec_get(f, o / f->ss); if (!fb) return EOC;
    if (f->type == 16) { v = *(u16 *)(fb + o % f->ss); return v >= 0xFFF8 ? EOC : v; }
    v = *(u32 *)(fb + o % f->ss) & 0x0FFFFFFF; return v >= EOC ? EOC : v;
}
static int fat_set(fs_t *f, u32 c, u32 v) {
    u8 b[4096];
    for (u32 n = 0; n < f->nfats; n++) {
        u32 base = f->fat + n * f->fatsz, o;
        if (f->type == 12) {
            o = c + c / 2; u32 s = o / f->ss, i = o % f->ss; v &= 0xFFF;
            if (blk_read(f->d, base + s, 1, b)) return E_IO;
            if (c & 1) b[i] = (b[i] & 15) | v << 4; else b[i] = v;
            if (i + 1 == f->ss) { if (blk_write(f->d, base + s, 1, b) || blk_read(f->d, base + s + 1, 1, b)) return E_IO; s++; i = (u32)-1; }
            if (c & 1) b[i + 1] = v >> 4; else b[i + 1] = (b[i + 1] & 0xF0) | v >> 8;
            if (blk_write(f->d, base + s, 1, b)) return E_IO;
            continue;
        }
        (void)base; o = c * (f->type == 16 ? 2 : 4); u32 i = o % f->ss; u8 *fb = fsec_get(f, o / f->ss); if (!fb) return E_IO;
        if (f->type == 16) *(u16 *)(fb + i) = v; else *(u32 *)(fb + i) = (*(u32 *)(fb + i) & 0xF0000000) | (v & 0x0FFFFFFF);
        f->fdirty = 1; break; /* todas las copias de la FAT se escriben en ffl() */
    }
    return 0;
}
static u32 eoc_val(fs_t *f) { return f->type == 12 ? 0xFFF : f->type == 16 ? 0xFFFF : 0x0FFFFFFF; }
static void update_m(fs_t *f) { if (f->m && f->freec >= 0) f->m->free_b = (u64)f->freec * f->csize; }
static void fsinfo_save(fs_t *f) {
    if (f->type != 32 || !f->fsinfo || f->freec < 0) return;
    u8 b[4096]; if (blk_read(f->d, f->fsinfo, 1, b)) return;
    if (*(u32 *)b != 0x41615252) return;
    *(u32 *)(b + 488) = f->freec; *(u32 *)(b + 492) = f->hint; blk_write(f->d, f->fsinfo, 1, b);
}
static u32 clus_alloc(fs_t *f, u32 prev) {
    u32 c = f->hint;
    for (u32 n = 0; n < f->nclus; n++, c++) {
        if (c < 2 || c >= f->nclus + 2) c = 2;
        if (fat_get(f, c) == 0) {
            if (fat_set(f, c, eoc_val(f))) return 0;
            if (prev) fat_set(f, prev, c);
            f->hint = c + 1; if (f->freec > 0) f->freec--; update_m(f); return c;
        }
    }
    return 0;
}
static void chain_free(fs_t *f, u32 c) { while (c >= 2 && c < EOC && c < f->nclus + 2) { u32 n = fat_get(f, c); fat_set(f, c, 0); if (f->freec >= 0) f->freec++; c = n; } update_m(f); fsinfo_save(f); }
static int clus_zero(fs_t *f, u32 c) { u8 *z = kzalloc(f->csize); if (!z) return E_NOMEM; int r = blk_write(f->d, c2l(f, c), f->spc, z); kfree(z); return r; }

/* localiza la entrada idx del directorio; dir 0 = raiz fija de FAT12/16 */
typedef struct { u32 cl, n; int valid; } dpos_t;
static int dloc(fs_t *f, u32 dir, u32 idx, u64 *lba, u32 *off, dpos_t *p, int ext) {
    u32 per = f->ss / 32;
    if (!dir) { if (idx >= f->rootent) return E_NOSPC; *lba = f->rootlba + idx / per; *off = (idx % per) * 32; return 0; }
    u32 pc = f->csize / 32, cn = idx / pc;
    if (!p->valid || p->n > cn) { p->cl = dir; p->n = 0; p->valid = 1; }
    while (p->n < cn) {
        u32 nx = fat_get(f, p->cl);
        if (nx < 2 || nx >= EOC) { if (!ext) return E_NOENT; nx = clus_alloc(f, p->cl); if (!nx || clus_zero(f, nx)) return E_NOSPC; }
        p->cl = nx; p->n++;
    }
    u32 r = idx % pc; *lba = c2l(f, p->cl) + r / per; *off = (r % per) * 32; return 0;
}
static u8 lfn_sum(const u8 *n) { u8 s = 0; for (int i = 0; i < 11; i++) s = ((s & 1) << 7) + (s >> 1) + n[i]; return s; }
static void short_to(const u8 *e, char *o) {
    int k = 0; u8 lc = e[12];
    for (int i = 0; i < 8 && e[i] != ' '; i++) { u8 c = e[i] == 5 ? 0xE5 : e[i]; o[k++] = (lc & 8) && c >= 'A' && c <= 'Z' ? c + 32 : c; }
    if (e[8] != ' ') { o[k++] = '.'; for (int i = 8; i < 11 && e[i] != ' '; i++) o[k++] = (lc & 16) && e[i] >= 'A' && e[i] <= 'Z' ? e[i] + 32 : e[i]; }
    o[k] = 0;
}
/* iterador: devuelve 1 si hay entrada, 0 al final */
static int dnext(fs_t *f, u32 dir, u32 *idx, dpos_t *p, ent_t *e) {
    u8 s[4096]; u16 lfn[260]; int have = 0, maxl = 0; u32 start = 0; u64 cached = ~0UL;
    for (;; (*idx)++) {
        u64 lba; u32 off;
        if (dloc(f, dir, *idx, &lba, &off, p, 0)) return 0;
        if (lba != cached) { if (blk_read(f->d, lba, 1, s)) return 0; cached = lba; }
        u8 *d = s + off;
        if (d[0] == 0) return 0;
        if (d[0] == 0xE5) { have = 0; continue; }
        if ((d[11] & 0x3F) == 0x0F) {
            int seq = d[0] & 0x3F; if (d[0] & 0x40) { have = 1; maxl = seq * 13; start = *idx; memset(lfn, 0, sizeof lfn); }
            if (seq < 1 || seq > 20) { have = 0; continue; }
            int b = (seq - 1) * 13; static const u8 pos[13] = {1,3,5,7,9,14,16,18,20,22,24,28,30};
            for (int i = 0; i < 13; i++) lfn[b + i] = *(u16 *)(d + pos[i]);
            continue;
        }
        if (d[11] & 8) { have = 0; continue; }
        e->attr = d[11]; e->size = *(u32 *)(d + 28); e->clus = *(u16 *)(d + 26) | (u32)*(u16 *)(d + 20) << 16; e->lba = lba; e->off = off; e->idx = *idx; e->first_idx = have ? start : *idx; e->dir = dir;
        if (have) { int k = 0; for (int i = 0; i < maxl && lfn[i] && lfn[i] != 0xFFFF && k < 252; i++) k += utf8_encode(lfn[i], e->name + k); e->name[k] = 0; }
        else short_to(d, e->name);
        (*idx)++; return 1;
    }
}
static int lookup(fs_t *f, u32 dir, const char *n, ent_t *e) { u32 i = 0; dpos_t p = {0}; while (dnext(f, dir, &i, &p, e)) if (!strcasecmp(e->name, n)) return 0; return E_NOENT; }
static u32 rootdir(fs_t *f) { return f->type == 32 ? f->rootclus : 0; }
/* resuelve una ruta; si parent, se detiene en el padre y deja el ultimo componente en leaf */
static int walk(fs_t *f, const char *path, ent_t *e, u32 *pdir, char *leaf) {
    u32 dir = rootdir(f); char c[256]; const char *p = path; int found = 0;
    for (;;) {
        while (*p == '/') p++;
        if (!*p) break;
        int k = 0; while (*p && *p != '/' && k < 255) c[k++] = *p++; c[k] = 0;
        while (*p == '/') p++;
        if (!*p && pdir) { *pdir = dir; strncpy0(leaf, c, 256); return lookup(f, dir, c, e); }
        int r = lookup(f, dir, c, e); if (r) return r;
        if (*p && !(e->attr & 0x10)) return E_NOTDIR;
        dir = e->clus; found = 1;
    }
    if (pdir) return E_INVAL;
    if (!found) { memset(e, 0, sizeof *e); e->attr = 0x10; e->clus = rootdir(f); e->name[0] = 0; }
    return 0;
}
static void now_fat(u16 *d, u16 *t) { u32 r[6]; rtc_read(r); *d = (r[5] - 1980) << 9 | r[4] << 5 | r[3]; *t = r[2] << 11 | r[1] << 5 | r[0] / 2; }
static int valid83(const char *n, u8 *o) {
    memset(o, ' ', 11); int i = 0, k = 0, ext = 0;
    if (!strcmp(n, ".") || !strcmp(n, "..")) return 0;
    for (; n[i]; i++) {
        u8 c = n[i];
        if (c == '.') { if (ext) return 0; ext = 1; k = 8; continue; }
        if (c >= 'a' && c <= 'z') return 0;
        if (c <= ' ' || c >= 127 || strchr("\"*+,/:;<=>?[\\]|", c)) return 0;
        if ((!ext && k >= 8) || (ext && k >= 11)) return 0;
        o[k++] = c;
    }
    return k > 0 && o[0] != ' ';
}
static void make_short(fs_t *f, u32 dir, const char *n, u8 *o) {
    char base[9]; char ext[4] = ""; int b = 0, x = 0; const char *dot = strrchr(n, '.');
    for (const char *p = n; *p && p != dot && b < 6; p++) { u8 c = *p; if (c >= 'a' && c <= 'z') c -= 32; if (c > ' ' && c < 127 && !strchr("\"*+,./:;<=>?[\\]|", c)) base[b++] = c; }
    if (!b) base[b++] = 'Z';
    if (dot) for (const char *p = dot + 1; *p && x < 3; p++) { u8 c = *p; if (c >= 'a' && c <= 'z') c -= 32; if (c > ' ' && c < 127 && !strchr("\"*+,./:;<=>?[\\]|", c)) ext[x++] = c; }
    for (int t = 1; t < 9999; t++) {
        char s[8]; int l = snprintf(s, 8, "~%d", t); int keep = MIN(b, 8 - l);
        memset(o, ' ', 11); memcpy(o, base, keep); memcpy(o + keep, s, l); memcpy(o + 8, ext, x);
        char nm[16]; short_to(o, nm); ent_t e; if (lookup(f, dir, nm, &e)) return;
    }
}
static int dir_add(fs_t *f, u32 dir, const char *name, u8 attr, u32 clus, ent_t *out) {
    u8 sn[11]; u16 u[256]; int ulen = 0, lfn = !valid83(name, sn);
    if (lfn) { make_short(f, dir, name, sn); const char *p = name; int c; while ((c = utf8_decode(&p)) && ulen < 255) u[ulen++] = c; }
    int need = (lfn ? (ulen + 12) / 13 : 0) + 1, run = 0; u32 i = 0, start = 0; dpos_t p = {0}; u8 s[4096];
    for (;; i++) {
        u64 lba; u32 off; int r = dloc(f, dir, i, &lba, &off, &p, 1); if (r) return r;
        if (blk_read(f->d, lba, 1, s)) return E_IO;
        u8 c = s[off];
        if (c == 0 || c == 0xE5) { if (!run) start = i; if (++run == need) break; } else run = 0;
    }
    u8 sum = lfn_sum(sn); dpos_t q = {0}; u16 dd, tt; now_fat(&dd, &tt);
    for (int k = 0; k < need; k++) {
        u64 lba; u32 off; if (dloc(f, dir, start + k, &lba, &off, &q, 1) || blk_read(f->d, lba, 1, s)) return E_IO;
        u8 *d = s + off; memset(d, 0, 32);
        if (k < need - 1) {
            int seq = need - 1 - k; d[0] = seq | (k == 0 ? 0x40 : 0); d[11] = 0x0F; d[13] = sum;
            static const u8 pos[13] = {1,3,5,7,9,14,16,18,20,22,24,28,30};
            for (int j = 0; j < 13; j++) { int w = (seq - 1) * 13 + j; u16 v = w < ulen ? u[w] : w == ulen ? 0 : 0xFFFF; *(u16 *)(d + pos[j]) = v; }
        } else {
            memcpy(d, sn, 11); if (d[0] == 0xE5) d[0] = 5; d[11] = attr; *(u16 *)(d + 14) = tt; *(u16 *)(d + 16) = dd; *(u16 *)(d + 18) = dd; *(u16 *)(d + 22) = tt; *(u16 *)(d + 24) = dd; *(u16 *)(d + 20) = clus >> 16; *(u16 *)(d + 26) = clus;
            out->lba = lba; out->off = off; out->idx = start + k; out->first_idx = start; out->dir = dir;
        }
        if (blk_write(f->d, lba, 1, s)) return E_IO;
    }
    out->attr = attr; out->clus = clus; out->size = 0; strncpy0(out->name, name, 256);
    return 0;
}
static int dir_del(fs_t *f, ent_t *e) {
    dpos_t p = {0}; u8 s[4096];
    for (u32 i = e->first_idx; i <= e->idx; i++) { u64 lba; u32 off; if (dloc(f, e->dir, i, &lba, &off, &p, 0) || blk_read(f->d, lba, 1, s)) return E_IO; s[off] = 0xE5; if (blk_write(f->d, lba, 1, s)) return E_IO; }
    return 0;
}
static int ent_update(fs_t *f, u64 lba, u32 off, u32 clus, u32 size) {
    u8 s[4096]; if (blk_read(f->d, lba, 1, s)) return E_IO; u8 *d = s + off; u16 dt, tm; now_fat(&dt, &tm);
    *(u16 *)(d + 20) = clus >> 16; *(u16 *)(d + 26) = clus; *(u32 *)(d + 28) = size; *(u16 *)(d + 22) = tm; *(u16 *)(d + 24) = dt; d[11] |= 0x20;
    return blk_write(f->d, lba, 1, s);
}
/* --- operaciones --- */
static int f_open(mount_t *m, const char *p, int fl, file_t *o) {
    fs_t *f = m->priv; ent_t *e = kmalloc(sizeof *e); u32 dir; char *leaf = kmalloc(256); int r;
    if (!*p) { r = E_ISDIR; goto out; }
    r = walk(f, p, e, &dir, leaf);
    if (r == E_NOENT && (fl & O_CREATE)) { r = dir_add(f, dir, leaf, 0x20, 0, e); if (r) goto out; }
    else if (r) goto out;
    if (e->attr & 0x10) { r = E_ISDIR; goto out; }
    if ((fl & O_TRUNC) && e->clus) { chain_free(f, e->clus); e->clus = 0; e->size = 0; ent_update(f, e->lba, e->off, 0, 0); }
    o->priv = f; o->size = e->size; o->a = e->clus; o->b = e->lba; o->c = e->off; o->d = 0; r = 0;
out: kfree(e); kfree(leaf); return r;
}
/* cluster que contiene la posicion pos; d = (indice<<32)|cluster en cache */
static u32 seek_cl(fs_t *f, file_t *o, u64 pos, int ext) {
    u32 want = pos / f->csize, idx = o->d >> 32, c = (u32)o->d;
    if (!o->a) { if (!ext) return 0; c = clus_alloc(f, 0); if (!c) return 0; if (pos) clus_zero(f, c); o->a = c; idx = 0; }
    else if (!c || idx > want) { c = o->a; idx = 0; }
    while (idx < want) {
        u32 n = fat_get(f, c);
        if (n < 2 || n >= EOC) { if (!ext) return 0; n = clus_alloc(f, c); if (!n) return 0; u64 cst = (u64)(idx + 1) * f->csize; if (!(cst >= o->size && pos <= cst)) clus_zero(f, n); /* si se escribe desde el principio del cluster, no hace falta poner ceros */ }
        c = n; idx++;
    }
    o->d = (u64)idx << 32 | c; return c;
}
static i64 f_read(file_t *o, void *b, u64 n) {
    fs_t *f = o->priv; u8 *p = b; u64 done = 0, pos = o->pos; u8 *tmp = 0;
    while (done < n) {
        u32 c = seek_cl(f, o, pos, 0); if (!c) break;
        u32 co = pos % f->csize, sec = co / f->ss, so = co % f->ss; u64 lba = c2l(f, c) + sec;
        if (so || n - done < f->ss) {
            if (!tmp) tmp = kmalloc(f->ss);
            if (blk_read(f->d, lba, 1, tmp)) break;
            u32 l = MIN(f->ss - so, n - done); memcpy(p + done, tmp + so, l); done += l; pos += l; continue;
        }
        u32 run = f->spc - sec; u64 want = (n - done) / f->ss;
        /* clusters contiguos en una sola lectura */
        u32 cc = c; while (run < want && run < 2048) { u32 nx = fat_get(f, cc); if (nx != cc + 1) break; cc = nx; run += f->spc; o->d += 1UL << 32; o->d = (o->d & ~0xFFFFFFFFUL) | cc; }
        u32 cnt = MIN(run, want);
        if (blk_read(f->d, lba, cnt, p + done)) break;
        done += (u64)cnt * f->ss; pos += (u64)cnt * f->ss;
    }
    kfree(tmp); return done;
}
static i64 f_write(file_t *o, const void *b, u64 n) {
    fs_t *f = o->priv; const u8 *p = b; u64 done = 0, pos = o->pos; u8 *tmp = kmalloc(f->ss);
    if (pos + n > 0xFFFFFFFFUL) { kfree(tmp); return E_NOSPC; }
    while (done < n) {
        u32 c = seek_cl(f, o, pos, 1); if (!c) break;
        u32 co = pos % f->csize, sec = co / f->ss, so = co % f->ss; u64 lba = c2l(f, c) + sec;
        if (so || n - done < f->ss) {
            if (blk_read(f->d, lba, 1, tmp)) break;
            u32 l = MIN(f->ss - so, n - done); memcpy(tmp + so, p + done, l);
            if (blk_write(f->d, lba, 1, tmp)) break;
            done += l; pos += l; continue;
        }
        u64 want = (n - done) / f->ss; u32 cnt = MIN(f->spc - sec, want), last = c;
        /* clusters contiguos: una sola escritura grande */
        while (cnt < want && cnt < 2048) { u32 nc = seek_cl(f, o, pos + (u64)cnt * f->ss, 1); if (nc != last + 1) break; last = nc; cnt += MIN((u64)f->spc, want - cnt); }
        if (blk_write(f->d, lba, cnt, p + done)) break;
        done += (u64)cnt * f->ss; pos += (u64)cnt * f->ss;
    }
    kfree(tmp); ffl(f);
    u64 sz = MAX(o->size, pos); ent_update(f, o->b, o->c, o->a, sz);
    return done ? (i64)done : E_NOSPC;
}
static int f_stat(mount_t *m, const char *p, zdirent_t *d) {
    fs_t *f = m->priv; ent_t *e = kmalloc(sizeof *e); int r = walk(f, p, e, 0, 0);
    if (!r) { memset(d, 0, sizeof *d); strncpy0(d->name, e->name, 256); d->size = e->size; d->is_dir = !!(e->attr & 0x10); }
    kfree(e); return r;
}
static int f_readdir(mount_t *m, const char *p, int idx, zdirent_t *d) {
    fs_t *f = m->priv; ent_t *e = kmalloc(sizeof *e); int r = walk(f, p, e, 0, 0);
    if (!r && !(e->attr & 0x10)) r = E_NOTDIR;
    if (!r) {
        u32 dir = e->clus, i = 0; dpos_t q = {0}; int k = 0; r = E_NOENT;
        while (dnext(f, dir, &i, &q, e)) {
            if (!strcmp(e->name, ".") || !strcmp(e->name, "..")) continue;
            if (e->attr & 2) continue;
            if (k++ == idx) { memset(d, 0, sizeof *d); strncpy0(d->name, e->name, 256); d->size = e->size; d->is_dir = !!(e->attr & 0x10); r = 0; break; }
        }
    }
    kfree(e); return r;
}
static int f_mkdir(mount_t *m, const char *p) {
    fs_t *f = m->priv; ent_t *e = kmalloc(sizeof *e); char *leaf = kmalloc(256); u32 dir; int r = walk(f, p, e, &dir, leaf);
    if (!r) r = E_EXIST;
    else if (r == E_NOENT) {
        u32 c = clus_alloc(f, 0); r = E_NOSPC;
        if (c && !clus_zero(f, c)) {
            r = dir_add(f, dir, leaf, 0x10, c, e);
            if (!r) { u8 s[4096]; u64 l = c2l(f, c); blk_read(f->d, l, 1, s); memset(s, ' ', 22); memset(s + 11, 0, 21); memset(s + 32, ' ', 11); memset(s + 43, 0, 21); s[0] = '.'; s[32] = '.'; s[33] = '.'; s[11] = 0x10; s[43] = 0x10;
                *(u16 *)(s + 26) = c; *(u16 *)(s + 20) = c >> 16; u32 pd = dir == rootdir(f) ? 0 : dir; *(u16 *)(s + 58) = pd; *(u16 *)(s + 52) = pd >> 16; r = blk_write(f->d, l, 1, s); }
        }
        fsinfo_save(f);
    }
    kfree(e); kfree(leaf); return r;
}
static int f_unlink(mount_t *m, const char *p) {
    fs_t *f = m->priv; ent_t *e = kmalloc(sizeof *e), *x = kmalloc(sizeof *x); u32 dir; char *leaf = kmalloc(256); int r = walk(f, p, e, &dir, leaf);
    if (!r && (e->attr & 0x10)) { u32 i = 0; dpos_t q = {0}; while (dnext(f, e->clus, &i, &q, x)) if (strcmp(x->name, ".") && strcmp(x->name, "..")) { r = E_NOTEMPTY; break; } }
    if (!r) { r = dir_del(f, e); if (!r && e->clus) chain_free(f, e->clus); }
    kfree(e); kfree(x); kfree(leaf); return r;
}
static int f_rename(mount_t *m, const char *a, const char *b) {
    fs_t *f = m->priv; ent_t *e = kmalloc(sizeof *e), *n = kmalloc(sizeof *n); u32 d1, d2; char *l1 = kmalloc(256), *l2 = kmalloc(256); int r = walk(f, a, e, &d1, l1);
    if (!r) { int x = walk(f, b, n, &d2, l2); if (!x) r = strcasecmp(a, b) ? E_EXIST : 0; else if (x != E_NOENT) r = x; else r = 0; if (!r && !strcasecmp(a, b)) { } }
    if (!r) { r = dir_del(f, e); if (!r) r = dir_add(f, d2, l2, e->attr, e->clus, n); if (!r) r = ent_update(f, n->lba, n->off, e->clus, e->size); if (!r && e->attr & 0x10) { u8 s[4096]; u64 l = c2l(f, e->clus); blk_read(f->d, l, 1, s); u32 pd = d2 == rootdir(f) ? 0 : d2; *(u16 *)(s + 58) = pd; *(u16 *)(s + 52) = pd >> 16; blk_write(f->d, l, 1, s); } }
    kfree(e); kfree(n); kfree(l1); kfree(l2); return r;
}
static void f_sync(mount_t *m) { ffl(m->priv); fsinfo_save(m->priv); }
/* crea un archivo con todos sus clusters seguidos: lo necesita el arranque BIOS */
static int f_contig(mount_t *m, const char *p, u64 size, u64 *lba) {
    fs_t *f = m->priv; ent_t *e = kmalloc(sizeof *e); char *leaf = kmalloc(256); u32 dir; int r = walk(f, p, e, &dir, leaf); u32 need = MAX((size + f->csize - 1) / f->csize, 1UL), run = 0, st = 0;
    if (!r) r = E_EXIST; else if (r == E_NOENT) { r = E_NOSPC; for (u32 c = 2; c < f->nclus + 2; c++) { if (fat_get(f, c)) { run = 0; continue; } if (!run) st = c; if (++run == need) { r = 0; break; } if ((c & 0xFFFF) == 0) yield(); }
        if (!r) { for (u32 i = 0; i < need && !r; i++) r = fat_set(f, st + i, i + 1 == need ? eoc_val(f) : st + i + 1); if (f->freec >= 0) f->freec -= need; if (!r) r = dir_add(f, dir, leaf, 0x20, st, e); if (!r) r = ent_update(f, e->lba, e->off, st, size); if (!r) *lba = c2l(f, st); fsinfo_save(f); update_m(f); } }
    kfree(e); kfree(leaf); return r;
}
static int w_open(mount_t *m, const char *p, int fl, file_t *o) { int r = f_open(m, p, fl, o); ffl(m->priv); return r; }
static int w_mkdir(mount_t *m, const char *p) { int r = f_mkdir(m, p); ffl(m->priv); fsinfo_save(m->priv); return r; }
static int w_unlink(mount_t *m, const char *p) { int r = f_unlink(m, p); ffl(m->priv); return r; }
static int w_rename(mount_t *m, const char *a, const char *b) { int r = f_rename(m, a, b); ffl(m->priv); return r; }
static int w_contig(mount_t *m, const char *p, u64 s, u64 *l) { int r = f_contig(m, p, s, l); ffl(m->priv); fsinfo_save(m->priv); return r; }
static const fsops_t ops = { w_open, f_read, f_write, 0, f_stat, f_readdir, w_mkdir, w_unlink, w_rename, f_sync, w_contig };

int fat_probe(blkdev_t *d, const char *name) {
    u8 *b = kmalloc(MAX(d->ssize, 4096U)); int r = -1;
    if (d->ssize > 4096 || blk_read(d, 0, 1, b) || b[510] != 0x55 || b[511] != 0xAA) goto out;
    u32 ss = *(u16 *)(b + 11), spc = b[13], rs = *(u16 *)(b + 14), nf = b[16], re = *(u16 *)(b + 17), ts = *(u16 *)(b + 19), fz = *(u16 *)(b + 22);
    if (ss != d->ssize || !spc || (spc & (spc - 1)) || !rs || !nf || nf > 2) goto out;
    if (!ts) ts = *(u32 *)(b + 32);
    if (!fz) fz = *(u32 *)(b + 36);
    if (!fz || !ts) goto out;
    u32 rsec = (re * 32 + ss - 1) / ss, data = rs + nf * fz + rsec;
    if (data >= ts) goto out;
    u32 cl = (ts - data) / spc;
    fs_t *f = kzalloc(sizeof *f); f->d = d; f->ss = ss; f->spc = spc; f->rsvd = rs; f->nfats = nf; f->fatsz = fz; f->rootent = re; f->fat = rs; f->rootlba = rs + nf * fz; f->data = data; f->nclus = cl; f->csize = ss * spc; f->hint = 2; f->freec = -1;
    f->type = cl < 4085 ? 12 : cl < 65525 ? 16 : 32;
    char lab[12]; int lo = f->type == 32 ? 71 : 43; memcpy(lab, b + lo, 11); lab[11] = 0; for (int i = 10; i >= 0 && lab[i] == ' '; i--) lab[i] = 0;
    if (f->type == 32) {
        f->rootclus = *(u32 *)(b + 44); f->fsinfo = *(u16 *)(b + 48);
        if (f->fsinfo) { u8 *s = kmalloc(4096); if (!blk_read(d, f->fsinfo, 1, s) && *(u32 *)s == 0x41615252) { u32 v = *(u32 *)(s + 488), h = *(u32 *)(s + 492); if (v <= cl) f->freec = v; if (h >= 2 && h < cl + 2) f->hint = h; } kfree(s); }
    }
    if (f->freec < 0 && cl <= 70000) { f->freec = 0; for (u32 c = 2; c < cl + 2; c++) if (!fat_get(f, c)) f->freec++; }
    char tp[8]; snprintf(tp, 8, "fat%u", f->type);
    ent_t *e = kmalloc(sizeof *e); int sys = !walk(f, "ZUNZUN/KERNEL.BIN", e, 0, 0); kfree(e);
    zdirent_t q; u32 ser = *(u32 *)(b + (f->type == 32 ? 67 : 39)); if (sys && bi.boot_serial && ser != bi.boot_serial) sys = 0; if (sys && !vfs_stat("/sys", &q)) sys = 0;
    int mi = vfs_mount(sys ? "sys" : name, &ops, f, d, tp, lab[0] && lab[0] != ' ' ? lab : 0);
    extern void vfs_set_size(int, u64, u64), vfs_set_free(int, volatile i64 *, u32);
    vfs_set_size(mi, (u64)cl * f->csize, 0); vfs_set_free(mi, &f->freec, f->csize);
    r = 0;
out: kfree(b); return r;
}
