/* Guardia de red: registro de cada conexion y permiso de Internet por programa.
   Todas las salidas de red de los programas pasan por aqui (TCP connect, UDP sendto, ping y DNS):
   - destinos de la red local (10/8, 172.16/12, 192.168/16, 169.254/16, difusion): permitidos y anotados;
   - destinos de Internet: segun la regla del programa: 1 permitir, 2 bloquear, 0 preguntar.
     Para preguntar se deja la peticion pendiente; el escritorio la muestra y contesta. Mientras, el programa espera
     (60 s como mucho; sin respuesta se bloquea).
   El registro es un anillo de 256 entradas; las salidas repetidas al mismo sitio en menos de un minuto se suman a
   la misma entrada (asi la Red local, que se anuncia cada 15 s, no llena el registro). */
#include "kernel.h"

typedef struct { char name[24]; u8 pol; } rule_t;
static rule_t rules[64]; static int nrules;
#define NL 256
static gl_t led[NL]; static u32 lseq; static int lpos;
typedef struct { u32 id; int pid, state; char name[24], host[48]; u32 ip; u16 port; } ask_t;   /* state 0 libre, 1 esperando, 2 permitido, 3 bloqueado */
static ask_t asks[8]; static u32 ask_id;
static int grants[16]; static char grant_name[16][24];   /* "solo esta vez": permitido para ese proceso mientras viva */
static mutex_t gm;

static int is_local(u32 ip) { u8 a = ip & 255, b = ip >> 8 & 255; return a == 10 || a == 127 || (a == 172 && b >= 16 && b < 32) || (a == 192 && b == 168) || (a == 169 && b == 254) || ip == 0xFFFFFFFF || (a >= 224 && a < 240) || !ip; }
static int rule_of(const char *n) { for (int i = 0; i < nrules; i++) if (!strcmp(rules[i].name, n)) return rules[i].pol; return 0; }
static void rule_set(const char *n, int pol) { for (int i = 0; i < nrules; i++) if (!strcmp(rules[i].name, n)) { rules[i].pol = pol; return; } if (nrules < 64) { strncpy0(rules[nrules].name, n, 24); rules[nrules].pol = pol; nrules++; } }
static int granted(int pid, const char *n) { for (int i = 0; i < 16; i++) if (grants[i] == pid && !strcmp(grant_name[i], n)) return 1; return 0; }
static void grant(int pid, const char *n) { static int k; grants[k] = pid; strncpy0(grant_name[k], n, 24); k = (k + 1) % 16; }

/* anota (o suma a una entrada reciente igual) y devuelve su numero de serie para contar bytes */
static u32 note(const char *app, u32 ip, int port, int proto, int verdict, const char *host) {
    u64 now = ticks * 1000 / HZ;
    for (int k = 0; k < NL; k++) { gl_t *g = &led[k]; if (g->seq && g->ip == ip && g->port == port && g->proto == proto && g->verdict == verdict && !strcmp(g->app, app) && now - g->t < 60000 && (!host || !host[0] || !strcmp(g->host, host))) { g->t = now; g->count++; return g->seq; } }
    gl_t *g = &led[lpos]; lpos = (lpos + 1) % NL; memset(g, 0, sizeof *g); g->seq = ++lseq; g->t = now; g->t0 = now; strncpy0(g->app, app, 24); g->ip = ip; g->port = port; g->proto = proto; g->verdict = verdict; g->count = 1; if (host) strncpy0(g->host, host, 48); return g->seq; }
void guard_bytes(u32 seq, u64 tx, u64 rx) { if (!seq) return; gl_t *g = 0; for (int k = 0; k < NL; k++) if (led[k].seq == seq) { g = &led[k]; break; } if (g) { g->tx += tx; g->rx += rx; } }

