/* Prueba en el Mac de los lectores de Forense: abre el disco sintetico de tools/mkwindows.py, busca la particion NTFS
   en la tabla GPT, recorre todas las carpetas leyendo cada archivo (huella sha1) y lee el registro.
   Imprime lo encontrado; correr.sh lo compara con lo que el generador dice que puso.
   Con "danado N" estropea el disco al azar N veces y lo recorre entero: no puede colgarse ni salirse de la memoria. */
#include "z.h"
#include "zntfs.h"
#include <CommonCrypto/CommonDigest.h>
static FILE *F; static u8 *IMG; static long ILEN;
static int rd(void *ctx, u64 lba, int n, void *buf) { (void)ctx; if (IMG) { if ((long)(lba + n) * 512 > ILEN) return -1; memcpy(buf, IMG + lba * 512, n * 512); return 0; }
    if (fseek(F, lba * 512, 0) || fread(buf, 512, n, F) != (size_t)n) return -1; return 0; }
static zntfs *Z; static int nfiles; static int quiet;
static int cb(const zn_ent *e, void *ctx);
static void walkdir(const char *path) { zn_list(Z, path, cb, (void *)path); }
static int cb(const zn_ent *e, void *ctx) {
    const char *dir = ctx; char p[1024]; snprintf(p, sizeof p, "%s%s%s", dir, dir[0] ? "/" : "", e->name);
    if (e->name[0] == '$' && !dir[0]) return 0;
    if (e->dir) { walkdir(p); return 0; }
    long n; u8 *d = zn_readall(Z, p, 1 << 26, &n); unsigned char h[20]; char hx[41] = "-";
    if (d) { CC_SHA1(d, (CC_LONG)n, h); for (int i = 0; i < 20; i++) sprintf(hx + 2 * i, "%02x", h[i]); }
    zn_ent st; int s = zn_stat(Z, p, &st);
    if (!quiet) printf("ARCHIVO %s|%ld|%s|%lu|%lu\n", p, d ? n : -1L, hx, s ? 0UL : st.size, s ? 0UL : st.mtime / 10000000 - 11644473600UL);
    free(d); nfiles++; return 0; }
static void registro(void) {
    long n; u8 *sw = zn_readall(Z, "\\Windows\\System32\\config\\SOFTWARE", 1 << 26, &n); zreg *r = sw ? zr_open(sw, n) : 0; char v[512];
    if (r) { const char *K = "Microsoft\\Windows NT\\CurrentVersion", *N[] = {"ProductName", "EditionID", "DisplayVersion", "CurrentBuild", "UBR", "InstallDate", "RegisteredOwner", "BigBlob"};
        for (int i = 0; i < 8; i++) { zr_str(r, K, N[i], v, sizeof v); if (!quiet) printf("SOFTWARE %s=%s\n", N[i], v); }
        int k = zr_key(r, K); u8 big[50000]; int t, bl = k >= 0 ? zr_value(r, k, "BigBlob", &t, big, sizeof big) : -1; unsigned char h[20]; char hx[41] = "-";
        if (bl > 0) { CC_SHA1(big, bl, h); for (int i = 0; i < 20; i++) sprintf(hx + 2 * i, "%02x", h[i]); } if (!quiet) printf("SOFTWARE BigBlob.sha1=%s %d\n", hx, bl); zr_close(r); }
    else if (!quiet) printf("SOFTWARE no se abre\n");
    free(sw);
    u8 *sy = zn_readall(Z, "Windows/System32/config/SYSTEM", 1 << 26, &n); r = sy ? zr_open(sy, n) : 0;
    if (r) { u32 cur = 0; zr_u32(r, "Select", "Current", &cur); char cs[40]; snprintf(cs, 40, "ControlSet%03u", cur); char k[200];
        snprintf(k, 200, "%s\\Control\\ComputerName\\ComputerName", cs); zr_str(r, k, "ComputerName", v, sizeof v); if (!quiet) printf("SYSTEM ComputerName=%s\n", v);
        u64 sh = 0; snprintf(k, 200, "%s\\Control\\Windows", cs); zr_u64(r, k, "ShutdownTime", &sh); if (!quiet) printf("SYSTEM ShutdownTime=%lu\n", sh ? sh / 10000000 - 11644473600UL : 0);
        snprintf(k, 200, "%s\\Services", cs); int sk = zr_key(r, k), ns = 0; char nm[256]; while (sk >= 0 && zr_subkey(r, sk, ns, nm, sizeof nm) >= 0) ns++; if (!quiet) printf("SYSTEM servicios=%d\n", ns);
        snprintf(k, 200, "%s\\Services\\iaStorAC", cs); zr_str(r, k, "ImagePath", v, sizeof v); if (!quiet) printf("SYSTEM iaStorAC.ImagePath=%s\n", v);
        u32 st = 9; zr_u32(r, k, "Start", &st); if (!quiet) printf("SYSTEM iaStorAC.Start=%u\n", st); zr_close(r); }
    else if (!quiet) printf("SYSTEM no se abre\n");
    free(sy); }
