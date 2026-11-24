/* Forense: rastreador de desastres de sistemas. Fase 1: busca los Windows de los discos del equipo, los
   reconoce sin modificar nada (version, equipo, ultimo apagado, arranque, volumen danado, drivers cambiados,
   lo que dice la reparacion automatica de Windows...) y guarda el expediente en la memoria USB.
   Fase 2: manda el expediente a un Lab (IA local o Lyceum) con el protocolo zz-forense/1; el Lab puede pedir mas
   pruebas de solo lectura, que el administrador aprueba. Disenyo completo y protocolo: docs/FORENSE.md */
#include "z.h"
#include "zntfs.h"

typedef struct { char disk[16], part[16]; u64 lba, sectors; int win; char title[120]; } sys_t;
static sys_t SY[32]; static int nsy, sel = -1, scroll, lines;
static zwin *W; static char msg[200], rep[24576], equipo[64], owner[120], casedir[160];
static char uname[8][64]; static int nuser;   /* usuarios de Windows: para anonimizar */
#define LW 300
#define TB 40

static int rd(void *ctx, u64 lba, int n, void *buf) { return z_disk_read((const char *)ctx, lba, n, buf) ? -5 : 0; }

/* FILETIME a fecha legible (UTC) */
static void fecha(u64 ft, char *o, int cap) {
    if (ft < 116444736000000000ULL) { snprintf(o, cap, "%s", _("desconocida")); return; }
    i64 t = (i64)(ft / 10000000 - 11644473600ULL), d = t / 86400, s = t % 86400;
    i64 z = d + 719468, era = z / 146097, doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, y = yoe + era * 400;
    i64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153, dd = doy - (153 * mp + 2) / 5 + 1, mm = mp < 10 ? mp + 3 : mp - 9; if (mm <= 2) y++;
    snprintf(o, cap, "%02ld/%02ld/%ld %02ld:%02ld UTC", dd, mm, y, s / 3600, s / 60 % 60); }
static u64 unix_ft(u32 u) { return ((u64)u + 11644473600ULL) * 10000000ULL; }

static int rl;   /* longitud del informe */
static void add(const char *fmt, ...) { __builtin_va_list ap; __builtin_va_start(ap, fmt); rl += vsnprintf(rep + rl, sizeof rep - rl, fmt, ap); __builtin_va_end(ap); if (rl >= (int)sizeof rep) rl = sizeof rep - 1; }

/* nombre de Windows a partir del registro: Windows 11 sigue diciendo "Windows 10" en ProductName */
static void win_name(zreg *r, char *o, int cap) {
    const char *K = "Microsoft\\Windows NT\\CurrentVersion"; char pn[80] = "", dv[40] = "", cb[20] = "", ubr[20] = "";
    zr_str(r, K, "ProductName", pn, 80); zr_str(r, K, "DisplayVersion", dv, 40); if (!dv[0]) zr_str(r, K, "ReleaseId", dv, 40);
    zr_str(r, K, "CurrentBuild", cb, 20); zr_str(r, K, "UBR", ubr, 20);
    if (atoi(cb) >= 22000 && !strncmp(pn, "Windows 10", 10)) { char t[80]; snprintf(t, 80, "Windows 11%s", pn + 10); strcpy(pn, t); }
    snprintf(o, cap, "%s%s%s%s%s%s%s", pn[0] ? pn : "Windows", dv[0] ? " " : "", dv, cb[0] ? " (" : "", cb[0] ? _("compilación ") : "", cb, cb[0] ? (ubr[0] ? "." : ")") : "");
    if (cb[0] && ubr[0]) { int l = strlen(o); snprintf(o + l, cap - l, "%s)", ubr); } }

static void scan(void) {
    nsy = 0; zdisk dk[16]; int nd = z_disks(dk, 16);
    for (int i = 0; i < nd && nsy < 32; i++) {
        zpart p[16]; int np = z_parts(dk[i].name, p, 16);
        for (int j = 0; j < np && nsy < 32; j++) {
            u8 b[512]; if (z_disk_read(dk[i].name, p[j].start, 1, b) || memcmp(b + 3, "NTFS    ", 8)) continue;
            sys_t *s = &SY[nsy]; memset(s, 0, sizeof *s); strcpy(s->disk, dk[i].name); strcpy(s->part, p[j].name); s->lba = p[j].start; s->sectors = p[j].sectors;
            zntfs *z = zn_open(rd, s->disk, s->lba);
            if (z && zn_lookup(z, "Windows\\System32\\config\\SYSTEM") >= 0) {
                s->win = 1; long n; u8 *h = zn_readall(z, "Windows\\System32\\config\\SOFTWARE", 64 << 20, &n); zreg *r = h ? zr_open(h, n) : 0;
                if (r) { win_name(r, s->title, sizeof s->title); zr_close(r); } else strcpy(s->title, _("Windows (registro ilegible)"));
                free(h); }
            else snprintf(s->title, sizeof s->title, "%s", z ? _("NTFS sin Windows") : _("NTFS ilegible"));
            zn_close(z); nsy++;
            char m[200]; snprintf(m, 200, "[prueba] forense sistema %s: %s", s->part, s->title); z_log(m); } }
    char m[60]; snprintf(m, 60, "[prueba] forense sistemas %d", nsy); z_log(m);
    if (sel >= nsy) sel = -1; if (sel < 0) for (int i = 0; i < nsy; i++) if (SY[i].win) { sel = i; break; } }

typedef struct { char name[64]; u64 mtime; } drv_t;
static drv_t DV[512]; static int ndv;
static int drv_cb(const zn_ent *e, void *ctx) { (void)ctx; if (!e->dir && ndv < 512) { strncpy0(DV[ndv].name, e->name, 64); DV[ndv].mtime = e->mtime; ndv++; } return 0; }
static int cmp_drv(const void *a, const void *b) { const drv_t *x = a, *y = b; return x->mtime < y->mtime ? 1 : x->mtime > y->mtime ? -1 : 0; }
static int cnt_cb(const zn_ent *e, void *ctx) { int *c = ctx; if (!e->dir) c[0]++; if (e->mtime > *(u64 *)(c + 2)) *(u64 *)(c + 2) = e->mtime; return 0; }
static char users[400];
static int user_cb(const zn_ent *e, void *ctx) { (void)ctx; static const char *skip[] = {"Public", "Default", "Default User", "All Users", "desktop.ini"};
    if (!e->dir) return 0; for (int i = 0; i < 5; i++) if (!strcasecmp(e->name, skip[i])) return 0;
    if (nuser < 8) strncpy0(uname[nuser++], e->name, 64);
    int l = strlen(users); snprintf(users + l, sizeof users - l, "%s%s", l ? ", " : "", e->name); return 0; }

