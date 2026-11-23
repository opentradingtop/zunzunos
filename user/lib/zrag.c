/* Busqueda en documentos para la IA (RAG) sin red ni modelos de embeddings: los archivos se pasan a texto,
   se trocean en fragmentos de unas 1200 letras que se solapan un poco y, para cada pregunta, se puntuan con
   BM25 (la formula clasica de los buscadores). Los mejores fragmentos se mandan al modelo junto a la pregunta.
   Formatos: txt, md, csv, html, rtf, docx, odt, xlsx (texto de las celdas) y pdf (con el Lector PDF sin ventana). */
#include "z.h"

#define CH 1200
#define OV 200

static int fold(u32 c) {   /* minusculas y sin tildes, para comparar palabras */
    if (c >= 'A' && c <= 'Z') return c + 32;
    static const u16 m[][2] = {{0xE1,'a'},{0xE0,'a'},{0xE2,'a'},{0xE4,'a'},{0xC1,'a'},{0xC0,'a'},{0xE9,'e'},{0xE8,'e'},{0xEA,'e'},{0xEB,'e'},{0xC9,'e'},{0xED,'i'},{0xEC,'i'},{0xEF,'i'},{0xCD,'i'},
        {0xF3,'o'},{0xF2,'o'},{0xF4,'o'},{0xF6,'o'},{0xD3,'o'},{0xFA,'u'},{0xF9,'u'},{0xFC,'u'},{0xDA,'u'},{0xDC,'u'},{0xF1,'n'},{0xD1,'n'},{0xE7,'c'},{0xC7,'c'}};
    for (unsigned i = 0; i < sizeof m / sizeof *m; i++) if (m[i][0] == c) return m[i][1];
    return c < 128 ? (int)c : 0; }
static const char *STOP[] = {"que","los","las","del","con","una","por","para","como","mas","pero","sus","este","esta","esto","son","fue","ser","han","hay","muy","sin","sobre","entre","cuando","todo","tambien",
    "donde","desde","porque","cual","the","and","for","with","that","this","from","are","was","were","what","which","you","your","how","not","but","have","has","into","about","its","de","la","el","en","y","a","se"};
static int stopw(const char *w) { for (unsigned i = 0; i < sizeof STOP / sizeof *STOP; i++) if (!strcmp(w, STOP[i])) return 1; return 0; }
/* palabras de un texto: devuelve cuantas, en o[][24] (sin repetir si uniq) */
static int words(const char *s, long n, char (*o)[24], int max, int uniq) { int k = 0; char w[24]; int l = 0; const char *p = s, *e = s + n;
    while (p <= e && k < max) { u32 c = p < e ? (u32)utf8_next(&p) : 0; if (p > e) c = 0; int f = c ? fold(c) : 0; int al = (f >= 'a' && f <= 'z') || (f >= '0' && f <= '9');
        if (al && l < 23) { w[l++] = f; continue; } if (al) continue;
        if (l >= 3 || (l >= 2 && w[0] >= '0' && w[0] <= '9')) { w[l] = 0; if (!stopw(w)) { int dup = 0; if (uniq) for (int i = 0; i < k; i++) if (!strcmp(o[i], w)) { dup = 1; break; } if (!dup) strcpy(o[k++], w); } }
        l = 0; if (!c) break; }
    return k; }

static char *strip_tags(const char *h) { sb_t b = {0}; sb_s(&b, ""); int in = 0, skip = 0;
    for (const char *p = h; *p; p++) { if (*p == '<') { in = 1; if (!strncasecmp(p, "<script", 7) || !strncasecmp(p, "<style", 6)) skip = 1; else if (!strncasecmp(p, "</script", 8) || !strncasecmp(p, "</style", 7)) skip = 0;
            if (!strncasecmp(p, "<p", 2) || !strncasecmp(p, "<br", 3) || !strncasecmp(p, "<li", 3) || !strncasecmp(p, "<h", 2) || !strncasecmp(p, "<tr", 3) || !strncasecmp(p, "<div", 4)) sb_s(&b, "\n"); continue; }
        if (*p == '>') { in = 0; continue; } if (in || skip) continue;
        if (*p == '&') { static const char *E[][2] = {{"&amp;","&"},{"&lt;","<"},{"&gt;",">"},{"&quot;","\""},{"&nbsp;"," "},{"&#39;","'"},{"&aacute;","\xc3\xa1"},{"&eacute;","\xc3\xa9"},{"&iacute;","\xc3\xad"},{"&oacute;","\xc3\xb3"},{"&uacute;","\xc3\xba"},{"&ntilde;","\xc3\xb1"}};
            int hit = 0; for (unsigned i = 0; i < sizeof E / sizeof *E; i++) { int l = strlen(E[i][0]); if (!strncasecmp(p, E[i][0], l)) { sb_s(&b, E[i][1]); p += l - 1; hit = 1; break; } } if (hit) continue; }
        sb_add(&b, p, 1); }
    return b.p; }
