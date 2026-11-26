/* Clonar: copia exacta de una unidad entera en otra, sector a sector.
   Sirve para cambiar el disco de un equipo por otro (la copia arranca igual que el original) o para duplicar una
   memoria USB. Se copian todos los sectores, tambien los del arranque y el espacio libre; solo se tocan:
   - con GPT y un destino de otro tamano, la cabecera y su copia de seguridad, que tiene que estar al final del disco;
   - con un destino menor, el espacio sin particiones del final, que no cabe (solo si todas las particiones caben).
   Mientras se copia, el origen queda en solo lectura (si es el sistema en uso, nada puede escribir en el) y el
   destino se desmonta. Los sectores que no se pueden leer se reintentan uno a uno y quedan a cero (se avisa).
   Al terminar se puede comprobar la copia: se vuelve a leer el destino y se compara con lo escrito (CRC por bloque). */
#include "z.h"
#define CH (1 << 20)
#define WW 760
#define WH 640
#define LX 16
#define RX 392
#define CW 352
#define TOP 90
#define RH 44
#define NR 8
static zwin *W; static zdisk D[16]; static int nd, so = -1, de = -1, check = 1, busy, cancel, done, ok_res; static char sysd[16], msgs[200], res[4][200], LB[16][40];
static int pct = -1; static u64 t_last;
typedef struct {
    int ok, kind, gfix; u64 S, T, n, end; u32 ss, es; char why[200], l1[200], l2[200], l3[200];
    u8 head[2 * 4096]; u8 *bk; u64 bk_lba, z0, z1;   /* z0..z1: copia vieja de la GPT del origen (en medio del destino mayor) */
} plan_t;
static plan_t P;

static void hum(u64 b, char *o) { if (b >> 30) snprintf(o, 20, "%lu.%lu GB", b >> 30, ((b >> 20) & 1023) * 10 / 1024); else if (b >> 20) snprintf(o, 20, "%lu MB", b >> 20); else snprintf(o, 20, "%lu KB", b >> 10); }
static u32 crc(const u8 *p, long n) { return z_crc32(p, n); }
static int is_sys(int i) { return i >= 0 && !strcmp(D[i].name, sysd); }
static void load(void) {
    nd = z_disks(D, 16); dk_sys_disk(sysd); if (so >= nd) so = -1; if (de >= nd) de = -1;
    zmount m[32]; int nm = z_mounts(m, 32);
    for (int i = 0; i < nd; i++) { LB[i][0] = 0; int l = strlen(D[i].name);
        for (int k = 0; k < nm; k++) if (!strncmp(m[k].dev, D[i].name, l) && (m[k].dev[l] == 0 || m[k].dev[l] == 'p') && m[k].label[0] && strlen(LB[i]) + strlen(m[k].label) < 36) { if (LB[i][0]) strcat(LB[i], ", "); strcat(LB[i], m[k].label); } }
}