/* informe y expediente del sistema elegido (solo lectura) */
static void analyze(void) {
    rl = 0; rep[0] = 0; equipo[0] = 0; owner[0] = 0; nuser = 0; casedir[0] = 0; scroll = 0; if (sel < 0 || sel >= nsy) return; sys_t *s = &SY[sel];
    zntfs *z = zn_open(rd, s->disk, s->lba); if (!z) { add("%s\n", _("No se puede leer el volumen NTFS.")); return; }
    char t[300], d1[40], d2[40]; long n;
    add("== %s ==\n%s\n%s: %s, %s %lu, %lu MB\n", "SISTEMA", s->title, _("Disco"), s->disk, _("sector"), s->lba, s->sectors / 2048);
    char label[64]; int ma, mi, fl; zn_volume(z, label, 64, &ma, &mi, &fl);
    add("%s: \"%s\", NTFS %d.%d\n", _("Volumen"), label, ma, mi);
    add("%s\n", fl & 1 ? _("ATENCIÓN: el volumen está marcado como dañado (Windows no se cerró bien o encontró errores).") : _("El volumen está limpio."));
    u8 *h = zn_readall(z, "Windows\\System32\\config\\SOFTWARE", 64 << 20, &n); zreg *r = h ? zr_open(h, n) : 0; u64 shut = 0;
    add("\n== %s ==\n", "WINDOWS");
    if (r) { const char *K = "Microsoft\\Windows NT\\CurrentVersion"; char ed[60] = "", *own = owner; u32 inst = 0; win_name(r, t, 300); zr_str(r, K, "EditionID", ed, 60); zr_str(r, K, "RegisteredOwner", own, 120); zr_u32(r, K, "InstallDate", &inst);
        fecha(unix_ft(inst), d1, 40); add("%s: %s\n%s: %s\n%s: %s\n%s: %s\n", _("Versión"), t, _("Edición"), ed, _("Instalado"), d1, _("Propietario registrado"), own);
        zr_close(r); }
    else add("%s\n", _("No se puede leer la colmena SOFTWARE del registro."));
    free(h);
    h = zn_readall(z, "Windows\\System32\\config\\SYSTEM", 64 << 20, &n); r = h ? zr_open(h, n) : 0;
    char cs[40] = "ControlSet001";
    if (r) { u32 cur = 1; zr_u32(r, "Select", "Current", &cur); snprintf(cs, 40, "ControlSet%03u", cur); char k[160];
        snprintf(k, 160, "%s\\Control\\ComputerName\\ComputerName", cs); zr_str(r, k, "ComputerName", equipo, 64);
        snprintf(k, 160, "%s\\Control\\Windows", cs); zr_u64(r, k, "ShutdownTime", &shut);
        char tz[80] = ""; snprintf(k, 160, "%s\\Control\\TimeZoneInformation", cs); zr_str(r, k, "TimeZoneKeyName", tz, 80);
        fecha(shut, d2, 40); add("%s: %s\n%s: %s\n%s: %s\n", _("Equipo"), equipo, _("Último apagado correcto"), d2, _("Zona horaria"), tz);
        snprintf(k, 160, "%s\\Services", cs); int sk = zr_key(r, k), nsv = 0, nboot = 0; char nm[128];
        while (sk >= 0 && zr_subkey(r, sk, nsv, nm, sizeof nm) >= 0) { char kk[300]; u32 st = 9; snprintf(kk, 300, "%s\\%s", k, nm); if (!zr_u32(r, kk, "Start", &st) && st == 0) nboot++; nsv++; }
        add("%s: %d (%d %s)\n", _("Servicios y drivers"), nsv, nboot, _("se cargan al arrancar"));
        zr_close(r); }
    else add("%s\n", _("No se puede leer la colmena SYSTEM del registro (puede ser la causa: Windows no arranca sin ella)."));
    free(h);
    /* arranque */
    add("\n== %s ==\n", "ARRANQUE");
    int uefi = 0; zmount m[32]; int nm = z_mounts(m, 32); zpart pp[16]; int np = z_parts(s->disk, pp, 16);
    for (int i = 0; i < np; i++) for (int j = 0; j < nm; j++) if (!strcmp(m[j].dev, pp[i].name)) { zdirent de; char p[120]; snprintf(p, 120, "/%s/EFI/Microsoft/Boot/bootmgfw.efi", m[j].name); if (!z_stat(p, &de)) { uefi = 1; add("%s: %s (%s)\n", _("Gestor de arranque de Windows"), p + 1, "UEFI"); } }
    if (!uefi) add("%s\n", zn_lookup(z, "bootmgr") >= 0 ? _("Arranque por BIOS: bootmgr está en este volumen.") : _("No se ha encontrado el gestor de arranque de Windows (ni partición EFI con bootmgfw.efi ni bootmgr)."));
    /* reparacion automatica de Windows */
    add("\n== %s ==\n", "REPARACION_AUTOMATICA");
    u8 *srt = zn_readall(z, "Windows\\System32\\LogFiles\\Srt\\SrtTrail.txt", 65536, &n);
    if (srt) { char *u = malloc(n * 2 + 4); int k = 0; if (n >= 2 && srt[0] == 0xFF && srt[1] == 0xFE) { zn_utf8(srt + 2, (n - 2) / 2, u, n * 2 + 4); k = strlen(u); } else { memcpy(u, srt, n); k = n; } u[k] = 0;
        int shown = 0; for (char *ln = u; *ln && shown < 40; ) { char *e = strchr(ln, '\n'); if (e) *e = 0; char *cr = strchr(ln, '\r'); if (cr) *cr = 0;
            if (strstr(ln, "Root cause") || strstr(ln, "Result:") || strstr(ln, "Error code") || strstr(ln, "is corrupt") || strstr(ln, "Boot critical") || strstr(ln, "Last successful") || strstr(ln, "repair attempts") || strstr(ln, "Name:")) { add("  %s\n", ln); shown++; }
            if (!e) break; ln = e + 1; }
        free(u); free(srt); }
    else add("%s\n", _("No hay SrtTrail.txt: la reparación automática de Windows no llegó a ejecutarse."));
    /* actualizaciones a medio instalar */
    add("\n== %s ==\n", "ACTUALIZACIONES");
    zn_ent pe; if (!zn_stat(z, "Windows\\WinSxS\\pending.xml", &pe)) { fecha(pe.mtime, d1, 40); add("%s (%s)\n", _("Hay una actualización a medio instalar: WinSxS\\pending.xml"), d1); }
    else add("%s\n", _("No hay actualizaciones a medio instalar."));
    /* pantallazos */
    add("\n== %s ==\n", "PANTALLAZOS");
    { int c[4] = {0}; zn_list(z, "Windows\\Minidump", cnt_cb, c); zn_ent me; int full = !zn_stat(z, "Windows\\MEMORY.DMP", &me);
      if (c[0]) { fecha(*(u64 *)(c + 2), d1, 40); add("%d %s %s\n", c[0], _("volcados de pantallazo azul; el último:"), d1); } else add("%s\n", _("No hay volcados de pantallazos azules (Windows\\Minidump vacío o inexistente)."));
      if (full) add("%s\n", _("Hay un volcado completo de memoria (MEMORY.DMP).")); }
    /* drivers cambiados poco antes del ultimo apagado */
    add("\n== %s ==\n", "DRIVERS");
    ndv = 0; zn_list(z, "Windows\\System32\\drivers", drv_cb, 0); qsort(DV, ndv, sizeof DV[0], cmp_drv);
    int rec = 0; u64 lim = shut ? shut - 14ULL * 86400 * 10000000 : 0;
    for (int i = 0; i < ndv && rec < 12; i++) if (shut && DV[i].mtime >= lim) { fecha(DV[i].mtime, d1, 40); add("  %s  %s\n", d1, DV[i].name); rec++; }
    add("%d %s; %d %s\n", ndv, _("drivers en System32\\drivers"), rec, _("cambiados en los 14 días anteriores al último apagado"));
    char m2[200]; if (rec) { snprintf(m2, 200, "[prueba] forense driver reciente %s", DV[0].name); z_log(m2); }
    /* copias del registro y usuarios */
    add("\n== %s ==\n", "COPIAS_REGISTRO");
    zn_ent rb; add("%s\n", !zn_stat(z, "Windows\\System32\\config\\RegBack\\SYSTEM", &rb) && rb.size ? _("Hay copias del registro en RegBack.") : _("Las copias de RegBack están vacías (normal desde Windows 10 1803)."));
    users[0] = 0; zn_list(z, "Users", user_cb, 0); add("\n== %s ==\n%s\n", "USUARIOS", users[0] ? users : "-");
    zn_close(z);
    lines = 0; for (char *p = rep; *p; p++) lines += *p == '\n';
    snprintf(m2, 200, "[prueba] forense informe %s: %s, %d lineas, sucio %d", s->part, equipo, lines, fl & 1); z_log(m2); }

