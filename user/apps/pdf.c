/* Lector PDF: texto (con su posicion y tamano), rectangulos e imagenes JPEG de cada pagina.
   Lee tablas de objetos clasicas y comprimidas (ObjStm), FlateDecode, ToUnicode, WinAnsi y Differences. */
#include "z.h"
enum { PN_NULL, PN_NUM, PN_NAME, PN_STR, PN_ARR, PN_DICT, PN_REF, PN_BOOL };
typedef struct po { int t, cnt, ref; double n; char *s; int sl; struct po **a; char **k; long end; } po;
static u8 *F; static long FN; static zwin *W; static char path[300], msg[200];
/* tabla de objetos: desplazamiento en el archivo o dentro de un flujo de objetos */
typedef struct { const u8 *base; long len, off; po *cache; } oe_t;
static oe_t *OT; static int NOB;
/* ---------- analizador ---------- */
static int ws(int c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == 0; }
static int delim(int c) { return ws(c) || c == '/' || c == '[' || c == ']' || c == '<' || c == '>' || c == '(' || c == ')' || c == '{' || c == '}' || c == '%'; }
typedef struct { const u8 *p, *e; } lx_t;
static void skip(lx_t *l) { for (;;) { while (l->p < l->e && ws(*l->p)) l->p++; if (l->p < l->e && *l->p == '%') { while (l->p < l->e && *l->p != '\n' && *l->p != '\r') l->p++; continue; } break; } }
static po *mk(int t) { po *o = calloc(1, sizeof *o); o->t = t; return o; }
static void push(po *o, po *v, char *key) { if (!(o->cnt & 7)) { o->a = realloc(o->a, (o->cnt + 8) * sizeof *o->a); if (o->t == PN_DICT) o->k = realloc(o->k, (o->cnt + 8) * sizeof *o->k); } o->a[o->cnt] = v; if (key) o->k[o->cnt] = key; o->cnt++; }
static int hexv(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static po *parse(lx_t *l, int depth) {
    skip(l); if (l->p >= l->e || depth > 40) return mk(PN_NULL); const u8 *p = l->p; int c = *p;
    if (c == '/') { l->p++; const u8 *s = l->p; while (l->p < l->e && !delim(*l->p)) l->p++; po *o = mk(PN_NAME); o->sl = l->p - s; o->s = malloc(o->sl + 1); int k = 0; for (const u8 *q = s; q < l->p; q++) { if (*q == '#' && q + 2 < l->p && hexv(q[1]) >= 0) { o->s[k++] = hexv(q[1]) * 16 + hexv(q[2]); q += 2; } else o->s[k++] = *q; } o->s[k] = 0; o->sl = k; return o; }
    if (c == '(') { l->p++; int dep = 1; po *o = mk(PN_STR); char *b = malloc(l->e - l->p + 1); int k = 0;
        while (l->p < l->e) { c = *l->p++; if (c == '\\') { if (l->p >= l->e) break; c = *l->p++; if (c == 'n') b[k++] = '\n'; else if (c == 'r') b[k++] = '\r'; else if (c == 't') b[k++] = '\t'; else if (c == 'b') b[k++] = 8; else if (c == 'f') b[k++] = 12; else if (c >= '0' && c <= '7') { int v = c - '0'; for (int i = 0; i < 2 && l->p < l->e && *l->p >= '0' && *l->p <= '7'; i++) v = v * 8 + *l->p++ - '0'; b[k++] = v; } else if (c == '\r' || c == '\n') { if (c == '\r' && l->p < l->e && *l->p == '\n') l->p++; } else b[k++] = c; }
            else if (c == '(') { dep++; b[k++] = c; } else if (c == ')') { if (!--dep) break; b[k++] = c; } else b[k++] = c; }
        o->s = b; o->sl = k; return o; }
    if (c == '<' && p + 1 < l->e && p[1] == '<') { l->p += 2; po *o = mk(PN_DICT); for (;;) { skip(l); if (l->p >= l->e) break; if (*l->p == '>' && l->p + 1 < l->e && l->p[1] == '>') { l->p += 2; break; } po *k = parse(l, depth + 1); if (k->t != PN_NAME) { if (l->p < l->e) l->p++; continue; } po *v = parse(l, depth + 1); push(o, v, k->s); free(k); } return o; }
    if (c == '<') { l->p++; po *o = mk(PN_STR); char *b = malloc((l->e - l->p) / 2 + 2); int k = 0, hi = -1; while (l->p < l->e && *l->p != '>') { int v = hexv(*l->p++); if (v < 0) continue; if (hi < 0) hi = v; else { b[k++] = hi * 16 + v; hi = -1; } } if (hi >= 0) b[k++] = hi * 16; if (l->p < l->e) l->p++; o->s = b; o->sl = k; return o; }
    if (c == '[') { l->p++; po *o = mk(PN_ARR); for (;;) { skip(l); if (l->p >= l->e) break; if (*l->p == ']') { l->p++; break; } push(o, parse(l, depth + 1), 0); } return o; }
    if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.') {
        const u8 *s = l->p; l->p++; while (l->p < l->e && !delim(*l->p)) l->p++; char t[40]; int n = MIN(l->p - s, 39L); memcpy(t, s, n); t[n] = 0; po *o = mk(PN_NUM); o->n = atof(t);
        lx_t q = *l; skip(&q); const u8 *s2 = q.p; while (q.p < q.e && *q.p >= '0' && *q.p <= '9') q.p++; if (q.p > s2 && q.p < q.e && !strchr("0123456789", t[0] == '-' ? 'x' : '0') ) {}
        if (q.p > s2) { lx_t r = q; skip(&r); if (r.p < r.e && *r.p == 'R' && (r.p + 1 >= r.e || delim(r.p[1]))) { o->t = PN_REF; o->ref = (int)o->n; l->p = r.p + 1; } }
        return o; }
    const u8 *s = l->p; while (l->p < l->e && !delim(*l->p)) l->p++; if (l->p == s) l->p++; int n = l->p - s;
    if (n == 4 && !memcmp(s, "true", 4)) { po *o = mk(PN_BOOL); o->n = 1; return o; } if (n == 5 && !memcmp(s, "false", 5)) return mk(PN_BOOL);
    po *o = mk(PN_NULL); o->s = malloc(n + 1); memcpy(o->s, s, n); o->s[n] = 0; o->sl = n; return o; /* palabra suelta (operador) */
}
static po *getobj(int num);
static po *res(po *o) { int g = 0; while (o && o->t == PN_REF && g++ < 20) o = getobj(o->ref); return o; }
static po *dget(po *d, const char *k) { d = res(d); if (!d || d->t != PN_DICT) return 0; for (int i = 0; i < d->cnt; i++) if (!strcmp(d->k[i], k)) return res(d->a[i]); return 0; }
static double num(po *o, double def) { o = res(o); return o && o->t == PN_NUM ? o->n : def; }
static int isname(po *o, const char *n) { o = res(o); return o && o->t == PN_NAME && !strcmp(o->s, n); }
/* ---------- objetos y flujos ---------- */
static po *getobj(int num) {
    if (num <= 0 || num >= NOB || !OT[num].base) return 0; oe_t *e = &OT[num]; if (e->cache) return e->cache;
    lx_t l = {e->base + e->off, e->base + e->len}; if (e->base == F) { skip(&l); while (l.p < l.e && *l.p >= '0' && *l.p <= '9') l.p++; skip(&l); while (l.p < l.e && *l.p >= '0' && *l.p <= '9') l.p++; skip(&l); if (l.p + 3 <= l.e && !memcmp(l.p, "obj", 3)) l.p += 3; }
    e->cache = mk(PN_NULL); po *o = parse(&l, 0); o->end = l.p - e->base; e->cache = o; return o;
}
static u8 *stream(int onum, long *len) {
    po *o = getobj(onum); if (!o || o->t != PN_DICT) return 0; oe_t *e = &OT[onum]; if (e->base != F) return 0;
    const u8 *p = F + o->end, *end = F + FN; while (p < end && ws(*p)) p++; if (end - p < 6 || memcmp(p, "stream", 6)) return 0; p += 6; if (*p == '\r') p++; if (*p == '\n') p++;
    long L = (long)num(dget(o, "Length"), -1); if (L < 0 || p + L > end) { const u8 *q = p; while (q + 9 <= end && memcmp(q, "endstream", 9)) q++; L = q - p; }
    po *f = dget(o, "Filter"); const char *flt[4] = {0}; int nf = 0; if (f && f->t == PN_NAME) flt[nf++] = f->s; else if (f && f->t == PN_ARR) for (int i = 0; i < f->cnt && nf < 4; i++) { po *x = res(f->a[i]); if (x && x->t == PN_NAME) flt[nf++] = x->s; }
    u8 *d = malloc(L + 1); memcpy(d, p, L); long n = L;
    for (int i = 0; i < nf; i++) {
        if (!strcmp(flt[i], "FlateDecode") || !strcmp(flt[i], "Fl")) { long cap = n * 6 + 65536; u8 *o2 = 0; long r = -1; for (int t = 0; t < 3 && r < 0; t++, cap *= 4) { free(o2); o2 = malloc(cap); r = z_inflate(d + 2, n - 2, o2, cap); } free(d); if (r < 0) { free(o2); return 0; } d = o2; n = r;
            po *dp = dget(o, "DecodeParms"); if (dp && dp->t == PN_ARR) dp = dp->cnt ? res(dp->a[0]) : 0; int pr = (int)num(dget(dp, "Predictor"), 1);
            if (pr >= 10) { int cols = (int)num(dget(dp, "Columns"), 1) * (int)num(dget(dp, "Colors"), 1) * (int)num(dget(dp, "BitsPerComponent"), 8) / 8, bpp = MAX(1, (int)num(dget(dp, "Colors"), 1) * (int)num(dget(dp, "BitsPerComponent"), 8) / 8); long rows = n / (cols + 1); u8 *o3 = calloc(rows * cols + 1, 1);
                for (long r2 = 0; r2 < rows; r2++) { int ft = d[r2 * (cols + 1)]; u8 *in = d + r2 * (cols + 1) + 1, *out = o3 + r2 * cols, *up = r2 ? o3 + (r2 - 1) * cols : 0; for (int x = 0; x < cols; x++) { int a = x >= bpp ? out[x - bpp] : 0, b = up ? up[x] : 0, c = up && x >= bpp ? up[x - bpp] : 0, v = in[x]; if (ft == 1) v += a; else if (ft == 2) v += b; else if (ft == 3) v += (a + b) / 2; else if (ft == 4) { int pp = a + b - c, pa = pp > a ? pp - a : a - pp, pb = pp > b ? pp - b : b - pp, pc = pp > c ? pp - c : c - pp; v += pa <= pb && pa <= pc ? a : pb <= pc ? b : c; } out[x] = v; } }
                free(d); d = o3; n = rows * cols; } }
        else if (!strcmp(flt[i], "ASCIIHexDecode") || !strcmp(flt[i], "AHx")) { u8 *o2 = malloc(n / 2 + 2); long k = 0; int hi = -1; for (long j = 0; j < n && d[j] != '>'; j++) { int v = hexv(d[j]); if (v < 0) continue; if (hi < 0) hi = v; else { o2[k++] = hi * 16 + v; hi = -1; } } free(d); d = o2; n = k; }
        else if (!strcmp(flt[i], "DCTDecode") || !strcmp(flt[i], "DCT")) break; else { free(d); return 0; }
    }
    d[n] = 0; *len = n; return d;
}
/* busca "N G obj" con la forma exacta; devuelve el numero y el inicio, o 0 */
static int objhdr(long i, long *start) { if (i < 4 || !(i + 3 >= FN || delim(F[i + 3]))) return 0; long j = i - 1; if (!ws(F[j])) return 0; while (j > 0 && ws(F[j])) j--; long ge = j; while (j > 0 && F[j] >= '0' && F[j] <= '9') j--; if (j == ge || !ws(F[j])) return 0; while (j > 0 && ws(F[j])) j--; long ne = j; while (j > 0 && F[j] >= '0' && F[j] <= '9') j--; if (j == ne) return 0; if (j > 0 && !ws(F[j]) && F[j] != '>' && F[j] != ']' && F[j] != ')') return 0; if (!(F[j] >= '0' && F[j] <= '9')) j++; int nm = atoi((char *)F + j); if (nm <= 0 || nm > 4000000) return 0; *start = j; return nm; }
static void index_objs(void) {
    NOB = 1; long st; for (long i = 0; i + 6 < FN; i++) if (F[i] == 'o' && F[i + 1] == 'b' && F[i + 2] == 'j') { int nm = objhdr(i, &st); if (nm && nm + 1 > NOB) NOB = nm + 1; }
    OT = calloc(NOB + 1, sizeof *OT);
    for (long i = 0; i + 6 < FN; i++) { if (F[i] == 's' && i > 0 && (ws(F[i - 1]) || F[i - 1] == '>') && i + 6 < FN && !memcmp(F + i, "stream", 6) && (F[i + 6] == '\r' || F[i + 6] == '\n')) { long k = i + 6; while (k + 9 < FN && memcmp(F + k, "endstream", 9)) k++; i = k + 8; continue; }
        if (F[i] == 'o' && F[i + 1] == 'b' && F[i + 2] == 'j') { int nm = objhdr(i, &st); if (nm && nm < NOB) { OT[nm].base = F; OT[nm].off = st; OT[nm].len = FN; OT[nm].cache = 0; } } }
    /* flujos de objetos (PDF 1.5) */
    for (int i = 1; i < NOB; i++) if (OT[i].base == F) { po *o = getobj(i); if (!o || !isname(dget(o, "Type"), "ObjStm")) continue; long n; u8 *s = stream(i, &n); if (!s) continue; int cnt = (int)num(dget(o, "N"), 0), first = (int)num(dget(o, "First"), 0); lx_t l = {s, s + n};
        for (int k = 0; k < cnt; k++) { po *a = parse(&l, 0), *b = parse(&l, 0); int nm = (int)a->n; long off = (long)b->n; if (nm > 0 && nm < NOB && OT[nm].base != F && first + off < n) { OT[nm].base = s; OT[nm].off = first + off; OT[nm].len = n; OT[nm].cache = 0; } } }
}
/* ---------- paginas ---------- */
static int PG[3000], NPG;
static void collect(po *n, int depth) { if (!n || depth > 30 || NPG >= 3000) return; po *k = dget(n, "Kids"); if (k && k->t == PN_ARR) { for (int i = 0; i < k->cnt; i++) { po *c = k->a[i]; if (c->t == PN_REF) { po *cc = getobj(c->ref); if (cc && dget(cc, "Kids")) collect(cc, depth + 1); else if (cc) PG[NPG++] = c->ref; } } } }
static po *page_attr(po *pg, const char *k) { for (int d = 0; pg && d < 30; d++) { po *v = dget(pg, k); if (v) return v; pg = dget(pg, "Parent"); } return 0; }
/* ---------- fuentes ---------- */
typedef struct { po *obj; int two, map[256], fn, first; u32 *cmap; int ncm; double w[256]; double dw; } font_t;
static const char *GN[][2] = {{"space", " "}, {"exclam", "!"}, {"quotedbl", "\""}, {"numbersign", "#"}, {"dollar", "$"}, {"percent", "%"}, {"ampersand", "&"}, {"quotesingle", "'"}, {"quoteright", "’"}, {"quoteleft", "‘"}, {"parenleft", "("}, {"parenright", ")"}, {"asterisk", "*"}, {"plus", "+"}, {"comma", ","}, {"hyphen", "-"}, {"minus", "-"}, {"period", "."}, {"slash", "/"}, {"zero", "0"}, {"one", "1"}, {"two", "2"}, {"three", "3"}, {"four", "4"}, {"five", "5"}, {"six", "6"}, {"seven", "7"}, {"eight", "8"}, {"nine", "9"}, {"colon", ":"}, {"semicolon", ";"}, {"less", "<"}, {"equal", "="}, {"greater", ">"}, {"question", "?"}, {"at", "@"}, {"bracketleft", "["}, {"backslash", "\\"}, {"bracketright", "]"}, {"underscore", "_"}, {"quotedblleft", "“"}, {"quotedblright", "”"}, {"endash", "–"}, {"emdash", "—"}, {"bullet", "•"}, {"ellipsis", "…"}, {"Euro", "€"}, {"questiondown", "¿"}, {"exclamdown", "¡"}, {"guillemotleft", "«"}, {"guillemotright", "»"}, {"degree", "°"}, {"ordfeminine", "ª"}, {"ordmasculine", "º"}, {"copyright", "©"}, {"registered", "®"}, {"fi", "fi"}, {"fl", "fl"}, {"germandbls", "ß"}, {"periodcentered", "·"}, {"section", "§"}};
static int glyph_uni(const char *n) {
    if (!n[1]) return (u8)n[0]; if (!strncmp(n, "uni", 3) && strlen(n) >= 7) { int v = 0; for (int i = 3; i < 7; i++) v = v * 16 + hexv(n[i]); return v; }
    for (unsigned i = 0; i < sizeof GN / sizeof GN[0]; i++) if (!strcmp(n, GN[i][0])) { const char *s = GN[i][1]; return utf8_next(&s); }
    static const char *acc[] = {"grave", "acute", "circumflex", "tilde", "dieresis", "ring", "cedilla"}; static const char *base = "AEIOUNYC aeiouny c";
    for (int a = 0; a < 7; a++) { int l = strlen(acc[a]), nl = strlen(n); if (nl == l + 1 && !strcmp(n + 1, acc[a])) { int c = n[0];
        static const u16 tab[7][2][6] = {{{0xC0, 0xC8, 0xCC, 0xD2, 0xD9, 0}, {0xE0, 0xE8, 0xEC, 0xF2, 0xF9, 0}}, {{0xC1, 0xC9, 0xCD, 0xD3, 0xDA, 0xDD}, {0xE1, 0xE9, 0xED, 0xF3, 0xFA, 0xFD}}, {{0xC2, 0xCA, 0xCE, 0xD4, 0xDB, 0}, {0xE2, 0xEA, 0xEE, 0xF4, 0xFB, 0}}, {{0xC3, 0, 0, 0xD5, 0, 0xD1}, {0xE3, 0, 0, 0xF5, 0, 0xF1}}, {{0xC4, 0xCB, 0xCF, 0xD6, 0xDC, 0x178}, {0xE4, 0xEB, 0xEF, 0xF6, 0xFC, 0xFF}}, {{0xC5, 0, 0, 0, 0, 0}, {0xE5, 0, 0, 0, 0, 0}}, {{0xC7, 0, 0, 0, 0, 0}, {0xE7, 0, 0, 0, 0, 0}}};
        int lo = c >= 'a', u = tolower(c), idx = u == 'a' ? 0 : u == 'e' ? 1 : u == 'i' ? 2 : u == 'o' ? 3 : u == 'u' ? 4 : (u == 'n' && a == 3) || (u == 'y' && a != 3) ? 5 : (u == 'c' && a == 6) ? 0 : -1; (void)base; if (idx >= 0 && tab[a][lo][idx]) return tab[a][lo][idx]; } }
    return '?';
}
static const u16 WIN80[32] = {0x20AC, 0, 0x201A, 0x192, 0x201E, 0x2026, 0x2020, 0x2021, 0x2C6, 0x2030, 0x160, 0x2039, 0x152, 0, 0x17D, 0, 0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x2DC, 0x2122, 0x161, 0x203A, 0x153, 0, 0x17E, 0x178};
static const u16 MACR[128] = {0xC4,0xC5,0xC7,0xC9,0xD1,0xD6,0xDC,0xE1,0xE0,0xE2,0xE4,0xE3,0xE5,0xE7,0xE9,0xE8,0xEA,0xEB,0xED,0xEC,0xEE,0xEF,0xF1,0xF3,0xF2,0xF4,0xF6,0xF5,0xFA,0xF9,0xFB,0xFC,0x2020,0xB0,0xA2,0xA3,0xA7,0x2022,0xB6,0xDF,0xAE,0xA9,0x2122,0xB4,0xA8,0x2260,0xC6,0xD8,0x221E,0xB1,0x2264,0x2265,0xA5,0xB5,0x2202,0x2211,0x220F,0x3C0,0x222B,0xAA,0xBA,0x3A9,0xE6,0xF8,0xBF,0xA1,0xAC,0x221A,0x192,0x2248,0x2206,0xAB,0xBB,0x2026,0xA0,0xC0,0xC3,0xD5,0x152,0x153,0x2013,0x2014,0x201C,0x201D,0x2018,0x2019,0xF7,0x25CA,0xFF,0x178,0x2044,0x20AC,0x2039,0x203A,0xFB01,0xFB02,0x2021,0xB7,0x201A,0x201E,0x2030,0xC2,0xCA,0xC1,0xCB,0xC8,0xCD,0xCE,0xCF,0xCC,0xD3,0xD4,0xF8FF,0xD2,0xDA,0xDB,0xD9,0x131,0x2C6,0x2DC,0xAF,0x2D8,0x2D9,0x2DA,0xB8,0x2DD,0x2DB,0x2C7};
static u32 hexs(const u8 *s, int n) { u32 v = 0; for (int i = 0; i < n; i++) v = v << 8 | s[i]; return v; }
static void load_cmap(font_t *f, po *tu) { long n; u8 *s = stream(tu->ref, &n); if (!s) return; lx_t l = {s, s + n}; int cap = 256; f->cmap = malloc(cap * 8); f->ncm = 0; po *prev[3] = {0};
    for (;;) { skip(&l); if (l.p >= l.e) break; po *o = parse(&l, 0); if (o->t == PN_NULL && o->s) {
            if (!strcmp(o->s, "beginbfchar") || !strcmp(o->s, "beginbfrange")) { int range = o->s[7] == 'r';
                for (;;) { skip(&l); if (l.p >= l.e || (*l.p == 'e')) break; po *a = parse(&l, 0), *b = parse(&l, 0), *c = range ? parse(&l, 0) : 0; if (a->t != PN_STR) continue; if (a->sl == 2) f->two = 1;
                    u32 lo = hexs((u8 *)a->s, a->sl), hi = range ? hexs((u8 *)b->s, b->sl) : lo; po *dst = range ? c : b;
                    for (u32 k = lo; k <= hi && k - lo < 65536; k++) { u32 uni = 0; if (dst->t == PN_STR) { const u8 *q = (u8 *)dst->s; uni = dst->sl >= 2 ? (q[0] << 8 | q[1]) + (k - lo) : q[0]; } else if (dst->t == PN_ARR && k - lo < (u32)dst->cnt && dst->a[k - lo]->t == PN_STR) { po *x = dst->a[k - lo]; uni = x->sl >= 2 ? ((u8)x->s[0] << 8 | (u8)x->s[1]) : (u8)x->s[0]; }
                        if (f->ncm == cap) { cap *= 2; f->cmap = realloc(f->cmap, cap * 8); } f->cmap[f->ncm * 2] = k; f->cmap[f->ncm * 2 + 1] = uni; f->ncm++; } } }
            else if (!strcmp(o->s, "begincodespacerange")) { skip(&l); po *a = parse(&l, 0); if (a->t == PN_STR && a->sl == 2) f->two = 1; } }
        prev[0] = prev[1]; prev[1] = prev[2]; prev[2] = o; }
    free(s); }