/* ---------- plan: que se copia y que se ajusta ---------- */
static u64 g64(const u8 *p) { return *(const u64 *)p; } static u32 g32(const u8 *p) { return *(const u32 *)p; }
static void p64(u8 *p, u64 v) { *(u64 *)p = v; } static void p32(u8 *p, u32 v) { *(u32 *)p = v; }
static int gpt_hdr_ok(const u8 *h, u32 ss) { if (memcmp(h, "EFI PART", 8)) return 0; u32 hs = g32(h + 12); if (hs < 92 || hs > ss) return 0; u8 t[4096]; memcpy(t, h, hs); p32(t + 16, 0); return crc(t, hs) == g32(h + 16); }
static void hdr_crc(u8 *h) { u32 hs = g32(h + 12); p32(h + 16, 0); p32(h + 16, crc(h, hs)); }
static int mkplan(void) {
    plan_t *p = &P; free(p->bk); memset(p, 0, sizeof *p); char a[20], b[20];
    if (so < 0 || de < 0) { strcpy(p->why, _("Elige el origen y el destino.")); return 0; }
    zdisk *s = &D[so], *t = &D[de]; p->S = s->sectors; p->T = t->sectors; p->ss = s->ssize;
    if (so == de) { strcpy(p->why, _("El origen y el destino tienen que ser unidades distintas.")); return 0; }
    if (is_sys(de)) { strcpy(p->why, _("El destino es la unidad del sistema en uso: no se puede borrar.")); return 0; }
    if (!strncmp(t->name, "cd", 2) || t->ssize == 2048) { strcpy(p->why, _("En esa unidad no se puede escribir.")); return 0; }
    if (s->ssize != t->ssize || s->ssize > 4096) { snprintf(p->why, 200, _("Los sectores son de distinto tamano (%u y %u bytes): la copia no arrancaria."), s->ssize, t->ssize); return 0; }
    u32 ss = p->ss; u8 *h = p->head; if (z_disk_read(s->name, 0, 2, h)) { strcpy(p->why, _("No se puede leer el principio del origen.")); return 0; }
    int mbr = h[510] == 0x55 && h[511] == 0xAA, pe = -1; u64 end = 0;
    if (mbr) for (int i = 0; i < 4; i++) { u8 *e = h + 446 + i * 16; if (e[4] == 0xEE) pe = i; if (e[4]) end = MAX(end, (u64)g32(e + 8) + g32(e + 12)); }
    u8 *g = h + ss; int gpt = 0;
    if (pe >= 0 && gpt_hdr_ok(g, ss)) {
        u64 el = g64(g + 72); u32 num = g32(g + 80), esz = g32(g + 84); u64 bytes = (u64)num * esz;
        if (esz >= 128 && bytes && bytes <= (1 << 20) && el >= 2) {
            p->es = (bytes + ss - 1) / ss; p->bk = malloc((p->es + 1) * ss); memset(p->bk, 0, (p->es + 1) * ss);
            if (!z_disk_read(s->name, el, p->es, p->bk) && crc(p->bk, bytes) == g32(g + 88)) { gpt = 1; end = 0;
                for (u32 i = 0; i < num; i++) { u8 *e = p->bk + (u64)i * esz; if (g64(e) || g64(e + 8)) end = MAX(end, g64(e + 40) + 1); } }
        }
    }
    p->kind = gpt ? 2 : mbr && end && pe < 0 ? 1 : 0;   /* GPT danada: solo copia entera */ p->end = p->kind ? end : p->S;
    if (p->end > p->S) { strcpy(p->why, _("La tabla de particiones del origen pasa del final del disco: no se puede copiar con seguridad.")); return 0; }
    if (p->T >= p->S) p->n = p->S;
    else if (p->kind == 2 && p->end + p->es + 1 <= p->T) p->n = p->T - 1 - p->es;
    else if (p->kind == 1 && p->end <= p->T) p->n = p->T;
    else { hum(p->T * ss, a); hum((p->kind ? p->end + (p->kind == 2 ? p->es + 1 : 0) : p->S) * ss, b); snprintf(p->why, 200, _("No cabe: el destino tiene %s y hacen falta al menos %s."), a, b); return 0; }
    if (gpt && p->T != p->S) {   /* la GPT dice donde acaba el disco: la cabecera, su copia y el MBR protector se ajustan */
        p->gfix = 1; u64 T = p->T; p64(g + 32, T - 1); p64(g + 48, T - 2 - p->es); hdr_crc(g);
        u8 *e = h + 446 + pe * 16; u32 old = g32(e + 12); if (g32(e + 8) == 1 && old >= MIN(p->S - 1, 0xFFFFFFFFULL)) p32(e + 12, (u32)MIN(T - 1, 0xFFFFFFFFULL));
        u8 *bh = p->bk + p->es * ss; memcpy(bh, g, ss); p64(bh + 24, T - 1); p64(bh + 32, 1); p64(bh + 72, T - 1 - p->es); hdr_crc(bh); p->bk_lba = T - 1 - p->es;
        if (p->T > p->S) { p->z0 = p->S - 1 - p->es; p->z1 = p->S; }   /* la copia vieja queda en el espacio libre: se borra para que nadie la confunda */
    }
    hum(p->S * ss, a); hum(p->n * ss, b);
    if (p->n == p->S) snprintf(p->l1, 200, _("Se copia el disco entero (%s), sector a sector: la copia sera identica."), a);
    else snprintf(p->l1, 200, _("Destino menor, pero las particiones caben: se copian %s de %s."), b, a);
    if (p->T > p->S) { hum((p->T - p->S) * ss, a); snprintf(p->l2, 200, _("El destino es %s mayor: ese espacio queda libre al final."), a); }
    if (p->gfix) strcpy(p->l2[0] ? p->l3 : p->l2, _("La tabla GPT se ajusta al tamano del destino."));
    if (is_sys(so)) strcpy(p->l2[0] ? p->l3 : p->l2, _("Es el sistema en uso: mientras se copia no se podra guardar nada en el."));
    return p->ok = 1;
}

