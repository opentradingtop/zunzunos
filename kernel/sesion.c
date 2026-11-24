/* Sesiones: administrador con PIN de 4 cifras e invitado.
   El PIN se guarda como SHA-256 con sal en /sys/ZUNZUN/SESION.TXT, que ningun programa puede leer.
   El invitado trabaja en /rd/tmp/invitado (memoria RAM): sus rutas /sys/... se redirigen ahi,
   no puede abrir los programas de administracion ni leer discos internos, y todo se borra al salir.
   Es una proteccion contra el uso casual: quien lea la memoria USB en otro equipo ve los archivos. */
#include "kernel.h"
#define PINF "/sys/ZUNZUN/SESION.TXT"
#define HOME "/rd/tmp/invitado"
static int mode; /* 0 sin sesion, 1 administrador, 2 invitado */
static int fails; static u64 wait_until;
static char *sstr(const char *h, const char *n) { int l = strlen(n); for (; *h; h++) if (!strncmp(h, n, l)) return (char *)h; return 0; }
#define strcpy(d, s) strncpy0(d, s, 256)
/* ---------- SHA-256 (igual que user/lib/zcrypto.c) ---------- */
static const u32 K[64] = {0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
void sha_blk(sha_t *c, const u8 *p) { u32 w[64]; for (int i = 0; i < 16; i++) w[i] = (u32)p[i * 4] << 24 | p[i * 4 + 1] << 16 | p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (int i = 16; i < 64; i++) { u32 s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3), s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10); w[i] = w[i - 16] + s0 + w[i - 7] + s1; }
    u32 a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3], e = c->h[4], f = c->h[5], g = c->h[6], h = c->h[7];
    for (int i = 0; i < 64; i++) { u32 t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i], t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & cc) ^ (b & cc)); h = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2; }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h; }