static int fstyle(po *fo) { po *b = dget(fo, "BaseFont"); const char *n = b && b->t == PN_NAME ? b->s : ""; int bold = strstr(n, "Bold") || strstr(n, "Black") || strstr(n, "Heavy"); if (strstr(n, "Courier") || strstr(n, "Mono")) return bold ? ZPF_MONOB : ZPF_MONO; if (strstr(n, "Times") || strstr(n, "Serif") || strstr(n, "Roman") || strstr(n, "Georgia") || strstr(n, "Garamond")) return bold ? ZPF_SERIFB : ZPF_SERIF; return bold ? ZPF_SANSB : ZPF_SANS; }
static font_t *font_get(po *fo) {
    static font_t *cache[64]; static int nc; for (int i = 0; i < nc; i++) if (cache[i]->obj == fo) return cache[i];
    font_t *f = calloc(1, sizeof *f); f->obj = fo; f->fn = fstyle(fo); f->dw = 500; if (isname(dget(fo, "Subtype"), "Type0")) f->two = 1;
    for (int i = 0; i < 256; i++) f->map[i] = i >= 0x80 && i < 0xA0 ? (WIN80[i - 0x80] ? WIN80[i - 0x80] : '?') : i;
    po *en = dget(fo, "Encoding"); if (isname(en, "MacRomanEncoding") || (en && en->t == PN_DICT && isname(dget(en, "BaseEncoding"), "MacRomanEncoding"))) for (int i = 0x80; i < 256; i++) f->map[i] = MACR[i - 0x80];
    if (en && en->t == PN_DICT) { po *df = dget(en, "Differences"); if (df && df->t == PN_ARR) { int code = 0; for (int i = 0; i < df->cnt; i++) { po *x = res(df->a[i]); if (x->t == PN_NUM) code = (int)x->n; else if (x->t == PN_NAME && code < 256) f->map[code++] = glyph_uni(x->s); } } }
    po *tu = 0; for (int i = 0; fo->t == PN_DICT && i < fo->cnt; i++) if (!strcmp(fo->k[i], "ToUnicode") && fo->a[i]->t == PN_REF) tu = fo->a[i]; if (tu) load_cmap(f, tu);
    po *wd = dget(fo, "Widths"); f->first = (int)num(dget(fo, "FirstChar"), 0); for (int i = 0; i < 256; i++) f->w[i] = 0; if (wd && wd->t == PN_ARR) for (int i = 0; i < wd->cnt && f->first + i < 256; i++) f->w[f->first + i] = num(wd->a[i], 0);
    if (f->two) { po *df = dget(fo, "DescendantFonts"); if (df && df->t == PN_ARR && df->cnt) { po *cf = res(df->a[0]); f->dw = num(dget(cf, "DW"), 1000); f->fn = fstyle(cf); } }
    if (nc < 64) cache[nc++] = f; return f; }
