/* Red local: intranet entre equipos ZunzunOS por cable (con router o con un cable directo entre dos equipos).
   Un equipo crea la red y es su administrador; invita a los demas (terminales), que tienen que aceptar en su
   pantalla tras comprobar que el codigo de 6 cifras es el mismo en los dos equipos (emparejamiento X25519).
   El administrador decide que carpetas se comparten, en que equipo estan y si se pueden cambiar.
   Sirve para cualquier numero de terminales: cada equipo anuncia su presencia y atiende peticiones.

   Dos modos del mismo programa:
     redlocal --servicio   en segundo plano (lo arranca el escritorio): anuncia el equipo (UDP 7140), atiende
                           invitaciones y peticiones de otros equipos (TCP 7141): listar, leer y escribir en las
                           carpetas compartidas de este equipo, mensajes y cambios de configuracion del administrador.
     redlocal              la ventana: equipos, carpetas compartidas y mensajes.
   Seguridad: clave de red de 32 bytes que solo conocen los equipos de la red. Cada conexion se autentica con
   reto y respuesta (HMAC-SHA256) y va cifrada con AES-GCM con una clave de sesion distinta (HKDF). */
#include "z.h"
#include "../lib/zcrypto.h"
#define PUDP 7140
#define PTCP 7141
#define CFG "/sys/ZUNZUN/RED.TXT"
#define IDF "/sys/ZUNZUN/RED-ID.TXT"
#define INBOX "/sys/ZUNZUN/RED-MENSAJES.TXT"
#define MAXM 64
#define MAXS 64
#define MAXF (256L << 20)

typedef struct { char id[12], name[32]; } mem_t;
typedef struct { char id[12], name[40], host[12], path[160]; int rw; } share_t;
static struct { char net[12], netname[40], admin[12]; u8 key[32]; int ver, nm, ns; mem_t m[MAXM]; share_t s[MAXS]; } R;
static char myid[12], myname[32];
static const char *tr_(const char *s) { return tr(s); }
static long atol(const char *s) { while (*s == ' ') s++; long v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + *s++ - '0'; return v; }

/* ---------- utilidades ---------- */
static void hex(const u8 *d, int n, char *o) { for (int i = 0; i < n; i++) { o[2 * i] = "0123456789abcdef"[d[i] >> 4]; o[2 * i + 1] = "0123456789abcdef"[d[i] & 15]; } o[2 * n] = 0; }
static int unhex(const char *s, u8 *o, int n) { for (int i = 0; i < n; i++) { int a = s[2 * i], b = a ? s[2 * i + 1] : 0; int x = a >= 'a' ? a - 87 : a - '0', y = b >= 'a' ? b - 87 : b - '0'; if (x < 0 || x > 15 || y < 0 || y > 15) return -1; o[i] = x << 4 | y; } return 0; }
static void safe(char *s) { for (; *s; s++) if (*s == '|' || *s == '\n' || *s == '\r') *s = ' '; }
static int split(char *s, char **f, int max) { int n = 0; f[n++] = s; for (char *p = s; *p && n < max; p++) if (*p == '|') { *p = 0; f[n++] = p + 1; } return n; }
static void kvs(const char *t, const char *k, char *o, int cap) { int l = strlen(k); o[0] = 0; for (const char *p = t; p && *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : 0) if (!strncmp(p, k, l) && p[l] == '=') { int i = 0; p += l + 1; while (p[i] && p[i] != '\n' && i < cap - 1) { o[i] = p[i]; i++; } o[i] = 0; return; } }

/* identidad del equipo: id de 8 cifras ligado a la MAC de la tarjeta de red. Si los datos se copian a otro
   equipo (otra MAC), ese equipo se hace un id nuevo; asi dos equipos nunca comparten id */
static char mymac[16];
static void ident(void) { znet ni; memset(&ni, 0, sizeof ni); z_netinfo(&ni); char mac[16]; hex(ni.mac, 6, mac); char *t = z_readfile(IDF, 0), smac[16] = "";
    if (t) { kvs(t, "id", myid, 12); kvs(t, "nombre", myname, 32); kvs(t, "mac", smac, 16); free(t); }
    zdirent dd; int innet = !z_stat(CFG, &dd);   /* ya en una red: se conserva el id aunque cambie la tarjeta (cable o anclaje USB) */
    int fresh = strlen(myid) != 8 || (!innet && strcmp(smac, mac));
    if (fresh) { u8 r[40], h[32]; zc_random(r, 32); memcpy(r + 32, ni.mac, 6); zc_hash(256, r, 38, h); hex(h, 4, myid); if (smac[0] && strcmp(smac, mac)) myname[0] = 0; }
    if (!myname[0]) snprintf(myname, 32, "Equipo-%c%c%c%c", toupper(myid[4]), toupper(myid[5]), toupper(myid[6]), toupper(myid[7]));
    strcpy(mymac, mac); if (fresh) { char b[100]; snprintf(b, 100, "id=%s\nnombre=%s\nmac=%s\n", myid, myname, mymac); z_mkdir("/sys/ZUNZUN"); z_writefile(IDF, b, strlen(b)); z_sync(); } }
static void ident_save(void) { char b[100]; snprintf(b, 100, "id=%s\nnombre=%s\nmac=%s\n", myid, myname, mymac); z_writefile(IDF, b, strlen(b)); z_sync(); }

static int cfg_text(sb_t *b) { char k[65]; hex(R.key, 32, k); sb_f(b, "ZRED1\nred=%s\nnombre=%s\nclave=%s\nadmin=%s\nversion=%d\n", R.net, R.netname, k, R.admin, R.ver);
    for (int i = 0; i < R.nm; i++) sb_f(b, "equipo=%s|%s\n", R.m[i].id, R.m[i].name); for (int i = 0; i < R.ns; i++) sb_f(b, "carpeta=%s|%s|%s|%s|%d\n", R.s[i].id, R.s[i].name, R.s[i].host, R.s[i].path, R.s[i].rw); return b->n; }
static int cfg_parse(char *t) { memset(&R, 0, sizeof R); if (!t || strncmp(t, "ZRED1\n", 6)) return -1; char v[80]; kvs(t, "red", R.net, 12); kvs(t, "nombre", R.netname, 40); kvs(t, "admin", R.admin, 12); kvs(t, "version", v, 16); R.ver = atoi(v);
    kvs(t, "clave", v, 80); if (unhex(v, R.key, 32)) { memset(&R, 0, sizeof R); return -1; }
    for (char *p = t; p && *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : 0) { char l[400]; int i = 0; while (p[i] && p[i] != '\n' && i < 399) { l[i] = p[i]; i++; } l[i] = 0; char *f[6];
        if (!strncmp(l, "equipo=", 7) && R.nm < MAXM) { int n = split(l + 7, f, 2); if (n == 2) { strncpy0(R.m[R.nm].id, f[0], 12); strncpy0(R.m[R.nm].name, f[1], 32); R.nm++; } }
        else if (!strncmp(l, "carpeta=", 8) && R.ns < MAXS) { int n = split(l + 8, f, 5); if (n == 5) { share_t *s = &R.s[R.ns++]; strncpy0(s->id, f[0], 12); strncpy0(s->name, f[1], 40); strncpy0(s->host, f[2], 12); strncpy0(s->path, f[3], 160); s->rw = atoi(f[4]); } } }
    return R.net[0] ? 0 : -1; }
static void cfg_load(void) { char *t = z_readfile(CFG, 0); if (cfg_parse(t)) memset(&R, 0, sizeof R); free(t); }
static int cfg_save(void) { sb_t b = {0}; cfg_text(&b); int e = z_writefile(CFG, b.p, b.n); free(b.p); z_sync(); return e; }
static int in_net(void) { return R.net[0] != 0; }
/* las carpetas que el administrador comparte en este equipo se crean si no existen */
static void mk_shares(void) { for (int i = 0; i < R.ns; i++) if (!strcmp(R.s[i].host, myid)) { char p[160]; strcpy(p, R.s[i].path); for (char *q = p + 1; *q; q++) if (*q == '/') { *q = 0; z_mkdir(p); *q = '/'; } z_mkdir(p); } }
static int is_admin(void) { return in_net() && !strcmp(R.admin, myid); }
static mem_t *member(const char *id) { for (int i = 0; i < R.nm; i++) if (!strcmp(R.m[i].id, id)) return &R.m[i]; return 0; }
static const char *mname(const char *id) { mem_t *m = member(id); return m ? m->name : id; }