void sha_init(sha_t *c) { static const u32 iv[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}; memcpy(c->h, iv, 32); c->len = 0; c->n = 0; }
void sha_up(sha_t *c, const void *d, long n) { const u8 *p = d; c->len += n; while (n > 0) { int k = MIN(64 - c->n, n); memcpy(c->buf + c->n, p, k); c->n += k; p += k; n -= k; if (c->n == 64) { sha_blk(c, c->buf); c->n = 0; } } }
void sha_end(sha_t *c, u8 *o) { u64 bits = c->len * 8; u8 pad = 0x80, z = 0; sha_up(c, &pad, 1); while (c->n != 56) sha_up(c, &z, 1); u8 l[8]; for (int i = 0; i < 8; i++) l[i] = bits >> (56 - 8 * i); sha_up(c, l, 8); for (int i = 0; i < 32; i++) o[i] = c->h[i / 4] >> (24 - 8 * (i % 4)); }
/* hash del PIN: 20000 vueltas de SHA-256 sobre sal + PIN (frena un poco el ataque por fuerza bruta) */
static void pin_hash(const u8 *salt, const char *pin, u8 *o) { sha_t c; sha_init(&c); sha_up(&c, salt, 16); sha_up(&c, pin, 4); sha_end(&c, o); for (int i = 0; i < 20000; i++) { sha_init(&c); sha_up(&c, o, 32); sha_up(&c, salt, 16); sha_end(&c, o); } }
static void hex(const u8 *b, int n, char *o) { static const char H[] = "0123456789abcdef"; for (int i = 0; i < n; i++) { o[i * 2] = H[b[i] >> 4]; o[i * 2 + 1] = H[b[i] & 15]; } o[n * 2] = 0; }
static int unhex(const char *s, u8 *b, int n) { for (int i = 0; i < n * 2; i++) { char c = s[i]; int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; if (v < 0) return -1; if (i & 1) b[i / 2] |= v; else b[i / 2] = v << 4; } return 0; }
static int pin_ok_fmt(const char *p) { for (int i = 0; i < 4; i++) if (p[i] < '0' || p[i] > '9') return 0; return !p[4]; }
/* lee la sal y el hash guardados; 0 si hay PIN */
static int pin_load(u8 *salt, u8 *h) { u64 n; char *b = vfs_slurp(PINF, &n); if (!b) return -1; int r = -1; char *p = n >= 4 + 32 + 1 + 64 ? sstr(b, "pin=") : 0; if (p && (u64)(p - b) + 4 + 97 <= n && p[36] == ':' && !unhex(p + 4, salt, 16) && !unhex(p + 37, h, 32)) r = 0; kfree(b); return r; }
static int has_sys(void) { zdirent_t d; return !vfs_stat("/sys", &d); }
int sess_has_pin(void) { u8 s[16], h[32]; return !pin_load(s, h); }
static int write_file(const char *p, const void *d, u64 n) { vfs_unlink(p); file_t *f; int e = vfs_open(p, O_WRITE | O_CREATE | O_TRUNC, &f); if (e) return e; i64 w = vfs_write(f, d, n); vfs_close(f); vfs_sync(); blk_sync(); return w == (i64)n ? 0 : E_IO; }
static int pin_save(const char *pin) {
    u8 salt[16], h[32]; sha_t c; sha_init(&c); for (int i = 0; i < 64; i++) { u64 t = rdtsc(); sha_up(&c, &t, 8); } u64 t = ticks; sha_up(&c, &t, 8); sha_end(&c, h); memcpy(salt, h, 16);
    pin_hash(salt, pin, h); char line[128] = "pin="; hex(salt, 16, line + 4); line[36] = ':'; hex(h, 32, line + 37); line[101] = '\n'; line[102] = 0;
    vfs_mkdir("/sys/ZUNZUN"); return write_file(PINF, line, strlen(line));
}
/* comprueba el PIN; con varios fallos seguidos hay que esperar (30 s, 60 s...) */
static int pin_check(const char *pin) {
    if (ticks < wait_until) return (int)((wait_until - ticks) * 1000 / HZ / 1000) + 1;
    u8 s[16], h[32], g[32]; if (pin_load(s, h)) return 0; if (!pin || !pin_ok_fmt(pin)) return E_ACCES;
    pin_hash(s, pin, g); int d = 0; for (int i = 0; i < 32; i++) d |= g[i] ^ h[i];
    if (!d) { fails = 0; return 0; }
    if (++fails >= 5) { wait_until = ticks + (u64)HZ * 30 * (1u << MIN(fails - 5, 5)); kprintf("sesion: demasiados intentos, espera\n"); }
    return E_ACCES;
}
/* ---------- carpeta del invitado ---------- */
static void rm_tree(const char *p) { zdirent_t d; char q[256]; for (int i = 0; !vfs_readdir(p, i, &d);) { if (!strcmp(d.name, ".") || !strcmp(d.name, "..")) { i++; continue; } snprintf(q, 256, "%s/%s", p, d.name); if (d.is_dir) rm_tree(q); if (vfs_unlink(q)) i++; } vfs_unlink(p); }
static void copy_file(const char *a, const char *b) { u64 n; void *d = vfs_slurp(a, &n); if (!d) return; file_t *f; if (!vfs_open(b, O_WRITE | O_CREATE | O_TRUNC, &f)) { vfs_write(f, d, n); vfs_close(f); } kfree(d); }
static void guest_home(void) {
    rm_tree(HOME); vfs_mkdir("/rd/tmp"); vfs_mkdir(HOME); vfs_mkdir(HOME "/DOCS"); vfs_mkdir(HOME "/ESCRITORIO"); vfs_mkdir(HOME "/ZUNZUN");
    static const char *cp[] = {"AJUSTES.TXT", "TEMA.TXT", "IMPRESORAS.TXT"}; char a[64], b[96];
    for (int i = 0; i < 3; i++) { snprintf(a, 64, "/sys/ZUNZUN/%s", cp[i]); snprintf(b, 96, HOME "/ZUNZUN/%s", cp[i]); copy_file(a, b); }
}
/* ---------- reglas de acceso del invitado ---------- */
static int ieq(const char *a, const char *b, int n) { for (int i = 0; i < n; i++) { char x = a[i], y = b[i]; if (x >= 'a' && x <= 'z') x -= 32; if (y >= 'a' && y <= 'z') y -= 32; if (x != y) return 0; if (!x) return 1; } return 1; }
/* rest es lo que sigue a "/sys/"; coincide con un nombre exacto o con una carpeta y lo de dentro */
static int under(const char *rest, const char *n) { int l = strlen(n); return ieq(rest, n, l) && (rest[l] == 0 || rest[l] == '/'); }
int sess_path(char *p, int write) {
    /* el archivo del PIN no es accesible para ningun programa */
    { char *q = p; char t[256]; int k = 0; for (; *q && k < 255; q++) if (!(*q == '/' && k && t[k - 1] == '/')) t[k++] = *q; t[k] = 0; if (ieq(t, "/sys/ZUNZUN/SESION.TXT", 23)) return E_ACCES; }
    if (mode != 2) return 0;
    /* normaliza barras dobles; rechaza "." y ".." */
    char t[256]; int k = 0; for (char *q = p; *q && k < 255; q++) { if (*q == '/' && k && t[k - 1] == '/') continue; t[k++] = *q; } while (k > 1 && t[k - 1] == '/') k--; t[k] = 0;
    for (char *q = t; (q = sstr(q, "/.")); q++) if (q[2] == 0 || q[2] == '/' || (q[2] == '.' && (q[3] == 0 || q[3] == '/'))) return E_ACCES;
    if (ieq(t, "/sys", 5)) { strcpy(p, t); return write ? E_ROFS : 0; }
    if (ieq(t, "/sys/", 5)) {
        const char *r = t + 5;
        if (under(r, "ZUNZUN-PUNTOS")) return E_ACCES;
        if (under(r, "DIAGNOST.TXT")) { strcpy(p, t); return 0; }
        if (under(r, "PROGRAMAS") || under(r, "EFI") || under(r, "LEEME.TXT") || under(r, "ZUNZUN/KERNEL.BIN") || under(r, "ZUNZUN/INITRD.BIN")) { strcpy(p, t); return write ? E_ROFS : 0; }
        char o[256]; if (snprintf(o, 256, HOME "/%s", r) >= 256) return E_INVAL; strcpy(p, o); return 0;
    }
    /* /rd: solo lectura salvo /rd/tmp */
    if (ieq(t, "/rd/", 4) || ieq(t, "/rd", 4)) { if (write && !ieq(t, "/rd/tmp/", 8)) return E_ROFS; strcpy(p, t); return 0; }
    /* otros volumenes: solo memorias y tarjetas extraibles, nunca los discos internos */
    const char *rest; mount_t *m = vfs_resolve(t, &rest);
    if (m && m->dev && !m->dev->removable && !(m->dev->parent && m->dev->parent->removable)) return E_ACCES;
    strcpy(p, t); return 0;
}
static const char *ADMIN_ONLY[] = {"config", "instalar", "discos", "rescate", "restaurar"};
int sess_spawn_ok(const char *path) {
    if (mode != 2) return 1; const char *n = strrchr(path, '/'); n = n ? n + 1 : path;
    for (unsigned i = 0; i < sizeof ADMIN_ONLY / sizeof *ADMIN_ONLY; i++) if (ieq(n, ADMIN_ONLY[i], strlen(ADMIN_ONLY[i]) + 1)) return 0;
    return 1;
}
int sess_guest(void) { return mode == 2; }
static void kill_apps(void) { zproc_t l[64]; int n = proc_list(l, 64); for (int i = 0; i < n; i++) if (l[i].pid != cur->proc->pid) proc_kill(l[i].pid); for (int t = 0; t < 50; t++) { n = proc_list(l, 64); if (n <= 1) break; msleep(20); } }
/* llamada 109: a = operacion. Las que cambian la sesion solo las puede pedir el escritorio (proceso 1) */
long sess_syscall(u64 op, const char *b, const char *c) {
    if (op == 0) return mode | (sess_has_pin() ? 8 : 0) | (has_sys() ? 16 : 0);
    if (cur->proc->pid != 1) return E_ACCES;
    if (op == 1) { int r = pin_check(b); if (r) return r; mode = 1; kprintf("sesion: administrador\n"); return 0; }
    if (op == 2) { if (mode == 1) kill_apps(); guest_home(); mode = 2; kprintf("sesion: invitado\n"); return 0; }
    if (op == 3) { kill_apps(); if (mode == 2) rm_tree(HOME); kprintf("sesion: cerrada (%s)\n", mode == 2 ? "invitado" : "administrador"); mode = 0; return 0; }
    if (op == 4) { if (mode != 1) return E_ACCES; if (!c || !pin_ok_fmt(c)) return E_INVAL; if (sess_has_pin()) { int r = pin_check(b); if (r) return r; } int e = pin_save(c); kprintf("sesion: PIN guardado (%d)\n", e); return e; }
    if (op == 5) return mode ? pin_check(b) : E_ACCES; /* desbloquear la pantalla sin cambiar de sesion */
    return E_INVAL;
}