static int cmap_get(font_t *f, u32 code) { int lo = 0, hi = f->ncm - 1; for (int i = 0; i < f->ncm; i++) if (f->cmap[i * 2] == code) return f->cmap[i * 2 + 1]; (void)lo; (void)hi; return -1; }
/* ---------- interprete de contenido ---------- */
typedef struct { double m[6]; u32 fill; } gs_t;
static void mul(double *r, const double *a, const double *b) { double t[6] = {a[0] * b[0] + a[1] * b[2], a[0] * b[1] + a[1] * b[3], a[2] * b[0] + a[3] * b[2], a[2] * b[1] + a[3] * b[3], a[4] * b[0] + a[5] * b[2] + b[4], a[4] * b[1] + a[5] * b[3] + b[5]}; memcpy(r, t, sizeof t); }
static zpdoc *D; static double PH_, PX0, PY0;
static void run(const u8 *s, long n, po *resrc, double *ctm0, int depth);
static void show(po *str, font_t *f, double sz, double *tm, double *ctm, gs_t *g, double tc, double tw, double th) {
    char out[2048]; int k = 0; double x0 = 0, adv = 0; double m[6]; mul(m, tm, ctm); double ex = m[4], ey = m[5], fs = sz * z_sqrt(m[2] * m[2] + m[3] * m[3]); (void)x0;
    for (int i = 0; i < str->sl; ) { u32 code; if (f && f->two) { code = (u8)str->s[i] << 8 | (u8)(i + 1 < str->sl ? str->s[i + 1] : 0); i += 2; } else code = (u8)str->s[i++];
        int u = f && f->ncm ? cmap_get(f, code) : -1; if (u < 0) u = f && !f->two ? f->map[code & 255] : '?'; if (u >= 32 && k < 2040) k += utf8_put(u, out + k);
        double w = f ? (f->two ? f->dw : (f->w[code & 255] ? f->w[code & 255] : 500)) : 500; adv += (w / 1000 * sz + tc + (code == 32 && !(f && f->two) ? tw : 0)) * th; }
    out[k] = 0; if (k && fs > 0.5) zp_text(D, ex - PX0, PH_ - (ey - PY0), f ? f->fn : ZPF_SANS, MIN(fs, 200.0), g->fill, out);
    double t[6] = {1, 0, 0, 1, adv, 0}; mul(tm, t, tm); }
