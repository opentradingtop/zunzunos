/* Grabadora de audio: 48 kHz, 16 bits, en estereo o mono, y en WAV (sin perdida) o MP3 (comprimido, de 64 a 320 kbps).
   Tambien convierte a MP3 grabaciones WAV que ya existan (de cualquier frecuencia; las que no son de MPEG-1 se
   pasan a 48 kHz). Las opciones se guardan en /sys/ZUNZUN/GRABADORA.TXT. */
#include "z.h"
#define CFG "/sys/ZUNZUN/GRABADORA.TXT"
typedef struct { char n[120]; u64 s; int sec; } it_t;
static zwin *W; static it_t L[200]; static int n, sel = -1, top, rec, pk[2], f = -1, gain = 100; static char dir[160], cur[260], msg[100]; static u64 dbytes, t0; static short *buf, *outb; static u8 *mb;
static int chans = 2, mp3 = 0, kbi = 2; static zmp3 *enc;
static const int KB[7] = { 64, 96, 128, 160, 192, 256, 320 };
static void cfg_save(void) { char b[120]; snprintf(b, 120, "canales=%d\nformato=%s\nkbps=%d\n", chans, mp3 ? "mp3" : "wav", KB[kbi]); z_writefile(CFG, b, strlen(b)); }
static void cfg_load(void) { char *t = z_readfile(CFG, 0); if (!t) return; char *p;
    if ((p = strstr(t, "canales="))) chans = p[8] == '1' ? 1 : 2; if ((p = strstr(t, "formato="))) mp3 = !strncmp(p + 8, "mp3", 3);
    if ((p = strstr(t, "kbps="))) { int k = atoi(p + 5); for (int i = 0; i < 7; i++) if (KB[i] == k) kbi = i; } free(t); }
/* duracion en segundos leyendo la cabecera: WAV (bytes por segundo) o MP3 (velocidad del primer cuadro) */
static int dur(const char *p, u64 size) {
    int fd = z_open(p, O_READ); if (fd < 0) return -1; u8 h[64]; long r = z_read(fd, h, 64); z_close(fd); if (r < 12) return -1;
    if (!memcmp(h, "RIFF", 4) && r >= 44) { u32 br = *(u32 *)(h + 28); return br ? (int)((size - 44) / br) : -1; }
    if (h[0] == 0xFF && (h[1] & 0xE0) == 0xE0) { static const short K1[16] = { 0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0 }, K2[16] = { 0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0 };
        int k = (h[1] & 8 ? K1 : K2)[h[2] >> 4]; return k ? (int)(size * 8 / (k * 1000)) : -1; }
    return -1; }
