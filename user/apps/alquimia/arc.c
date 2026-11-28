/* Alquimia: archivadores (lo que era PRESSA y PRESSA PRO).
   Crear: ZIP (deflate o guardado, con contrasena opcional WinZip AES-256 AE-2), TAR (ustar con nombres largos GNU),
   TAR.GZ y GZ. Todo por flujo: cada archivo se lee en trozos de 1 MB, asi que no hace falta tenerlo entero en memoria.
   El ZIP lleva descriptor de datos tras cada archivo (no se vuelve atras en el archivo que se escribe).
   Abrir: ZIP (guardado y deflate; AES-128/192/256 AE-1 y AE-2; ZipCrypto; ZIP64; nombres UTF-8 o CP437),
   TAR (ustar, GNU y pax), TAR.GZ/TGZ y GZ. Se comprueba el CRC de cada archivo. */
enum { AF_ZIP, AF_TGZ, AF_TAR, AF_GZ, AF_NONE };
static const char *const AF_EXT[] = {".zip", ".tar.gz", ".tar", ".gz"};
typedef int (*prog_fn)(long done, long total, const char *what);   /* devuelve distinto de 0 para cancelar */
#define CHUNK (1 << 20)

/* ---------- lista de lo que se mete ---------- */
typedef struct { char *src; char *name; long size; int dir; const u8 *mem; } aitem;   /* mem: datos en memoria (reempaquetar) */
typedef struct { aitem *v; int n, cap; long total; } alist;
static void al_add(alist *l, const char *src, const char *name, long size, int dir, const u8 *mem) {
    if (l->n == l->cap) { l->cap = l->cap * 2 + 64; l->v = realloc(l->v, l->cap * sizeof *l->v); }
    aitem *a = &l->v[l->n++]; a->src = src ? strdup(src) : 0; a->name = strdup(name); a->size = size; a->dir = dir; a->mem = mem; if (!dir) l->total += size; }
static void al_free(alist *l) { for (int i = 0; i < l->n; i++) { free(l->v[i].src); free(l->v[i].name); } free(l->v); memset(l, 0, sizeof *l); }
static void al_walk(alist *l, const char *path, const char *name, int depth) {
    zdirent d; if (z_stat(path, &d)) return; if (!d.is_dir) { al_add(l, path, name, d.size, 0, 0); return; }
    char nm[600]; snprintf(nm, 600, "%s/", name); al_add(l, path, nm, 0, 1, 0); if (depth > 24) return;
    for (int i = 0; !z_readdir(path, i, &d); i++) { if (!strcmp(d.name, ".") || !strcmp(d.name, "..")) continue; if (!strcmp(d.name, ".PAPELERA")) continue;
        char p[600], n2[600]; snprintf(p, 600, "%s/%s", path, d.name); snprintf(n2, 600, "%s/%s", name, d.name); al_walk(l, p, n2, depth + 1); } }
static const char *base_name(const char *p) { const char *s = strrchr(p, '/'); return s ? s + 1 : p; }
/* anade un archivo o una carpeta entera (con su nombre como raiz dentro del archivo) */
static void al_path(alist *l, const char *path) { al_walk(l, path, base_name(path), 0); }

/* ---------- escritura con bufer ---------- */
typedef struct { int fd; long n; int err; } wout;
static void wo(wout *w, const void *p, long n) { if (w->err || n <= 0) return; long r = z_write(w->fd, p, n); if (r != n) w->err = r < 0 ? (int)r : -28; else w->n += n; }
static void wo16(wout *w, u32 v) { u8 t[2] = {v, v >> 8}; wo(w, t, 2); }
static void wo32(wout *w, u32 v) { u8 t[4] = {v, v >> 8, v >> 16, v >> 24}; wo(w, t, 4); }
static void s16(sb_t *b, u32 v) { u8 t[2] = {v, v >> 8}; sb_add(b, t, 2); }
static void s32(sb_t *b, u32 v) { u8 t[4] = {v, v >> 8, v >> 16, v >> 24}; sb_add(b, t, 4); }
static void dos_now(u16 *tm, u16 *dt) { u32 t[6]; z_time(t); int y = t[5] < 1980 ? 1980 : t[5]; *tm = t[2] << 11 | t[1] << 5 | t[0] / 2; *dt = (y - 1980) << 9 | t[4] << 5 | t[3]; }
static u32 unix_now(void) { u32 t[6]; z_time(t); int y = t[5], m = t[4], d = t[3]; if (m < 3) { y--; m += 12; } long days = 365L * y + y / 4 - y / 100 + y / 400 + (153 * (m - 3) + 2) / 5 + d - 719469; return (u32)(days * 86400 + t[2] * 3600 + t[1] * 60 + t[0]); }
/* los formatos que ya vienen comprimidos se guardan sin deflate: no ganan nada y se tarda menos */
static int already_packed(const char *n) { static const char *E[] = {".jpg", ".jpeg", ".png", ".gif", ".webp", ".mp3", ".mp4", ".m4a", ".ogg", ".avi", ".mkv", ".mov", ".zip", ".gz", ".tgz", ".7z", ".rar", ".bz2", ".xz", ".zst", ".docx", ".xlsx", ".pptx", ".odt", ".ods", ".epub", ".jar", ".apk"}; for (unsigned i = 0; i < sizeof E / sizeof *E; i++) if (z_ends(n, E[i])) return 1; return 0; }

