/* Radio: emisoras de todo el mundo por Internet. El catalogo es Radio Browser (radio-browser.info), abierto y sin
   cuentas: decenas de miles de emisoras con su pais, generos, formato y popularidad. Se piden solo las emisoras en MP3,
   el formato que decodifica ZunzunOS (zmp3d.c, propio).
   La emision llega por HTTP o HTTPS (z_http_ex, a trozos). Si la emisora manda metadatos ICY (titulo de lo que suena
   cada icy-metaint bytes) se separan del audio. El MP3 se guarda en un deposito, se decodifica y se pasa a los 48 kHz
   de la tarjeta. Mientras suena, la ventana sigue viva desde el "pump" de z_http_ex.
   Favoritas y pais propio en /sys/ZUNZUN/RADIO.TXT. Para las pruebas, "servidor=" fija el catalogo (tools/fake_radio.py). */
#include "z.h"
#define CFG "/sys/ZUNZUN/RADIO.TXT"
#define TB 40
#define SB 64
#define RH 24
#define MAXS 500
typedef struct { char uuid[48], name[120], url[480], cc[4], tags[120]; int kbps; long clicks; } st_t;
typedef struct { char name[80], code[4]; long n; } cat_t;
static zwin *W; static char msg[300], fixed_srv[128], mycc[4] = "ES", query[120]; static int qcur, qfoc;
static st_t *S, *FV; static int ns, nfv, top, sel = -1; static cat_t *C; static int nc;
static int view;   /* 0 Populares, 1 Mi pais, 2 Paises, 3 Generos, 4 Favoritas, 5 Busqueda, 6 un pais o genero elegido */
static char vtitle[120];
static void strncat0(char *d, const char *s, int cap) { int l = strlen(d); if (l < cap - 1) strncpy0(d + l, s, cap - l); }
static int isalnum(int c) { return isalpha(c) || isdigit(c); }
static void enc(sb_t *b, const char *s) { static const char hx[] = "0123456789ABCDEF"; for (const u8 *p = (const u8 *)s; *p; p++) { if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~') sb_add(b, (const char *)p, 1); else { char t[3] = {'%', hx[*p >> 4], hx[*p & 15]}; sb_add(b, t, 3); } } }
static long jnum(const char *v, const char *e) { if (!v) return 0; while (v < e && (*v == ' ' || *v == '"')) v++; long n = 0; while (v < e && *v >= '0' && *v <= '9') n = n * 10 + *v++ - '0'; return n; }
static void jstr(const char *obj, const char *e, const char *k, char *o, int cap) { o[0] = 0; const char *v = zj_get(obj, e, k); if (v) zj_str(v, e, o, cap); for (char *p = o; *p; p++) if (*p == '\t' || *p == '\n' || *p == '\r') *p = ' '; }
void draw(void);

/* ---------- favoritas y ajustes ---------- */
static void cfg_load(void) { nfv = 0; char *t = z_readfile(CFG, 0);
    for (char *p = t; p && *p; ) { char *e = strchr(p, '\n'); if (e) *e = 0;
        if (!strncmp(p, "servidor=", 9)) strncpy0(fixed_srv, p + 9, 128); else if (!strncmp(p, "pais=", 5)) strncpy0(mycc, p + 5, 4);
        else if (!strncmp(p, "fav=", 4) && nfv < MAXS) { char *f[6]; int k = 0; f[k++] = p + 4; for (char *q = p + 4; *q && k < 6; q++) if (*q == '\t') { *q = 0; f[k++] = q + 1; }
            if (k == 6) { st_t *s = &FV[nfv++]; memset(s, 0, sizeof *s); strncpy0(s->uuid, f[0], 48); strncpy0(s->name, f[1], 120); strncpy0(s->url, f[2], 480); strncpy0(s->cc, f[3], 4); s->kbps = atoi(f[4]); strncpy0(s->tags, f[5], 120); } }
        p = e ? e + 1 : 0; } free(t); }
static void cfg_save(void) { sb_t b = {0}; if (fixed_srv[0]) sb_f(&b, "servidor=%s\n", fixed_srv); sb_f(&b, "pais=%s\n", mycc);
    for (int i = 0; i < nfv; i++) { st_t *s = &FV[i]; sb_f(&b, "fav=%s\t%s\t%s\t%s\t%d\t%s\n", s->uuid, s->name, s->url, s->cc, s->kbps, s->tags); }
    z_mkdir("/sys/ZUNZUN"); z_writefile(CFG, b.p, b.n); free(b.p); z_sync(); }
static int fav_idx(const char *uuid) { for (int i = 0; i < nfv; i++) if (!strcmp(FV[i].uuid, uuid)) return i; return -1; }

/* ---------- catalogo (Radio Browser): varios servidores; si uno falla se prueba el siguiente ---------- */
static const char *SV[] = {"de1", "fi1", "de2", "nl1", "at1"}; static int sv0;
static int cat_get(const char *pq, char **out, long *n) {
    zhttp_opt o = {0, 0, 0, 0, 20000}; zhttp_info inf; int e = -1;
    for (int k = 0; k < (fixed_srv[0] ? 1 : 5); k++) { char u[600]; if (fixed_srv[0]) snprintf(u, 600, "%s%s", fixed_srv, pq); else snprintf(u, 600, "https://%s.api.radio-browser.info%s", SV[(sv0 + k) % 5], pq);
        e = z_http_ex("GET", u, 0, 0, 0, &o, out, n, &inf); if (!e && inf.status == 200) { sv0 = (sv0 + k) % 5; return 0; } free(*out); *out = 0; if (!e) e = -5; }
    snprintf(msg, 300, "%s: %s", tr("No se pudo leer el catálogo de emisoras"), z_http_err(e)); return e; }
static void load_stations(const char *filter, const char *title) {
    strncpy0(vtitle, title, 120); snprintf(msg, 300, "%s", tr("Buscando emisoras...")); draw();
    char pq[600]; snprintf(pq, 600, "/json/stations/search?codec=MP3&hidebroken=true&order=clickcount&reverse=true&limit=%d%s", MAXS, filter);
    char *b = 0; long n = 0; ns = 0; top = 0; sel = -1; if (cat_get(pq, &b, &n)) return; const char *e = b + n;
    for (int i = 0; ns < MAXS; i++) { const char *it = zj_item(b, e, i); if (!it) break; st_t *s = &S[ns]; memset(s, 0, sizeof *s);
        jstr(it, e, "stationuuid", s->uuid, 48); jstr(it, e, "name", s->name, 120); jstr(it, e, "url_resolved", s->url, 480); if (!s->url[0]) jstr(it, e, "url", s->url, 480);
        jstr(it, e, "countrycode", s->cc, 4); jstr(it, e, "tags", s->tags, 120); s->kbps = (int)jnum(zj_get(it, e, "bitrate"), e); s->clicks = jnum(zj_get(it, e, "clickcount"), e);
        char codec[16]; jstr(it, e, "codec", codec, 16); if (s->name[0] && s->url[0] && !strcasecmp(codec, "MP3")) ns++; }
    free(b); snprintf(msg, 300, tr("%d emisoras"), ns); char lg[200]; snprintf(lg, 200, "[prueba] radio emisoras %s: %d", title, ns); z_log(lg); }
static void load_cats(int countries) {
    snprintf(msg, 300, "%s", tr("Cargando...")); draw(); char *b = 0; long n = 0; nc = 0; top = 0; sel = -1;
    if (cat_get(countries ? "/json/countries?order=stationcount&reverse=true&hidebroken=true" : "/json/tags?order=stationcount&reverse=true&hidebroken=true&limit=200", &b, &n)) return;
    const char *e = b + n; for (int i = 0; nc < 300; i++) { const char *it = zj_item(b, e, i); if (!it) break; cat_t *c = &C[nc]; memset(c, 0, sizeof *c);
        jstr(it, e, "name", c->name, 80); jstr(it, e, "iso_3166_1", c->code, 4); c->n = jnum(zj_get(it, e, "stationcount"), e); if (c->name[0] && c->n > 0) nc++; }
    free(b); snprintf(msg, 300, countries ? tr("%d países") : tr("%d géneros"), nc); char lg[120]; snprintf(lg, 120, "[prueba] radio %s: %d", countries ? "paises" : "generos", nc); z_log(lg); }

/* ---------- reproduccion ---------- */
#define BCAP (3 << 20)
static u8 *B; static long bs, be;              /* deposito de MP3: datos validos en [bs, be) */
static zmp3d *dec; static short fb[2304]; static int fbn, fbi, rate, ch, fok, fcl, fcr; static long ffr, frames_dec;
static int playing, prebuf, has_card, vol = 80, stop_req, ended; static st_t cur, next_st; static int want_next;
static char title[200];                         /* lo que suena, segun los metadatos ICY */
static long icy, icy_left, meta_left; static char meta[4100]; static int meta_n, meta_ready; static long bytes_in; static u64 t_start;
static void meta_parse(void) { meta[meta_n] = 0; char *p = strstr(meta, "StreamTitle='"); if (!p) return; p += 13; char *q = strstr(p, "';"); if (!q) q = strrchr(p, '\''); if (!q) return; *q = 0;
    if (strcmp(title, p)) { strncpy0(title, p, 200); char lg[300]; snprintf(lg, 300, "[prueba] radio titulo: %s", title); z_log(lg); } }
/* siguiente muestra a la frecuencia de la emisora; 0 si falta MP3 en el deposito */
static int next_smp(int *l, int *r) {
    while (fbi >= fbn) { long av = be - bs; if (av < 4096 && !ended) return 0; if (av <= 0) return 0;
        int used = 0, hz = 0, c = 0; fbn = mp3d_frame(dec, B + bs, av, fb, &used, &hz, &c); if (!used) { if (ended) bs = be; return 0; } bs += used; fbi = 0;
        if (fbn > 0) { if (!rate || hz != rate || c != ch) { if (!rate) { char lg[200]; snprintf(lg, 200, "[prueba] radio sonando %s: %d Hz, %d canales", cur.name, hz, c); z_log(lg); } rate = hz; ch = c; } frames_dec++; } }
    *l = fb[fbi * ch]; *r = ch == 2 ? fb[fbi * ch + 1] : *l; fbi++; return 1; }
/* pasa a la tarjeta lo que quepa (48 kHz, estereo, 16 bits). Sin tarjeta se decodifica igual, al ritmo del reloj */
static void feed(void) {
    if (!playing) return; if (prebuf) { if (be - bs < 48 * 1024 && !ended) return; prebuf = 0; }
    static short o[4096];
    for (int round = 0; round < 64; round++) { if (has_card && z_audio_space() < 8192) break;
        if (!has_card) { static u64 t; u64 now = z_ms(); if (!t) t = now; if ((long)(now - t) < 40) break; t = now; }
        int n = 0; long step = rate ? ((long)rate << 16) / 48000 : 65536; if (!fok) fok = next_smp(&fcl, &fcr); if (!fok) { if (!ended) prebuf = 1; break; }
        while (n < 2048 && fok) { o[n * 2] = fcl; o[n * 2 + 1] = fcr; n++; ffr += step; while (ffr >= 65536 && fok) { ffr -= 65536; fok = next_smp(&fcl, &fcr); } }
        if (n && has_card) z_audio(o, n * 4); if (!fok && !ended) { prebuf = 1; break; } } }
static int st_cb(const char *d, long n, void *c) {
    if (!bytes_in) { zhttp_info *inf = c; icy = inf->icy > 0 && inf->icy < 1000000 ? inf->icy : 0; icy_left = icy; if (icy) { char lg[80]; snprintf(lg, 80, "[prueba] radio metadatos cada %ld bytes", icy); z_log(lg); } }
    bytes_in += n;
    for (long i = 0; i < n; ) {
        if (meta_left > 0 || (icy && icy_left == 0)) {   /* metadatos ICY: un byte de longitud (x16) y el texto */
            if (meta_left == 0 && !meta_ready) { meta_left = (u8)d[i++] * 16; meta_n = 0; meta_ready = 1; if (!meta_left) { meta_ready = 0; icy_left = icy; } continue; }
            long k = MIN(meta_left, n - i); if (meta_n + k < (long)sizeof meta) { memcpy(meta + meta_n, d + i, k); meta_n += k; } i += k; meta_left -= k;
            if (!meta_left) { meta_parse(); meta_ready = 0; icy_left = icy; } continue; }
        long k = icy ? MIN(icy_left, n - i) : n - i;
        if (bs > BCAP / 2) { memmove(B, B + bs, be - bs); be -= bs; bs = 0; }
        if (be + k > BCAP) { bs = be = 0; prebuf = 1; }   /* desbordado (no deberia pasar): se empieza de nuevo */
        memcpy(B + be, d + i, k); be += k; i += k; if (icy) icy_left -= k; }
    feed(); return stop_req; }
static int handle(zevent *e);
static int pump(void *c) { zevent e; while (z_event(&e, 0)) handle(&e); feed(); static u64 t; if (z_ms() - t > 500) { t = z_ms(); draw(); } return stop_req; }
static void play(st_t *s) {
    cur = *s; playing = 1; stop_req = 0; ended = 0; title[0] = 0; bs = be = 0; prebuf = 1; rate = ch = 0; fbn = fbi = 0; ffr = 0; fok = 0; frames_dec = 0; bytes_in = 0; t_start = z_ms();
    free(dec); dec = mp3d_new(); snprintf(msg, 300, "%s %s...", tr("Conectando con"), cur.name); draw();
    { char pq[120], *b = 0; long n = 0; snprintf(pq, 120, "/json/url/%s", cur.uuid); if (cur.uuid[0] && !cat_get(pq, &b, &n)) free(b); msg[0] = 0; }   /* la cuenta de escuchas que pide Radio Browser */
    char url[1100]; strncpy0(url, cur.url, 1100); int e = 0; zhttp_info inf;
    for (int hop = 0; hop < 4; hop++) {
        icy = 0; icy_left = 0; meta_left = 0; meta_ready = 0; zhttp_opt o = {"Icy-MetaData: 1\r\n", st_cb, pump, &inf, 15000}; char *rest = 0; long rl = 0; bytes_in = 0;
        /* el cb lee icy de inf con el primer dato: z_http_ex rellena las cabeceras antes de llamar al cb */
        e = z_http_ex("GET", url, 0, 0, 0, &o, &rest, &rl, &inf); free(rest);
        if (!e && inf.status >= 300 && inf.status < 400 && inf.final[0] && strcmp(inf.final, url)) { strncpy0(url, inf.final, 1100); continue; }
        break; }
    ended = 1; while (playing && !stop_req && be - bs > 0 && !e) { zevent ev; if (z_event(&ev, 20)) handle(&ev); feed(); if (fok == 0 && be - bs < 64) break; }
    char lg[300]; snprintf(lg, 300, "[prueba] radio parada %s: %ld bytes, %ld cuadros, %d", cur.name, bytes_in, frames_dec, e); z_log(lg);
    if (!stop_req && !want_next) snprintf(msg, 300, "%s: %s", tr("La emisora dejó de emitir"), e ? z_http_err(e) : inf.status != 200 ? tr("no está disponible") : tr("fin de la emisión"));
    playing = 0; stop_req = 0; }

/* ---------- ventana ---------- */
static const char *TABS[] = {"Populares", "Mi país", "Países", "Géneros", "Favoritas"};
static int tw(int i) { return g_textw(tr(TABS[i])) + 16; }
static int tx(int i) { int x = 8; for (int k = 0; k < i; k++) x += tw(k) + 6; return x; }
static int qx(void) { return tx(5) + 10; }
static int list_y(void) { return TB + 30; }
static int nrows(void) { return view == 2 || view == 3 ? nc : view == 4 ? nfv : ns; }
static st_t *row_st(int i) { return view == 4 ? &FV[i] : &S[i]; }
void draw(void) { g_clear(W, UI_WHITE); g_rect(W, 0, 0, W->w, TB, UI_PANEL);
    for (int i = 0; i < 5; i++) ui_button(W, tx(i), 7, tw(i), 26, TABS[i], (view == i || (i == 2 && view == 6 && vtitle[0] == '#') ) ? 2 : 0);
    int qw = MAX(120, W->w - qx() - 90); ui_input(W, qx(), 7, qw, 26, query, qcur, qfoc); ui_button(W, qx() + qw + 6, 7, 70, 26, "Buscar", 0);
    int y = TB + 6; char h[200]; if (view == 2) strcpy(h, tr("Elige un país")); else if (view == 3) strcpy(h, tr("Elige un género")); else if (view == 4) strcpy(h, tr("Tus emisoras favoritas")); else snprintf(h, 200, "%s", vtitle[0] == '#' ? vtitle + 1 : vtitle);
    g_textb(W, 12, y, h, UI_TEXT); y = list_y(); g_rect(W, 0, y - 2, W->w, 1, UI_LINE);
    int rows = (W->h - SB - y) / RH, n = nrows(); top = MAX(0, MIN(top, n - rows));
    for (int i = top; i < n && i < top + rows; i++) { int Y = y + (i - top) * RH; if (i == sel) g_rect(W, 0, Y, W->w, RH, UI_ACC2);
        if (view == 2 || view == 3) { cat_t *c = &C[i]; char t[40]; zw_clip(W, 12, Y, W->w - 140, RH); g_text(W, 12, Y + 4, c->name, UI_TEXT); zw_noclip(W); snprintf(t, 40, "%ld", c->n); g_text(W, W->w - 100, Y + 4, t, UI_MUTED); continue; }
        st_t *s = row_st(i); int on = playing && !strcmp(s->uuid, cur.uuid); if (on) g_circle(W, 18, Y + 12, 5, 0x16A34A, 1);
        if (fav_idx(s->uuid) >= 0) g_text(W, 30, Y + 4, "*", 0xD97706);
        zw_clip(W, 44, Y, W->w - 380, RH); g_text(W, 44, Y + 4, s->name, on ? UI_ACC : UI_TEXT); zw_noclip(W);
        zw_clip(W, W->w - 330, Y, 200, RH); g_text(W, W->w - 330, Y + 4, s->tags, UI_MUTED); zw_noclip(W);
        char t[40]; snprintf(t, 40, "%s  %d kbps", s->cc, s->kbps); g_text(W, W->w - 120, Y + 4, t, UI_MUTED); }
    if (!n && view != 2 && view != 3) g_text(W, 44, y + 6, tr(view == 4 ? "Aún no tienes favoritas: pulsa * mientras suena una emisora." : "No hay emisoras."), UI_MUTED);
    /* barra del reproductor */
    int by = W->h - SB; g_grad(W, 0, by, W->w, SB, 0x1F2937, 0x111827);
    if (playing) { g_textb(W, 12, by + 8, cur.name, 0xFFFFFF); char t[260]; if (title[0]) snprintf(t, 260, "%s", title); else if (prebuf) { char a[16]; snprintf(a, 16, "%ld KB", (be - bs) / 1024); snprintf(t, 260, "%s %s", tr("Cargando..."), a); } else snprintf(t, 260, "%s · %d kbps", cur.cc, cur.kbps);
        zw_clip(W, 12, by + 28, W->w - 340, 20); g_text(W, 12, by + 30, t, 0x5EEAD4); zw_noclip(W); }
    else g_text(W, 12, by + 22, msg[0] ? msg : tr("Elige una emisora de la lista."), 0x9CA3AF);
    int bx = W->w - 312; ui_button(W, bx, by + 17, 70, 30, "Parar", playing ? 2 : 0); ui_button(W, bx + 76, by + 17, 34, 30, "*", 0); ui_button(W, bx + 116, by + 17, 34, 30, "-", 0); ui_button(W, bx + 156, by + 17, 34, 30, "+", 0);
    char v[16]; snprintf(v, 16, "%d%%", vol); g_text(W, bx + 198, by + 24, v, 0xFFFFFF); if (!has_card) g_text(W, bx + 240, by + 24, tr("sin sonido"), 0xF87171);
    zw_present(W); }
static void toggle_fav(void) { if (!playing && sel < 0) return; st_t *s = playing ? &cur : row_st(sel); if (view == 2 || view == 3) return; int k = fav_idx(s->uuid);
    if (k >= 0) { memmove(&FV[k], &FV[k + 1], (nfv - k - 1) * sizeof *FV); nfv--; snprintf(msg, 300, "%s: %s", tr("Quitada de favoritas"), s->name); } else if (nfv < MAXS) { FV[nfv++] = *s; snprintf(msg, 300, "%s: %s", tr("Añadida a favoritas"), s->name); }
    cfg_save(); char lg[200]; snprintf(lg, 200, "[prueba] radio favoritas %d", nfv); z_log(lg); }
static void go_view(int v) { view = v; top = 0; sel = -1;
    if (v == 0) load_stations("", tr("Las más escuchadas del mundo"));
    else if (v == 1) { char f[40], t[80]; snprintf(f, 40, "&countrycode=%s", mycc); snprintf(t, 80, "%s (%s)", tr("Mi país"), mycc); load_stations(f, t); }
    else if (v == 2) load_cats(1); else if (v == 3) load_cats(0); else if (v == 4) { snprintf(msg, 300, tr("%d favoritas"), nfv); } }
static void search(void) { if (!query[0]) return; sb_t f = {0}; sb_s(&f, "&name="); enc(&f, query); char t[160]; snprintf(t, 160, "%s: %s", tr("Buscar"), query); view = 5; load_stations(f.p, t); free(f.p); qfoc = 0; }
static void pick_cat(int i) { cat_t *c = &C[i]; sb_t f = {0}; char t[120];
    if (view == 2) { if (c->code[0]) { sb_s(&f, "&countrycode="); enc(&f, c->code); strncpy0(mycc, c->code, 4); cfg_save(); } else { sb_s(&f, "&countryExact=true&country="); enc(&f, c->name); } snprintf(t, 120, "#%s", c->name); }
    else { sb_s(&f, "&tagExact=true&tag="); enc(&f, c->name); snprintf(t, 120, "%s: %s", tr("Género"), c->name); }
    view = 6; load_stations(f.p, t); free(f.p); }
/* devuelve 1 si hay que cortar la emision en curso (otra emisora, Parar o cerrar) */
static int handle(zevent *e) {
    if (e->type == EV_REDRAW) { draw(); return 0; } if (e->win != (u32)W->id) return 0;
    if (e->type == EV_CLOSE) { if (playing) { stop_req = 1; } z_exit(0); }
    if (e->type == EV_RESIZE) { zw_resized(W, e->x, e->y); draw(); return 0; }
    if (e->type == EV_WHEEL) { top -= (int)e->key * 3; draw(); return 0; }
    if (e->type == EV_KEY) { if (qfoc) { if (e->ch == '\n') search(); else ui_input_key(query, 110, &qcur, e); } else if (e->ch == ' ' && playing) stop_req = 1; draw(); return 0; }
    if (e->type != EV_DOWN) return 0; int by = W->h - SB, bx = W->w - 312;
    if (e->y < TB) { int qw = MAX(120, W->w - qx() - 90); if (e->x >= qx() && e->x < qx() + qw) { qfoc = 1; ui_input_click(W, e, query, &qcur); } else if (e->x >= qx() + qw + 6 && e->x < qx() + qw + 76) search();
        else for (int i = 4; i >= 0; i--) if (e->x >= tx(i)) { if (e->x < tx(i) + tw(i)) { qfoc = 0; go_view(i); } break; } draw(); return 0; }
    qfoc = 0;
    if (e->y >= by + 17 && e->y < by + 47) { if (e->x >= bx && e->x < bx + 70) { if (playing) stop_req = 1; } else if (e->x >= bx + 76 && e->x < bx + 110) toggle_fav();
        else if (e->x >= bx + 116 && e->x < bx + 150) z_audio_vol(vol = MAX(0, vol - 10)); else if (e->x >= bx + 156 && e->x < bx + 190) z_audio_vol(vol = MIN(100, vol + 10)); draw(); return 0; }
    if (e->y >= list_y() && e->y < by) { int i = top + (e->y - list_y()) / RH; if (i < nrows()) { sel = i;
            if (view == 2 || view == 3) pick_cat(i); else { next_st = *row_st(i); want_next = 1; if (playing) stop_req = 1; } } draw(); }
    return 0; }
int main(const char *a) {
    zsys s; z_sysinfo(&s); W = zw_new("Radio", -1, 0, MIN(960, (int)s.scr_w - 40), MIN(640, (int)s.scr_h - 80), WF_DECOR | WF_RESIZE); if (!W) return 1;
    S = malloc(MAXS * sizeof *S); FV = malloc(MAXS * sizeof *FV); C = malloc(300 * sizeof *C); B = malloc(BCAP);
    char card[64]; has_card = z_audio_info(card, 64); vol = z_audio_vol(-1); cfg_load(); if (!fixed_srv[0]) sv0 = (int)(z_ms() % 5);
    z_log("[prueba] radio abierta"); go_view(nfv ? 4 : 0); draw();
    { int sx, sy; zw_screen(W, 200, list_y() + RH / 2, &sx, &sy); int px, py; zw_screen(W, W->w - 312 + 35, W->h - SB + 32, &px, &py); int fx, fy; zw_screen(W, W->w - 312 + 93, W->h - SB + 32, &fx, &fy); int t2x, t2y; zw_screen(W, tx(2) + tw(2) / 2, 20, &t2x, &t2y);
        char lg[200]; snprintf(lg, 200, "[prueba] radio lista (fila0 en %d,%d) (parar en %d,%d) (favorita en %d,%d) (paises en %d,%d)", sx, sy, px, py, fx, fy, t2x, t2y); z_log(lg); }
    zevent e; for (;;) { if (want_next) { want_next = 0; play(&next_st); draw(); continue; } if (!z_event(&e, -1)) continue; handle(&e); }
}
