/* Alquimia: el conversor (lo que era ALQUIMIA). Reconoce el tipo de archivo, dice a que formatos se puede pasar y
   hace la conversion con las opciones elegidas. Cualquier archivo, ademas, a Base64, volcado hexadecimal, data URI
   o huella SHA-256. */
enum { CK_NONE, CK_IMG, CK_AUD, CK_VID, CK_DOC, CK_DATA, CK_ARC, CK_B64, CK_N };
static const char *const CVK_NAME[CK_N] = {"Archivo", "Imagen", "Sonido", "Vídeo", "Documento", "Datos", "Archivo comprimido", "Texto Base64"};
typedef struct { int pct, maxw, maxh, quality, gray, rot; int rate, ch, kbps, norm; int paper, fsize; char delim; int header; int gif_fps, gif_w; int level; char pw[128]; } cvopt;
static void cv_defaults(cvopt *o) { memset(o, 0, sizeof *o); o->pct = 100; o->quality = 88; o->kbps = 192; o->fsize = 11; o->header = 1; o->gif_fps = 10; o->gif_w = 480; o->level = 2; }
__attribute__((unused)) static void cv_setopt(cvopt *o, const char *k, const char *v) { int n = atoi(v);
    if (!strcmp(k, "pct")) o->pct = n; else if (!strcmp(k, "ancho")) o->maxw = n; else if (!strcmp(k, "alto")) o->maxh = n; else if (!strcmp(k, "calidad")) o->quality = n; else if (!strcmp(k, "gris")) o->gray = n; else if (!strcmp(k, "girar")) o->rot = n;
    else if (!strcmp(k, "hz")) o->rate = n; else if (!strcmp(k, "canales")) o->ch = n; else if (!strcmp(k, "kbps")) o->kbps = n; else if (!strcmp(k, "normalizar")) o->norm = n;
    else if (!strcmp(k, "papel")) o->paper = n; else if (!strcmp(k, "letra")) o->fsize = n; else if (!strcmp(k, "separador")) o->delim = v[0] == 't' ? '\t' : v[0] == 'a' ? 0 : v[0]; else if (!strcmp(k, "cabecera")) o->header = n;
    else if (!strcmp(k, "gif_fps")) o->gif_fps = n; else if (!strcmp(k, "gif_ancho")) o->gif_w = n; else if (!strcmp(k, "clave")) strncpy0(o->pw, v, 128); }
static int ext_in(const char *p, const char *list) { const char *d = strrchr(p, '.'); if (!d || strchr(d, '/')) return 0; char e[16]; int k = 0; for (d++; *d && k < 14; d++) e[k++] = tolower(*d); e[k] = 0; char pat[20], all[300]; snprintf(pat, 20, ",%s,", e); snprintf(all, 300, ",%s,", list); return strstr(all, pat) != 0; }
static int cv_kind(const char *path, const u8 *h, int n) {
    if (z_ends(path, ".tar.gz")) return CK_ARC;
    if (ext_in(path, "png,jpg,jpeg,jpe,bmp,dib,gif,ppm,pgm,pbm,pnm,tga,ico")) return CK_IMG; if (ext_in(path, "wav,wave,mp3,aif,aiff,aifc")) return CK_AUD; if (ext_in(path, "avi")) return CK_VID;
    if (ext_in(path, "txt,text,md,markdown,html,htm,xhtml,rtf,docx,odt,epub")) return CK_DOC; if (ext_in(path, "csv,tsv,tab,json,xml,yaml,yml,xlsx")) return CK_DATA;
    if (ext_in(path, "zip,tar,gz,tgz")) return CK_ARC; if (ext_in(path, "b64,base64")) return CK_B64;
    if (h && n >= 12) { if (h[0] == 0x89 && h[1] == 'P') return CK_IMG; if (h[0] == 0xFF && h[1] == 0xD8) return CK_IMG; if (!memcmp(h, "GIF8", 4) || (h[0] == 'B' && h[1] == 'M')) return CK_IMG;
        if (!memcmp(h, "RIFF", 4) && !memcmp(h + 8, "WAVE", 4)) return CK_AUD; if (!memcmp(h, "RIFF", 4) && !memcmp(h + 8, "AVI ", 4)) return CK_VID; if (!memcmp(h, "ID3", 3) || (h[0] == 0xFF && (h[1] & 0xE0) == 0xE0)) return CK_AUD;
        if (!memcmp(h, "FORM", 4)) return CK_AUD; if (h[0] == 'P' && h[1] == 'K') return CK_ARC; if (h[0] == 0x1F && h[1] == 0x8B) return CK_ARC; if (!memcmp(h, "{\\rtf", 5)) return CK_DOC; }
    return CK_NONE; }