static void image(po *xo, int num_, gs_t *g) {
    long n; u8 *s = stream(num_, &n); if (!s) return; int iw = (int)num(dget(xo, "Width"), 0), ih = (int)num(dget(xo, "Height"), 0); u32 *px = 0; int w = 0, h = 0; po *f = dget(xo, "Filter"); int dct = isname(f, "DCTDecode") || (f && f->t == PN_ARR && f->cnt && isname(f->a[f->cnt - 1], "DCTDecode"));
    if (dct) { if (z_jpeg_decode(s, n, &px, &w, &h)) px = 0; }
    else if (iw > 0 && ih > 0 && (int)num(dget(xo, "BitsPerComponent"), 8) == 8) { po *cs = dget(xo, "ColorSpace"); int nc = isname(cs, "DeviceGray") ? 1 : isname(cs, "DeviceCMYK") ? 4 : 3; if (n >= (long)iw * ih * nc) { px = malloc((long)iw * ih * 4); for (long i = 0; i < (long)iw * ih; i++) { u8 *q = s + i * nc; px[i] = nc == 1 ? q[0] * 0x010101u : nc == 3 ? (u32)q[0] << 16 | q[1] << 8 | q[2] : (u32)((255 - q[0]) * (255 - q[3]) / 255) << 16 | ((255 - q[1]) * (255 - q[3]) / 255) << 8 | ((255 - q[2]) * (255 - q[3]) / 255); } w = iw; h = ih; } }
    free(s); if (!px) return; double *m = g->m; double x = m[4], y = m[5], ww = z_fabs(m[0]), hh = z_fabs(m[3]); if (ww > 1 && hh > 1) zp_image(D, x - PX0, PH_ - (y - PY0) - hh, ww, hh, px, w, h); free(px); }