/* xlsx: texto de las cadenas compartidas y de los valores, hoja por hoja */
static char *xlsx_text(const u8 *d, long n) { sb_t b = {0}; sb_s(&b, ""); long l; u8 *x = zip_get(d, n, "xl/sharedStrings.xml", &l);
    if (x) { char *t = strip_tags((char *)x); sb_s(&b, t); free(t); free(x); sb_s(&b, "\n"); }
    for (int i = 1; i < 30; i++) { char nm[40]; snprintf(nm, 40, "xl/worksheets/sheet%d.xml", i); x = zip_get(d, n, nm, &l); if (!x) break;
        for (char *p = (char *)x; (p = strstr(p, "<v>")); ) { p += 3; char *e = strstr(p, "</v>"); if (!e) break; sb_add(&b, p, e - p); sb_s(&b, " "); p = e; } free(x); sb_s(&b, "\n"); }
    return b.p; }

/* texto de un archivo; los PDF los lee el Lector PDF sin ventana (pump mantiene viva la ventana mientras) */
char *zr_read(const char *path, long *len, char *err, int ecap, int (*pump)(void *), void *ctx) {
    if (err) err[0] = 0; char *r = 0;
    if (z_ends(path, ".pdf")) { char out[80]; snprintf(out, 80, "/rd/tmp/zrag-%lu.txt", (unsigned long)(z_ms() % 1000000)); z_mkdir("/rd/tmp"); z_unlink(out);
        char arg[400]; snprintf(arg, 400, "--texto %s|%s", path, out); int pid = z_spawn("/rd/bin/pdf", arg); if (pid < 0) { if (err) strncpy0(err, tr("no se pudo abrir el Lector PDF"), ecap); return 0; }
        u64 t0 = z_ms(); for (;;) { zproc ps[64]; int n = z_procs(ps, 64), alive = 0; for (int i = 0; i < n; i++) if (ps[i].pid == pid) alive = 1; if (!alive) break;
            if (z_ms() - t0 > 90000) { z_kill(pid); if (err) strncpy0(err, tr("el PDF tarda demasiado"), ecap); return 0; } if (pump && pump(ctx)) { z_kill(pid); return 0; } z_sleep(50); }
        r = z_readfile(out, 0); z_unlink(out); if (!r && err) strncpy0(err, tr("no tiene texto legible"), ecap); }
    else if (z_ends(path, ".html") || z_ends(path, ".htm")) { char *h = z_readfile(path, 0); if (h) { r = strip_tags(h); free(h); } }
    else if (z_ends(path, ".xlsx")) { long n; u8 *d = z_readfile(path, &n); if (d) { r = xlsx_text(d, n); free(d); } }
    else { long n; r = doc_import(path, &n); if (r) { long bin = 0; for (long i = 0; i < MIN(n, 4096L); i++) if (!r[i] || ((u8)r[i] < 9)) bin++; if (bin > 8) { free(r); r = 0; if (err) strncpy0(err, tr("no es un documento de texto"), ecap); } } }
    if (!r) { if (err && !err[0]) strncpy0(err, tr("no se pudo leer"), ecap); return 0; }
    /* sin espacios al final de las lineas, sin \r, saltos de pagina como linea en blanco y como mucho una linea vacia seguida */
    long w = 0, nl = 0; for (long i = 0; r[i]; i++) { char c = r[i]; if (c == '\r') continue; if (c == '\f') c = '\n'; if (c == '\t') c = ' ';
        if (c == '\n') { while (w && r[w - 1] == ' ') w--; if (++nl > 2) continue; } else if (c != ' ') nl = 0; r[w++] = c; }
    while (w && (r[w - 1] == '\n' || r[w - 1] == ' ')) w--; r[w] = 0; long n = w; if (len) *len = n; return r; }

/* fragmentos: cortes de unas CH letras en un salto de parrafo, de linea o un espacio, con OV letras de solape */
int zr_chunk(zrdoc *d) { free(d->co); free(d->cl); d->co = 0; d->cl = 0; d->nch = 0; long p = 0, n = d->n; int cap = 0;
    while (p < n) { long e = MIN(n, p + CH); if (e < n) { long best = -1; for (long i = e; i > p + CH / 2; i--) { if (d->txt[i] == '\n' && d->txt[i - 1] == '\n') { best = i; break; } } if (best < 0) for (long i = e; i > p + CH / 2; i--) if (d->txt[i] == '\n' || d->txt[i] == '.') { best = i + 1; break; } if (best < 0) for (long i = e; i > p + CH / 2; i--) if (d->txt[i] == ' ') { best = i + 1; break; } if (best > 0) e = best; }
        while (e < n && ((u8)d->txt[e] & 0xC0) == 0x80) e++;
        if (d->nch == cap) { cap = cap * 2 + 16; d->co = realloc(d->co, cap * sizeof(long)); d->cl = realloc(d->cl, cap * sizeof(int)); } d->co[d->nch] = p; d->cl[d->nch] = e - p; d->nch++;
        if (e >= n) break; long q = e - OV; while (q > p && d->txt[q] != ' ' && d->txt[q] != '\n') q--; p = q > p ? q + 1 : e; while (p < n && ((u8)d->txt[p] & 0xC0) == 0x80) p++; }
    return d->nch; }

