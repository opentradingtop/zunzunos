/* Reproductor de sonido: WAV (PCM de 8 o 16 bits) y MP3 (MPEG capas I, II y III, con el decodificador propio zmp3d.c), mono o estereo,
   cualquier frecuencia (se pasa a los 48 kHz de la tarjeta). */
#include "z.h"
static zwin *W; static char path[256], card[64], st[100]; static u8 *D; static long pcm, plen, pos; static int rate, ch, bits, playing, vol = 80, has, ismp3;
static zmp3d *dec; static short fb[2304]; static int fbn, fbi; static long ffr; static int fcl, fcr, fok;   /* MP3: cuadro decodificado y muestra por la que va */
static int is_mp3(const char *p, const u8 *d, long n) { if (n >= 3 && !memcmp(d, "ID3", 3)) return 1; if (z_ends(p, ".mp3")) return 1; return n >= 2 && d[0] == 0xFF && (d[1] & 0xE0) == 0xE0; }
static void load(const char *p) {
    strncpy0(path, p, 256); free(D); D = 0; playing = 0; pos = 0; ismp3 = 0; fbn = fbi = 0; ffr = 0; fok = 0; long n; u8 *d = z_readfile(p, &n);
    strcpy(st, _("Formato no valido")); if (!d) { strcpy(st, _("No se puede abrir")); return; }
    if (n >= 44 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WAVE", 4)) {
        long o = 12; pcm = 0;
        while (o + 8 <= n) { u32 l = *(u32 *)(d + o + 4); if (!memcmp(d + o, "fmt ", 4)) { ch = *(u16 *)(d + o + 10); rate = *(u32 *)(d + o + 12); bits = *(u16 *)(d + o + 22); if (*(u16 *)(d + o + 8) != 1 && *(u16 *)(d + o + 8) != 0xFFFE) { free(d); return; } } else if (!memcmp(d + o, "data", 4)) { pcm = o + 8; plen = MIN((long)l, n - pcm); break; } o += 8 + l + (l & 1); }
        if (!pcm || !rate || (bits != 8 && bits != 16) || ch < 1 || ch > 2) { free(d); return; }
        D = d; snprintf(st, 100, _("%d Hz, %d bits, %s, %ld s"), rate, bits, ch == 2 ? _("estereo") : "mono", plen / (rate * ch * bits / 8)); return; }
    if (!is_mp3(p, d, n)) { free(d); return; }
    /* MP3: se recorre una vez sin decodificar para saber frecuencia, canales y duracion */
    if (!dec) dec = mp3d_new(); if (!dec) { free(d); return; }
    long o = 0, smp = 0, fr = 0; rate = ch = 0;
    while (o < n) { int used, hz, c, s = mp3d_frame(dec, d + o, n - o, 0, &used, &hz, &c); if (!used) break; o += used; if (s) { smp += s; fr++; if (!rate) { rate = hz; ch = c; } } }
    if (!fr || !rate) { free(d); return; }
    free(dec); dec = mp3d_new(); D = d; pcm = 0; plen = n; ismp3 = 1; bits = 16;
    long sec = smp / rate; snprintf(st, 100, _("MP3, %d Hz, %s, %ld kbps, %ld:%02ld"), rate, ch == 2 ? _("estereo") : "mono", smp ? n * 8 * rate / smp / 1000 : 0, sec / 60, sec % 60);
    char m[100]; snprintf(m, 100, "[prueba] mp3 abierto %d Hz %d canales %ld cuadros", rate, ch, fr); z_log(m); }