/* fuente de datos de un elemento: archivo en disco o memoria */
typedef struct { int fd; const u8 *mem; long left, at; } isrc;
static int is_open(isrc *s, const aitem *a) { s->mem = a->mem; s->left = a->size; s->at = 0; s->fd = -1; if (a->mem) return 0; s->fd = z_open(a->src, O_READ); return s->fd < 0 ? s->fd : 0; }
static long is_read(isrc *s, u8 *b, long n) { if (s->left <= 0) return 0; n = MIN(n, s->left); long r; if (s->mem) { memcpy(b, s->mem + s->at, n); r = n; } else r = z_read(s->fd, b, n); if (r > 0) { s->left -= r; s->at += r; } return r; }
static void is_close(isrc *s) { if (s->fd >= 0) z_close(s->fd); s->fd = -1; }

typedef struct { long in, out; int files; char err[160]; } astat;
static int ar_err(astat *st, const char *m, const char *x) { snprintf(st->err, 160, "%s%s%s", m, x ? ": " : "", x ? x : ""); return -1; }

/* ---------- ZIP ---------- */
static int zip_write(wout *w, alist *L, int level, const char *pw, prog_fn pr, astat *st) {
    sb_t cd = {0}; sb_s(&cd, ""); u16 tm, dt; dos_now(&tm, &dt); u8 *buf = malloc(CHUNK); long done = 0; int aes = pw && pw[0];
    if (!buf) return ar_err(st, tr("Falta memoria"), 0);
    for (int i = 0; i < L->n; i++) {
        aitem *a = &L->v[i]; int nl = strlen(a->name); long off = w->n; if (off > 0xFFFFFFF0L) { free(buf); free(cd.p); return ar_err(st, tr("El ZIP pasaría de 4 GB (ZIP64 no se crea)"), 0); }
        int meth = a->dir || !a->size || already_packed(a->name) ? 0 : 8, enc = aes && !a->dir; u16 fl = 0x800 | (a->dir ? 0 : 8) | (enc ? 1 : 0);
        u8 ext[11] = {0x01, 0x99, 7, 0, 2, 0, 'A', 'E', 3, (u8)meth, 0};
        wo32(w, 0x04034B50); wo16(w, enc ? 51 : 20); wo16(w, fl); wo16(w, enc ? 99 : meth); wo16(w, tm); wo16(w, dt); wo32(w, 0); wo32(w, 0); wo32(w, 0); wo16(w, nl); wo16(w, enc ? 11 : 0); wo(w, a->name, nl); if (enc) wo(w, ext, 11);
        u32 crc = 0; long cs = 0, us = 0;
        if (!a->dir) {
            isrc s; int e = is_open(&s, a); if (e) { free(buf); free(cd.p); return ar_err(st, z_errstr(e), a->name); }
            wza_t wz; if (enc) { u8 salt[16], pv[2]; zc_random(salt, 16); wza_init(&wz, pw, salt, 3, pv); wo(w, salt, 16); wo(w, pv, 2); cs += 18; }
            dfl_t *d = meth == 8 ? dfl_new(level) : 0; if (meth == 8 && !d) { is_close(&s); free(buf); free(cd.p); return ar_err(st, tr("Falta memoria"), 0); }
            for (;;) { long r = is_read(&s, buf, CHUNK), dn = 0; if (r < 0) { e = (int)r; break; }
                if (r > 0) { crc = z_crc_add(crc, buf, r); us += r; }
                u8 *outp; long on;
                if (d) { if (r > 0) dfl_put(d, buf, r); else dfl_end(d); outp = d->out; on = d->on; } else { outp = buf; on = r; }
                if (enc) wza_enc(&wz, outp, on); wo(w, outp, on); cs += on; if (d) d->on = 0; dn = r;
                done += dn; if (pr && pr(done, L->total, a->name)) { e = -125; break; } if (r == 0 || w->err) break; }
            is_close(&s); dfl_free(d);
            if (e) { free(buf); free(cd.p); return e == -125 ? ar_err(st, tr("Cancelado"), 0) : ar_err(st, z_errstr(e), a->name); }
            if (enc) { u8 tag[10]; wza_tag(&wz, tag); wo(w, tag, 10); cs += 10; }
            if (us > 0xFFFFFFFFL || cs > 0xFFFFFFFFL) { free(buf); free(cd.p); return ar_err(st, tr("Un archivo pasa de 4 GB (ZIP64 no se crea)"), a->name); }
            wo32(w, 0x08074B50); wo32(w, enc ? 0 : crc); wo32(w, cs); wo32(w, us); st->files++; }
        s32(&cd, 0x02014B50); s16(&cd, enc ? 0x033F : 0x0314); s16(&cd, enc ? 51 : 20); s16(&cd, fl); s16(&cd, enc ? 99 : meth); s16(&cd, tm); s16(&cd, dt); s32(&cd, enc ? 0 : crc); s32(&cd, cs); s32(&cd, us);
        s16(&cd, nl); s16(&cd, enc ? 11 : 0); s16(&cd, 0); s16(&cd, 0); s16(&cd, 0); s32(&cd, a->dir ? 0x41ED0010 : 0x81A40000); s32(&cd, off); sb_add(&cd, a->name, nl); if (enc) sb_add(&cd, ext, 11);
        st->in += us; if (w->err) break; }
    long cdo = w->n; wo(w, cd.p, cd.n); wo32(w, 0x06054B50); wo16(w, 0); wo16(w, 0); wo16(w, L->n); wo16(w, L->n); wo32(w, cd.n); wo32(w, cdo); wo16(w, 0);
    free(cd.p); free(buf); if (L->n > 65535) return ar_err(st, tr("Demasiados archivos para un ZIP sin ZIP64"), 0); return w->err ? ar_err(st, z_errstr(w->err), 0) : 0; }