/* salidas posibles para cada tipo (las codificaciones valen para todos) */
static const char *const OUT_IMG[] = {"png", "jpg", "bmp", "gif", "ico", "tga", "ppm", "pdf", "svg", 0};
static const char *const OUT_AUD[] = {"mp3", "wav", "aiff", 0};
static const char *const OUT_VID[] = {"gif", "frames", "png", "wav", "mp3", 0};
static const char *const OUT_DOC[] = {"pdf", "docx", "odt", "epub", "html", "md", "rtf", "txt", 0};
static const char *const OUT_DATA[] = {"xlsx", "csv", "tsv", "json", "xml", "yaml", "html", "md", "txt", 0};
static const char *const OUT_ARC[] = {"zip", "tgz", "tar", 0};
static const char *const OUT_B64[] = {"decode", 0};
static const char *const OUT_ENC[] = {"base64", "hex", "datauri", "sha256", 0};
static int cv_outputs(int k, const char **o, int max) { const char *const *L = k == CK_IMG ? OUT_IMG : k == CK_AUD ? OUT_AUD : k == CK_VID ? OUT_VID : k == CK_DOC ? OUT_DOC : k == CK_DATA ? OUT_DATA : k == CK_ARC ? OUT_ARC : k == CK_B64 ? OUT_B64 : 0; int n = 0;
    if (L) for (int i = 0; L[i] && n < max; i++) if (!strstr(L[i], "-no")) o[n++] = L[i]; for (int i = 0; OUT_ENC[i] && n < max; i++) o[n++] = OUT_ENC[i]; return n; }
static const char *cv_label(const char *id) { static const char *const M[][2] = {{"frames", "ZIP de fotogramas"}, {"png", "PNG"}, {"jpg", "JPG"}, {"tgz", "TAR.GZ"}, {"base64", "Base64"}, {"hex", "Volcado HEX"}, {"datauri", "Data URI"}, {"sha256", "Huella SHA-256"}, {"decode", "Decodificar"}, {"md", "Markdown"}, {"aiff", "AIFF"}};
    for (unsigned i = 0; i < sizeof M / sizeof *M; i++) if (!strcmp(M[i][0], id)) return tr(M[i][1]); static char up[16]; int k = 0; for (; id[k] && k < 15; k++) up[k] = toupper(id[k]); up[k] = 0; return up; }
static const char *cv_ext(const char *id) { if (!strcmp(id, "frames")) return "-fotogramas.zip"; if (!strcmp(id, "tgz")) return ".tar.gz"; if (!strcmp(id, "base64")) return ".b64"; if (!strcmp(id, "hex")) return ".hex.txt"; if (!strcmp(id, "datauri")) return ".datauri.txt"; if (!strcmp(id, "sha256")) return ".sha256.txt"; if (!strcmp(id, "decode")) return "";
    static char e[16]; snprintf(e, 16, ".%s", id); return e; }