static void save(void) {
    if (!rl) { strcpy(msg, _("Elige un sistema")); return; }
    u32 t[6]; z_time(t); char eq[64]; int k = 0; for (char *p = equipo[0] ? equipo : "equipo"; *p && k < 40; p++) eq[k++] = (*p == '/' || *p == ' ') ? '_' : *p; eq[k] = 0;
    z_mkdir("/sys/FORENSE"); snprintf(casedir, 160, "/sys/FORENSE/%04u%02u%02u-%02u%02u-%s", t[5], t[4], t[3], t[2], t[1], eq); z_mkdir(casedir);
    char p[200]; snprintf(p, 200, "%s/expediente.txt", casedir);
    char *o = malloc(rl + 400); int n = snprintf(o, 400, "zz-forense/1\r\n%s\r\n\r\n", _("Expediente de Forense (ZunzúnOS). Solo lectura: no se ha modificado nada del disco."));
    for (char *q = rep; *q; q++) { if (*q == '\n') o[n++] = '\r'; o[n++] = *q; }
    int e = z_writefile(p, o, n); free(o);
    if (e < 0) snprintf(msg, sizeof msg, "%s", _("No se pudo guardar el expediente")); else snprintf(msg, sizeof msg, "%s %s", _("Expediente guardado en"), p);
    char m[260]; snprintf(m, 260, "[prueba] forense expediente %s: %d", p, e < 0 ? e : 0); z_log(m); }


/* ================================================================ Labs (fase 2) */
void draw(void);
#define ISDIG(c) ((c) >= '0' && (c) <= '9')
#define ISALN(c) (ISDIG(c) || (((c) | 32) >= 'a' && ((c) | 32) <= 'z'))
/* a.b.c.d al principio de s: 1 y en *n los caracteres usados */
static int ip4(const char *s, int v[4], int *n) { const char *p = s; for (int i = 0; i < 4; i++) { if (!ISDIG(*p)) return 0; int x = 0, k = 0; while (ISDIG(*p) && k < 4) { x = x * 10 + (*p++ - '0'); k++; } if (k > 3 || x > 255) return 0; v[i] = x; if (i < 3) { if (*p != '.') return 0; p++; } } *n = p - s; return 1; }
typedef struct { char name[64], url[200], key[256], model[128]; int anon; } lab_t;
static lab_t LB[8]; static int nlb, pref = -1, cur_lab = -1;
static char f_name[64], f_url[200], f_key[256], f_model[128]; static int f_anon = 1, ffoc = -1, fcur, showkey;
static int view;                       /* 0 informe, 1 conversacion con el Lab, 2 configurar Labs */
static char conv[32768]; static int cl, cscroll; int cscroll_follow;   /* la conversacion se va al final con cada novedad */
static char cfgdir[64], lumen_key[256], lumen_model[128];
#define LYC_URL "https://api.lyceum.technology/openai/v1"

static void cadd(const char *fmt, ...) { extern int cscroll_follow; cscroll_follow = 1; __builtin_va_list ap; __builtin_va_start(ap, fmt); cl += vsnprintf(conv + cl, sizeof conv - cl, fmt, ap); __builtin_va_end(ap); if (cl >= (int)sizeof conv) cl = sizeof conv - 1; }
static void kv(const char *t, const char *k, char *o, int cap) { int l = strlen(k); o[0] = 0;
    for (const char *p = t; p && *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : 0) if (!strncmp(p, k, l) && p[l] == '=') { int i = 0; p += l + 1; while (p[i] && p[i] != '\n' && p[i] != '\r' && i < cap - 1) { o[i] = p[i]; i++; } o[i] = 0; return; } }
static void field(char **p, char *o, int cap) { int i = 0; while (**p && **p != '\t' && **p != '\n' && **p != '\r') { if (i < cap - 1) o[i++] = **p; (*p)++; } o[i] = 0; if (**p == '\t') (*p)++; }
static void labs_load(void) {
    zdirent d; strcpy(cfgdir, z_stat("/sys", &d) ? "/rd/tmp/ZUNZUN" : "/sys/ZUNZUN"); z_mkdir(cfgdir);
    char p[96]; snprintf(p, 96, "%s/FORENSE.TXT", cfgdir); char *t = z_readfile(p, 0); nlb = 0; pref = -1;
    if (t) { char v[16]; kv(t, "preferido", v, 16); pref = v[0] ? atoi(v) : -1;
        for (int i = 0; i < 8; i++) { char k[8], line[900]; snprintf(k, 8, "lab%d", i); kv(t, k, line, sizeof line); if (!line[0]) continue;
            lab_t *l = &LB[nlb]; char *q = line, an[8]; field(&q, l->name, 64); field(&q, l->url, 200); field(&q, l->key, 256); field(&q, l->model, 128); field(&q, an, 8); l->anon = atoi(an); nlb++; }
        free(t); }
    if (pref >= nlb) pref = nlb ? 0 : -1;
    /* la clave de Lyceum que ya tenga LUMEN */
    snprintf(p, 96, "%s/LUMEN.TXT", cfgdir); t = z_readfile(p, 0); lumen_key[0] = 0; lumen_model[0] = 0;
    if (t) { char s[200]; kv(t, "clave_lyceum", lumen_key, 256); kv(t, "modelo_lyceum", lumen_model, 128); kv(t, "servidor", s, 200);
        if (!lumen_key[0] && strstr(s, "lyceum.technology")) { kv(t, "clave", lumen_key, 256); kv(t, "modelo", lumen_model, 128); } free(t); }
    if (!lumen_model[0]) strcpy(lumen_model, "lyceum/router"); }
static void labs_save(void) {
    char *o = malloc(8192); int n = snprintf(o, 8192, "preferido=%d\n", pref);
    for (int i = 0; i < nlb; i++) n += snprintf(o + n, 8192 - n, "lab%d=%s\t%s\t%s\t%s\t%d\n", i, LB[i].name, LB[i].url, LB[i].key, LB[i].model, LB[i].anon);
    char p[96]; snprintf(p, 96, "%s/FORENSE.TXT", cfgdir); z_writefile(p, o, n); free(o); }

/* http:// solo dentro de la red local; fuera de ella, https:// (y entonces siempre se anonimiza) */
static int host_private(const char *url) {
    const char *h = strstr(url, "://"); h = h ? h + 3 : url; char host[128]; int k = 0; while (h[k] && h[k] != ':' && h[k] != '/' && k < 127) { host[k] = h[k]; k++; } host[k] = 0;
    int v[4], used; if (ip4(host, v, &used) && !host[used]) { int a = v[0], b = v[1]; return a == 10 || a == 127 || (a == 192 && b == 168) || (a == 172 && b >= 16 && b <= 31) || (a == 169 && b == 254); }
    if (!strcasecmp(host, "localhost") || !strchr(host, '.')) return 1;
    int l = strlen(host); const char *suf[] = {".local", ".lan", ".home", ".internal", ".intranet"};
    for (int i = 0; i < 5; i++) { int sl = strlen(suf[i]); if (l > sl && !strcasecmp(host + l - sl, suf[i])) return 1; }
    return 0; }

/* ---- anonimizar: datos personales -> marcas, y de vuelta ---- */
typedef struct { char real[120], mark[24]; } am_t; static am_t AM[24]; static int nam;
static void am_add(const char *real, const char *mark) { if (strlen(real) < 3 || nam >= 24) return; for (int i = 0; i < nam; i++) if (!strcasecmp(AM[i].real, real)) return;
    strncpy0(AM[nam].real, real, 120); strncpy0(AM[nam].mark, mark, 24); nam++; }
static void am_build(void) {
    nam = 0; if (equipo[0]) am_add(equipo, "[EQUIPO]");
    for (int i = 0; i < nuser; i++) { char m[24]; snprintf(m, 24, "[USUARIO%d]", i + 1); am_add(uname[i], m); }   /* antes que el propietario */
    if (owner[0]) { am_add(owner, "[PROPIETARIO]"); char w[120]; strncpy0(w, owner, 120); int k = 1;
        for (char *s = w; *s; ) { char *sp = strchr(s, ' '); if (sp) *sp = 0; char m[24]; snprintf(m, 24, "[NOMBRE%d]", k++); am_add(s, m); if (!sp) break; s = sp + 1; } }
    /* lo mas largo primero: "Marta Garcia" antes que "Marta" */
    for (int i = 0; i < nam; i++) for (int j = i + 1; j < nam; j++) if (strlen(AM[j].real) > strlen(AM[i].real)) { am_t x = AM[i]; AM[i] = AM[j]; AM[j] = x; } }