/* ---------- TAR (y TAR.GZ por encima de un deflate) ---------- */
typedef struct { wout *w; dfl_t *d; u32 crc; long raw; } tout;
static void to(tout *t, const void *p, long n) { if (n <= 0) return; t->raw += n; if (!t->d) { wo(t->w, p, n); return; } t->crc = z_crc_add(t->crc, p, n); dfl_put(t->d, p, n); if (t->d->on > CHUNK) { wo(t->w, t->d->out, t->d->on); t->d->on = 0; } }
static void tar_oct(u8 *h, int at, int len, u64 v) { for (int i = len - 2; i >= 0; i--) { h[at + i] = '0' + (v & 7); v >>= 3; } h[at + len - 1] = 0; }
static void tar_hdr(tout *t, const char *name, long size, int type, u32 mt) {
    u8 h[512]; int nl = strlen(name);
    if (nl > 99) {   /* nombre largo: primero una cabecera GNU ././@LongLink con el nombre como datos */
        memset(h, 0, 512); strcpy((char *)h, "././@LongLink"); tar_oct(h, 100, 8, 0644); tar_oct(h, 108, 8, 0); tar_oct(h, 116, 8, 0); tar_oct(h, 124, 12, nl + 1); tar_oct(h, 136, 12, mt); h[156] = 'L'; memcpy(h + 257, "ustar  ", 8);
        memset(h + 148, ' ', 8); u32 s = 0; for (int i = 0; i < 512; i++) s += h[i]; tar_oct(h, 148, 7, s); h[155] = ' '; to(t, h, 512);
        long pad = (512 - (nl + 1) % 512) % 512; to(t, name, nl); u8 z[512] = {0}; to(t, z, 1 + pad); }
    memset(h, 0, 512); memcpy(h, name, MIN(nl, 100)); tar_oct(h, 100, 8, type == '5' ? 0755 : 0644); tar_oct(h, 108, 8, 0); tar_oct(h, 116, 8, 0); tar_oct(h, 124, 12, size); tar_oct(h, 136, 12, mt); h[156] = type;
    memcpy(h + 257, "ustar", 6); h[263] = '0'; h[264] = '0'; memset(h + 148, ' ', 8); u32 s = 0; for (int i = 0; i < 512; i++) s += h[i]; tar_oct(h, 148, 7, s); h[155] = ' '; to(t, h, 512); }
static int tar_write(wout *w, alist *L, int gz, int level, prog_fn pr, astat *st) {
    tout t = {w, 0, 0, 0}; if (gz) { t.d = dfl_new(level); if (!t.d) return ar_err(st, tr("Falta memoria"), 0); u8 g[10] = {0x1F, 0x8B, 8, 0, 0, 0, 0, 0, level >= 3 ? 2 : level <= 1 ? 4 : 0, 3}; u32 mt = unix_now(); g[4] = mt; g[5] = mt >> 8; g[6] = mt >> 16; g[7] = mt >> 24; wo(w, g, 10); }
    u8 *buf = malloc(CHUNK); long done = 0; u32 mt = unix_now(); int e = 0; if (!buf) { dfl_free(t.d); return ar_err(st, tr("Falta memoria"), 0); }
    for (int i = 0; i < L->n && !e && !w->err; i++) { aitem *a = &L->v[i];
        tar_hdr(&t, a->name, a->dir ? 0 : a->size, a->dir ? '5' : '0', mt); if (a->dir) continue;
        isrc s; e = is_open(&s, a); if (e) { ar_err(st, z_errstr(e), a->name); break; } long left = a->size;
        while (left > 0) { long r = is_read(&s, buf, MIN((long)CHUNK, left)); if (r <= 0) { memset(buf, 0, MIN((long)CHUNK, left)); r = MIN((long)CHUNK, left); }   /* si el archivo encoge mientras se lee, se rellena */
            to(&t, buf, r); left -= r; done += r; if (pr && pr(done, L->total, a->name)) { e = -125; break; } }
        is_close(&s); long pad = (512 - a->size % 512) % 512; memset(buf, 0, 512); to(&t, buf, pad); st->in += a->size; st->files++; }
    if (!e) { memset(buf, 0, 1024); to(&t, buf, 1024); }
    if (t.d) { if (!e) { dfl_end(t.d); wo(w, t.d->out, t.d->on); wo32(w, t.crc); wo32(w, (u32)t.raw); } dfl_free(t.d); }
    free(buf); if (e == -125) return ar_err(st, tr("Cancelado"), 0); if (e) return -1; return w->err ? ar_err(st, z_errstr(w->err), 0) : 0; }
