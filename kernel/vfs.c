/* Sistema de archivos virtual y disco RAM /rd */
#include "kernel.h"
#define MAXMNT 32
static mount_t *mounts[MAXMNT]; static int nmnt; static mutex_t vfs_lock;
int vfs_mount(const char *n, const fsops_t *o, void *priv, blkdev_t *d, const char *type, const char *label) {
    mutex_lock(&vfs_lock);
    if (nmnt >= MAXMNT) { mutex_unlock(&vfs_lock); return -1; }
    mount_t *m = kzalloc(sizeof *m); mounts[nmnt++] = m;
    strncpy0(m->name, n, 16); m->ops = o; m->priv = priv; m->dev = d; strncpy0(m->fstype, type, 8); if (label) strncpy0(m->label, label, 16); m->ro = d && strcmp(n, "sys");
    int r = nmnt - 1; mutex_unlock(&vfs_lock);
    kprintf("vfs: /%s (%s) %s\n", n, type, label ? label : "");
    return r;
}
mount_t *vfs_mount_ptr(int i) { return i >= 0 && i < nmnt ? mounts[i] : 0; }
void vfs_set_size(int i, u64 t, u64 f) { if (i >= 0 && i < nmnt) { mounts[i]->total = t; mounts[i]->free_b = f; } }
void vfs_set_free(int i, volatile i64 *src, u32 mul) { if (i >= 0 && i < nmnt) { mounts[i]->free_src = src; mounts[i]->free_mul = mul; } }
void vfs_umount_dev(blkdev_t *d) {
    mutex_lock(&vfs_lock);
    for (int i = 0; i < nmnt; i++) if (mounts[i]->dev == d || (mounts[i]->dev && mounts[i]->dev->parent == d)) { mounts[i]->dead = 1; memmove(&mounts[i], &mounts[i + 1], (nmnt - i - 1) * sizeof mounts[0]); nmnt--; i--; }
    mutex_unlock(&vfs_lock);
}
int vfs_set_ro(const char *n, int ro) { mutex_lock(&vfs_lock); int r = E_NOENT; for (int i = 0; i < nmnt; i++) if (!strcmp(mounts[i]->name, n)) { mounts[i]->ro = ro; r = 0; } mutex_unlock(&vfs_lock); return r; }
void vfs_sync(void) { mutex_lock(&vfs_lock); for (int i = 0; i < nmnt; i++) if (!mounts[i]->ro && mounts[i]->ops->sync) mounts[i]->ops->sync(mounts[i]); mutex_unlock(&vfs_lock); }
static mount_t *resolve(const char *p, const char **rest) {
    if (*p != '/') return 0;
    p++; const char *e = p; while (*e && *e != '/') e++;
    u64 l = e - p;
    for (int i = 0; i < nmnt; i++) if (strlen(mounts[i]->name) == l && !strncmp(mounts[i]->name, p, l)) { while (*e == '/') e++; *rest = e; return mounts[i]; }
    return 0;
}
mount_t *vfs_resolve(const char *p, const char **rest) { mutex_lock(&vfs_lock); mount_t *m = resolve(p, rest); mutex_unlock(&vfs_lock); return m; }
int vfs_mounts(zmount_t *o, int max) {
    mutex_lock(&vfs_lock); int n = MIN(max, nmnt);
    for (int i = 0; i < n; i++) { mount_t *m = mounts[i]; memcpy(o[i].name, m->name, 16); memcpy(o[i].fstype, m->fstype, 8); memcpy(o[i].label, m->label, 16); o[i].total = m->total; o[i].free_b = m->free_src && *m->free_src >= 0 ? (u64)*m->free_src * m->free_mul : m->free_b; strncpy0(o[i].dev, m->dev ? m->dev->name : "ram", 16); o[i].ro = m->ro; }
    mutex_unlock(&vfs_lock); return n;
}
int vfs_open(const char *path, int fl, file_t **out) {
    int prot = crypt_path(path);   /* fuera del cerrojo: puede leer CIFRADO.TXT */
    mutex_lock(&vfs_lock); const char *r; mount_t *m = resolve(path, &r); int e = E_NOENT;
    if (m && m->ro && (fl & (O_WRITE | O_CREATE | O_TRUNC | O_APPEND))) e = E_ROFS;
    else if (m) {
        file_t *f = kzalloc(sizeof *f);
        if (!f) e = E_NOMEM;
        else { f->m = m; f->flags = fl; f->refs = 1; e = m->ops->open(m, r, fl, f); if (e) kfree(f); else { if (fl & O_APPEND) f->pos = f->size; if (prot) { e = crypt_open(f, fl); if (e) { if (m->ops->close) m->ops->close(f); kfree(f); } } if (!e) *out = f; } }
    }
    mutex_unlock(&vfs_lock); return e;
}
i64 vfs_read(file_t *f, void *b, u64 n) {
    if (f->is_sock) return sock_recv(f, b, n, -1);
    if (!(f->flags & O_READ)) return E_BADF;
    if (f->m->dead) return E_IO;
    mutex_lock(&vfs_lock); i64 r = 0;
    if (f->pos < f->size) { r = f->enc ? crypt_read(f, b, MIN(n, f->size - f->pos)) : f->m->ops->read(f, b, MIN(n, f->size - f->pos)); if (r > 0) f->pos += r; }
    mutex_unlock(&vfs_lock); return r;
}
i64 vfs_write(file_t *f, const void *b, u64 n) {
    if (f->is_sock) return sock_send(f, b, n);
    if (!(f->flags & O_WRITE)) return E_BADF;
    if (f->m->dead) return E_IO;
    if (!f->m->ops->write) return E_ROFS;
    mutex_lock(&vfs_lock); i64 r = f->enc ? crypt_write(f, b, n) : f->m->ops->write(f, b, n);
    if (r > 0) { f->pos += r; if (f->pos > f->size) f->size = f->pos; }
    mutex_unlock(&vfs_lock); return r;
}
i64 vfs_seek(file_t *f, i64 o, int wh) {
    if (f->is_sock) return E_INVAL;
    int h = f->enc ? 16 : 0;   /* los cifrados tienen 16 bytes de cabecera que el programa no ve */
    i64 b = wh == 1 ? (i64)f->pos - h : wh == 2 ? (i64)f->size - h : 0;
    if (b + o < 0) return E_INVAL;
    f->pos = b + o + h; return f->pos - h;
}
void vfs_close(file_t *f) {
    if (f->is_sock) { sock_close(f); return; }
    mutex_lock(&vfs_lock); if (--f->refs == 0) { if (!f->m->dead && f->m->ops->close) f->m->ops->close(f); kfree(f); } mutex_unlock(&vfs_lock);
}
#define WRAP(name, call) mutex_lock(&vfs_lock); const char *r; mount_t *m = resolve(p, &r); int e = E_NOENT; if (m) { e = m->ops->name ? call : E_ROFS; } mutex_unlock(&vfs_lock); return e;
int vfs_stat(const char *p, zdirent_t *d) {
    if (!strcmp(p, "/")) { memset(d, 0, sizeof *d); d->is_dir = 1; return 0; }
    WRAP(stat, m->ops->stat(m, r, d))
}
int vfs_readdir(const char *p, int i, zdirent_t *d) {
    if (!strcmp(p, "/")) {
        mutex_lock(&vfs_lock); int e = E_NOENT;
        if (i < nmnt) { memset(d, 0, sizeof *d); strncpy0(d->name, mounts[i]->name, 256); d->is_dir = 1; e = 0; }
        mutex_unlock(&vfs_lock); return e;
    }
    WRAP(readdir, m->ops->readdir(m, r, i, d))
}
#define WRAPW(name, call) mutex_lock(&vfs_lock); const char *r; mount_t *m = resolve(p, &r); int e = E_NOENT; if (m) { e = m->ro || !m->ops->name ? E_ROFS : call; } mutex_unlock(&vfs_lock); return e;
int vfs_contig(const char *p, u64 size, u64 *lba) { WRAPW(contig, m->ops->contig(m, r, size, lba)) }
int vfs_mkdir(const char *p) { WRAPW(mkdir, m->ops->mkdir(m, r)) }
int vfs_unlink(const char *p) { WRAPW(unlink, m->ops->unlink(m, r)) }
int vfs_rename(const char *p, const char *q) {
    const char *r2; mount_t *m2 = resolve(q, &r2); if (!m2) return E_NOENT;
    mutex_lock(&vfs_lock); const char *r; mount_t *m = resolve(p, &r); int e = E_NOENT;
    if (m) e = m != m2 ? E_INVAL : m->ro ? E_ROFS : m->ops->rename ? m->ops->rename(m, r, r2) : E_ROFS;
    mutex_unlock(&vfs_lock); return e;
}
void *vfs_slurp(const char *p, u64 *size) {
    file_t *f; if (vfs_open(p, O_READ, &f)) return 0;
    u8 *b = kmalloc(f->size + 1);
    if (!b) { vfs_close(f); return 0; }
    u64 got = 0;
    while (got < f->size) { i64 r = vfs_read(f, b + got, f->size - got); if (r <= 0) break; got += r; }
    b[got] = 0; *size = got; vfs_close(f);
    return b;
}