/* ---------- dibujo ---------- */
static void row(int col, int i, int y) {
    int x = col ? RX : LX, sel = col ? de == i : so == i; char a[20], b[160]; hum(D[i].sectors * D[i].ssize, a);
    int bad = col && (is_sys(i) || i == so || !strncmp(D[i].name, "cd", 2));
    g_round(W, x, y, CW, RH - 4, 4, sel ? UI_ACC : UI_LINE); g_round(W, x + 1, y + 1, CW - 2, RH - 6, 3, sel ? UI_ACC2 : bad ? UI_PANEL : UI_WHITE);
    g_textb(W, x + 10, y + 5, D[i].name, bad ? UI_MUTED : UI_TEXT); g_text(W, x + 90, y + 5, a, UI_MUTED);
    const char *tag = is_sys(i) ? _("sistema en uso") : D[i].removable ? _("extraible") : _("interno"); g_text(W, x + CW - 10 - g_textw(tag), y + 5, tag, is_sys(i) ? 0xB91C1C : UI_MUTED);
    snprintf(b, 160, "%s%s%s", D[i].model, LB[i][0] ? "  -  " : "", LB[i]); zw_clip(W, x + 10, y, CW - 20, RH - 4); g_text(W, x + 10, y + 23, b, UI_MUTED); zw_noclip(W);
}
static void draw(void) {
    g_clear(W, UI_BG); g_grad(W, 0, 0, W->w, 54, 0x0E7490, 0x155E75); g_text2(W, 16, 11, "Clonar disco", 0xFFFFFF, 2);
    g_text(W, 230, 20, "Copia exacta de una unidad en otra", 0xCFFAFE);
    g_textb(W, LX, 66, "1. Origen (lo que se copia)", UI_TEXT); g_textb(W, RX, 66, "2. Destino (se borra entero)", 0xB91C1C);
    if (!nd) g_text(W, LX, TOP, "No hay discos", UI_TEXT);
    for (int i = 0; i < nd && i < NR; i++) { row(0, i, TOP + i * RH); row(1, i, TOP + i * RH); }
    int y = TOP + NR * RH + 4;
    if (done) { for (int i = 0; i < 4; i++) if (res[i][0]) g_text(W, LX, y + i * 18, res[i], i == 0 ? (ok_res ? 0x15803D : 0xB91C1C) : UI_TEXT); }
    else if (P.ok) { const char *L[4] = {P.l1, P.l2, P.l3, "Despues, no arranques con las dos unidades conectadas: son identicas."}; int k = 0; for (int i = 0; i < 4; i++) if (L[i][0]) g_text(W, LX, y + 18 * k++, L[i], i == 3 ? UI_MUTED : UI_TEXT); }
    else if (so >= 0 || de >= 0) g_text(W, LX, y, P.why, so >= 0 && de >= 0 ? 0xB91C1C : UI_MUTED);
    y += 76; g_frame(W, LX, y, 16, 16, UI_LINE); if (check) { g_rect(W, LX + 4, y + 4, 8, 8, UI_ACC); } g_text(W, LX + 24, y, "Comprobar la copia al terminar (tarda mas, pero asegura que es identica)", UI_TEXT);
    int by = W->h - 46; if (pct >= 0) { g_rect(W, LX, by - 34, W->w - 2 * LX, 12, UI_LINE); g_rect(W, LX, by - 34, (W->w - 2 * LX) * pct / 1000, 12, UI_ACC); }
    g_text(W, LX, by - 16, msgs, UI_TEXT);
    if (busy) ui_button(W, W->w - 148, by, 132, 32, "Cancelar", 0); else ui_button(W, W->w - 148, by, 132, 32, "Clonar", P.ok ? 2 : 0);
    zw_present(W);
}