/* ---------- GZ de un solo archivo ---------- */
static int gz_write(wout *w, aitem *a, int level, prog_fn pr, astat *st) {
    if (a->dir) return ar_err(st, tr("GZ guarda un solo archivo, no carpetas: usa TAR.GZ"), 0);
    const char *bn = base_name(a->name); u8 g[10] = {0x1F, 0x8B, 8, 8, 0, 0, 0, 0, level >= 3 ? 2 : level <= 1 ? 4 : 0, 3}; u32 mt = unix_now(); g[4] = mt; g[5] = mt >> 8; g[6] = mt >> 16; g[7] = mt >> 24; wo(w, g, 10); wo(w, bn, strlen(bn) + 1);
    dfl_t *d = dfl_new(level); u8 *buf = malloc(CHUNK); if (!d || !buf) { dfl_free(d); free(buf); return ar_err(st, tr("Falta memoria"), 0); }
    isrc s; int e = is_open(&s, a); u32 crc = 0; long tot = 0;
    while (!e) { long r = is_read(&s, buf, CHUNK); if (r < 0) { e = (int)r; break; } if (!r) break; crc = z_crc_add(crc, buf, r); tot += r; dfl_put(d, buf, r); wo(w, d->out, d->on); d->on = 0; if (pr && pr(tot, a->size, bn)) e = -125; }
    is_close(&s); if (!e) { dfl_end(d); wo(w, d->out, d->on); wo32(w, crc); wo32(w, (u32)tot); st->in += tot; st->files = 1; }
    dfl_free(d); free(buf); if (e == -125) return ar_err(st, tr("Cancelado"), 0); if (e) return ar_err(st, z_errstr(e), a->name); return w->err ? ar_err(st, z_errstr(w->err), 0) : 0; }

/* crea el archivo: se escribe aparte (.parcial) y se cambia de nombre al terminar */
static int arc_create(int fmt, alist *L, const char *out, int level, const char *pw, prog_fn pr, astat *st) {
    memset(st, 0, sizeof *st); if (!L->n) return ar_err(st, tr("No hay nada que comprimir"), 0);
    if (fmt == AF_GZ && L->n != 1) return ar_err(st, tr("GZ guarda un solo archivo: elige TAR.GZ o ZIP para varios"), 0);
    char tmp[620]; snprintf(tmp, 620, "%s.parcial", out); z_unlink(tmp); int fd = z_open(tmp, O_WRITE | O_CREATE | O_TRUNC); if (fd < 0) return ar_err(st, z_errstr(fd), out);
    wout w = {fd, 0, 0}; int r = fmt == AF_ZIP ? zip_write(&w, L, level, pw, pr, st) : fmt == AF_GZ ? gz_write(&w, &L->v[0], level, pr, st) : tar_write(&w, L, fmt == AF_TGZ, level, pr, st);
    z_close(fd); if (r) { z_unlink(tmp); z_sync(); return r; }
    z_unlink(out); int e = z_rename(tmp, out); z_sync(); if (e) { z_unlink(tmp); return ar_err(st, z_errstr(e), out); } st->out = w.n; return 0; }