static const char *mime_of(const char *p) { static const char *const M[][2] = {{".png", "image/png"}, {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"}, {".gif", "image/gif"}, {".bmp", "image/bmp"}, {".ico", "image/x-icon"}, {".svg", "image/svg+xml"}, {".webp", "image/webp"}, {".mp3", "audio/mpeg"}, {".wav", "audio/wav"}, {".pdf", "application/pdf"}, {".html", "text/html"}, {".htm", "text/html"}, {".txt", "text/plain"}, {".css", "text/css"}, {".js", "text/javascript"}, {".json", "application/json"}, {".xml", "application/xml"}, {".csv", "text/csv"}, {".zip", "application/zip"}, {".woff2", "font/woff2"}, {".ttf", "font/ttf"}};
    for (unsigned i = 0; i < sizeof M / sizeof *M; i++) if (z_ends(p, M[i][0])) return M[i][1]; return "application/octet-stream"; }
static void w_hex(const u8 *d, long n, sb_t *b) { for (long o = 0; o < n; o += 16) { sb_f(b, "%08lx  ", o); for (int i = 0; i < 16; i++) { if (o + i < n) sb_f(b, "%02x ", d[o + i]); else sb_s(b, "   "); if (i == 7) sb_s(b, " "); } sb_s(b, " |"); for (int i = 0; i < 16 && o + i < n; i++) { char c = d[o + i] >= 32 && d[o + i] < 127 ? d[o + i] : '.'; sb_add(b, &c, 1); } sb_s(b, "|\n"); } sb_f(b, "%08lx\n", n); }
static long b64_dec(const char *s, long n, u8 *o) { int v[256]; for (int i = 0; i < 256; i++) v[i] = -1; for (int i = 0; i < 64; i++) v[(u8)B64[i]] = i; v['-'] = 62; v['_'] = 63; u32 acc = 0; int bits = 0; long k = 0;
    for (long i = 0; i < n; i++) { u8 c = s[i]; if (c == '=') break; if (v[c] < 0) continue; acc = acc << 6 | v[c]; bits += 6; if (bits >= 8) { bits -= 8; o[k++] = acc >> bits; } } return k; }
static void img_post(zimg *m, const cvopt *o) {
    if (o->rot == 90 || o->rot == 180 || o->rot == 270) { int W = o->rot == 180 ? m->w : m->h, H = o->rot == 180 ? m->h : m->w; u32 *t = malloc((long)W * H * 4);
        for (int y = 0; y < m->h; y++) for (int x = 0; x < m->w; x++) { u32 p = m->px[(long)y * m->w + x]; int nx, ny; if (o->rot == 90) { nx = m->h - 1 - y; ny = x; } else if (o->rot == 180) { nx = m->w - 1 - x; ny = m->h - 1 - y; } else { nx = y; ny = m->w - 1 - x; } t[(long)ny * W + nx] = p; }
        free(m->px); m->px = t; m->w = W; m->h = H; }
    img_fit(m, o->pct, o->maxw, o->maxh);
    if (o->gray) for (long i = 0; i < (long)m->w * m->h; i++) { u32 p = m->px[i]; u32 g = ((p >> 16 & 255) * 77 + (p >> 8 & 255) * 150 + (p & 255) * 29) >> 8; m->px[i] = g * 0x010101u; } }
static int img_out(zimg *m, const char *id, const cvopt *o, sb_t *b) {
    if (!strcmp(id, "png")) return enc_png(m, b); if (!strcmp(id, "jpg")) return enc_jpg(m, MAX(1, MIN(100, o->quality)), b); if (!strcmp(id, "bmp")) { enc_bmp(m, b); return 0; } if (!strcmp(id, "gif")) { enc_gif(m, b); return 0; }
    if (!strcmp(id, "ico")) return enc_ico(m, b); if (!strcmp(id, "tga")) { enc_tga(m, b); return 0; } if (!strcmp(id, "ppm")) { enc_ppm(m, b); return 0; } if (!strcmp(id, "svg")) { enc_svg(m, b); return 0; }
    if (!strcmp(id, "pdf")) { if (m->w > 2480 || m->h > 2480) img_fit(m, 100, 2480, 2480); return enc_pdf_img(m, o->paper, b); } return -1; }
static int pcm_load(const char *path, const u8 *d, long n, zpcm *p, char *err) { memset(p, 0, sizeof *p); int r = wav_decode(d, n, p, err); if (r == -1) r = aiff_decode(d, n, p, err); if (r == -1) r = mp3_decode(d, n, p, err); if (r == -1) strcpy(err, tr("No se reconoce el sonido")); (void)path; return r ? -1 : 0; }
static int aud_out(zpcm *p, const char *id, const cvopt *o, sb_t *b, prog_fn pr) {
    if (o->ch == 1 || o->ch == 2) pcm_channels(p, o->ch); if (o->rate >= 8000) pcm_rate(p, o->rate); if (o->norm) pcm_normalize(p);
    if (!strcmp(id, "wav")) { enc_wav(p, b); return 0; } if (!strcmp(id, "aiff")) { enc_aiff(p, b); return 0; } if (!strcmp(id, "mp3")) { int k = o->kbps; static const int K[] = {32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320}; int best = 192; for (int i = 0; i < 14; i++) if (K[i] <= k) best = K[i]; return enc_mp3(p, best, b, pr); } return -1; }
/* AVI (el MJPEG que graba ZunzunOS): GIF animado, fotogramas, un fotograma y la pista de sonido */
static int vid_conv(const char *in, const char *id, const cvopt *o, sb_t *b, prog_fn pr, char *err) {
    zavi_r v; int e = zavi_load(&v, in); if (e) { strcpy(err, tr("No es un AVI que se pueda leer")); return -1; } int r = 0;
    if (!strcmp(id, "wav") || !strcmp(id, "mp3")) { if (v.na <= 0 || (v.abits != 16 && v.abits != 8)) { zavi_free(&v); strcpy(err, tr("El vídeo no tiene sonido")); return -1; }
        sb_t raw = {0}; for (int i = 0; i < v.na; i++) { long n; u8 *a = zavi_read_audio(&v, i, &n); if (a) { sb_add(&raw, a, n); free(a); } }
        zpcm p; p.ch = MAX(1, MIN(2, v.achan)); p.rate = v.arate; int bp = v.abits / 8; p.n = raw.n / (bp * v.achan); pcm_cap(&p, p.n); for (long i = 0; i < p.n; i++) for (int c = 0; c < p.ch; c++) { const u8 *q = (u8 *)raw.p + (i * v.achan + c) * bp; p.s[i * p.ch + c] = bp == 2 ? (short)G16(q) : (short)((q[0] - 128) << 8); }
        free(raw.p); r = aud_out(&p, id, o, b, pr); pcm_free(&p); zavi_free(&v); return r; }
    if (v.nf <= 0) { zavi_free(&v); strcpy(err, tr("El vídeo no tiene imagen")); return -1; }
    if (!strcmp(id, "png")) { long n; u8 *j = zavi_read(&v, v.nf / 2, &n); zimg m = {0}; if (!j || z_jpeg_decode(j, n, &m.px, &m.w, &m.h)) { free(j); zavi_free(&v); strcpy(err, tr("No se puede leer el fotograma")); return -1; } free(j); img_post(&m, o); r = enc_png(&m, b); img_free(&m); zavi_free(&v); return r; }
    if (!strcmp(id, "frames")) { mzip mz = {0}; for (int i = 0; i < v.nf; i++) { long n; u8 *j = zavi_read(&v, i, &n); if (!j) continue; char nm[40]; snprintf(nm, 40, "fotograma-%05d.jpg", i + 1); mz_add(&mz, nm, j, n, 1); free(j); if (pr && pr(i, v.nf, nm)) { free(mz.z.p); free(mz.cd.p); zavi_free(&v); strcpy(err, tr("Cancelado")); return -1; } } mz_end(&mz, b); zavi_free(&v); return 0; }
    if (!strcmp(id, "gif")) { double fps = 1e6 / MAX(1u, v.us_frame), want = MAX(1, MIN(25, o->gif_fps)); if (want > fps) want = fps; double step = fps / want; int delay = (int)(100.0 / want + 0.5), w = 0, h = 0, cnt = 0;
        for (double f = 0; f < v.nf && cnt < 1500; f += step, cnt++) { long n; u8 *j = zavi_read(&v, (int)f, &n); zimg m = {0}; if (!j || z_jpeg_decode(j, n, &m.px, &m.w, &m.h)) { free(j); continue; } free(j);
            if (!w) { cvopt q = *o; q.maxw = MAX(16, o->gif_w); img_post(&m, &q); w = m.w; h = m.h; gif_begin(b, w, h, 1); } else { if (o->rot) { cvopt q = *o; q.pct = 100; q.maxw = q.maxh = 0; img_post(&m, &q); } img_resize(&m, w, h); if (o->gray) { cvopt q; cv_defaults(&q); q.gray = 1; img_post(&m, &q); } }
            gif_frame(b, &m, delay, 0); img_free(&m); if (pr && pr((long)f, v.nf, "GIF")) { zavi_free(&v); strcpy(err, tr("Cancelado")); return -1; } }
        zavi_free(&v); if (!w) { strcpy(err, tr("No se puede leer el fotograma")); return -1; } u8 t = 0x3B; sb_add(b, &t, 1); return 0; }
    zavi_free(&v); return -1; }
static int arc_repack(const char *in, const char *id, const char *out, const cvopt *o, prog_fn pr, char *err) {
    arch a; if (arch_open(&a, in)) { strncpy0(err, a.err, 200); arch_close(&a); return -1; } alist L = {0}; u8 **keep = calloc(a.n + 1, sizeof(u8 *));
    for (int i = 0; i < a.n; i++) { aent *x = &a.e[i]; char rel[600]; safe_rel(x->name, rel, 600); if (!rel[0]) continue; u8 *d = 0; char e2[160] = ""; int r = arch_get(&a, x, o->pw, &d, e2);
        if (r) { for (int k = 0; k < i; k++) free(keep[k]); free(keep); al_free(&L); arch_close(&a); strncpy0(err, r == -2 ? tr("Este archivo está cifrado: escribe la contraseña") : r == -3 ? tr("Contraseña incorrecta") : e2, 200); return -1; }
        keep[i] = d; if (x->dir) { char nm[620]; snprintf(nm, 620, "%s/", rel); al_add(&L, 0, nm, 0, 1, 0); } else al_add(&L, 0, rel, x->size, 0, d); }
    astat st; int r = arc_create(!strcmp(id, "zip") ? AF_ZIP : !strcmp(id, "tgz") ? AF_TGZ : AF_TAR, &L, out, o->level, 0, pr, &st); if (r) strncpy0(err, st.err, 200);
    for (int i = 0; i < a.n; i++) free(keep[i]); free(keep); al_free(&L); arch_close(&a); return r; }
/* la conversion: in -> out con el formato id */
static int cv_convert(const char *in, const char *id, const char *out, const cvopt *o, prog_fn pr, char *err) {
    err[0] = 0; zdirent st; if (z_stat(in, &st) || st.is_dir) { strcpy(err, tr("No se encuentra el archivo")); return -1; }
    u8 h[16] = {0}; { int fd = z_open(in, O_READ); if (fd >= 0) { z_read(fd, h, 16); z_close(fd); } } int k = cv_kind(in, h, 16);
    if (k == CK_ARC && (!strcmp(id, "zip") || !strcmp(id, "tgz") || !strcmp(id, "tar"))) return arc_repack(in, id, out, o, pr, err);
    sb_t b = {0}; sb_s(&b, ""); int r = -1;
    if (k == CK_VID && strcmp(id, "base64") && strcmp(id, "hex") && strcmp(id, "datauri") && strcmp(id, "sha256")) r = vid_conv(in, id, o, &b, pr, err);
    else { long n; u8 *d = z_readfile(in, &n); if (!d) { free(b.p); strcpy(err, tr("No se puede leer el archivo")); return -1; } if (pr) pr(0, 1, base_name(in));
        if (!strcmp(id, "base64")) { b64_enc(&b, d, n, 76); r = 0; }
        else if (!strcmp(id, "hex")) { w_hex(d, n, &b); r = 0; }
        else if (!strcmp(id, "datauri")) { sb_f(&b, "data:%s;base64,", mime_of(in)); b64_enc(&b, d, n, 0); sb_s(&b, "\n"); r = 0; }
        else if (!strcmp(id, "sha256")) { u8 hs[32]; zc_hash(256, d, n, hs); for (int i = 0; i < 32; i++) sb_f(&b, "%02x", hs[i]); sb_f(&b, "  %s\n\nSHA-256  ", base_name(in)); for (int i = 0; i < 32; i++) sb_f(&b, "%02x", hs[i]); sb_f(&b, "\nCRC-32   %08x\n%s %ld bytes\n", z_crc32(d, n), tr("Tamaño:"), n); r = 0; }
        else if (!strcmp(id, "decode")) { const char *s = (char *)d; long sn = n; const char *c = 0; for (long i = 0; i + 7 < MIN(n, 200L); i++) if (!strncmp(s + i, ";base64,", 8)) { c = s + i + 8; break; } if (c) { sn -= c - s; s = c; } u8 *o2 = malloc(sn * 3 / 4 + 8); long on = b64_dec(s, sn, o2); sb_add(&b, o2, on); free(o2); r = on > 0 ? 0 : -1; if (r) strcpy(err, tr("No hay Base64 que decodificar")); }
        else if (k == CK_IMG) { zimg m; if (img_decode(d, n, &m)) strcpy(err, tr("No se puede leer la imagen (¿formato o variante no soportados?)")); else { img_post(&m, o); r = img_out(&m, id, o, &b); if (r && !err[0]) strcpy(err, tr("No se puede escribir la imagen")); img_free(&m); } }
        else if (k == CK_AUD) { zpcm p; if (!pcm_load(in, d, n, &p, err)) { r = aud_out(&p, id, o, &b, pr); if (r == -2) strcpy(err, tr("Cancelado")); pcm_free(&p); } }
        else if (k == CK_DOC) { zdoc dc; if (!doc_read(in, d, n, &dc, err)) { r = 0;
                if (!strcmp(id, "pdf")) r = w_pdf(&dc, o->paper, o->fsize, &b); else if (!strcmp(id, "docx")) w_docx(&dc, &b); else if (!strcmp(id, "odt")) w_odt(&dc, &b); else if (!strcmp(id, "epub")) w_epub(&dc, &b); else if (!strcmp(id, "html")) w_html(&dc, &b); else if (!strcmp(id, "md")) w_md(&dc, &b); else if (!strcmp(id, "rtf")) w_rtf(&dc, &b); else if (!strcmp(id, "txt")) w_txt(&dc, &b); else r = -1;
                doc_free(&dc); } }
        else if (k == CK_DATA) { ztab t; if (!tab_read(in, d, n, o->delim, &t, err)) { r = 0; int hd = o->header;
                if (!strcmp(id, "xlsx")) w_xlsx(&t, hd, &b); else if (!strcmp(id, "csv")) w_csv(&t, o->delim && o->delim != '\t' ? o->delim : ',', &b); else if (!strcmp(id, "tsv")) w_csv(&t, '\t', &b); else if (!strcmp(id, "json")) w_json(&t, hd, &b); else if (!strcmp(id, "xml")) w_xml(&t, hd, &b); else if (!strcmp(id, "yaml")) w_yaml(&t, hd, &b); else if (!strcmp(id, "html")) w_html_tab(&t, hd, &b); else if (!strcmp(id, "md")) w_md_tab(&t, hd, &b); else if (!strcmp(id, "txt")) w_txt_tab(&t, &b); else r = -1;
                tab_free(&t); } }
        free(d); }
    if (r) { free(b.p); if (!err[0]) strcpy(err, tr("Esa conversión no es posible")); return -1; }
    int e = z_writefile(out, b.p, b.n); free(b.p); z_sync(); if (e) { snprintf(err, 200, "%s: %s", z_errstr(e), out); return -1; } if (pr) pr(1, 1, ""); return 0; }
/* nombre de salida: carpeta + nombre sin extension + la del formato; si ya existe, (2), (3)... */
static void cv_outname(const char *in, const char *id, const char *dir, char *o, int cap) {
    char bn[300]; strncpy0(bn, base_name(in), 300); char *dot = strrchr(bn, '.'); if (z_ends(bn, ".tar.gz")) bn[strlen(bn) - 7] = 0; else if (!strcmp(id, "decode")) { if (z_ends(bn, ".b64") || z_ends(bn, ".base64")) *dot = 0; else if (z_ends(bn, ".txt")) { *dot = 0; strcat(bn, ".bin"); } else strcat(bn, ".bin"); } else if (dot && dot != bn && !(id[0] == 's' && !strcmp(id, "sha256")) && strcmp(id, "base64") && strcmp(id, "hex") && strcmp(id, "datauri")) *dot = 0;
    const char *ex = cv_ext(id); snprintf(o, cap, "%s/%s%s", dir, bn, ex); zdirent st; for (int k = 2; !z_stat(o, &st) && k < 1000; k++) { int l = strlen(ex); char b2[320]; snprintf(b2, 320, "%s (%d)", bn, k); if (!strcmp(id, "decode") && strrchr(bn, '.')) { char *d2 = strrchr(bn, '.'); snprintf(b2, 320, "%.*s (%d)%s", (int)(d2 - bn), bn, k, d2); } snprintf(o, cap, "%s/%s%s", dir, b2, ex); (void)l; } }