/* cambia en s (malloc) cada 'de' por 'a' sin distinguir mayusculas; devuelve el nuevo texto */
static char *subst(char *s, const char *de, const char *a) {
    int dl = strlen(de), al = strlen(a), n = 0; for (char *p = s; (p = strcasestr(p, de)); p += dl) n++; if (!n) return s;
    char *o = malloc(strlen(s) + (long)n * (al > dl ? al - dl : 0) + 1), *q = o; char *p = s, *f;
    while ((f = strcasestr(p, de))) { memcpy(q, p, f - p); q += f - p; memcpy(q, a, al); q += al; p = f + dl; } strcpy(q, p); free(s); return o; }
static char *anon(const char *s, int on) {
    char *o = strdup(s); if (!on) return o;
    for (int i = 0; i < nam; i++) o = subst(o, AM[i].real, AM[i].mark);
    /* correos y direcciones IP */
    for (char *p = o; *p; p++) { if (*p == '@' && p > o && p[1] && p[-1] != ' ') { char *a = p, *b = p + 1; while (a > o && (ISALN(a[-1]) || strchr("._-+", a[-1]))) a--; while (*b && (ISALN(*b) || strchr(".-", *b))) b++;
            if (a < p && b > p + 1 && strchr(p, '.') && strchr(p, '.') < b) { char *x = malloc(strlen(o) + 16); memcpy(x, o, a - o); int k = a - o; memcpy(x + k, "[CORREO]", 8); k += 8; strcpy(x + k, b); p = x + k - 1; free(o); o = x; } } }
    for (char *p = o; *p; p++) { int v[4], n = 0; if ((p == o || !ISDIG(p[-1])) && ip4(p, v, &n) && !ISDIG(p[n]) && p[n] != '.') {
            char *x = malloc(strlen(o) + 8); int k = p - o; memcpy(x, o, k); memcpy(x + k, "[IP]", 4); k += 4; strcpy(x + k, p + n); p = x + k - 1; free(o); o = x; } }
    return o; }
static char *deanon(const char *s, int on) { char *o = strdup(s); if (!on) return o; for (int i = 0; i < nam; i++) o = subst(o, AM[i].mark, AM[i].real); return o; }

/* ---- conversacion: lo que se manda al Lab (ya anonimizado) ---- */
typedef struct { const char *role; char *text; } lmsg_t; static lmsg_t LM[48]; static int nlm;
static void lm_clear(void) { for (int i = 0; i < nlm; i++) free(LM[i].text); nlm = 0; }
static void lm_add(const char *role, char *text) { if (nlm < 48) { LM[nlm].role = role; LM[nlm].text = text; nlm++; } else free(text); }

static const char *SYS_ES =
    "zz-forense/1\n"
    "Eres el Lab de analisis de Forense, el rastreador de desastres de ZunzunOS. Un administrador ha arrancado ZunzunOS desde una memoria USB "
    "en un equipo cuyo Windows ya no arranca y te manda un expediente con pruebas leidas del disco, sin modificar nada. Tu trabajo: encontrar "
    "la causa mas probable y proponer como repararlo paso a paso. Nada se ejecuta sin que el administrador lo apruebe.\n\n"
    "Responde SIEMPRE con un unico objeto JSON (puede llevar texto alrededor, pero cuenta el JSON):\n"
    "{\"protocolo\":\"zz-forense/1\",\"diagnostico\":\"...\",\"hipotesis\":[{\"causa\":\"...\",\"probabilidad\":0.7,\"pruebas\":[\"...\"]}],"
    "\"pedir\":[{\"id\":\"p1\",\"accion\":\"leer_archivo\",\"args\":{\"ruta\":\"\\\\Windows\\\\...\",\"max_kb\":64}}],"
    "\"pasos\":[{\"id\":\"r1\",\"accion\":\"desactivar_driver\",\"args\":{\"servicio\":\"...\"},\"explicacion\":\"...\",\"riesgo\":\"bajo\"}],\"fin\":false}\n\n"
    "Pruebas de solo lectura que puedes pedir en \"pedir\" (el administrador las aprueba y te llega el resultado):\n"
    "- leer_archivo {\"ruta\",\"max_kb\" (64 como mucho)}: el texto del archivo (el final si es largo).\n"
    "- listar_carpeta {\"ruta\"}: nombres, tamanos y fechas.\n"
    "- leer_registro {\"colmena\": \"SYSTEM\" o \"SOFTWARE\", \"clave\", \"valor\" opcional}: valores y subclaves de la clave.\n"
    "(eventos, pantallazo y salud_disco llegaran en una version posterior.)\n"
    "Reparaciones que puedes proponer en \"pasos\" (cada una con copia previa y aprobacion): reparar_arranque, desactivar_driver {\"servicio\"}, "
    "deshacer_actualizacion_pendiente, liberar_espacio, restaurar_registro_copia, marcar_disco_revisado. No inventes otras acciones.\n"
    "Pide pruebas solo si las necesitas; con un diagnostico solido deja \"pedir\" vacio. Pon \"fin\": true cuando no quede nada por hacer. "
    "Algunos datos personales vienen cambiados por marcas como [USUARIO1] o [EQUIPO]: usalas tal cual.\n";

/* una peticion al Lab: texto de la respuesta del asistente (malloc) o 0 con el error en err */
static u64 wait_t0; static int pump(void *ctx) { (void)ctx; zevent e; while (z_event(&e, 0)) if (e.type == EV_REDRAW) draw();
    static u64 last; if (z_ms() - last > 500) { last = z_ms(); snprintf(msg, sizeof msg, "%s %lu s", _("Esperando al Lab..."), (z_ms() - wait_t0) / 1000); draw(); } return 0; }
static char *ask_lab(lab_t *L, char *err, int ecap) {
    long n = 200; for (int i = 0; i < nlm; i++) n += zj_escape(LM[i].text, 0, 0) + 40;
    n += strlen(L->model) * 2 + 200; char *b = malloc(n); long k = 0;
    k += snprintf(b + k, n - k, "{\"model\":"); k += zj_escape(L->model, b + k, n - k); k += snprintf(b + k, n - k, ",\"temperature\":0.2,\"stream\":false,\"messages\":[");
    for (int i = 0; i < nlm; i++) { k += snprintf(b + k, n - k, "%s{\"role\":\"%s\",\"content\":", i ? "," : "", LM[i].role); k += zj_escape(LM[i].text, b + k, n - k); k += snprintf(b + k, n - k, "}"); }
    k += snprintf(b + k, n - k, "]}");
    char u[260], h[400]; snprintf(u, 260, "%s/chat/completions", L->url); h[0] = 0; if (L->key[0]) snprintf(h, 400, "Authorization: Bearer %s\r\n", L->key);
    zhttp_opt o = {h, 0, pump, 0, 300000}; zhttp_info inf; char *out = 0; long ol = 0; wait_t0 = z_ms();
    char m[160]; snprintf(m, 160, "[prueba] forense lab enviado %ld bytes a %s", k, L->name); z_log(m);
    int e = z_http_ex("POST", u, b, k, "application/json", &o, &out, &ol, &inf); free(b); msg[0] = 0;
    if (e < 0) { snprintf(err, ecap, "%s: %s", _("No se pudo contactar con el Lab"), z_http_err(e)); free(out); return 0; }
    const char *end = out + ol, *p = out; while (p < end && *p != '{') p++;
    if (inf.status != 200) { char em[300] = ""; const char *er = p < end ? zj_get(p, end, "error") : 0; const char *mm = er ? zj_get(er, end, "message") : 0;
        if (mm) zj_str(mm, end, em, 300); else if (out) { int l = ol < 200 ? (int)ol : 200; memcpy(em, out, l); em[l] = 0; }
        snprintf(err, ecap, "%s %d: %s", _("El Lab respondió con un error"), inf.status, em); free(out); return 0; }
    const char *ch = p < end ? zj_get(p, end, "choices") : 0, *c0 = ch ? zj_item(ch, end, 0) : 0, *ms = c0 ? zj_get(c0, end, "message") : 0, *ct = ms ? zj_get(ms, end, "content") : 0;
    if (!ct) { snprintf(err, ecap, "%s", _("La respuesta del Lab no tiene el formato de la API de chat.")); free(out); return 0; }
    char *txt = malloc(ol + 1); zj_str(ct, end, txt, ol + 1); free(out); return txt; }