/* 0 si puede salir (y *seq para el registro), E_ACCES si no */
int guard_check(u32 ip, int port, int proto, const char *host, u32 *seq) {
    if (seq) *seq = 0; if (!cur || !cur->proc) return 0; proc_t *p = cur->proc; char app[24]; strncpy0(app, p->name, 24);
    if (!host && is_local(ip)) { mutex_lock(&gm); u32 s = note(app, ip, port, proto, 3, 0); mutex_unlock(&gm); if (seq) *seq = s; return 0; }
    mutex_lock(&gm); int pol = rule_of(app); int ok = pol == 1 || granted(p->pid, app);
    if (!ok && pol == 2) { note(app, ip, port, proto, 2, host); static char lapp[24]; static u64 lt; int first = strcmp(lapp, app) || ticks - lt > 30 * HZ; if (first) { strncpy0(lapp, app, 24); lt = ticks; } mutex_unlock(&gm); if (first) kprintf("guardia: %s bloqueado (%s)\n", app, host ? host : "ip"); return E_ACCES; }
    if (!ok) {   /* preguntar al usuario por medio del escritorio */
        ask_t *a = 0; for (int i = 0; i < 8; i++) if (!asks[i].state) { a = &asks[i]; break; }
        if (!a) { note(app, ip, port, proto, 2, host); mutex_unlock(&gm); return E_ACCES; }
        memset(a, 0, sizeof *a); a->id = ++ask_id; a->pid = p->pid; a->state = 1; strncpy0(a->name, app, 24); if (host) strncpy0(a->host, host, 48); a->ip = ip; a->port = port; u32 id = a->id; mutex_unlock(&gm);
        kprintf("guardia: %s pide salir a Internet (%s)\n", app, host ? host : "ip");
        u64 end = ticks + 60 * HZ; int st = 1;
        while (st == 1) { msleep(50); mutex_lock(&gm); st = a->id == id ? a->state : 3; if (st != 1 || ticks > end || p->killed) { if (st == 1) st = 3; if (a->id == id) a->state = 0; } mutex_unlock(&gm); }
        mutex_lock(&gm); ok = st == 2; if (ok) grant(p->pid, app); }
    u32 s = note(app, ip, port, proto, ok ? 1 : 2, host); mutex_unlock(&gm); if (seq) *seq = s;
    return ok ? 0 : E_ACCES; }

/* llamada 114: op 0 poner regla (a nombre, b regla), 1 leer reglas (a destino, b cuantas), 2 leer registro (a destino, b cuantas),
   3 siguiente pregunta pendiente (a destino zask_t), 4 contestar (a id, b 1 una vez, 2 siempre, 3 bloquear una vez, 4 bloquear siempre), 5 borrar registro */
long guard_syscall(int op, u64 a, u64 b) {
    mutex_lock(&gm); long r = 0;
    if (op == 0) { char n[24]; strncpy0(n, (const char *)a, 24); if (!strcmp(n, "*")) nrules = 0; else rule_set(n, (int)b & 3); }   /* "*": borrar todas (cambio de sesion) */
    else if (op == 1) { zrule_t *o = (zrule_t *)a; int n = MIN((int)b, nrules); for (int i = 0; i < n; i++) { strncpy0(o[i].name, rules[i].name, 24); o[i].pol = rules[i].pol; } r = n; }
    else if (op == 2) { gl_t *o = (gl_t *)a; int n = 0; for (int k = 0; k < NL && n < (int)b; k++) { int i = (lpos - 1 - k + 2 * NL) % NL; if (led[i].seq) o[n++] = led[i]; } r = n; }
    else if (op == 3) { zask_t *o = (zask_t *)a; r = 0; for (int i = 0; i < 8; i++) if (asks[i].state == 1) { o->id = asks[i].id; o->pid = asks[i].pid; strncpy0(o->name, asks[i].name, 24); strncpy0(o->host, asks[i].host, 48); o->ip = asks[i].ip; o->port = asks[i].port; r = 1; break; } }
    else if (op == 4) { for (int i = 0; i < 8; i++) if (asks[i].state == 1 && asks[i].id == (u32)a) { asks[i].state = b == 1 || b == 2 ? 2 : 3; if (b == 2) rule_set(asks[i].name, 1); if (b == 4) rule_set(asks[i].name, 2); } }
    else if (op == 5) { memset(led, 0, sizeof led); lpos = 0; }
    else r = E_INVAL;
    mutex_unlock(&gm); return r; }
/* puede cambiar las reglas: Configuracion en la sesion del administrador (el escritorio, proceso 1, siempre) */
int guard_admin(void) { return cur && cur->proc && !strcmp(cur->proc->name, "config") && !sess_guest(); }
