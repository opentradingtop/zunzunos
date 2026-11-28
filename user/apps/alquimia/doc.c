/* Alquimia: documentos. Modelo comun: una lista de parrafos (titulos de nivel 1 a 6, parrafo, vineta, numerado,
   cita y codigo). Lee TXT, Markdown, HTML, RTF, DOCX, ODT y EPUB; escribe TXT, Markdown, HTML, RTF, DOCX, ODT,
   EPUB y PDF. Se conservan la estructura y el texto, no los tipos de letra ni las imagenes. */
enum { PK_P, PK_H1, PK_H2, PK_H3, PK_H4, PK_H5, PK_H6, PK_LI, PK_NUM, PK_QUOTE, PK_CODE };
typedef struct { int k; char *t; } dpar;
typedef struct { dpar *p; int n, cap; char title[200]; } zdoc;
static void doc_add(zdoc *d, int k, const char *t, long n) {
    while (n > 0 && (t[0] == ' ' || t[0] == '\t' || t[0] == '\n' || t[0] == '\r') && k != PK_CODE) { t++; n--; } while (n > 0 && (t[n - 1] == ' ' || t[n - 1] == '\n' || t[n - 1] == '\r' || t[n - 1] == '\t')) n--;
    if (n <= 0 && k != PK_CODE) return; if (d->n == d->cap) { d->cap = d->cap * 2 + 64; d->p = realloc(d->p, d->cap * sizeof *d->p); } char *s = malloc(n + 1); memcpy(s, t, n); s[n] = 0; d->p[d->n].k = k; d->p[d->n++].t = s;
    if (!d->title[0] && k >= PK_H1 && k <= PK_H2) strncpy0(d->title, s, 200); }
static void doc_free(zdoc *d) { for (int i = 0; i < d->n; i++) free(d->p[i].t); free(d->p); memset(d, 0, sizeof *d); }
/* espacios seguidos a uno (no en codigo) */
static void sp_add(sb_t *b, const char *s, long n, int *sp) { for (long i = 0; i < n; i++) { char c = s[i]; if (c == '\r') continue; if (c == ' ' || c == '\n' || c == '\t') { if (!*sp) { sb_add(b, " ", 1); *sp = 1; } } else { sb_add(b, &c, 1); *sp = 0; } } }
/* ---------- entidades XML/HTML ---------- */
static const struct { const char *n; u16 c; } ENT[] = {{"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", 0xA0}, {"aacute", 0xE1}, {"eacute", 0xE9}, {"iacute", 0xED}, {"oacute", 0xF3}, {"uacute", 0xFA}, {"Aacute", 0xC1}, {"Eacute", 0xC9}, {"Iacute", 0xCD}, {"Oacute", 0xD3}, {"Uacute", 0xDA}, {"ntilde", 0xF1}, {"Ntilde", 0xD1}, {"uuml", 0xFC}, {"Uuml", 0xDC}, {"ouml", 0xF6}, {"Ouml", 0xD6}, {"auml", 0xE4}, {"Auml", 0xC4}, {"szlig", 0xDF}, {"ccedil", 0xE7}, {"Ccedil", 0xC7}, {"agrave", 0xE0}, {"egrave", 0xE8}, {"ograve", 0xF2}, {"acirc", 0xE2}, {"ecirc", 0xEA}, {"ocirc", 0xF4}, {"iquest", 0xBF}, {"iexcl", 0xA1}, {"laquo", 0xAB}, {"raquo", 0xBB}, {"copy", 0xA9}, {"reg", 0xAE}, {"deg", 0xB0}, {"euro", 0x20AC}, {"hellip", 0x2026}, {"mdash", 0x2014}, {"ndash", 0x2013}, {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"bull", 0x2022}, {"middot", 0xB7}, {"ordf", 0xAA}, {"ordm", 0xBA}, {"times", 0xD7}};
/* decodifica una entidad que empieza en s (tras '&'); devuelve los bytes consumidos o 0 */
static int ent_dec(const char *s, sb_t *b) { if (s[0] == '#') { u32 v = 0; int i = 1, hex = s[1] == 'x' || s[1] == 'X'; if (hex) i++; int st = i; while (s[i] && s[i] != ';' && i < 10) { int c = tolower(s[i]); if (isdigit(c)) v = v * (hex ? 16 : 10) + c - '0'; else if (hex && c >= 'a' && c <= 'f') v = v * 16 + c - 'a' + 10; else return 0; i++; } if (s[i] != ';' || i == st) return 0; char o[5]; sb_add(b, o, utf8_put(v ? v : 0xFFFD, o)); return i + 1; }
    for (unsigned k = 0; k < sizeof ENT / sizeof *ENT; k++) { int l = strlen(ENT[k].n); if (!strncmp(s, ENT[k].n, l) && s[l] == ';') { char o[5]; sb_add(b, o, utf8_put(ENT[k].c, o)); return l + 1; } } return 0; }
static void xml_text_add(sb_t *b, const char *s, long n, int *sp, int keep) { for (long i = 0; i < n; i++) { if (s[i] == '&') { sb_t t = {0}; int k = ent_dec(s + i + 1, &t); if (k) { if (keep) sb_add(b, t.p, t.n); else sp_add(b, t.p, t.n, sp); free(t.p); i += k; continue; } free(t.p); } if (keep) { if (s[i] != '\r') sb_add(b, s + i, 1); } else sp_add(b, s + i, 1, sp); } }
/* nombre de etiqueta sin prefijo de espacio de nombres, en minusculas */
static int tag_name(const char *p, char *o, int cap, int *close) { *close = 0; p++; if (*p == '/') { *close = 1; p++; } int k = 0; while (*p && !isspace((u8)*p) && *p != '>' && *p != '/' && k < cap - 1) o[k++] = tolower(*p++); o[k] = 0; char *c = strchr(o, ':'); if (c) memmove(o, c + 1, strlen(c)); return k; }
static int attr_get(const char *tag, const char *tagend, const char *name, char *o, int cap) { int nl = strlen(name); for (const char *p = tag; p < tagend; p++) { if ((p == tag || isspace((u8)p[-1]) || p[-1] == ':') && !strncasecmp(p, name, nl) && p[nl] == '=') { p += nl + 1; char q = *p; if (q == '"' || q == '\'') p++; else q = ' '; int k = 0; while (p < tagend && *p != q && *p != '>' && k < cap - 1) o[k++] = *p++; o[k] = 0; return 1; } } o[0] = 0; return 0; }

