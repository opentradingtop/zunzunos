/* Cifrado opcional de los datos (Configuracion > Cifrado de datos).
   Se activa con una frase de paso. Desde entonces los archivos nuevos de Documentos, Escritorio, la configuracion
   (ZUNZUN) y los puntos de restauracion se guardan cifrados; al activarlo, Configuracion reescribe los que ya habia.
   Formato de cada archivo: "ZZCIFRA1" + 8 bytes al azar (nonce) y despues los datos con AES-256 en modo CTR
   (bloque = nonce + numero de bloque). Todo pasa por la capa de archivos (vfs.c), asi los programas no notan nada.
   La clave maestra (32 bytes al azar) se guarda en /sys/ZUNZUN/CIFRADO.TXT envuelta con la frase de paso:
   PBKDF2-HMAC-SHA256 (100000 vueltas) da 64 bytes: 32 para envolver la clave y 32 para comprobar (HMAC) que la frase
   es la buena. Sin la frase (al encender, antes de escribirla) los archivos cifrados no se pueden abrir. */
#include "kernel.h"
#define CF "/sys/ZUNZUN/CIFRADO.TXT"
#define ITER 100000
static const u8 SB[256] = {
 0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
 0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
 0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
 0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
 0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
 0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
 0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
 0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16};
typedef struct { u8 rk[240]; } aes_t;
static u8 xt(u8 x) { return (u8)(x << 1) ^ ((x >> 7) * 0x1b); }
static void aes_key(aes_t *a, const u8 *k) { memcpy(a->rk, k, 32); u8 rc = 1;
    for (int i = 8; i < 60; i++) { u8 t[4]; memcpy(t, a->rk + 4 * (i - 1), 4);
        if (i % 8 == 0) { u8 x = t[0]; t[0] = SB[t[1]] ^ rc; t[1] = SB[t[2]]; t[2] = SB[t[3]]; t[3] = SB[x]; rc = xt(rc); } else if (i % 8 == 4) for (int j = 0; j < 4; j++) t[j] = SB[t[j]];
        for (int j = 0; j < 4; j++) a->rk[4 * i + j] = a->rk[4 * (i - 8) + j] ^ t[j]; } }
static void aes_enc(const aes_t *a, const u8 *in, u8 *out) { u8 s[16], t[16]; for (int i = 0; i < 16; i++) s[i] = in[i] ^ a->rk[i];
    for (int r = 1; r <= 14; r++) { for (int c = 0; c < 4; c++) for (int w = 0; w < 4; w++) t[c * 4 + w] = SB[s[((c + w) % 4) * 4 + w]];   /* SubBytes + ShiftRows */
        if (r < 14) for (int c = 0; c < 4; c++) { u8 *p = t + c * 4, a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3], x = a0 ^ a1 ^ a2 ^ a3; p[0] ^= x ^ xt(a0 ^ a1); p[1] ^= x ^ xt(a1 ^ a2); p[2] ^= x ^ xt(a2 ^ a3); p[3] ^= x ^ xt(a3 ^ a0); }
        for (int i = 0; i < 16; i++) s[i] = t[i] ^ a->rk[16 * r + i]; }
    memcpy(out, s, 16); }
/* HMAC-SHA256 y PBKDF2 */
static void hmac(const u8 *key, int kl, const u8 *m, int ml, u8 *o) { u8 k[64] = {0}, ip[64], op[64], h[32]; if (kl > 64) { sha_t c; sha_init(&c); sha_up(&c, key, kl); sha_end(&c, k); } else memcpy(k, key, kl);
    for (int i = 0; i < 64; i++) { ip[i] = k[i] ^ 0x36; op[i] = k[i] ^ 0x5c; } sha_t c; sha_init(&c); sha_up(&c, ip, 64); sha_up(&c, m, ml); sha_end(&c, h); sha_init(&c); sha_up(&c, op, 64); sha_up(&c, h, 32); sha_end(&c, o); }
void crypt_pbkdf2(const u8 *pw, int pl, const u8 *salt, int sl, int iter, u8 *out, int ol) {
    for (int blk = 1; ol > 0; blk++) { u8 m[80], u[32], t[32]; int n = MIN(sl, 64); memcpy(m, salt, n); m[n] = blk >> 24; m[n + 1] = blk >> 16; m[n + 2] = blk >> 8; m[n + 3] = blk; hmac(pw, pl, m, n + 4, u); memcpy(t, u, 32);
        for (int i = 1; i < iter; i++) { hmac(pw, pl, u, 32, u); for (int j = 0; j < 32; j++) t[j] ^= u[j]; } int k = MIN(32, ol); memcpy(out, t, k); out += k; ol -= k; } }