/* busca en el texto el objeto JSON del protocolo */
static const char *find_obj(const char *t, const char **end) {
    const char *e = t + strlen(t);
    for (const char *p = t; (p = strchr(p, '{')); p++) { const char *q = zj_skip(p, e); if (!q) continue; const char *pr = zj_get(p, q, "protocolo"); char v[32];
        if (pr && zj_str(pr, q, v, 32) > 0 && !strcmp(v, "zz-forense/1")) { *end = q; return p; } }
    return 0; }

/* ---- pruebas de solo lectura que pide el Lab ---- */
static zntfs *Zl; static char rbuf[65536]; static int rbl;
static void radd(const char *fmt, ...) { __builtin_va_list ap; __builtin_va_start(ap, fmt); rbl += vsnprintf(rbuf + rbl, sizeof rbuf - rbl, fmt, ap); __builtin_va_end(ap); if (rbl >= (int)sizeof rbuf) rbl = sizeof rbuf - 1; }
static int list_cb(const zn_ent *e, void *ctx) { int *n = ctx; if (++*n > 300) return 1; char d[40]; fecha(e->mtime, d, 40); if (e->dir) radd("  [%s]  %s\n", e->name, d); else radd("  %s  %lu  %s\n", e->name, e->size, d); return 0; }
static void text_of(const u8 *b, long n) {   /* archivo a texto: UTF-16 con BOM, texto normal o binario */
    if (n >= 2 && b[0] == 0xFF && b[1] == 0xFE) { char *u = malloc(n * 2 + 4); zn_utf8(b + 2, (int)((n - 2) / 2), u, n * 2 + 4); radd("%s\n", u); free(u); return; }
    long bad = 0; for (long i = 0; i < n; i++) if (b[i] < 9 || (b[i] > 13 && b[i] < 32)) bad++;
    if (bad * 20 > n) { radd("(%s, %ld bytes; %s)\n", _("binario"), n, _("primeros 256 en hexadecimal")); for (long i = 0; i < n && i < 256; i++) radd("%02x%s", b[i], i % 32 == 31 ? "\n" : " "); radd("\n"); return; }
    for (long i = 0; i < n; i++) { if (rbl >= (int)sizeof rbuf - 2) break; rbuf[rbl++] = b[i] == '\r' ? ' ' : b[i]; } rbuf[rbl] = 0; radd("\n"); }