/* ---------- HTML (y XHTML de los EPUB) ---------- */
static void html_read(const char *s, zdoc *d) {
    sb_t t = {0}; sb_s(&t, ""); int kind = PK_P, sp = 1, pre = 0, skip = 0, lists[16], nl = 0, in_title = 0; char nm[32];
    #define FLUSH() do { doc_add(d, kind, t.p, t.n); t.n = 0; t.p[0] = 0; sp = 1; } while (0)
    for (const char *p = s; *p;) {
        if (*p == '<') { if (!strncmp(p, "<!--", 4)) { const char *e = strstr(p, "-->"); p = e ? e + 3 : p + strlen(p); continue; }
            const char *e = strchr(p, '>'); if (!e) break; int cl; tag_name(p, nm, 32, &cl);
            if (!strcmp(nm, "script") || !strcmp(nm, "style")) { if (!cl) skip++; else if (skip) skip--; p = e + 1; continue; }
            if (!strcmp(nm, "title")) { in_title = !cl; p = e + 1; continue; }
            if (nm[0] == 'h' && nm[1] >= '1' && nm[1] <= '6' && !nm[2]) { FLUSH(); kind = cl ? PK_P : PK_H1 + nm[1] - '1'; }
            else if (!strcmp(nm, "p") || !strcmp(nm, "div") || !strcmp(nm, "section") || !strcmp(nm, "article") || !strcmp(nm, "tr") || !strcmp(nm, "dt") || !strcmp(nm, "dd") || !strcmp(nm, "figcaption")) { FLUSH(); if (!pre) kind = PK_P; }
            else if (!strcmp(nm, "ul") || !strcmp(nm, "ol")) { FLUSH(); if (!cl && nl < 16) lists[nl++] = nm[0] == 'o'; else if (cl && nl) nl--; kind = PK_P; }
            else if (!strcmp(nm, "li")) { FLUSH(); kind = cl ? PK_P : nl && lists[nl - 1] ? PK_NUM : PK_LI; }
            else if (!strcmp(nm, "blockquote")) { FLUSH(); kind = cl ? PK_P : PK_QUOTE; }
            else if (!strcmp(nm, "pre")) { FLUSH(); pre = !cl; kind = cl ? PK_P : PK_CODE; }
            else if (!strcmp(nm, "br")) { if (pre) sb_s(&t, "\n"); else { FLUSH(); } }
            else if (!strcmp(nm, "td") || !strcmp(nm, "th")) { if (cl) { sb_s(&t, "\t"); sp = 1; } }
            p = e + 1; continue; }
        const char *q = p; while (*q && *q != '<') q++; if (!skip && !in_title) xml_text_add(&t, p, q - p, &sp, pre); else if (in_title && !d->title[0]) { sb_t x = {0}; int s2 = 0; xml_text_add(&x, p, q - p, &s2, 0); if (x.p) strncpy0(d->title, x.p, 200); free(x.p); } p = q; }
    FLUSH(); free(t.p);
    #undef FLUSH
}
/* ---------- Markdown ---------- */
static void md_inline(sb_t *b, const char *s, long n) {   /* quita **, __, `, ~~ y deja el texto de los enlaces */
    for (long i = 0; i < n; i++) { char c = s[i];
        if ((c == '*' || c == '_' || c == '`' || c == '~') && !(c == '_' && i > 0 && i + 1 < n && isalpha((u8)s[i - 1]) && isalpha((u8)s[i + 1]))) continue;
        if (c == '!' && i + 1 < n && s[i + 1] == '[') continue;
        if (c == '[') { long j = i + 1; while (j < n && s[j] != ']') j++; if (j + 1 < n && s[j + 1] == '(') { long k = j + 2; while (k < n && s[k] != ')') k++; md_inline(b, s + i + 1, j - i - 1); i = k; continue; } }
        if (c == '\\' && i + 1 < n) { sb_add(b, s + ++i, 1); continue; } sb_add(b, &c, 1); } }
