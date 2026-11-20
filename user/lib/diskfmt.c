/* Formateo (FAT16, FAT32, exFAT, ext2) e instalacion en disco */
#include "z.h"
static u8 *ZB; static const int ZN = 256;
static int wr(const char *d, u64 l, u64 n, const void *b, u32 ss) { const u8 *p = b; while (n) { int c = MIN(n, 256UL); int e = z_disk_write(d, l, c, p); if (e) return e; l += c; n -= c; p += c * ss; } return 0; }
static int zeros(const char *d, u64 l, u64 n, u32 ss) { if (!ZB) ZB = calloc(ZN, 4096); while (n) { int c = MIN(n, (u64)ZN); int e = z_disk_write(d, l, c, ZB); if (e) return e; l += c; n -= c; } return 0; }
static void msg(dk_cb c, int p, const char *m) { if (c) c(p, m); }
static u32 serial(void) { return rnd(); }
static void label11(char *o, const char *l) { memset(o, ' ', 11); for (int i = 0; i < 11 && l[i]; i++) o[i] = toupper(l[i]) ; }
typedef struct { u32 ss, spc, rs, fz, roots, ncl; u64 data, total; int type; } fatl;
static int fat_layout(u64 total, u32 ss, int type, fatl *f) {
    memset(f, 0, sizeof *f); f->ss = ss; f->type = type; f->total = total; u64 by = total * ss;
    if (total > 0xFFFFFFFFUL) return -22;
    if (type == 32) {
        u32 cb = by <= (260UL << 20) ? 512 : by <= (8UL << 30) ? 4096 : by <= (16UL << 30) ? 8192 : by <= (32UL << 30) ? 16384 : 32768; if (cb < ss) cb = ss;
        f->spc = cb / ss; f->rs = 32; u32 fz = 1; u64 cl; for (;;) { u64 d = f->rs + 2UL * fz; if (d >= total) return -28; cl = (total - d) / f->spc; u32 n = (cl + 2) * 4 / ss + 1; if (n <= fz) break; fz = n; }
        if (cl < 65525) return -28; f->fz = fz; f->ncl = cl; f->data = f->rs + 2UL * fz; return 0;
    }
    f->rs = 1; f->roots = 512; u32 rsec = (512 * 32 + ss - 1) / ss;
    for (u32 cb = ss; cb <= 32768; cb *= 2) { u32 spc = cb / ss, fz = 1; u64 cl; for (;;) { u64 d = 1 + 2UL * fz + rsec; if (d >= total) return -28; cl = (total - d) / spc; u32 n = (cl + 2) * 2 / ss + 1; if (n <= fz) break; fz = n; } if (cl < 65525) { if (cl < 4085) return -28; f->spc = spc; f->fz = fz; f->ncl = cl; f->data = 1 + 2UL * fz + rsec; return 0; } }
    return -28;
}
static int fat_mk(const char *d, u64 base, const fatl *f, const char *label, u64 hidden, dk_cb cb) {
    u32 ss = f->ss; u8 *b = calloc(1, ss); char l[11]; label11(l, label[0] ? label : "ZUNZUN"); int e; msg(cb, 5, "Escribiendo tablas...");
    if ((e = zeros(d, base, f->data + (f->type == 32 ? f->spc : 0), ss))) { free(b); return e; }
    b[0] = 0xEB; b[1] = f->type == 32 ? 0x58 : 0x3C; b[2] = 0x90; memcpy(b + 3, "ZUNZUNOS", 8); *(u16 *)(b + 11) = ss; b[13] = f->spc; *(u16 *)(b + 14) = f->rs; b[16] = 2; *(u16 *)(b + 17) = f->roots; if (f->total < 65536) *(u16 *)(b + 19) = f->total; else *(u32 *)(b + 32) = f->total; b[21] = 0xF8; *(u16 *)(b + 24) = 63; *(u16 *)(b + 26) = 255; *(u32 *)(b + 28) = hidden; int o;
    if (f->type == 32) { *(u32 *)(b + 36) = f->fz; *(u32 *)(b + 44) = 2; *(u16 *)(b + 48) = 1; *(u16 *)(b + 50) = 6; o = 64; memcpy(b + 82, "FAT32   ", 8); memcpy(b + 71, l, 11); } else { *(u16 *)(b + 22) = f->fz; o = 36; memcpy(b + 54, "FAT16   ", 8); memcpy(b + 43, l, 11); }
    b[o] = 0x80; b[o + 2] = 0x29; *(u32 *)(b + o + 3) = serial(); b[ss - 2] = 0x55; b[ss - 1] = 0xAA; if ((e = z_disk_write(d, base, 1, b))) goto out;
    if (f->type == 32) { z_disk_write(d, base + 6, 1, b); memset(b, 0, ss); *(u32 *)b = 0x41615252; *(u32 *)(b + 484) = 0x61417272; *(u32 *)(b + 488) = f->ncl - 1; *(u32 *)(b + 492) = 3; b[ss - 2] = 0x55; b[ss - 1] = 0xAA; z_disk_write(d, base + 1, 1, b); z_disk_write(d, base + 7, 1, b); }
    memset(b, 0, ss); if (f->type == 32) { ((u32 *)b)[0] = 0x0FFFFFF8; ((u32 *)b)[1] = 0x0FFFFFFF; ((u32 *)b)[2] = 0x0FFFFFFF; } else { ((u16 *)b)[0] = 0xFFF8; ((u16 *)b)[1] = 0xFFFF; }
    z_disk_write(d, base + f->rs, 1, b); e = z_disk_write(d, base + f->rs + f->fz, 1, b); if (e) goto out;
    memset(b, 0, ss); memcpy(b, l, 11); b[11] = 8; e = z_disk_write(d, base + (f->type == 32 ? f->data : f->rs + 2UL * f->fz), 1, b);
out: free(b); return e;
}
/* ----- exFAT ----- */
static u32 rotsum(u32 c, u8 v) { return ((c & 1) ? 0x80000000u : 0) + (c >> 1) + v; }
static int ex_mk(const char *d, u64 base, u64 total, u32 ss, const char *label, u64 hidden, dk_cb cb) {
    int sb = 0; while ((1u << sb) < ss) sb++; u64 by = total * ss; u32 cb_ = by <= (256UL << 20) ? 4096 : by <= (32UL << 30) ? 32768 : 131072; int cs = 0; while ((ss << cs) < cb_) cs++; u32 spc = 1u << cs;
    if (total < 16384) return -28; u32 fo = 2048 / (ss / 512), fl = 1, ho = 0, cc = 0;
    for (int it = 0; it < 8; it++) { ho = (fo + fl + spc - 1) / spc * spc; cc = (total - ho) / spc; fl = ((u64)cc + 2) * 4 / ss + 1; } ho = (fo + fl + spc - 1) / spc * spc; cc = (total - ho) / spc; if (total > 0xFFFFFFFFUL * 4) return -28;
    u32 csz = spc * ss, bml = (cc + 7) / 8, bmc = (bml + csz - 1) / csz; static const u16 runs[]={0}; (void)runs;
    u16 *up = malloc(1024); int n = 0; up[n++] = 0xFFFF; up[n++] = 0x61; for (int c = 0x61; c <= 0x7A; c++) up[n++] = c - 32; up[n++] = 0xFFFF; up[n++] = 0xE0 - 0x7B; for (int c = 0xE0; c <= 0xFE; c++) up[n++] = c == 0xF7 ? c : c - 32; up[n++] = 0xFFFF; up[n++] = 0xFFFF; up[n++] = 0xFFFF; up[n++] = 0xFF; (void)0; n -= 4; up[n++] = 0xFFFF; up[n++] = (u16)(0x10000UL - 0xFF);
    u32 ul = n * 2, us = 0; for (u32 i = 0; i < ul; i++) us = rotsum(us, ((u8 *)up)[i]);
    u32 c_bm = 2, c_up = 2 + bmc, c_rt = c_up + 1, used = bmc + 2; msg(cb, 5, "Escribiendo tablas..."); int e = zeros(d, base, ho + (u64)(bmc + 2) * spc, ss); if (e) return e;
    u8 *s = calloc(12, ss); s[0] = 0xEB; s[1] = 0x76; s[2] = 0x90; memcpy(s + 3, "EXFAT   ", 8); *(u64 *)(s + 64) = hidden; *(u64 *)(s + 72) = total; *(u32 *)(s + 80) = fo; *(u32 *)(s + 84) = fl; *(u32 *)(s + 88) = ho; *(u32 *)(s + 92) = cc; *(u32 *)(s + 96) = c_rt; *(u32 *)(s + 100) = serial(); *(u16 *)(s + 104) = 0x100; s[108] = sb; s[109] = cs; s[110] = 1; s[111] = 0x80; s[112] = 0; s[510] = 0x55; s[511] = 0xAA;
    for (int i = 1; i <= 8; i++) { u8 *x = s + i * ss; x[ss - 2] = 0x55; x[ss - 1] = 0xAA; }
    u32 ck = 0; for (u32 i = 0; i < 11 * ss; i++) { if (i == 106 || i == 107 || i == 112) continue; ck = rotsum(ck, s[i]); } u32 *t = (u32 *)(s + 11 * ss); for (u32 i = 0; i < ss / 4; i++) t[i] = ck;
    wr(d, base, 12, s, ss); e = wr(d, base + 12, 12, s, ss); free(s); if (e) return e;
    u32 *fa = calloc(used + 2, 4); fa[0] = 0xFFFFFFF8; fa[1] = 0xFFFFFFFF; for (u32 i = 0; i < bmc; i++) fa[c_bm + i] = i + 1 == bmc ? 0xFFFFFFFF : c_bm + i + 1; fa[c_up] = 0xFFFFFFFF; fa[c_rt] = 0xFFFFFFFF; u32 fb = (used + 2) * 4, fs = (fb + ss - 1) / ss; u8 *q = calloc(fs, ss); memcpy(q, fa, fb); e = wr(d, base + fo, fs, q, ss); free(q); free(fa); if (e) return e;
    u8 *bm = calloc(1, ss * spc); for (u32 i = 0; i < used; i++) bm[i >> 3] |= 1 << (i & 7); e = wr(d, base + ho, spc, bm, ss); free(bm); if (e) return e;
    u8 *cu = calloc(1, csz); memcpy(cu, up, ul); e = wr(d, base + ho + (u64)(c_up - 2) * spc, spc, cu, ss); if (e) { free(cu); return e; }
    memset(cu, 0, csz); u8 *r = cu; int ln = 0; r[0] = 0x83; const char *l = label; while (*l && ln < 11) *(u16 *)(r + 2 + ln++ * 2) = (u8)*l++; r[1] = ln; r += 32; r[0] = 0x81; *(u32 *)(r + 20) = c_bm; *(u64 *)(r + 24) = bml; r += 32; r[0] = 0x82; *(u32 *)(r + 4) = us; *(u32 *)(r + 20) = c_up; *(u64 *)(r + 24) = ul;
    e = wr(d, base + ho + (u64)(c_rt - 2) * spc, spc, cu, ss); free(cu); free(up); return e;
}
/* ----- ext2 ----- */
static int ext_mk(const char *d, u64 base, u64 total, u32 ss, const char *label, dk_cb cb) {
    if (ss > 4096) return -22; u32 spb = 4096 / ss; u64 blocks = total / spb; if (blocks < 2048 || blocks > 0xFFFFFFFFUL) return -28; u32 bpg = 32768, ipg = 2048, itb = ipg * 128 / 4096; u32 groups = (blocks + bpg - 1) / bpg; u32 last = blocks - (u64)(groups - 1) * bpg, gdb = (groups * 32 + 4095) / 4096, meta = 1 + gdb + 2 + itb;
    if (last < meta + 32) { if (groups == 1) return -28; groups--; blocks = (u64)groups * bpg; last = bpg; } u8 *b = malloc(4096); u8 *gd = calloc(gdb, 4096); u64 freeb = 0; u32 now = 1700000000;
#define BW(n, c, p) wr(d, base + (u64)(n) * spb, (u64)(c) * spb, p, ss)
    for (u32 g = 0; g < groups; g++) { u64 st = (u64)g * bpg; u32 gb = g + 1 == groups ? last : bpg, us = meta + (g == 0 ? 2 : 0); u32 *e = (u32 *)(gd + g * 32); e[0] = st + 1 + gdb; e[1] = st + 2 + gdb; e[2] = st + 3 + gdb; *(u16 *)(gd + g * 32 + 12) = gb - us; *(u16 *)(gd + g * 32 + 14) = ipg - (g == 0 ? 11 : 0); *(u16 *)(gd + g * 32 + 16) = g == 0 ? 2 : 0; freeb += gb - us; }
    u8 *sb = calloc(1, 4096); u8 *S = sb + 1024; u32 *I = (u32 *)S; I[0] = ipg * groups; I[1] = blocks; I[3] = freeb; I[4] = ipg * groups - 11; I[6] = 2; I[7] = 2; I[8] = bpg; I[9] = bpg; I[10] = ipg; I[11] = now; I[12] = now; *(u16 *)(S + 54) = 0xFFFF; *(u16 *)(S + 56) = 0xEF53; *(u16 *)(S + 58) = 1; *(u16 *)(S + 60) = 1; I[19] = 1; I[21] = 11; *(u16 *)(S + 88) = 128; I[24] = 2; for (int i = 0; i < 16; i++) S[104 + i] = rnd(); strncpy0((char *)S + 120, label, 16);
    int e = 0;
    for (u32 g = 0; g < groups && !e; g++) { u64 st = (u64)g * bpg; u32 gb = g + 1 == groups ? last : bpg, us = meta + (g == 0 ? 2 : 0); char m[48]; if (g % 8 == 0) { snprintf(m, 48, _("Grupo %u de %u"), g + 1, groups); msg(cb, 5 + g * 90 / groups, m); }
        if (g == 0) e = BW(st, 1, sb); else { u8 *c = calloc(1, 4096); memcpy(c, S, 1024); *(u16 *)(c + 90) = g; e = BW(st, 1, c); free(c); } if (!e) e = BW(st + 1, gdb, gd);
        memset(b, 0, 4096); for (u32 i = 0; i < us; i++) b[i >> 3] |= 1 << (i & 7); for (u32 i = gb; i < 32768; i++) b[i >> 3] |= 1 << (i & 7); if (!e) e = BW(st + 1 + gdb, 1, b);
        memset(b, 0, 4096); if (g == 0) for (int i = 0; i < 11; i++) b[i >> 3] |= 1 << (i & 7); for (u32 i = ipg; i < 32768; i++) b[i >> 3] |= 1 << (i & 7); if (!e) e = BW(st + 2 + gdb, 1, b);
        if (!e) e = zeros(d, base + (st + 3 + gdb) * spb, (u64)itb * spb, ss); }
    if (!e) { u64 ds = 3 + gdb + itb; u8 *t = calloc(1, 4096); u8 *in = t + 128; *(u16 *)in = 0x41ED; *(u32 *)(in + 4) = 4096; *(u32 *)(in + 8) = now; *(u32 *)(in + 12) = now; *(u32 *)(in + 16) = now; *(u16 *)(in + 26) = 3; *(u32 *)(in + 28) = 8; *(u32 *)(in + 40) = ds; u8 *lf = t + 128 * 10; *(u16 *)lf = 0x41C0; *(u32 *)(lf + 4) = 4096; *(u16 *)(lf + 26) = 2; *(u32 *)(lf + 28) = 8; *(u32 *)(lf + 40) = ds + 1; e = BW(3 + gdb, 1, t);
        u8 *r = b; memset(r, 0, 4096); static const struct { int ino; const char *n; int ty; } de[] = {{2, ".", 2}, {2, "..", 2}, {11, "lost+found", 2}}; int p = 0; for (int i = 0; i < 3; i++) { int nl = strlen(de[i].n), rl = i == 2 ? 4096 - p : (8 + nl + 3) & ~3; *(u32 *)(r + p) = de[i].ino; *(u16 *)(r + p + 4) = rl; r[p + 6] = nl; r[p + 7] = de[i].ty; memcpy(r + p + 8, de[i].n, nl); p += rl; } if (!e) e = BW(ds, 1, r);
        memset(r, 0, 4096); *(u32 *)r = 11; *(u16 *)(r + 4) = 12; r[6] = 1; r[7] = 2; r[8] = '.'; *(u32 *)(r + 12) = 2; *(u16 *)(r + 16) = 4084; r[18] = 2; r[19] = 2; r[20] = '.'; r[21] = '.'; if (!e) e = BW(ds + 1, 1, r); free(t); }
    free(b); free(gd); free(sb); return e;
}
int dk_mkfs(const char *d, u64 base, u64 total, u32 ss, int fs, const char *label, u64 hidden, dk_cb cb) {
    int e; if (fs == DK_FAT32 || fs == DK_FAT16) { fatl f; e = fat_layout(total, ss, fs == DK_FAT32 ? 32 : 16, &f); if (e) return e; e = fat_mk(d, base, &f, label, hidden, cb); } else if (fs == DK_EXFAT) e = ex_mk(d, base, total, ss, label, hidden, cb); else e = ext_mk(d, base, total, ss, label, cb);
    msg(cb, 100, e ? "Error" : "Terminado"); return e;
}
static const u8 ptype[] = {0x0C, 0x0E, 0x07, 0x83};
static void chs(u32 l, u8 *o) { if (l >= 1024UL * 255 * 63) { o[0] = 0xFE; o[1] = 0xFF; o[2] = 0xFF; return; } u32 c = l / (255 * 63); o[0] = (l / 63) % 255; o[1] = ((c >> 2) & 0xC0) | (l % 63 + 1); o[2] = c; }
static void pentry(u8 *e, int active, u8 type, u64 st, u64 n) { u32 a = st, c = MIN(n, 0xFFFFFFFFUL); e[0] = active ? 0x80 : 0; chs(a, e + 1); e[4] = type; chs(a + c - 1, e + 5); *(u32 *)(e + 8) = a; *(u32 *)(e + 12) = c; }
int dk_format_disk(const char *disk, u64 total, u32 ss, int fs, const char *label, dk_cb cb) {
    u64 st = (1UL << 20) / ss, n = MIN(total - st, 0xFFFFFFFFUL); msg(cb, 1, "Borrando tabla de particiones..."); int e = zeros(disk, 0, 64, ss); if (!e) e = zeros(disk, total - 64, 64, ss); if (e) return e;
    e = dk_mkfs(disk, st, n, ss, fs, label, st, cb); if (e) return e; u8 *b = calloc(1, ss); pentry(b + 446, 0, ptype[fs], st, n); *(u32 *)(b + 440) = serial(); b[510] = 0x55; b[511] = 0xAA; e = z_disk_write(disk, 0, 1, b); free(b); z_sync(); z_disk_rescan(disk); return e;
}
int dk_format_part(const char *disk, const zpart *p, u32 ss, int fs, const char *label, dk_cb cb) {
    int e = dk_mkfs(p->name, 0, p->sectors, ss, fs, label, p->start, cb); if (e) return e; u8 *b = malloc(ss); if (!z_disk_read(disk, 0, 1, b) && b[510] == 0x55 && b[511] == 0xAA) for (int i = 0; i < 4; i++) { u8 *x = b + 446 + i * 16; if (x[4] != 0xEE && *(u32 *)(x + 8) == p->start) { x[4] = ptype[fs]; z_disk_write(disk, 0, 1, b); break; } } free(b); z_sync(); z_disk_rescan(p->name); return 0;
}
int dk_sys_disk(char *o) { zmount m[32]; int n = z_mounts(m, 32); o[0] = 0; for (int i = 0; i < n; i++) if (!strcmp(m[i].name, "sys")) { strncpy0(o, m[i].dev, 16); char *p = strrchr(o, 'p'); if (p && isdigit(p[1]) && p > o && isdigit(p[-1])) *p = 0; return 1; } return 0; }
/* ----- instalacion: misma estructura que la memoria USB ----- */
typedef struct { u32 *fat; u32 cap, next, cs; const fatl *f; const char *d; u64 base; u16 dd, tt; int err; } ib_t;
static u64 cl_lba(ib_t *b, u32 c) { return b->base + b->f->data + (u64)(c - 2) * b->f->spc; }
static u32 ialloc(ib_t *b, const void *data, u32 len) { u32 n = MAX((len + b->cs - 1) / b->cs, 1U), first = b->next; if (first + n > b->cap) { b->err = -28; return first; } for (u32 c = first; c < first + n; c++) b->fat[c] = c + 1 == first + n ? 0x0FFFFFFF : c + 1; b->next += n; u32 secs = (len + b->f->ss - 1) / b->f->ss; u8 *t = malloc(n * b->cs); memset(t, 0, n * b->cs); memcpy(t, data, len); (void)secs; int e = wr(b->d, cl_lba(b, first), (u64)n * b->f->spc, t, b->f->ss); if (e) b->err = e; free(t); return first; }
static void dent(ib_t *b, u8 *e, const char *n83, u8 attr, u32 cl, u32 size) { memcpy(e, n83, 11); e[11] = attr; *(u16 *)(e + 14) = b->tt; *(u16 *)(e + 16) = b->dd; *(u16 *)(e + 18) = b->dd; *(u16 *)(e + 20) = cl >> 16; *(u16 *)(e + 22) = b->tt; *(u16 *)(e + 24) = b->dd; *(u16 *)(e + 26) = cl; *(u32 *)(e + 28) = size; }
int dk_install(const char *disk, u64 total, u32 ss, dk_cb cb) {
    if (ss != 512) return -22; long kl, il, el, bl, ll; msg(cb, 2, "Preparando archivos..."); u8 *K = z_readfile("/rd/inst/KERNEL.BIN", &kl), *I = z_readfile("/rd/inst/INITRD.BIN", &il), *E = z_readfile("/rd/inst/BOOTX64.EFI", &el), *B = z_readfile("/rd/inst/boot.bin", &bl), *L = z_readfile("/rd/inst/LEEME.TXT", &ll); if (!K || !I || !E || !B || !L) return -2;
    u64 st = 2048, n = MIN(total - st, 0xFFFFFFFFUL); fatl f; int e = fat_layout(n, ss, 32, &f); if (e) return e; if (bl > 2047 * 512) return -22;
    msg(cb, 5, "Borrando el inicio y el final del disco..."); if ((e = zeros(disk, 0, 2048, ss)) || (e = zeros(disk, total - 64, 64, ss))) return e; msg(cb, 10, "Creando el sistema de archivos..."); if ((e = fat_mk(disk, st, &f, "ZUNZUN", st, 0))) return e;
    ib_t b; memset(&b, 0, sizeof b); b.f = &f; b.d = disk; b.base = st; b.cs = f.spc * ss; b.cap = MIN(f.ncl + 2, (u32)(24UL << 20) / b.cs + 64); b.fat = calloc(b.cap, 4); b.next = 3; b.fat[0] = 0x0FFFFFF8; b.fat[1] = 0x0FFFFFFF; b.fat[2] = 0x0FFFFFFF; u32 t[6]; z_time(t); b.dd = (t[5] - 1980) << 9 | t[4] << 5 | t[3]; b.tt = t[2] << 11 | t[1] << 5;
    msg(cb, 25, "Copiando el kernel..."); u32 ck = ialloc(&b, K, kl); msg(cb, 40, "Copiando los programas..."); u32 ci = ialloc(&b, I, il); msg(cb, 60, "Copiando el cargador UEFI..."); u32 ce = ialloc(&b, E, el), cl = ialloc(&b, L, ll);
    u8 *dir = calloc(1, b.cs); u32 zz = b.next; dent(&b, dir, ".          ", 0x10, zz, 0); dent(&b, dir + 32, "..         ", 0x10, 0, 0); dent(&b, dir + 64, "KERNEL  BIN", 5, ck, kl); dent(&b, dir + 96, "INITRD  BIN", 5, ci, il); ialloc(&b, dir, b.cs);
    memset(dir, 0, b.cs); u32 ef = b.next, bt = ef + 1; dent(&b, dir, ".          ", 0x10, ef, 0); dent(&b, dir + 32, "..         ", 0x10, 0, 0); dent(&b, dir + 64, "BOOT       ", 0x10, bt, 0); ialloc(&b, dir, b.cs);
    memset(dir, 0, b.cs); dent(&b, dir, ".          ", 0x10, bt, 0); dent(&b, dir + 32, "..         ", 0x10, ef, 0); dent(&b, dir + 64, "BOOTX64 EFI", 0x20, ce, el); ialloc(&b, dir, b.cs);
    memset(dir, 0, b.cs); char l[11]; label11(l, "ZUNZUN"); memcpy(dir, l, 11); dir[11] = 8; dent(&b, dir + 32, "EFI        ", 0x10, ef, 0); dent(&b, dir + 64, "ZUNZUN     ", 0x10, zz, 0); dent(&b, dir + 96, "LEEME   TXT", 0x20, cl, ll); if (!e) e = wr(disk, cl_lba(&b, 2), f.spc, dir, ss); free(dir); if (b.err) return b.err; if (e) return e;
    msg(cb, 75, "Escribiendo la tabla FAT..."); u32 fs = (b.next * 4 + ss - 1) / ss; u8 *q = calloc(fs, ss); memcpy(q, b.fat, b.next * 4); e = wr(disk, st + f.rs, fs, q, ss); if (!e) e = wr(disk, st + f.rs + f.fz, fs, q, ss); free(q); if (e) return e;
    u8 *fi = calloc(1, ss); *(u32 *)fi = 0x41615252; *(u32 *)(fi + 484) = 0x61417272; *(u32 *)(fi + 488) = f.ncl - (b.next - 2); *(u32 *)(fi + 492) = b.next; fi[510] = 0x55; fi[511] = 0xAA; z_disk_write(disk, st + 1, 1, fi); z_disk_write(disk, st + 7, 1, fi); free(fi);
    msg(cb, 85, "Comprobando lo escrito..."); for (int w = 0; w < 2; w++) { u8 *src = w ? I : K; long len = w ? il : kl; u32 c0 = w ? ci : ck; u64 secs = (len + 511) / 512; u8 *r = malloc(secs * 512); long off = 0; u64 lba = cl_lba(&b, c0); while (off < (long)(secs * 512)) { int c = MIN((secs * 512 - off) / 512, 256UL); if ((e = z_disk_read(disk, lba, c, r + off))) break; off += c * 512L; lba += c; } if (!e && memcmp(src, r, len)) e = -5; free(r); if (e) return e; }
    msg(cb, 92, "Instalando el arranque..."); u32 secs = (bl + 511) / 512; u8 *bb = calloc(MAX(secs, 2U), 512); memcpy(bb, B, bl); if (memcmp(bb + 512, "ZZBOOT01", 8)) { free(bb); return -22; } ((u32 *)(bb + 520))[0] = cl_lba(&b, ck); ((u32 *)(bb + 520))[1] = (kl + 511) / 512; ((u32 *)(bb + 520))[2] = cl_lba(&b, ci); ((u32 *)(bb + 520))[3] = (il + 511) / 512; *(u32 *)(bb + 440) = serial(); pentry(bb + 446, 1, 0x0C, st, n); bb[510] = 0x55; bb[511] = 0xAA;
    e = wr(disk, 1, secs - 1, bb + 512, ss); if (!e) e = z_disk_write(disk, 0, 1, bb); free(bb); free(b.fat); z_sync(); if (!e) { z_disk_rescan(disk); char mn[24], dst[60]; snprintf(mn, 24, "%sp1", disk); z_set_ro(mn, 0); snprintf(dst, 60, "/%s/PROGRAMAS", mn); zdirent de; if (!z_stat("/sys/PROGRAMAS", &de)) { msg(cb, 96, "Copiando apps web..."); z_mkdir(dst); char a[360], c[360]; for (int i = 0; !z_readdir("/sys/PROGRAMAS", i, &de); i++) if (!de.is_dir) { snprintf(a, 360, "/sys/PROGRAMAS/%s", de.name); snprintf(c, 360, "%s/%s", dst, de.name); long l; char *dt = z_readfile(a, &l); if (dt) { z_writefile(c, dt, l); free(dt); } } } z_set_ro(mn, 1); } msg(cb, 100, e ? "Error" : "Instalado"); return e;
}
/* Sustituye kernel, programas y cargador UEFI de forma segura: primero se escriben y
 * comprueban copias nuevas, luego se apunta el arranque a ellas y al final se renombran. */