/* ---------- equipos vistos (anuncios UDP) ---------- */
typedef struct { char id[12], name[32], net[12], netname[40]; char role; u32 ip; u64 seen; } peer_t;
static peer_t P[128]; static int np, service_mode;
/* el servicio apunta los equipos que oye (sus anuncios llegan al servicio, no a la ventana) y la ventana los lee */
#define PEERS "/rd/tmp/red-equipos.txt"
static void peers_save(void) { sb_t b = {0}; sb_s(&b, ""); for (int i = 0; i < np; i++) sb_f(&b, "%s|%s|%s|%s|%c|%u|%lu\n", P[i].id, P[i].name, P[i].net, P[i].netname, P[i].role, P[i].ip, (unsigned long)P[i].seen); z_mkdir("/rd/tmp"); z_writefile(PEERS, b.p, b.n); free(b.p); }
static peer_t *peer(const char *id);
static void peers_load(void) { char *t = z_readfile(PEERS, 0); if (!t) return; for (char *p = t; p && *p; ) { char *e = strchr(p, '\n'); if (e) *e = 0; char *f[8]; int k = split(p, f, 8);
        if (k == 7) { u64 seen = (u64)atol(f[6]); peer_t *q = peer(f[0]); if (!q && np < 128) { q = &P[np++]; memset(q, 0, sizeof *q); } if (q && seen >= q->seen && (u32)atol(f[5])) { strncpy0(q->id, f[0], 12); strncpy0(q->name, f[1], 32); strncpy0(q->net, f[2], 12); strncpy0(q->netname, f[3], 40); q->role = f[4][0]; q->ip = (u32)atol(f[5]); q->seen = seen; } }
        p = e ? e + 1 : 0; } free(t); }
static peer_t *peer(const char *id) { for (int i = 0; i < np; i++) if (!strcmp(P[i].id, id)) return &P[i]; return 0; }
static int online(const char *id) { if (!strcmp(id, myid)) return 1; peer_t *p = peer(id); return p && z_ms() - p->seen < 45000; }
static void soy(char *o, int cap) { char nm[32], nn[40]; strcpy(nm, myname); strcpy(nn, R.netname); safe(nm); safe(nn); snprintf(o, cap, "ZRED1|SOY|%s|%s|%s|%s|%c", myid, nm, in_net() ? R.net : "-", in_net() ? nn : "-", is_admin() ? 'A' : in_net() ? 'T' : 'L'); }
static void on_udp(int us, char *m, long n, u32 ip, int port) { m[n] = 0; char *f[8]; int k = split(m, f, 8); if (k < 2 || strcmp(f[0], "ZRED1")) return;
    if (!strcmp(f[1], "BUSCA")) { char o[200]; soy(o, 200); znet ni; if (z_netinfo(&ni) || !ni.ip || !ip) return; z_sendto(us, ip, port, o, strlen(o)); }   /* sin direccion todavia no se contesta */
    else if (!strcmp(f[1], "SOY") && k >= 7 && strcmp(f[2], myid) && ip) { peer_t *p = peer(f[2]); if (!p && np < 128) { p = &P[np++]; char lg[80]; snprintf(lg, 80, "[prueba] red encontrado %s", f[3]); z_log(lg); } if (!p) return; strncpy0(p->id, f[2], 12); strncpy0(p->name, f[3], 32); strncpy0(p->net, f[4], 12); strncpy0(p->netname, f[5], 40); p->role = f[6][0]; p->ip = ip; p->seen = z_ms(); if (service_mode) peers_save(); } }

/* ---------- conexion cifrada ---------- */
typedef struct { int s; zgcm g; u64 tx, rx; int dir; u8 rb[512]; int rn, rp; } conn_t;
static int raw_send(conn_t *c, const void *d, long n) { const u8 *p = d; while (n > 0) { long r = z_send(c->s, p, n); if (r <= 0) return -1; p += r; n -= r; } return 0; }
static int raw_recv(conn_t *c, void *d, long n, int to) { u8 *p = d; while (n > 0) { if (c->rp < c->rn) { long k = MIN(n, (long)(c->rn - c->rp)); memcpy(p, c->rb + c->rp, k); c->rp += k; p += k; n -= k; continue; }
        long r = z_recv(c->s, c->rb, sizeof c->rb, to); if (r <= 0) return -1; c->rn = r; c->rp = 0; } return 0; }
static int line(conn_t *c, char *o, int cap, int to) { int i = 0; for (;;) { char ch; if (raw_recv(c, &ch, 1, to)) return -1; if (ch == '\n') break; if (i < cap - 1) o[i++] = ch; } o[i] = 0; return i; }
static int lsend(conn_t *c, const char *s) { if (raw_send(c, s, strlen(s))) return -1; return raw_send(c, "\n", 1); }
static void ivf(u8 *iv, int dir, u64 n) { memset(iv, 0, 12); iv[0] = dir; for (int i = 0; i < 8; i++) iv[4 + i] = n >> (8 * i); }
static int csend(conn_t *c, const void *d, long n) { u8 *b = malloc(n + 20); if (!b) return -1; b[0] = n; b[1] = n >> 8; b[2] = n >> 16; b[3] = n >> 24; u8 iv[12]; ivf(iv, c->dir, c->tx++);
    gcm_seal(&c->g, iv, b, 4, d, n, b + 4, b + 4 + n); int r = raw_send(c, b, n + 20); free(b); return r; }
static int csends(conn_t *c, const char *s) { return csend(c, s, strlen(s)); }
static long crecv(conn_t *c, u8 **out, int to) { u8 h[4]; if (raw_recv(c, h, 4, to)) return -1; long n = h[0] | h[1] << 8 | h[2] << 16 | (long)h[3] << 24; if (n < 0 || n > (2L << 20)) return -1;
    u8 *b = malloc(n + 17); if (raw_recv(c, b, n + 16, to)) { free(b); return -1; } u8 iv[12]; ivf(iv, !c->dir, c->rx++); u8 *pl = malloc(n + 1);
    if (gcm_open(&c->g, iv, h, 4, b, n, pl, b + n)) { free(b); free(pl); return -1; }   /* 0 = la etiqueta cuadra */ free(b); pl[n] = 0; *out = pl; return n; }
static void sess_key(conn_t *c, const u8 *nc, const u8 *ns) { u8 salt[32], prk[32], k[16]; memcpy(salt, nc, 16); memcpy(salt + 16, ns, 16); zc_hkdf_extract(256, salt, 32, R.key, 32, prk); zc_hkdf_expand(256, prk, (const u8 *)"zred1 sesion", 12, k, 16); gcm_init(&c->g, k, 16); }
static void proof(const u8 *nc, const u8 *ns, char who, char *o) { u8 m[33], h[32]; memcpy(m, nc, 16); memcpy(m + 16, ns, 16); m[32] = who; zc_hmac(256, R.key, 32, m, 33, h); hex(h, 32, o); }
/* cliente: conecta con el equipo id y se autentica */
static int cconnect(conn_t *c, const char *id, char *err) { memset(c, 0, sizeof *c); peer_t *p = peer(id); if (!p || !online(id)) { strcpy(err, tr_("el equipo no está en línea")); return -1; }
    c->s = z_socket(1); if (c->s < 0 || z_connect(c->s, p->ip, PTCP, 5000)) { if (c->s >= 0) z_close(c->s); strcpy(err, tr_("no se pudo conectar")); return -1; }
    u8 nc[16], ns[16]; zc_random(nc, 16); char h[40], l[200]; hex(nc, 16, h); snprintf(l, 200, "ZRED1|HOLA|%s|%s|%s", R.net, myid, h); lsend(c, l);
    char *f[4]; if (line(c, l, 200, 5000) < 0 || split(l, f, 4) < 2 || strcmp(f[0], "RETO") || unhex(f[1], ns, 16)) { z_close(c->s); strcpy(err, tr_("el otro equipo no acepta esta red")); return -1; }
    char pr[65]; proof(nc, ns, 'c', pr); snprintf(l, 200, "PRUEBA|%s", pr); lsend(c, l); char want[65]; proof(nc, ns, 's', want);
    if (line(c, l, 200, 5000) < 0 || strncmp(l, "BIEN|", 5) || strcmp(l + 5, want)) { z_close(c->s); strcpy(err, tr_("clave de red distinta: el equipo necesita otra invitación")); return -1; }
    sess_key(c, nc, ns); c->dir = 0; return 0; }