static void md_read(const char *s, zdoc *d) {
    sb_t t = {0}; sb_s(&t, ""); int kind = PK_P, code = 0;
    #define FL() do { if (t.n) doc_add(d, kind, t.p, t.n); t.n = 0; t.p[0] = 0; kind = PK_P; } while (0)
    for (const char *p = s; *p;) { const char *e = strchr(p, '\n'); long l = e ? e - p : (long)strlen(p); const char *nx = e ? e + 1 : p + l; while (l && p[l - 1] == '\r') l--;
        if (l >= 3 && (!strncmp(p, "```", 3) || !strncmp(p, "~~~", 3))) { if (code) { doc_add(d, PK_CODE, t.p, t.n); t.n = 0; t.p[0] = 0; code = 0; kind = PK_P; } else { FL(); code = 1; } p = nx; continue; }
        if (code) { sb_add(&t, p, l); sb_s(&t, "\n"); p = nx; continue; }
        long i = 0; while (i < l && i < 4 && p[i] == ' ') i++; const char *q = p + i; long ql = l - i;
        if (!ql) { FL(); p = nx; continue; }
        int h = 0; while (h < ql && h < 7 && q[h] == '#') h++;
        if (h >= 1 && h <= 6 && (h == ql || q[h] == ' ')) { FL(); long a = h; while (a < ql && q[a] == ' ') a++; long z = ql; while (z > a && (q[z - 1] == '#' || q[z - 1] == ' ')) z--; md_inline(&t, q + a, z - a); kind = PK_H1 + h - 1; FL(); p = nx; continue; }
        if (ql >= 3 && (q[0] == '-' || q[0] == '*' || q[0] == '_')) { int all = 1; for (long k = 0; k < ql; k++) if (q[k] != q[0] && q[k] != ' ') all = 0; if (all) { FL(); p = nx; continue; } }
        if (ql >= 2 && (q[0] == '-' || q[0] == '*' || q[0] == '+') && q[1] == ' ') { FL(); kind = PK_LI; md_inline(&t, q + 2, ql - 2); p = nx; continue; }
        long dg = 0; while (dg < ql && isdigit((u8)q[dg])) dg++; if (dg && dg + 1 < ql && (q[dg] == '.' || q[dg] == ')') && q[dg + 1] == ' ') { FL(); kind = PK_NUM; md_inline(&t, q + dg + 2, ql - dg - 2); p = nx; continue; }
        if (q[0] == '>') { if (kind != PK_QUOTE) { FL(); kind = PK_QUOTE; } else sb_s(&t, " "); long a = 1; if (a < ql && q[a] == ' ') a++; md_inline(&t, q + a, ql - a); p = nx; continue; }
        if (q[0] == '|') { FL(); for (long k = 0; k < ql; k++) { if (q[k] == '|') { if (k && k + 1 < ql) sb_s(&t, "\t"); } else if (q[k] != '-' || 0) sb_add(&t, q + k, 1); } int dash = 1; for (long k = 0; k < ql; k++) if (q[k] != '|' && q[k] != '-' && q[k] != ':' && q[k] != ' ') dash = 0; if (dash) { t.n = 0; t.p[0] = 0; } else FL(); p = nx; continue; }
        /* subrayado de titulo: linea de === o --- bajo un parrafo */
        if (t.n && kind == PK_P && ql >= 2 && (q[0] == '=' || q[0] == '-')) { int all = 1; for (long k = 0; k < ql; k++) if (q[k] != q[0]) all = 0; if (all) { kind = q[0] == '=' ? PK_H1 : PK_H2; FL(); p = nx; continue; } }
        if (t.n) sb_s(&t, kind == PK_P || kind == PK_QUOTE || kind == PK_LI || kind == PK_NUM ? " " : "\n"); md_inline(&t, q, ql); p = nx; }
    if (code) doc_add(d, PK_CODE, t.p, t.n); else FL(); free(t.p);
    #undef FL
}
static void txt_read(const char *s, zdoc *d) {   /* un parrafo por linea; las lineas en blanco separan */
    for (const char *p = s; *p;) { const char *e = strchr(p, '\n'); long l = e ? e - p : (long)strlen(p); doc_add(d, PK_P, p, l); p = e ? e + 1 : p + l; } }
/* ---------- DOCX ---------- */
static void docx_read(const char *x, zdoc *d) {
    sb_t t = {0}; sb_s(&t, ""); int kind = PK_P, in_t = 0; char nm[32], v[64];
    for (const char *p = x; *p;) {
        if (*p == '<') { const char *e = strchr(p, '>'); if (!e) break; int cl; tag_name(p, nm, 32, &cl);
            if (!strcmp(nm, "p")) { if (cl) { doc_add(d, kind, t.p, t.n); t.n = 0; t.p[0] = 0; } else if (e[-1] != '/') kind = PK_P; }
            else if (!strcmp(nm, "pstyle") && attr_get(p, e, "w:val", v, 64)) { char lv[64]; for (int i = 0; v[i]; i++) lv[i] = tolower(v[i]), lv[i + 1] = 0;
                if (!strcmp(lv, "title") || !strcmp(lv, "titulo") || !strcmp(lv, "título")) kind = PK_H1; else if ((strstr(lv, "heading") || strstr(lv, "ttulo") || strstr(lv, "titulo") || strstr(lv, "berschrift")) && isdigit((u8)lv[strlen(lv) - 1])) kind = PK_H1 + MIN(5, MAX(0, lv[strlen(lv) - 1] - '1'));
                else if (strstr(lv, "list") || strstr(lv, "lista")) kind = strstr(lv, "number") ? PK_NUM : PK_LI; else if (strstr(lv, "quote") || strstr(lv, "cita")) kind = PK_QUOTE; else if (strstr(lv, "code") || strstr(lv, "codigo")) kind = PK_CODE; }
            else if (!strcmp(nm, "numpr") && kind == PK_P) kind = PK_LI;
            else if (!strcmp(nm, "t")) in_t = !cl && e[-1] != '/'; else if (!strcmp(nm, "tab")) sb_s(&t, "\t"); else if (!strcmp(nm, "br") || !strcmp(nm, "cr")) sb_s(&t, "\n");
            p = e + 1; continue; }
        const char *q = p; while (*q && *q != '<') q++; if (in_t) { int sp = 0; xml_text_add(&t, p, q - p, &sp, 1); } p = q; }
    free(t.p); }
/* ---------- ODT ---------- */
static void odt_read(const char *x, zdoc *d) {
    sb_t t = {0}; sb_s(&t, ""); int kind = PK_P, depth = 0, inl = 0; char nm[32], v[16];
    for (const char *p = x; *p;) {
        if (*p == '<') { const char *e = strchr(p, '>'); if (!e) break; int cl; tag_name(p, nm, 32, &cl); int self = e[-1] == '/';
            if (!strcmp(nm, "h") || !strcmp(nm, "p")) { if (cl || self) { if (!self || t.n) { doc_add(d, kind, t.p, t.n); t.n = 0; t.p[0] = 0; } depth = 0; } else { if (t.n) { doc_add(d, kind, t.p, t.n); t.n = 0; t.p[0] = 0; } depth = 1; kind = nm[0] == 'h' ? PK_H1 + MAX(0, MIN(5, (attr_get(p, e, "text:outline-level", v, 16) ? atoi(v) : 1) - 1)) : inl ? PK_LI : PK_P; } }
            else if (!strcmp(nm, "list-item")) inl = !cl; else if (!strcmp(nm, "s")) { char c[8]; int k = attr_get(p, e, "text:c", c, 8) ? atoi(c) : 1; for (int i = 0; i < k && i < 64; i++) sb_s(&t, " "); }
            else if (!strcmp(nm, "tab")) sb_s(&t, "\t"); else if (!strcmp(nm, "line-break")) sb_s(&t, "\n");
            p = e + 1; continue; }
        const char *q = p; while (*q && *q != '<') q++; if (depth) { int sp = 0; xml_text_add(&t, p, q - p, &sp, 1); } p = q; }
    free(t.p); }