void crypt_aes_test(const u8 *key, const u8 *in, u8 *out) { aes_t a; aes_key(&a, key); aes_enc(&a, in, out); }

static int loaded, enabled, unlocked, disabling; static aes_t KA; static u8 salt[16], wrapped[32], mac[32];
static void hexs(const u8 *b, int n, char *o) { static const char H[] = "0123456789abcdef"; for (int i = 0; i < n; i++) { o[2 * i] = H[b[i] >> 4]; o[2 * i + 1] = H[b[i] & 15]; } o[2 * n] = 0; }
static int unhexs(const char *s, u8 *b, int n) { for (int i = 0; i < 2 * n; i++) { char c = s[i]; int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; if (v < 0) return -1; if (i & 1) b[i / 2] |= v; else b[i / 2] = v << 4; } return 0; }
static const char *field(const char *t, u64 n, const char *k) { int l = strlen(k); for (u64 i = 0; i + l < n; i++) if ((i == 0 || t[i - 1] == '\n') && !memcmp(t + i, k, l)) return t + i + l; return 0; }
static void load(void) { if (loaded) return; zdirent_t d; if (vfs_stat("/sys", &d)) return; loaded = 1; u64 n; char *t = vfs_slurp(CF, &n); if (!t) return;
    const char *s1 = field(t, n, "sal="), *s2 = field(t, n, "clave="), *s3 = field(t, n, "mac=");
    if (s1 && s2 && s3 && !unhexs(s1, salt, 16) && !unhexs(s2, wrapped, 32) && !unhexs(s3, mac, 32)) enabled = 1; kfree(t); }
static void rnd(u8 *o, int n) { sha_t c; sha_init(&c); for (int i = 0; i < 256; i++) { u64 t = rdtsc(); sha_up(&c, &t, 8); for (volatile int j = 0; j < (int)(t & 31); j++); } extern u8 net_first_mac[6]; sha_up(&c, net_first_mac, 6); u64 tk = ticks; sha_up(&c, &tk, 8); u8 h[32]; sha_end(&c, h);
    for (int i = 0; i < n; i += 32) { sha_t d; sha_init(&d); sha_up(&d, h, 32); u8 k = i; sha_up(&d, &k, 1); u64 t = rdtsc(); sha_up(&d, &t, 8); u8 o2[32]; sha_end(&d, o2); memcpy(o + i, o2, MIN(32, n - i)); } }
/* deriva de la frase la clave para envolver (32) y la de comprobar (32) */
static void derive(const char *pass, u8 *wk, u8 *hk) { u8 dk[64]; crypt_pbkdf2((const u8 *)pass, strlen(pass), salt, 16, ITER, dk, 64); memcpy(wk, dk, 32); memcpy(hk, dk + 32, 32); }
static int save(void) { char b[400], h1[40], h2[70], h3[70]; hexs(salt, 16, h1); hexs(wrapped, 32, h2); hexs(mac, 32, h3); int l = snprintf(b, 400, "ZZCIFRA1\nsal=%s\nvueltas=%d\nclave=%s\nmac=%s\n", h1, ITER, h2, h3);
    file_t *f; int e = vfs_open(CF, O_WRITE | O_CREATE | O_TRUNC, &f); if (e) return e; i64 w = vfs_write(f, b, l); vfs_close(f); vfs_sync(); return w == l ? 0 : E_IO; }

/* rutas protegidas: datos del usuario. Quedan fuera el sistema y lo que hace falta antes de entrar (tema, ajustes, PIN) */
int crypt_path(const char *p) { load(); if (!enabled) return 0;
    if (!strncmp(p, "/sys/DOCS/", 10) || !strncmp(p, "/sys/ESCRITORIO/", 16) || !strncmp(p, "/sys/ZUNZUN-PUNTOS/", 19)) return 1;
    if (!strncmp(p, "/sys/ZUNZUN/", 12)) { const char *n = p + 12; static const char *X[] = {"KERNEL.BIN", "INITRD.BIN", "CIFRADO.TXT", "SESION.TXT", "TEMA.TXT", "AJUSTES.TXT"}; for (unsigned i = 0; i < sizeof X / sizeof *X; i++) if (!strcmp(n, X[i])) return 0; return 1; }
    return 0; }