/* una peticion y su respuesta (texto); payload opcional despues de la cabecera */
static long request(const char *id, const char *hdr, const void *pay, long pn, u8 **resp, char *err) { conn_t c; if (cconnect(&c, id, err)) return -1;
    if (csends(&c, hdr) || (pay && csend(&c, pay, pn))) { z_close(c.s); strcpy(err, tr_("se cortó la conexión")); return -1; }
    long n = crecv(&c, resp, 30000); z_close(c.s); if (n < 0) { strcpy(err, tr_("sin respuesta")); return -1; }
    if (strncmp((char *)*resp, "OK", 2)) { snprintf(err, 120, "%s", (char *)*resp + (strncmp((char *)*resp, "ERR|", 4) ? 0 : 4)); free(*resp); *resp = 0; return -1; } return n; }

/* ---------- carpetas: rutas seguras dentro de la carpeta compartida ---------- */
static share_t *share(const char *id) { for (int i = 0; i < R.ns; i++) if (!strcmp(R.s[i].id, id)) return &R.s[i]; return 0; }
static int resolve(share_t *s, const char *rel, char *o, int cap) { if (strstr(rel, "..") || strchr(rel, '|')) return -1; while (*rel == '/') rel++; snprintf(o, cap, "%s%s%s", s->path, *rel ? "/" : "", rel); int l = strlen(o); while (l > 1 && o[l - 1] == '/') o[--l] = 0; return 0; }

/* ======================= servicio ======================= */
static zwin *note; static char note_t[600]; static u64 note_until;
static void note_draw(void) { if (!note) return; g_clear(note, UI_PANEL); g_frame(note, 0, 0, note->w, note->h, UI_ACC); g_textb(note, 14, 12, tr_("Red local"), UI_TEXT); int y = 36; const char *p = note_t; char l[80];
    while (*p && y < note->h - 20) { int k = 0; while (p[k] && p[k] != '\n' && k < 54) k++; if (p[k] && p[k] != '\n') { int b = k; while (b > 0 && p[b] != ' ') b--; if (b > 10) k = b; } memcpy(l, p, k); l[k] = 0; g_text(note, 14, y, l, UI_TEXT); y += 18; p += k; while (*p == ' ' || *p == '\n') p++; } zw_present(note); }
static void notify(const char *t) { strncpy0(note_t, t, 600); zsys s; z_sysinfo(&s); if (!note) note = zw_new("Red local", s.scr_w - 470, s.scr_h - 220, 460, 150, WF_POPUP); note_until = z_ms() + 12000; note_draw(); }
/* invitacion: este equipo (sin red) recibe la invitacion del administrador y el usuario acepta o no */
static int ask_accept(const char *who, const char *net, const char *code) { zsys s; z_sysinfo(&s); zwin *w = zw_new("Invitación a una red local", (s.scr_w - 520) / 2, (s.scr_h - 250) / 2, 520, 250, WF_DECOR); if (!w) return 0;
    int r = -1; u64 t0 = z_ms(); zevent e;
    while (r < 0 && z_ms() - t0 < 120000) { g_clear(w, UI_BG); char b[200]; snprintf(b, 200, tr_("%s te invita a la red \"%s\"."), who, net); g_textb(w, 20, 20, b, UI_TEXT);
        g_text(w, 20, 50, tr_("Comprueba que el administrador ve el mismo código:"), UI_MUTED); g_text2(w, 150, 80, code, UI_ACC, 3); g_text(w, 20, 140, tr_("Si es el mismo, acepta para unir este equipo a la red."), UI_MUTED);
        ui_button(w, 260, 190, 120, 32, "Aceptar", 2); ui_button(w, 390, 190, 110, 32, "Rechazar", 0); zw_present(w);
        if (!z_event(&e, 1000) || e.win != (u32)w->id) continue; if (e.type == EV_CLOSE) r = 0; else if (e.type == EV_DOWN && e.y >= 190 && e.y < 222) { if (e.x >= 260 && e.x < 380) r = 1; else if (e.x >= 390 && e.x < 500) r = 0; } else if (e.type == EV_KEY && e.ch == 27) r = 0; }
    zw_close(w); return r == 1; }
static void sas(const u8 *sh, const u8 *a, const u8 *b, char *o) { zsha256 c; u8 h[32]; sha256_init(&c); sha256_up(&c, sh, 32); sha256_up(&c, a, 32); sha256_up(&c, b, 32); sha256_end(&c, h); u32 v = ((u32)h[0] << 24 | h[1] << 16 | h[2] << 8 | h[3]) % 1000000; snprintf(o, 12, "%03u %03u", v / 1000, v % 1000); }
static void pair_key(const u8 *sh, u8 *k) { u8 prk[32]; zc_hkdf_extract(256, (const u8 *)"zred1", 5, sh, 32, prk); zc_hkdf_expand(256, prk, (const u8 *)"zred1 invitacion", 16, k, 16); }
static void srv_invite(conn_t *c, char **f, int k) {
    if (in_net() || k < 7) { lsend(c, "NO|ya estoy en una red"); return; } u8 apub[32], priv[32], pub[32], sh[32]; if (unhex(f[6], apub, 32)) return;
    zc_random(priv, 32); x25519_base(pub, priv); x25519(sh, priv, apub); char ph[65], l[200], code[12]; hex(pub, 32, ph); char nm[32]; strcpy(nm, myname); safe(nm); snprintf(l, 200, "PUB|%s|%s|%s", ph, myid, nm); lsend(c, l); sas(sh, apub, pub, code);
    z_log("[prueba] red invitacion recibida"); int ok = ask_accept(f[5], f[3], code); lsend(c, ok ? "ACEPTO" : "RECHAZO"); if (!ok) { z_log("[prueba] red invitacion rechazada"); return; } z_log("[prueba] red invitacion aceptada");
    u8 h[4]; if (raw_recv(c, h, 4, 15000)) { z_log("[prueba] red invitacion: no llego la configuracion"); return; } long n = h[0] | h[1] << 8 | h[2] << 16 | (long)h[3] << 24; if (n <= 0 || n > 200000) { z_log("[prueba] red invitacion: tamano mal"); return; } u8 *b = malloc(n + 16), *pl = malloc(n + 1);
    if (raw_recv(c, b, n + 16, 15000)) { free(b); free(pl); return; } u8 kk[16], iv[12] = {0}; pair_key(sh, kk); zgcm g; gcm_init(&g, kk, 16);
    if (gcm_open(&g, iv, h, 4, b, n, pl, b + n)) { free(b); free(pl); lsend(c, "MAL"); z_log("[prueba] red invitacion: no se pudo descifrar"); return; } pl[n] = 0; free(b);
    if (cfg_parse((char *)pl)) z_log("[prueba] red invitacion: configuracion no valida");
    if (cfg_parse((char *)pl) == 0) { cfg_save(); mk_shares(); lsend(c, "LISTO"); char m[200]; snprintf(m, 200, tr_("Este equipo ya forma parte de la red \"%s\"."), R.netname); notify(m); char lg[120]; snprintf(lg, 120, "[prueba] red unido a %s como %s", R.net, myid); z_log(lg); } free(pl); }