static char *zip_text(const u8 *z, long n, const char *name) { long l; u8 *x = zip_get(z, n, name, &l); if (!x) return 0; char *s = malloc(l + 1); memcpy(s, x, l); s[l] = 0; free(x); return s; }
/* el zip_get del sistema solo lee guardado y deflate: es lo que usan DOCX, ODT y EPUB */
static void epub_read(const u8 *z, long n, zdoc *d) {
    char *c = zip_text(z, n, "META-INF/container.xml"); char opf[300] = "content.opf"; if (c) { char *r = strstr(c, "full-path"); if (r) { char *e = strchr(r, '>'); attr_get(r - 1, e ? e : r + strlen(r), "full-path", opf, 300); } free(c); }
    char *o = zip_text(z, n, opf); if (!o) return; char base[300]; strncpy0(base, opf, 300); char *sl = strrchr(base, '/'); if (sl) sl[1] = 0; else base[0] = 0;
    { char *t = strstr(o, "<dc:title"); if (t) { t = strchr(t, '>'); char *e = t ? strstr(t, "</dc:title") : 0; if (t && e) { sb_t b = {0}; int sp = 0; xml_text_add(&b, t + 1, e - t - 1, &sp, 0); if (b.p) strncpy0(d->title, b.p, 200); free(b.p); } } }
    for (char *it = strstr(o, "<itemref"); it; it = strstr(it + 8, "<itemref")) { char *e = strchr(it, '>'); if (!e) break; char id[120], href[300]; if (!attr_get(it, e, "idref", id, 120)) continue;
        int found = 0; for (char *m = strstr(o, "<item "); m && !found; m = strstr(m + 5, "<item ")) { char *me = strchr(m, '>'); char mid[120]; if (me && attr_get(m, me, "id", mid, 120) && !strcmp(mid, id) && attr_get(m, me, "href", href, 300)) found = 1; }
        if (!found) continue; char full[600]; snprintf(full, 600, "%s%s", base, href); char *h = zip_text(z, n, full); if (h) { html_read(h, d); free(h); } }
    free(o); }
static int doc_read(const char *path, const u8 *data, long n, zdoc *d, char *err) {
    memset(d, 0, sizeof *d); char *s = 0;
    if (z_ends(path, ".docx")) { s = zip_text(data, n, "word/document.xml"); if (!s) { strcpy(err, tr("No es un DOCX válido")); return -1; } docx_read(s, d); }
    else if (z_ends(path, ".odt")) { s = zip_text(data, n, "content.xml"); if (!s) { strcpy(err, tr("No es un ODT válido")); return -1; } odt_read(s, d); }
    else if (z_ends(path, ".epub")) { epub_read(data, n, d); if (!d->n) { strcpy(err, tr("No es un EPUB válido o no tiene texto")); return -1; } return 0; }
    else if (z_ends(path, ".rtf")) { long l; s = doc_import(path, &l); if (!s) { strcpy(err, tr("No se puede leer el RTF")); return -1; } txt_read(s, d); }
    else { s = malloc(n + 1); memcpy(s, data, n); s[n] = 0; char *t = s; if (n >= 3 && (u8)t[0] == 0xEF && (u8)t[1] == 0xBB && (u8)t[2] == 0xBF) t += 3;
        if (z_ends(path, ".html") || z_ends(path, ".htm") || z_ends(path, ".xhtml")) html_read(t, d); else if (z_ends(path, ".md") || z_ends(path, ".markdown")) md_read(t, d); else txt_read(t, d); }
    free(s); if (!d->title[0]) { strncpy0(d->title, base_name(path), 200); char *dot = strrchr(d->title, '.'); if (dot && dot != d->title) *dot = 0; } return 0; }

/* ---------- escritura ---------- */
static void w_txt(const zdoc *d, sb_t *b) { int num = 0; for (int i = 0; i < d->n; i++) { const dpar *p = &d->p[i]; num = p->k == PK_NUM ? num + 1 : 0;
        if (p->k == PK_LI) sb_f(b, "  • "); else if (p->k == PK_NUM) sb_f(b, "  %d. ", num); else if (p->k == PK_QUOTE) sb_s(b, "    ");
        sb_s(b, p->t); sb_s(b, "\n"); if (p->k == PK_H1 || p->k == PK_H2) { int l = 0; for (const char *q = p->t; *q;) { utf8_next(&q); l++; } for (int k = 0; k < l && k < 200; k++) sb_s(b, p->k == PK_H1 ? "=" : "-"); sb_s(b, "\n"); }
        int nx = i + 1 < d->n ? d->p[i + 1].k : -1; if (!((p->k == PK_LI || p->k == PK_NUM) && (nx == PK_LI || nx == PK_NUM))) sb_s(b, "\n"); } }
static void w_md(const zdoc *d, sb_t *b) { int num = 0; for (int i = 0; i < d->n; i++) { const dpar *p = &d->p[i]; num = p->k == PK_NUM ? num + 1 : 0;
        if (p->k >= PK_H1 && p->k <= PK_H6) { for (int k = 0; k <= p->k - PK_H1; k++) sb_s(b, "#"); sb_s(b, " "); sb_s(b, p->t); }
        else if (p->k == PK_LI) { sb_s(b, "- "); sb_s(b, p->t); } else if (p->k == PK_NUM) { sb_f(b, "%d. ", num); sb_s(b, p->t); } else if (p->k == PK_QUOTE) { sb_s(b, "> "); sb_s(b, p->t); }
        else if (p->k == PK_CODE) { sb_s(b, "```\n"); sb_s(b, p->t); if (p->t[0] && p->t[strlen(p->t) - 1] != '\n') sb_s(b, "\n"); sb_s(b, "```"); } else sb_s(b, p->t);
        sb_s(b, "\n"); int nx = i + 1 < d->n ? d->p[i + 1].k : -1; if (!((p->k == PK_LI || p->k == PK_NUM) && nx == p->k)) sb_s(b, "\n"); } }