static int cmp(const void *a, const void *b) { return strcmp(((it_t *)b)->n, ((it_t *)a)->n); }
static void load(void) { n = 0; zdirent d; for (int i = 0; n < 200 && !z_readdir(dir, i, &d); i++) if (z_ends(d.name, ".wav") || z_ends(d.name, ".mp3")) { strncpy0(L[n].n, d.name, 120); L[n].s = d.size; char p[300]; snprintf(p, 300, "%s/%s", dir, d.name); L[n].sec = dur(p, d.size); n++; } qsort(L, n, sizeof *L, cmp); if (sel >= n) sel = -1; }
static void hdr(u8 *h, u32 l, int ch) { memcpy(h, "RIFF", 4); *(u32 *)(h + 4) = 36 + l; memcpy(h + 8, "WAVEfmt ", 8); *(u32 *)(h + 16) = 16; *(u16 *)(h + 20) = 1; *(u16 *)(h + 22) = ch; *(u32 *)(h + 24) = 48000; *(u32 *)(h + 28) = 48000 * 2 * ch; *(u16 *)(h + 32) = 2 * ch; *(u16 *)(h + 34) = 16; memcpy(h + 36, "data", 4); *(u32 *)(h + 40) = l; }
static int sel_wav(void) { return sel >= 0 && z_ends(L[sel].n, ".wav"); }
static void meter(int y, int h, int v) { g_rect(W, 20, y, W->w - 40, h, 0x374151); int w = (W->w - 40) * MIN(v, 32767) / 32767; g_rect(W, 20, y, w, h, v > 28000 ? 0xEF4444 : 0x22C55E); }
static void draw(void) {
    g_clear(W, UI_BG); g_rect(W, 0, 0, W->w, 150, 0x111827); u64 sec = rec ? (z_ms() - t0) / 1000 : 0; char b[60]; snprintf(b, 60, "%02lu:%02lu", sec / 60, sec % 60);
    g_text2(W, 20, 18, b, rec ? 0xFCA5A5 : 0xE5E7EB, 4); g_circle(W, 196, 50, 12, rec && (z_ms() & 512) ? 0xEF4444 : 0x4B5563, 1);
    ui_button(W, 230, 16, 100, 34, rec ? "Parar" : "Grabar", 2); ui_button(W, 338, 16, 40, 34, "-", 0); ui_button(W, 382, 16, 40, 34, "+", 0); snprintf(b, 60, _("Ganancia %d%%"), gain); g_text(W, 240, 58, b, 0xD1D5DB);
    ui_button(W, 20, 78, 100, 28, chans == 2 ? "Estereo" : "Mono", rec ? 0 : 1); ui_button(W, 128, 78, 70, 28, mp3 ? "MP3" : "WAV", rec ? 0 : 1);
    if (mp3) { snprintf(b, 60, "%d kbps", KB[kbi]); ui_button(W, 206, 78, 90, 28, b, rec ? 0 : 1); }
    g_text(W, mp3 ? 306 : 208, 84, mp3 ? (chans == 2 ? _("comprimido") : _("comprimido, mono")) : _("sin perdida"), 0x9CA3AF);
    if (chans == 2) { meter(116, 8, pk[0]); meter(128, 8, pk[1]); } else meter(118, 12, pk[0]);
    int vis = (W->h - 196) / 22; for (int i = top; i < n && i < top + vis; i++) { int y = 156 + (i - top) * 22; if (i == sel) g_rect(W, 0, y, W->w, 22, UI_ACC2); g_text(W, 14, y + 3, L[i].n, UI_TEXT);
        if (L[i].sec >= 0) snprintf(b, 60, "%d:%02d  %lu KB", L[i].sec / 60, L[i].sec % 60, L[i].s >> 10); else snprintf(b, 60, "%lu KB", L[i].s >> 10); g_text(W, W->w - 14 - g_textw(b), y + 3, b, UI_MUTED); }
    int by = W->h - 36; ui_button(W, 12, by, 84, 28, "Escuchar", sel >= 0 ? 2 : 0); ui_button(W, 102, by, 74, 28, "Borrar", 0); ui_button(W, 182, by, 80, 28, "A MP3", sel_wav() ? 1 : 0); g_text(W, 272, by + 6, msg, UI_MUTED); zw_present(W); }
static void start(void) {
    int e = z_rec_start(); if (e) { strcpy(msg, _("Este equipo no tiene entrada de audio")); return; } u32 t[6]; z_time(t);
    snprintf(cur, 260, "%s/voz-%u%02u%02u-%02u%02u%02u.%s", dir, t[5], t[4], t[3], t[2], t[1], t[0], mp3 ? "mp3" : "wav");
    if (mp3 && !(enc = mp3e_new(48000, chans, KB[kbi]))) { z_rec_stop(); strcpy(msg, _("Formato no valido")); return; }
    f = z_open(cur, O_WRITE | O_CREATE | O_TRUNC); if (f < 0) { z_rec_stop(); mp3e_free(enc); enc = 0; strcpy(msg, z_errstr(f)); return; }
    if (!mp3) { u8 h[44]; hdr(h, 0, chans); z_write(f, h, 44); }
    dbytes = 0; rec = 1; t0 = z_ms(); pk[0] = pk[1] = 0; strcpy(msg, _("Grabando..."));
    char m[80]; snprintf(m, 80, "[prueba] grabacion iniciada %s %d", mp3 ? "mp3" : "wav", chans); z_log(m); }