static void srv_session(conn_t *c, char **f, int k) {
    if (!in_net() || k < 5 || strcmp(f[2], R.net)) { lsend(c, "NO"); return; } char who[12]; strncpy0(who, f[3], 12); if (!member(who) && strcmp(who, R.admin)) { lsend(c, "NO"); return; }
    u8 nc[16], ns[16]; if (unhex(f[4], nc, 16)) return; zc_random(ns, 16); char h[40], l[200]; hex(ns, 16, h); snprintf(l, 200, "RETO|%s", h); lsend(c, l);
    char want[65]; proof(nc, ns, 'c', want); if (line(c, l, 200, 5000) < 0 || strncmp(l, "PRUEBA|", 7) || strcmp(l + 7, want)) { z_log("[prueba] red prueba incorrecta"); return; }
    char pr[65]; proof(nc, ns, 's', pr); snprintf(l, 200, "BIEN|%s", pr); lsend(c, l); sess_key(c, nc, ns); c->dir = 1;
    u8 *q; long n; while ((n = crecv(c, &q, 20000)) >= 0) { char *a[6]; char hd[600]; strncpy0(hd, (char *)q, 600); free(q); int na = split(hd, a, 6); char lg[200]; snprintf(lg, 200, "[prueba] red %s de %s", a[0], mname(who)); z_log(lg);
        if (!strcmp(a[0], "LISTA") && na >= 3) { share_t *s = share(a[1]); char p[400]; if (!s || strcmp(s->host, myid) || resolve(s, a[2], p, 400)) { csends(c, "ERR|carpeta no disponible"); continue; }
            sb_t o = {0}; sb_s(&o, "OK\n"); zdirent e; for (int i = 0; !z_readdir(p, i, &e); i++) { if (e.name[0] == '.') continue; sb_f(&o, "%c|%lu|%s\n", e.is_dir ? 'D' : 'F', (unsigned long)e.size, e.name); } csend(c, o.p, o.n); free(o.p); }
        else if (!strcmp(a[0], "LEE") && na >= 3) { share_t *s = share(a[1]); char p[400]; long fl; u8 *d; if (!s || strcmp(s->host, myid) || resolve(s, a[2], p, 400) || !(d = z_readfile(p, &fl))) { csends(c, "ERR|no se pudo leer"); continue; }
            char ok[40]; snprintf(ok, 40, "OK|%ld", fl); csends(c, ok); for (long o = 0; o < fl; o += 65536) csend(c, d + o, MIN(65536L, fl - o)); free(d); }
        else if (!strcmp(a[0], "ESCRIBE") && na >= 4) { share_t *s = share(a[1]); char p[400]; long sz = atol(a[3]), got = 0; sb_t d = {0}; sb_add(&d, "", 0);
            while (got < sz) { u8 *q2; long m = crecv(c, &q2, 20000); if (m < 0) break; sb_add(&d, q2, m); free(q2); got += m; }
            if (got != sz) break; if (!s || strcmp(s->host, myid) || !s->rw || resolve(s, a[2], p, 400) || sz > MAXF) { csends(c, s && !s->rw ? "ERR|carpeta de solo lectura" : "ERR|no se pudo escribir"); free(d.p); continue; }
            int e = z_writefile(p, d.p, d.n); free(d.p); z_sync(); csends(c, e ? "ERR|no se pudo escribir" : "OK"); }
        else if ((!strcmp(a[0], "BORRA") || !strcmp(a[0], "CARPETA")) && na >= 3) { share_t *s = share(a[1]); char p[400]; if (!s || strcmp(s->host, myid) || !s->rw || resolve(s, a[2], p, 400) || !a[2][0]) { csends(c, "ERR|no permitido"); continue; }
            int e = a[0][0] == 'B' ? z_unlink(p) : z_mkdir(p); z_sync(); csends(c, e ? "ERR|no se pudo" : "OK"); }
        else if (!strcmp(a[0], "RENOMBRA") && na >= 4) { share_t *s = share(a[1]); char p[400], q3[400], r2[400]; char *sl = strrchr(a[2], '/'); snprintf(r2, 400, "%.*s%s%s", sl ? (int)(sl - a[2]) : 0, a[2], sl ? "/" : "", a[3]);
            if (!s || strcmp(s->host, myid) || !s->rw || strchr(a[3], '/') || resolve(s, a[2], p, 400) || resolve(s, r2, q3, 400)) { csends(c, "ERR|no permitido"); continue; } int e = z_rename(p, q3); z_sync(); csends(c, e ? "ERR|no se pudo" : "OK"); }
        else if (!strcmp(a[0], "MENSAJE") && na >= 2) { u32 t[6]; z_time(t); char *old = z_readfile(INBOX, 0); sb_t o = {0}; sb_f(&o, "%02u/%02u %02u:%02u|%s|", t[3], t[4], t[2], t[1], mname(who)); sb_s(&o, a[1]); sb_s(&o, "\n"); if (old) { sb_s(&o, old); free(old); }
            z_writefile(INBOX, o.p, MIN(o.n, 200000L)); free(o.p); z_sync(); csends(c, "OK"); char m[600]; snprintf(m, 600, "%s %s:\n%s", tr_("Mensaje de"), mname(who), a[1]); notify(m); }
        else if (!strcmp(a[0], "CONFIG") && na >= 2) { if (strcmp(who, R.admin)) { csends(c, "ERR|solo el administrador"); continue; } u8 *t2; long m = crecv(c, &t2, 20000); if (m < 0) break;
            char key[32]; memcpy(key, R.key, 32); char net[12]; strcpy(net, R.net); int ver = R.ver; if (cfg_parse((char *)t2) || strcmp(R.net, net)) { cfg_load(); csends(c, "ERR|configuracion no valida"); free(t2); continue; }
            if (R.ver < ver) { cfg_load(); csends(c, "OK"); free(t2); continue; } if (!member(myid)) { z_unlink(CFG); memset(&R, 0, sizeof R); z_sync(); z_log("[prueba] red sacado de la red"); notify(tr_("El administrador ha sacado este equipo de la red local.")); csends(c, "OK"); free(t2); break; }
            cfg_save(); mk_shares(); free(t2); csends(c, "OK"); (void)key; char lg2[80]; snprintf(lg2, 80, "[prueba] red configuracion %d", R.ver); z_log(lg2); }
        else if (!strcmp(a[0], "PIDECONFIG")) { sb_t o = {0}; sb_s(&o, "OK\n"); cfg_text(&o); csend(c, o.p, o.n); free(o.p); }
        else csends(c, "ERR|peticion desconocida"); } }
static int service(void) { service_mode = 1;
    ident(); cfg_load(); int us = z_socket(2), ts = z_socket(1); if (us < 0 || ts < 0 || z_bind(us, PUDP) || z_bind(ts, PTCP)) { z_log("[prueba] red servicio: no se pudo abrir los puertos"); return 1; }
    char lg[120]; snprintf(lg, 120, "[prueba] red servicio listo: %s %s (%s)", myid, myname, in_net() ? R.net : "sin red"); z_log(lg); u64 last_soy = 0, last_cfg = 0; u8 m[1500];
    for (;;) { u64 now = z_ms(); if (now - last_soy > 15000) { last_soy = now; char o[200]; cfg_load(); soy(o, 200); z_sendto(us, 0xFFFFFFFF, PUDP, o, strlen(o)); }
        u32 fr[2]; long n; while ((n = z_recvfrom(us, m, sizeof m - 1, 20, fr)) > 0) on_udp(us, (char *)m, n, fr[0], fr[1]);
        int cs = z_accept(ts, 150); if (cs >= 0) { conn_t c; memset(&c, 0, sizeof c); c.s = cs; char l[400]; if (line(&c, l, 400, 5000) > 0) { char *f[8]; int k = split(l, f, 8); if (k >= 2 && !strcmp(f[0], "ZRED1")) { if (!strcmp(f[1], "INVITA")) srv_invite(&c, f, k); else if (!strcmp(f[1], "HOLA")) srv_session(&c, f, k); } } z_close(cs); }
        /* un terminal que vuelve pide al administrador la configuracion por si cambio mientras estaba apagado */
        if (in_net() && !is_admin() && now - last_cfg > 60000 && online(R.admin)) { last_cfg = now; u8 *r; char err[120]; if (request(R.admin, "PIDECONFIG", 0, 0, &r, err) > 0) { int ver = R.ver; char *t = strchr((char *)r, '\n'); if (t && !cfg_parse(t + 1) && R.ver >= ver) { if (member(myid)) cfg_save(); else { z_unlink(CFG); memset(&R, 0, sizeof R); z_sync(); } } else cfg_load(); free(r); } }
        if (note) { zevent e; while (z_event(&e, 0)) { if (e.win == (u32)note->id && (e.type == EV_DOWN || e.type == EV_CLOSE)) note_until = 0; else if (e.type == EV_REDRAW) note_draw(); } if (z_ms() > note_until) { zw_close(note); note = 0; } } } }