static void w_html_body(const zdoc *d, sb_t *b) { int list = 0;
    for (int i = 0; i < d->n; i++) { const dpar *p = &d->p[i]; int want = p->k == PK_LI ? 1 : p->k == PK_NUM ? 2 : 0; if (list != want) { if (list) sb_s(b, list == 1 ? "</ul>\n" : "</ol>\n"); if (want) sb_s(b, want == 1 ? "<ul>\n" : "<ol>\n"); list = want; }
        const char *tg = p->k >= PK_H1 && p->k <= PK_H6 ? 0 : p->k == PK_LI || p->k == PK_NUM ? "li" : p->k == PK_QUOTE ? "blockquote" : p->k == PK_CODE ? "pre" : "p";
        if (!tg) sb_f(b, "<h%d>", p->k - PK_H1 + 1); else sb_f(b, "<%s>", tg); sb_esc(b, p->t, strlen(p->t)); if (!tg) sb_f(b, "</h%d>\n", p->k - PK_H1 + 1); else sb_f(b, "</%s>\n", tg); }
    if (list) sb_s(b, list == 1 ? "</ul>\n" : "</ol>\n"); }
static void w_html(const zdoc *d, sb_t *b) { sb_s(b, "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n<title>"); sb_esc(b, d->title, strlen(d->title));
    sb_s(b, "</title>\n<style>body{font-family:Georgia,serif;max-width:46em;margin:2em auto;padding:0 1em;line-height:1.55;color:#1f2937}h1,h2,h3{font-family:Helvetica,Arial,sans-serif}pre{background:#f3f4f6;padding:.8em;overflow:auto}blockquote{border-left:4px solid #d97706;margin-left:0;padding-left:1em;color:#4b5563}</style>\n</head>\n<body>\n");
    w_html_body(d, b); sb_s(b, "</body>\n</html>\n"); }
static void rtf_str(sb_t *b, const char *s) { for (const char *p = s; *p;) { int c = utf8_next(&p); if (c == '\\' || c == '{' || c == '}') sb_f(b, "\\%c", c); else if (c == '\n') sb_s(b, "\\line "); else if (c == '\t') sb_s(b, "\\tab "); else if (c < 128) { char x = c; sb_add(b, &x, 1); } else if (c < 0x10000) sb_f(b, "\\u%d?", c > 32767 ? c - 65536 : c); else { int v = c - 0x10000, h = 0xD800 + (v >> 10), l = 0xDC00 + (v & 1023); sb_f(b, "\\u%d?\\u%d?", h - 65536, l - 65536); } } }
static void w_rtf(const zdoc *d, sb_t *b) { sb_s(b, "{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1{\\fonttbl{\\f0\\froman Times New Roman;}{\\f1\\fswiss Arial;}{\\f2\\fmodern Courier New;}}\n\\viewkind4\\pard\\sa160\\f0\\fs24\n"); int num = 0;
    for (int i = 0; i < d->n; i++) { const dpar *p = &d->p[i]; num = p->k == PK_NUM ? num + 1 : 0; static const int HS[6] = {40, 32, 28, 26, 24, 24};
        if (p->k >= PK_H1 && p->k <= PK_H6) sb_f(b, "\\pard\\sb240\\sa120\\keepn\\f1\\fs%d\\b ", HS[p->k - PK_H1]); else if (p->k == PK_LI) sb_s(b, "\\pard\\li360\\fi-240\\sa60\\f0\\fs24 \\bullet\\tab "); else if (p->k == PK_NUM) sb_f(b, "\\pard\\li360\\fi-240\\sa60\\f0\\fs24 %d.\\tab ", num);
        else if (p->k == PK_QUOTE) sb_s(b, "\\pard\\li720\\sa160\\f0\\fs24\\i "); else if (p->k == PK_CODE) sb_s(b, "\\pard\\sa160\\f2\\fs20 "); else sb_s(b, "\\pard\\sa160\\f0\\fs24 ");
        rtf_str(b, p->t); sb_s(b, p->k >= PK_H1 && p->k <= PK_H6 ? "\\b0\\par\n" : p->k == PK_QUOTE ? "\\i0\\par\n" : "\\par\n"); } sb_s(b, "}\n"); }
/* ZIP en memoria para DOCX, ODT, EPUB, XLSX: deflate, salvo los que piden ir guardados (mimetype) */
typedef struct { sb_t z, cd; int n; } mzip;
static void mz_add(mzip *m, const char *name, const void *d, long n, int store) { u16 tm, dt; dos_now(&tm, &dt); u32 crc = z_crc32(d, n); long cn = n; u8 *c = 0; if (!store && n > 64) { c = dfl_all(d, n, 2, &cn); if (!c || cn >= n) { free(c); c = 0; cn = n; } }
    int meth = c ? 8 : 0, nl = strlen(name); long off = m->z.n; if (!m->z.p) sb_s(&m->z, ""); s32(&m->z, 0x04034B50); s16(&m->z, 20); s16(&m->z, 0x800); s16(&m->z, meth); s16(&m->z, tm); s16(&m->z, dt); s32(&m->z, crc); s32(&m->z, cn); s32(&m->z, n); s16(&m->z, nl); s16(&m->z, 0); sb_add(&m->z, name, nl); sb_add(&m->z, c ? c : d, cn); free(c);
    if (!m->cd.p) sb_s(&m->cd, ""); s32(&m->cd, 0x02014B50); s16(&m->cd, 20); s16(&m->cd, 20); s16(&m->cd, 0x800); s16(&m->cd, meth); s16(&m->cd, tm); s16(&m->cd, dt); s32(&m->cd, crc); s32(&m->cd, cn); s32(&m->cd, n); s16(&m->cd, nl); s16(&m->cd, 0); s16(&m->cd, 0); s16(&m->cd, 0); s16(&m->cd, 0); s32(&m->cd, 0); s32(&m->cd, off); sb_add(&m->cd, name, nl); m->n++; }