/* --- disco RAM: lista plana de rutas; admite escritura --- */
typedef struct rnode { struct rnode *next; char path[200]; u8 *data; u64 size, cap; int is_dir, is_static; } rnode_t;
static rnode_t *rlist;
static rnode_t *rfind(const char *p) { for (rnode_t *n = rlist; n; n = n->next) if (!strcmp(n->path, p)) return n; return 0; }
static rnode_t *radd(const char *p, int dir) {
    rnode_t *n = kzalloc(sizeof *n); strncpy0(n->path, p, sizeof n->path); n->is_dir = dir; n->next = rlist; rlist = n; return n;
}
static void rparents(const char *p) {
    char b[200]; strncpy0(b, p, sizeof b);
    for (char *s = b; *s; s++) if (*s == '/') { *s = 0; if (!rfind(b)) radd(b, 1); *s = '/'; }
}
static int r_open(mount_t *m, const char *p, int fl, file_t *f) {
    rnode_t *n = rfind(p);
    if (!n) { if (!(fl & O_CREATE)) return E_NOENT; rparents(p); n = radd(p, 0); }
    if (n->is_dir) return E_ISDIR;
    if ((fl & O_TRUNC) && !n->is_static) n->size = 0;
    f->priv = n; f->size = n->size; return 0;
}
static i64 r_read(file_t *f, void *b, u64 c) { rnode_t *n = f->priv; memcpy(b, n->data + f->pos, c); return c; }
static i64 r_write(file_t *f, const void *b, u64 c) {
    rnode_t *n = f->priv; u64 e = f->pos + c;
    if (n->is_static || e > n->cap) {
        u64 cap = MAX(e * 2, 4096UL); u8 *d = kzalloc(cap); if (!d) return E_NOSPC;
        if (n->data) memcpy(d, n->data, n->size);
        if (!n->is_static) kfree(n->data);
        n->data = d; n->cap = cap; n->is_static = 0;
    }
    memcpy(n->data + f->pos, b, c); if (e > n->size) n->size = e;
    return c;
}
static void fill(zdirent_t *d, rnode_t *n, const char *nm) { memset(d, 0, sizeof *d); strncpy0(d->name, nm, 256); d->size = n->size; d->is_dir = n->is_dir; }
static int r_stat(mount_t *m, const char *p, zdirent_t *d) {
    if (!*p) { memset(d, 0, sizeof *d); d->is_dir = 1; return 0; }
    rnode_t *n = rfind(p); if (!n) return E_NOENT;
    const char *b = strrchr(p, '/'); fill(d, n, b ? b + 1 : p); return 0;
}
static int r_readdir(mount_t *m, const char *p, int idx, zdirent_t *d) {
    u64 l = strlen(p); int i = 0;
    for (rnode_t *n = rlist; n; n = n->next) {
        const char *q = n->path;
        if (l) { if (strncmp(q, p, l) || q[l] != '/') continue; q += l + 1; }
        if (strchr(q, '/')) continue;
        if (i++ == idx) { fill(d, n, q); return 0; }
    }
    return E_NOENT;
}
static int r_mkdir(mount_t *m, const char *p) { if (rfind(p)) return E_EXIST; rparents(p); radd(p, 1); return 0; }
static int r_unlink(mount_t *m, const char *p) {
    rnode_t **pp = &rlist; u64 l = strlen(p);
    for (rnode_t *n = rlist; n; n = n->next) if (!strncmp(n->path, p, l) && n->path[l] == '/') return E_NOTEMPTY;
    for (; *pp; pp = &(*pp)->next) if (!strcmp((*pp)->path, p)) { rnode_t *n = *pp; *pp = n->next; if (!n->is_static) kfree(n->data); kfree(n); return 0; }
    return E_NOENT;
}
static int r_rename(mount_t *m, const char *a, const char *b) { rnode_t *n = rfind(a); if (!n) return E_NOENT; if (rfind(b)) return E_EXIST; rparents(b); strncpy0(n->path, b, sizeof n->path); return 0; }
static const fsops_t rdops = { r_open, r_read, r_write, 0, r_stat, r_readdir, r_mkdir, r_unlink, r_rename, 0 };

typedef struct PACKED { char name[52]; u32 off, size, pad; } rde_t;
void vfs_init(void) {
    u8 *ird = P2V(bi.initrd_phys);
    vfs_mount("rd", &rdops, 0, 0, "ramfs", "SISTEMA");
    radd("tmp", 1);
    if (memcmp(ird, "ZRD1", 4)) panic("initrd no valido");
    u32 n = *(u32 *)(ird + 4); rde_t *e = (rde_t *)(ird + 16);
    for (u32 i = 0; i < n; i++) {
        char nm[53]; memcpy(nm, e[i].name, 52); nm[52] = 0; rparents(nm);
        rnode_t *r = radd(nm, 0); r->data = ird + e[i].off; r->size = r->cap = e[i].size; r->is_static = 1;
    }
    rparents("inst/x"); rnode_t *self = radd("inst/INITRD.BIN", 0); self->data = ird; self->size = self->cap = *(u32 *)(ird + 8); self->is_static = 1;
    kprintf("initrd: %u archivos\n", n);
}