void dk_root(const char *d, char *o) { strncpy0(o, d, 16); char *p = strrchr(o, 'p'); if (p && p > o && isdigit(p[1]) && isdigit(p[-1])) *p = 0; }
static int put_contig(const char *disk, u64 start, const char *path, const u8 *d, long n, u64 *lba) { z_unlink(path); int e = z_fat_contig(path, n, lba); if (e) return e; int f = z_open(path, O_WRITE); if (f < 0) return f; long w = z_write(f, d, n); z_close(f); z_sync(); if (w != n) return -5; u64 s = (n + 511) / 512; u8 *r = malloc(s * 512); u64 off = 0; while (off < s && !e) { int c = MIN(s - off, 256UL); e = z_disk_read(disk, start + *lba + off, c, r + off * 512); off += c; } if (!e && memcmp(r, d, n)) e = -5; free(r); return e; }
int dk_update_system(const char *mn, const char *dev, const char *kp, const char *ip, const char *ep, const char *bp, dk_cb cb) {
    char disk[16], p[80], q[80]; dk_root(dev, disk); zpart pt[16]; int np = z_parts(disk, pt, 16); u64 start = 0; int found = 0; for (int i = 0; i < np; i++) if (!strcmp(pt[i].name, dev)) { start = pt[i].start; found = 1; } if (!found) return -22; u8 *s1 = malloc(512); int e = z_disk_read(disk, 1, 1, s1); if (e || memcmp(s1, "ZZBOOT01", 8)) { free(s1); return e ? e : -22; }
    long kl, il, el = 0, bl = 0; msg(cb, 5, "Leyendo el sistema nuevo..."); u8 *K = z_readfile(kp, &kl), *I = z_readfile(ip, &il), *E = ep ? z_readfile(ep, &el) : 0, *B = bp ? z_readfile(bp, &bl) : 0; if (!K || !I || memcmp(K, "ZUNKERN1", 8) || memcmp(I, "ZRD1", 4)) { free(s1); return -22; } u64 kb, ib;
    z_set_ro(mn, 0); snprintf(p, 80, "/%s/ZUNZUN/KERNEL.NEW", mn); msg(cb, 20, "Escribiendo el kernel..."); if ((e = put_contig(disk, start, p, K, kl, &kb))) return e; snprintf(p, 80, "/%s/ZUNZUN/INITRD.NEW", mn); msg(cb, 40, "Escribiendo los programas..."); if ((e = put_contig(disk, start, p, I, il, &ib))) return e; msg(cb, 65, "Actualizando el arranque...");
    u32 f[4] = {(u32)(start + kb), (u32)((kl + 511) / 512), (u32)(start + ib), (u32)((il + 511) / 512)}; if (B && bl > 520 && !memcmp(B + 512, "ZZBOOT01", 8)) { u32 secs = (bl + 511) / 512; u8 *b = calloc(MAX(secs, 2U), 512); memcpy(b, B, bl); memcpy(b + 520, f, 16); e = wr(disk, 1, secs - 1, b + 512, 512); u8 *m = malloc(512); if (!e) e = z_disk_read(disk, 0, 1, m); if (!e) { memcpy(m, b, 440); e = z_disk_write(disk, 0, 1, m); } free(m); free(b); } else { memcpy(s1 + 8, f, 16); e = z_disk_write(disk, 1, 1, s1); } free(s1); if (e) return e; z_sync(); msg(cb, 80, "Activando...");
    snprintf(p, 80, "/%s/ZUNZUN/KERNEL.BIN", mn); z_unlink(p); snprintf(q, 80, "/%s/ZUNZUN/KERNEL.NEW", mn); if ((e = z_rename(q, p))) return e; snprintf(p, 80, "/%s/ZUNZUN/INITRD.BIN", mn); z_unlink(p); snprintf(q, 80, "/%s/ZUNZUN/INITRD.NEW", mn); if ((e = z_rename(q, p))) return e; if (E) { snprintf(q, 80, "/%s/EFI/BOOT/BOOTX64.NEW", mn); if (!(e = z_writefile(q, E, el))) { snprintf(p, 80, "/%s/EFI/BOOT/BOOTX64.EFI", mn); z_unlink(p); e = z_rename(q, p); } } free(K); free(I); free(E); free(B); z_sync(); msg(cb, 100, e ? "Error" : "Sistema actualizado"); return e;
}