typedef struct { int d, c; double s; } hit_t;
static int hcmp(const void *a, const void *b) { double x = ((const hit_t *)a)->s, y = ((const hit_t *)b)->s; return x < y ? 1 : x > y ? -1 : 0; }
static int ocmp(const void *a, const void *b) { const hit_t *x = a, *y = b; return x->d != y->d ? x->d - y->d : x->c - y->c; }
/* elige fragmentos para la pregunta q hasta budget letras y los escribe en out con su origen.
   Si todo cabe, van enteros. Devuelve cuantos fragmentos van; *docs dice de cuantos archivos. */
int zr_search(zrdoc *D, int nd, const char *q, long budget, sb_t *out, int *docs) {
    long tot = 0; int nc = 0; for (int i = 0; i < nd; i++) { if (!D[i].txt) continue; tot += D[i].n; if (!D[i].nch) zr_chunk(&D[i]); nc += D[i].nch; } if (docs) *docs = 0; if (!nc) return 0;
    hit_t *H = malloc(nc * sizeof *H); int k = 0;
    if (tot <= budget) { for (int i = 0; i < nd; i++) if (D[i].txt) { H[k].d = i; H[k].c = -1; H[k].s = 1; k++; } }
    else { char (*qw)[24] = malloc(64 * 24); int nq = words(q, strlen(q), qw, 64, 1); char (*cw)[24] = malloc(600 * 24);
        double avg = 0; for (int i = 0; i < nd; i++) for (int c = 0; c < D[i].nch; c++) avg += D[i].cl[c]; avg /= nc;
        int *df = calloc(nq + 1, sizeof(int)); int (*tf)[64] = malloc((long)nc * sizeof *tf); int j = 0;
        for (int i = 0; i < nd; i++) for (int c = 0; c < D[i].nch; c++, j++) { memset(tf[j], 0, sizeof tf[j]); int nw = words(D[i].txt + D[i].co[c], D[i].cl[c], cw, 600, 0);
            for (int w = 0; w < nw; w++) for (int t = 0; t < nq; t++) if (!strcmp(cw[w], qw[t]) || (strlen(qw[t]) >= 5 && !strncmp(cw[w], qw[t], strlen(qw[t]) - 1) && strlen(cw[w]) <= strlen(qw[t]) + 2)) tf[j][t]++;
            for (int t = 0; t < nq; t++) if (tf[j][t]) df[t]++; }
        j = 0; for (int i = 0; i < nd; i++) for (int c = 0; c < D[i].nch; c++, j++) { double s = 0; for (int t = 0; t < nq; t++) if (tf[j][t]) { double idf = z_ln((nc - df[t] + 0.5) / (df[t] + 0.5) + 1.0), f = tf[j][t];
                s += idf * f * 2.2 / (f + 1.2 * (0.25 + 0.75 * D[i].cl[c] / avg)); } H[k].d = i; H[k].c = c; H[k].s = s - c * 1e-6; k++; }
        free(df); free(tf); free(qw); free(cw);
        /* primero los fragmentos con puntos; despues, si queda sitio, el mejor de cada archivo que no salio (asi el modelo sabe de todos) */
        qsort(H, k, sizeof *H, hcmp); long used = 0; int keep = 0; char *got = calloc(nd, 1);
        for (int i = 0; i < k; i++) { if (H[i].s <= 0) break; int l = D[H[i].d].cl[H[i].c]; if (used + l > budget) { if (keep) continue; } got[H[i].d] = 1; hit_t t = H[i]; H[i] = H[keep]; H[keep++] = t; used += l; if (used >= budget) break; }
        for (int i = keep; i < k && used < budget; i++) { if (got[H[i].d]) continue; int l = D[H[i].d].cl[H[i].c]; if (used + l > budget) continue; got[H[i].d] = 1; hit_t t = H[i]; H[i] = H[keep]; H[keep++] = t; used += l; }
        free(got);
        k = keep; qsort(H, k, sizeof *H, ocmp); }
    int seen[64] = {0}, nds = 0;
    for (int i = 0; i < k; i++) { zrdoc *d = &D[H[i].d]; if (H[i].d < 64 && !seen[H[i].d]) { seen[H[i].d] = 1; nds++; }
        if (H[i].c < 0) sb_f(out, "\n[%s]\n", d->name); else sb_f(out, "\n[%s, %s %d %s %d]\n", d->name, tr("fragmento"), H[i].c + 1, tr("de"), d->nch);
        long o = H[i].c < 0 ? 0 : d->co[H[i].c], l = H[i].c < 0 ? d->n : d->cl[H[i].c]; sb_add(out, d->txt + o, l); sb_s(out, "\n"); }
    free(H); if (docs) *docs = nds; return k; }
void zr_free(zrdoc *d) { free(d->txt); free(d->co); free(d->cl); d->txt = 0; d->co = 0; d->cl = 0; d->n = 0; d->nch = 0; }