/* ======================= ventana ======================= */
static zwin *W; static int tab, sel_sh = -1, nent, etop, us = -1; static char msg[300], cwd[300]; static u64 last_busca;
typedef struct { char d; long size; char name[200]; } ent_t; static ent_t E[400];
static char compose[400]; static int ccur, cfoc;
static void busca(void) { if (us < 0) { us = z_socket(2); } char o[] = "ZRED1|BUSCA"; z_sendto(us, 0xFFFFFFFF, PUDP, o, strlen(o)); last_busca = z_ms(); }
static void poll_udp(void) { static u64 lp; if (z_ms() - lp > 2000) { lp = z_ms(); peers_load(); } if (us < 0) return; u8 m[1500]; u32 fr[2]; long n; while ((n = z_recvfrom(us, m, sizeof m - 1, 0, fr)) > 0) on_udp(us, (char *)m, n, fr[0], fr[1]); }
static int service_running(void) { zproc p[64]; int n = z_procs(p, 64), c = 0; for (int i = 0; i < n; i++) if (!strcmp(p[i].name, "redlocal")) c++; return c >= 2; }
static void push_to(char (*ids)[12], int n) { R.ver++; cfg_save(); sb_t b = {0}; cfg_text(&b); int ok = 0, fail = 0;
    for (int i = 0; i < n; i++) { if (!strcmp(ids[i], myid) || !online(ids[i])) continue; conn_t c; char err[120]; if (cconnect(&c, ids[i], err)) { fail++; continue; } char h[40]; snprintf(h, 40, "CONFIG|%d", R.ver); u8 *r = 0;
        if (!csends(&c, h) && !csend(&c, b.p, b.n) && crecv(&c, &r, 15000) > 0 && !strncmp((char *)r, "OK", 2)) ok++; else fail++; free(r); z_close(c.s); }
    free(b.p); snprintf(msg, 300, tr_("Cambios enviados a %d equipos%s"), ok, fail ? tr_(" (algunos no respondieron; los recibirán al volver)") : ""); char lg[80]; snprintf(lg, 80, "[prueba] red configuracion enviada %d", R.ver); z_log(lg); }
static void push_config(void) { char ids[MAXM][12]; for (int i = 0; i < R.nm; i++) strcpy(ids[i], R.m[i].id); push_to(ids, R.nm); }
static void create_net(void) { char n[40] = "Mi red"; if (!ui_prompt("Crear una red local", "Nombre de la red:", n, 38) || !n[0]) return; safe(n); memset(&R, 0, sizeof R); u8 r[4]; zc_random(r, 4); hex(r, 4, R.net); strcpy(R.netname, n); zc_random(R.key, 32); strcpy(R.admin, myid); R.ver = 1;
    strcpy(R.m[0].id, myid); strcpy(R.m[0].name, myname); R.nm = 1; cfg_save(); snprintf(msg, 300, tr_("Red \"%s\" creada. Ahora invita a los demás equipos."), n); char lg[80]; snprintf(lg, 80, "[prueba] red creada %s", R.net); z_log(lg); }
static void invite(peer_t *p) { msg[0] = 0; u8 priv[32], pub[32], bpub[32], sh[32]; zc_random(priv, 32); x25519_base(pub, priv); conn_t c; memset(&c, 0, sizeof c); c.s = z_socket(1);
    if (z_connect(c.s, p->ip, PTCP, 5000)) { z_close(c.s); snprintf(msg, 300, "%s: %s", p->name, tr_("no se pudo conectar")); return; }
    char ph[65], l[400], nm[32], nn[40]; hex(pub, 32, ph); strcpy(nm, myname); strcpy(nn, R.netname); safe(nm); safe(nn); snprintf(l, 400, "ZRED1|INVITA|%s|%s|%s|%s|%s", R.net, nn, myid, nm, ph); lsend(&c, l);
    char *f[5]; if (line(&c, l, 400, 8000) < 0 || split(l, f, 5) < 4 || strcmp(f[0], "PUB") || unhex(f[1], bpub, 32)) { z_close(c.s); snprintf(msg, 300, "%s: %s", p->name, tr_("ya está en otra red")); return; }
    char bid[12], bname[32]; strncpy0(bid, f[2], 12); strncpy0(bname, f[3], 32); x25519(sh, priv, bpub); char code[12]; sas(sh, pub, bpub, code);
    g_rect(W, 0, W->h - 120, W->w, 98, UI_ACC2); char t[200]; snprintf(t, 200, tr_("Esperando a que %s acepte. Su pantalla debe mostrar este código:"), bname); g_text(W, 20, W->h - 110, t, UI_TEXT); g_text2(W, 20, W->h - 86, code, UI_ACC, 3); zw_present(W);
    char lg[80]; snprintf(lg, 80, "[prueba] red codigo %s", code); z_log(lg);
    if (line(&c, l, 400, 125000) < 0 || strcmp(l, "ACEPTO")) { z_close(c.s); snprintf(msg, 300, tr_("%s no aceptó la invitación"), bname); return; }
    if (!member(bid) && R.nm < MAXM) { strcpy(R.m[R.nm].id, bid); strcpy(R.m[R.nm].name, bname); R.nm++; } R.ver++; sb_t b = {0}; cfg_text(&b); u8 *o = malloc(b.n + 20); o[0] = b.n; o[1] = b.n >> 8; o[2] = b.n >> 16; o[3] = b.n >> 24;
    u8 kk[16], iv[12] = {0}; pair_key(sh, kk); zgcm g; gcm_init(&g, kk, 16); gcm_seal(&g, iv, o, 4, (u8 *)b.p, b.n, o + 4, o + 4 + b.n); raw_send(&c, o, b.n + 20); free(o); free(b.p);
    int ok = line(&c, l, 400, 10000) >= 0 && !strcmp(l, "LISTO"); z_close(c.s); { char lg2[120]; snprintf(lg2, 120, "[prueba] red invitacion respuesta final: %s", ok ? "LISTO" : l); z_log(lg2); } if (!ok) { snprintf(msg, 300, "%s: %s", bname, tr_("no se completó la invitación")); return; }
    cfg_save(); p->net[0] = 0; strcpy(p->net, R.net); p->role = 'T'; push_config(); snprintf(msg, 300, tr_("%s ya forma parte de la red"), bname); snprintf(lg, 80, "[prueba] red invitado %s", bid); z_log(lg); }
static void remove_member(int i) { if (i <= 0 || i >= R.nm || !strcmp(R.m[i].id, myid)) return; char q[160]; snprintf(q, 160, tr_("¿Sacar a %s de la red? Se cambiará la clave de la red."), R.m[i].name); if (!ui_confirm("Sacar de la red", q)) return;
    char gone[12]; strcpy(gone, R.m[i].id); memmove(&R.m[i], &R.m[i + 1], (R.nm - i - 1) * sizeof R.m[0]); R.nm--; for (int k = 0; k < R.ns; k++) if (!strcmp(R.s[k].host, gone)) { memmove(&R.s[k], &R.s[k + 1], (R.ns - k - 1) * sizeof R.s[0]); R.ns--; k--; }
    /* primero se avisa al equipo que sale (con la clave antigua) y despues se cambia la clave para los demas */
    { conn_t c; char err[120]; if (online(gone) && !cconnect(&c, gone, err)) { sb_t b = {0}; R.ver++; cfg_text(&b); char h[40]; snprintf(h, 40, "CONFIG|%d", R.ver); u8 *r = 0; csends(&c, h); csend(&c, b.p, b.n); crecv(&c, &r, 8000); free(r); free(b.p); z_close(c.s); } }
    u8 old[32]; memcpy(old, R.key, 32); sb_t b = {0}; zc_random(R.key, 32); R.ver++; cfg_text(&b); u8 nk[32]; memcpy(nk, R.key, 32); memcpy(R.key, old, 32); int sent = 0;
    for (int k = 0; k < R.nm; k++) { if (!strcmp(R.m[k].id, myid) || !online(R.m[k].id)) continue; conn_t c; char err[120]; if (cconnect(&c, R.m[k].id, err)) continue; char h[40]; snprintf(h, 40, "CONFIG|%d", R.ver); u8 *r = 0; csends(&c, h); csend(&c, b.p, b.n); if (crecv(&c, &r, 8000) > 0) sent++; free(r); z_close(c.s); }
    memcpy(R.key, nk, 32); free(b.p); cfg_save(); snprintf(msg, 300, tr_("Equipo sacado de la red. Clave nueva enviada a %d equipos (los apagados necesitarán otra invitación)."), sent); }
