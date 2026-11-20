/* Videos: reproduce AVI con MJPEG (las grabaciones de pantalla y muchas camaras) y su sonido PCM.
   Barra inferior: reproducir/pausa, parar, barra de tiempo (pulsar para saltar), abrir. */
#include "z.h"
#define CB 40
static zwin *W; static zavi_r R; static char path[300], msg[200]; static int loaded, playing, cur = -1, fw, fh; static u32 *F; static u64 t_play; static int f_play;
static long *acum; static int apos; /* muestras de sonido acumuladas por trozo */
static char lst[40][120]; static int nl;
static void list_videos(void) { char d[160], dir[200]; z_docdir(d); snprintf(dir, 200, "%s/Videos", d); zdirent e; nl = 0; for (int i = 0; nl < 40 && !z_readdir(dir, i, &e); i++) if (z_ends(e.name, ".avi") || z_ends(e.name, ".AVI")) snprintf(lst[nl++], 120, "%s/%s", dir, e.name); }
static void show(int i) { if (!loaded || i < 0 || i >= R.nf || i == cur) return; if (!R.fl[i]) { cur = i; return; } /* cuadro vacio: se repite el anterior */ long n; u8 *j = zavi_read(&R, i, &n); if (!j) return; u32 *px; int w, h; if (!z_jpeg_decode(j, n, &px, &w, &h)) { free(F); F = px; fw = w; fh = h; cur = i; } free(j); }
static long ms_of(int f) { return (long)f * R.us_frame / 1000; }
static void fmt_t(long ms, char *o) { long s = ms / 1000; snprintf(o, 16, "%ld:%02ld", s / 60, s % 60); }
static void draw(void) { int H = W->h - CB; g_rect(W, 0, 0, W->w, H, 0);
    if (loaded && F) { long k = MIN((long)W->w * 1024 / fw, (long)H * 1024 / fh); int dw = fw * k / 1024, dh = fh * k / 1024, x0 = (W->w - dw) / 2, y0 = (H - dh) / 2; for (int y = MAX(0, y0); y < MIN(H, y0 + dh); y++) { u32 *o = W->px + (long)y * W->w, *r = F + (long)(y - y0) * 1024 / k * fw; for (int x = MAX(0, x0); x < MIN(W->w, x0 + dw); x++) { long sx = (long)(x - x0) * 1024 / k; o[x] = r[sx < fw ? sx : fw - 1]; } } }
    else { int y = 20; g_text2(W, 20, y, _("Vídeos"), 0xFFFFFF, 2); y += 46; g_text(W, 20, y, _("Reproduce vídeos AVI (MJPEG), como las grabaciones de pantalla."), 0x9CA3AF); y += 30; if (!nl) g_text(W, 20, y, _("No hay vídeos en Documentos/Videos. Pulsa Abrir para elegir otro."), 0x9CA3AF);
        for (int i = 0; i < nl; i++) { g_round(W, 16, y - 2, W->w - 32, 24, 4, 0x1F2937); g_text(W, 26, y + 2, strrchr(lst[i], '/') + 1, 0xFFFFFF); y += 28; } }
    g_rect(W, 0, H, W->w, CB, UI_PANEL); ui_button(W, 8, H + 7, 44, 26, playing ? "||" : ">", 2); ui_button(W, 56, H + 7, 44, 26, "[]", 0); ui_button(W, W->w - 84, H + 7, 76, 26, "Abrir", 0);
    if (loaded) { char a[16], b[16], t[40]; fmt_t(ms_of(MAX(cur, 0)), a); fmt_t(ms_of(R.nf), b); snprintf(t, 40, "%s / %s", a, b); int bx = 110, bw = W->w - 84 - 16 - bx - 110; g_rect(W, bx, H + 18, bw, 6, UI_LINE); g_rect(W, bx, H + 18, (long)bw * (cur + 1) / R.nf, 6, UI_ACC); g_text(W, bx + bw + 10, H + 12, t, UI_TEXT); }
    if (msg[0]) { g_rect(W, 0, H - 22, W->w, 22, 0x111827); g_text(W, 8, H - 19, msg, 0xFCA5A5); } zw_present(W); }