static void mz_s(mzip *m, const char *name, const char *s) { mz_add(m, name, s, strlen(s), 0); }
static void mz_end(mzip *m, sb_t *out) { long cdo = m->z.n; sb_add(&m->z, m->cd.p, m->cd.n); s32(&m->z, 0x06054B50); s16(&m->z, 0); s16(&m->z, 0); s16(&m->z, m->n); s16(&m->z, m->n); s32(&m->z, m->cd.n); s32(&m->z, cdo); s16(&m->z, 0); sb_add(out, m->z.p, m->z.n); free(m->z.p); free(m->cd.p); }
static void w_docx(const zdoc *d, sb_t *out) { mzip m = {0};
    mz_s(&m, "[Content_Types].xml", "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/><Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/><Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/><Override PartName=\"/docProps/core.xml\" ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/></Types>");
    mz_s(&m, "_rels/.rels", "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/><Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" Target=\"docProps/core.xml\"/></Relationships>");
    mz_s(&m, "word/_rels/document.xml.rels", "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/></Relationships>");
    sb_t c = {0}; sb_s(&c, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:title>"); sb_esc(&c, d->title, strlen(d->title)); sb_s(&c, "</dc:title><dc:creator>Alquimia - ZunzunOS</dc:creator></cp:coreProperties>"); mz_add(&m, "docProps/core.xml", c.p, c.n, 0); free(c.p);
    sb_t s = {0}; sb_s(&s, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii=\"Cambria\" w:hAnsi=\"Cambria\"/><w:sz w:val=\"24\"/></w:rPr></w:rPrDefault><w:pPrDefault><w:pPr><w:spacing w:after=\"160\" w:line=\"276\" w:lineRule=\"auto\"/></w:pPr></w:pPrDefault></w:docDefaults><w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/></w:style>");
    static const int HS[6] = {40, 32, 28, 26, 24, 24}; for (int i = 1; i <= 6; i++) sb_f(&s, "<w:style w:type=\"paragraph\" w:styleId=\"Heading%d\"><w:name w:val=\"heading %d\"/><w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:pPr><w:keepNext/><w:spacing w:before=\"240\" w:after=\"120\"/><w:outlineLvl w:val=\"%d\"/></w:pPr><w:rPr><w:rFonts w:ascii=\"Calibri\" w:hAnsi=\"Calibri\"/><w:b/><w:sz w:val=\"%d\"/></w:rPr></w:style>", i, i, i - 1, HS[i - 1]);
    sb_s(&s, "<w:style w:type=\"paragraph\" w:styleId=\"ListBullet\"><w:name w:val=\"List Bullet\"/><w:basedOn w:val=\"Normal\"/><w:pPr><w:spacing w:after=\"60\"/><w:ind w:left=\"720\" w:hanging=\"360\"/></w:pPr></w:style><w:style w:type=\"paragraph\" w:styleId=\"ListNumber\"><w:name w:val=\"List Number\"/><w:basedOn w:val=\"Normal\"/><w:pPr><w:spacing w:after=\"60\"/><w:ind w:left=\"720\" w:hanging=\"360\"/></w:pPr></w:style><w:style w:type=\"paragraph\" w:styleId=\"Quote\"><w:name w:val=\"Quote\"/><w:basedOn w:val=\"Normal\"/><w:pPr><w:ind w:left=\"720\"/></w:pPr><w:rPr><w:i/></w:rPr></w:style><w:style w:type=\"paragraph\" w:styleId=\"Code\"><w:name w:val=\"Code\"/><w:basedOn w:val=\"Normal\"/><w:rPr><w:rFonts w:ascii=\"Courier New\" w:hAnsi=\"Courier New\"/><w:sz w:val=\"20\"/></w:rPr></w:style></w:styles>");
    mz_add(&m, "word/styles.xml", s.p, s.n, 0); free(s.p);
    sb_t b = {0}; sb_s(&b, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body>"); int num = 0;
    for (int i = 0; i < d->n; i++) { const dpar *p = &d->p[i]; num = p->k == PK_NUM ? num + 1 : 0; const char *st = p->k >= PK_H1 && p->k <= PK_H6 ? 0 : p->k == PK_LI ? "ListBullet" : p->k == PK_NUM ? "ListNumber" : p->k == PK_QUOTE ? "Quote" : p->k == PK_CODE ? "Code" : 0;
        sb_s(&b, "<w:p>"); if (p->k >= PK_H1 && p->k <= PK_H6) sb_f(&b, "<w:pPr><w:pStyle w:val=\"Heading%d\"/></w:pPr>", p->k - PK_H1 + 1); else if (st) sb_f(&b, "<w:pPr><w:pStyle w:val=\"%s\"/></w:pPr>", st);
        sb_s(&b, "<w:r>"); if (p->k == PK_LI) sb_s(&b, "<w:t xml:space=\"preserve\">\xE2\x80\xA2\t</w:t>"); else if (p->k == PK_NUM) sb_f(&b, "<w:t xml:space=\"preserve\">%d.\t</w:t>", num);
        const char *q = p->t; for (;;) { const char *e = q; while (*e && *e != '\n' && *e != '\t') e++; if (e > q) { sb_s(&b, "<w:t xml:space=\"preserve\">"); sb_esc(&b, q, e - q); sb_s(&b, "</w:t>"); } if (!*e) break; sb_s(&b, *e == '\n' ? "<w:br/>" : "<w:tab/>"); q = e + 1; }
        sb_s(&b, "</w:r></w:p>"); }
    sb_s(&b, "<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/><w:pgMar w:top=\"1417\" w:right=\"1417\" w:bottom=\"1417\" w:left=\"1417\" w:header=\"708\" w:footer=\"708\" w:gutter=\"0\"/></w:sectPr></w:body></w:document>");
    mz_add(&m, "word/document.xml", b.p, b.n, 0); free(b.p); mz_end(&m, out); }
static void odt_body(const zdoc *d, sb_t *b) { int list = 0;
    for (int i = 0; i < d->n; i++) { const dpar *p = &d->p[i]; int want = p->k == PK_LI ? 1 : p->k == PK_NUM ? 2 : 0; if (list != want) { if (list) sb_s(b, "</text:list>"); if (want) sb_f(b, "<text:list text:style-name=\"%s\">", want == 1 ? "LB" : "LN"); list = want; }
        if (want) sb_s(b, "<text:list-item>");
        if (p->k >= PK_H1 && p->k <= PK_H6) sb_f(b, "<text:h text:style-name=\"Heading_20_%d\" text:outline-level=\"%d\">", p->k - PK_H1 + 1, p->k - PK_H1 + 1); else sb_f(b, "<text:p text:style-name=\"%s\">", p->k == PK_QUOTE ? "Quotations" : p->k == PK_CODE ? "Preformatted_20_Text" : "Text_20_body");
        for (const char *q = p->t; *q; q++) { if (*q == '\n') sb_s(b, "<text:line-break/>"); else if (*q == '\t') sb_s(b, "<text:tab/>"); else if (*q == ' ' && q[1] == ' ') { int k = 0; while (q[k] == ' ') k++; sb_f(b, " <text:s text:c=\"%d\"/>", k - 1); q += k - 1; } else sb_esc(b, q, 1); }
        sb_s(b, p->k >= PK_H1 && p->k <= PK_H6 ? "</text:h>" : "</text:p>"); if (want) sb_s(b, "</text:list-item>"); }
    if (list) sb_s(b, "</text:list>"); }
static void w_odt(const zdoc *d, sb_t *out) { mzip m = {0}; mz_add(&m, "mimetype", "application/vnd.oasis.opendocument.text", 39, 1);
    mz_s(&m, "META-INF/manifest.xml", "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.2\"><manifest:file-entry manifest:full-path=\"/\" manifest:media-type=\"application/vnd.oasis.opendocument.text\"/><manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/><manifest:file-entry manifest:full-path=\"styles.xml\" manifest:media-type=\"text/xml\"/><manifest:file-entry manifest:full-path=\"meta.xml\" manifest:media-type=\"text/xml\"/></manifest:manifest>");
    sb_t mt = {0}; sb_s(&mt, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-meta xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\" office:version=\"1.2\"><office:meta><dc:title>"); sb_esc(&mt, d->title, strlen(d->title)); sb_s(&mt, "</dc:title><meta:generator>Alquimia - ZunzunOS</meta:generator></office:meta></office:document-meta>"); mz_add(&m, "meta.xml", mt.p, mt.n, 0); free(mt.p);
    const char *NS = "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" office:version=\"1.2\"";
    sb_t s = {0}; sb_f(&s, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-styles %s><office:styles><style:style style:name=\"Standard\" style:family=\"paragraph\"><style:text-properties fo:font-size=\"12pt\"/></style:style><style:style style:name=\"Text_20_body\" style:display-name=\"Text body\" style:family=\"paragraph\" style:parent-style-name=\"Standard\"><style:paragraph-properties fo:margin-bottom=\"0.25cm\"/></style:style>", NS);
    static const int HS[6] = {20, 16, 14, 13, 12, 12}; for (int i = 1; i <= 6; i++) sb_f(&s, "<style:style style:name=\"Heading_20_%d\" style:display-name=\"Heading %d\" style:family=\"paragraph\" style:parent-style-name=\"Standard\" style:default-outline-level=\"%d\"><style:paragraph-properties fo:margin-top=\"0.4cm\" fo:margin-bottom=\"0.2cm\" fo:keep-with-next=\"always\"/><style:text-properties fo:font-size=\"%dpt\" fo:font-weight=\"bold\"/></style:style>", i, i, i, HS[i - 1]);
    sb_s(&s, "<style:style style:name=\"Quotations\" style:family=\"paragraph\" style:parent-style-name=\"Text_20_body\"><style:paragraph-properties fo:margin-left=\"1cm\"/><style:text-properties fo:font-style=\"italic\"/></style:style><style:style style:name=\"Preformatted_20_Text\" style:display-name=\"Preformatted Text\" style:family=\"paragraph\" style:parent-style-name=\"Standard\"><style:text-properties style:font-name=\"Courier New\" fo:font-family=\"'Courier New'\" fo:font-size=\"10pt\"/></style:style>");
    sb_s(&s, "<text:list-style style:name=\"LB\"><text:list-level-style-bullet text:level=\"1\" text:bullet-char=\"\xE2\x80\xA2\"><style:list-level-properties text:space-before=\"0.4cm\" text:min-label-width=\"0.5cm\"/></text:list-level-style-bullet></text:list-style><text:list-style style:name=\"LN\"><text:list-level-style-number text:level=\"1\" style:num-suffix=\".\" style:num-format=\"1\"><style:list-level-properties text:space-before=\"0.4cm\" text:min-label-width=\"0.6cm\"/></text:list-level-style-number></text:list-style></office:styles></office:document-styles>");
    mz_add(&m, "styles.xml", s.p, s.n, 0); free(s.p);
    sb_t c = {0}; sb_f(&c, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-content %s><office:body><office:text>", NS); odt_body(d, &c); sb_s(&c, "</office:text></office:body></office:document-content>"); mz_add(&m, "content.xml", c.p, c.n, 0); free(c.p); mz_end(&m, out); }
/* EPUB 3 (con toc.ncx para lectores antiguos): un capitulo por cada titulo de nivel 1 */
static void w_epub(const zdoc *d, sb_t *out) { mzip m = {0}; mz_add(&m, "mimetype", "application/epub+zip", 20, 1);
    mz_s(&m, "META-INF/container.xml", "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\"><rootfiles><rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/></rootfiles></container>");
    int starts[1000], nc = 0; starts[nc++] = 0; for (int i = 1; i < d->n && nc < 999; i++) if (d->p[i].k == PK_H1) starts[nc++] = i; starts[nc] = d->n; if (nc > 1 && starts[1] == 0) { }
    char id[40]; u32 r = rnd() ^ z_crc32((const u8 *)d->title, strlen(d->title)); snprintf(id, 40, "urn:uuid:%08x-5a5a-4c0e-8d1a-%08x%04x", r, rnd(), rnd() & 0xFFFF);
    sb_t nav = {0}, ncx = {0}, opf = {0}; sb_s(&nav, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE html>\n<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\"><head><title>"); sb_esc(&nav, d->title, strlen(d->title)); sb_s(&nav, "</title></head><body><nav epub:type=\"toc\" id=\"toc\"><ol>");
    sb_f(&ncx, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\"><head><meta name=\"dtb:uid\" content=\"%s\"/></head><docTitle><text>", id); sb_esc(&ncx, d->title, strlen(d->title)); sb_s(&ncx, "</text></docTitle><navMap>");
    for (int c = 0; c < nc; c++) { zdoc part = *d; part.p = d->p + starts[c]; part.n = starts[c + 1] - starts[c]; if (!part.n) continue; sb_t x = {0};
        sb_s(&x, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE html>\n<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>"); const char *ct = part.p[0].k == PK_H1 ? part.p[0].t : d->title; sb_esc(&x, ct, strlen(ct)); sb_s(&x, "</title><link rel=\"stylesheet\" type=\"text/css\" href=\"estilo.css\"/></head><body>\n");
        w_html_body(&part, &x); sb_s(&x, "</body></html>\n"); char nm[64]; snprintf(nm, 64, "OEBPS/cap%03d.xhtml", c + 1); mz_add(&m, nm, x.p, x.n, 0); free(x.p);
        sb_f(&nav, "<li><a href=\"cap%03d.xhtml\">", c + 1); sb_esc(&nav, ct, strlen(ct)); sb_s(&nav, "</a></li>"); sb_f(&ncx, "<navPoint id=\"p%d\" playOrder=\"%d\"><navLabel><text>", c + 1, c + 1); sb_esc(&ncx, ct, strlen(ct)); sb_f(&ncx, "</text></navLabel><content src=\"cap%03d.xhtml\"/></navPoint>", c + 1); }
    sb_s(&nav, "</ol></nav></body></html>\n"); sb_s(&ncx, "</navMap></ncx>\n"); mz_add(&m, "OEBPS/nav.xhtml", nav.p, nav.n, 0); mz_add(&m, "OEBPS/toc.ncx", ncx.p, ncx.n, 0); free(nav.p); free(ncx.p);
    mz_s(&m, "OEBPS/estilo.css", "body{font-family:serif;line-height:1.5}h1,h2,h3{font-family:sans-serif}pre{font-family:monospace;white-space:pre-wrap}blockquote{margin-left:1.5em;font-style:italic}");
    u32 t[6]; z_time(t); sb_f(&opf, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\" unique-identifier=\"uid\"><metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:identifier id=\"uid\">%s</dc:identifier><dc:title>", id); sb_esc(&opf, d->title, strlen(d->title));
    sb_f(&opf, "</dc:title><dc:language>%s</dc:language><meta property=\"dcterms:modified\">%04u-%02u-%02uT%02u:%02u:%02uZ</meta></metadata><manifest><item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/><item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/><item id=\"css\" href=\"estilo.css\" media-type=\"text/css\"/>", TH.lang == 1 ? "en" : "es", t[5], t[4], t[3], t[2], t[1], t[0]);
    for (int c = 0; c < nc; c++) if (starts[c + 1] > starts[c]) sb_f(&opf, "<item id=\"c%d\" href=\"cap%03d.xhtml\" media-type=\"application/xhtml+xml\"/>", c + 1, c + 1);
    sb_s(&opf, "</manifest><spine toc=\"ncx\">"); for (int c = 0; c < nc; c++) if (starts[c + 1] > starts[c]) sb_f(&opf, "<itemref idref=\"c%d\"/>", c + 1); sb_s(&opf, "</spine></package>\n"); mz_add(&m, "OEBPS/content.opf", opf.p, opf.n, 0); free(opf.p); mz_end(&m, out); }
static void sprintf_bul(char *t, const char *s) { strcpy(t, "\xE2\x80\xA2 "); strcat(t, s); }
static int w_pdf(const zdoc *d, int paper, int fsize, sb_t *out) { zpset s = pdf_set(paper, 0); zpdoc *pd = zp_new(&s); zpflow f; zp_flow_init(&f, pd, 56); f.num = 1; double fs = fsize > 5 ? fsize : 11; int num = 0;
    for (int i = 0; i < d->n; i++) { const dpar *p = &d->p[i]; num = p->k == PK_NUM ? num + 1 : 0; static const double HK[6] = {2.0, 1.6, 1.35, 1.2, 1.1, 1.0};
        if (p->k >= PK_H1 && p->k <= PK_H6) { if (f.y + fs * 4 > f.ybot && f.started) zp_flow_newpage(&f); f.y += fs * 0.6; zp_para(&f, ZPF_SANSB, fs * HK[p->k - PK_H1], 0x111827, p->t, 0, fs * 0.5); }
        else if (p->k == PK_LI || p->k == PK_NUM) { char *t = malloc(strlen(p->t) + 16); if (p->k == PK_LI) sprintf_bul(t, p->t); else { char nb[12]; snprintf(nb, 12, "%d. ", num); strcpy(t, nb); strcat(t, p->t); } double x0 = f.x0; f.x0 += fs * 1.5; zp_para(&f, ZPF_SERIF, fs, 0x1F2937, t, 0, fs * 0.2); f.x0 = x0; free(t); }
        else if (p->k == PK_QUOTE) { double x0 = f.x0; f.x0 += fs * 2; zp_para(&f, ZPF_SERIF, fs, 0x4B5563, p->t, 0, fs * 0.6); f.x0 = x0; }
        else if (p->k == PK_CODE) zp_para(&f, ZPF_MONO, fs * 0.85, 0x111827, p->t, 0, fs * 0.6);
        else zp_para(&f, ZPF_SERIF, fs, 0x1F2937, p->t, 0, fs * 0.6); }
    if (!f.started) zp_flow_newpage(&f); u8 *o; long n = zp_render(pd, &s, "pdf", &o); zp_free(pd); if (n <= 0) return -1; sb_add(out, o, n); free(o); return 0; }
