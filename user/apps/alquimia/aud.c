/* Alquimia: sonido. Lee WAV (PCM de 8, 16, 24 y 32 bits, coma flotante y WAVE_FORMAT_EXTENSIBLE), AIFF/AIFF-C,
   MP3 (decodificador del sistema) y la pista de sonido de los AVI. Escribe WAV de 16 bits, AIFF y MP3 (codificador
   del sistema). Por el camino: mono o estereo, otra frecuencia (interpolacion lineal) y normalizar al pico. */
typedef struct { short *s; long n; int rate, ch; } zpcm;   /* n: muestras por canal; s intercalado */
static void pcm_free(zpcm *p) { free(p->s); memset(p, 0, sizeof *p); }
static int pcm_cap(zpcm *p, long n) { p->s = malloc(n * p->ch * 2 + 16); return p->s ? 0 : -1; }
static int wav_decode(const u8 *d, long len, zpcm *o, char *err) {
    if (len < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) return -1; int fmt = 0, ch = 0, rate = 0, bits = 0; long dp = -1, dl = 0;
    for (long p = 12; p + 8 <= len;) { u32 l = G32(d + p + 4); if (!memcmp(d + p, "fmt ", 4) && l >= 16) { fmt = G16(d + p + 8); ch = G16(d + p + 10); rate = G32(d + p + 12); bits = G16(d + p + 22); if (fmt == 0xFFFE && l >= 40) fmt = G16(d + p + 32); }
        else if (!memcmp(d + p, "data", 4)) { dp = p + 8; dl = MIN((long)l, len - dp); break; } p += 8 + l + (l & 1); }
    if (dp < 0 || ch < 1 || ch > 8 || rate < 1000 || (fmt != 1 && fmt != 3) || (fmt == 3 && bits != 32 && bits != 64) || (fmt == 1 && bits != 8 && bits != 16 && bits != 24 && bits != 32)) { strcpy(err, tr("WAV con un formato que no se puede leer")); return -2; }
    int bp = bits / 8, oc = MIN(ch, 2); long n = dl / (bp * ch); o->rate = rate; o->ch = oc; o->n = n; if (pcm_cap(o, n)) { strcpy(err, tr("Falta memoria")); return -2; }
    for (long i = 0; i < n; i++) for (int c = 0; c < oc; c++) { const u8 *q = d + dp + (i * ch + c) * bp; int v;
        if (fmt == 3) { double f; if (bits == 32) { union { u32 u; float f; } x; x.u = G32(q); f = x.f; } else { union { u64 u; double f; } x; x.u = G64(q); f = x.f; } f = f > 1 ? 1 : f < -1 ? -1 : f; v = (int)(f * 32767); }
        else if (bp == 1) v = (q[0] - 128) << 8; else if (bp == 2) v = (short)G16(q); else if (bp == 3) v = (short)(q[1] | q[2] << 8); else v = (short)G16(q + 2);
        o->s[i * oc + c] = v; }
    return 0; }
/* numero en coma flotante extendida de 80 bits (la frecuencia del AIFF) */
static double ext80(const u8 *p) { int e = ((p[0] & 127) << 8 | p[1]) - 16383 - 63; u64 m = 0; for (int i = 0; i < 8; i++) m = m << 8 | p[2 + i]; double v = (double)m; while (e > 0) { v *= 2; e--; } while (e < 0) { v /= 2; e++; } return p[0] & 128 ? -v : v; }
static int aiff_decode(const u8 *d, long len, zpcm *o, char *err) {
    if (len < 12 || memcmp(d, "FORM", 4) || (memcmp(d + 8, "AIFF", 4) && memcmp(d + 8, "AIFC", 4))) return -1; int ch = 0, bits = 0, sw = 0; long frames = 0, dp = -1; double rate = 0;
    for (long p = 12; p + 8 <= len;) { u32 l = (u32)d[p + 4] << 24 | d[p + 5] << 16 | d[p + 6] << 8 | d[p + 7];
        if (!memcmp(d + p, "COMM", 4) && l >= 18) { ch = d[p + 8] << 8 | d[p + 9]; frames = (long)((u32)d[p + 10] << 24 | d[p + 11] << 16 | d[p + 12] << 8 | d[p + 13]); bits = d[p + 14] << 8 | d[p + 15]; rate = ext80(d + p + 16);
            if (!memcmp(d + 8, "AIFC", 4) && l >= 22) { if (!memcmp(d + p + 26, "sowt", 4)) sw = 1; else if (memcmp(d + p + 26, "NONE", 4) && memcmp(d + p + 26, "twos", 4)) { strcpy(err, tr("AIFF-C comprimido: no se puede leer")); return -2; } } }
        else if (!memcmp(d + p, "SSND", 4)) { u32 off = (u32)d[p + 8] << 24 | d[p + 9] << 16 | d[p + 10] << 8 | d[p + 11]; dp = p + 16 + off; }
        p += 8 + l + (l & 1); }
    if (dp < 0 || ch < 1 || ch > 8 || (bits != 8 && bits != 16 && bits != 24 && bits != 32) || rate < 1000) { strcpy(err, tr("AIFF con un formato que no se puede leer")); return -2; }
    int bp = bits / 8, oc = MIN(ch, 2); frames = MIN(frames, (len - dp) / (bp * ch)); o->rate = (int)(rate + 0.5); o->ch = oc; o->n = frames; if (pcm_cap(o, frames)) { strcpy(err, tr("Falta memoria")); return -2; }
    for (long i = 0; i < frames; i++) for (int c = 0; c < oc; c++) { const u8 *q = d + dp + (i * ch + c) * bp; int v; if (bp == 1) v = (signed char)q[0] << 8; else if (sw) v = (short)(q[bp - 2] | q[bp - 1] << 8); else v = (short)(q[0] << 8 | q[1]); o->s[i * oc + c] = v; }
    return 0; }