/* ---------- lectura ---------- */
typedef struct { char *name; long size, csize, off; u32 crc; u8 dir, meth, enc, ast, aver, ameth; u16 fl, dtime; } aent;   /* enc 1 ZipCrypto, 2 AES */
typedef struct { int kind, fd; long fsize; u8 *mem; long mn; aent *e; int n, cap; int encrypted; char err[160]; char path[400]; } arch;
static int ar_rd(arch *a, long off, void *b, long n) { if (a->mem) { if (off < 0 || off + n > a->mn) return -1; memcpy(b, a->mem + off, n); return 0; } if (z_seek(a->fd, off, 0) != off) return -1; long r = z_read(a->fd, b, n); return r == n ? 0 : -1; }
static aent *ae_new(arch *a) { if (a->n == a->cap) { a->cap = a->cap * 2 + 64; a->e = realloc(a->e, a->cap * sizeof *a->e); } aent *x = &a->e[a->n++]; memset(x, 0, sizeof *x); return x; }
static void arch_close(arch *a) { for (int i = 0; i < a->n; i++) free(a->e[i].name); free(a->e); free(a->mem); if (a->fd >= 0) z_close(a->fd); memset(a, 0, sizeof *a); a->fd = -1; }
#define G16(p) ((u32)(p)[0] | (u32)(p)[1] << 8)
#define G32(p) ((u32)(p)[0] | (u32)(p)[1] << 8 | (u32)(p)[2] << 16 | (u32)(p)[3] << 24)
static u64 G64(const u8 *p) { return (u64)G32(p) | (u64)G32(p + 4) << 32; }
/* nombres de los ZIP antiguos de Windows (CP437) a UTF-8 */
static const u16 CP437[128] = {0xC7,0xFC,0xE9,0xE2,0xE4,0xE0,0xE5,0xE7,0xEA,0xEB,0xE8,0xEF,0xEE,0xEC,0xC4,0xC5,0xC9,0xE6,0xC6,0xF4,0xF6,0xF2,0xFB,0xF9,0xFF,0xD6,0xDC,0xA2,0xA3,0xA5,0x20A7,0x192,
    0xE1,0xED,0xF3,0xFA,0xF1,0xD1,0xAA,0xBA,0xBF,0x2310,0xAC,0xBD,0xBC,0xA1,0xAB,0xBB,0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255D,0x255C,0x255B,0x2510,
    0x2514,0x2534,0x252C,0x251C,0x2500,0x253C,0x255E,0x255F,0x255A,0x2554,0x2569,0x2566,0x2560,0x2550,0x256C,0x2567,0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256B,0x256A,0x2518,0x250C,0x2588,0x2584,0x258C,0x2590,0x2580,
    0x3B1,0xDF,0x393,0x3C0,0x3A3,0x3C3,0xB5,0x3C4,0x3A6,0x398,0x3A9,0x3B4,0x221E,0x3C6,0x3B5,0x2229,0x2261,0xB1,0x2265,0x2264,0x2320,0x2321,0xF7,0x2248,0xB0,0x2219,0xB7,0x221A,0x207F,0xB2,0x25A0,0xA0};
static int valid_utf8(const u8 *p, int n) { int multi = 0; for (int i = 0; i < n;) { u8 c = p[i]; int k = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1; if (k < 0 || i + k >= n + (k ? 0 : 1)) return 0; for (int j = 1; j <= k; j++) if ((p[i + j] & 0xC0) != 0x80) return 0; multi |= k > 0; i += k + 1; } return multi; }
/* sin la marca UTF-8, un nombre que es UTF-8 valido con letras no ASCII se toma como UTF-8 (lo hacen Info-ZIP en Linux y macOS) */
static char *zname(const u8 *p, int n, int utf8) { if (!utf8 && valid_utf8(p, n)) utf8 = 1; char *o = malloc(n * 3 + 1), *q = o; for (int i = 0; i < n; i++) { u8 c = p[i]; if (c < 128 || utf8) *q++ = c ? c : '_'; else q += utf8_put(CP437[c - 128], q); } *q = 0; return o; }
static int zip_open(arch *a) {
    long fs = a->fsize; long tl = MIN(fs, 65557L); u8 *t = malloc(tl + 1); if (!t || ar_rd(a, fs - tl, t, tl)) { free(t); snprintf(a->err, 160, "%s", tr("No se puede leer el archivo")); return -1; }
    long e = tl - 22; while (e >= 0 && G32(t + e) != 0x06054B50) e--; if (e < 0) { free(t); snprintf(a->err, 160, "%s", tr("No es un ZIP o está incompleto")); return -1; }
    u64 cnt = G16(t + e + 10), cds = G32(t + e + 12), cdo = G32(t + e + 16);
    if (e >= 20 && G32(t + e - 20) == 0x07064B50) { u64 z64 = G64(t + e - 20 + 8); u8 h[56]; if (!ar_rd(a, (long)z64, h, 56) && G32(h) == 0x06064B50) { cnt = G64(h + 32); cds = G64(h + 40); cdo = G64(h + 48); } }
    free(t); if (cds > (64u << 20) || (long)(cdo + cds) > fs) { snprintf(a->err, 160, "%s", tr("El directorio del ZIP está dañado")); return -1; }
    u8 *cd = malloc(cds + 1); if (!cd || ar_rd(a, (long)cdo, cd, (long)cds)) { free(cd); snprintf(a->err, 160, "%s", tr("No se puede leer el archivo")); return -1; }
    long p = 0; for (u64 i = 0; i < cnt && p + 46 <= (long)cds; i++) { const u8 *h = cd + p; if (G32(h) != 0x02014B50) break;
        int nl = G16(h + 28), xl = G16(h + 30), cl = G16(h + 32); if (p + 46 + nl + xl + cl > (long)cds) break; aent *x = ae_new(a);
        x->fl = G16(h + 8); x->meth = (u8)MIN(G16(h + 10), 255u); x->dtime = G16(h + 12); x->crc = G32(h + 16); x->csize = G32(h + 20); x->size = G32(h + 24); x->off = G32(h + 42);
        x->name = zname(h + 46, nl, x->fl & 0x800);
        for (const u8 *q = h + 46 + nl, *qe = q + xl; q + 4 <= qe;) { int id = G16(q), sz = G16(q + 2); const u8 *v = q + 4; if (v + sz > qe) break;
            if (id == 0x0001) { int k = 0; if (G32(h + 24) == 0xFFFFFFFF && k + 8 <= sz) { x->size = (long)G64(v + k); k += 8; } if (G32(h + 20) == 0xFFFFFFFF && k + 8 <= sz) { x->csize = (long)G64(v + k); k += 8; } if (G32(h + 42) == 0xFFFFFFFF && k + 8 <= sz) x->off = (long)G64(v + k); }
            else if (id == 0x9901 && sz >= 7) { x->aver = G16(v); x->ast = v[4]; x->ameth = G16(v + 5); }
            else if (id == 0x7075 && sz > 5) { free(x->name); x->name = zname(v + 5, sz - 5, 1); }
            q = v + sz; }
        if (x->fl & 1) { x->enc = x->meth == 99 ? 2 : 1; a->encrypted = 1; }
        int L = strlen(x->name); x->dir = L && x->name[L - 1] == '/'; p += 46 + nl + xl + cl; }
    free(cd); return 0; }