static void add_share(void) { const char *it[MAXM]; int map[MAXM], n = 0; for (int i = 0; i < R.nm; i++) if (online(R.m[i].id)) { map[n] = i; it[n++] = R.m[i].name; } if (!n) return;
    int sx, sy; zw_screen(W, 200, 120, &sx, &sy); int r = n == 1 ? 0 : ui_menu(sx, sy, it, n); if (r < 0) return; share_t s; memset(&s, 0, sizeof s); strcpy(s.host, R.m[map[r]].id);
    strcpy(s.name, "Compartida"); if (!ui_prompt("Carpeta compartida", "Nombre que verán los demás:", s.name, 38) || !s.name[0]) return; safe(s.name);
    strcpy(s.path, "/sys/DOCS/Compartida"); char q[200]; snprintf(q, 200, tr_("Carpeta en el equipo %s:"), R.m[map[r]].name); if (!ui_prompt("Carpeta compartida", q, s.path, 158) || s.path[0] != '/') return; safe(s.path);
    static const char *rw[] = {"Solo lectura", "Lectura y escritura"}; int k = ui_menu(sx, sy + 40, rw, 2); if (k < 0) return; s.rw = k; u8 rr[3]; zc_random(rr, 3); hex(rr, 3, s.id);
    if (R.ns < MAXS) R.s[R.ns++] = s; mk_shares(); push_config(); char lg[160]; snprintf(lg, 160, "[prueba] red carpeta %s en %s", s.name, mname(s.host)); z_log(lg); }
static int local(void) { return sel_sh >= 0 && !strcmp(R.s[sel_sh].host, myid); }
static void list_dir(void) { nent = 0; etop = 0; if (sel_sh < 0 || sel_sh >= R.ns) return; share_t *s = &R.s[sel_sh]; char h[400], err[120]; snprintf(h, 400, "LISTA|%s|%s", s->id, cwd); u8 *r; strcpy(msg, tr_("Leyendo la carpeta..."));
    if (local()) { char p[400]; sb_t o = {0}; sb_s(&o, "OK\n"); if (!resolve(s, cwd, p, 400)) { zdirent e; for (int i = 0; !z_readdir(p, i, &e); i++) if (e.name[0] != '.') sb_f(&o, "%c|%lu|%s\n", e.is_dir ? 'D' : 'F', (unsigned long)e.size, e.name); } r = (u8 *)o.p; }
    else if (request(s->host, h, 0, 0, &r, err) < 0) { snprintf(msg, 300, "%s: %s", s->name, err); return; }
    for (char *p = strchr((char *)r, '\n'); p && nent < 400; ) { p++; char *e = strchr(p, '\n'); if (!e) break; *e = 0; char *f[3]; if (split(p, f, 3) == 3) { E[nent].d = f[0][0]; E[nent].size = atol(f[1]); strncpy0(E[nent].name, f[2], 200); nent++; } p = e; }
    for (int i = 0; i < nent; i++) for (int j = i + 1; j < nent; j++) if ((E[j].d == 'D') > (E[i].d == 'D') || ((E[j].d == 'D') == (E[i].d == 'D') && strcasecmp(E[j].name, E[i].name) < 0)) { ent_t t = E[i]; E[i] = E[j]; E[j] = t; }
    free(r); snprintf(msg, 300, tr_("%d elementos"), nent); char lg[120]; snprintf(lg, 120, "[prueba] red lista %s/%s: %d", s->name, cwd, nent); z_log(lg); }
static int fetch(ent_t *e, char *dst, int cap, int open) { share_t *s = &R.s[sel_sh]; char rel[400], h[500], err[120]; snprintf(rel, 400, "%s%s%s", cwd, cwd[0] ? "/" : "", e->name); snprintf(h, 500, "LEE|%s|%s", s->id, rel);
    if (local()) { char p[400]; if (resolve(s, rel, p, 400)) return -1; if (open) { strncpy0(dst, p, cap); return 0; } long n; u8 *d = z_readfile(p, &n); if (!d) { strcpy(msg, tr_("No se pudo leer")); return -1; } char dd[120]; z_docdir(dd); strcat(dd, "/Red"); z_mkdir(dd); snprintf(dst, cap, "%s/%s", dd, e->name); int w = z_writefile(dst, d, n); free(d); z_sync(); return w ? -1 : 0; }
    conn_t c; if (cconnect(&c, s->host, err)) { snprintf(msg, 300, "%s", err); return -1; } u8 *r; csends(&c, h); long n = crecv(&c, &r, 20000); if (n < 0 || strncmp((char *)r, "OK|", 3)) { snprintf(msg, 300, "%s", n < 0 ? tr_("sin respuesta") : (char *)r + 4); if (n >= 0) free(r); z_close(c.s); return -1; }
    long sz = atol((char *)r + 3); free(r); sb_t d = {0}; sb_add(&d, "", 0); while (d.n < sz) { u8 *q; long m = crecv(&c, &q, 20000); if (m < 0) break; sb_add(&d, q, m); free(q); snprintf(msg, 300, "%s %ld / %ld KB", tr_("Recibiendo"), d.n / 1024, sz / 1024); } z_close(c.s);
    if (d.n != sz) { free(d.p); strcpy(msg, tr_("se cortó la conexión")); return -1; }
    char dd[120]; if (open) { z_mkdir("/rd/tmp"); z_mkdir("/rd/tmp/red"); strcpy(dd, "/rd/tmp/red"); } else { z_docdir(dd); strcat(dd, "/Red"); z_mkdir(dd); } snprintf(dst, cap, "%s/%s", dd, e->name);
    int w = z_writefile(dst, d.p, d.n); free(d.p); z_sync(); if (w) { snprintf(msg, 300, "%s: %s", tr_("No se pudo guardar"), z_errstr(w)); return -1; } char lg[400]; snprintf(lg, 400, "[prueba] red descargado %s: %ld", dst, sz); z_log(lg); return 0; }
static void upload(void) { share_t *s = &R.s[sel_sh]; char p[300]; z_docdir(p); strcat(p, "/"); if (!ui_prompt("Subir un archivo", "Archivo de este equipo:", p, 290)) return; long n; u8 *d = z_readfile(p, &n); if (!d) { snprintf(msg, 300, "%s %s", tr_("No se puede leer"), p); return; }
    const char *b = strrchr(p, '/'); b = b ? b + 1 : p; char h[500], err[120]; snprintf(h, 500, "ESCRIBE|%s|%s%s%s|%ld", s->id, cwd, cwd[0] ? "/" : "", b, n);
    if (local()) { char rel[400], q[400]; snprintf(rel, 400, "%s%s%s", cwd, cwd[0] ? "/" : "", b); int w = resolve(s, rel, q, 400) ? -1 : z_writefile(q, d, n); free(d); z_sync(); list_dir(); if (w) strcpy(msg, tr_("No se pudo subir")); else snprintf(msg, 300, "%s: %s", tr_("Subido"), b); return; } conn_t c; if (cconnect(&c, s->host, err)) { free(d); snprintf(msg, 300, "%s", err); return; }
    csends(&c, h); for (long o = 0; o < n; o += 65536) csend(&c, d + o, MIN(65536L, n - o)); if (!n) csend(&c, "", 0); free(d); u8 *r; long m = crecv(&c, &r, 30000); z_close(c.s);
    if (m < 0 || strncmp((char *)r, "OK", 2)) { snprintf(msg, 300, "%s: %s", tr_("No se pudo subir"), m < 0 ? tr_("sin respuesta") : (char *)r + 4); if (m >= 0) free(r); return; } free(r); char lg[400]; snprintf(lg, 400, "[prueba] red subido %s (%ld)", b, n); z_log(lg); list_dir(); snprintf(msg, 300, "%s: %s", tr_("Subido"), b); }
static void simple(const char *op, const char *rel, const char *extra) { share_t *s = &R.s[sel_sh]; char h[600], err[120]; u8 *r;
    if (local()) { char p[400], q[400]; if (resolve(s, rel, p, 400)) return; if (op[0] == 'B') z_unlink(p); else if (op[0] == 'C') z_mkdir(p); else { char r2[400]; const char *sl = strrchr(rel, '/'); snprintf(r2, 400, "%.*s%s%s", sl ? (int)(sl - rel) : 0, rel, sl ? "/" : "", extra); if (!resolve(s, r2, q, 400)) z_rename(p, q); } z_sync(); list_dir(); return; } snprintf(h, 600, "%s|%s|%s%s%s", op, s->id, rel, extra ? "|" : "", extra ? extra : ""); if (request(s->host, h, 0, 0, &r, err) < 0) { snprintf(msg, 300, "%s", err); return; } free(r); list_dir(); }