static u32 rgb(double r, double g, double b) { int R = MAX(0, MIN(255, (int)(r * 255))), G = MAX(0, MIN(255, (int)(g * 255))), B = MAX(0, MIN(255, (int)(b * 255))); return R << 16 | G << 8 | B; }
static void run(const u8 *s, long n, po *resrc, double *ctm0, int depth) {
    if (depth > 4) return; lx_t l = {s, s + n}; po *st[16]; int ns = 0; gs_t gst[32]; int gp = 0; gs_t g; memcpy(g.m, ctm0, sizeof g.m); g.fill = 0;
    double tm[6] = {1, 0, 0, 1, 0, 0}, tlm[6] = {1, 0, 0, 1, 0, 0}, sz = 12, tl = 0, tc = 0, tw = 0, th = 1; font_t *f = 0; double rx = 0, ry = 0, rw = 0, rh = 0; int haver = 0;
    po *fonts = dget(resrc, "Font"), *xobj = dget(resrc, "XObject");
    while (l.p < l.e) { skip(&l); if (l.p >= l.e) break; po *o = parse(&l, 0);
        if (o->t != PN_NULL || !o->s) { if (ns < 16) st[ns++] = o; continue; } const char *op = o->s;
        #define A(i) (ns > (i) ? num(st[i], 0) : 0)
        if (!strcmp(op, "BI")) { while (l.p + 2 < l.e && !(l.p[0] == 'E' && l.p[1] == 'I' && delim(l.p[2]) && ws(l.p[-1]))) l.p++; l.p += 2; }
        else if (!strcmp(op, "q")) { if (gp < 32) gst[gp++] = g; } else if (!strcmp(op, "Q")) { if (gp) g = gst[--gp]; }
        else if (!strcmp(op, "cm") && ns >= 6) { double m[6] = {A(0), A(1), A(2), A(3), A(4), A(5)}; mul(g.m, m, g.m); }
        else if (!strcmp(op, "BT")) { double id[6] = {1, 0, 0, 1, 0, 0}; memcpy(tm, id, sizeof tm); memcpy(tlm, id, sizeof tlm); }
        else if (!strcmp(op, "Tf") && ns >= 2) { sz = A(1); po *fo = fonts && st[0]->t == PN_NAME ? dget(fonts, st[0]->s) : 0; f = fo ? font_get(fo) : 0; }
        else if (!strcmp(op, "Td") || !strcmp(op, "TD")) { double t[6] = {1, 0, 0, 1, A(0), A(1)}; mul(tlm, t, tlm); memcpy(tm, tlm, sizeof tm); if (op[1] == 'D') tl = -A(1); }
        else if (!strcmp(op, "Tm") && ns >= 6) { double m[6] = {A(0), A(1), A(2), A(3), A(4), A(5)}; memcpy(tm, m, sizeof tm); memcpy(tlm, m, sizeof tlm); }
        else if (!strcmp(op, "T*")) { double t[6] = {1, 0, 0, 1, 0, -tl}; mul(tlm, t, tlm); memcpy(tm, tlm, sizeof tm); }
        else if (!strcmp(op, "TL")) tl = A(0); else if (!strcmp(op, "Tc")) tc = A(0); else if (!strcmp(op, "Tw")) tw = A(0); else if (!strcmp(op, "Tz")) th = A(0) / 100;
        else if (!strcmp(op, "Tj") && ns) { if (st[0]->t == PN_STR) show(st[0], f, sz, tm, g.m, &g, tc, tw, th); }
        else if ((!strcmp(op, "'") || !strcmp(op, "\"")) && ns) { double t[6] = {1, 0, 0, 1, 0, -tl}; mul(tlm, t, tlm); memcpy(tm, tlm, sizeof tm); if (st[ns - 1]->t == PN_STR) show(st[ns - 1], f, sz, tm, g.m, &g, tc, tw, th); }
        else if (!strcmp(op, "TJ") && ns && st[0]->t == PN_ARR) { po *a = st[0]; for (int i = 0; i < a->cnt; i++) { if (a->a[i]->t == PN_STR) show(a->a[i], f, sz, tm, g.m, &g, tc, tw, th); else if (a->a[i]->t == PN_NUM) { double t[6] = {1, 0, 0, 1, -a->a[i]->n / 1000 * sz * th, 0}; mul(tm, t, tm); } } }
        else if (!strcmp(op, "rg") && ns >= 3) g.fill = rgb(A(0), A(1), A(2)); else if (!strcmp(op, "g") && ns >= 1) g.fill = rgb(A(0), A(0), A(0));
        else if (!strcmp(op, "k") && ns >= 4) g.fill = rgb((1 - A(0)) * (1 - A(3)), (1 - A(1)) * (1 - A(3)), (1 - A(2)) * (1 - A(3)));
        else if ((!strcmp(op, "sc") || !strcmp(op, "scn")) && ns >= 3 && st[0]->t == PN_NUM) g.fill = rgb(A(0), A(1), A(2)); else if ((!strcmp(op, "sc") || !strcmp(op, "scn")) && ns == 1) g.fill = rgb(A(0), A(0), A(0));
        else if (!strcmp(op, "re") && ns >= 4) { rx = A(0); ry = A(1); rw = A(2); rh = A(3); haver = 1; }
        else if ((!strcmp(op, "f") || !strcmp(op, "F") || !strcmp(op, "f*") || !strcmp(op, "B")) && haver) { double *m = g.m; double x0 = rx * m[0] + ry * m[2] + m[4], y0 = rx * m[1] + ry * m[3] + m[5], w = rw * m[0], h = rh * m[3]; if (w < 0) { x0 += w; w = -w; } if (h < 0) { y0 += h; h = -h; } if (g.fill != 0xFFFFFF && w * h < 4e5) zp_rect(D, x0 - PX0, PH_ - (y0 - PY0) - h, w, h, g.fill, ZP_NONE, 0); haver = 0; }
        else if (!strcmp(op, "n") || !strcmp(op, "S") || !strcmp(op, "s") || !strcmp(op, "W") || !strcmp(op, "h")) { if (op[0] != 'W' && op[0] != 'h') haver = 0; }
        else if (!strcmp(op, "Do") && ns && st[0]->t == PN_NAME && xobj) { po *ref = 0; po *xd = res(xobj); for (int i = 0; xd && xd->t == PN_DICT && i < xd->cnt; i++) if (!strcmp(xd->k[i], st[0]->s)) ref = xd->a[i]; if (ref && ref->t == PN_REF) { po *xo = getobj(ref->ref);
                if (isname(dget(xo, "Subtype"), "Image")) image(xo, ref->ref, &g);
                else if (isname(dget(xo, "Subtype"), "Form")) { long fl; u8 *fs = stream(ref->ref, &fl); if (fs) { double m[6] = {1, 0, 0, 1, 0, 0}; po *mt = dget(xo, "Matrix"); if (mt && mt->t == PN_ARR && mt->cnt == 6) for (int i = 0; i < 6; i++) m[i] = num(mt->a[i], 0); double c2[6]; mul(c2, m, g.m); po *r2 = dget(xo, "Resources"); run(fs, fl, r2 ? r2 : resrc, c2, depth + 1); free(fs); } } } }
        #undef A
        ns = 0; }
}
static zpdoc *build_page(int i, const zpset *s0) {
    zpset s = {0}; po *pg = getobj(PG[i]); po *mb = page_attr(pg, "MediaBox"); double x0 = 0, y0 = 0, x1 = 612, y1 = 792; if (mb && mb->t == PN_ARR && mb->cnt == 4) { x0 = num(mb->a[0], 0); y0 = num(mb->a[1], 0); x1 = num(mb->a[2], 612); y1 = num(mb->a[3], 792); }
    s.pw = x1 - x0; s.ph = y1 - y0; zpdoc *d = zp_new(&s); zp_page(d); D = d; PH_ = s.ph; PX0 = x0; PY0 = y0; (void)s0;
    po *rs = page_attr(pg, "Resources"); po *ct = 0; for (int k = 0; pg && pg->t == PN_DICT && k < pg->cnt; k++) if (!strcmp(pg->k[k], "Contents")) ct = pg->a[k];
    double id[6] = {1, 0, 0, 1, 0, 0}; int refs[256], nr = 0; if (ct && ct->t == PN_REF) { po *c2 = getobj(ct->ref); if (c2 && c2->t == PN_ARR) { for (int k = 0; k < c2->cnt && nr < 256; k++) if (c2->a[k]->t == PN_REF) refs[nr++] = c2->a[k]->ref; } else refs[nr++] = ct->ref; } else if (ct && ct->t == PN_ARR) for (int k = 0; k < ct->cnt && nr < 256; k++) if (ct->a[k]->t == PN_REF) refs[nr++] = ct->a[k]->ref;
    long tot = 0; u8 *all = 0; for (int k = 0; k < nr; k++) { long n; u8 *b = stream(refs[k], &n); if (!b) continue; all = realloc(all, tot + n + 2); memcpy(all + tot, b, n); tot += n; all[tot++] = '\n'; free(b); }
    if (all) { run(all, tot, rs, id, 0); free(all); } return d;
}
/* ---------- interfaz ---------- */
static int cur, zoom = 100, scy; static zpdoc *CD; static u32 *IMG; static int iw_, ih_, fit = 1;
#define TB 36
static const char *BT[] = {"Abrir", "<", ">", "-", "+", "Ajustar", "Imprimir", "Guardar texto"};
static int bx(int i) { int x = 6; for (int k = 0; k < i; k++) x += g_textw(_(BT[k])) + 18; return x; }
static void render(void) { free(IMG); IMG = 0; if (!NPG) return; zp_free(CD); CD = build_page(cur, 0); double pw = zp_pw(CD); int dpi = fit ? (int)((W->w - 30) * 72 / pw) : 96 * zoom / 100; dpi = MAX(20, MIN(dpi, 300)); IMG = zp_raster(CD, 0, 1, dpi, &iw_, &ih_); }
static void draw(void) { g_clear(W, 0x525659); g_rect(W, 0, 0, W->w, TB, UI_PANEL); for (int i = 0; i < 8; i++) ui_button(W, bx(i), 5, g_textw(_(BT[i])) + 14, 26, BT[i], i == 5 && fit ? 2 : 0);
    char t[120]; snprintf(t, 120, "%s %d / %d", _("Página"), NPG ? cur + 1 : 0, NPG); g_text(W, bx(8) + 4, 10, t, UI_TEXT);
    if (IMG) { int x = MAX(10, (W->w - iw_) / 2), y = TB + 10 - scy; zw_clip(W, 0, TB, W->w, W->h - TB - 22); g_rect(W, x + 4, y + 4, iw_, ih_, 0x2A2C2E); g_image(W, x, y, iw_, ih_, IMG); zw_noclip(W); }
    else g_text(W, 20, TB + 20, NPG ? _("Esta página no tiene contenido legible.") : _("Abre un archivo PDF con el botón Abrir."), 0xFFFFFF);
    g_rect(W, 0, W->h - 22, W->w, 22, UI_PANEL); g_text(W, 8, W->h - 19, msg, UI_TEXT); zw_present(W); }