static int strnlen0(const char *s, int n) { int i = 0; while (i < n && s[i]) i++; return i; }
/* TAR desde la posicion base del archivo o de la memoria */
static long tar_num(const u8 *p, int n) { if (p[0] & 0x80) { long v = 0; for (int i = 1; i < n; i++) v = v << 8 | p[i]; return v; } long v = 0; for (int i = 0; i < n && p[i]; i++) if (p[i] >= '0' && p[i] <= '7') v = v * 8 + (p[i] - '0'); return v; }
static int tar_ok(const u8 *h) { u32 s = 0; for (int i = 0; i < 512; i++) s += i >= 148 && i < 156 ? ' ' : h[i]; return (long)s == tar_num(h + 148, 8); }
static int tar_open(arch *a, long total) {
    long o = 0; char *lname = 0; u8 h[512]; int any = 0;
    while (o + 512 <= total) { if (ar_rd(a, o, h, 512)) break; int zero = 1; for (int i = 0; i < 512; i++) if (h[i]) { zero = 0; break; } if (zero) break;
        if (!tar_ok(h)) { if (!any) { snprintf(a->err, 160, "%s", tr("No es un TAR válido")); free(lname); return -1; } break; } any = 1;
        long sz = tar_num(h + 124, 12), dpos = o + 512; int ty = h[156];
        if (ty == 'L' || ty == 'x') {   /* nombre largo GNU o cabecera pax con path= */
            u8 *d = malloc(sz + 1); if (d && !ar_rd(a, dpos, d, sz)) { d[sz] = 0; if (ty == 'L') { free(lname); lname = strdup((char *)d); }
                else for (char *q = (char *)d; q < (char *)d + sz;) { long rl = atoi(q); char *sp = strchr(q, ' '); if (rl <= 0 || !sp) break; if (!strncmp(sp + 1, "path=", 5)) { char *v = sp + 6, *e = q + rl - 1; free(lname); lname = malloc(e - v + 1); memcpy(lname, v, e - v); lname[e - v] = 0; } q += rl; } }
            free(d); o = dpos + (sz + 511) / 512 * 512; continue; }
        if (ty == 'g') { o = dpos + (sz + 511) / 512 * 512; continue; }
        aent *x = ae_new(a); if (lname) { x->name = lname; lname = 0; } else { char nm[260]; int pl = 0; if (!memcmp(h + 257, "ustar", 5) && h[345]) { pl = strnlen0((char *)h + 345, 155); memcpy(nm, h + 345, pl); nm[pl++] = '/'; } int nl = strnlen0((char *)h, 100); memcpy(nm + pl, h, nl); nm[pl + nl] = 0; x->name = strdup(nm); }
        x->dir = ty == '5'; x->size = ty == '0' || ty == 0 || ty == '7' ? sz : 0; x->csize = x->size; x->off = dpos; x->meth = 0;
        if (ty != '0' && ty != 0 && ty != '7' && ty != '5') { free(x->name); a->n--; }   /* enlaces y dispositivos: no se extraen */
        else if (x->dir) { int L = strlen(x->name); if (L && x->name[L - 1] != '/') { x->name = realloc(x->name, L + 2); strcat(x->name, "/"); } }
        o = dpos + (sz + 511) / 512 * 512; }
    free(lname); return 0; }