static void open_(const char *p) { if (loaded) zavi_free(&R); loaded = 0; free(F); F = 0; cur = -1; playing = 0; msg[0] = 0; int e = zavi_load(&R, p); if (e) { snprintf(msg, 200, "%s: %s", _("No se puede reproducir"), e == -22 ? _("no es un AVI con vídeo MJPEG") : z_errstr(e)); return; }
    strncpy0(path, p, 300); loaded = 1; free(acum); acum = malloc((R.na + 1) * sizeof(long)); acum[0] = 0; for (int i = 0; i < R.na; i++) acum[i + 1] = acum[i] + R.al[i] / MAX(1, R.achan * R.abits / 8); show(0); const char *n = strrchr(p, '/'); zw_title(W->id, n ? n + 1 : p);
    char m[400]; snprintf(m, 400, "[prueba] video %s: %dx%d, %d cuadros, sonido %d Hz", p, R.w, R.h, R.nf, R.arate); z_log(m); }
/* sonido: se pasa a 48 kHz estereo de 16 bits (lo que espera la tarjeta) y se adelanta un poco */
static void feed_audio(void) { if (!loaded || R.astream < 0 || !R.na || R.abits != 16) return; static short o[65536]; while (apos < R.na && z_audio_space() > 65536) { long n; u8 *d = zavi_read_audio(&R, apos, &n); if (!d) break; short *s = (short *)d; int ch = MAX(1, R.achan), fr = n / (2 * ch), k = 0; long step = ((long)R.arate << 16) / 48000; for (long p = 0; (p >> 16) < fr && k < 32760; p += step) { short l = s[(p >> 16) * ch], r = ch > 1 ? s[(p >> 16) * ch + 1] : l; o[k * 2] = l; o[k * 2 + 1] = r; k++; } z_audio(o, k * 4); free(d); apos++; } }
static void seek_to(int f) { f = MAX(0, MIN(f, R.nf - 1)); f_play = f; t_play = z_ms(); show(f); if (R.na) { long smp = (long)ms_of(f) * R.arate / 1000; apos = 0; while (apos < R.na && acum[apos + 1] <= smp) apos++; } }
static void play(int on) { if (!loaded) return; playing = on; if (on) { if (cur >= R.nf - 1) cur = -1, show(0); seek_to(MAX(cur, 0)); z_log("[prueba] video reproduciendo"); } }
int main(const char *a) { zsys s; z_sysinfo(&s); W = zw_new("Vídeos", -1, 0, MIN(860, (int)s.scr_w - 40), MIN(600, (int)s.scr_h - 80), WF_DECOR | WF_RESIZE); list_videos(); if (a[0]) { open_(a); if (loaded) play(1); } draw(); zevent e;
    for (;;) { int g = z_event(&e, playing ? 5 : -1); if (playing) { int f = f_play + (int)((z_ms() - t_play) * 1000 / R.us_frame); if (f >= R.nf) { playing = 0; show(R.nf - 1); z_log("[prueba] video terminado"); } else if (f != cur) show(f); feed_audio(); { static u64 ld; if (z_ms() - ld > 30) { ld = z_ms(); draw(); } } }
        if (!g) continue; if (e.type == EV_REDRAW) { draw(); continue; } if (e.win != (u32)W->id) continue; if (e.type == EV_CLOSE) return 0; if (e.type == EV_RESIZE) { zw_resized(W, e.x, e.y); draw(); continue; }
        int H = W->h - CB;
        if (e.type == EV_KEY) { if (e.ch == ' ') play(!playing); else if (loaded && (e.key == K_RIGHT || e.key == K_LEFT)) seek_to(cur + (e.key == K_RIGHT ? 1 : -1) * (int)(5000000L / R.us_frame)); draw(); continue; }
        if (e.type != EV_DOWN) continue;
        if (e.y >= H) { if (e.x < 52) play(!playing); else if (e.x < 100) { playing = 0; if (loaded) seek_to(0); } else if (e.x >= W->w - 84) { char p[300]; strcpy(p, path[0] ? path : "/sys/DOCS/Videos/"); if (ui_prompt("Abrir vídeo", "Ruta del archivo AVI:", p, 290) && p[0]) { open_(p); if (loaded) play(1); } }
            else if (loaded) { int bx = 110, bw = W->w - 84 - 16 - bx - 110; if (e.x >= bx && e.x < bx + bw) { seek_to((long)(e.x - bx) * R.nf / bw); } } }
        else if (!loaded && nl) { int i = (e.y - 96) / 28; if (e.y >= 94 && i >= 0 && i < nl) { open_(lst[i]); if (loaded) play(1); } }
        else if (loaded) play(!playing);
        draw(); }
}
