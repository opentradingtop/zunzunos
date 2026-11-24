/* Lector NTFS propio (zntfs.c) y lector de colmenas del registro de Windows (zreg.c), para Forense. */
#ifndef ZNTFS_H
#define ZNTFS_H
typedef struct zntfs zntfs;
typedef int (*zn_rd)(void *ctx, u64 lba, int n, void *buf);     /* lee n sectores de 512 bytes */
typedef struct { u64 ref, size, ctime, mtime, atime; u32 attrs; int dir; char name[256]; } zn_ent;   /* fechas FILETIME */
zntfs *zn_open(zn_rd rd, void *ctx, u64 lba0);                    /* 0 si en lba0 no hay un NTFS legible */
void zn_close(zntfs *z);
i64 zn_lookup(zntfs *z, const char *path);                        /* "\Windows\System32" o "Windows/System32" */
int zn_stat(zntfs *z, const char *path, zn_ent *e);
long zn_read(zntfs *z, const char *path, u64 off, void *buf, long cap);
u8 *zn_readall(zntfs *z, const char *path, long max, long *len); /* hasta max bytes; el resultado acaba en 0 */
int zn_list(zntfs *z, const char *path, int (*cb)(const zn_ent *e, void *ctx), void *ctx);   /* numero de entradas o <0 */
int zn_volume(zntfs *z, char *label, int cap, int *major, int *minor, int *flags);           /* flags & 1: sucio */
u64 zn_clusters(zntfs *z, u32 *csize);
long zn_attr_read_raw(zntfs *z, void *attr, u64 off, void *buf, u32 len);
void zn_utf8(const u8 *utf16, int n, char *out, int cap);

/* colmenas del registro (regf) */
typedef struct zreg zreg;
zreg *zr_open(const u8 *data, long len);                         /* no copia: data tiene que seguir viva */
void zr_close(zreg *r);
int zr_key(zreg *r, const char *path);                            /* "ControlSet001\Services"; -1 si no existe */
int zr_subkey(zreg *r, int key, int i, char *name, int cap);      /* subclave i (0..); su celda o -1 al acabar */
int zr_value(zreg *r, int key, const char *name, int *type, u8 *out, int cap);   /* longitud de los datos o -1 */
int zr_value_at(zreg *r, int key, int i, char *name, int cap, int *type);        /* nombre del valor i o -1 */
int zr_str(zreg *r, const char *path, const char *value, char *out, int cap);    /* texto legible (cadena, numero...) */
int zr_u32(zreg *r, const char *path, const char *value, u32 *out);
int zr_u64(zreg *r, const char *path, const char *value, u64 *out);
#endif