static void ks(const u8 *nonce, u64 off, u8 *b, u64 n) { u8 ctr[16], k[16]; u64 blk = ~0ULL; for (u64 i = 0; i < n; i++) { u64 o = off + i, bl = o / 16; if (bl != blk) { blk = bl; memcpy(ctr, nonce, 8); for (int j = 0; j < 8; j++) ctr[8 + j] = bl >> (56 - 8 * j); aes_enc(&KA, ctr, k); } b[i] ^= k[o % 16]; } }
/* al abrir (con la capa de archivos bloqueada): mira la cabecera; un archivo nuevo o vaciado se crea ya cifrado */
int crypt_open(file_t *f, int fl) { f->enc = 0; u64 keep = f->pos;
    if (f->size >= 16) { u8 h[16]; f->pos = 0; i64 r = f->m->ops->read(f, h, 16); f->pos = keep; if (r == 16 && !memcmp(h, "ZZCIFRA1", 8)) { if (!unlocked) return E_ACCES; f->enc = 1; memcpy(f->nonce, h + 8, 8); f->pos = (fl & O_APPEND) ? f->size : 16; } return 0; }
    if (f->size == 0 && (fl & O_WRITE)) { if (!unlocked) return disabling ? 0 : E_ACCES; if (disabling) return 0; if (!f->m->ops->write) return 0;
        u8 h[16]; memcpy(h, "ZZCIFRA1", 8); rnd(h + 8, 8); f->pos = 0; i64 w = f->m->ops->write(f, h, 16); if (w != 16) return E_IO; f->size = 16; f->pos = 16; f->enc = 1; memcpy(f->nonce, h + 8, 8); }
    return 0; }
i64 crypt_read(file_t *f, void *b, u64 n) { i64 r = f->m->ops->read(f, b, n); if (r > 0) ks(f->nonce, f->pos - 16, b, r); return r; }
i64 crypt_write(file_t *f, const void *b, u64 n) { u8 *t = kmalloc(4096); if (!t) return E_NOMEM; u64 done = 0; u64 pos0 = f->pos;
    while (done < n) { u64 c = MIN(4096, n - done); memcpy(t, (const u8 *)b + done, c); ks(f->nonce, f->pos - 16, t, c); i64 w = f->m->ops->write(f, t, c); if (w <= 0) { kfree(t); f->pos = pos0; return done ? (i64)done : w; } done += w; f->pos += w; if (f->pos > f->size) f->size = f->pos; if ((u64)w < c) break; }
    kfree(t); f->pos = pos0; return done; }   /* vfs_write avanza pos y size */

/* llamada 115: op 0 estado (1 activado, 2 abierto, 4 desactivandose), 1 abrir con la frase, 2 activar (frase nueva),
   3 empezar a desactivar (comprueba la frase; desde ahi se escribe sin cifrar), 4 terminar de desactivar */
long crypt_syscall(int op, const char *pass) { load();
    if (op == 0) return (enabled ? 1 : 0) | (unlocked ? 2 : 0) | (disabling ? 4 : 0);
    if (sess_guest()) return E_ACCES;
    if (op == 1 || op == 3) { if (!enabled) return E_INVAL; u8 wk[32], hk[32], m2[32], mk[32]; derive(pass, wk, hk); hmac(hk, 32, wrapped, 32, m2); int bad = 0; for (int i = 0; i < 32; i++) bad |= m2[i] ^ mac[i]; if (bad) { kprintf("cifrado: frase incorrecta\n"); return E_ACCES; }
        for (int i = 0; i < 32; i++) mk[i] = wrapped[i] ^ wk[i]; aes_key(&KA, mk); memset(mk, 0, 32); unlocked = 1; if (op == 3) disabling = 1; kprintf("cifrado: %s\n", op == 3 ? "desactivando" : "abierto"); return 0; }
    if (op == 2) { if (enabled) return E_EXIST; if (strlen(pass) < 8) return E_INVAL; u8 mk[32], wk[32], hk[32]; rnd(mk, 32); rnd(salt, 16); derive(pass, wk, hk); for (int i = 0; i < 32; i++) wrapped[i] = mk[i] ^ wk[i]; hmac(hk, 32, wrapped, 32, mac);
        aes_key(&KA, mk); memset(mk, 0, 32); int e = save(); if (e) return e; enabled = unlocked = 1; disabling = 0; kprintf("cifrado: activado\n"); return 0; }
    if (op == 4) { if (!disabling) return E_INVAL; vfs_unlink(CF); vfs_sync(); enabled = unlocked = disabling = 0; memset(&KA, 0, sizeof KA); kprintf("cifrado: desactivado\n"); return 0; }
    return E_INVAL; }