static void pbuild(zpdoc *d, const zpset *s, void *c) { /* cada pagina del PDF, escalada al papel */
    for (int i = 0; i < NPG; i++) { zpdoc *pd = build_page(i, 0); int w, h; u32 *px = zp_raster(pd, s, 1, 150, &w, &h); zp_page(d); if (px) { double k = MIN(zp_pw(d) / w, zp_ph(d) / h); zp_image(d, (zp_pw(d) - w * k) / 2, (zp_ph(d) - h * k) / 2, w * k, h * k, px, w, h); free(px); } zp_free(pd); } }
static void save_text(void) { char p[320]; strcpy(p, path); char *dot = strrchr(p, '.'); if (dot) *dot = 0; strcat(p, ".txt"); sb_t b = {0}; sb_s(&b, ""); zpdoc *keep = CD; for (int i = 0; i < NPG; i++) { zpdoc *pd = build_page(i, 0); extern int zp_text_dump(zpdoc *, sb_t *); zp_text_dump(pd, &b); sb_s(&b, "\n\f\n"); zp_free(pd); } CD = keep; int e = z_writefile(p, b.p, b.n); free(b.p); snprintf(msg, 200, "%s %s", e ? z_errstr(e) : _("Texto guardado en"), e ? "" : p); }
static int open_(const char *p) { long n; u8 *d = z_readfile(p, &n); if (!d) { snprintf(msg, 200, "%s %s", _("No se puede abrir"), p); return -1; } if (n < 8 || memcmp(d, "%PDF", 4)) { free(d); strcpy(msg, _("No es un archivo PDF")); return -1; }
    F = d; FN = n; strncpy0(path, p, 300); index_objs(); NPG = 0; po *cat = 0; for (int i = 1; i < NOB && !cat; i++) if (OT[i].base) { po *o = getobj(i); if (o && isname(dget(o, "Type"), "Catalog")) cat = o; }
    po *pages = cat ? dget(cat, "Pages") : 0; if (pages) collect(pages, 0); if (!NPG) for (int i = 1; i < NOB && NPG < 3000; i++) if (OT[i].base) { po *o = getobj(i); if (o && isname(dget(o, "Type"), "Page")) PG[NPG++] = i; }
    cur = 0; scy = 0; render(); const char *b = strrchr(p, '/'); zw_title(W->id, b ? b + 1 : p); snprintf(msg, 200, "%d %s, %d %s", NPG, _("páginas"), NOB - 1, _("objetos")); char m[400]; snprintf(m, 400, "[prueba] pdf abierto %s: %d paginas", p, NPG); z_log(m); return 0; }