static int obn; static void flush_(void) { if (obn) { if (enc) { long w = mp3e_put(enc, outb, obn / chans, mb); z_write(f, mb, w); dbytes += w; } else { z_write(f, outb, obn * 2); dbytes += obn * 2; } obn = 0; } }
static void pump(void) {
    for (int it = 0; it < 6; it++) { long r = z_rec_read(buf, 32768); if (r < 4) break; int fr = r / 4, p0 = 0, p1 = 0;
        for (int i = 0; i < fr; i++) { int l = buf[i * 2] * gain / 100, rr = buf[i * 2 + 1] * gain / 100; l = MAX(-32768, MIN(l, 32767)); rr = MAX(-32768, MIN(rr, 32767));
            if (chans == 2) { outb[obn++] = l; outb[obn++] = rr; } else { int v = (l + rr) / 2; outb[obn++] = v; l = rr = v; }
            int a = l < 0 ? -l : l, c = rr < 0 ? -rr : rr; if (a > p0) p0 = a; if (c > p1) p1 = c; }
        pk[0] = MAX(p0, pk[0] * 3 / 4); pk[1] = MAX(p1, pk[1] * 3 / 4); if (obn >= 64000) flush_(); } }
static void stop(void) {
    pump(); z_rec_stop(); pump(); flush_();
    if (enc) { long w = mp3e_end(enc, mb); z_write(f, mb, w); dbytes += w; mp3e_free(enc); enc = 0; } else { u8 h[44]; hdr(h, dbytes, chans); z_seek(f, 0, 0); z_write(f, h, 44); }
    z_close(f); f = -1; rec = 0; pk[0] = pk[1] = 0; z_sync(); load(); snprintf(msg, 100, _("Guardado: %lu KB"), dbytes >> 10);
    char m[80]; snprintf(m, 80, "[prueba] grabacion guardada %lu bytes", dbytes); z_log(m); }

/* WAV -> MP3 al lado, con el mismo nombre. Lee por trozos; si la frecuencia no es de MPEG-1 la pasa a 48 kHz (lineal). */
static void convert(void) {
    char in[300], out[300]; snprintf(in, 300, "%s/%s", dir, L[sel].n); strcpy(out, in); strcpy(out + strlen(out) - 4, ".mp3");
    int fd = z_open(in, O_READ); if (fd < 0) { strcpy(msg, z_errstr(fd)); return; }
    u8 h[512]; long hn = z_read(fd, h, 512); int ch = 0, rate = 0, bits = 0; long data = 0, dlen = 0;
    if (hn >= 44 && !memcmp(h, "RIFF", 4) && !memcmp(h + 8, "WAVE", 4)) for (long o = 12; o + 8 <= hn;) { u32 l = *(u32 *)(h + o + 4);
        if (!memcmp(h + o, "fmt ", 4)) { ch = *(u16 *)(h + o + 10); rate = *(u32 *)(h + o + 12); bits = *(u16 *)(h + o + 22); }
        else if (!memcmp(h + o, "data", 4)) { data = o + 8; dlen = l; break; } o += 8 + l + (l & 1); }
    if (!data || ch < 1 || ch > 2 || (bits != 8 && bits != 16) || rate < 4000) { z_close(fd); strcpy(msg, _("Formato no valido")); return; }
    int orate = rate == 32000 || rate == 44100 || rate == 48000 ? rate : rate == 22050 || rate == 11025 ? 44100 : 48000;
    zmp3 *e = mp3e_new(orate, ch, KB[kbi]); int o = z_open(out, O_WRITE | O_CREATE | O_TRUNC); if (!e || o < 0) { z_close(fd); if (o >= 0) z_close(o); mp3e_free(e); strcpy(msg, _("No se puede abrir")); return; }
    z_seek(fd, data, 0); int fs = ch * bits / 8; u8 *ib = malloc(65536); long done = 0, wr = 0, step = ((long)rate << 16) / orate, frac = 0; int prev[2] = { 0, 0 }, have = 0; u64 tl = 0;
    snprintf(msg, 100, _("Convirtiendo...")); draw();
    while (done < dlen) {
        long r = z_read(fd, ib, MIN(65536 / fs * fs, dlen - done)); if (r < fs) break; done += r; int fr = r / fs, on = 0;
        for (int i = 0; i < fr; i++) { int s[2]; for (int c = 0; c < ch; c++) s[c] = bits == 16 ? ((short *)(ib + i * fs))[c] : (ib[i * fs + c] - 128) << 8;
            if (orate == rate) { for (int c = 0; c < ch; c++) outb[on++] = s[c]; }
            else { if (!have) { prev[0] = s[0]; prev[1] = s[ch - 1]; have = 1; }   /* salidas entre la muestra anterior y esta */
                while (frac < 65536) { for (int c = 0; c < ch; c++) outb[on++] = prev[c] + (int)(((long)(s[c] - prev[c]) * frac) >> 16); frac += step; }
                frac -= 65536; prev[0] = s[0]; if (ch == 2) prev[1] = s[1]; }
            if (on >= 120000) { long w = mp3e_put(e, outb, on / ch, mb); z_write(o, mb, w); wr += w; on = 0; } }
        if (on) { long w = mp3e_put(e, outb, on / ch, mb); z_write(o, mb, w); wr += w; }
        if (z_ms() - tl > 300) { tl = z_ms(); snprintf(msg, 100, _("Convirtiendo... %d%%"), (int)(done * 100 / MAX(dlen, 1))); draw(); } }
    long w = mp3e_end(e, mb); z_write(o, mb, w); wr += w; mp3e_free(e); z_close(o); z_close(fd); free(ib); z_sync(); load();
    for (int i = 0; i < n; i++) if (!strcmp(L[i].n, strrchr(out, '/') + 1)) sel = i;
    snprintf(msg, 100, _("MP3: %lu KB (era %lu KB)"), wr >> 10, (u64)(dlen + 44) >> 10);
    char m[100]; snprintf(m, 100, "[prueba] mp3 convertido %lu bytes de %ld", wr, dlen); z_log(m); }

