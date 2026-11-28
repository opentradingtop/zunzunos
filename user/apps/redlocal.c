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
   reto y respuesta (HMAC-SHA256) y va cifrada con AES-GCM con una clave de sesion distinta (HKDF).
   Por Internet (docs/RED.md): el servicio abre el puerto en el router (PCP, NAT-PMP o UPnP), anuncia sus direcciones a
   los demas equipos (con numero de secuencia, para las IP que cambian) y, si hay un rele en la red, espera alli las
   llamadas: el rele solo pasa bytes cifrados de extremo a extremo. Las invitaciones por Internet usan un codigo con
   un secreto de 80 bits y las direcciones del administrador; el codigo de 6 cifras se sigue comprobando en las dos
   pantallas. Los mensajes llevan adjuntos (por partes, con reanudacion) y se guardan para reintentar si el otro
   equipo no esta. */
#include "z.h"
#include "../lib/zcrypto.h"
#define PUDP 7140
#define PTCP 7141
#define PRELE 7142   /* puerto por omision del rele (tools/zred-rele.py) */
#define MAXADJ 10
#define MAXAF (64LL << 20)   /* tamano maximo de cada adjunto */
#define E_TIMEOUT_ -110
/* identidad y configuracion por equipo (la MAC de la tarjeta integrada en el nombre): una misma memoria USB usada en
   varios equipos guarda la de cada uno y ninguno se hace pasar por otro */
static char CFG[64], IDF[64];
#define OLD_CFG "/sys/ZUNZUN/RED.TXT"
#define OLD_IDF "/sys/ZUNZUN/RED-ID.TXT"
#define INBOX "/sys/ZUNZUN/RED-MENSAJES.TXT"
#define MAXM 64
#define MAXS 64
#define MAXF (256LL << 20)

typedef struct { char id[12], name[32]; } mem_t;
typedef struct { char id[12], name[40], host[12], path[160]; int rw; } share_t;
#define MAXP 16
typedef struct { char id[12], name[64], host[12], pname[64], lang[8], model[64]; int paper, dpi, color; } rprn_t;
static struct { char net[12], netname[40], admin[12], rele[100]; u8 key[32]; int ver, nm, ns, np; mem_t m[MAXM]; share_t s[MAXS]; rprn_t p[MAXP]; } R;
/* ajustes de Internet de este equipo (en RED-ID): acceso activado, direcciones escritas a mano y servidor STUN opcional */
static int inet_on; static char manual[200], stunsrv[80];
static char myid[12], myname[32];
static const char *tr_(const char *s) { return tr(s); }
static long atol(const char *s) { while (*s == ' ') s++; long v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + *s++ - '0'; return v; }

/* ---------- utilidades ---------- */
static void hex(const u8 *d, int n, char *o) { for (int i = 0; i < n; i++) { o[2 * i] = "0123456789abcdef"[d[i] >> 4]; o[2 * i + 1] = "0123456789abcdef"[d[i] & 15]; } o[2 * n] = 0; }
static int unhex(const char *s, u8 *o, int n) { for (int i = 0; i < n; i++) { int a = s[2 * i], b = a ? s[2 * i + 1] : 0; int x = a >= 'a' ? a - 87 : a - '0', y = b >= 'a' ? b - 87 : b - '0'; if (x < 0 || x > 15 || y < 0 || y > 15) return -1; o[i] = x << 4 | y; } return 0; }
static void strncat0(char *d, const char *s, int cap) { int l = strlen(d); if (l < cap - 1) strncpy0(d + l, s, cap - l); }
static void safe(char *s) { for (; *s; s++) if (*s == '|' || *s == '\n' || *s == '\r') *s = ' '; }
static int split(char *s, char **f, int max) { int n = 0; f[n++] = s; for (char *p = s; *p && n < max; p++) if (*p == '|') { *p = 0; f[n++] = p + 1; } return n; }
static void kvs(const char *t, const char *k, char *o, int cap) { int l = strlen(k); o[0] = 0; for (const char *p = t; p && *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : 0) if (!strncmp(p, k, l) && p[l] == '=') { int i = 0; p += l + 1; while (p[i] && p[i] != '\n' && i < cap - 1) { o[i] = p[i]; i++; } o[i] = 0; return; } }

/* identidad del equipo: id de 8 cifras ligado a la MAC de la tarjeta de red. Si los datos se copian a otro
   equipo (otra MAC), ese equipo se hace un id nuevo; asi dos equipos nunca comparten id */
static char mymac[16], ABF[64], INVF[64];
static void id_extra(const char *t) { char v[8]; kvs(t, "internet", v, 8); inet_on = v[0] == '1'; kvs(t, "publica", manual, 200); kvs(t, "stun", stunsrv, 80); }
static void ident_save(void) { char b[500]; snprintf(b, 500, "id=%s\nnombre=%s\nmac=%s\ninternet=%d\npublica=%s\nstun=%s\n", myid, myname, mymac, inet_on, manual, stunsrv); z_writefile(IDF, b, strlen(b)); z_sync(); }
static void ident(void) { zsys sy; z_sysinfo(&sy); u8 *hm = sy.hwmac; static const u8 z6[6]; if (!memcmp(hm, z6, 6)) { znet ni; memset(&ni, 0, sizeof ni); z_netinfo(&ni); memcpy(sy.hwmac, ni.mac, 6); }
    hex(sy.hwmac, 6, mymac); snprintf(CFG, 64, "/sys/ZUNZUN/RED-%s.TXT", mymac); snprintf(IDF, 64, "/sys/ZUNZUN/RED-ID-%s.TXT", mymac); snprintf(ABF, 64, "/sys/ZUNZUN/RED-DIR-%s.TXT", mymac); snprintf(INVF, 64, "/sys/ZUNZUN/RED-INVITA-%s.TXT", mymac);
    char *t = z_readfile(IDF, 0);
    if (!t) { /* archivos de la version anterior: son de este equipo si tienen su MAC o no tienen ninguna */
        char *o = z_readfile(OLD_IDF, 0); if (o) { char om[16]; kvs(o, "mac", om, 16); if (!om[0] || !strcmp(om, mymac)) { kvs(o, "id", myid, 12); kvs(o, "nombre", myname, 32); z_rename(OLD_CFG, CFG); z_unlink(OLD_IDF); z_log("[prueba] red identidad anterior adoptada"); } free(o); } }
    else { kvs(t, "id", myid, 12); kvs(t, "nombre", myname, 32); id_extra(t); free(t); }
    if (strlen(myid) != 8) { u8 r[40], h[32]; zc_random(r, 32); memcpy(r + 32, sy.hwmac, 6); zc_hash(256, r, 38, h); hex(h, 4, myid); }
    if (!myname[0]) snprintf(myname, 32, "Equipo-%c%c%c%c", toupper(myid[4]), toupper(myid[5]), toupper(myid[6]), toupper(myid[7]));
    z_mkdir("/sys/ZUNZUN"); ident_save(); }
/* el servicio vuelve a leer los ajustes de Internet (la ventana los cambia) */
static void id_reload(void) { char *t = z_readfile(IDF, 0); if (t) { id_extra(t); free(t); } }

static int cfg_text(sb_t *b) { char k[65]; hex(R.key, 32, k); sb_f(b, "ZRED1\nred=%s\nnombre=%s\nclave=%s\nadmin=%s\nversion=%d\n", R.net, R.netname, k, R.admin, R.ver); if (R.rele[0]) sb_f(b, "rele=%s\n", R.rele);
    for (int i = 0; i < R.nm; i++) sb_f(b, "equipo=%s|%s\n", R.m[i].id, R.m[i].name); for (int i = 0; i < R.ns; i++) sb_f(b, "carpeta=%s|%s|%s|%s|%d\n", R.s[i].id, R.s[i].name, R.s[i].host, R.s[i].path, R.s[i].rw);
    for (int i = 0; i < R.np; i++) sb_f(b, "impresora=%s|%s|%s|%s|%s|%d|%d|%d|%s\n", R.p[i].id, R.p[i].name, R.p[i].host, R.p[i].pname, R.p[i].lang, R.p[i].paper, R.p[i].dpi, R.p[i].color, R.p[i].model); return b->n; }