/* ---------- copia ---------- */
static void eta(char *o, u64 left_ms) { u64 s = left_ms / 1000; if (s >= 3600) snprintf(o, 40, _("quedan %lu h %02lu min"), s / 3600, s / 60 % 60); else if (s >= 60) snprintf(o, 40, _("quedan %lu min"), (s + 59) / 60); else snprintf(o, 40, _("quedan %lu s"), s); }
static void pump(void) {   /* sin bloquear: Cancelar y cerrar la ventana */
    zevent e; while (z_event(&e, 0)) { if (e.type == EV_REDRAW) draw(); if (e.win != (u32)W->id) continue;
        int by = W->h - 46; if ((e.type == EV_DOWN && e.y >= by && e.x >= W->w - 148) || e.type == EV_CLOSE) { if (ui_confirm("Parar la copia", "¿Parar la copia? El destino quedara incompleto y no servira.")) cancel = 1; draw(); } }
}
static void show(const char *ph, u64 dn, u64 tot, u64 t0) {
    u64 now = z_ms(); if (now - t_last < 300 && dn < tot) return; t_last = now; pct = (int)(dn * 1000 / (tot ? tot : 1));
    char a[20], b[20], r[40] = ""; hum(dn, a); hum(tot, b); u64 el = now - t0; u64 sp = el ? dn * 1000 / el : 0; if (sp && dn < tot) eta(r, (tot - dn) / (sp ? sp : 1) * 1000);
    snprintf(msgs, 200, "%s %s / %s   %lu MB/s   %s", _(ph), a, b, sp >> 20, r); draw(); pump();
}
static u64 bad, badl[6]; static const char *fail;
static int rd(const char *n, u64 lba, u32 c, u8 *buf, u32 ss) {   /* 0 bien; -1 el origen ha desaparecido; -2 cancelado */
    for (int t = 0; t < 2; t++) { int r = z_disk_read(n, lba, c, buf); if (!r) return 0; if (r == -2) return -1; }
    for (u32 i = 0; i < c; i++) { u8 *p = buf + (u64)i * ss; int ok = 0, r = 0;
        for (int t = 0; t < 3 && !ok; t++) { r = z_disk_read(n, lba + i, 1, p); ok = !r; if (r == -2) return -1; }
        if (!ok) { memset(p, 0, ss); if (bad < 6) badl[bad] = lba + i; bad++; char m[80]; snprintf(m, 80, "[prueba] clonar sector ilegible %lu", lba + i); z_log(m); }
        if (i % 64 == 63) { pump(); if (cancel) return -2; } }
    return 0;
}
static void result(int ok, const char *a, const char *b, const char *c) { ok_res = ok; done = 1; strncpy0(res[0], a, 200); strncpy0(res[1], b ? b : "", 200); strncpy0(res[2], c ? c : "", 200); res[3][0] = 0; }
static void clone(void) {
    if (!P.ok || busy) return; zdisk s = D[so], t = D[de]; char a[20], q[300], c[16] = ""; hum(t.sectors * t.ssize, a);
    snprintf(q, 300, _("Se borrara TODO %s (%s, %s) y quedara igual que %s. Escribe BORRAR:"), t.name, t.model, a, s.name);
    if (!ui_prompt("Clonar disco", q, c, 16) || strcmp(c, _("BORRAR"))) { strcpy(msgs, _("Cancelado")); return; }
    busy = 1; cancel = 0; done = 0; bad = 0; fail = 0; strcpy(msgs, _("Preparando...")); pct = 0; draw();
    /* el origen, en solo lectura mientras se copia (lo que estaba pendiente se escribe antes); el destino, desmontado */
    z_sync(); z_disk_ctl(2, s.name); z_disk_ctl(0, t.name); long wc0 = z_disk_ctl(1, s.name);
    char lg[200]; int e = 0; u64 per = CH / s.ssize, nch = 0; u32 *crcs = 0; u8 *buf = malloc(CH);
    if (!mkplan()) { e = 1; fail = P.why; } else { nch = (P.n + per - 1) / per; crcs = malloc(nch * 4 + 4); if (!crcs || !buf) { e = 1; fail = _("No hay memoria suficiente."); } }
    u32 ss = P.ss; u64 t0 = z_ms(), tot = P.n * ss; t_last = 0; static char fb[200]; char *ph = check ? "1/2 Copiando" : "Copiando";
    snprintf(lg, 200, "[prueba] clonar empieza %s -> %s: %lu sectores, tipo %d, ajuste gpt %d", s.name, t.name, P.n, P.kind, P.gfix); z_log(lg);
    for (u64 k = 0; k < nch && !e; k++) {
        u64 lba = k * per; u32 cnt = (u32)MIN(per, P.n - lba); int r = rd(s.name, lba, cnt, buf, ss);
        if (r == -1) { e = 1; fail = _("El origen ha dejado de responder (¿se ha desconectado?)."); break; } if (r == -2 || cancel) { e = 2; break; }
        if (lba == 0 && P.gfix) memcpy(buf, P.head, 2 * ss);   /* MBR protector y cabecera GPT ajustados */
        if (P.z1) for (u64 x = MAX(P.z0, lba); x < MIN(P.z1, lba + cnt); x++) memset(buf + (x - lba) * ss, 0, ss);
        crcs[k] = crc(buf, (long)cnt * ss);
        int w = z_disk_write(t.name, lba, cnt, buf); if (w && w != -2) w = z_disk_write(t.name, lba, cnt, buf);
        if (w) { e = 1; snprintf(fb, 200, w == -2 ? _("El destino ha dejado de responder (¿se ha desconectado?).") : _("Error al escribir en el destino cerca del sector %lu: puede estar estropeado."), lba); fail = fb; break; }
        show(ph, (lba + cnt) * ss, tot, t0); if (cancel) { e = 2; break; }
    }
    if (!e && P.gfix && z_disk_write(t.name, P.bk_lba, P.es + 1, P.bk)) { e = 1; fail = _("No se pudo escribir la copia de seguridad de la GPT al final del destino."); }
    z_sync(); long wc1 = z_disk_ctl(1, s.name); int changed = wc1 != wc0;
    z_disk_ctl(3, s.name);
    u64 copy_ms = z_ms() - t0; int verified = 0, diff = 0; u64 dlba = 0;
    if (!e && check) {   /* comprobar: se vuelve a leer el destino y se compara con lo que se escribio */
        u64 t1 = z_ms(); t_last = 0;
        for (u64 k = 0; k < nch; k++) { u64 lba = k * per; u32 cnt = (u32)MIN(per, P.n - lba); int r = z_disk_read(t.name, lba, cnt, buf); if (r) r = z_disk_read(t.name, lba, cnt, buf);
            if (r || crc(buf, (long)cnt * ss) != crcs[k]) { diff = 1; dlba = lba; break; } show("2/2 Comprobando", (lba + cnt) * ss, tot, t1); if (cancel) { e = 2; break; } }
        if (!e && !diff && P.gfix) { u8 *b2 = malloc((P.es + 1) * ss); if (z_disk_read(t.name, P.bk_lba, P.es + 1, b2) || memcmp(b2, P.bk, (P.es + 1) * ss)) { diff = 1; dlba = P.bk_lba; } free(b2); }
        if (!e) verified = !diff;
    }
    z_disk_rescan(t.name); free(buf); free(crcs); busy = 0; pct = -1; msgs[0] = 0; load();
    char x[200], y2[200] = "", z[200] = ""; u64 secs = copy_ms / 1000;
    if (e == 2) result(0, _("Copia cancelada: el destino esta incompleto y no sirve."), _("Vuelve a clonar o formatea el destino con Discos."), 0);
    else if (e) result(0, _("La copia no ha terminado."), fail, 0);
    else if (diff) { snprintf(x, 200, _("La comprobacion ha encontrado diferencias cerca del sector %lu: el destino no es fiable."), dlba); result(0, x, _("Prueba con otro cable, otro puerto u otra unidad de destino."), 0); }
    else {
        hum(tot, a); snprintf(x, 200, verified ? _("Copia exacta terminada y comprobada: %s en %lu min %02lu s.") : _("Copia exacta terminada: %s en %lu min %02lu s."), a, secs / 60, secs % 60);
        if (bad == 1) snprintf(y2, 200, _("Atencion: un sector del origen no se pudo leer y queda a cero (el %lu)."), badl[0]);
        else if (bad) snprintf(y2, 200, _("Atencion: %lu sectores ilegibles del origen quedan a cero (el primero: %lu)."), bad, badl[0]);
        else if (changed) strcpy(y2, _("Atencion: algo escribio en el origen durante la copia; conviene repetirla."));
        else strcpy(y2, _("Ya puedes cambiar el disco: la copia arranca igual que el original."));
        strcpy(z, _("No arranques con las dos unidades conectadas a la vez: son identicas."));
        result(!bad && !changed, x, y2, z);
    }
    snprintf(lg, 200, "[prueba] clonar %s -> %s: %s (%lu sectores, %lu ilegibles, comprobada %d, origen cambiado %d)", s.name, t.name, e == 2 ? "cancelada" : e ? "error" : diff ? "diferencias" : "OK", P.n, bad, verified, changed); z_log(lg);
    if (e == 1 && fail) { snprintf(lg, 200, "[prueba] clonar fallo: %s", fail); z_log(lg); }
    mkplan();
}
static void logpos(void) {
    char lg[200]; int sx, sy;
    for (int i = 0; i < nd && i < NR; i++) { zw_screen(W, LX + CW / 2, TOP + i * RH + 20, &sx, &sy); snprintf(lg, 200, "[prueba] clonar origen %s en %d,%d", D[i].name, sx, sy); z_log(lg); zw_screen(W, RX + CW / 2, TOP + i * RH + 20, &sx, &sy); snprintf(lg, 200, "[prueba] clonar destino %s en %d,%d", D[i].name, sx, sy); z_log(lg); }
    int cx, cy, bx, bpy; zw_screen(W, LX + 8, TOP + NR * RH + 4 + 76 + 8, &cx, &cy); zw_screen(W, W->w - 148 + 66, W->h - 46 + 16, &bx, &bpy);
    snprintf(lg, 200, "[prueba] clonar lista %d discos (comprobar en %d,%d) (boton en %d,%d)", nd, cx, cy, bx, bpy); z_log(lg);
}
int main(const char *a) {
    W = zw_new("Clonar", -1, 0, WW, WH, WF_DECOR); if (!W) return 1; load(); mkplan(); draw(); logpos(); zevent e; int lastn = nd;
    for (;;) {
        if (!z_event(&e, 3000)) { if (!busy) { load(); if (nd != lastn) { lastn = nd; so = de = -1; done = 0; logpos(); } mkplan(); draw(); } continue; }
        if (e.type == EV_REDRAW) { draw(); continue; } if (e.win != (u32)W->id) continue; if (e.type == EV_CLOSE) return 0;
        if (e.type != EV_DOWN) continue;
        int by = W->h - 46, cy = TOP + NR * RH + 4 + 76;
        if (e.y >= TOP && e.y < TOP + NR * RH) { int i = (e.y - TOP) / RH; if (i < nd) { if (e.x >= LX && e.x < LX + CW) { so = i; done = 0; } else if (e.x >= RX && e.x < RX + CW) { de = i; done = 0; }
            if (!mkplan() && so >= 0 && de >= 0) { char lg[260]; snprintf(lg, 260, "[prueba] clonar no se puede %s -> %s: %s", D[so].name, D[de].name, P.why); z_log(lg); } } }
        else if (e.y >= cy - 2 && e.y < cy + 18 && e.x < W->w - 200) check = !check;
        else if (e.y >= by && e.x >= W->w - 148) clone();
        draw();
    }
}