int main(const char *a) {
    char d[120]; z_docdir(d); snprintf(dir, 160, "%s/GRABACIONES", d); z_mkdir(dir); cfg_load(); buf = malloc(32768); outb = malloc(1 << 18); mb = malloc(1 << 18);
    W = zw_new("Grabadora", -1, 0, 480, 400, WF_DECOR | WF_RESIZE); load(); draw(); zevent e; u64 last = 0;
    for (;;) {
        int g = z_event(&e, rec ? 40 : -1); if (g && e.type == EV_REDRAW) { draw(); continue; }
        if (rec) { pump(); if (z_ms() - last > 80) { last = z_ms(); draw(); } }
        if (!g || e.win != (u32)W->id) continue;
        if (e.type == EV_CLOSE) { if (rec) stop(); return 0; }
        if (e.type == EV_RESIZE) zw_resized(W, e.x, e.y);
        else if (e.type == EV_WHEEL) top = MAX(0, MIN(top - (int)e.key, n - 1));
        else if (e.type == EV_KEY && e.ch == ' ') { if (rec) stop(); else start(); }
        else if (e.type == EV_DOWN) { int by = W->h - 36;
            if (e.y < 56 && e.x >= 230 && e.x < 330) { if (rec) stop(); else start(); }
            else if (e.y < 56 && e.x >= 338 && e.x < 378) gain = MAX(25, gain - 25);
            else if (e.y < 56 && e.x >= 382 && e.x < 422) gain = MIN(800, gain + 25);
            else if (!rec && e.y >= 78 && e.y < 106 && e.x >= 20 && e.x < 120) { chans = 3 - chans; cfg_save(); }
            else if (!rec && e.y >= 78 && e.y < 106 && e.x >= 128 && e.x < 198) { mp3 = !mp3; cfg_save(); }
            else if (!rec && mp3 && e.y >= 78 && e.y < 106 && e.x >= 206 && e.x < 296) { kbi = (kbi + 1) % 7; cfg_save(); }
            else if (e.y >= by) { char p[300]; if (sel >= 0) snprintf(p, 300, "%s/%s", dir, L[sel].n);
                if (sel >= 0 && e.x < 96) z_spawn("/rd/bin/player", p);
                else if (sel >= 0 && e.x >= 102 && e.x < 176 && ui_confirm("Borrar", "Borrar esta grabacion?")) { z_unlink(p); z_sync(); load(); }
                else if (!rec && sel_wav() && e.x >= 182 && e.x < 262) convert(); }
            else if (e.y >= 156) { int i = top + (e.y - 156) / 22; sel = i < n ? i : -1; } }
        else continue;
        draw(); }
}