int main(const char *a) { int wp; a = z_print_arg(a, &wp);
    zsys sy; z_sysinfo(&sy); W = zw_new("Lector PDF", -1, 0, MIN(860, (int)sy.scr_w - 40), MIN(720, (int)sy.scr_h - 80), WF_DECOR | WF_RESIZE); if (a[0]) open_(a); draw(); if (wp && NPG) { zp_dialog(strrchr(path, '/') ? strrchr(path, '/') + 1 : path, pbuild, 0); draw(); } zevent e;
    for (;;) { if (!z_event(&e, -1)) continue; if (e.type == EV_REDRAW) { draw(); continue; } if (e.win != (u32)W->id) continue; if (e.type == EV_CLOSE) return 0; if (e.type == EV_RESIZE) { zw_resized(W, e.x, e.y); render(); draw(); continue; }
        int b = -1; if (e.type == EV_DOWN && e.y < TB) { for (int i = 7; i >= 0; i--) if (e.x >= bx(i)) { if (e.x < bx(i) + g_textw(_(BT[i])) + 14) b = i; break; } }
        if (e.type == EV_KEY) { if (e.key == K_PGDN || e.key == K_RIGHT) b = 2; else if (e.key == K_PGUP || e.key == K_LEFT) b = 1; else if (e.key == K_DOWN) scy += 40; else if (e.key == K_UP) scy = MAX(0, scy - 40); else if (e.ch == 16) b = 6; else if (e.ch == '+') b = 4; else if (e.ch == '-') b = 3; }
        if (e.type == EV_WHEEL) { scy = MAX(0, MIN(scy - (int)e.key * 60, MAX(0, ih_ - (W->h - TB - 40)))); }
        if (b == 0) { char p[300]; strcpy(p, path[0] ? path : "/sys/DOCS/"); if (ui_prompt("Abrir", "Ruta del archivo PDF:", p, 290)) open_(p); }
        else if (b == 1 && cur > 0) { cur--; scy = 0; render(); } else if (b == 2 && cur + 1 < NPG) { cur++; scy = 0; render(); }
        else if (b == 3) { fit = 0; zoom = MAX(25, zoom * 4 / 5); render(); } else if (b == 4) { fit = 0; zoom = MIN(400, zoom * 5 / 4); render(); } else if (b == 5) { fit = 1; render(); }
        else if (b == 6 && NPG) zp_dialog(strrchr(path, '/') ? strrchr(path, '/') + 1 : path, pbuild, 0); else if (b == 7 && NPG) save_text();
        draw(); }
}