static void send_msg(void) { if (!compose[0]) return; const char *it[MAXM + 1]; int map[MAXM], n = 0; it[n] = "Todos"; map[n++] = -1; for (int i = 0; i < R.nm; i++) if (strcmp(R.m[i].id, myid)) { map[n] = i; it[n++] = R.m[i].name; }
    int sx, sy; zw_screen(W, W->w - 200, W->h - 110, &sx, &sy); int r = ui_menu(sx, sy, it, n); if (r < 0) return; int ok = 0, tot = 0; char t[420]; strcpy(t, compose); safe(t); char h[440]; snprintf(h, 440, "MENSAJE|%s", t);
    for (int k = 0; k < n; k++) { if (k == 0 || (r && k != r)) continue; tot++; u8 *q; char err[120]; if (request(R.m[map[k]].id, h, 0, 0, &q, err) >= 0) { ok++; free(q); } }
    snprintf(msg, 300, tr_("Mensaje entregado a %d de %d"), ok, tot); if (ok) { compose[0] = 0; ccur = 0; } char lg[80]; snprintf(lg, 80, "[prueba] red mensaje enviado %d/%d", ok, tot); z_log(lg); }

/* zonas pulsables */
typedef struct { int x, y, w, h, id; } hz_t; static hz_t HZ[300]; static int nhz;
static void hz(int x, int y, int w, int h, int id) { if (nhz < 300) HZ[nhz++] = (hz_t){x, y, w, h, id}; }
static int bw(const char *s) { return g_textw(tr(s)) + 22; }
static void btn(int x, int y, const char *s, int st, int id) { ui_button(W, x, y, bw(s), 28, s, st); hz(x, y, bw(s), 28, id); }
enum { B_TAB = 1, B_FIELD = 50, B_CREATE = 10, B_RENAME, B_LEAVE, B_DELNET, B_ADDSH, B_UP, B_UPLOAD, B_MKDIR, B_REFRESH, B_SEND, B_CLOSEB, B_INV = 100, B_DEL = 300, B_SH = 500, B_SHDEL = 700, B_ENT = 900 };
static void draw(void) { nhz = 0; g_clear(W, UI_WHITE); g_rect(W, 0, 0, W->w, 40, UI_PANEL); static const char *T[] = {"Equipos", "Carpetas", "Mensajes"}; int x = 10;
    for (int i = 0; i < 3; i++) { ui_button(W, x, 6, bw(T[i]), 28, T[i], tab == i ? 2 : 0); hz(x, 6, bw(T[i]), 28, B_TAB + i); x += bw(T[i]) + 6; }
    char b[300]; int y = 56, X = 20, w = W->w - 40; zs_area(W, 0, 40, W->w, W->h - 62, 0);
    if (tab == 0) { g_text2(W, X, y, in_net() ? R.netname : tr_("Sin red local"), UI_TEXT, 2); y += 40;
        snprintf(b, 300, "%s: %s   (%s %s)", tr_("Este equipo"), myname, tr_("id"), myid); g_text(W, X, y, b, UI_MUTED); if (!in_net()) btn(X + w - bw("Cambiar nombre"), y - 6, "Cambiar nombre", 0, B_RENAME); y += 26;
        znet ni; char ip[20] = "-"; if (!z_netinfo(&ni) && ni.ip) z_fmt_ip(ni.ip, ip); snprintf(b, 300, "%s: %s%s", tr_("Dirección"), ip, !z_netinfo(&ni) && ni.local ? tr_(" (cable directo, sin router)") : ""); g_text(W, X, y, b, UI_MUTED); y += 30;
        if (!in_net()) { g_text(W, X, y, tr_("Crea una red para ser su administrador, o espera a que el administrador de una red invite a este equipo."), UI_TEXT); y += 30; btn(X, y, "Crear una red nueva", 2, B_CREATE); y += 46; }
        else { snprintf(b, 300, "%s: %s%s", tr_("Administrador"), mname(R.admin), is_admin() ? tr_(" (este equipo)") : online(R.admin) ? tr_(" (en línea)") : tr_(" (no está en línea)")); g_text(W, X, y, b, UI_TEXT); y += 30;
            g_textb(W, X, y, tr_("Equipos de la red"), UI_TEXT); y += 24; for (int i = 0; i < R.nm; i++) { int on = online(R.m[i].id); g_circle(W, X + 6, y + 9, 5, on ? 0x16A34A : 0x9CA3AF, 1); peer_t *p = peer(R.m[i].id); char ip2[20] = ""; if (p) z_fmt_ip(p->ip, ip2);
                snprintf(b, 300, "%s%s   %s  %s", R.m[i].name, !strcmp(R.m[i].id, myid) ? tr_(" (este equipo)") : "", on ? tr_("en línea") : tr_("apagado o sin cable"), ip2); g_text(W, X + 18, y + 2, b, UI_TEXT);
                if (is_admin() && i > 0) btn(X + w - bw("Sacar de la red"), y - 4, "Sacar de la red", 0, B_DEL + i); y += 30; }
            y += 6; if (is_admin()) btn(X, y, "Borrar la red", 0, B_DELNET); else btn(X, y, "Salir de la red", 0, B_LEAVE); y += 44; }
        int nf = 0; for (int i = 0; i < np; i++) if (z_ms() - P[i].seen < 45000 && (!in_net() || strcmp(P[i].net, R.net))) nf++;
        g_textb(W, X, y, is_admin() ? tr_("Equipos encontrados que no están en la red") : tr_("Otros equipos encontrados"), UI_TEXT); y += 24; if (!nf) { g_text(W, X, y, tr_("Ninguno todavía. Comprueba el cable y que el otro equipo tenga ZunzúnOS encendido."), UI_MUTED); y += 24; }
        for (int i = 0; i < np; i++) { peer_t *p = &P[i]; if (z_ms() - p->seen >= 45000 || (in_net() && !strcmp(p->net, R.net))) continue; char ip2[20]; z_fmt_ip(p->ip, ip2);
            if (p->role == 'L') snprintf(b, 300, "%s   %s   %s", p->name, ip2, tr_("sin red")); else snprintf(b, 300, "%s   %s   %s \"%s\"", p->name, ip2, tr_("en la red"), p->netname); g_text(W, X + 18, y + 2, b, UI_TEXT);
            if (is_admin() && p->role == 'L') btn(X + w - bw("Invitar a la red"), y - 4, "Invitar a la red", 2, B_INV + i); y += 30; } }
    else if (tab == 1) { if (!in_net()) g_text(W, X, y, tr_("Primero une este equipo a una red local (pestaña Equipos)."), UI_MUTED);
        else if (sel_sh < 0) { g_textb(W, X, y, tr_("Carpetas compartidas de la red"), UI_TEXT); if (is_admin()) btn(X + w - bw("Compartir una carpeta"), y - 6, "Compartir una carpeta", 2, B_ADDSH); y += 30;
            if (!R.ns) { g_text(W, X, y, is_admin() ? tr_("Todavía no hay carpetas. Pulsa Compartir una carpeta.") : tr_("El administrador todavía no ha compartido carpetas."), UI_MUTED); }
            for (int i = 0; i < R.ns; i++) { share_t *s = &R.s[i]; int on = online(s->host); g_rect(W, X, y, w, 40, i % 2 ? UI_WHITE : UI_BG); g_circle(W, X + 12, y + 20, 5, on ? 0x16A34A : 0x9CA3AF, 1); g_textb(W, X + 26, y + 4, s->name, UI_TEXT);
                snprintf(b, 300, "%s %s · %s · %s", tr_("en"), mname(s->host), tr_(s->rw ? "lectura y escritura" : "solo lectura"), s->path); g_text(W, X + 26, y + 22, b, UI_MUTED); hz(X, y, w - 140, 40, B_SH + i); if (is_admin()) btn(X + w - bw("Dejar de compartir"), y + 6, "Dejar de compartir", 0, B_SHDEL + i); y += 44; } }
        else { share_t *s = &R.s[sel_sh]; snprintf(b, 300, "%s / %s", s->name, cwd); g_textb(W, X, y, b, UI_TEXT); y += 26; int bx = X; btn(bx, y, "Cerrar", 0, B_CLOSEB); bx += bw("Cerrar") + 6; btn(bx, y, "Subir de nivel", 0, B_UP); bx += bw("Subir de nivel") + 6; btn(bx, y, "Actualizar", 0, B_REFRESH); bx += bw("Actualizar") + 6;
            if (s->rw) { btn(bx, y, "Subir un archivo...", 0, B_UPLOAD); bx += bw("Subir un archivo...") + 6; btn(bx, y, "Nueva carpeta", 0, B_MKDIR); } y += 38; int rows = (W->h - 22 - y) / 24; etop = MAX(0, MIN(etop, nent - rows));
            for (int i = 0; i < rows && etop + i < nent; i++) { ent_t *e = &E[etop + i]; char t[260]; if (e->d == 'D') snprintf(t, 260, "[%s]  %s", tr_("carpeta"), e->name); else snprintf(t, 260, "%s   (%lu KB)", e->name, (unsigned long)((e->size + 1023) / 1024)); g_text(W, X + 6, y + 4, t, e->d == 'D' ? UI_ACC : UI_TEXT); hz(X, y, w, 24, B_ENT + etop + i); y += 24; }
            if (!nent) g_text(W, X, y, tr_("Carpeta vacía."), UI_MUTED); } }
    else { if (!in_net()) g_text(W, X, y, tr_("Primero une este equipo a una red local (pestaña Equipos)."), UI_MUTED);
        else { g_textb(W, X, y, tr_("Escribe un mensaje para los equipos de la red"), UI_TEXT); y += 24; ui_input(W, X, y, w - bw("Enviar a...") - 10, 28, compose, ccur, cfoc); hz(X, y, w - bw("Enviar a...") - 10, 28, B_FIELD); btn(X + w - bw("Enviar a..."), y, "Enviar a...", 2, B_SEND); y += 44;
            g_textb(W, X, y, tr_("Mensajes recibidos"), UI_TEXT); y += 24; char *t = z_readfile(INBOX, 0); int k = 0; for (char *p = t; p && *p && y < W->h - 40; k++) { char *e = strchr(p, '\n'); if (e) *e = 0; char *f[3]; char l[700]; strncpy0(l, p, 700); if (split(l, f, 3) == 3) { snprintf(b, 300, "%s  %s:", f[0], f[1]); g_textb(W, X, y, b, UI_TEXT); y += 18; g_text(W, X + 16, y, f[2], UI_TEXT); y += 26; } p = e ? e + 1 : 0; }
            if (!k) g_text(W, X, y, tr_("Ninguno."), UI_MUTED); free(t); } }
    zs_stop(W); zs_show(W); g_rect(W, 0, W->h - 22, W->w, 22, UI_PANEL); g_text(W, 8, W->h - 19, msg, UI_TEXT); zw_present(W); }