static void do_request(const char *id, const char *acc, const char *args, const char *e, int an) {
    char ruta[400] = "", tmp[400]; const char *v;
    if ((v = zj_get(args, e, "ruta")) || (v = zj_get(args, e, "archivo"))) { zj_str(v, e, tmp, 400); char *d = deanon(tmp, an); strncpy0(ruta, d, 400); free(d); }
    radd("[%s] %s %s\n", id, acc, ruta);
    if (!strcmp(acc, "leer_archivo")) {
        long mk = 64; if ((v = zj_get(args, e, "max_kb"))) { long m; if (zj_milli(v, e, &m)) mk = m / 1000; } if (mk < 1) mk = 1; if (mk > 64) mk = 64;
        zn_ent st; if (zn_stat(Zl, ruta, &st) || st.dir) { radd("%s\n", _("No existe ese archivo.")); return; }
        long want = mk * 1024; u64 off = st.size > (u64)want ? st.size - want : 0; long len = st.size - off; u8 *b = malloc(len + 1);
        long got = zn_read(Zl, ruta, off, b, len); if (got < 0) { radd("%s (%ld)\n", _("No se pudo leer"), got); free(b); return; }
        if (off) radd("(%s %lu bytes; %s)\n", _("archivo de"), st.size, _("solo el final")); text_of(b, got); free(b);
        char m[200]; snprintf(m, 200, "[prueba] forense lab prueba %s leer_archivo: %ld bytes", id, got); z_log(m); }
    else if (!strcmp(acc, "listar_carpeta")) { int n = 0; if (zn_list(Zl, ruta, list_cb, &n) < 0) radd("%s\n", _("No existe esa carpeta.")); else if (n > 300) radd("(%s)\n", _("solo las 300 primeras")); }
    else if (!strcmp(acc, "leer_registro")) {
        char col[24] = "", clave[300] = "", valor[120] = ""; if ((v = zj_get(args, e, "colmena"))) zj_str(v, e, col, 24); if ((v = zj_get(args, e, "clave"))) { zj_str(v, e, tmp, 300); char *d = deanon(tmp, an); strncpy0(clave, d, 300); free(d); }
        if ((v = zj_get(args, e, "valor"))) zj_str(v, e, valor, 120);
        for (char *c = col; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
        if (strcmp(col, "SYSTEM") && strcmp(col, "SOFTWARE")) { radd("%s\n", _("Solo se pueden leer las colmenas SYSTEM y SOFTWARE (las demás guardan contraseñas o datos personales).")); return; }
        char hp[80]; snprintf(hp, 80, "Windows\\System32\\config\\%s", col); long n; u8 *h = zn_readall(Zl, hp, 256 << 20, &n); zreg *r = h ? zr_open(h, n) : 0;
        if (!r) { radd("%s\n", _("No se puede abrir la colmena.")); free(h); return; }
        char *cs = strcasestr(clave, "CurrentControlSet"); if (cs && !strcmp(col, "SYSTEM")) { u32 c = 1; zr_u32(r, "Select", "Current", &c); char nk[300]; snprintf(nk, 300, "%.*sControlSet%03u%s", (int)(cs - clave), clave, c, cs + 17); strcpy(clave, nk); }
        int key = zr_key(r, clave); radd("%s\\%s\n", col, clave);
        if (key < 0) radd("%s\n", _("No existe esa clave."));
        else { char nm[256], val[512];
            if (valor[0]) { if (zr_str(r, clave, valor, val, sizeof val) < 0) radd("%s\n", _("No existe ese valor.")); else radd("  %s = %s\n", valor, val); }
            else { for (int i = 0; i < 100; i++) { int ty; if (zr_value_at(r, key, i, nm, sizeof nm, &ty) < 0) break; zr_str(r, clave, nm[0] ? nm : "@", val, sizeof val); radd("  %s = %s\n", nm[0] ? nm : "(predeterminado)", val); }
                for (int i = 0; i < 200; i++) { if (zr_subkey(r, key, i, nm, sizeof nm) < 0) break; radd("  [%s]\n", nm); } } }
        zr_close(r); free(h); char m[200]; snprintf(m, 200, "[prueba] forense lab prueba %s leer_registro %s", id, clave); z_log(m); }
    else if (!strcmp(acc, "eventos") || !strcmp(acc, "pantallazo") || !strcmp(acc, "salud_disco")) radd("%s\n", _("Esta prueba aún no está disponible en esta versión de Forense."));
    else radd("%s\n", _("Acción desconocida: rechazada (no está en el catálogo)."));
    radd("\n"); }

/* respuesta del Lab -> vista legible; devuelve cuantas pruebas pide (y en *fin si da el caso por cerrado) */
static int show_reply(const char *obj, const char *e, int an, int *fin) {
    char t[2048]; const char *v; *fin = (v = zj_get(obj, e, "fin")) && zj_true(v, e);
    if ((v = zj_get(obj, e, "diagnostico"))) { zj_str(v, e, t, sizeof t); char *d = deanon(t, an); cadd("\n== %s ==\n%s\n", _("DIAGNÓSTICO"), d); free(d); }
    const char *hs = zj_get(obj, e, "hipotesis"), *h; if (hs && zj_item(hs, e, 0)) cadd("\n== %s ==\n", _("CAUSAS POSIBLES"));
    for (int i = 0; hs && (h = zj_item(hs, e, i)) && i < 12; i++) { long pr = 0; char c[1024] = ""; if ((v = zj_get(h, e, "causa"))) zj_str(v, e, c, sizeof c); if ((v = zj_get(h, e, "probabilidad"))) zj_milli(v, e, &pr);
        char *d = deanon(c, an); cadd("%2ld%%  %s\n", pr / 10, d); free(d); const char *ps = zj_get(h, e, "pruebas"), *pe;
        for (int j = 0; ps && (pe = zj_item(ps, e, j)) && j < 6; j++) { zj_str(pe, e, c, sizeof c); d = deanon(c, an); cadd("      - %s\n", d); free(d); } }
    const char *pd = zj_get(obj, e, "pedir"), *q; int np = 0; if (pd && zj_item(pd, e, 0)) cadd("\n== %s ==\n", _("EL LAB PIDE ESTAS PRUEBAS (solo lectura)"));
    for (int i = 0; pd && (q = zj_item(pd, e, i)) && i < 10; i++) { char id[16] = "", ac[40] = "", ar[600] = ""; if ((v = zj_get(q, e, "id"))) zj_str(v, e, id, 16); if ((v = zj_get(q, e, "accion"))) zj_str(v, e, ac, 40);
        const char *a = zj_get(q, e, "args"); if (a) { const char *ae = zj_skip(a, e); int l = ae ? ae - a : 0; if (l > 599) l = 599; memcpy(ar, a, l); ar[l] = 0; }
        char *d = deanon(ar, an); cadd("  [%s] %s %s\n", id, ac, d); free(d); np++; }
    const char *st = zj_get(obj, e, "pasos"), *s; if (st && zj_item(st, e, 0)) cadd("\n== %s ==\n", _("REPARACIONES PROPUESTAS"));
    for (int i = 0; st && (s = zj_item(st, e, i)) && i < 10; i++) { char id[16] = "", ac[40] = "", ex[1024] = "", ri[20] = "", ar[400] = ""; if ((v = zj_get(s, e, "id"))) zj_str(v, e, id, 16); if ((v = zj_get(s, e, "accion"))) zj_str(v, e, ac, 40);
        if ((v = zj_get(s, e, "explicacion"))) zj_str(v, e, ex, sizeof ex); if ((v = zj_get(s, e, "riesgo"))) zj_str(v, e, ri, 20);
        const char *a = zj_get(s, e, "args"); if (a) { const char *ae = zj_skip(a, e); int l = ae ? ae - a : 0; if (l > 399) l = 399; memcpy(ar, a, l); ar[l] = 0; }
        char *d = deanon(ex, an), *d2 = deanon(ar, an); cadd("  [%s] %s %s  (%s: %s)\n      %s\n", id, ac, d2, _("riesgo"), ri, d); free(d); free(d2); }
    if (st && zj_item(st, e, 0)) cadd("%s\n", _("Las reparaciones automáticas llegarán en la fase 4. De momento puedes hacerlas a mano con estas indicaciones."));
    if (*fin) cadd("\n%s\n", _("El Lab da el caso por cerrado."));
    return np; }

static void save(void);
static void lab_save_conv(void) {
    if (!casedir[0]) save(); if (!casedir[0]) return;
    char p[220]; snprintf(p, 220, "%s/lab.txt", casedir); z_writefile(p, conv, cl);
    long n = 0; for (int i = 0; i < nlm; i++) n += strlen(LM[i].text) + 40; char *o = malloc(n + 1); long k = 0;
    for (int i = 0; i < nlm; i++) k += snprintf(o + k, n + 1 - k, "----- %s -----\n%s\n", LM[i].role, LM[i].text);
    snprintf(p, 220, "%s/lab-enviado.txt", casedir); z_writefile(p, o, k); free(o); }

static void lab_run(void) {
    if (!rl || sel < 0) { strcpy(msg, _("Elige un sistema")); return; }
    if (pref < 0 || pref >= nlb) { view = 2; strcpy(msg, _("Primero configura un Lab.")); return; }
    lab_t *L = &LB[pref]; int an = L->anon || !host_private(L->url);
    char q[300]; snprintf(q, 300, _("Se enviará el expediente al Lab «%s». %s"), L->name, an ? _("Los datos personales irán cambiados por marcas.") : _("Irá tal cual (Lab de confianza en tu red)."));
    if (!ui_confirm("Analizar en el Lab", q)) return;
    am_build(); lm_clear(); view = 1; cl = 0; conv[0] = 0; cscroll = 0;
    cadd("%s: %s (%s)\n%s: %s\n", _("Lab"), L->name, L->url, _("Modelo"), L->model);
    char *sysp = malloc(strlen(SYS_ES) + 80); snprintf(sysp, strlen(SYS_ES) + 80, "%s%s", SYS_ES, TH.lang ? "Write the texts for the administrator in English.\n" : "Escribe los textos para el administrador en espanol.\n");
    lm_add("system", sysp); char *ex = malloc(rl + 100); snprintf(ex, rl + 100, "EXPEDIENTE\n%s", rep); lm_add("user", anon(ex, an)); free(ex);
    Zl = zn_open(rd, SY[sel].disk, SY[sel].lba);
    for (int round = 0; round < 8; round++) {
        cadd("\n%s %d...\n", _("Consultando al Lab, ronda"), round + 1); draw();
        char err[400]; char *ans = ask_lab(L, err, sizeof err);
        if (!ans) { cadd("%s\n", err); char m[300]; snprintf(m, 300, "[prueba] forense lab error: %s", err); z_log(m); break; }
        lm_add("assistant", strdup(ans));
        const char *e, *obj = find_obj(ans, &e); int fin = 0;
        if (!obj) { char *d = deanon(ans, an); cadd("\n%s\n%s\n", _("El Lab no ha respondido con el formato zz-forense/1. Su respuesta:"), d); free(d); free(ans); z_log("[prueba] forense lab sin protocolo"); break; }
        int np = show_reply(obj, e, an, &fin); char m[160]; snprintf(m, 160, "[prueba] forense lab respuesta ronda %d: pide %d, fin %d", round + 1, np, fin); z_log(m);
        if (fin || !np) { free(ans); break; }
        draw(); char qq[200]; snprintf(qq, 200, _("El Lab pide %d pruebas de solo lectura (las ves en la ventana). ¿Se las doy?"), np);
        if (!ui_confirm("Pruebas pedidas por el Lab", qq)) { cadd("%s\n", _("No autorizadas.")); lm_add("user", strdup("El administrador no autoriza mas pruebas. Da tu mejor diagnostico con lo que tienes y deja \"pedir\" vacio.")); free(ans); continue; }
        rbl = 0; rbuf[0] = 0; radd("RESULTADOS\n"); const char *pd = zj_get(obj, e, "pedir"), *it;
        for (int i = 0; pd && (it = zj_item(pd, e, i)) && i < 10; i++) { char id[16] = "", ac[40] = ""; const char *v; if ((v = zj_get(it, e, "id"))) zj_str(v, e, id, 16); if ((v = zj_get(it, e, "accion"))) zj_str(v, e, ac, 40);
            static const char NOARGS[] = "{}"; const char *a = zj_get(it, e, "args"); do_request(id, ac, a ? a : NOARGS, a ? e : NOARGS + 2, an); }
        cadd("%s (%d bytes)\n", _("Pruebas enviadas al Lab"), rbl); lm_add("user", anon(rbuf, an)); free(ans); }
    zn_close(Zl); Zl = 0; lab_save_conv(); z_log("[prueba] forense lab fin"); }

/* ---- vista de Labs: lista + formulario ---- */
typedef struct { int x, y, w, h, id; } hz_t; static hz_t HZ[40]; static int nhz;
static void hz(int x, int y, int w, int h, int id) { if (nhz < 40) HZ[nhz++] = (hz_t){x, y, w, h, id}; }
enum { H_ROW = 1, H_FIELD = 20, H_ANON = 30, H_SAVE, H_NEW, H_DEL, H_TEST, H_PREF, H_LUMEN, H_SHOW };
static char *ffield(int i, int *cap) { switch (i) { case 0: *cap = 60; return f_name; case 1: *cap = 190; return f_url; case 2: *cap = 250; return f_key; default: *cap = 120; return f_model; } }
static void form_load(int i) { cur_lab = i; if (i >= 0) { strcpy(f_name, LB[i].name); strcpy(f_url, LB[i].url); strcpy(f_key, LB[i].key); strcpy(f_model, LB[i].model); f_anon = LB[i].anon; }
    else { f_name[0] = 0; strcpy(f_url, "http://"); f_key[0] = 0; f_model[0] = 0; f_anon = 1; } ffoc = -1; showkey = 0; }
static void form_save(void) {
    if (!f_name[0] || strncmp(f_url, "http", 4)) { strcpy(msg, _("Pon un nombre y una URL base (http:// o https://).")); return; }
    if (!strncmp(f_url, "http://", 7) && !host_private(f_url)) { strcpy(msg, _("Fuera de tu red local el Lab tiene que usar https://.")); return; }
    int l = strlen(f_url); while (l && f_url[l - 1] == '/') f_url[--l] = 0;
    int i = cur_lab; if (i < 0) { if (nlb >= 8) { strcpy(msg, _("Como mucho 8 Labs.")); return; } i = nlb++; }
    strcpy(LB[i].name, f_name); strcpy(LB[i].url, f_url); strcpy(LB[i].key, f_key); strcpy(LB[i].model, f_model); LB[i].anon = f_anon || !host_private(f_url);
    if (pref < 0) pref = i; cur_lab = i; labs_save(); snprintf(msg, sizeof msg, "%s: %s", _("Lab guardado"), f_name);
    char m[200]; snprintf(m, 200, "[prueba] forense lab guardado %s %s", f_name, f_url); z_log(m); }
static void form_test(void) {
    char u[260], h[400]; snprintf(u, 260, "%s/models", f_url); h[0] = 0; if (f_key[0]) snprintf(h, 400, "Authorization: Bearer %s\r\n", f_key);
    zhttp_opt o = {h, 0, pump, 0, 30000}; zhttp_info inf; char *out = 0; long ol = 0; wait_t0 = z_ms(); int e = z_http_ex("GET", u, 0, 0, 0, &o, &out, &ol, &inf);
    if (e < 0) snprintf(msg, sizeof msg, "%s: %s", _("No se pudo contactar con el Lab"), z_http_err(e));
    else if (inf.status != 200) snprintf(msg, sizeof msg, "%s %d", _("El Lab respondió con un error"), inf.status);
    else { const char *end = out + ol, *p = out; while (p < end && *p != '{') p++; const char *d = p < end ? zj_get(p, end, "data") : 0, *m0 = d ? zj_item(d, end, 0) : 0, *id = m0 ? zj_get(m0, end, "id") : 0; char mid[128] = "";
        if (id) zj_str(id, end, mid, 128); if (!f_model[0] && mid[0]) strcpy(f_model, mid);
        snprintf(msg, sizeof msg, "%s%s%s", _("El Lab responde."), mid[0] ? " " : "", mid[0] ? mid : ""); }
    free(out); }
static void draw_labs(int x, int y) {
    nhz = 0; int w = W->w - x - 16;
    g_textb(W, x, y, "Labs", UI_TEXT); g_text(W, x + 60, y, _("Cualquier servidor compatible con la API de OpenAI: Lyceum, Ollama, llama.cpp, vLLM..."), UI_MUTED); y += 26;
    for (int i = 0; i < nlb; i++) { if (i == cur_lab) g_rect(W, x - 4, y - 3, w, 24, UI_ACC2); char b[300]; snprintf(b, 300, "%s %s  —  %s  (%s)", i == pref ? "*" : " ", LB[i].name, LB[i].url, LB[i].model);
        zw_clip(W, x, y - 3, w, 24); g_text(W, x, y, b, UI_TEXT); zw_noclip(W); hz(x - 4, y - 3, w, 24, H_ROW + i); y += 26; }
    if (!nlb) { g_text(W, x, y, _("Todavía no hay ningún Lab."), UI_MUTED); y += 26; }
    y += 8; static const char *L[] = {"Nombre", "URL base", "API key", "Modelo"};
    for (int i = 0; i < 4; i++) { g_text(W, x, y + 6, _(L[i]), UI_TEXT); int cap; char *f = ffield(i, &cap), mk[260];
        if (i == 2 && !showkey) { int n = strlen(f); memset(mk, '*', n); mk[n] = 0; f = mk; }
        ui_input(W, x + 100, y, w - 100 - (i == 2 ? 90 : 0), 26, f, ffoc == i ? fcur : 0, ffoc == i); hz(x + 100, y, w - 100 - (i == 2 ? 90 : 0), 26, H_FIELD + i);
        if (i == 2) { ui_button(W, x + w - 84, y, 84, 26, showkey ? "Ocultar" : "Mostrar", 0); hz(x + w - 84, y, 84, 26, H_SHOW); }
        y += 34; }
    int cloud = strncmp(f_url, "http://", 7) && f_url[0] && !host_private(f_url);
    g_frame(W, x + 100, y + 4, 16, 16, UI_LINE); if (f_anon || cloud) g_rect(W, x + 104, y + 8, 8, 8, UI_ACC);
    g_text(W, x + 124, y + 4, cloud ? _("Anonimizar (siempre, porque el Lab está fuera de tu red)") : _("Anonimizar los datos personales"), UI_TEXT); hz(x + 100, y, 400, 24, H_ANON); y += 34;
    static const char *B[] = {"Guardar", "Nuevo", "Borrar", "Probar conexión", "Preferido"}; int bx = x + 100;
    for (int i = 0; i < 5; i++) { int bw = g_textw(_(B[i])) + 24; ui_button(W, bx, y, bw, 28, B[i], i == 0 ? 2 : 0); hz(bx, y, bw, 28, H_SAVE + i); bx += bw + 8; }
    y += 40;
    int has_ly = 0; for (int i = 0; i < nlb; i++) if (strstr(LB[i].url, "lyceum.technology")) has_ly = 1;
    if (lumen_key[0] && !has_ly) { int bw = g_textw(_("Añadir Lyceum con la clave de LUMEN")) + 24; ui_button(W, x + 100, y, bw, 28, "Añadir Lyceum con la clave de LUMEN", 0); hz(x + 100, y, bw, 28, H_LUMEN); y += 40; }
    g_text(W, x, y, _("Protocolo zz-forense/1: ver docs/FORENSE.md. Las claves se guardan solo en esta memoria USB."), UI_MUTED); }
static void labs_click(zevent *e) {
    for (int i = nhz - 1; i >= 0; i--) { hz_t *h = &HZ[i]; if (e->x < h->x || e->x >= h->x + h->w || e->y < h->y || e->y >= h->y + h->h) continue; int id = h->id;
        if (id >= H_ROW && id < H_FIELD) form_load(id - H_ROW);
        else if (id >= H_FIELD && id < H_ANON) { ffoc = id - H_FIELD; int cap; char *f = ffield(ffoc, &cap); if (ffoc == 2 && !showkey) fcur = strlen(f); else ui_input_click(W, e, f, &fcur); }
        else if (id == H_ANON) f_anon = !f_anon; else if (id == H_SAVE) form_save(); else if (id == H_NEW) form_load(-1);
        else if (id == H_DEL && cur_lab >= 0 && ui_confirm("Borrar Lab", LB[cur_lab].name)) { for (int k = cur_lab; k < nlb - 1; k++) LB[k] = LB[k + 1]; nlb--; if (pref >= nlb) pref = nlb - 1; labs_save(); form_load(-1); }
        else if (id == H_TEST) form_test(); else if (id == H_PREF && cur_lab >= 0) { pref = cur_lab; labs_save(); }
        else if (id == H_LUMEN) { cur_lab = -1; strcpy(f_name, "Lyceum"); strcpy(f_url, LYC_URL); strcpy(f_key, lumen_key); strcpy(f_model, lumen_model); f_anon = 1; form_save(); }
        else if (id == H_SHOW) showkey = !showkey;
        return; }
    ffoc = -1; }

/* ================================================================ pantalla */
/* texto con titulos "== X ==", lineas partidas por letras y avisos en rojo */
static int text_view(const char *buf, int x, int y, int w, int vh, int *scr) {
    int cols = MAX(20, (w - 8) / 8), vis = 0;
    for (const char *p = buf; *p; ) { const char *e = strchr(p, '\n'); int k = e ? e - p : (int)strlen(p), c = 0; for (int i = 0; i < k; i++) c += (p[i] & 0xC0) != 0x80; vis += c ? (c + cols - 1) / cols : 1; if (!e) break; p = e + 1; }
    int maxs = MAX(0, vis - vh / 18 + 1); if (*scr > maxs) *scr = maxs; if (*scr < 0) *scr = 0;
    zs_area(W, x, y, w, vh, *scr * 18); zw_clip(W, x, y, w, vh);
    int ln = 0; for (const char *p = buf; *p && ln < *scr + vh / 18 + 1; ) { const char *e = strchr(p, '\n'); int k = e ? e - p : (int)strlen(p); int head = p[0] == '=';
        char t0[16]; int tk = k < 15 ? k : 15; memcpy(t0, p, tk); t0[tk] = 0; int warn = strstr(t0, "ATENC") || strstr(t0, "WARN");
        int i = 0; do { int s = i, c = 0; while (i < k && c < cols) { i++; while (i < k && (p[i] & 0xC0) == 0x80) i++; c++; }
            if (ln >= *scr) { char l[600]; int n = i - s < 599 ? i - s : 599; memcpy(l, p + s, n); l[n] = 0; int yy = y + (ln - *scr) * 18; if (head) g_textb(W, x, yy, l, UI_ACC); else g_text(W, x + (s ? 16 : 0), yy, l, warn ? 0xB91C1C : UI_TEXT); }
            ln++; } while (i < k);
        if (!e) break; p = e + 1; }
    zw_noclip(W); zs_stop(W); return vis; }

static const char *TABS[] = {"Informe", "Lab", "Labs"};
void draw(void) {
    g_clear(W, UI_BG); g_rect(W, 0, 0, W->w, TB, UI_PANEL);
    ui_button(W, 8, 7, 150, 26, "Buscar de nuevo", 0); ui_button(W, 166, 7, 170, 26, "Guardar expediente", rl ? 2 : 0); ui_button(W, 344, 7, 170, 26, "Analizar en el Lab", rl ? 2 : 0);
    int tx = 530; for (int i = 0; i < 3; i++) { int bw = g_textw(_(TABS[i])) + 24; ui_button(W, tx, 7, bw, 26, TABS[i], view == i ? 2 : 0); tx += bw + 6; }
    g_rect(W, 0, TB, LW, W->h - TB, UI_PANEL); g_textb(W, 12, TB + 10, "Sistemas encontrados", UI_TEXT);
    if (!nsy) g_text(W, 12, TB + 36, _("No hay volúmenes NTFS en los discos."), UI_MUTED);
    for (int i = 0; i < nsy; i++) { int y = TB + 32 + i * 46; if (i == sel) g_rect(W, 0, y, LW, 44, UI_ACC2); zw_clip(W, 8, y, LW - 14, 44);
        g_textb(W, 12, y + 4, SY[i].title, SY[i].win ? UI_TEXT : UI_MUTED); char b[80]; snprintf(b, 80, "%s · %lu MB", SY[i].part, SY[i].sectors / 2048); g_text(W, 12, y + 23, b, UI_MUTED); zw_noclip(W); }
    { const char *ro = _("Solo lectura: Forense no cambia nada del disco."); zw_clip(W, 0, TB, LW - 8, W->h - TB);   /* en dos lineas si no cabe */
      if (g_textw(ro) < LW - 24) g_text(W, 12, W->h - 50, ro, UI_MUTED); else { const char *sp = strchr(ro, ':'); char a[80]; int k = sp ? sp - ro + 1 : 0; memcpy(a, ro, k); a[k] = 0; g_text(W, 12, W->h - 68, a, UI_MUTED); g_text(W, 12, W->h - 50, sp ? sp + 2 : ro, UI_MUTED); }
      zw_noclip(W); }
    int x = LW + 14, y = TB + 10, vh = W->h - TB - 40;
    if (view == 0) lines = text_view(rep, x, y, W->w - x - 8, vh, &scroll);
    else if (view == 1) { if (cscroll_follow) { cscroll = 1 << 20; cscroll_follow = 0; } text_view(cl ? conv : _("Pulsa «Analizar en el Lab» para mandar el expediente."), x, y, W->w - x - 8, vh, &cscroll); }
    else draw_labs(x, y);
    zs_show(W);
    g_rect(W, LW, W->h - 28, W->w - LW, 28, UI_PANEL); zw_clip(W, LW + 8, W->h - 28, W->w - LW - 16, 28); g_text(W, LW + 10, W->h - 22, msg, UI_TEXT); zw_noclip(W);
    zw_present(W); }

int main(const char *a) {
    (void)a; W = zw_new("Forense", -1, 0, 1000, 660, WF_DECOR); labs_load(); form_load(pref); strcpy(msg, _("Buscando sistemas en los discos...")); draw();
    scan(); analyze(); msg[0] = 0; if (!nsy) strcpy(msg, _("Conecta el disco del equipo averiado o arranca ZunzúnOS en él."));
    { int sx, sy, lx, ly, tx = 530; for (int i = 0; i < 2; i++) tx += g_textw(_(TABS[i])) + 30; zw_screen(W, 344 + 85, 20, &sx, &sy); zw_screen(W, tx + 20, 20, &lx, &ly);
      char m[160]; snprintf(m, 160, "[prueba] forense listo (lab en %d,%d) (labs en %d,%d)", sx, sy, lx, ly); z_log(m); }
    draw(); zevent e;
    for (;;) { if (!z_event(&e, -1)) continue; if (e.type == EV_REDRAW) { draw(); continue; } if (e.win != (u32)W->id) continue;
        if (e.type == EV_CLOSE) return 0; if (e.type == EV_RESIZE) { zw_resized(W, e.x, e.y); draw(); continue; }
        if (view != 2 && zs_event(W, &e)) { draw(); continue; }
        int *sc = view == 1 ? &cscroll : &scroll;
        if (e.type == EV_WHEEL) { *sc -= (int)e.key * 3; draw(); continue; }
        if (e.type == EV_KEY) {
            if (view == 2 && ffoc >= 0) { if (e.key == K_TAB) ffoc = (ffoc + 1) % 4; else if (e.ch == '\n') form_save(); else { int cap; char *f = ffield(ffoc, &cap); if (ffoc == 2 && !showkey && e.ch != 22 && e.ch != 8) { draw(); continue; } ui_input_key(f, cap, &fcur, &e); } draw(); continue; }
            if (e.key == K_DOWN) (*sc)++; else if (e.key == K_UP) (*sc)--; else if (e.key == K_PGDN) *sc += 20; else if (e.key == K_PGUP) *sc -= 20; else if (e.ch == 19) save(); else if (e.ch == 12) lab_run(); draw(); continue; }
        if (e.type == EV_DOWN) {
            if (e.y < TB) { if (e.x >= 8 && e.x < 158) { strcpy(msg, _("Buscando sistemas en los discos...")); draw(); scan(); analyze(); msg[0] = 0; } else if (e.x >= 166 && e.x < 336) save(); else if (e.x >= 344 && e.x < 514) lab_run();
                else { int tx = 530; for (int i = 0; i < 3; i++) { int bw = g_textw(_(TABS[i])) + 24; if (e.x >= tx && e.x < tx + bw) view = i; tx += bw + 6; } } }
            else if (e.x < LW) { int i = (e.y - TB - 32) / 46; if (e.y >= TB + 32 && i < nsy && i != sel) { sel = i; strcpy(msg, _("Analizando...")); draw(); analyze(); msg[0] = 0; } }
            else if (view == 2) labs_click(&e);
            draw(); } }
}