static int cfg_parse(char *t) { memset(&R, 0, sizeof R); if (!t || strncmp(t, "ZRED1\n", 6)) return -1; char v[80]; kvs(t, "red", R.net, 12); kvs(t, "nombre", R.netname, 40); kvs(t, "admin", R.admin, 12); kvs(t, "version", v, 16); R.ver = atoi(v); kvs(t, "rele", R.rele, 100); safe(R.rele);
    kvs(t, "clave", v, 80); if (unhex(v, R.key, 32)) { memset(&R, 0, sizeof R); return -1; }
    for (char *p = t; p && *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : 0) { char l[400]; int i = 0; while (p[i] && p[i] != '\n' && i < 399) { l[i] = p[i]; i++; } l[i] = 0; char *f[6];
        if (!strncmp(l, "equipo=", 7) && R.nm < MAXM) { int n = split(l + 7, f, 2); if (n == 2) { strncpy0(R.m[R.nm].id, f[0], 12); strncpy0(R.m[R.nm].name, f[1], 32); R.nm++; } }
        else if (!strncmp(l, "impresora=", 10) && R.np < MAXP) { char *g[9]; int n = split(l + 10, g, 9); if (n >= 8) { rprn_t *q = &R.p[R.np++]; strncpy0(q->id, g[0], 12); strncpy0(q->name, g[1], 64); strncpy0(q->host, g[2], 12); strncpy0(q->pname, g[3], 64); strncpy0(q->lang, g[4], 8); q->paper = atoi(g[5]); q->dpi = atoi(g[6]); q->color = atoi(g[7]); if (n > 8) strncpy0(q->model, g[8], 64); } }
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
/* LAN: ip y seen (anuncios UDP). Internet: iseen (ultima conexion autenticada) y via, la ruta que funciono
   ("d:host:puerto" directa o "r" por el rele); ichk: ultima comprobacion del servicio */
typedef struct { char id[12], name[32], net[12], netname[40]; char role; u32 ip; u64 seen; char via[80]; u64 iseen, ichk; } peer_t;
static peer_t P[128]; static int np, service_mode;
/* el servicio apunta los equipos que oye (sus anuncios llegan al servicio, no a la ventana) y la ventana los lee */
#define PEERS "/rd/tmp/red-equipos.txt"
static void peers_save(void) { sb_t b = {0}; sb_s(&b, ""); for (int i = 0; i < np; i++) sb_f(&b, "%s|%s|%s|%s|%c|%u|%lu|%s|%lu\n", P[i].id, P[i].name, P[i].net, P[i].netname, P[i].role ? P[i].role : 'L', P[i].ip, (unsigned long)P[i].seen, P[i].via[0] ? P[i].via : "-", (unsigned long)P[i].iseen); z_mkdir("/rd/tmp"); z_writefile(PEERS, b.p, b.n); free(b.p); }
static peer_t *peer(const char *id);
static peer_t *peer_mk(const char *id) { peer_t *q = peer(id); if (!q && np < 128) { q = &P[np++]; memset(q, 0, sizeof *q); strncpy0(q->id, id, 12); } return q; }
static void peers_load(void) { char *t = z_readfile(PEERS, 0); if (!t) return; for (char *p = t; p && *p; ) { char *e = strchr(p, '\n'); if (e) *e = 0; char *f[10]; int k = split(p, f, 10);
        if (k >= 7) { u64 seen = (u64)atol(f[6]); u32 ip = (u32)atol(f[5]); peer_t *q = peer_mk(f[0]);
            if (q && ip && seen >= q->seen) { strncpy0(q->name, f[1], 32); strncpy0(q->net, f[2], 12); strncpy0(q->netname, f[3], 40); q->role = f[4][0]; q->ip = ip; q->seen = seen; }
            if (q && k >= 9) { u64 is = (u64)atol(f[8]); if (is > q->iseen) { q->iseen = is; strncpy0(q->via, strcmp(f[7], "-") ? f[7] : "", 80); } } }
        p = e ? e + 1 : 0; } free(t); }
static peer_t *peer(const char *id) { for (int i = 0; i < np; i++) if (!strcmp(P[i].id, id)) return &P[i]; return 0; }
static int lan_on(const char *id) { peer_t *p = peer(id); return p && p->ip && z_ms() - p->seen < 45000; }
static int inet_seen(const char *id) { peer_t *p = peer(id); return p && p->iseen && z_ms() - p->iseen < 150000; }
static int online(const char *id) { if (!strcmp(id, myid)) return 1; return lan_on(id) || inet_seen(id); }

/* ---------- direcciones de Internet de cada equipo ----------
   Cada equipo anuncia las suyas con un numero de secuencia que sube cuando cambian; gana la secuencia mayor, venga de
   quien venga (asi un equipo que cambio de IP se reencuentra aunque solo haya podido avisar a otro). RED-DIR: id|seq|dirs */
#define MAXA 200
typedef struct { char id[12]; u32 seq; char a[MAXA]; } addr_t; static addr_t AB[MAXM + 1]; static int nab;
static void addr_clean(char *s) { char *o = s; for (char *p = s; *p; p++) if (isalpha(*p) || isdigit(*p) || strchr(".:,-_/*", *p)) *o++ = *p; *o = 0; }
static void ab_load(void) { nab = 0; char *t = z_readfile(ABF, 0); for (char *p = t; p && *p; ) { char *e = strchr(p, '\n'); if (e) *e = 0; char *f[3]; if (split(p, f, 3) == 3 && nab <= MAXM) { strncpy0(AB[nab].id, f[0], 12); AB[nab].seq = (u32)atol(f[1]); strncpy0(AB[nab].a, f[2], MAXA); nab++; } p = e ? e + 1 : 0; } free(t); }
static void ab_save(void) { sb_t b = {0}; sb_s(&b, ""); for (int i = 0; i < nab; i++) sb_f(&b, "%s|%u|%s\n", AB[i].id, AB[i].seq, AB[i].a); z_writefile(ABF, b.p, b.n); free(b.p); z_sync(); }
static addr_t *ab_get(const char *id) { for (int i = 0; i < nab; i++) if (!strcmp(AB[i].id, id)) return &AB[i]; return 0; }
/* 1 si cambia algo. Solo equipos de la red; las direcciones de este equipo solo las pone el propio servicio */
static int ab_merge(const char *id, u32 seq, const char *a, int self) { if ((!member(id) && strcmp(id, R.admin)) || (!self && !strcmp(id, myid))) return 0; addr_t *x = ab_get(id);
    if (!x) { if (nab > MAXM) return 0; x = &AB[nab++]; memset(x, 0, sizeof *x); strncpy0(x->id, id, 12); } else if (seq <= x->seq) return 0;
    x->seq = seq; strncpy0(x->a, a, MAXA); addr_clean(x->a); return 1; }

/* ---------- direcciones IP ---------- */
#define IP4(a, b, c, d) ((u32)(a) | (u32)(b) << 8 | (u32)(c) << 16 | (u32)(d) << 24)
static int parse_ip(const char *s, u32 *ip) { u32 v = 0; for (int i = 0; i < 4; i++) { if (!isdigit(*s)) return -1; int o = 0; while (isdigit(*s)) o = o * 10 + *s++ - '0'; if (o > 255) return -1; v |= (u32)o << (8 * i); if (i < 3 && *s++ != '.') return -1; } if (*s) return -1; *ip = v; return 0; }
static int ip_private(u32 ip) { u8 a = ip, b = ip >> 8; return a == 10 || a == 127 || (a == 172 && b >= 16 && b < 32) || (a == 192 && b == 168) || (a == 169 && b == 254) || !ip; }
static int ip_cgnat(u32 ip) { u8 a = ip, b = ip >> 8; return a == 100 && b >= 64 && b < 128; }
/* una conexion entrante es de la red local si viene de la misma subred (sin contar el router: lo que llega por un puerto
   abierto en el router puede venir de el), de una direccion 169.254 de cable directo o si este equipo no tiene router */
static int from_lan(u32 ip) { znet ni; if (z_netinfo(&ni) || !ni.ip) return 0; if ((ip & 0xFFFF) == (169 | 254 << 8) || ni.local || !ni.gw) return 1;   /* sin router no hay camino a Internet: llega por el cable */ if (ni.gw && ip == ni.gw) return 0; return ni.mask && (ip & ni.mask) == (ni.ip & ni.mask); }
/* "host:puerto" (o "host", con el puerto por omision); el host puede ser una IP o un nombre DNS */
static int hostport(const char *s, char *h, int hc, int *port, int defp) { int i = 0; while (*s == ' ') s++; while (*s && *s != ':' && *s != '/' && *s != ',' && i < hc - 1) h[i++] = *s++; h[i] = 0; *port = defp; if (*s == ':') *port = atoi(s + 1); return h[0] && *port > 0 && *port < 65536 ? 0 : -1; }
static int resolve_host(const char *h, u32 *ip) { if (!parse_ip(h, ip)) return 0; return z_dns(h, ip) || !*ip ? -1 : 0; }
static int dial(const char *hp, int defp, int to) { char h[100]; int pt; u32 ip; if (hostport(hp, h, 100, &pt, defp) || resolve_host(h, &ip)) return -1; int s = z_socket(1); if (s < 0) return -1; if (z_connect(s, ip, pt, to)) { z_close(s); return -1; } return s; }
static void soy(char *o, int cap) { char nm[32], nn[40]; strcpy(nm, myname); strcpy(nn, R.netname); safe(nm); safe(nn); snprintf(o, cap, "ZRED1|SOY|%s|%s|%s|%s|%c", myid, nm, in_net() ? R.net : "-", in_net() ? nn : "-", is_admin() ? 'A' : in_net() ? 'T' : 'L'); }
static void on_udp(int us, char *m, long n, u32 ip, int port) { m[n] = 0; char *f[8]; int k = split(m, f, 8); if (k < 2 || strcmp(f[0], "ZRED1")) return;
    if (!strcmp(f[1], "BUSCA")) { char o[200]; soy(o, 200); znet ni; if (z_netinfo(&ni) || !ni.ip || !ip) return; z_sendto(us, ip, port, o, strlen(o)); }   /* sin direccion todavia no se contesta */
    else if (!strcmp(f[1], "SOY") && k >= 7 && strcmp(f[2], myid) && ip) { peer_t *p = peer(f[2]); if (!p && np < 128) { p = &P[np++]; char lg[80]; snprintf(lg, 80, "[prueba] red encontrado %s", f[3]); z_log(lg); } if (!p) return; strncpy0(p->id, f[2], 12); strncpy0(p->name, f[3], 32); strncpy0(p->net, f[4], 12); strncpy0(p->netname, f[5], 40); p->role = f[6][0]; p->ip = ip; p->seen = z_ms(); if (service_mode) peers_save(); } }

/* ---------- conexion cifrada ---------- */
/* dl: hora limite (z_ms) para leer, 0 sin limite; inet: la conexion viene de Internet (o del rele) */
typedef struct { int s; zgcm g; u64 tx, rx; int dir; u8 rb[512]; int rn, rp; u64 dl; int inet; } conn_t;
static int raw_send(conn_t *c, const void *d, long n) { const u8 *p = d; while (n > 0) { long r = z_send(c->s, p, n); if (r <= 0) return -1; p += r; n -= r; } return 0; }
static int raw_recv(conn_t *c, void *d, long n, int to) { u8 *p = d; while (n > 0) { if (c->rp < c->rn) { long k = MIN(n, (long)(c->rn - c->rp)); memcpy(p, c->rb + c->rp, k); c->rp += k; p += k; n -= k; continue; }
        if (c->dl) { long left = (long)(c->dl - z_ms()); if (left <= 0) return -1; if (to > left) to = left; }
        long r = z_recv(c->s, c->rb, sizeof c->rb, to); if (r <= 0) return -1; c->rn = r; c->rp = 0; } return 0; }
/* sin esperar: 1 si hay una linea completa, 0 si todavia no, -1 si se cerro (para las conexiones que esperan en el rele) */
static int try_line(conn_t *c, char *o, int cap) {
    for (;;) { for (int i = c->rp; i < c->rn; i++) if (c->rb[i] == '\n') { int n = MIN(i - c->rp, cap - 1); memcpy(o, c->rb + c->rp, n); o[n] = 0; c->rp = i + 1; return 1; }
        if (c->rp) { memmove(c->rb, c->rb + c->rp, c->rn - c->rp); c->rn -= c->rp; c->rp = 0; } if (c->rn >= (int)sizeof c->rb) return -1;
        long r = z_recv(c->s, c->rb + c->rn, sizeof c->rb - c->rn, 0); if (r == 0) return -1; if (r < 0) return r == E_TIMEOUT_ ? 0 : -1; c->rn += r; } }
static int line(conn_t *c, char *o, int cap, int to) { int i = 0; for (;;) { char ch; if (raw_recv(c, &ch, 1, to)) return -1; if (ch == '\n') break; if (i < cap - 1) o[i++] = ch; } o[i] = 0; return i; }
static int lsend(conn_t *c, const char *s) { if (raw_send(c, s, strlen(s))) return -1; return raw_send(c, "\n", 1); }
static void ivf(u8 *iv, int dir, u64 n) { memset(iv, 0, 12); iv[0] = dir; for (int i = 0; i < 8; i++) iv[4 + i] = n >> (8 * i); }
static int csend(conn_t *c, const void *d, long n) { u8 *b = malloc(n + 20); if (!b) return -1; b[0] = n; b[1] = n >> 8; b[2] = n >> 16; b[3] = n >> 24; u8 iv[12]; ivf(iv, c->dir, c->tx++);
    gcm_seal(&c->g, iv, b, 4, d, n, b + 4, b + 4 + n); int r = raw_send(c, b, n + 20); free(b); return r; }
static int csends(conn_t *c, const char *s) { return csend(c, s, strlen(s)); }
static long crecv(conn_t *c, u8 **out, int to) { u8 h[4]; if (raw_recv(c, h, 4, to)) return -1; long n = h[0] | h[1] << 8 | h[2] << 16 | (long)h[3] << 24; if (n < 0 || n > (2LL << 20)) return -1;
    u8 *b = malloc(n + 17); if (raw_recv(c, b, n + 16, to)) { free(b); return -1; } u8 iv[12]; ivf(iv, !c->dir, c->rx++); u8 *pl = malloc(n + 1);
    if (gcm_open(&c->g, iv, h, 4, b, n, pl, b + n)) { free(b); free(pl); return -1; }   /* 0 = la etiqueta cuadra */ free(b); pl[n] = 0; *out = pl; return n; }
static void sess_key(conn_t *c, const u8 *nc, const u8 *ns) { u8 salt[32], prk[32], k[16]; memcpy(salt, nc, 16); memcpy(salt + 16, ns, 16); zc_hkdf_extract(256, salt, 32, R.key, 32, prk); zc_hkdf_expand(256, prk, (const u8 *)"zred1 sesion", 12, k, 16); gcm_init(&c->g, k, 16); }
static void proof(const u8 *nc, const u8 *ns, char who, char *o) { u8 m[33], h[32]; memcpy(m, nc, 16); memcpy(m + 16, ns, 16); m[32] = who; zc_hmac(256, R.key, 32, m, 33, h); hex(h, 32, o); }
static int ct_eq(const u8 *a, const u8 *b, int n) { u8 d = 0; for (int i = 0; i < n; i++) d |= a[i] ^ b[i]; return !d; }
/* HOLA2: el cliente no dice en claro ni la red ni quien es; manda una etiqueta HMAC(clave de red, nc|id) que solo puede
   calcular un equipo de la red y que el servidor compara con la de cada equipo. Asi, por Internet, nadie que mire o que
   pruebe sabe de que red es este equipo. La version anterior (HOLA con la red y el id) solo se acepta en la red local. */
static void hola_tag(const u8 *nc, const char *id, u8 *o) { u8 m[40]; memcpy(m, nc, 16); int l = strlen(id); memcpy(m + 16, id, l); zc_hmac(256, R.key, 32, m, 16 + l, o); }
/* ---------- rele (tools/zred-rele.py): R.rele = "host:puerto[/clave]"; cada equipo espera en un buzon que solo saben
   calcular los equipos de la red (el rele no sabe ni de que red es ni que equipo) ---------- */
static void rele_tok(char *o, int cap) { const char *sl = strchr(R.rele, '/'); strncpy0(o, sl ? sl + 1 : "-", cap); if (!o[0]) strcpy(o, "-"); }
static void buzon(const char *id, char *o) { u8 h[32]; char m[40]; snprintf(m, 40, "zred1 buzon %s", id); zc_hmac(256, R.key, 32, m, strlen(m), h); hex(h, 12, o); }
static int rele_open(conn_t *c, const char *verb, const char *box, int to) { memset(c, 0, sizeof *c); if (!R.rele[0]) return -1; c->s = dial(R.rele, PRELE, to); if (c->s < 0) return -1;
    char tok[64], l[200]; rele_tok(tok, 64); snprintf(l, 200, "ZRELE1|%s|%s|%s", verb, box, tok); if (lsend(c, l)) { z_close(c->s); return -1; } return 0; }
static int rele_call(conn_t *c, const char *box) { if (rele_open(c, "CONECTA", box, 5000)) return -1; char l[100]; c->dl = z_ms() + 6000; if (line(c, l, 100, 6000) < 0 || strcmp(l, "ZRELE1|UNIDO")) { z_close(c->s); return -1; } c->dl = 0; return 0; }
/* el saludo sobre una conexion ya abierta: 0 bien, -1 se corto o no contesto, -2 el otro no reconoce la clave */
/* to: cuanto esperar el reto. La ventana espera mas (el servicio del otro equipo puede estar ocupado unos segundos con otra
   conexion); el servicio, poco (si dos servicios se llaman a la vez, los dos esperan y ninguno contesta) */
static int hs_client(conn_t *c, const char *id, int old, int to) { u8 nc[16], ns[16]; zc_random(nc, 16); char h[40], l[200]; hex(nc, 16, h); c->dl = z_ms() + to + 4000;
    if (old) snprintf(l, 200, "ZRED1|HOLA|%s|%s|%s", R.net, myid, h); else { u8 t[32]; char th[40]; hola_tag(nc, myid, t); hex(t, 16, th); snprintf(l, 200, "ZRED1|HOLA2|%s|%s", h, th); } lsend(c, l);
    char *f[4]; int n = line(c, l, 200, to); if (n < 0) return -1; if (split(l, f, 4) < 2 || strcmp(f[0], "RETO") || unhex(f[1], ns, 16)) return -2;
    char pr[65]; proof(nc, ns, 'c', pr); snprintf(l, 200, "PRUEBA|%s", pr); lsend(c, l); char want[65]; proof(nc, ns, 's', want);
    if (line(c, l, 200, 5000) < 0 || strncmp(l, "BIEN|", 5) || strcmp(l + 5, want)) return -2;
    sess_key(c, nc, ns); c->dir = 0; c->dl = 0; return 0; }
/* cliente: conecta con el equipo id por la primera ruta que funcione (red local, sus direcciones de Internet, el rele)
   y se autentica. force: probar aunque no conste en linea (las comprobaciones del servicio) */
static int cconnect2(conn_t *c, const char *id, char *err, int force) { memset(c, 0, sizeof *c); peer_t *p = peer_mk(id); if (!p || (!force && !online(id))) { strcpy(err, tr_("el equipo no está en línea")); return -1; }
    char rt[12][MAXA]; int nr = 0; if (lan_on(id)) strcpy(rt[nr++], "l");
    if (inet_on) { if (p->via[0]) strncpy0(rt[nr++], p->via, MAXA); addr_t *a = ab_get(id);
        for (const char *q = a ? a->a : ""; *q && nr < 11; ) { char one[MAXA]; int k = 0; while (*q && *q != ',' && k < MAXA - 3) one[k++] = *q++; one[k] = 0; if (*q == ',') q++; if (!one[0] || one[0] == '*') continue;
            char r[MAXA]; snprintf(r, MAXA, "d:%s", one); int dup = 0; for (int i = 0; i < nr; i++) if (!strcmp(rt[i], r)) dup = 1; if (!dup) strcpy(rt[nr++], r); }
        if (R.rele[0] && strcmp(p->via, "r")) strcpy(rt[nr++], "r"); }
    if (!nr) { strcpy(err, tr_("el equipo no está en línea")); return -1; } strcpy(err, tr_("no se pudo conectar"));
    for (int i = 0; i < nr; i++) { const char *r = rt[i]; int lan = r[0] == 'l'; memset(c, 0, sizeof *c); c->s = -1;
        if (lan) { c->s = z_socket(1); if (c->s >= 0 && z_connect(c->s, p->ip, PTCP, 5000)) { z_close(c->s); c->s = -1; } }
        else if (r[0] == 'd') c->s = dial(r + 2, PTCP, service_mode ? 3000 : 5000);
        else if (r[0] == 'r') { char box[40]; buzon(id, box); if (rele_call(c, box)) c->s = -1; }
        if (c->s < 0) continue; c->inet = !lan;
        int hto = service_mode ? 10000 : 15000, h = hs_client(c, id, 0, hto);
        if (h == -1 && lan) { z_close(c->s); c->s = z_socket(1); if (c->s >= 0 && !z_connect(c->s, p->ip, PTCP, 5000)) { c->rn = c->rp = 0; h = hs_client(c, id, 1, hto); } }   /* equipo con la version anterior */
        if (!h) { if (!lan) { strncpy0(p->via, r, 80); p->iseen = z_ms(); if (service_mode) peers_save(); } return 0; }
        if (c->s >= 0) z_close(c->s); if (h == -2) { strcpy(err, tr_("clave de red distinta: el equipo necesita otra invitación")); return -1; } }
    if (!lan_on(id)) p->via[0] = 0; return -1; }
static int cconnect(conn_t *c, const char *id, char *err) { return cconnect2(c, id, err, 0); }
/* una peticion y su respuesta (texto); payload opcional despues de la cabecera */
static long request(const char *id, const char *hdr, const void *pay, long pn, u8 **resp, char *err) { conn_t c; if (cconnect(&c, id, err)) return -1;
    if (csends(&c, hdr) || (pay && csend(&c, pay, pn))) { z_close(c.s); strcpy(err, tr_("se cortó la conexión")); return -1; }
    long n = crecv(&c, resp, 30000); z_close(c.s); if (n < 0) { strcpy(err, tr_("sin respuesta")); return -1; }
    if (strncmp((char *)*resp, "OK", 2)) { snprintf(err, 120, "%s", (char *)*resp + (strncmp((char *)*resp, "ERR|", 4) ? 0 : 4)); free(*resp); *resp = 0; return -1; } return n; }

/* impresoras de este equipo (las de IMPRESORAS.TXT, sin las que vienen de la red) */
static int local_printers(zprinter *o, int max) { zprinter all[32]; int n = zp_printers(all, 32), k = 0; for (int i = 0; i < n && k < max; i++) if (strncmp(all[i].uri, "red://", 6)) o[k++] = all[i]; return k; }
/* trabajos que los programas de este equipo dejan para impresoras de la red (zp_send con red://EQUIPO/ID) */
#define SPOOL "/rd/tmp/RED-IMPRIMIR"
static void spool(void) { zdirent e; char inf[64] = ""; for (int i = 0; !z_readdir(SPOOL, i, &e); i++) if (z_ends(e.name, ".inf")) { strncpy0(inf, e.name, 64); break; } if (!inf[0]) return;
    char b[160], f[200], g[200]; strcpy(b, inf); b[strlen(b) - 4] = 0; snprintf(f, 200, SPOOL "/%s.inf", b); snprintf(g, 200, SPOOL "/%s.env", b); if (z_rename(f, g)) return;   /* recogido: ya no se puede cancelar */
    char *t = z_readfile(g, 0); long n = 0; snprintf(f, 200, SPOOL "/%s.bin", b); u8 *d = z_readfile(f, &n); int rc = -5; char err[120] = "";
    if (t && d) { char *bar = strchr(t, '|'), *sl = strchr(t, '/'); if (bar) *bar = 0; if (sl) *sl = 0; if (sl) { if (bar) safe(bar + 1); char h[300]; snprintf(h, 300, "IMPRIME|%s|%ld|%.200s", sl + 1, n, bar ? bar + 1 : "ZunzunOS");
            conn_t c; int cr = -1; for (int k = 0; k < 3 && (cr = cconnect(&c, t, err)); k++) z_sleep(3000);   /* el otro servicio puede estar ocupado un momento */
            if (!cr) { if (!csends(&c, h)) { for (long o = 0; o < n; o += 65536) csend(&c, d + o, MIN(65536LL, n - o)); if (!n) csend(&c, "", 0); u8 *r = 0; if (crecv(&c, &r, 120000) > 0) { rc = strncmp((char *)r, "OK", 2) ? (strncmp((char *)r, "ERR|", 4) ? -5 : atoi((char *)r + 4) ? atoi((char *)r + 4) : -5) : 0; } free(r); } z_close(c.s); } else rc = -111; } }
    char lg[200]; snprintf(lg, 200, "[prueba] red trabajo enviado a %s: %ld bytes, %d %s", t ? mname(t) : "?", n, rc, err); z_log(lg);
    char res[200], v[16]; snprintf(res, 200, SPOOL "/%s.res", b); snprintf(v, 16, "%d", rc); z_writefile(res, v, strlen(v)); z_unlink(f); z_unlink(g); free(t); free(d); }
/* ---------- carpetas: rutas seguras dentro de la carpeta compartida ---------- */
static share_t *share(const char *id) { for (int i = 0; i < R.ns; i++) if (!strcmp(R.s[i].id, id)) return &R.s[i]; return 0; }
static int resolve(share_t *s, const char *rel, char *o, int cap) { if (strstr(rel, "..") || strchr(rel, '|')) return -1; while (*rel == '/') rel++; snprintf(o, cap, "%s%s%s", s->path, *rel ? "/" : "", rel); int l = strlen(o); while (l > 1 && o[l - 1] == '/') o[--l] = 0; return 0; }

/* ======================= servicio ======================= */
static zwin *note; static char note_t[600]; static u64 note_until;
static void note_draw(void) { if (!note) return; g_clear(note, UI_PANEL); g_frame(note, 0, 0, note->w, note->h, UI_ACC); g_textb(note, 14, 12, tr_("Red local"), UI_TEXT); int y = 36; const char *p = note_t; char l[80];
    while (*p && y < note->h - 20) { int k = 0; while (p[k] && p[k] != '\n' && k < 54) k++; if (p[k] && p[k] != '\n') { int b = k; while (b > 0 && p[b] != ' ') b--; if (b > 10) k = b; } memcpy(l, p, k); l[k] = 0; g_text(note, 14, y, l, UI_TEXT); y += 18; p += k; while (*p == ' ' || *p == '\n') p++; } zw_present(note); }
/* ---------- conversaciones ----------
   Una conversacion es el grupo de equipos al que se envio su primer mensaje: sus ids ordenados y separados por comas
   (este equipo incluido). Las respuestas van a todo el grupo. Se guardan en CHATS, una linea por mensaje y en orden:
   grupo|AAAA-MM-DD HH:MM|id de quien lo envia|entregado (k/n en los propios, vacio en los recibidos)|texto */
#define CHATS "/sys/ZUNZUN/RED-CHATS.TXT"
static int conv_has(const char *conv, const char *id) { int l = strlen(id); for (const char *p = conv; p && *p; ) { if (!strncmp(p, id, l) && (p[l] == ',' || !p[l])) return 1; p = strchr(p, ','); if (p) p++; } return 0; }
static void conv_make(char ids[][12], int n, char *out, int cap) {
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && strcmp(ids[j - 1], ids[j]) > 0; j--) { char t[12]; strcpy(t, ids[j]); strcpy(ids[j], ids[j - 1]); strcpy(ids[j - 1], t); }
    out[0] = 0; for (int i = 0; i < n; i++) { if (i && !strcmp(ids[i], ids[i - 1])) continue; if ((int)(strlen(out) + strlen(ids[i]) + 2) >= cap) break; if (out[0]) strcat(out, ","); strcat(out, ids[i]); } }
/* parte una linea de CHATS en sus 5 campos (sobre una copia) */
static int chat_split(char *l, char **f) { int k = 0; f[k++] = l; for (char *p = l; *p && k < 5; p++) if (*p == '|') { *p = 0; f[k++] = p + 1; } return k == 5; }
static void chat_store(const char *conv, const char *from, const char *text, const char *st) {
    u32 t[6]; z_time(t); long n = 0; char *old = z_readfile(CHATS, &n); sb_t o = {0}; sb_s(&o, "");
    if (old) { long k = 0; if (n > 300000) { k = n - 300000; while (k < n && old[k] != '\n') k++; if (k < n) k++; } sb_add(&o, old + k, n - k); free(old); }   /* los mas antiguos se van */
    sb_f(&o, "%s|%04u-%02u-%02u %02u:%02u|%s|%s|%s\n", conv, t[5], t[4], t[3], t[2], t[1], from, st, text); z_writefile(CHATS, o.p, o.n); free(o.p); z_sync(); }
static void notify(const char *t) { strncpy0(note_t, t, 600); zsys s; z_sysinfo(&s); if (!note) note = zw_new("Red local", s.scr_w - 470, s.scr_h - 220, 460, 150, WF_POPUP); note_until = z_ms() + 12000; note_draw(); }
/* ---------- mensajes con adjuntos ----------
   En CHATS el texto de un mensaje con adjuntos lleva detras, separados por \x1F, sus adjuntos: nombre\x1Etamano\x1Eruta
   (en los recibidos, la ruta en Documentos/Red/Adjuntos; en los enviados, el archivo original). El estado de los propios
   es entregados/total#idmensaje, para ir sumando las entregas que hace luego el servicio.
   Envio (sirve en los dos sentidos de una conexion ya autenticada):
     el que envia:  MENSAJE2|grupo|id|n \n texto \n nombre|tamano|sha256 (n lineas)
     el que recibe: YA (ya lo tenia) | OK|desde0|desde1... (lo que ya tiene de cada adjunto, para reanudar) | ERR|...
     el que envia:  los bytes que faltan de cada adjunto, en trozos de 64 KB
     el que recibe: FIN cuando todo esta guardado y comprobado (o ERR|...) */
typedef struct { char path[200], name[100]; long size; char sha[65]; } att_t;
static void adj_dir(char *o, int cap) { char d[120]; z_docdir(d); snprintf(o, cap, "%s/Red", d); z_mkdir(o); strncat0(o, "/Adjuntos", cap); z_mkdir(o); }
static void base_name(const char *p, char *o, int cap) { const char *b = strrchr(p, '/'); strncpy0(o, b ? b + 1 : p, cap); safe(o); for (char *q = o; *q; q++) if ((u8)*q < 32 || *q == '/' || *q == '\\' || *q == ':') *q = '_'; while (o[0] == '.') memmove(o, o + 1, strlen(o)); if (!o[0]) strcpy(o, "adjunto"); }
static int file_sha(const char *path, long size, char *hx) { int fd = z_open(path, O_READ); if (fd < 0) return -1; zsha256 c; sha256_init(&c); u8 *b = malloc(65536); long got = 0;
    while (got < size) { long r = z_read(fd, b, MIN(65536L, size - got)); if (r <= 0) break; sha256_up(&c, b, r); got += r; } z_close(fd); free(b); if (got != size) return -1; u8 h[32]; sha256_end(&c, h); hex(h, 32, hx); return 0; }
static int att_info(const char *path, att_t *a) { zdirent d; memset(a, 0, sizeof *a); if (z_stat(path, &d) || d.is_dir) return -1; if ((long long)d.size > MAXAF) return -2; strncpy0(a->path, path, 200); base_name(path, a->name, 100); a->size = d.size; return file_sha(path, a->size, a->sha); }
static void att_text(const char *text, att_t *A, int na, char *o, int cap) { strncpy0(o, text, cap); for (int i = 0; i < na; i++) { char e[400]; snprintf(e, 400, "\x1F%s\x1E%ld\x1E%s", A[i].name, A[i].size, A[i].path); strncat0(o, e, cap); } }
/* estado de un mensaje propio: entregados/total */
static void chat_st(const char *msgid, int add, int set_ok, int set_tot) { long n = 0; char *t = z_readfile(CHATS, &n); if (!t) return; sb_t o = {0}; sb_s(&o, ""); int ch = 0; char tag[24]; snprintf(tag, 24, "#%s", msgid);
    for (char *p = t; p && *p; ) { char *e = strchr(p, '\n'); if (e) *e = 0; char l[3200]; strncpy0(l, p, 3200); char *f[5];
        if ((long)strlen(p) < 3190 && chat_split(l, f) && z_ends(f[3], tag)) { int ok = atoi(f[3]), tot = atoi(strchr(f[3], '/') ? strchr(f[3], '/') + 1 : "0"); if (set_tot) { ok = set_ok; tot = set_tot; } else ok = MIN(tot, ok + add);
            sb_f(&o, "%s|%s|%s|%d/%d#%s|%s\n", f[0], f[1], f[2], ok, tot, msgid, f[4]); ch = 1; }
        else { sb_s(&o, p); sb_s(&o, "\n"); } p = e ? e + 1 : 0; }
    if (ch) { z_writefile(CHATS, o.p, o.n); z_sync(); } free(o.p); free(t); }
typedef void (*prog_fn)(long done, long total, const char *name);
/* 0 entregado, 1 el otro equipo tiene la version anterior (no conoce MENSAJE2), -1 fallo (se puede reintentar) */
static int send_msg(conn_t *c, const char *conv, const char *msgid, const char *text, att_t *A, int na, prog_fn pf, char *err) {
    sb_t h = {0}; sb_f(&h, "MENSAJE2|%s|%s|%d\n%s\n", conv, msgid, na, text); for (int i = 0; i < na; i++) sb_f(&h, "%s|%ld|%s\n", A[i].name, A[i].size, A[i].sha);
    int e = csend(c, h.p, h.n); free(h.p); u8 *r = 0; if (e || crecv(c, &r, 30000) < 0) { strcpy(err, tr_("se cortó la conexión")); return -1; }
    if (!strcmp((char *)r, "YA")) { free(r); return 0; } if (!strncmp((char *)r, "ERR|peticion desconocida", 24)) { free(r); return 1; }
    if (strncmp((char *)r, "OK", 2)) { snprintf(err, 120, "%s", (char *)r + (strncmp((char *)r, "ERR|", 4) ? 0 : 4)); free(r); return -1; }
    long off[MAXADJ] = {0}, total = 0, done = 0; { char *q = (char *)r + 2; for (int i = 0; i < na && q && *q == '|'; i++) { off[i] = atol(q + 1); if (off[i] < 0 || off[i] > A[i].size) off[i] = 0; q = strchr(q + 1, '|'); } } free(r);
    for (int i = 0; i < na; i++) total += A[i].size - off[i];
    u8 *b = malloc(65536); int bad = 0;
    for (int i = 0; i < na && !bad; i++) { if (off[i] >= A[i].size) continue; if (off[i]) { char lg[200]; snprintf(lg, 200, "[prueba] red adjunto %s reanudado desde %ld", A[i].name, off[i]); z_log(lg); }
        zdirent d; int fd = z_stat(A[i].path, &d) || (long)d.size != A[i].size ? -1 : z_open(A[i].path, O_READ); if (fd < 0 || (off[i] && z_seek(fd, off[i], 0) != off[i])) { if (fd >= 0) z_close(fd); snprintf(err, 120, "%s: %s", A[i].name, tr_("el archivo ya no está o ha cambiado")); bad = 2; break; }
        for (long o = off[i]; o < A[i].size; ) { long n = z_read(fd, b, MIN(65536L, A[i].size - o)); if (n <= 0 || csend(c, b, n)) { bad = 1; break; } o += n; done += n; if (pf) pf(done, total, A[i].name); }
        z_close(fd); }
    free(b); if (bad) { if (bad == 1) strcpy(err, tr_("se cortó la conexión")); return -1; }
    if (crecv(c, &r, 60000) < 0) { strcpy(err, tr_("sin respuesta")); return -1; } int ok = !strcmp((char *)r, "FIN"); if (!ok) snprintf(err, 120, "%s", (char *)r + (strncmp((char *)r, "ERR|", 4) ? 0 : 4)); free(r); return ok ? 0 : -1; }
/* mensajes ya recibidos (por si el que envia reintenta despues de un corte al final) */
#define RECIB "/sys/ZUNZUN/RED-RECIBIDOS.TXT"
static int msg_seen(const char *id) { char *t = z_readfile(RECIB, 0); int r = 0; for (char *p = t; p && *p; ) { if (!strncmp(p, id, 16) && p[16] == '\n') { r = 1; break; } p = strchr(p, '\n'); if (p) p++; } free(t); return r; }
static void msg_mark(const char *id) { long n = 0; char *t = z_readfile(RECIB, &n); sb_t o = {0}; sb_s(&o, ""); if (t) { long k = n > 17 * 400 ? n - 17 * 400 : 0; sb_add(&o, t + k, n - k); free(t); } sb_f(&o, "%.16s\n", id); z_writefile(RECIB, o.p, o.n); free(o.p); z_sync(); }
static void uniq_path(const char *dir, const char *name, char *o, int cap) { snprintf(o, cap, "%s/%s", dir, name); zdirent d; const char *dot = strrchr(name, '.'); int bl = dot && dot != name ? (int)(dot - name) : (int)strlen(name);
    for (int k = 2; !z_stat(o, &d) && k < 1000; k++) snprintf(o, cap, "%s/%.*s (%d)%s", dir, bl, name, k, dot && dot != name ? dot : ""); }
static int recv_msg(conn_t *c, const char *from, char *pl) {
    char *nl = strchr(pl, '\n'); if (!nl) { csends(c, "ERR|mensaje no valido"); return -1; } *nl = 0; char *f[5]; int k = split(pl, f, 5); char *text = nl + 1, *rest = strchr(text, '\n'); if (rest) *rest++ = 0;
    if (k < 4 || strlen(f[2]) != 16 || unhex(f[2], (u8[8]){0}, 8)) { csends(c, "ERR|mensaje no valido"); return -1; } char conv[700], msgid[20]; strncpy0(conv, f[1], 700); strncpy0(msgid, f[2], 20); int na = atoi(f[3]);
    if (!conv_has(conv, from) || !conv_has(conv, myid)) { csends(c, "ERR|conversacion no valida"); return -1; } if (na < 0 || na > MAXADJ) { csends(c, "ERR|demasiados adjuntos"); return -1; }
    att_t A[MAXADJ]; long tot = 0; for (int i = 0; i < na; i++) { char *e = rest ? strchr(rest, '\n') : 0; if (!rest || !e) { csends(c, "ERR|mensaje no valido"); return -1; } *e = 0; char *g[3];
        if (split(rest, g, 3) != 3 || strlen(g[2]) != 64) { csends(c, "ERR|mensaje no valido"); return -1; } memset(&A[i], 0, sizeof A[i]); base_name(g[0], A[i].name, 100); A[i].size = atol(g[1]); strncpy0(A[i].sha, g[2], 65);
        if (A[i].size < 0 || A[i].size > MAXAF) { csends(c, "ERR|adjunto demasiado grande"); return -1; } tot += A[i].size; rest = e + 1; }
    if (msg_seen(msgid)) { csends(c, "YA"); return 0; }
    char dir[160], pd[200]; adj_dir(dir, 160); snprintf(pd, 200, "%s/.parcial", dir); z_mkdir(pd);
    sb_t o = {0}; sb_s(&o, "OK"); long off[MAXADJ]; for (int i = 0; i < na; i++) { snprintf(A[i].path, 200, "%s/%s-%d", pd, msgid, i); zdirent d; off[i] = z_stat(A[i].path, &d) ? 0 : (long)d.size; if (off[i] > A[i].size) { z_unlink(A[i].path); off[i] = 0; } sb_f(&o, "|%ld", off[i]); }
    if (csend(c, o.p, o.n)) { free(o.p); return -1; } free(o.p);
    if (tot) { char lg[120]; snprintf(lg, 120, "[prueba] red recibiendo %d adjuntos de %s (%ld bytes)", na, mname(from), tot); z_log(lg); }
    /* se escribe en bloques de 1 MB (añadir a un archivo FAT recorre su cadena: pocos bloques grandes van mucho mas deprisa);
       lo ya escrito queda para reanudar */
    u8 *blk = malloc(1 << 20); long bn = 0;
    for (int i = 0; i < na; i++) { if (off[i] >= A[i].size) continue; int fd = z_open(A[i].path, O_WRITE | O_CREATE | O_APPEND); if (fd < 0) { free(blk); csends(c, "ERR|no se pudo guardar"); return -1; } bn = 0;
        while (off[i] + bn < A[i].size) { u8 *q; long m = crecv(c, &q, 30000); int cut = m < 0 || m > (1 << 20) || m > A[i].size - off[i] - bn;
            if (!cut) { if (bn + m > (1 << 20)) { if (z_write(fd, blk, bn) != bn) { free(q); z_close(fd); free(blk); csends(c, "ERR|no cabe en el disco"); return -1; } off[i] += bn; bn = 0; z_sync(); char lg[160]; snprintf(lg, 160, "[prueba] red adjunto %s: %ld MB", A[i].name, off[i] >> 20); z_log(lg); }
                memcpy(blk + bn, q, m); bn += m; free(q); continue; }
            if (m >= 0) free(q); if (bn && z_write(fd, blk, bn) == bn) off[i] += bn; z_close(fd); z_sync(); free(blk); char lg[160]; snprintf(lg, 160, "[prueba] red adjunto %s cortado en %ld", A[i].name, off[i]); z_log(lg); return -1; }
        if (bn && z_write(fd, blk, bn) != bn) { z_close(fd); free(blk); csends(c, "ERR|no cabe en el disco"); return -1; } off[i] += bn; z_close(fd); z_sync(); }
    free(blk);
    for (int i = 0; i < na; i++) { char hx[65]; if (file_sha(A[i].path, A[i].size, hx) || strcmp(hx, A[i].sha)) { z_unlink(A[i].path); z_sync(); csends(c, "ERR|adjunto dañado, se volverá a enviar"); return -1; } }
    for (int i = 0; i < na; i++) { char dst[200]; uniq_path(dir, A[i].name, dst, 200); if (z_rename(A[i].path, dst)) strncpy0(dst, A[i].path, 200); strncpy0(A[i].path, dst, 200); }
    safe(text); char st[2000]; att_text(text, A, na, st, 2000); chat_store(conv, from, st, ""); msg_mark(msgid); csends(c, "FIN");
    char m[600]; if (na) snprintf(m, 600, "%s %s:\n%s\n(%d %s)", tr_("Mensaje de"), mname(from), text, na, tr_("adjuntos en Documentos/Red/Adjuntos")); else snprintf(m, 600, "%s %s:\n%s", tr_("Mensaje de"), mname(from), text); notify(m);
    char lg[400]; int l = snprintf(lg, 400, "[prueba] red mensaje de %s con %d adjuntos", mname(from), na); for (int i = 0; i < na && l < 380; i++) l += snprintf(lg + l, 400 - l, " %s", A[i].path); z_log(lg); return 0; }
/* ---------- cola de salida: mensajes que no se pudieron entregar todavia (equipo apagado, sin ruta, corte) ----------
   un archivo por mensaje y destinatario en RED-SALIDA; el servicio los reintenta y el otro equipo los recoge al conectar */
#define OUTD "/sys/ZUNZUN/RED-SALIDA"
typedef struct { char to[12], conv[700], msgid[20], text[420]; att_t A[MAXADJ]; int na; } out_t;
static out_t OB;
static void out_add(const char *to, const char *conv, const char *msgid, const char *text, att_t *A, int na) { z_mkdir(OUTD); sb_t b = {0}; sb_f(&b, "para=%s\nconv=%s\nmsg=%s\ntexto=%s\n", to, conv, msgid, text);
    for (int i = 0; i < na; i++) sb_f(&b, "adj=%s|%s|%ld|%s\n", A[i].path, A[i].name, A[i].size, A[i].sha); char p[120]; snprintf(p, 120, OUTD "/%s-%s.txt", msgid, to); z_writefile(p, b.p, b.n); free(b.p); z_sync(); }
static int out_load(const char *path, out_t *o) { char *t = z_readfile(path, 0); if (!t) return -1; memset(o, 0, sizeof *o); kvs(t, "para", o->to, 12); kvs(t, "conv", o->conv, 700); kvs(t, "msg", o->msgid, 20); kvs(t, "texto", o->text, 420);
    for (char *p = t; p && *p && o->na < MAXADJ; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : 0) if (!strncmp(p, "adj=", 4)) { char l[600]; int i = 0; p += 4; while (p[i] && p[i] != '\n' && i < 599) { l[i] = p[i]; i++; } l[i] = 0; char *f[4];
            if (split(l, f, 4) == 4) { att_t *a = &o->A[o->na++]; strncpy0(a->path, f[0], 200); strncpy0(a->name, f[1], 100); a->size = atol(f[2]); strncpy0(a->sha, f[3], 65); } }
    free(t); return o->to[0] && o->msgid[0] ? 0 : -1; }
static int out_count(const char *to) { zdirent e; int n = 0; for (int i = 0; !z_readdir(OUTD, i, &e); i++) { int l = strlen(e.name); if (l > 21 && !strncmp(e.name + 17, to, 8) && z_ends(e.name, ".txt")) n++; } return n; }
/* entrega por una conexion ya abierta un mensaje de la cola; si sale bien lo borra de la cola y lo cuenta como entregado */
static int out_send(conn_t *c, const char *file) { char p[160]; snprintf(p, 160, OUTD "/%s", file); if (out_load(p, &OB)) { z_unlink(p); return -1; } char err[120] = "";
    int r = send_msg(c, OB.conv, OB.msgid, OB.text, OB.A, OB.na, 0, err); if (r == 1) { char h[1200]; snprintf(h, sizeof h, "CHAT|%s|%s", OB.conv, OB.text); u8 *q = 0; if (!csends(c, h) && crecv(c, &q, 15000) > 0 && !strncmp((char *)q, "OK", 2)) r = 0; free(q); }
    char lg[300]; snprintf(lg, 300, "[prueba] red pendiente para %s: %s %s", mname(OB.to), r ? "falla" : "entregado", err); z_log(lg);
    if (!r || strstr(err, tr_("el archivo ya no está o ha cambiado"))) { z_unlink(p); z_sync(); if (!r) chat_st(OB.msgid, 1, 0, 0); } return r; }
/* invitacion: este equipo (sin red) recibe la invitacion del administrador y el usuario acepta o no */
/* ventana con el codigo de 6 cifras y Aceptar / Rechazar (la misma en el que invita por Internet y en el invitado) */
static int ask_code(const char *title, const char *l1, const char *l2, const char *l3, const char *code, const char *warn) { zsys s; z_sysinfo(&s); zwin *w = zw_new(title, (s.scr_w - 520) / 2, (s.scr_h - 250) / 2, 520, 250, WF_DECOR); if (!w) return 0;
    int r = -1; u64 t0 = z_ms(); zevent e;
    while (r < 0 && z_ms() - t0 < 120000) { g_clear(w, UI_BG); g_textb(w, 20, 20, l1, UI_TEXT);
        g_text(w, 20, 50, l2, UI_MUTED); g_text2(w, 150, 80, code, UI_ACC, 3); g_text(w, 20, 140, l3, UI_MUTED); if (warn && warn[0]) g_text(w, 20, 160, warn, 0xB45309);
        ui_button(w, 260, 190, 120, 32, "Aceptar", 2); ui_button(w, 390, 190, 110, 32, "Rechazar", 0); zw_present(w);
        if (!z_event(&e, 1000) || e.win != (u32)w->id) continue; if (e.type == EV_CLOSE) r = 0; else if (e.type == EV_DOWN && e.y >= 190 && e.y < 222) { if (e.x >= 260 && e.x < 380) r = 1; else if (e.x >= 390 && e.x < 500) r = 0; } else if (e.type == EV_KEY && e.ch == 27) r = 0; }
    zw_close(w); return r == 1; }
static int ask_accept(const char *who, const char *net, const char *code, const char *oldnet) { char b[200], wn[200] = ""; snprintf(b, 200, tr_("%s te invita a la red \"%s\"."), who, net); if (oldnet[0]) snprintf(wn, 200, tr_("Este equipo dejará la red \"%s\"."), oldnet);
    return ask_code("Invitación a una red local", b, tr_("Comprueba que el administrador ve el mismo código:"), tr_("Si es el mismo, acepta para unir este equipo a la red."), code, wn); }
static void sas(const u8 *sh, const u8 *a, const u8 *b, char *o) { zsha256 c; u8 h[32]; sha256_init(&c); sha256_up(&c, sh, 32); sha256_up(&c, a, 32); sha256_up(&c, b, 32); sha256_end(&c, h); u32 v = ((u32)h[0] << 24 | h[1] << 16 | h[2] << 8 | h[3]) % 1000000; snprintf(o, 12, "%03u %03u", v / 1000, v % 1000); }
static void pair_key(const u8 *sh, u8 *k) { u8 prk[32]; zc_hkdf_extract(256, (const u8 *)"zred1", 5, sh, 32, prk); zc_hkdf_expand(256, prk, (const u8 *)"zred1 invitacion", 16, k, 16); }
/* ======================= Internet ======================= */
static void push_config(void); static void handle_conn(conn_t *c, int inet, u32 ip);
/* ---------- invitaciones por Internet ----------
   El administrador crea un codigo ZRED1-SECRETO@direccion,direccion,rele:host:puerto con un secreto de 80 bits (16 letras)
   que guarda en RED-INVITA (dos dias, un solo uso). El invitado conecta, demuestra que sabe el secreto (HMAC) sin
   enviarlo y el administrador demuestra lo mismo: nadie que no tenga el codigo puede hacerse pasar por ninguno de los
   dos. Despues X25519 y el codigo de 6 cifras en las dos pantallas, como en la red local. */
static long days_civil(int y, int m, int d) { y -= m <= 2; long era = (y >= 0 ? y : y - 399) / 400; long yoe = y - era * 400; long doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468; }
static long now_min(void) { u32 t[6]; z_time(t); return days_civil(t[5], t[4], t[3]) * 1440 + t[2] * 60 + t[1]; }
static const char B32[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
static void b32(const u8 *d, int n, char *o) { int bits = 0, k = 0; u32 v = 0; for (int i = 0; i < n; i++) { v = (v << 8 | d[i]) & 0xFFFF; bits += 8; while (bits >= 5) { o[k++] = B32[(v >> (bits - 5)) & 31]; bits -= 5; } } if (bits) o[k++] = B32[(v << (5 - bits)) & 31]; o[k] = 0; }
static int unb32(const char *s, u8 *o, int n) { int bits = 0, k = 0; u32 v = 0; for (; *s && k < n; s++) { if (*s == '-' || *s == ' ') continue; const char *p = strchr(B32, toupper(*s)); if (!p) return -1; v = (v << 5 | (u32)(p - B32)) & 0xFFFF; bits += 5; if (bits >= 8) { o[k++] = v >> (bits - 8); bits -= 8; } } return k == n ? 0 : -1; }
typedef struct { u8 s[10]; long exp; } inv_t;
static int inv_list(inv_t *o, int max) { char *t = z_readfile(INVF, 0); int n = 0; long now = now_min(); for (char *p = t; p && *p && n < max; ) { char *e = strchr(p, '\n'); if (e) *e = 0; char *f[2];
        if (split(p, f, 2) == 2 && !unhex(f[0], o[n].s, 10)) { o[n].exp = atol(f[1]); if (o[n].exp > now) n++; } p = e ? e + 1 : 0; } free(t); return n; }
static void inv_write(inv_t *I, int n) { sb_t b = {0}; sb_s(&b, ""); for (int i = 0; i < n; i++) { char h[24]; hex(I[i].s, 10, h); sb_f(&b, "%s|%ld\n", h, I[i].exp); } z_writefile(INVF, b.p, b.n); free(b.p); z_sync(); }
static void inv_tag(const u8 *S, char *o) { u8 m[20], h[32]; memcpy(m, "zred1 inv", 9); memcpy(m + 9, S, 10); zc_hash(256, m, 19, h); hex(h, 8, o); }
static void inv_box(const u8 *S, char *o) { u8 h[32]; zc_hmac(256, S, 10, "zred1 buzon", 11, h); hex(h, 12, o); }
static void inv_mac(const u8 *S, char role, const u8 *p1, const u8 *p2, const char *a, const char *b, u8 *o) { u8 m[300]; int n = 0; m[n++] = role; memcpy(m + n, p1, 32); n += 32; if (p2) { memcpy(m + n, p2, 32); n += 32; } n += snprintf((char *)m + n, 300 - n, "%s|%s", a, b); zc_hmac(256, S, 10, m, n, o); }
static void inv_key(const u8 *S, const u8 *sh, u8 *k) { u8 prk[32]; zc_hkdf_extract(256, S, 10, sh, 32, prk); zc_hkdf_expand(256, prk, (const u8 *)"zred1 invitacion internet", 25, k, 16); }
/* ---------- defensa: direcciones de Internet que fallan la autenticacion ----------
   6 fallos en 10 minutos: esa direccion no se atiende durante 10 minutos. Ademas, como mucho 40 saludos por minuto
   desde Internet (el rele cuenta como Internet pero no se bloquea: por el pasan los demas equipos). */
typedef struct { u32 ip; int n; u64 t0, until; } ban_t; static ban_t BAN[32]; static int nhs; static u64 hs_t0;
static ban_t *ban_find(u32 ip, int mk) { ban_t *o = &BAN[0]; for (int i = 0; i < 32; i++) { if (BAN[i].ip == ip) return &BAN[i]; if (BAN[i].t0 < o->t0) o = &BAN[i]; } if (!mk) return 0; memset(o, 0, sizeof *o); o->ip = ip; o->t0 = z_ms(); return o; }
static int ban_check(u32 ip) { u64 now = z_ms(); if (now - hs_t0 > 60000) { hs_t0 = now; nhs = 0; } if (++nhs > 40) return 1; ban_t *b = ip ? ban_find(ip, 0) : 0; return b && b->until > now; }
static void ban_fail(u32 ip) { if (!ip) return; ban_t *b = ban_find(ip, 1); u64 now = z_ms(); if (now - b->t0 > 600000) { b->t0 = now; b->n = 0; } if (++b->n >= 6 && b->until < now) { b->until = now + 600000; char ip2[20], lg[80]; z_fmt_ip(ip, ip2); snprintf(lg, 80, "[prueba] red bloqueada %s por intentos fallidos", ip2); z_log(lg); } }
/* ---------- el router: abrir el puerto TCP 7141 con PCP (RFC 6887), NAT-PMP (RFC 6886) o UPnP IGD ---------- */
static long udp_ask(u32 ip, int port, const void *q, int qn, u8 *r, int rcap, int to) { int s = z_socket(2); if (s < 0) return -1; long n = -1;
    for (int t = 0; t < 2 && n < 0; t++) { if (z_sendto(s, ip, port, q, qn) < 0) break; u64 end = z_ms() + to; while (z_ms() < end) { u32 fr[2]; long m = z_recvfrom(s, r, rcap, (int)(end - z_ms()) + 1, fr); if (m > 0 && fr[0] == ip) { n = m; break; } if (m <= 0) break; } }
    z_close(s); return n; }
static struct { int ok, eport, cgnat; u32 eip, lip, gw; char how[12], ctl[300], svc[100]; u64 at; } NAT;
static u32 pub_ip; static int rele_ok; static char inet_state[16] = "off";
/* 1 hecho, 0 no contesta, -1 error, -2 contesta NAT-PMP (version 0) */
static int pcp_map(u32 gw, u32 lip, int life, u32 *eip, int *eport) { u8 q[60], r[1100]; static u8 nonce[12]; static int nn; if (!nn) { zc_random(nonce, 12); nn = 1; } memset(q, 0, 60); q[0] = 2; q[1] = 1;
    q[4] = life >> 24; q[5] = life >> 16; q[6] = life >> 8; q[7] = life; q[18] = q[19] = 0xFF; memcpy(q + 20, &lip, 4); memcpy(q + 24, nonce, 12); q[36] = 6; q[40] = PTCP >> 8; q[41] = PTCP & 255;
    int sug = *eport ? *eport : PTCP; q[42] = sug >> 8; q[43] = sug; q[54] = q[55] = 0xFF;
    long n = udp_ask(gw, 5351, q, 60, r, sizeof r, 1200); if (n < 4) return 0; if (r[0] == 0) return -2; if (r[0] != 2 || r[1] != 0x81 || n < 60) return 0; if (r[3]) return -1; *eport = r[42] << 8 | r[43]; memcpy(eip, r + 56, 4); return 1; }
static int natpmp_map(u32 gw, int life, u32 *eip, int *eport) { u8 q[12] = {0, 0}, r[64]; long n = udp_ask(gw, 5351, q, 2, r, 64, 1200); if (n < 12 || r[0] != 0 || r[1] != 128 || (r[2] | r[3])) return 0; memcpy(eip, r + 8, 4);
    int sug = *eport ? *eport : PTCP; u8 m[12] = {0, 2, 0, 0, PTCP >> 8, PTCP & 255, (u8)(sug >> 8), (u8)sug, (u8)(life >> 24), (u8)(life >> 16), (u8)(life >> 8), (u8)life};
    n = udp_ask(gw, 5351, m, 12, r, 64, 1200); if (n < 16 || r[1] != 130 || (r[2] | r[3])) return -1; *eport = r[10] << 8 | r[11]; return 1; }
/* contenido de <tag>...</tag> (con o sin prefijo de espacio de nombres) */
static int xml_get(const char *x, const char *tag, char *o, int cap) { int l = strlen(tag); for (const char *p = x; (p = strstr(p, tag)); p += l) { if (p == x || (p[-1] != '<' && p[-1] != ':') || p[l] != '>') continue; const char *a = p + l + 1, *b = strchr(a, '<'); if (!b) return -1;
        int n = MIN((int)(b - a), cap - 1); memcpy(o, a, n); o[n] = 0; while (n && (o[n - 1] == ' ' || o[n - 1] == '\r' || o[n - 1] == '\n')) o[--n] = 0; while (o[0] == ' ' || o[0] == '\n' || o[0] == '\r') memmove(o, o + 1, strlen(o)); return 0; } return -1; }
static int upnp_find(u32 gw) { int s = z_socket(2); if (s < 0) return -1; const char *ms = "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n\r\n";
    z_sendto(s, IP4(239, 255, 255, 250), 1900, ms, strlen(ms)); z_sendto(s, gw, 1900, ms, strlen(ms));   /* tambien directo al router: hay redes sin multidifusion */
    char loc[300] = ""; u64 end = z_ms() + 2500; char b[1500];
    while (!loc[0] && z_ms() < end) { u32 fr[2]; long n = z_recvfrom(s, b, sizeof b - 1, (int)(end - z_ms()) + 1, fr); if (n <= 0) break; b[n] = 0;
        for (char *p = b; p && *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : 0) if (!strncasecmp(p, "LOCATION:", 9)) { p += 9; while (*p == ' ') p++; int i = 0; while (p[i] && p[i] != '\r' && p[i] != '\n' && i < 299) { loc[i] = p[i]; i++; } loc[i] = 0; break; } }
    z_close(s); if (strncmp(loc, "http://", 7)) return -1; char *x = 0; long xl = 0; zhttp_info inf; if (z_http("GET", loc, 0, 0, 0, &x, &xl, &inf) || inf.status != 200 || !x) { free(x); return -1; }
    char base[300]; if (xml_get(x, "URLBase", base, 300)) { strncpy0(base, loc, 300); char *sl = strchr(base + 7, '/'); if (sl) *sl = 0; } else { int l = strlen(base); if (l && base[l - 1] == '/') base[l - 1] = 0; }
    int ok = -1; for (const char *p = x; (p = strstr(p, "<service>")); p += 9) { const char *e = strstr(p, "</service>"); if (!e) break; char blk[1200]; int n = MIN((int)(e - p), 1199); memcpy(blk, p, n); blk[n] = 0; char st[100], cu[200];
        if (xml_get(blk, "serviceType", st, 100) || xml_get(blk, "controlURL", cu, 200) || (!strstr(st, "WANIPConnection") && !strstr(st, "WANPPPConnection"))) continue;
        strncpy0(NAT.svc, st, 100); if (!strncmp(cu, "http", 4)) strncpy0(NAT.ctl, cu, 300); else snprintf(NAT.ctl, 300, "%s%s%s", base, cu[0] == '/' ? "" : "/", cu); ok = 0; break; }
    free(x); return ok; }
static int soap(const char *act, const char *args, char *resp, int cap) { sb_t b = {0}; sb_f(&b, "<?xml version=\"1.0\"?>\r\n<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:%s xmlns:u=\"%s\">%s</u:%s></s:Body></s:Envelope>\r\n", act, NAT.svc, args, act);
    char h[200]; snprintf(h, 200, "SOAPAction: \"%s#%s\"\r\n", NAT.svc, act); zhttp_opt o; memset(&o, 0, sizeof o); o.hdrs = h; o.timeout_ms = 6000; char *out = 0; long ol = 0; zhttp_info inf;
    int e = z_http_ex("POST", NAT.ctl, b.p, b.n, "text/xml; charset=\"utf-8\"", &o, &out, &ol, &inf); free(b.p); resp[0] = 0; if (out) { strncpy0(resp, out, cap); free(out); } return e ? -1 : inf.status; }
static int upnp_map(u32 gw, u32 lip, int life, u32 *eip, int *eport) { if (!NAT.ctl[0] && upnp_find(gw)) return 0; char lp[20], a[700], r[2000]; z_fmt_ip(lip, lp); int p = *eport ? *eport : PTCP, tries = 0;
    for (;;) { snprintf(a, 700, "<NewRemoteHost></NewRemoteHost><NewExternalPort>%d</NewExternalPort><NewProtocol>TCP</NewProtocol><NewInternalPort>%d</NewInternalPort><NewInternalClient>%s</NewInternalClient><NewEnabled>1</NewEnabled><NewPortMappingDescription>ZunzunOS Red local</NewPortMappingDescription><NewLeaseDuration>%d</NewLeaseDuration>", p, PTCP, lp, life);
        int st = soap("AddPortMapping", a, r, 2000); if (st == 200) break; if (st < 0) { NAT.ctl[0] = 0; return 0; }
        if (++tries > 6) return -1; if (strstr(r, "725") && life) life = 0; else if (strstr(r, "718")) p++; else return -1; }   /* 725: solo admite permanentes; 718: el puerto ya esta cogido */
    *eport = p; if (soap("GetExternalIPAddress", "", r, 2000) == 200) { char ip[40]; if (!xml_get(r, "NewExternalIPAddress", ip, 40)) parse_ip(ip, eip); } return 1; }
static void nat_unmap(void) { if (!NAT.ok) return; u32 ip; int ep = NAT.eport; char a[300], r[600];
    if (!strcmp(NAT.how, "UPnP")) { snprintf(a, 300, "<NewRemoteHost></NewRemoteHost><NewExternalPort>%d</NewExternalPort><NewProtocol>TCP</NewProtocol>", ep); soap("DeletePortMapping", a, r, 600); }
    else if (!strcmp(NAT.how, "PCP")) pcp_map(NAT.gw, NAT.lip, 0, &ip, &ep); else natpmp_map(NAT.gw, 0, &ip, &ep);
    NAT.ok = 0; z_log("[prueba] red puerto del router cerrado"); }
/* IP publica vista por el rele o por un servidor STUN (opcional, no hay ninguno por omision) */
static u32 rele_whoami(void) { conn_t c; if (rele_open(&c, "QUIENSOY", "-", 4000)) { rele_ok = 0; return 0; } char l[100]; c.dl = z_ms() + 5000; u32 ip = 0; if (line(&c, l, 100, 5000) > 0 && !strncmp(l, "ZRELE1|ERES|", 12)) parse_ip(l + 12, &ip); z_close(c.s); return ip; }
static u32 stun_ip(void) { char h[80]; int pt; u32 ip; if (hostport(stunsrv, h, 80, &pt, 3478) || resolve_host(h, &ip)) return 0; u8 q[20] = {0, 1, 0, 0, 0x21, 0x12, 0xA4, 0x42}, r[600]; zc_random(q + 8, 12); long n = udp_ask(ip, pt, q, 20, r, 600, 1500); if (n < 20 || r[0] != 1 || r[1] != 1) return 0;
    for (long o = 20; o + 4 <= n; ) { int t = r[o] << 8 | r[o + 1], l = r[o + 2] << 8 | r[o + 3]; if (o + 4 + l > n) break; const u8 *v = r + o + 4;
        if ((t == 0x20 || t == 1) && l >= 8 && v[1] == 1) { u8 b[4]; memcpy(b, v + 4, 4); if (t == 0x20) { b[0] ^= 0x21; b[1] ^= 0x12; b[2] ^= 0xA4; b[3] ^= 0x42; } u32 x; memcpy(&x, b, 4); return x; } o += 4 + ((l + 3) & ~3); } return 0; }
#define ISTAT "/rd/tmp/red-internet.txt"
static void inet_write(void) { addr_t *me = ab_get(myid); char e[24] = "", p[20] = ""; if (NAT.ok) { z_fmt_ip(NAT.eip, e); snprintf(e + strlen(e), 8, ":%d", NAT.eport); } if (pub_ip) z_fmt_ip(pub_ip, p);
    char b[600]; snprintf(b, 600, "estado=%s\nmetodo=%s\nexterna=%s\npublica=%s\ndirs=%s\nrele=%d\n", inet_state, NAT.ok ? NAT.how : "", e, p, me && in_net() ? me->a : "", rele_ok); z_mkdir("/rd/tmp"); z_writefile(ISTAT, b, strlen(b)); }
static int ping_member(const char *id);
/* calcula las direcciones de este equipo en Internet y, si cambian, sube su numero y se las dice a todos */
static void inet_refresh(int force) { znet ni; if (z_netinfo(&ni) || !ni.ip) { strcpy(inet_state, "sinred"); inet_write(); return; }
    if (!inet_on || !in_net()) { nat_unmap(); strcpy(inet_state, "off"); inet_write(); return; }
    if (force || !NAT.at || ni.ip != NAT.lip || ni.gw != NAT.gw || z_ms() - NAT.at > 20 * 60000) {
        u32 eip = 0; int ep = NAT.ok ? NAT.eport : 0, r = 0, r2 = 0; NAT.ok = 0; NAT.how[0] = 0; NAT.cgnat = 0; if (ni.gw != NAT.gw) NAT.ctl[0] = 0;
        if (ni.gw && !ni.local) { r = pcp_map(ni.gw, ni.ip, 3600, &eip, &ep); if (r == 1) strcpy(NAT.how, "PCP");
            else { if (r != -2) { r2 = upnp_map(ni.gw, ni.ip, 3600, &eip, &ep); if (r2 == 1) strcpy(NAT.how, "UPnP"); } if (r2 != 1 && natpmp_map(ni.gw, 3600, &eip, &ep) == 1) strcpy(NAT.how, "NAT-PMP"); } }
        NAT.lip = ni.ip; NAT.gw = ni.gw; NAT.at = z_ms(); if (NAT.how[0]) { NAT.ok = 1; NAT.eip = eip; NAT.eport = ep; NAT.cgnat = ip_cgnat(eip) ? 1 : ip_private(eip) ? 2 : 0; }
        char ip2[20] = "-", lg[160]; if (NAT.ok) z_fmt_ip(eip, ip2); snprintf(lg, 160, "[prueba] red internet: router %s %s:%d%s", NAT.ok ? NAT.how : "no abre el puerto", ip2, NAT.ok ? ep : 0, NAT.cgnat == 1 ? " (CGNAT)" : NAT.cgnat == 2 ? " (doble NAT)" : ""); z_log(lg);
        pub_ip = NAT.ok && !NAT.cgnat ? NAT.eip : 0; if (R.rele[0]) { u32 w = rele_whoami(); if (!pub_ip) pub_ip = w; } if (!pub_ip && stunsrv[0]) pub_ip = stun_ip(); }
    /* las escritas a mano (con *:puerto = la IP publica) y la del router si de verdad es publica */
    char a[MAXA] = "", pip[20] = ""; if (pub_ip) z_fmt_ip(pub_ip, pip);
    for (const char *q = manual; *q; ) { char one[100]; int k = 0; while (*q && *q != ',' && k < 99) one[k++] = *q++; one[k] = 0; if (*q == ',') q++; addr_clean(one); if (!one[0]) continue;
        char x[120]; if (one[0] == '*') { if (!pip[0]) continue; snprintf(x, 120, "%s%s", pip, one + 1); } else strcpy(x, one); if (!strchr(x, ':')) snprintf(x + strlen(x), 8, ":%d", PTCP); if (strlen(a) + strlen(x) + 2 < MAXA && !strstr(a, x)) { if (a[0]) strcat(a, ","); strcat(a, x); } }
    if (NAT.ok && !NAT.cgnat) { char x[40]; z_fmt_ip(NAT.eip, x); snprintf(x + strlen(x), 8, ":%d", NAT.eport); if (!strstr(a, x) && strlen(a) + strlen(x) + 2 < MAXA) { if (a[0]) strcat(a, ","); strcat(a, x); } }
    strcpy(inet_state, NAT.ok ? (NAT.cgnat == 1 ? "cgnat" : NAT.cgnat == 2 ? "doble" : "ok") : a[0] ? "manual" : ni.local || !ni.gw ? "sinrouter" : "cerrado");
    addr_t *me = ab_get(myid); int ann = 0; if (!me || strcmp(me->a, a)) { u32 seq = me ? me->seq + 1 : 1; ab_merge(myid, seq, a, 1); ab_save(); ann = 1; char lg[300]; snprintf(lg, 300, "[prueba] red direcciones propias %u: %s", seq, a[0] ? a : "ninguna"); z_log(lg); }
    inet_write(); if (ann) for (int i = 0; i < R.nm; i++) if (strcmp(R.m[i].id, myid) && !lan_on(R.m[i].id)) ping_member(R.m[i].id); }
/* recoge los mensajes que otro equipo guardaba para este (cuando este no se puede alcanzar desde fuera) */
static void recoge(conn_t *c, const char *id) { if (csends(c, "RECOGE")) return; for (;;) { u8 *q; long n = crecv(c, &q, 60000); if (n < 0) return; if (!strcmp((char *)q, "NADA")) { free(q); return; }
        int r = strncmp((char *)q, "MENSAJE2|", 9) ? -1 : recv_msg(c, id, (char *)q); free(q); if (r) return; } }
/* comprueba que un equipo de Internet esta, intercambia direcciones (las suyas y las que sabe de los demas) y recoge mensajes */
static int ping_member(const char *id) { conn_t c; char err[120]; if (cconnect2(&c, id, err, 1)) return -1; addr_t *me = ab_get(myid); char h[300]; snprintf(h, 300, "PING|%u|%s", me ? me->seq : 0, me ? me->a : ""); u8 *r = 0;
    if (csends(&c, h) || crecv(&c, &r, 15000) < 0 || strncmp((char *)r, "OK|", 3)) { free(r); z_close(c.s); return -1; }
    int pend = atoi((char *)r + 3), ch = 0; for (char *p = strchr((char *)r, '\n'); p; ) { p++; char *e = strchr(p, '\n'); if (e) *e = 0; char *f[3]; if (split(p, f, 3) == 3) ch |= ab_merge(f[0], (u32)atol(f[1]), f[2], 0); p = e; } free(r);
    if (ch) { ab_save(); char lg[80]; snprintf(lg, 80, "[prueba] red direcciones nuevas por %s", mname(id)); z_log(lg); }
    if (pend > 0) recoge(&c, id); z_close(c.s); peers_save(); return 0; }
static void inet_tick(void) { static int rr; static u64 lt; u64 now = z_ms(); if (!in_net() || !inet_on || !R.nm || now - lt < 5000) return; lt = now;
    for (int t = 0; t < R.nm; t++) { rr = (rr + 1) % R.nm; const char *id = R.m[rr].id; if (!strcmp(id, myid) || lan_on(id)) continue; peer_t *p = peer_mk(id); if (!p || now - p->ichk < 55000) continue;
        addr_t *a = ab_get(id); if (!R.rele[0] && (!a || !a->a[0])) continue; p->ichk = now; ping_member(id); break; } }
static void out_tick(void) { static u64 lt; u64 now = z_ms(); if (!in_net() || now - lt < 12000) return; lt = now; zdirent e; char nm[16][40]; int n = 0;
    for (int i = 0; !z_readdir(OUTD, i, &e) && n < 16; i++) if (z_ends(e.name, ".txt") && strlen(e.name) == 29) strncpy0(nm[n++], e.name, 40);
    for (int i = 0; i < n; i++) { char to[12]; strncpy0(to, nm[i] + 17, 9); if (!member(to)) { char p[80]; snprintf(p, 80, OUTD "/%s", nm[i]); z_unlink(p); continue; } if (!online(to)) continue;
        conn_t c; char err[120]; if (cconnect(&c, to, err)) continue; out_send(&c, nm[i]); z_close(c.s); } }
/* ---------- esperar llamadas en el rele: una conexion abierta en el buzon de este equipo y otra, en el administrador,
   en el de la ultima invitacion pendiente (y una segunda en el propio, para no quedar sin nadie mientras se atiende una). El rele manda SIGUE cada 25 s; LLAMADA cuando alguien conecta ---------- */
typedef struct { conn_t c; int on; char box[40]; u64 last, retry; } wslot_t; static wslot_t WS[3];
static void rele_tick(void) { u64 now = z_ms(); inv_t I[8]; int ni = is_admin() && R.rele[0] && inet_on ? inv_list(I, 8) : 0;
    for (int i = 0; i < 3; i++) { wslot_t *w = &WS[i]; char want[40] = "";   /* dos en el buzon propio: mientras se atiende una llamada, la otra sigue esperando */
        if (inet_on && in_net() && R.rele[0]) { if (i < 2) buzon(myid, want); else if (ni) inv_box(I[ni - 1].s, want); }
        if (w->on && strcmp(w->box, want)) { z_close(w->c.s); w->on = 0; }
        if (!w->on && want[0] && now >= w->retry) { if (rele_open(&w->c, "ESPERA", want, 4000)) { w->retry = now + 30000; if (!i) rele_ok = 0; continue; } w->on = 1; strcpy(w->box, want); w->last = now; if (!i) { rele_ok = 1; z_log("[prueba] red esperando en el rele"); inet_write(); } }
        if (!w->on) continue; char l[200]; int r;
        while ((r = try_line(&w->c, l, 200)) == 1) { if (!strcmp(l, "ZRELE1|SIGUE")) w->last = now; else if (!strcmp(l, "ZRELE1|LLAMADA")) { z_log("[prueba] red llamada por el rele"); w->on = 0; handle_conn(&w->c, 1, 0); z_close(w->c.s); w->retry = 0; break; }
            else { z_close(w->c.s); w->on = 0; w->retry = now + 60000; break; } }
        if (w->on && (r < 0 || now - w->last > 75000)) { z_close(w->c.s); w->on = 0; w->retry = now + (r < 0 ? 5000 : 0); if (!i) { rele_ok = 0; inet_write(); } } } }
/* el administrador recibe a un equipo que trae un codigo de invitacion: ZRED1|UNIRSE|etiqueta|pub|id|nombre|hmac */
static void srv_join(conn_t *c, char **f, int k, u32 ip) { cfg_load(); inv_t I[8]; int ni = is_admin() ? inv_list(I, 8) : 0, w = -1;
    for (int i = 0; k >= 7 && i < ni; i++) { char t[20]; inv_tag(I[i].s, t); if (!strcmp(t, f[2])) w = i; }
    u8 bpub[32], mac[32], want[32]; char bid[12], bname[32]; if (w < 0 || unhex(f[3], bpub, 32) || unhex(f[6], mac, 32)) { lsend(c, "NO"); if (c->inet) ban_fail(ip); z_log("[prueba] red union: invitacion desconocida"); return; }
    strncpy0(bid, f[4], 12); strncpy0(bname, f[5], 32); inv_mac(I[w].s, 'b', bpub, 0, bid, bname, want);
    if (!ct_eq(mac, want, 32) || strlen(bid) != 8 || !strcmp(bid, myid)) { lsend(c, "NO"); if (c->inet) ban_fail(ip); z_log("[prueba] red union: prueba incorrecta"); return; } safe(bname);
    u8 priv[32], apub[32], sh[32], am[32]; zc_random(priv, 32); x25519_base(apub, priv); x25519(sh, priv, bpub); char nm[32], nn[40]; strcpy(nm, myname); strcpy(nn, R.netname); safe(nm); safe(nn);
    inv_mac(I[w].s, 'a', apub, bpub, nm, nn, am); char ah[65], amh[65], l[400]; hex(apub, 32, ah); hex(am, 32, amh); snprintf(l, 400, "PUB|%s|%s|%s|%s|%s", ah, myid, nm, nn, amh); lsend(c, l);
    char code[12]; sas(sh, apub, bpub, code); { char lg[120]; snprintf(lg, 120, "[prueba] red union por internet de %s: codigo %s", bname, code); z_log(lg); }
    c->dl = 0; char b1[200]; snprintf(b1, 200, tr_("%s quiere unirse a la red \"%s\" con tu invitación."), bname, R.netname);
    int ok = ask_code("Invitación por Internet", b1, tr_("Comprueba que el otro equipo ve el mismo código:"), tr_("Si es el mismo, acepta para que entre en la red."), code, member(bid) ? tr_("Ese equipo ya estaba en la red: se le dará la clave otra vez.") : "");
    if (!ok) { lsend(c, "RECHAZO"); z_log("[prueba] red union rechazada"); return; }
    if (line(c, l, 400, 130000) < 0 || strcmp(l, "ACEPTO")) { z_log("[prueba] red union: el otro no acepto"); return; }
    cfg_load(); if (!member(bid) && R.nm < MAXM) { strcpy(R.m[R.nm].id, bid); strcpy(R.m[R.nm].name, bname); R.nm++; } R.ver++; sb_t b = {0}; cfg_text(&b); u8 *o = malloc(b.n + 20); o[0] = b.n; o[1] = b.n >> 8; o[2] = b.n >> 16; o[3] = b.n >> 24;
    u8 kk[16], iv[12] = {0}; inv_key(I[w].s, sh, kk); zgcm g; gcm_init(&g, kk, 16); gcm_seal(&g, iv, o, 4, (u8 *)b.p, b.n, o + 4, o + 4 + b.n); raw_send(c, o, b.n + 20); free(o); free(b.p);
    if (line(c, l, 400, 15000) < 0 || strcmp(l, "LISTO")) { z_log("[prueba] red union: no se completo"); cfg_load(); return; }
    cfg_save(); memmove(&I[w], &I[w + 1], (ni - w - 1) * sizeof I[0]); inv_write(I, ni - 1); peer_t *p = peer_mk(bid); if (p) { p->iseen = z_ms(); strncpy0(p->name, bname, 32); peers_save(); }
    char m[200]; snprintf(m, 200, tr_("%s ya forma parte de la red"), bname); notify(m); { char lg[80]; snprintf(lg, 80, "[prueba] red invitado por internet %s", bid); z_log(lg); } push_config(); }
static void srv_invite(conn_t *c, char **f, int k) {
    if (k < 7 || (in_net() && !strcmp(R.net, f[2]))) { lsend(c, "NO|ya estoy en esa red"); return; } char oldnet[40] = ""; if (in_net()) strcpy(oldnet, R.netname); u8 apub[32], priv[32], pub[32], sh[32]; if (unhex(f[6], apub, 32)) return;
    zc_random(priv, 32); x25519_base(pub, priv); x25519(sh, priv, apub); char ph[65], l[200], code[12]; hex(pub, 32, ph); char nm[32]; strcpy(nm, myname); safe(nm); snprintf(l, 200, "PUB|%s|%s|%s", ph, myid, nm); lsend(c, l); sas(sh, apub, pub, code);
    z_log("[prueba] red invitacion recibida"); int ok = ask_accept(f[5], f[3], code, oldnet); lsend(c, ok ? "ACEPTO" : "RECHAZO"); if (!ok) { z_log("[prueba] red invitacion rechazada"); return; } z_log("[prueba] red invitacion aceptada");
    u8 h[4]; if (raw_recv(c, h, 4, 15000)) { z_log("[prueba] red invitacion: no llego la configuracion"); return; } long n = h[0] | h[1] << 8 | h[2] << 16 | (long)h[3] << 24; if (n <= 0 || n > 200000) { z_log("[prueba] red invitacion: tamano mal"); return; } u8 *b = malloc(n + 16), *pl = malloc(n + 1);
    if (raw_recv(c, b, n + 16, 15000)) { free(b); free(pl); return; } u8 kk[16], iv[12] = {0}; pair_key(sh, kk); zgcm g; gcm_init(&g, kk, 16);
    if (gcm_open(&g, iv, h, 4, b, n, pl, b + n)) { free(b); free(pl); lsend(c, "MAL"); z_log("[prueba] red invitacion: no se pudo descifrar"); return; } pl[n] = 0; free(b);
    if (cfg_parse((char *)pl)) z_log("[prueba] red invitacion: configuracion no valida");
    if (cfg_parse((char *)pl) == 0) { cfg_save(); mk_shares(); lsend(c, "LISTO"); char m[200]; snprintf(m, 200, tr_("Este equipo ya forma parte de la red \"%s\"."), R.netname); notify(m); char lg[120]; snprintf(lg, 120, "[prueba] red unido a %s como %s", R.net, myid); z_log(lg); } free(pl); }
static void srv_session(conn_t *c, char **f, int k, u32 ip) {
    cfg_load();   /* la ventana de este mismo equipo puede haber cambiado la configuracion hace un momento */
    char who[12] = ""; u8 nc[16], ns[16];
    if (!strcmp(f[1], "HOLA2")) { u8 tg[16]; if (in_net() && k >= 4 && !unhex(f[2], nc, 16) && !unhex(f[3], tg, 16)) for (int i = -1; i < R.nm && !who[0]; i++) { const char *id = i < 0 ? R.admin : R.m[i].id; u8 t[32]; hola_tag(nc, id, t); if (ct_eq(t, tg, 16)) strncpy0(who, id, 12); } }
    else if (in_net() && k >= 5 && !strcmp(f[2], R.net) && !unhex(f[4], nc, 16)) { strncpy0(who, f[3], 12); if (!member(who) && strcmp(who, R.admin)) who[0] = 0; }
    if (!who[0] || !strcmp(who, myid)) { lsend(c, "NO"); if (c->inet) ban_fail(ip); return; }
    zc_random(ns, 16); char h[40], l[200]; hex(ns, 16, h); snprintf(l, 200, "RETO|%s", h); lsend(c, l);
    char want[65]; proof(nc, ns, 'c', want); if (line(c, l, 200, 5000) < 0) { z_log("[prueba] red saludo sin terminar"); return; }   /* se canso de esperar: no es un ataque */
    if (strncmp(l, "PRUEBA|", 7) || strcmp(l + 7, want)) { z_log("[prueba] red prueba incorrecta"); if (c->inet) ban_fail(ip); return; }
    char pr[65]; proof(nc, ns, 's', pr); snprintf(l, 200, "BIEN|%s", pr); lsend(c, l); sess_key(c, nc, ns); c->dir = 1; c->dl = 0;
    if (c->inet) { peer_t *p = peer_mk(who); if (p) { p->iseen = z_ms(); peers_save(); } }
    u8 *q; long n; while ((n = crecv(c, &q, 20000)) >= 0) { if (!strncmp((char *)q, "MENSAJE2|", 9)) { { char lg[120]; snprintf(lg, 120, "[prueba] red CHAT de %s (con adjuntos posibles)%s", mname(who), c->inet ? " por internet" : ""); z_log(lg); } int r = recv_msg(c, who, (char *)q); free(q); if (r) break; continue; }
        char *a[6]; char hd[1400]; strncpy0(hd, (char *)q, 1400); free(q); int na = split(hd, a, 6); char lg[200]; snprintf(lg, 200, "[prueba] red %s de %s%s", a[0], mname(who), c->inet ? " por internet" : ""); z_log(lg);
        if (!strcmp(a[0], "PING") && na >= 3) { if (ab_merge(who, (u32)atol(a[1]), a[2], 0)) { ab_save(); char lg2[300]; snprintf(lg2, 300, "[prueba] red direccion de %s: %s", mname(who), a[2]); z_log(lg2); }
            sb_t o = {0}; sb_f(&o, "OK|%d\n", out_count(who)); for (int i = 0; i < nab; i++) if (member(AB[i].id) || !strcmp(AB[i].id, R.admin)) sb_f(&o, "%s|%u|%s\n", AB[i].id, AB[i].seq, AB[i].a); csend(c, o.p, o.n); free(o.p); continue; }
        if (!strcmp(a[0], "RECOGE")) { zdirent e; char nm[16][40]; int nn = 0; for (int i = 0; !z_readdir(OUTD, i, &e) && nn < 16; i++) if (strlen(e.name) == 29 && !strncmp(e.name + 17, who, 8)) strncpy0(nm[nn++], e.name, 40);
            int bad = 0; for (int i = 0; i < nn && !bad; i++) bad = out_send(c, nm[i]) < 0; if (bad) break; csends(c, "NADA"); continue; }
        if (!strcmp(a[0], "LISTA") && na >= 3) { share_t *s = share(a[1]); char p[400]; if (!s || strcmp(s->host, myid) || resolve(s, a[2], p, 400)) { csends(c, "ERR|carpeta no disponible"); continue; }
            sb_t o = {0}; sb_s(&o, "OK\n"); zdirent e; for (int i = 0; !z_readdir(p, i, &e); i++) { if (e.name[0] == '.') continue; sb_f(&o, "%c|%lu|%s\n", e.is_dir ? 'D' : 'F', (unsigned long)e.size, e.name); } csend(c, o.p, o.n); free(o.p); }
        else if (!strcmp(a[0], "LEE") && na >= 3) { share_t *s = share(a[1]); char p[400]; long fl; u8 *d; if (!s || strcmp(s->host, myid) || resolve(s, a[2], p, 400) || !(d = z_readfile(p, &fl))) { csends(c, "ERR|no se pudo leer"); continue; }
            char ok[40]; snprintf(ok, 40, "OK|%ld", fl); csends(c, ok); for (long o = 0; o < fl; o += 65536) csend(c, d + o, MIN(65536LL, fl - o)); free(d); }
        else if (!strcmp(a[0], "ESCRIBE") && na >= 4) { share_t *s = share(a[1]); char p[400]; long sz = atol(a[3]), got = 0; sb_t d = {0}; sb_add(&d, "", 0);
            while (got < sz) { u8 *q2; long m = crecv(c, &q2, 20000); if (m < 0) break; sb_add(&d, q2, m); free(q2); got += m; }
            if (got != sz) break; if (!s || strcmp(s->host, myid) || !s->rw || resolve(s, a[2], p, 400) || sz > MAXF) { csends(c, s && !s->rw ? "ERR|carpeta de solo lectura" : "ERR|no se pudo escribir"); free(d.p); continue; }
            int e = z_writefile(p, d.p, d.n); free(d.p); z_sync(); csends(c, e ? "ERR|no se pudo escribir" : "OK"); }
        else if ((!strcmp(a[0], "BORRA") || !strcmp(a[0], "CARPETA")) && na >= 3) { share_t *s = share(a[1]); char p[400]; if (!s || strcmp(s->host, myid) || !s->rw || resolve(s, a[2], p, 400) || !a[2][0]) { csends(c, "ERR|no permitido"); continue; }
            int e = a[0][0] == 'B' ? z_unlink(p) : z_mkdir(p); z_sync(); csends(c, e ? "ERR|no se pudo" : "OK"); }
        else if (!strcmp(a[0], "RENOMBRA") && na >= 4) { share_t *s = share(a[1]); char p[400], q3[400], r2[400]; char *sl = strrchr(a[2], '/'); snprintf(r2, 400, "%.*s%s%s", sl ? (int)(sl - a[2]) : 0, a[2], sl ? "/" : "", a[3]);
            if (!s || strcmp(s->host, myid) || !s->rw || strchr(a[3], '/') || resolve(s, a[2], p, 400) || resolve(s, r2, q3, 400)) { csends(c, "ERR|no permitido"); continue; } int e = z_rename(p, q3); z_sync(); csends(c, e ? "ERR|no se pudo" : "OK"); }
        else if ((!strcmp(a[0], "CHAT") && na >= 3) || (!strcmp(a[0], "MENSAJE") && na >= 2)) {   /* MENSAJE: version anterior, conversacion de dos */
            char conv[700], *text = a[0][0] == 'C' ? a[2] : a[1]; if (a[0][0] == 'C') strncpy0(conv, a[1], 700); else { char ids[2][12]; strcpy(ids[0], who); strcpy(ids[1], myid); conv_make(ids, 2, conv, 700); }
            if (!conv_has(conv, who) || !conv_has(conv, myid)) { csends(c, "ERR|conversacion no valida"); continue; }
            safe(text); chat_store(conv, who, text, ""); csends(c, "OK"); char m[600]; snprintf(m, 600, "%s %s:\n%s", tr_("Mensaje de"), mname(who), text); notify(m); }
        else if (!strcmp(a[0], "CONFIG") && na >= 2) { if (strcmp(who, R.admin)) { csends(c, "ERR|solo el administrador"); continue; } u8 *t2; long m = crecv(c, &t2, 20000); if (m < 0) break;
            char key[32]; memcpy(key, R.key, 32); char net[12]; strcpy(net, R.net); int ver = R.ver; if (cfg_parse((char *)t2) || strcmp(R.net, net)) { cfg_load(); csends(c, "ERR|configuracion no valida"); free(t2); continue; }
            if (R.ver < ver) { cfg_load(); csends(c, "OK"); free(t2); continue; } if (!member(myid)) { z_unlink(CFG); memset(&R, 0, sizeof R); z_sync(); z_log("[prueba] red sacado de la red"); notify(tr_("El administrador ha sacado este equipo de la red local.")); csends(c, "OK"); free(t2); break; }
            cfg_save(); mk_shares(); free(t2); csends(c, "OK"); (void)key; char lg2[80]; snprintf(lg2, 80, "[prueba] red configuracion %d (%d impresoras)", R.ver, R.np); z_log(lg2); }
        else if (!strcmp(a[0], "IMPRESORAS")) { zprinter pr[16]; int n = local_printers(pr, 16); sb_t o = {0}; sb_s(&o, "OK\n"); for (int i = 0; i < n; i++) { char nm[64]; strcpy(nm, pr[i].name); safe(nm); sb_f(&o, "%s|%s|%d|%d|%d|%s\n", nm, pr[i].lang, pr[i].paper, pr[i].dpi, pr[i].color, pr[i].model); } csend(c, o.p, o.n); free(o.p); }
        else if (!strcmp(a[0], "IMPRIME") && na >= 4) { long sz = atol(a[2]), got = 0; sb_t d = {0}; sb_add(&d, "", 0); while (got < sz) { u8 *q2; long m = crecv(c, &q2, 20000); if (m < 0) break; sb_add(&d, q2, m); free(q2); got += m; } if (got != sz) { free(d.p); break; }
            rprn_t *rp = 0; for (int i = 0; i < R.np; i++) if (!strcmp(R.p[i].id, a[1]) && !strcmp(R.p[i].host, myid)) rp = &R.p[i]; zprinter pr[16]; int n = local_printers(pr, 16), k = -1; for (int i = 0; rp && i < n; i++) if (!strcmp(pr[i].name, rp->pname)) k = i;
            if (k < 0) { csends(c, "ERR|la impresora ya no esta en este equipo"); free(d.p); continue; } char job[120]; snprintf(job, 120, "%s (%s)", a[3], mname(who)); char m2[200]; snprintf(m2, 200, tr_("Imprimiendo un trabajo de %s en %s"), mname(who), pr[k].name); notify(m2);
            int e = zp_send_job(&pr[k], (u8 *)d.p, d.n, job); free(d.p); char lg2[200]; snprintf(lg2, 200, "[prueba] red impresion de %s en %s: %ld bytes, %d", mname(who), pr[k].name, sz, e); z_log(lg2); char r2[40]; snprintf(r2, 40, e ? "ERR|%d" : "OK", e); csends(c, r2); }
        else if (!strcmp(a[0], "PIDECONFIG")) { sb_t o = {0}; sb_s(&o, "OK\n"); cfg_text(&o); csend(c, o.p, o.n); free(o.p); }
        else csends(c, "ERR|peticion desconocida"); } }
/* una conexion entrante (directa o llegada por el rele): por Internet solo se atiende lo que va autenticado */
static void handle_conn(conn_t *c, int inet, u32 ip) { c->inet = inet; c->dl = z_ms() + 8000; char l[400]; if (line(c, l, 400, 5000) <= 0) { if (inet) ban_fail(ip); return; } char *f[8]; int k = split(l, f, 8);
    if (k < 2 || strcmp(f[0], "ZRED1")) { if (inet) ban_fail(ip); return; }
    if (!strcmp(f[1], "INVITA")) { if (inet) { z_log("[prueba] red invitacion desde Internet rechazada"); ban_fail(ip); return; } c->dl = 0; srv_invite(c, f, k); }   /* la invitacion de la red local abre una ventana: solo desde la red local */
    else if (!strcmp(f[1], "HOLA")) { if (!inet) srv_session(c, f, k, ip); }
    else if (!strcmp(f[1], "HOLA2")) srv_session(c, f, k, ip);
    else if (!strcmp(f[1], "UNIRSE")) srv_join(c, f, k, ip);
    else if (inet) ban_fail(ip); }
static int service(void) { service_mode = 1;
    ident(); cfg_load(); ab_load(); z_unlink(ISTAT); int us = z_socket(2), ts = z_socket(1); if (us < 0 || ts < 0 || z_bind(us, PUDP) || z_bind(ts, PTCP)) { z_log("[prueba] red servicio: no se pudo abrir los puertos"); return 1; }
    char lg[120]; snprintf(lg, 120, "[prueba] red servicio listo: %s %s (%s)", myid, myname, in_net() ? R.net : "sin red"); z_log(lg); u64 last_soy = 0, last_cfg = 0; u8 m[1500];
    for (;;) { u64 now = z_ms(); if (now - last_soy > 15000) { last_soy = now; char o[200]; cfg_load(); soy(o, 200); z_sendto(us, 0xFFFFFFFF, PUDP, o, strlen(o)); }
        u32 fr[2]; long n; while ((n = z_recvfrom(us, m, sizeof m - 1, 20, fr)) > 0) on_udp(us, (char *)m, n, fr[0], fr[1]);
        { static u64 ls; if (now - ls > 400) { ls = now; spool(); } }
        int cs = z_accept(ts, 150); if (cs >= 0) { conn_t c; memset(&c, 0, sizeof c); c.s = cs; u32 pr[2] = {0, 0}; z_peer(cs, pr); int inet = !from_lan(pr[0]);
            if (inet && (!inet_on || ban_check(pr[0]))) { char lg[80], ip2[20]; z_fmt_ip(pr[0], ip2); snprintf(lg, 80, "[prueba] red conexion de Internet rechazada (%s)", ip2); z_log(lg); } else handle_conn(&c, inet, pr[0]); z_close(cs); }
        { static u64 lr; zdirent fl; int force = !z_stat("/rd/tmp/red-comprobar", &fl); if (force) { z_unlink("/rd/tmp/red-comprobar"); ab_load(); }
            { static u32 lip; znet nn; if (!z_netinfo(&nn) && nn.ip != lip) { lip = nn.ip; force = 1; } }   /* direccion nueva (DHCP, otro cable): otra vez al router */
            if (force) cfg_load(); { static char ln[12] = "-"; if (strcmp(ln, R.net)) { strcpy(ln, R.net); force = 1; } }   /* red creada o unida hace un momento */ if (force || now - lr > 60000 || !lr) { lr = z_ms(); id_reload(); inet_refresh(force); } }
        rele_tick(); inet_tick(); out_tick();
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
    char gone[12]; strcpy(gone, R.m[i].id); memmove(&R.m[i], &R.m[i + 1], (R.nm - i - 1) * sizeof R.m[0]); R.nm--; for (int k = 0; k < R.ns; k++) if (!strcmp(R.s[k].host, gone)) { memmove(&R.s[k], &R.s[k + 1], (R.ns - k - 1) * sizeof R.s[0]); R.ns--; k--; } for (int k = 0; k < R.np; k++) if (!strcmp(R.p[k].host, gone)) { memmove(&R.p[k], &R.p[k + 1], (R.np - k - 1) * sizeof R.p[0]); R.np--; k--; }
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
static void add_printer(void) { const char *it[MAXM]; int map[MAXM], n = 0; for (int i = 0; i < R.nm; i++) if (online(R.m[i].id)) { map[n] = i; it[n++] = R.m[i].name; } if (!n) return;
    int sx, sy; zw_screen(W, 200, 120, &sx, &sy); int r = n == 1 ? 0 : ui_menu(sx, sy, it, n); if (r < 0) return; mem_t *h = &R.m[map[r]]; zprinter pr[16]; int np2 = 0;
    if (!strcmp(h->id, myid)) np2 = local_printers(pr, 16);
    else { u8 *q; char err[120]; if (request(h->id, "IMPRESORAS", 0, 0, &q, err) < 0) { snprintf(msg, 300, "%s: %s", h->name, err); return; }
        for (char *p = strchr((char *)q, '\n'); p && np2 < 16; ) { p++; char *e = strchr(p, '\n'); if (!e) break; *e = 0; char *f[6]; if (split(p, f, 6) == 6) { zprinter *z = &pr[np2++]; memset(z, 0, sizeof *z); strncpy0(z->name, f[0], 64); strncpy0(z->lang, f[1], 8); z->paper = atoi(f[2]); z->dpi = atoi(f[3]); z->color = atoi(f[4]); strncpy0(z->model, f[5], 64); } p = e; } free(q); }
    if (!np2) { snprintf(msg, 300, tr_("%s no tiene impresoras configuradas (se añaden en Impresoras de ese equipo)"), h->name); return; }
    const char *pn[16]; for (int i = 0; i < np2; i++) pn[i] = pr[i].name; int k = np2 == 1 ? 0 : ui_menu(sx, sy + 40, pn, np2); if (k < 0 || R.np >= MAXP) return;
    rprn_t q; memset(&q, 0, sizeof q); u8 rr[3]; zc_random(rr, 3); hex(rr, 3, q.id); snprintf(q.name, 64, "%.40s (%s %.16s)", pr[k].name, tr_("en"), h->name); safe(q.name); strcpy(q.host, h->id); strncpy0(q.pname, pr[k].name, 64); safe(q.pname);
    strncpy0(q.lang, pr[k].lang, 8); q.paper = pr[k].paper; q.dpi = pr[k].dpi; q.color = pr[k].color; strncpy0(q.model, pr[k].model, 64); safe(q.model); R.p[R.np++] = q; push_config();
    snprintf(msg, 300, tr_("%s compartida: ya aparece al imprimir en los demás equipos"), q.name); char lg[160]; snprintf(lg, 160, "[prueba] red impresora compartida %s", q.name); z_log(lg); }
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
    csends(&c, h); for (long o = 0; o < n; o += 65536) csend(&c, d + o, MIN(65536LL, n - o)); if (!n) csend(&c, "", 0); free(d); u8 *r; long m = crecv(&c, &r, 30000); z_close(c.s);
    if (m < 0 || strncmp((char *)r, "OK", 2)) { snprintf(msg, 300, "%s: %s", tr_("No se pudo subir"), m < 0 ? tr_("sin respuesta") : (char *)r + 4); if (m >= 0) free(r); return; } free(r); char lg[400]; snprintf(lg, 400, "[prueba] red subido %s (%ld)", b, n); z_log(lg); list_dir(); snprintf(msg, 300, "%s: %s", tr_("Subido"), b); }
static void simple(const char *op, const char *rel, const char *extra) { share_t *s = &R.s[sel_sh]; char h[600], err[120]; u8 *r;
    if (local()) { char p[400], q[400]; if (resolve(s, rel, p, 400)) return; if (op[0] == 'B') z_unlink(p); else if (op[0] == 'C') z_mkdir(p); else { char r2[400]; const char *sl = strrchr(rel, '/'); snprintf(r2, 400, "%.*s%s%s", sl ? (int)(sl - rel) : 0, rel, sl ? "/" : "", extra); if (!resolve(s, r2, q, 400)) z_rename(p, q); } z_sync(); list_dir(); return; } snprintf(h, 600, "%s|%s|%s%s%s", op, s->id, rel, extra ? "|" : "", extra ? extra : ""); if (request(s->host, h, 0, 0, &r, err) < 0) { snprintf(msg, 300, "%s", err); return; } free(r); list_dir(); }
/* ---- mensajes: lista de conversaciones a la izquierda y el dialogo a la derecha ---- */
#define OLDC "*"   /* conversacion de solo lectura con los mensajes de la version anterior (RED-MENSAJES.TXT) */
typedef struct { char conv[700], last[20]; int n; } conv_t; static conv_t CV[64]; static int ncv;
static struct { char conv[700]; int seen; } SEEN[64]; static int nseen, seen_ready;
static char cur_conv[700]; static int newconv, chat_off; static u8 pick[MAXM];
static int seen_get(const char *c) { for (int i = 0; i < nseen; i++) if (!strcmp(SEEN[i].conv, c)) return SEEN[i].seen; return 0; }
static void seen_set(const char *c, int n) { for (int i = 0; i < nseen; i++) if (!strcmp(SEEN[i].conv, c)) { SEEN[i].seen = n; return; } if (nseen < 64) { strncpy0(SEEN[nseen].conv, c, 700); SEEN[nseen++].seen = n; } }
static void chats_load(void) { ncv = 0; char *t = z_readfile(CHATS, 0);
    for (char *p = t; p && *p; ) { char *e = strchr(p, '\n'); if (e) *e = 0; char *f[5]; if (chat_split(p, f)) { int k = -1; for (int i = 0; i < ncv; i++) if (!strcmp(CV[i].conv, f[0])) k = i;
            if (k < 0 && ncv < 64) { k = ncv++; strncpy0(CV[k].conv, f[0], 700); CV[k].n = 0; } if (k >= 0) { CV[k].n++; strncpy0(CV[k].last, f[1], 20); } } p = e ? e + 1 : 0; }
    free(t); for (int i = 1; i < ncv; i++) for (int j = i; j > 0 && strcmp(CV[j - 1].last, CV[j].last) < 0; j--) { conv_t x = CV[j]; CV[j] = CV[j - 1]; CV[j - 1] = x; }   /* la mas reciente arriba */
    char *o = z_readfile(INBOX, 0); if (o && *o && ncv < 64) { strcpy(CV[ncv].conv, OLDC); CV[ncv].last[0] = 0; CV[ncv].n = 0; ncv++; } free(o);
    if (!seen_ready) { seen_ready = 1; for (int i = 0; i < ncv; i++) seen_set(CV[i].conv, CV[i].n); } }
/* nombre de una conversacion: los demas equipos del grupo ("Todos" si son todos los de la red) */
static void conv_title(const char *conv, char *o, int cap) { o[0] = 0; if (!strcmp(conv, OLDC)) { strncpy0(o, tr_("Mensajes anteriores"), cap); return; }
    int n = 0, all = R.nm > 2; for (int i = 0; i < R.nm; i++) if (!conv_has(conv, R.m[i].id)) all = 0;
    if (all) { strncpy0(o, tr_("Todos"), cap); return; }
    for (const char *p = conv; *p; ) { char id[12]; int k = 0; while (*p && *p != ',' && k < 11) id[k++] = *p++; id[k] = 0; if (*p == ',') p++; if (!strcmp(id, myid)) continue;
        if (n++) strncat0(o, ", ", cap); strncat0(o, mname(id), cap); } }
static void draw(void);
static att_t PA[MAXADJ]; static int npa; static char prog_who[40];
static void att_add(void) { if (npa >= MAXADJ) { snprintf(msg, 300, tr_("Como mucho %d adjuntos por mensaje"), MAXADJ); return; } char p[260]; if (!z_pick_file(tr_("Adjuntar un archivo"), 0, p, 260)) return;
    strcpy(msg, tr_("Preparando el adjunto...")); draw(); int r = att_info(p, &PA[npa]); if (r == -2) { snprintf(msg, 300, tr_("%s es demasiado grande (como mucho %d MB)"), p, (int)(MAXAF >> 20)); return; } if (r) { snprintf(msg, 300, "%s %s", tr_("No se puede leer"), p); return; }
    long tot = 0; for (int i = 0; i <= npa; i++) tot += PA[i].size; if (tot > 4 * MAXAF) { strcpy(msg, tr_("Los adjuntos de un mensaje no pueden pasar de 256 MB")); return; }
    npa++; msg[0] = 0; char lg[300]; snprintf(lg, 300, "[prueba] red adjunto preparado %s (%ld)", PA[npa - 1].name, PA[npa - 1].size); z_log(lg); }
static void prog_draw(long done, long total, const char *name) { static u64 lt; static int lp = -1; int pc = total ? (int)(done * 100 / total) : 100; if (z_ms() - lt < 120 && pc < 100) return; lt = z_ms();
    int y = W->h - 22; g_rect(W, 0, y, W->w, 22, UI_PANEL); g_rect(W, 6, y + 3, W->w - 12, 16, UI_LINE); g_rect(W, 6, y + 3, (W->w - 12) * pc / 100, 16, 0x86EFAC);
    char b[300]; snprintf(b, 300, "%s %s %s %s: %d%%  (%ld / %ld KB)", tr_("Enviando"), name, tr_("a"), prog_who, pc, done / 1024, total / 1024); g_text(W, 12, y + 3, b, UI_TEXT); zw_present(W);
    if (pc / 25 != lp / 25 || done == 0) { lp = pc; char lg[120]; snprintf(lg, 120, "[prueba] red enviando adjuntos %d%%", pc); z_log(lg); } }
static void chat_send(void) { if (!compose[0] && !npa) return; char conv[700];
    if (newconv) { char ids[MAXM + 1][12]; int n = 0; strcpy(ids[n++], myid); for (int i = 0; i < R.nm; i++) if (pick[i] && strcmp(R.m[i].id, myid)) strcpy(ids[n++], R.m[i].id);
        if (n < 2) { strcpy(msg, tr_("Marca al menos un equipo para empezar la conversación.")); return; } conv_make(ids, n, conv, 700); }
    else if (cur_conv[0] && strcmp(cur_conv, OLDC)) strcpy(conv, cur_conv); else return;
    char t[420]; strcpy(t, compose); safe(t); u8 r8[8]; zc_random(r8, 8); char mid[20]; hex(r8, 8, mid); int ok = 0, tot = 0;
    for (const char *p = conv; *p; ) { char id[12]; int k = 0; while (*p && *p != ',' && k < 11) id[k++] = *p++; id[k] = 0; if (*p == ',') p++; if (strcmp(id, myid)) tot++; }
    { char st[40], all[2000]; snprintf(st, 40, "0/%d#%s", tot, mid); att_text(t, PA, npa, all, 2000); chat_store(conv, myid, all, st); }
    for (const char *p = conv; *p; ) { char id[12]; int k = 0; while (*p && *p != ',' && k < 11) id[k++] = *p++; id[k] = 0; if (*p == ',') p++; if (!strcmp(id, myid)) continue;
        conn_t c; char err[120] = ""; int r = -1; strncpy0(prog_who, mname(id), 40);
        if (!cconnect(&c, id, err)) { r = send_msg(&c, conv, mid, t, PA, npa, npa ? prog_draw : 0, err);
            if (r == 1) { char h[1200]; snprintf(h, sizeof h, "CHAT|%s|%s%s", conv, t, npa ? tr_(" (adjuntos no enviados: el otro equipo tiene una versión anterior)") : ""); u8 *q = 0; r = !csends(&c, h) && crecv(&c, &q, 15000) > 0 && !strncmp((char *)q, "OK", 2) ? 0 : -1; free(q); }
            z_close(c.s); }
        if (!r) ok++; else { out_add(id, conv, mid, t, PA, npa); char lg[200]; snprintf(lg, 200, "[prueba] red mensaje en cola para %s: %s", mname(id), err); z_log(lg); } }
    chat_st(mid, 0, ok, tot); strcpy(cur_conv, conv); newconv = 0; chat_off = 0; compose[0] = 0; ccur = 0; npa = 0;
    chats_load(); for (int i = 0; i < ncv; i++) if (!strcmp(CV[i].conv, conv)) seen_set(conv, CV[i].n);
    if (ok == tot) snprintf(msg, 300, tr_("Mensaje entregado a %d de %d"), ok, tot); else snprintf(msg, 300, tr_("Entregado a %d de %d; los demás lo recibirán en cuanto se puedan conectar"), ok, tot);
    char lg[80]; snprintf(lg, 80, "[prueba] red chat enviado %d/%d", ok, tot); z_log(lg); }
/* cuantos bytes de s caben en una linea de maxc caracteres (corta por un espacio si puede) */
static int wrap_n(const char *s, int maxc) { int i = 0, c = 0, sp = -1; while (s[i] && c < maxc) { if (s[i] == ' ') sp = i; u8 b = (u8)s[i]; i += b < 0x80 ? 1 : b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : 2; c++; }
    if (!s[i]) return i; if (sp > 0) return sp + 1; return i ? i : 1; }

/* zonas pulsables */
typedef struct { int x, y, w, h, id; } hz_t; static hz_t HZ[300]; static int nhz;
static void hz(int x, int y, int w, int h, int id) { if (nhz < 300) HZ[nhz++] = (hz_t){x, y, w, h, id}; }
static int bw(const char *s) { return g_textw(tr(s)) + 22; }
static void btn(int x, int y, const char *s, int st, int id) { ui_button(W, x, y, bw(s), 28, s, st); hz(x, y, bw(s), 28, id); }
enum { B_TAB = 1, B_FIELD = 50, B_CREATE = 10, B_RENAME, B_LEAVE, B_DELNET, B_ADDSH, B_UP, B_UPLOAD, B_MKDIR, B_REFRESH, B_SEND, B_CLOSEB, B_NEWC, B_ALL, B_INV = 100, B_DEL = 300, B_SH = 500, B_SHDEL = 700, B_ENT = 900, B_ADDP = 1400, B_PDEL = 1500, B_CONV = 1600, B_PICK = 1700, B_ATT = 1800, B_ATTDEL = 1900,
       B_INET = 23, B_ICHECK, B_IMAN, B_IRELE, B_IINV, B_ICOPY, B_IJOIN, B_ATTACH };
static void draw(void);
/* ---- Internet: estado, direcciones, rele e invitaciones ---- */
static char invcode[300];
static void inet_kick(void) { z_mkdir("/rd/tmp"); z_writefile("/rd/tmp/red-comprobar", "1", 1); }   /* el servicio comprueba el router y relee los ajustes */
static void ist_get(const char *k, char *o, int cap) { static char *t; static u64 tt; if (!t || z_ms() - tt > 1500) { free(t); t = z_readfile(ISTAT, 0); tt = z_ms(); } kvs(t ? t : "", k, o, cap); }
static void make_invite(void) { if (!is_admin()) return; if (!inet_on) { strcpy(msg, tr_("Primero activa el acceso por Internet.")); return; } ab_load(); addr_t *me = ab_get(myid); char a[MAXA] = ""; if (me) strncpy0(a, me->a, MAXA);
    if (!a[0] && !R.rele[0]) { strcpy(msg, tr_("Este equipo todavía no tiene dirección en Internet: abre el puerto, escribe una dirección a mano o pon un relé.")); return; }
    inv_t I[8]; int n = inv_list(I, 8); if (n == 8) { memmove(I, I + 1, 7 * sizeof I[0]); n = 7; } zc_random(I[n].s, 10); I[n].exp = now_min() + 2 * 1440; char k[20]; b32(I[n].s, 10, k); n++; inv_write(I, n);
    snprintf(invcode, 300, "ZRED1-%s@%s%s%s%s", k, a, a[0] && R.rele[0] ? "," : "", R.rele[0] ? "rele:" : "", R.rele); z_clip_set(invcode, strlen(invcode));
    strcpy(msg, tr_("Código copiado: dáselo a la otra casa (por teléfono, correo...). Sirve una vez y caduca en 2 días.")); char lg[340]; snprintf(lg, 340, "[prueba] red codigo de invitacion %s", invcode); z_log(lg); }
static void join_code(void) { char code[300] = ""; msg[0] = 0; if (!ui_prompt("Unirse a una red por Internet", "Código de invitación que te ha dado el administrador:", code, 290) || !code[0]) return;
    char *c0 = code; while (*c0 == ' ') c0++; char *at = strchr(c0, '@'); u8 S[10]; if (strncasecmp(c0, "ZRED1-", 6) || !at) { strcpy(msg, tr_("Ese código no es válido.")); return; } *at = 0; if (unb32(c0 + 6, S, 10)) { strcpy(msg, tr_("Ese código no es válido.")); return; }
    char addrs[260]; strncpy0(addrs, at + 1, 260); for (int l = strlen(addrs); l && addrs[l - 1] == ' '; ) addrs[--l] = 0;
    if (!inet_on) { inet_on = 1; ident_save(); inet_kick(); }   /* quien se une por Internet quiere el acceso por Internet */
    strcpy(msg, tr_("Conectando con el administrador...")); draw(); conn_t c; memset(&c, 0, sizeof c); c.s = -1; char via[MAXA] = "", direct[MAXA] = "";
    for (const char *q = addrs; *q && c.s < 0; ) { char one[200]; int k = 0; while (*q && *q != ',' && k < 199) one[k++] = *q++; one[k] = 0; if (*q == ',') q++; if (!one[0]) continue;
        if (!strncasecmp(one, "rele:", 5)) { char old[100]; strcpy(old, R.rele); strncpy0(R.rele, one + 5, 100); char box[40]; inv_box(S, box); if (rele_call(&c, box)) c.s = -1; else strcpy(via, "r"); strcpy(R.rele, old); }
        else { addr_clean(one); if (strlen(direct) + strlen(one) + 2 < MAXA) { if (direct[0]) strcat(direct, ","); strcat(direct, one); } memset(&c, 0, sizeof c); c.s = dial(one, PTCP, 6000); if (c.s >= 0) snprintf(via, MAXA, "d:%s", one); } }
    if (c.s < 0) { strcpy(msg, tr_("No se pudo conectar con el administrador: ¿está encendido y con el acceso por Internet activado?")); z_log("[prueba] red union: sin conexion"); return; }
    u8 priv[32], pub[32], apub[32], sh[32], mac[32], am[32], want[32]; zc_random(priv, 32); x25519_base(pub, priv); char nm[32], ph[65], mh[65], tag[20], l[400]; strcpy(nm, myname); safe(nm); inv_mac(S, 'b', pub, 0, myid, nm, mac); inv_tag(S, tag); hex(pub, 32, ph); hex(mac, 32, mh);
    snprintf(l, 400, "ZRED1|UNIRSE|%s|%s|%s|%s|%s", tag, ph, myid, nm, mh); lsend(&c, l); c.dl = z_ms() + 15000; char *f[7];
    if (line(&c, l, 400, 15000) < 0 || split(l, f, 7) < 6 || strcmp(f[0], "PUB") || unhex(f[1], apub, 32) || unhex(f[5], am, 32)) { z_close(c.s); strcpy(msg, tr_("El administrador no reconoce el código: puede que haya caducado o que ya se haya usado.")); z_log("[prueba] red union: codigo no aceptado"); return; }
    char aid[12], aname[32], anet[40]; strncpy0(aid, f[2], 12); strncpy0(aname, f[3], 32); strncpy0(anet, f[4], 40); inv_mac(S, 'a', apub, pub, aname, anet, want);
    if (!ct_eq(am, want, 32)) { z_close(c.s); strcpy(msg, tr_("La respuesta no viene del administrador: alguien se está haciendo pasar por él.")); z_log("[prueba] red union: respuesta falsa"); return; }
    x25519(sh, priv, apub); char six[12]; sas(sh, apub, pub, six); c.dl = 0; { char lg[80]; snprintf(lg, 80, "[prueba] red union: codigo %s", six); z_log(lg); }
    char b1[200], wn[200] = ""; snprintf(b1, 200, tr_("%s te invita a la red \"%s\" por Internet."), aname, anet); if (in_net()) snprintf(wn, 200, tr_("Este equipo dejará la red \"%s\"."), R.netname);
    if (!ask_code("Invitación por Internet", b1, tr_("Comprueba que el administrador ve el mismo código:"), tr_("Si es el mismo, acepta para unir este equipo a la red."), six, wn)) { lsend(&c, "RECHAZO"); z_close(c.s); strcpy(msg, tr_("Invitación rechazada")); return; }
    lsend(&c, "ACEPTO"); strcpy(msg, tr_("Esperando a que el administrador acepte...")); draw();
    u8 h[4]; long n = 0; if (raw_recv(&c, h, 4, 130000) || (n = h[0] | h[1] << 8 | h[2] << 16 | (long)h[3] << 24) <= 0 || n > 200000) { z_close(c.s); strcpy(msg, tr_("El administrador no aceptó la invitación")); z_log("[prueba] red union: no llego la configuracion"); return; }
    u8 *b = malloc(n + 16), *pl = malloc(n + 1); u8 kk[16], iv[12] = {0}; inv_key(S, sh, kk); zgcm g; gcm_init(&g, kk, 16);
    if (raw_recv(&c, b, n + 16, 15000) || gcm_open(&g, iv, h, 4, b, n, pl, b + n)) { free(b); free(pl); z_close(c.s); strcpy(msg, tr_("no se completó la invitación")); return; } pl[n] = 0; free(b);
    if (cfg_parse((char *)pl)) { free(pl); z_close(c.s); cfg_load(); strcpy(msg, tr_("no se completó la invitación")); return; } free(pl); cfg_save(); mk_shares(); lsend(&c, "LISTO"); z_close(c.s);
    ab_load(); ab_merge(aid, 0, direct, 0); ab_save(); peer_t *p = peer_mk(aid); if (p) { p->iseen = z_ms(); strncpy0(p->via, via, 80); } inet_kick();
    snprintf(msg, 300, tr_("Este equipo ya forma parte de la red \"%s\"."), R.netname); char lg[120]; snprintf(lg, 120, "[prueba] red unido a %s como %s (por internet)", R.net, myid); z_log(lg); tab = 4; }
/* texto con saltos de linea a lo ancho de w; devuelve la y siguiente */
static int wtext(int x, int y, int w, const char *s, u32 col) { int maxc = MAX(10, w / 8); while (*s) { int k = wrap_n(s, maxc); char seg[400]; int kk = MIN(k, 399); memcpy(seg, s, kk); seg[kk] = 0; g_text(W, x, y, seg, col); y += 18; s += k; } return y; }
static void draw_inet(int X, int y, int w) { char b[400], lg[600]; int lk = 0, sx, sy; lg[0] = 0;
#define POS(nm, xx, yy) do { zw_screen(W, (xx), (yy), &sx, &sy); lk += snprintf(lg + lk, 600 - lk, " (%s en %d,%d)", nm, sx, sy); } while (0)
    lk += snprintf(lg, 600, "[prueba] red internet"); g_textb(W, X, y, tr_("Conexión por Internet"), UI_TEXT); y += 24;
    y = wtext(X, y, w, tr_("Une equipos de casas distintas como si estuvieran en la misma red: carpetas, impresoras y mensajes, cifrados de extremo a extremo."), UI_MUTED) + 8;
    const char *tb = inet_on ? "Desactivar el acceso por Internet" : "Activar el acceso por Internet"; btn(X, y, tb, inet_on ? 0 : 2, B_INET); POS("activar", X + 20, y + 14);
    g_circle(W, X + bw(tb) + 20, y + 14, 5, inet_on ? 0x16A34A : 0x9CA3AF, 1); g_text(W, X + bw(tb) + 32, y + 6, inet_on ? tr_("Activado") : tr_("Desactivado"), UI_TEXT); y += 40;
    if (inet_on) { char st[16], how[16], ext[40], pub[20], dirs[MAXA], rl[4]; ist_get("estado", st, 16); ist_get("metodo", how, 16); ist_get("externa", ext, 40); ist_get("publica", pub, 20); ist_get("dirs", dirs, MAXA); ist_get("rele", rl, 4);
        if (!strcmp(st, "ok")) snprintf(b, 400, tr_("Puerto abierto en el router con %s: %s"), how, ext);
        else if (!strcmp(st, "cgnat")) strcpy(b, tr_("El router no tiene una IP pública (CGNAT, típico del móvil y de algunas compañías): hace falta un relé para que te llamen."));
        else if (!strcmp(st, "doble")) strcpy(b, tr_("Hay dos routers seguidos (doble NAT): abre el puerto a mano en los dos o usa un relé."));
        else if (!strcmp(st, "cerrado")) strcpy(b, tr_("El router no deja abrir el puerto solo (UPnP, NAT-PMP y PCP apagados): ábrelo a mano (TCP 7141) o usa un relé."));
        else if (!strcmp(st, "manual")) strcpy(b, tr_("Se usan las direcciones escritas a mano."));
        else if (!strcmp(st, "sinrouter")) strcpy(b, tr_("Este equipo no sale a Internet por un router (cable directo)."));
        else if (!strcmp(st, "off") && !in_net()) strcpy(b, tr_("Se abrirá el puerto cuando este equipo esté en una red."));
        else strcpy(b, tr_("Comprobando el router..."));
        y = wtext(X, y, w, b, UI_TEXT) + 4; snprintf(b, 400, "%s: %s", tr_("Direcciones de este equipo en Internet"), dirs[0] ? dirs : tr_("ninguna")); y = wtext(X, y, w, b, UI_MUTED);
        if (pub[0]) { snprintf(b, 400, "%s: %s", tr_("IP pública"), pub); g_text(W, X, y, b, UI_MUTED); y += 18; }
        char rh[100]; strncpy0(rh, R.rele, 100); char *sl = strchr(rh, '/'); if (sl) *sl = 0; snprintf(b, 400, "%s: %s%s", tr_("Relé de la red"), rh[0] ? rh : tr_("ninguno"), !rh[0] ? "" : rl[0] == '1' ? tr_(" (conectado)") : tr_(" (sin conexión)")); g_text(W, X, y, b, UI_MUTED); y += 26;
        int bx = X; btn(bx, y, "Comprobar ahora", 0, B_ICHECK); POS("comprobar", bx + 20, y + 14); bx += bw("Comprobar ahora") + 6; btn(bx, y, "Direcciones a mano...", 0, B_IMAN); POS("manual", bx + 20, y + 14); bx += bw("Direcciones a mano...") + 6;
        if (is_admin()) { btn(bx, y, "Relé de la red...", 0, B_IRELE); POS("rele", bx + 20, y + 14); } y += 42; }
    if (is_admin()) { g_textb(W, X, y, tr_("Invitar a un equipo de otra casa"), UI_TEXT); y += 24; btn(X, y, "Crear un código de invitación", 2, B_IINV); POS("invitar", X + 20, y + 14);
        if (invcode[0]) { btn(X + bw("Crear un código de invitación") + 6, y, "Copiar", 0, B_ICOPY); y += 36; g_text(W, X, y, tr_("Código (un solo uso, caduca en 2 días):"), UI_MUTED); y += 20; y = wtext(X, y, w, invcode, UI_ACC); y += 6; } else y += 40; }
    else { g_textb(W, X, y, tr_("Unirse a una red de otra casa"), UI_TEXT); y += 24; btn(X, y, "Unirse con un código de invitación...", 2, B_IJOIN); POS("unirse", X + 20, y + 14); y += 42; }
    if (in_net()) { g_textb(W, X, y, tr_("Cómo se llega a cada equipo"), UI_TEXT); y += 24;
        for (int i = 0; i < R.nm && y < W->h - 40; i++) { if (!strcmp(R.m[i].id, myid)) continue; peer_t *p = peer(R.m[i].id); int on = online(R.m[i].id); g_circle(W, X + 6, y + 9, 5, on ? 0x16A34A : 0x9CA3AF, 1); char r[200];
            if (lan_on(R.m[i].id)) { char ip[20]; z_fmt_ip(p->ip, ip); snprintf(r, 200, "%s (%s)", tr_("por la red local"), ip); }
            else if (inet_seen(R.m[i].id)) { if (p->via[0] == 'r') strcpy(r, tr_("por Internet, a través del relé")); else if (p->via[0] == 'd') snprintf(r, 200, "%s (%s)", tr_("por Internet"), p->via + 2); else strcpy(r, tr_("conectado por Internet; recoge sus mensajes al conectar")); }
            else strcpy(r, tr_("sin conexión")); snprintf(b, 400, "%s   %s", R.m[i].name, r); g_text(W, X + 18, y + 2, b, UI_TEXT); y += 24; } }
    static char last[600]; if (strcmp(lg, last)) { strcpy(last, lg); z_log(lg); }
#undef POS
}
/* pestana Mensajes */
static char ATP[64][200]; static int natp;   /* rutas de los adjuntos que se ven en pantalla */
static void draw_chat(int X, int y, int w) { char b[300]; chats_load(); int H = W->h - 22, LW = 250, cx = X + LW + 16, cw = w - LW - 16; char lg[600]; int lk = 0, sx, sy;
    btn(X, y, "Nueva conversación", newconv ? 2 : 0, B_NEWC); zw_screen(W, X + 20, y + 14, &sx, &sy); lk += snprintf(lg + lk, 600 - lk, "[prueba] red chat (nueva en %d,%d)", sx, sy); y += 40;
    g_rect(W, X + LW + 6, 50, 1, H - 56, UI_LINE);
    if (!ncv) g_text(W, X, y, tr_("Ninguna todavía."), UI_MUTED);
    for (int i = 0; i < ncv && y < H - 50; i++) { conv_t *c = &CV[i]; int on = !newconv && !strcmp(c->conv, cur_conv), unread = c->n - seen_get(c->conv); if (on) { seen_set(c->conv, c->n); unread = 0; }
        g_rect(W, X, y, LW, 44, on ? 0xDBEAFE : i % 2 ? UI_WHITE : UI_BG); char t[200]; conv_title(c->conv, t, 200); char t2[32]; snprintf(t2, 32, "%.*s", LW / 8 - 2, t); if (unread > 0) g_textb(W, X + 8, y + 5, t2, UI_TEXT); else g_text(W, X + 8, y + 5, t2, UI_TEXT);
        if (c->last[0]) { snprintf(b, 300, "%.2s/%.2s %s", c->last + 8, c->last + 5, c->last + 11); g_text(W, X + 8, y + 24, b, UI_MUTED); }
        if (unread > 0) { snprintf(b, 300, "%d", unread); g_circle(W, X + LW - 18, y + 22, 10, 0x2563EB, 1); g_text(W, X + LW - 18 - g_textw(b) / 2, y + 14, b, UI_WHITE); }
        hz(X, y, LW, 44, B_CONV + i); if (i == 0) { zw_screen(W, X + 40, y + 22, &sx, &sy); lk += snprintf(lg + lk, 600 - lk, " (conv0 en %d,%d)", sx, sy); } y += 46; }
    int fy = H - 40, top = 56;   /* campo de escribir abajo; arriba, la cabecera */
    if (newconv) { g_textb(W, cx, top, tr_("Nueva conversación: marca con quién quieres hablar"), UI_TEXT); int yy = top + 30, n = 0;
        for (int i = 0; i < R.nm && yy < fy - 40; i++) { if (!strcmp(R.m[i].id, myid)) continue; g_frame(W, cx, yy + 2, 14, 14, UI_TEXT); if (pick[i]) g_rect(W, cx + 3, yy + 5, 8, 8, UI_ACC);
            g_circle(W, cx + 30, yy + 9, 5, online(R.m[i].id) ? 0x16A34A : 0x9CA3AF, 1); g_text(W, cx + 42, yy + 2, R.m[i].name, UI_TEXT); hz(cx, yy, cw, 22, B_PICK + i);
            if (!n++) { zw_screen(W, cx + 7, yy + 9, &sx, &sy); lk += snprintf(lg + lk, 600 - lk, " (equipo0 en %d,%d)", sx, sy); } yy += 28; }
        if (!n) g_text(W, cx, yy, tr_("No hay más equipos en la red."), UI_MUTED); else if (n > 1) { btn(cx, yy + 6, "Todos", 0, B_ALL); zw_screen(W, cx + 20, yy + 20, &sx, &sy); lk += snprintf(lg + lk, 600 - lk, " (todos en %d,%d)", sx, sy); }
        if (!npa) g_text(W, cx, fy - 24, tr_("El primer mensaje decide el grupo: las respuestas llegan a todos ellos."), UI_MUTED); }
    else if (!cur_conv[0]) g_text(W, cx, top, tr_("Elige una conversación o empieza una nueva."), UI_MUTED);
    else if (!strcmp(cur_conv, OLDC)) { g_textb(W, cx, top, tr_("Mensajes anteriores"), UI_TEXT); int yy = top + 28; char *t = z_readfile(INBOX, 0);
        for (char *p = t; p && *p && yy < H - 30; ) { char *e = strchr(p, '\n'); if (e) *e = 0; char *f[3]; char l[700]; strncpy0(l, p, 700); if (split(l, f, 3) == 3) { snprintf(b, 300, "%s  %s:", f[0], f[1]); g_textb(W, cx, yy, b, UI_TEXT); yy += 18; g_text(W, cx + 16, yy, f[2], UI_TEXT); yy += 26; } p = e ? e + 1 : 0; } free(t); }
    else { char t[200]; conv_title(cur_conv, t, 200); g_textb(W, cx, top, t, UI_TEXT);
        /* los mensajes de la conversacion, de abajo arriba: cada burbuja con sus lineas */
        char *all = z_readfile(CHATS, 0); char *L[400]; int nl = 0; for (char *p = all; p && *p; ) { char *e = strchr(p, '\n'); if (e) *e = 0; int pl = strlen(cur_conv); if (!strncmp(p, cur_conv, pl) && p[pl] == '|' && nl < 400) L[nl++] = p; p = e ? e + 1 : 0; }
        int maxc = (cw * 2 / 3 - 20) / 8; if (maxc < 10) maxc = 10; int fb = npa ? fy - 30 : fy, yb = fb - 12 + chat_off, area0 = top + 26; zw_clip(W, cx, area0, cw, fb - 8 - area0); natp = 0;
        for (int i = nl - 1; i >= 0 && yb > area0; i--) { char l[3200]; strncpy0(l, L[i], 3200); char *f[5]; if (!chat_split(l, f)) continue; int mine = !strcmp(f[2], myid), lines = 0, mw = 0;
            char *ap = strchr(f[4], '\x1F'); if (ap) *ap++ = 0; char an[MAXADJ][140], apath[MAXADJ][200]; int nat = 0;   /* adjuntos: nombre\x1Etamano\x1Eruta separados por \x1F */
            for (char *q = ap; q && *q && nat < MAXADJ; ) { char *e = strchr(q, '\x1F'); if (e) *e = 0; char *g1 = strchr(q, '\x1E'), *g2 = g1 ? strchr(g1 + 1, '\x1E') : 0; if (g1 && g2) { *g1 = 0; *g2 = 0; long sz = atol(g1 + 1); snprintf(an[nat], 140, "> %s (%ld KB)", q, (sz + 1023) / 1024); strncpy0(apath[nat], g2 + 1, 200); nat++; } q = e ? e + 1 : 0; }
            for (const char *q = f[4]; *q; ) { int k = wrap_n(q, maxc); int cc = 0; for (int j = 0; j < k; j++) if (((u8)q[j] & 0xC0) != 0x80) cc++; if (cc > mw) mw = cc; q += k; lines++; } if (!lines && !nat) lines = 1;
            for (int j = 0; j < nat; j++) mw = MAX(mw, (int)strlen(an[j]));
            char st[40]; strncpy0(st, f[3], 40); char *hs = strchr(st, '#'); if (hs) *hs = 0; int pend = mine && hs && atoi(st) < atoi(strchr(st, '/') ? strchr(st, '/') + 1 : "0");
            char hd[160]; if (mine) snprintf(hd, 160, "%.2s/%.2s %s  %s %s%s", f[1] + 8, f[1] + 5, f[1] + 11, tr_("entregado"), st, pend ? tr_(" · el resto, pendiente") : ""); else snprintf(hd, 160, "%s  %.2s/%.2s %s", mname(f[2]), f[1] + 8, f[1] + 5, f[1] + 11);
            int bwid = MIN(cw - 8, MAX(mw * 8, g_textw(hd)) + 20), bh = (lines + nat) * 18 + 30; yb -= bh; int bx = mine ? cx + cw - bwid - 4 : cx + 4;
            g_round(W, bx, yb, bwid, bh - 6, 8, mine ? 0xDCFCE7 : 0xF3F4F6); g_text(W, bx + 10, yb + 4, hd, UI_MUTED); int ly = yb + 22;
            for (const char *q = f[4]; *q; ) { int k = wrap_n(q, maxc); char seg[400]; int kk = MIN(k, 399); memcpy(seg, q, kk); seg[kk] = 0; g_text(W, bx + 10, ly, seg, UI_TEXT); ly += 18; q += k; }
            for (int j = 0; j < nat; j++) { g_text(W, bx + 10, ly, an[j], UI_ACC); if (ly >= area0 && natp < 64) { strncpy0(ATP[natp], apath[j], 200); hz(bx, ly - 2, bwid, 20, B_ATT + natp); if (!natp) { zw_screen(W, bx + 30, ly + 8, &sx, &sy); lk += snprintf(lg + lk, 600 - lk, " (adjunto0 en %d,%d)", sx, sy); } natp++; } ly += 18; } }
        zw_noclip(W); if (chat_off > 0 && yb > area0) chat_off = MAX(0, chat_off - (yb - area0)); free(all); }
    if (newconv || (cur_conv[0] && strcmp(cur_conv, OLDC))) { int ab = bw("Adjuntar..."), fw = cw - bw("Enviar") - ab - 16; btn(cx, fy, "Adjuntar...", 0, B_ATTACH); zw_screen(W, cx + 20, fy + 14, &sx, &sy); lk += snprintf(lg + lk, 600 - lk, " (adjuntar en %d,%d)", sx, sy);
        ui_input(W, cx + ab + 6, fy, fw, 28, compose, ccur, cfoc); hz(cx + ab + 6, fy, fw, 28, B_FIELD); btn(cx + ab + fw + 16, fy, "Enviar", 2, B_SEND);
        zw_screen(W, cx + ab + 26, fy + 14, &sx, &sy); lk += snprintf(lg + lk, 600 - lk, " (campo en %d,%d)", sx, sy);
        if (npa) { int ax = cx; g_rect(W, cx, fy - 30, cw, 26, UI_BG); for (int i = 0; i < npa; i++) { char t[140]; snprintf(t, 140, "%.40s (%ld KB)  x", PA[i].name, (PA[i].size + 1023) / 1024); int tw = g_textw(t) + 16; if (ax + tw > cx + cw) break;
                g_round(W, ax, fy - 28, tw, 22, 6, 0xDBEAFE); g_text(W, ax + 8, fy - 25, t, UI_TEXT); hz(ax + tw - 20, fy - 28, 20, 22, B_ATTDEL + i); ax += tw + 6; } } }
    static char lastlg[600]; if (strcmp(lg, lastlg)) { strcpy(lastlg, lg); z_log(lg); } }
static void draw(void) { nhz = 0; g_clear(W, UI_WHITE); g_rect(W, 0, 0, W->w, 40, UI_PANEL); static const char *T[] = {"Equipos", "Carpetas", "Mensajes", "Impresoras", "Internet"}; int x = 10;
    if (in_net()) chats_load(); int unread = 0; for (int i = 0; i < ncv; i++) if (tab != 2 || newconv || strcmp(CV[i].conv, cur_conv)) unread += MAX(0, CV[i].n - seen_get(CV[i].conv));
    for (int i = 0; i < 5; i++) { ui_button(W, x, 6, bw(T[i]), 28, T[i], tab == i ? 2 : 0); hz(x, 6, bw(T[i]), 28, B_TAB + i);
        if (i == 4) { static int tl; if (!tl) { tl = 1; int sx, sy; zw_screen(W, x + bw(T[i]) / 2, 20, &sx, &sy); char lg[80]; snprintf(lg, 80, "[prueba] red pestanas (internet en %d,%d)", sx, sy); z_log(lg); } }
        if (i == 2 && unread > 0) { char u[12]; snprintf(u, 12, "%d", unread); g_circle(W, x + bw(T[i]) - 2, 10, 8, 0xDC2626, 1); g_text(W, x + bw(T[i]) - 2 - g_textw(u) / 2, 2, u, UI_WHITE); } x += bw(T[i]) + 6; }
    char b[300]; int y = 56, X = 20, w = W->w - 40; zs_area(W, 0, 40, W->w, W->h - 62, 0);
    if (tab == 0) { g_text2(W, X, y, in_net() ? R.netname : tr_("Sin red local"), UI_TEXT, 2); y += 40;
        snprintf(b, 300, "%s: %s   (%s %s)", tr_("Este equipo"), myname, tr_("id"), myid); g_text(W, X, y, b, UI_MUTED); if (!in_net()) btn(X + w - bw("Cambiar nombre"), y - 6, "Cambiar nombre", 0, B_RENAME); y += 26;
        znet ni; char ip[20] = "-"; if (!z_netinfo(&ni) && ni.ip) z_fmt_ip(ni.ip, ip); snprintf(b, 300, "%s: %s%s", tr_("Dirección"), ip, !z_netinfo(&ni) && ni.local ? tr_(" (cable directo, sin router)") : ""); g_text(W, X, y, b, UI_MUTED); y += 30;
        if (!in_net()) { g_text(W, X, y, tr_("Crea una red para ser su administrador, o espera a que el administrador de una red invite a este equipo."), UI_TEXT); y += 30; btn(X, y, "Crear una red nueva", 2, B_CREATE); y += 46; }
        else { snprintf(b, 300, "%s: %s%s", tr_("Administrador"), mname(R.admin), is_admin() ? tr_(" (este equipo)") : online(R.admin) ? tr_(" (en línea)") : tr_(" (no está en línea)")); g_text(W, X, y, b, UI_TEXT); y += 30;
            g_textb(W, X, y, tr_("Equipos de la red"), UI_TEXT); y += 24; for (int i = 0; i < R.nm; i++) { int on = online(R.m[i].id); g_circle(W, X + 6, y + 9, 5, on ? 0x16A34A : 0x9CA3AF, 1); peer_t *p = peer(R.m[i].id); char ip2[80] = ""; int lan = lan_on(R.m[i].id); if (p && lan) z_fmt_ip(p->ip, ip2); else if (p && on && p->via[0] == 'd') strncpy0(ip2, p->via + 2, 80); else if (p && on && p->via[0] == 'r') strcpy(ip2, tr_("por el relé"));
                snprintf(b, 300, "%s%s   %s  %s", R.m[i].name, !strcmp(R.m[i].id, myid) ? tr_(" (este equipo)") : "", on ? (lan || !strcmp(R.m[i].id, myid) ? tr_("en línea") : tr_("en línea por Internet")) : tr_("apagado o sin conexión"), ip2); g_text(W, X + 18, y + 2, b, UI_TEXT);
                if (is_admin() && i > 0) btn(X + w - bw("Sacar de la red"), y - 4, "Sacar de la red", 0, B_DEL + i); y += 30; }
            y += 6; if (is_admin()) btn(X, y, "Borrar la red", 0, B_DELNET); else btn(X, y, "Salir de la red", 0, B_LEAVE); y += 44; }
        int nf = 0; for (int i = 0; i < np; i++) if (z_ms() - P[i].seen < 45000 && (!in_net() || strcmp(P[i].net, R.net))) nf++;
        g_textb(W, X, y, is_admin() ? tr_("Equipos encontrados que no están en la red") : tr_("Otros equipos encontrados"), UI_TEXT); y += 24; if (!nf) { g_text(W, X, y, tr_("Ninguno todavía. Comprueba el cable y que el otro equipo tenga ZunzúnOS encendido."), UI_MUTED); y += 24; }
        for (int i = 0; i < np; i++) { peer_t *p = &P[i]; if (z_ms() - p->seen >= 45000 || (in_net() && !strcmp(p->net, R.net))) continue; char ip2[20]; z_fmt_ip(p->ip, ip2);
            if (p->role == 'L') snprintf(b, 300, "%s   %s   %s", p->name, ip2, tr_("sin red")); else snprintf(b, 300, "%s   %s   %s \"%s\"", p->name, ip2, tr_("en la red"), p->netname); g_text(W, X + 18, y + 2, b, UI_TEXT);
            if (is_admin()) btn(X + w - bw("Añadir a la red"), y - 4, "Añadir a la red", 2, B_INV + i); y += 30; } }
    else if (tab == 3) { if (!in_net()) g_text(W, X, y, tr_("Primero une este equipo a una red local (pestaña Equipos)."), UI_MUTED);
        else { g_textb(W, X, y, tr_("Impresoras compartidas de la red"), UI_TEXT); if (is_admin()) btn(X + w - bw("Compartir una impresora"), y - 6, "Compartir una impresora", 2, B_ADDP); y += 30;
            g_text(W, X, y, tr_("Aparecen en el diálogo Imprimir de todos los equipos de la red."), UI_MUTED); y += 18; g_text(W, X, y, tr_("El equipo que tiene la impresora tiene que estar encendido."), UI_MUTED); y += 30;
            if (!R.np) g_text(W, X, y, is_admin() ? tr_("Todavía no hay impresoras. Pulsa Compartir una impresora.") : tr_("El administrador todavía no ha compartido impresoras."), UI_MUTED);
            for (int i = 0; i < R.np; i++) { rprn_t *q = &R.p[i]; int on = online(q->host); g_rect(W, X, y, w, 40, i % 2 ? UI_WHITE : UI_BG); g_circle(W, X + 12, y + 20, 5, on ? 0x16A34A : 0x9CA3AF, 1); g_textb(W, X + 26, y + 4, q->name, UI_TEXT);
                snprintf(b, 300, "%s %s · %s · %s", tr_("conectada a"), mname(q->host), q->model[0] ? q->model : q->pname, q->lang); g_text(W, X + 26, y + 22, b, UI_MUTED); if (is_admin()) btn(X + w - bw("Dejar de compartir"), y + 6, "Dejar de compartir", 0, B_PDEL + i); y += 44; } } }
    else if (tab == 1) { if (!in_net()) g_text(W, X, y, tr_("Primero une este equipo a una red local (pestaña Equipos)."), UI_MUTED);
        else if (sel_sh < 0) { g_textb(W, X, y, tr_("Carpetas compartidas de la red"), UI_TEXT); if (is_admin()) btn(X + w - bw("Compartir una carpeta"), y - 6, "Compartir una carpeta", 2, B_ADDSH); y += 30;
            if (!R.ns) { g_text(W, X, y, is_admin() ? tr_("Todavía no hay carpetas. Pulsa Compartir una carpeta.") : tr_("El administrador todavía no ha compartido carpetas."), UI_MUTED); }
            for (int i = 0; i < R.ns; i++) { share_t *s = &R.s[i]; int on = online(s->host); g_rect(W, X, y, w, 40, i % 2 ? UI_WHITE : UI_BG); g_circle(W, X + 12, y + 20, 5, on ? 0x16A34A : 0x9CA3AF, 1); g_textb(W, X + 26, y + 4, s->name, UI_TEXT);
                snprintf(b, 300, "%s %s · %s · %s", tr_("en"), mname(s->host), tr_(s->rw ? "lectura y escritura" : "solo lectura"), s->path); g_text(W, X + 26, y + 22, b, UI_MUTED); hz(X, y, w - 140, 40, B_SH + i); if (is_admin()) btn(X + w - bw("Dejar de compartir"), y + 6, "Dejar de compartir", 0, B_SHDEL + i); y += 44; } }
        else { share_t *s = &R.s[sel_sh]; snprintf(b, 300, "%s / %s", s->name, cwd); g_textb(W, X, y, b, UI_TEXT); y += 26; int bx = X; btn(bx, y, "Cerrar", 0, B_CLOSEB); bx += bw("Cerrar") + 6; btn(bx, y, "Subir de nivel", 0, B_UP); bx += bw("Subir de nivel") + 6; btn(bx, y, "Actualizar", 0, B_REFRESH); bx += bw("Actualizar") + 6;
            if (s->rw) { btn(bx, y, "Subir un archivo...", 0, B_UPLOAD); bx += bw("Subir un archivo...") + 6; btn(bx, y, "Nueva carpeta", 0, B_MKDIR); } y += 38; int rows = (W->h - 22 - y) / 24; etop = MAX(0, MIN(etop, nent - rows));
            for (int i = 0; i < rows && etop + i < nent; i++) { ent_t *e = &E[etop + i]; char t[260]; if (e->d == 'D') snprintf(t, 260, "[%s]  %s", tr_("carpeta"), e->name); else snprintf(t, 260, "%s   (%lu KB)", e->name, (unsigned long)((e->size + 1023) / 1024)); g_text(W, X + 6, y + 4, t, e->d == 'D' ? UI_ACC : UI_TEXT); hz(X, y, w, 24, B_ENT + etop + i); y += 24; }
            if (!nent) g_text(W, X, y, tr_("Carpeta vacía."), UI_MUTED); } }
    else if (tab == 4) draw_inet(X, y, w);
    else { if (!in_net()) g_text(W, X, y, tr_("Primero une este equipo a una red local (pestaña Equipos)."), UI_MUTED); else draw_chat(X, y - 6, w); }
    zs_stop(W); zs_show(W); g_rect(W, 0, W->h - 22, W->w, 22, UI_PANEL); g_text(W, 8, W->h - 19, msg, UI_TEXT); zw_present(W); }
static void click(zevent *e) { for (int i = nhz - 1; i >= 0; i--) { hz_t *h = &HZ[i]; if (e->x < h->x || e->y < h->y || e->x >= h->x + h->w || e->y >= h->y + h->h) continue; int id = h->id; msg[0] = 0;
        if (id >= B_ATTDEL && id < B_ATTDEL + MAXADJ) { int k = id - B_ATTDEL; if (k < npa) { memmove(&PA[k], &PA[k + 1], (npa - k - 1) * sizeof PA[0]); npa--; } }
        else if (id >= B_ATT && id < B_ATT + 64) { int k = id - B_ATT; if (k < natp) { const char *bn = strrchr(ATP[k], '/'); if (z_open_path(ATP[k]) < 0) snprintf(msg, 300, "%s %s", tr_("No se encuentra"), ATP[k]); else snprintf(msg, 300, "%s %s", tr_("Abierto"), bn ? bn + 1 : ATP[k]); char lg[260]; snprintf(lg, 260, "[prueba] red adjunto abierto %s", ATP[k]); z_log(lg); } }
        else if (id >= B_TAB && id < B_TAB + 5) { tab = id - B_TAB; cfoc = tab == 2; }
        else if (id == B_ATTACH) att_add();
        else if (id == B_INET) { inet_on = !inet_on; ident_save(); inet_kick(); strcpy(msg, inet_on ? tr_("Acceso por Internet activado: comprobando el router...") : tr_("Acceso por Internet desactivado: solo la red local.")); z_log(inet_on ? "[prueba] red internet activado" : "[prueba] red internet desactivado"); }
        else if (id == B_ICHECK) { inet_kick(); strcpy(msg, tr_("Comprobando el router...")); }
        else if (id == B_IMAN) { char m2[200]; strcpy(m2, manual); if (ui_prompt("Direcciones a mano", "Nombres o IP:puerto de este equipo en Internet, separados por comas (*:7141 = la IP pública que se vea):", m2, 190)) { addr_clean(m2); strcpy(manual, m2); ident_save(); inet_kick(); } }
        else if (id == B_IRELE) { char r2[100]; strcpy(r2, R.rele); if (ui_prompt("Relé de la red", "Dirección del relé (nombre o IP:puerto; /clave si la tiene). Vacío: sin relé.", r2, 98)) { addr_clean(r2); strcpy(R.rele, r2); push_config(); inet_kick(); char lg[160]; snprintf(lg, 160, "[prueba] red rele %s", R.rele); z_log(lg); } }
        else if (id == B_IINV) make_invite(); else if (id == B_ICOPY) { z_clip_set(invcode, strlen(invcode)); strcpy(msg, tr_("Código copiado")); } else if (id == B_IJOIN) join_code();
        else if (id == B_NEWC) { newconv = 1; memset(pick, 0, sizeof pick); cfoc = 1; }
        else if (id == B_ALL) { for (int k = 0; k < R.nm; k++) pick[k] = 1; }
        else if (id >= B_PICK) { int k = id - B_PICK; if (k < MAXM) pick[k] = !pick[k]; }
        else if (id >= B_CONV) { int k = id - B_CONV; if (k < ncv) { strcpy(cur_conv, CV[k].conv); newconv = 0; chat_off = 0; cfoc = 1; seen_set(CV[k].conv, CV[k].n); } }
        else if (id == B_CREATE) create_net(); else if (id == B_RENAME) { char n[32]; strcpy(n, myname); if (ui_prompt("Nombre del equipo", "Nombre de este equipo en la red:", n, 30) && n[0]) { safe(n); strcpy(myname, n); ident_save(); } }
        else if (id == B_LEAVE) { if (ui_confirm("Salir de la red", "¿Sacar este equipo de la red local? Necesitará otra invitación para volver.")) { z_unlink(CFG); memset(&R, 0, sizeof R); z_sync(); strcpy(msg, tr_("Este equipo ya no está en la red")); } }
        else if (id == B_DELNET) { if (ui_confirm("Borrar la red", "¿Borrar esta red? Los terminales dejarán de poder usarla.")) { char ids[MAXM][12]; int n = R.nm; for (int k = 0; k < n; k++) strcpy(ids[k], R.m[k].id); R.nm = 1; strcpy(R.m[0].id, myid); strcpy(R.m[0].name, myname); R.ns = 0; push_to(ids, n); z_unlink(CFG); memset(&R, 0, sizeof R); z_sync(); strcpy(msg, tr_("Red borrada")); } }
        else if (id >= B_PDEL) { int k = id - B_PDEL; char q[200]; snprintf(q, 200, tr_("¿Dejar de compartir %s?"), R.p[k].name); if (k < R.np && ui_confirm("Dejar de compartir", q)) { memmove(&R.p[k], &R.p[k + 1], (R.np - k - 1) * sizeof R.p[0]); R.np--; push_config(); } }
        else if (id == B_ADDP) add_printer();
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
        else if (id == B_SEND) chat_send(); else if (id == B_FIELD) { cfoc = 1; ui_input_click(W, e, compose, &ccur); }
        break; } }
int main(const char *a) {
    if (!strcmp(a, "--servicio")) return service();
    if ((z_sess(0, 0, 0) & 7) == 2) { ui_confirm("Red local", "La red local solo se puede usar en la sesión del administrador."); return 0; }
    ident(); cfg_load(); ab_load(); if (!service_running()) z_spawn("/rd/bin/redlocal", "--servicio");
    zsys s; z_sysinfo(&s); W = zw_new("Red local", -1, 0, MIN(920, (int)s.scr_w - 40), MIN(640, (int)s.scr_h - 80), WF_DECOR | WF_RESIZE); if (!W) return 1;
    busca(); draw(); z_log("[prueba] red local abierta"); zevent e;
    for (;;) { int g = z_event(&e, 500); poll_udp(); u64 now = z_ms(); if (now - last_busca > 5000) { busca(); cfg_load(); ab_load(); } if (!g) { draw(); continue; } if (e.type == EV_REDRAW) { draw(); continue; } if (e.win != (u32)W->id) continue;
        if (zs_event(W, &e)) { draw(); continue; }
        if (e.type == EV_CLOSE) return 0; if (e.type == EV_RESIZE) { zw_resized(W, e.x, e.y); draw(); continue; }
        if (e.type == EV_WHEEL) { if (tab == 2) chat_off = MAX(0, chat_off + (int)e.key * 54); else etop -= (int)e.key * 3; draw(); continue; }
        if (e.type == EV_KEY) { if (tab == 2 && cfoc) { if (e.ch == '\n') chat_send(); else ui_input_key(compose, 380, &ccur, &e); } else if (e.key == K_F5) { busca(); if (sel_sh >= 0) list_dir(); } draw(); continue; }
        if (e.type == EV_DOWN) { if (e.btn & 2 && tab == 2 && cfoc) { ui_input_menu(W, compose, 380, &ccur, &e); draw(); continue; } click(&e); draw(); } }
}