static void click(zevent *e) { for (int i = nhz - 1; i >= 0; i--) { hz_t *h = &HZ[i]; if (e->x < h->x || e->y < h->y || e->x >= h->x + h->w || e->y >= h->y + h->h) continue; int id = h->id; msg[0] = 0;
        if (id >= B_TAB && id < B_TAB + 3) { tab = id - B_TAB; cfoc = tab == 2; }
        else if (id == B_CREATE) create_net(); else if (id == B_RENAME) { char n[32]; strcpy(n, myname); if (ui_prompt("Nombre del equipo", "Nombre de este equipo en la red:", n, 30) && n[0]) { safe(n); strcpy(myname, n); ident_save(); } }
        else if (id == B_LEAVE) { if (ui_confirm("Salir de la red", "¿Sacar este equipo de la red local? Necesitará otra invitación para volver.")) { z_unlink(CFG); memset(&R, 0, sizeof R); z_sync(); strcpy(msg, tr_("Este equipo ya no está en la red")); } }
        else if (id == B_DELNET) { if (ui_confirm("Borrar la red", "¿Borrar esta red? Los terminales dejarán de poder usarla.")) { char ids[MAXM][12]; int n = R.nm; for (int k = 0; k < n; k++) strcpy(ids[k], R.m[k].id); R.nm = 1; strcpy(R.m[0].id, myid); strcpy(R.m[0].name, myname); R.ns = 0; push_to(ids, n); z_unlink(CFG); memset(&R, 0, sizeof R); z_sync(); strcpy(msg, tr_("Red borrada")); } }
        else if (id >= B_ENT) { int k = id - B_ENT; if (k >= nent) break; ent_t *en = &E[k]; if (en->d == 'D') { if (strlen(cwd) + strlen(en->name) < 290) { if (cwd[0]) strcat(cwd, "/"); strcat(cwd, en->name); } list_dir(); break; }
            static const char *it[] = {"Abrir", "Guardar en Documentos", "Cambiar nombre", "Borrar"}; int sx, sy; zw_screen(W, e->x, e->y, &sx, &sy); int r = ui_menu(sx, sy, it, R.s[sel_sh].rw ? 4 : 2); char dst[400], rel[400]; snprintf(rel, 400, "%s%s%s", cwd, cwd[0] ? "/" : "", en->name);
            if (r == 0 && !fetch(en, dst, 400, 1)) { z_open_path(dst); snprintf(msg, 300, "%s %s", tr_("Abierto"), en->name); } else if (r == 1 && !fetch(en, dst, 400, 0)) snprintf(msg, 300, "%s %s", tr_("Guardado en"), dst);
            else if (r == 2) { char n[200]; strcpy(n, en->name); if (ui_prompt("Cambiar nombre", "Nombre nuevo:", n, 190) && n[0]) simple("RENOMBRA", rel, n); } else if (r == 3) { char q[300]; snprintf(q, 300, tr_("¿Borrar %s del otro equipo?"), en->name); if (ui_confirm("Borrar", q)) simple("BORRA", rel, 0); } }
        else if (id >= B_SHDEL) { int k = id - B_SHDEL; char q[200]; snprintf(q, 200, tr_("¿Dejar de compartir %s? Los archivos no se borran."), R.s[k].name); if (ui_confirm("Dejar de compartir", q)) { memmove(&R.s[k], &R.s[k + 1], (R.ns - k - 1) * sizeof R.s[0]); R.ns--; push_config(); } }
        else if (id >= B_SH) { sel_sh = id - B_SH; cwd[0] = 0; list_dir(); }
        else if (id >= B_DEL) remove_member(id - B_DEL); else if (id >= B_INV) { int k = id - B_INV; if (k < np) invite(&P[k]); }
        else if (id == B_ADDSH) add_share(); else if (id == B_CLOSEB) { sel_sh = -1; nent = 0; } else if (id == B_REFRESH) list_dir();
        else if (id == B_UP) { char *s = strrchr(cwd, '/'); if (s) *s = 0; else cwd[0] = 0; list_dir(); } else if (id == B_UPLOAD) upload();
        else if (id == B_MKDIR) { char n[120] = ""; if (ui_prompt("Nueva carpeta", "Nombre:", n, 110) && n[0] && !strchr(n, '/')) { char rel[400]; snprintf(rel, 400, "%s%s%s", cwd, cwd[0] ? "/" : "", n); simple("CARPETA", rel, 0); } }
        else if (id == B_SEND) send_msg(); else if (id == B_FIELD) { cfoc = 1; ccur = strlen(compose); }
        break; } }
int main(const char *a) {
    if (!strcmp(a, "--servicio")) return service();
    if ((z_sess(0, 0, 0) & 7) == 2) { ui_confirm("Red local", "La red local solo se puede usar en la sesión del administrador."); return 0; }
    ident(); cfg_load(); if (!service_running()) z_spawn("/rd/bin/redlocal", "--servicio");
    zsys s; z_sysinfo(&s); W = zw_new("Red local", -1, 0, MIN(920, (int)s.scr_w - 40), MIN(640, (int)s.scr_h - 80), WF_DECOR | WF_RESIZE); if (!W) return 1;
    busca(); draw(); z_log("[prueba] red local abierta"); zevent e;
    for (;;) { int g = z_event(&e, 500); poll_udp(); u64 now = z_ms(); if (now - last_busca > 5000) { busca(); cfg_load(); } if (!g) { draw(); continue; } if (e.type == EV_REDRAW) { draw(); continue; } if (e.win != (u32)W->id) continue;
        if (zs_event(W, &e)) { draw(); continue; }
        if (e.type == EV_CLOSE) return 0; if (e.type == EV_RESIZE) { zw_resized(W, e.x, e.y); draw(); continue; }
        if (e.type == EV_WHEEL) { etop -= (int)e.key * 3; draw(); continue; }
        if (e.type == EV_KEY) { if (tab == 2 && cfoc) { if (e.ch == '\n') send_msg(); else ui_input_key(compose, 380, &ccur, &e); } else if (e.key == K_F5) { busca(); if (sel_sh >= 0) list_dir(); } draw(); continue; }
        if (e.type == EV_DOWN) { if (e.btn & 2 && tab == 2 && cfoc) { ui_input_menu(W, compose, 380, &ccur, &e); draw(); continue; } click(&e); draw(); } }
}