static u64 ntfs_lba(void) {   /* GPT: la particion de datos basicos de Microsoft */
    u8 h[512], e[128]; if (rd(0, 1, 1, h) || memcmp(h, "EFI PART", 8)) return 0; u64 el = *(u64 *)(h + 72); u32 ne = *(u32 *)(h + 80), es = *(u32 *)(h + 84);
    static const u8 basic[16] = {0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44, 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7};
    for (u32 i = 0; i < ne && i < 128; i++) { u8 s[512]; if (rd(0, el + i * es / 512, 1, s)) return 0; memcpy(e, s + (i * es) % 512, 128); if (!memcmp(e, basic, 16)) return *(u64 *)(e + 32); }
    return 0; }
int main(int argc, char **argv) {
    F = fopen(argv[1], "rb"); if (!F) return 2; u64 lba = ntfs_lba(); printf("NTFS en el sector %lu\n", lba);
    if (argc > 3 && !strcmp(argv[2], "danado")) {   /* robustez: estropear bytes al azar en el NTFS y leerlo todo */
        fseek(F, 0, 2); ILEN = ftell(F); IMG = malloc(ILEN); fseek(F, 0, 0); if (fread(IMG, 1, ILEN, F) != (size_t)ILEN) return 2; u8 *orig = malloc(ILEN); memcpy(orig, IMG, ILEN);
        int N = atoi(argv[3]); unsigned s = 12345; quiet = 1; int opened = 0;
        u8 *bs = IMG + lba * 512; u64 mft = (u64)*(u64 *)(bs + 48) * 4096, mftlen = 0;   /* zona del MFT (primer trozo) */
        Z = zn_open(rd, 0, lba); if (Z) { zn_ent e; mftlen = 400 * 1024; zn_close(Z); }
        for (int it = 0; it < N; it++) { memcpy(IMG, orig, ILEN); int k = 1 + it % 40;
            for (int j = 0; j < k; j++) { s = s * 1103515245 + 12345; u64 o = it % 2 ? lba * 512 + mft + (s >> 4) % mftlen : lba * 512 + (u64)(s >> 4) % (100u << 20);
                s = s * 1103515245 + 12345; if (o < (u64)ILEN) IMG[o] = s >> 16; }
            Z = zn_open(rd, 0, lba); if (!Z) continue; opened++; walkdir(""); registro(); zn_close(Z); }
        printf("danado: %d pasadas (%d abrieron el volumen; la mitad estropeando el MFT), sin cuelgues\n", N, opened);
        /* colmenas estropeadas: SYSTEM y SOFTWARE con bytes al azar, consultadas enteras */
        memcpy(IMG, orig, ILEN); Z = zn_open(rd, 0, lba); long hl[2]; u8 *h[2] = { zn_readall(Z, "Windows/System32/config/SYSTEM", 1 << 26, &hl[0]), zn_readall(Z, "Windows/System32/config/SOFTWARE", 1 << 26, &hl[1]) };
        int hok = 0;
        for (int it = 0; it < N; it++) { int w = it & 1; u8 *c = malloc(hl[w]); memcpy(c, h[w], hl[w]); int k = 1 + it % 60;
            for (int j = 0; j < k; j++) { s = s * 1103515245 + 12345; c[(s >> 4) % hl[w]] = s >> 20; }
            zreg *r = zr_open(c, hl[w]); if (r) { hok++; char nm[256], v[512]; int stack[64], sp = 0; stack[sp++] = zr_key(r, "");
                for (int guard = 0; sp && guard < 5000; guard++) { int key = stack[--sp]; if (key < 0) continue; int t;
                    for (int i = 0; i < 50 && zr_value_at(r, key, i, nm, sizeof nm, &t) >= 0; i++) { u8 b[256]; zr_value(r, key, nm, &t, b, sizeof b); }
                    for (int i = 0; i < 400; i++) { int sk = zr_subkey(r, key, i, nm, sizeof nm); if (sk < 0) break; if (sp < 64) stack[sp++] = sk; } }
                zr_str(r, "Microsoft\\Windows NT\\CurrentVersion", "ProductName", v, sizeof v); zr_close(r); }
            free(c); }
        printf("colmenas danadas: %d pasadas (%d abrieron), sin cuelgues\n", N, hok); return 0; }
    Z = zn_open(rd, 0, lba); if (!Z) { printf("NTFS no se abre\n"); return 1; }
    char label[64]; int ma, mi, fl; zn_volume(Z, label, sizeof label, &ma, &mi, &fl); printf("VOLUMEN %s %d.%d sucio=%d\n", label, ma, mi, fl & 1);
    walkdir(""); printf("TOTAL %d\n", nfiles); registro();
    zn_ent e; printf("NOENCUENTRA %d %d\n", zn_stat(Z, "\\Windows\\noexiste.txt", &e), zn_stat(Z, "C:\\WINDOWS\\SYSTEM32\\DRIVERS\\IASTORAC.SYS", &e) == 0 && e.size > 0);
    printf("CORTO %d\n", zn_lookup(Z, "PROGRA~1") == zn_lookup(Z, "Program Files") && zn_lookup(Z, "Program Files") > 0);
    zn_close(Z); return 0; }