/* siguiente muestra (l, r) del archivo; 0 al llegar al final */
static int next(int *l, int *r) {
    if (!ismp3) { int fs = ch * bits / 8; if (pos + fs > plen) return 0; const u8 *s = D + pcm + pos; pos += fs;
        if (bits == 16) { *l = ((short *)s)[0]; *r = ch == 2 ? ((short *)s)[1] : *l; } else { *l = (s[0] - 128) << 8; *r = ch == 2 ? (s[1] - 128) << 8 : *l; } return 1; }
    while (fbi >= fbn) { if (pos >= plen) return 0; int used, hz, c; fbn = mp3d_frame(dec, D + pos, plen - pos, fb, &used, &hz, &c); if (!used) { pos = plen; return 0; } pos += used; fbi = 0; if (c != ch) fbn = 0; }
    *l = fb[fbi * ch]; *r = ch == 2 ? fb[fbi * ch + 1] : *l; fbi++; return 1; }
static void rewind_(void) { pos = 0; fbn = fbi = 0; ffr = 0; fok = 0; if (ismp3) { free(dec); dec = mp3d_new(); } }
static void draw(void) {
    g_grad(W, 0, 0, W->w, W->h, 0x1F2937, 0x111827); const char *n = strrchr(path, '/'); g_textb(W, 16, 14, n ? n + 1 : "Sin archivo", 0xFFFFFF); g_text(W, 16, 34, st, 0x9CA3AF); g_text(W, 16, 54, card, 0x5EEAD4);
    g_rect(W, 16, 80, W->w - 32, 8, 0x374151); if (D && plen) g_rect(W, 16, 80, (W->w - 32) * (pos / 1024) / MAX(1, plen / 1024), 8, 0x14B8A6);
    ui_button(W, 16, 102, 90, 30, playing ? "Pausa" : "Tocar", 2); ui_button(W, 112, 102, 70, 30, "Parar", 0); ui_button(W, 188, 102, 70, 30, "Abrir", 0); ui_button(W, 280, 102, 34, 30, "-", 0); ui_button(W, 318, 102, 34, 30, "+", 0);
    char v[16]; snprintf(v, 16, "%d%%", vol); g_text(W, 360, 110, v, 0xFFFFFF); zw_present(W); }
static void feed(void) {
    static short o[4096]; int frames = 0; long step = ((long)rate << 16) / 48000;
    if (!fok) fok = next(&fcl, &fcr);
    while (frames < 2048 && fok) { o[frames * 2] = fcl; o[frames * 2 + 1] = fcr; frames++; ffr += step; while (ffr >= 65536 && fok) { ffr -= 65536; fok = next(&fcl, &fcr); } }
    if (frames) z_audio(o, frames * 4);
    if (!fok) { playing = 0; rewind_(); z_log("[prueba] reproduccion terminada"); } }
int main(const char *a) {
    W = zw_new("Musica", -1, 0, 440, 150, WF_DECOR); has = z_audio_info(card, 64); vol = z_audio_vol(-1); load(a[0] ? a : "/rd/res/tono.wav"); draw(); zevent e; u64 t = 0;
    for (;;) { int g = z_event(&e, playing ? 5 : -1);
        if (g) { if (e.type == EV_REDRAW) { draw(); continue; } if (e.win != (u32)W->id) continue; if (e.type == EV_CLOSE) return 0;
            if (e.type == EV_DOWN && e.y >= 102 && e.y < 132) { if (e.x < 106) { if (D && has) playing = !playing; } else if (e.x < 182) { playing = 0; rewind_(); } else if (e.x < 258) { char n[256]; strcpy(n, path); if (ui_prompt("Abrir sonido", "Ruta del sonido (WAV o MP3):", n, 240)) load(n); } else if (e.x >= 280 && e.x < 314) z_audio_vol(vol = MAX(0, vol - 10)); else if (e.x >= 318 && e.x < 352) z_audio_vol(vol = MIN(100, vol + 10)); draw(); }
            else if (e.type == EV_KEY && e.ch == ' ') { if (D && has) playing = !playing; draw(); } }
        if (playing && z_audio_space() > 16384) feed(); if (playing && z_ms() - t > 250) { t = z_ms(); draw(); } }
}