/* GZ: descomprime entero en memoria (un solo miembro, que es lo que hacen gzip y todos los programas) */
static u8 *gz_unpack(const u8 *d, long n, long *on, char *fname, int fcap, char *err) {
    if (fname) fname[0] = 0; if (n < 18 || d[0] != 0x1F || d[1] != 0x8B) { strcpy(err, tr("No es un GZ")); return 0; }
    if (d[2] != 8) { strcpy(err, tr("GZ con un método desconocido")); return 0; } int f = d[3]; long q = 10;
    if (f & 4) q += 2 + G16(d + q); if (f & 8) { long s = q; while (q < n && d[q]) q++; if (fname) { int l = (int)MIN((long)fcap - 1, q - s); memcpy(fname, d + s, l); fname[l] = 0; } q++; } if (f & 16) { while (q < n && d[q]) q++; q++; } if (f & 2) q += 2;
    if (q >= n - 8) { strcpy(err, tr("El GZ está dañado")); return 0; }
    u32 isz = G32(d + n - 4), crc = G32(d + n - 8); u8 *o = malloc((long)isz + 1); if (!o) { strcpy(err, tr("Falta memoria")); return 0; }
    long r = z_inflate(d + q, n - q - 8, o, isz); if (r != (long)isz) { free(o); strcpy(err, tr("El GZ está dañado")); return 0; }
    if (z_crc32(o, r) != crc) { free(o); strcpy(err, tr("El CRC no coincide: archivo dañado")); return 0; }
    *on = r; return o; }
static int kind_of(const u8 *h, int n, const char *path) {
    if (n >= 4 && G32(h) == 0x04034B50) return AF_ZIP; if (n >= 4 && (G32(h) == 0x06054B50)) return AF_ZIP;
    if (n >= 3 && h[0] == 0x1F && h[1] == 0x8B && h[2] == 8) return z_ends(path, ".tgz") || z_ends(path, ".tar.gz") ? AF_TGZ : AF_GZ;
    if (n >= 512 && tar_ok(h)) return AF_TAR; if (z_ends(path, ".zip")) return AF_ZIP; return AF_NONE; }
static int arch_open(arch *a, const char *path) {
    memset(a, 0, sizeof *a); a->fd = -1; strncpy0(a->path, path, 400); int fd = z_open(path, O_READ); if (fd < 0) { snprintf(a->err, 160, "%s", z_errstr(fd)); return -1; }
    a->fd = fd; a->fsize = z_seek(fd, 0, 2); u8 h[512]; memset(h, 0, 512); z_seek(fd, 0, 0); int hn = (int)z_read(fd, h, 512); a->kind = kind_of(h, hn, path);
    if (a->kind == AF_ZIP) return zip_open(a);
    if (a->kind == AF_TAR) return tar_open(a, a->fsize);
    if (a->kind == AF_GZ || a->kind == AF_TGZ) {
        u8 *d = malloc(a->fsize + 1); if (!d || ar_rd(a, 0, d, a->fsize)) { free(d); snprintf(a->err, 160, "%s", tr("No se puede leer el archivo")); return -1; }
        char fn[200]; long n; u8 *u = gz_unpack(d, a->fsize, &n, fn, 200, a->err); free(d); if (!u) return -1;
        z_close(a->fd); a->fd = -1; a->mem = u; a->mn = n;
        if (n >= 512 && tar_ok(u)) { a->kind = AF_TGZ; return tar_open(a, n); }
        a->kind = AF_GZ; aent *x = ae_new(a); if (!fn[0]) { strncpy0(fn, base_name(path), 200); int l = strlen(fn); if (z_ends(fn, ".gz")) fn[l - 3] = 0; else if (z_ends(fn, ".tgz")) strcpy(fn + l - 4, ".tar"); else strcat(fn, ".sin-gz"); }
        x->name = strdup(fn); x->size = x->csize = n; x->off = 0; x->crc = z_crc32(u, n); return 0; }
    snprintf(a->err, 160, "%s", tr("No es un archivo comprimido que Alquimia conozca (ZIP, TAR, TAR.GZ o GZ)")); return -1; }
