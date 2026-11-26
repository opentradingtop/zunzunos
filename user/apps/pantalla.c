/* Grabar pantalla: guarda la pantalla en video AVI (MJPEG) y, si se quiere, el microfono.
   El archivo va a Documentos/Videos y se puede ver con Videos, VLC o Windows. */
#include "z.h"
static zwin *W; static zsys si; static char msg[200], path[220]; static int fps = 10, mic, rec, last_ok;
static zavi_t A; static u64 t0, next_t; static long frame_no; static u32 *px; static u8 *jpg; static long jn; static short abuf[24000]; static int an; static short rbuf[32768];
static void draw(void) { g_clear(W, UI_WHITE); int y = 14; g_textb(W, 16, y, _("Grabar pantalla"), UI_TEXT); y += 30;
    g_text(W, 16, y + 5, _("Imágenes por segundo"), UI_TEXT); static const int F[3] = {5, 10, 15}; for (int i = 0; i < 3; i++) { char b[8]; snprintf(b, 8, "%d", F[i]); ui_button(W, 210 + i * 56, y, 50, 26, b, fps == F[i] ? 2 : 0); } y += 36;
    g_text(W, 16, y + 5, _("Micrófono"), UI_TEXT); ui_button(W, 210, y, 80, 26, "Sí", mic ? 2 : 0); ui_button(W, 296, y, 80, 26, "No", mic ? 0 : 2); y += 44;
    g_round(W, 16, y, 170, 36, 6, rec ? 0x374151 : 0xDC2626); if (!rec) g_circle(W, 36, y + 18, 7, 0xFFFFFF, 1); else g_rect(W, 30, y + 12, 12, 12, 0xFFFFFF); g_textb(W, 52, y + 10, _(rec ? "Detener" : "Grabar"), 0xFFFFFF);
    if (!rec && last_ok) ui_button(W, 196, y + 5, 120, 26, "Ver el vídeo", 0); y += 48;
    zw_clip(W, 16, y, W->w - 32, 40); g_text(W, 16, y, msg, UI_MUTED); zw_noclip(W); g_text(W, 16, W->h - 22, _("Al grabar, esta ventana se minimiza. Para parar, ábrela en la barra."), UI_MUTED); zw_present(W); }
static void pump_audio(void) { if (!mic) return; for (int it = 0; it < 6; it++) { long r = z_rec_read(rbuf, sizeof rbuf); if (r < 8) break; int fr = r / 4; for (int i = 0; i + 1 < fr; i += 2) { int v = (rbuf[i * 2] + rbuf[i * 2 + 1] + rbuf[i * 2 + 2] + rbuf[i * 2 + 3]) / 4; abuf[an++] = v; if (an == 24000 / 4) { zavi_audio(&A, abuf, an); an = 0; } } } }
static void start(void) { char d[160], dir[200]; z_docdir(d); snprintf(dir, 200, "%s/Videos", d); z_mkdir(dir); u32 t[6]; z_time(t); snprintf(path, 220, "%s/grabacion-%04u%02u%02u-%02u%02u%02u.avi", dir, t[5], t[4], t[3], t[2], t[1], t[0]);
    int e = zavi_open(&A, path, si.scr_w, si.scr_h, fps, mic ? 24000 : 0); if (e) { snprintf(msg, 200, "%s: %s", _("No se pudo crear el vídeo"), z_errstr(e)); return; }
    if (mic && z_rec_start() < 0) { mic = 0; } px = malloc((long)si.scr_w * si.scr_h * 4); rec = 1; last_ok = 0; frame_no = 0; an = 0; jpg = 0; jn = 0; t0 = next_t = z_ms(); zw_op(W->id, 2, 0, 0); char m[300]; snprintf(m, 300, "[prueba] grabando pantalla %s %d ips mic %d", path, fps, mic); z_log(m); }
static void stop(void) { if (!rec) return; rec = 0; pump_audio(); if (mic) { z_rec_stop(); pump_audio(); } if (an) { zavi_audio(&A, abuf, an); an = 0; } long frames = A.frames, bytes = A.pos; zavi_close(&A); z_sync(); free(px); px = 0; free(jpg); jpg = 0;
    long secs = frames / fps; snprintf(msg, 200, "%s %s (%ld:%02ld, %ld MB)", _("Guardado en"), strrchr(path, '/') + 1, secs / 60, secs % 60, bytes >> 20); last_ok = 1; zw_title(W->id, "Grabar pantalla"); char m[300]; snprintf(m, 300, "[prueba] grabacion de pantalla %ld cuadros, %ld bytes", frames, bytes); z_log(m); }
/* un cuadro: captura, JPEG y escritura; si el equipo va lento se repite el ultimo cuadro para que el video dure lo mismo que la grabacion */
static void tick(void) { u64 now = z_ms(); pump_audio(); if (now < next_t) return; u64 ta = z_ms(); int r = z_capture(px, (long)si.scr_w * si.scr_h); u64 tb = z_ms(); if (r > 0) { free(jpg); jn = z_jpeg_encode(px, r >> 16, r & 0xFFFF, 70, &jpg); } (void)ta; (void)tb;
    long due = (long)((z_ms() - t0) * fps / 1000) + 1; int first = 1; do { u64 tw = z_ms(); if (jn > 0 && zavi_frame(&A, jpg, first ? jn : 0)) { snprintf(msg, 200, "%s", _("Error al escribir el vídeo (¿memoria llena?)")); stop(); return; } frame_no++; first = 0; (void)tw; } while (frame_no < due && jn > 0);
    next_t = t0 + (u64)frame_no * 1000 / fps; { static u64 lt; if (now - lt >= 1000) { lt = now; char t[60]; long s = (now - t0) / 1000; snprintf(t, 60, "%s %ld:%02ld  %ld MB", _("Grabando"), s / 60, s % 60, A.pos >> 20); zw_title(W->id, t); } }
    if (A.pos > (1LL << 30)) { snprintf(msg, 200, "%s", _("Se alcanzó 1 GB: grabación detenida")); stop(); } }
int main(const char *a) { z_sysinfo(&si); W = zw_new("Grabar pantalla", -1, 0, 600, 250, WF_DECOR); strcpy(msg, _("Graba todo lo que se ve en la pantalla en un vídeo.")); draw(); zevent e;
    for (;;) { int g = z_event(&e, rec ? 5 : -1); if (rec) tick(); if (!g) continue; if (e.type == EV_REDRAW) { draw(); continue; } if (e.win != (u32)W->id) continue; if (e.type == EV_CLOSE) { stop(); return 0; }
        if (e.type == EV_FOCUS && rec) { draw(); continue; }
        if (e.type != EV_DOWN) continue; int y = 44; if (!rec && e.y >= y && e.y < y + 26) { int i = (e.x - 210) / 56; if (e.x >= 210 && i < 3) fps = i == 0 ? 5 : i == 1 ? 10 : 15; } y += 36;
        if (!rec && e.y >= y && e.y < y + 26) { if (e.x >= 210 && e.x < 290) mic = 1; else if (e.x >= 296 && e.x < 376) mic = 0; } y += 44;
        if (e.y >= y && e.y < y + 36 && e.x >= 16 && e.x < 186) { if (rec) stop(); else start(); } else if (!rec && last_ok && e.y >= y + 5 && e.y < y + 31 && e.x >= 196 && e.x < 316) z_spawn("/rd/bin/video", path);
        draw(); }
}