static int mp3_decode(const u8 *d, long len, zpcm *o, char *err) {
    zmp3d *dec = mp3d_new(); short fb[1152 * 2 + 64]; long cap = 0, n = 0, p = 0; int ch = 0, rate = 0; short *s = 0;
    while (p < len) { int used = 0, hz = 0, c = 0, k = mp3d_frame(dec, d + p, len - p, fb, &used, &hz, &c); if (!used) break; p += used; if (k <= 0) continue;
        if (!ch) { ch = c; rate = hz; } if (c != ch || hz != rate) continue;
        if (n + k > cap) { cap = (n + k) * 2 + 65536; s = realloc(s, cap * ch * 2); if (!s) break; } memcpy(s + n * ch, fb, k * ch * 2); n += k; }
    free(dec); if (!s || !n) { free(s); strcpy(err, tr("No se ha podido decodificar el MP3")); return -2; } o->s = s; o->n = n; o->ch = ch; o->rate = rate; return 0; }
static void pcm_channels(zpcm *p, int ch) { if (!ch || ch == p->ch) return; short *o = malloc(p->n * ch * 2 + 16); if (!o) return;
    for (long i = 0; i < p->n; i++) { if (ch == 1) o[i] = (short)(((int)p->s[i * 2] + p->s[i * 2 + 1]) / 2); else { o[i * 2] = o[i * 2 + 1] = p->s[i]; } } free(p->s); p->s = o; p->ch = ch; }
static void pcm_rate(zpcm *p, int rate) { if (!rate || rate == p->rate || p->n < 2) return; long n = (long)((double)p->n * rate / p->rate); short *o = malloc(n * p->ch * 2 + 16); if (!o) return; u64 step = ((u64)p->rate << 32) / rate, pos = 0;
    for (long i = 0; i < n; i++, pos += step) { long k = (long)(pos >> 32); u32 f = (u32)(pos >> 16) & 0xFFFF; if (k >= p->n - 1) { k = p->n - 2; f = 0xFFFF; } for (int c = 0; c < p->ch; c++) { int a = p->s[k * p->ch + c], b = p->s[(k + 1) * p->ch + c]; o[i * p->ch + c] = (short)(a + (((long)(b - a) * f) >> 16)); } }
    free(p->s); p->s = o; p->n = n; p->rate = rate; }
static void pcm_normalize(zpcm *p) { int pk = 1; for (long i = 0; i < p->n * p->ch; i++) { int v = p->s[i] < 0 ? -p->s[i] : p->s[i]; if (v > pk) pk = v; } long g = 29204L * 65536 / pk; /* -1 dBFS */ for (long i = 0; i < p->n * p->ch; i++) { long v = p->s[i] * g >> 16; p->s[i] = (short)MAX(-32768L, MIN(32767L, v)); } }
static void enc_wav(const zpcm *p, sb_t *b) { u32 dl = p->n * p->ch * 2; sb_add(b, "RIFF", 4); s32(b, 36 + dl); sb_add(b, "WAVEfmt ", 8); s32(b, 16); s16(b, 1); s16(b, p->ch); s32(b, p->rate); s32(b, p->rate * p->ch * 2); s16(b, p->ch * 2); s16(b, 16); sb_add(b, "data", 4); s32(b, dl); sb_add(b, p->s, dl); }
static void be32(sb_t *b, u32 v) { u8 t[4] = {v >> 24, v >> 16, v >> 8, v}; sb_add(b, t, 4); } static void be16(sb_t *b, u32 v) { u8 t[2] = {v >> 8, v}; sb_add(b, t, 2); }
static void enc_aiff(const zpcm *p, sb_t *b) { u32 dl = p->n * p->ch * 2; sb_add(b, "FORM", 4); be32(b, 4 + 26 + 16 + dl); sb_add(b, "AIFFCOMM", 8); be32(b, 18); be16(b, p->ch); be32(b, p->n); be16(b, 16);
    u8 e[10] = {0}; u32 r = p->rate; int ex = 16383 + 31; while (!(r & 0x80000000u)) { r <<= 1; ex--; } e[0] = ex >> 8; e[1] = ex; e[2] = r >> 24; e[3] = r >> 16; e[4] = r >> 8; e[5] = r; sb_add(b, e, 10);
    sb_add(b, "SSND", 4); be32(b, 8 + dl); be32(b, 0); be32(b, 0); for (long i = 0; i < p->n * p->ch; i++) be16(b, (u16)p->s[i]); }
static int enc_mp3(zpcm *p, int kbps, sb_t *b, prog_fn pr) {
    int r = p->rate; if (r != 32000 && r != 44100 && r != 48000) pcm_rate(p, r < 38000 ? (r <= 33000 ? 32000 : 44100) : r < 46000 ? 44100 : 48000);
    if (p->ch > 2) pcm_channels(p, 2); zmp3 *e = mp3e_new(p->rate, p->ch, kbps); if (!e) return -1; u8 *ob = malloc(1 << 17); long step = 1152 * 16;
    for (long i = 0; i < p->n; i += step) { long k = MIN(step, p->n - i); long n = mp3e_put(e, p->s + i * p->ch, k, ob); if (n > 0) sb_add(b, ob, n); if (pr && pr(i, p->n, "MP3")) { mp3e_free(e); free(ob); return -2; } }
    long n = mp3e_end(e, ob); if (n > 0) sb_add(b, ob, n); mp3e_free(e); free(ob); return 0; }