/* datos de una entrada, descifrados y descomprimidos: 0 bien; -1 error (en err); -2 falta contrasena; -3 contrasena mala */
static int arch_get(arch *a, aent *x, const char *pw, u8 **out, char *err) {
    *out = 0; if (x->dir) { *out = malloc(1); return 0; }
    if (a->kind != AF_ZIP) { u8 *b = malloc(x->size + 1); if (!b || ar_rd(a, x->off, b, x->size)) { free(b); strcpy(err, tr("No se puede leer el archivo")); return -1; } *out = b; return 0; }
    u8 lh[30]; if (ar_rd(a, x->off, lh, 30) || G32(lh) != 0x04034B50) { strcpy(err, tr("Cabecera local dañada")); return -1; }
    long dp = x->off + 30 + G16(lh + 26) + G16(lh + 28), cs = x->csize; u8 *c = malloc(cs + 1); if (!c) { strcpy(err, tr("Falta memoria")); return -1; } if (ar_rd(a, dp, c, cs)) { free(c); strcpy(err, tr("El archivo está cortado")); return -1; }
    int meth = x->meth; u8 *dat = c; long dn = cs;
    if (x->enc) { if (!pw || !pw[0]) { free(c); return -2; }
        if (x->enc == 2) { int sl = aes_klen(x->ast) / 2; if (cs < sl + 12) { free(c); strcpy(err, tr("Entrada cifrada dañada")); return -1; }
            wza_t wz; u8 pv[2]; wza_init(&wz, pw, c, x->ast, pv); if (memcmp(pv, c + sl, 2)) { free(c); return -3; }
            dat = c + sl + 2; dn = cs - sl - 12; wza_dec(&wz, dat, dn); u8 tag[10]; wza_tag(&wz, tag); if (memcmp(tag, c + cs - 10, 10)) { free(c); strcpy(err, tr("La comprobación HMAC falla: archivo alterado o dañado")); return -1; } meth = x->ameth; }
        else { if (cs < 12) { free(c); strcpy(err, tr("Entrada cifrada dañada")); return -1; } zc_t z; zcr_init(&z, pw); zcr_dec(&z, c, cs); u8 chk = x->fl & 8 ? x->dtime >> 8 : x->crc >> 24; if (c[11] != chk) { free(c); return -3; } dat = c + 12; dn = cs - 12; } }
    u8 *u = 0;
    if (meth == 0) { u = malloc(dn + 1); if (u) memcpy(u, dat, dn); if (dn != x->size) { free(u); free(c); strcpy(err, tr("Tamaño incorrecto")); return -1; } }
    else if (meth == 8) { u = malloc(x->size + 1); if (u) { long r = z_inflate(dat, dn, u, x->size); if (r != x->size) { free(u); u = 0; free(c); strcpy(err, tr("Los datos comprimidos están dañados")); return -1; } } }
    else { free(c); snprintf(err, 160, "%s %d (%s)", tr("Método de compresión no soportado:"), meth, meth == 12 ? "bzip2" : meth == 14 ? "LZMA" : meth == 9 ? "Deflate64" : meth == 93 ? "Zstandard" : meth == 95 ? "XZ" : "?"); return -1; }
    free(c); if (!u) { strcpy(err, tr("Falta memoria")); return -1; }
    if (!(x->enc == 2 && x->aver == 2) && z_crc32(u, x->size) != x->crc) { free(u); strcpy(err, tr("El CRC no coincide: archivo dañado")); return -1; }
    *out = u; return 0; }
/* nombre seguro dentro de la carpeta de destino: sin /, .., unidades ni caracteres que FAT no admite */
static void safe_rel(const char *n, char *o, int cap) { int k = 0; const char *p = n; o[0] = 0;
    while (*p) { while (*p == '/' || *p == '\\') p++; const char *s = p; while (*p && *p != '/' && *p != '\\') p++; int l = p - s; if (!l) continue; if ((l == 1 && s[0] == '.') || (l == 2 && s[0] == '.' && s[1] == '.')) continue;
        if (k && k < cap - 1) o[k++] = '/'; for (int i = 0; i < l && k < cap - 1; i++) { u8 c = s[i]; o[k++] = c < 32 || strchr(":*?\"<>|", c) ? '_' : c; } o[k] = 0; }
    while (k > 0 && (o[k - 1] == ' ' || o[k - 1] == '.')) o[--k] = 0; }
static void mkdirs(const char *path) { char t[700]; strncpy0(t, path, 700); for (char *p = t + 1; *p; p++) if (*p == '/') { *p = 0; z_mkdir(t); *p = '/'; } z_mkdir(t); }
/* extrae (o con test solo comprueba) las entradas marcadas en sel (0 = todas) */
static int arch_extract(arch *a, const char *dest, const char *pw, const u8 *sel, int test, prog_fn pr, astat *st) {
    memset(st, 0, sizeof *st); long total = 0, done = 0; for (int i = 0; i < a->n; i++) if (!sel || sel[i]) total += a->e[i].size; if (!test) mkdirs(dest);
    for (int i = 0; i < a->n; i++) { aent *x = &a->e[i]; if (sel && !sel[i]) continue;
        char rel[600], p[800]; safe_rel(x->name, rel, 600); if (!rel[0]) continue; snprintf(p, 800, "%s/%s", dest, rel);
        if (pr && pr(done, total, x->name)) return ar_err(st, tr("Cancelado"), 0);
        if (x->dir) { if (!test) mkdirs(p); continue; }
        u8 *d; char err[160] = ""; int r = arch_get(a, x, pw, &d, err);
        if (r == -2) return ar_err(st, tr("Este archivo está cifrado: escribe la contraseña"), x->name);
        if (r == -3) return ar_err(st, tr("Contraseña incorrecta"), x->name);
        if (r) return ar_err(st, err, x->name);
        if (!test) { char *sl = strrchr(p, '/'); if (sl) { *sl = 0; mkdirs(p); *sl = '/'; } int e = z_writefile(p, d, x->size); if (e) { free(d); return ar_err(st, z_errstr(e), p); } }
        free(d); done += x->size; st->in += x->csize; st->out += x->size; st->files++; }
    if (!test) z_sync(); if (pr) pr(total, total, ""); return 0; }
