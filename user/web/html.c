/* Motor HTML/CSS de MiOS.
 *
 * Tres fases:
 *   1. Analisis: el texto HTML se convierte en un arbol de nodos.
 *   2. Estilo: a cada nodo se le calcula su estilo (estilos por defecto de
 *      cada etiqueta + hojas <style> + atributo style=""), con selectores
 *      de etiqueta, .clase, #id, descendientes y variables CSS --var.
 *   3. Maquetacion: flujo de bloques y lineas de texto con ajuste de
 *      palabras, listas, tablas y controles de formulario. El resultado es
 *      una lista de operaciones de dibujo (rectangulos y textos) en
 *      coordenadas del documento, que la ventana pinta con desplazamiento.
 *
 * No hay JavaScript: los <script> se ignoran. */
#include "kernel.h"

/* ====================================================================== */
/* Memoria: todo el documento sale de una "arena" que se libera de golpe.  */

struct chunk { struct chunk *next; u32 used, cap; };

struct arena { struct chunk *head; };

static void *aalloc(struct arena *a, u32 n) {
    n = (n + 7) & ~7u;
    if (!a->head || a->head->used + n > a->head->cap) {
#ifdef MIOS_POCKET
        u32 cap = n > 6144 ? n : 6144;               /* el ESP32 tiene poca RAM */
#else
        u32 cap = n > 65536 ? n : 65536;
#endif
        struct chunk *c = kmalloc(sizeof(struct chunk) + cap);
        if (!c) return NULL;
        c->next = a->head; c->used = 0; c->cap = cap;
        a->head = c;
    }
    void *p = (u8 *)(a->head + 1) + a->head->used;
    a->head->used += n;
    memset(p, 0, n);
    return p;
}

static char *astrndup(struct arena *a, const char *s, int n) {
    char *d = aalloc(a, (u32)n + 1);
    if (d) { memcpy(d, s, (size_t)n); d[n] = 0; }
    return d;
}

static void afree(struct arena *a) {
    while (a->head) { struct chunk *n = a->head->next; kfree(a->head); a->head = n; }
}

/* ====================================================================== */
/* Etiquetas                                                               */

enum {
    T_NONE, T_HTML, T_HEAD, T_BODY, T_TITLE, T_STYLE, T_SCRIPT, T_META, T_LINK, T_BASE,
    T_P, T_DIV, T_SPAN, T_H1, T_H2, T_H3, T_H4, T_H5, T_H6, T_BR, T_HR, T_A,
    T_B, T_STRONG, T_I, T_EM, T_U, T_S, T_STRIKE, T_DEL, T_INS, T_SMALL, T_BIG,
    T_CODE, T_PRE, T_KBD, T_SAMP, T_TT, T_VAR, T_BLOCKQUOTE, T_UL, T_OL, T_LI,
    T_DL, T_DT, T_DD, T_TABLE, T_THEAD, T_TBODY, T_TFOOT, T_TR, T_TD, T_TH,
    T_CAPTION, T_COL, T_COLGROUP, T_IMG, T_INPUT, T_BUTTON, T_SELECT, T_OPTION,
    T_OPTGROUP, T_TEXTAREA, T_LABEL, T_FORM, T_HEADER, T_FOOTER, T_NAV, T_MAIN,
    T_SECTION, T_ARTICLE, T_ASIDE, T_FIGURE, T_FIGCAPTION, T_CENTER, T_FONT,
    T_SUP, T_SUB, T_MARK, T_DETAILS, T_SUMMARY, T_NOSCRIPT, T_SVG, T_CANVAS,
    T_IFRAME, T_VIDEO, T_AUDIO, T_ABBR, T_CITE, T_Q, T_ADDRESS, T_FIELDSET,
    T_LEGEND, T_TEMPLATE, T_PROGRESS, T_METER, T_WBR, T_AREA, T_SOURCE, T_TRACK,
    T_EMBED, T_PARAM, T_OBJECT, T_MAP, T_PICTURE, T_TIME, T_DIALOG, T_HGROUP,
    T_COUNT
};

static const char *tag_names[T_COUNT] = {
    "", "html", "head", "body", "title", "style", "script", "meta", "link", "base",
    "p", "div", "span", "h1", "h2", "h3", "h4", "h5", "h6", "br", "hr", "a",
    "b", "strong", "i", "em", "u", "s", "strike", "del", "ins", "small", "big",
    "code", "pre", "kbd", "samp", "tt", "var", "blockquote", "ul", "ol", "li",
    "dl", "dt", "dd", "table", "thead", "tbody", "tfoot", "tr", "td", "th",
    "caption", "col", "colgroup", "img", "input", "button", "select", "option",
    "optgroup", "textarea", "label", "form", "header", "footer", "nav", "main",
    "section", "article", "aside", "figure", "figcaption", "center", "font",
    "sup", "sub", "mark", "details", "summary", "noscript", "svg", "canvas",
    "iframe", "video", "audio", "abbr", "cite", "q", "address", "fieldset",
    "legend", "template", "progress", "meter", "wbr", "area", "source", "track",
    "embed", "param", "object", "map", "picture", "time", "dialog", "hgroup",
};

static int is_void(int t) {
    return t == T_BR || t == T_HR || t == T_IMG || t == T_INPUT || t == T_META ||
           t == T_LINK || t == T_BASE || t == T_COL || t == T_WBR || t == T_AREA ||
           t == T_SOURCE || t == T_TRACK || t == T_EMBED || t == T_PARAM;
}

/* Etiquetas que cierran un <p> abierto. */
static int closes_p(int t) {
    switch (t) {
    case T_P: case T_DIV: case T_H1: case T_H2: case T_H3: case T_H4: case T_H5:
    case T_H6: case T_UL: case T_OL: case T_DL: case T_TABLE: case T_PRE:
    case T_BLOCKQUOTE: case T_HR: case T_HEADER: case T_FOOTER: case T_NAV:
    case T_MAIN: case T_SECTION: case T_ARTICLE: case T_ASIDE: case T_FIGURE:
    case T_FORM: case T_FIELDSET: case T_ADDRESS: case T_DETAILS: case T_CENTER:
        return 1;
    }
    return 0;
}

/* ====================================================================== */
/* Arbol y estilo                                                          */

enum { D_INLINE, D_BLOCK, D_NONE, D_LISTITEM, D_TABLE, D_ROW, D_CELL, D_ROWGROUP };
enum { A_LEFT, A_CENTER, A_RIGHT };

struct style {
    u32 color, bg;
    u8 has_bg, bold, italic, underline, strike, scale, align, display, pre;
    u8 list, upper, hidden, border_any, center_auto, mono;
    short m[4], p[4];                    /* arriba, derecha, abajo, izquierda */
    u8 bw[4];
    u32 bc[4];
    int width, width_pct, max_width;     /* 0 = sin fijar */
    int min_height;
};

enum { N_ELEM, N_TEXT };

struct node {
    u8 type, tag;
    struct node *parent, *first, *last, *next;
    char *text;                          /* nodos de texto (CP437) */
    int tlen;
    char *id, *cls, *style, *href, *align, *bgcolor, *color, *width, *height;
    char *alt, *value, *placeholder, *itype, *border, *start, *colspan, *name, *src, *action, *method;
    int hidden_attr, checked;
    char *calc, *cif;                    /* data-calc y data-if */
    char *equiv, *content;               /* <meta http-equiv content> */
    char *edit;                          /* texto de un campo editable */
    int dec, selidx;                     /* data-dec + 1 (0 = auto); opcion elegida */
    struct style s;
    int y;                               /* posicion para los enlaces #ancla */
};

struct op {
    u8 type, scale, bold, underline, strike, mono;
    int x, y, w, h;
    int dy, ih;                          /* colocacion dentro de la linea */
    u32 color, bg;
    char *str;
    int len;
    char *href;
    struct node *node;                   /* control de formulario que se puede pulsar */
    u32 *px; int iw, ih2;                /* imagen */
};
enum { OP_RECT, OP_TEXT, OP_IMG };

struct rule { struct sel *sel; int spec, order; char *decls; };

#define MAX_COMPOUND 8
struct compound { int tag, any, impossible, nclass; char *id; char *cls[6]; };
struct sel { int n; struct compound c[MAX_COMPOUND]; };

struct var { char *name, *value; };

struct html_doc {
    struct arena a;
    struct node *root;
    char *title;
    int has_script;
    struct rule *rules; int nrules, caprules;
    struct var *vars; int nvars, capvars;
    struct op *ops; int nops, capops;
    int height, width;
    u32 bg;
    struct node *focus;                  /* campo con el cursor */
    struct node **ids; int nids;         /* nodos con id (para las formulas) */
    struct node **ctrls; int nctrls;     /* controles de formulario, en orden */
    char *imgs[96]; int nimgs;           /* imagenes que faltan por descargar */
    char submit[3072]; int submit_post;  /* formulario enviado: accion?datos */
};
int (*html_img_get)(const char *src, u32 **px, int *w, int *h);

static struct node *nth_option(struct node *sel, int idx, int *count);
static int is_submit(struct node *n);
static const char *option_text(struct node *o);
static int visible_if(struct html_doc *d, struct node *n);
static int doc_lookup(void *user, const char *name, int len, const char **text, int *is_expr);

/* ====================================================================== */
/* Utilidades de texto                                                     */

static int lc(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int is_ws(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
static int is_alnum(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

static int starts_ci(const char *s, const char *end, const char *pre) {
    while (*pre) {
        if (s >= end || lc((u8)*s) != *pre) return 0;
        s++; pre++;
    }
    return 1;
}

static const char *find_ci(const char *s, const char *end, const char *needle) {
    for (; s < end; s++) if (starts_ci(s, end, needle)) return s;
    return end;
}

static char *trim(char *s) {
    while (is_ws((u8)*s)) s++;
    int n = (int)strlen(s);
    while (n && is_ws((u8)s[n - 1])) s[--n] = 0;
    return s;
}

/* ====================================================================== */
/* Entidades y texto                                                       */

static const struct { const char *name; u16 uni; } entities[] = {
    {"amp",'&'},{"lt",'<'},{"gt",'>'},{"quot",'"'},{"apos",'\''},{"nbsp",0xA0},
    {"copy",0xA9},{"reg",0xAE},{"trade",0x2122},{"hellip",0x2026},{"mdash",0x2014},
    {"ndash",0x2013},{"lsquo",0x2018},{"rsquo",0x2019},{"ldquo",0x201C},{"rdquo",0x201D},
    {"laquo",0xAB},{"raquo",0xBB},{"bull",0x2022},{"middot",0xB7},{"deg",0xB0},
    {"plusmn",0xB1},{"times",0xD7},{"divide",0xF7},{"euro",0x20AC},{"pound",0xA3},
    {"yen",0xA5},{"cent",0xA2},{"sect",0xA7},{"para",0xB6},{"iexcl",0xA1},{"iquest",0xBF},
    {"aacute",0xE1},{"eacute",0xE9},{"iacute",0xED},{"oacute",0xF3},{"uacute",0xFA},
    {"Aacute",0xC1},{"Eacute",0xC9},{"Iacute",0xCD},{"Oacute",0xD3},{"Uacute",0xDA},
    {"ntilde",0xF1},{"Ntilde",0xD1},{"uuml",0xFC},{"Uuml",0xDC},{"auml",0xE4},
    {"ouml",0xF6},{"Auml",0xC4},{"Ouml",0xD6},{"szlig",0xDF},{"agrave",0xE0},
    {"egrave",0xE8},{"ograve",0xF2},{"ccedil",0xE7},{"Ccedil",0xC7},{"rarr",0x2192},
    {"larr",0x2190},{"uarr",0x2191},{"darr",0x2193},{"harr",0x2194},{"check",0x2713},
    {"hearts",0x2665},{"ordm",0xBA},{"ordf",0xAA},{"frac12",0xBD},{"frac14",0xBC},
    {"sup2",0xB2},{"sup3",0xB3},{"micro",0xB5},{"alpha",0x3B1},{"beta",0xDF},
    {"pi",0x3C0},{"sigma",0x3C3},{"Sigma",0x3A3},{"omega",0x3C9},{"Omega",0x3A9},
    {"delta",0x3B4},{"mu",0xB5},{"le",0x2264},{"ge",0x2265},{"ne",0x2260},
    {"thinsp",0x2009},{"ensp",0x2002},{"emsp",0x2003},{"zwj",0x200D},{"zwnj",0x200C},
    {"shy",0xAD},{"infin",0x221E},{"asymp",0x2248},{"minus",0x2212},{"prime",0x2032},
    {"acirc",0xE2},{"ecirc",0xEA},{"ocirc",0xF4},{"icirc",0xEE},{"ucirc",0xFB},
    {"atilde",0xE3},{"otilde",0xF5},{"iuml",0xEF},{"euml",0xEB},{"aring",0xE5},
    {"rArr",0x21D2},{"hArr",0x2194},{"star",0x2606},{"starf",0x2605},
    {"rsaquo",0x203A},{"lsaquo",0x2039},{"sbquo",0x201A},{"bdquo",0x201E},{"dagger",0x2020},
    {"permil",0x2030},{"loz",0x25CA},{"spades",0x2660},{"clubs",0x2663},{"diams",0x2666},
    {"crarr",0x21B5},{"half",0xBD},{"frac34",0xBE},{"not",0xAC},{"curren",0xA4},
    {"brvbar",0xA6},{"uml",0xA8},{"macr",0xAF},{"acute",0xB4},{"cedil",0xB8},
    {"sup1",0xB9},{"Agrave",0xC0},{"Egrave",0xC8},{"Ograve",0xD2},{"yuml",0xFF},
    {"lambda",0x3BB},{"theta",0x3B8},{"epsilon",0x3B5},{"gamma",0x3B3},{"Delta",0x394},
    {"tau",0x3C4},{"phi",0x3C6},{"radic",0x221A},{"sum",0x2211},{"part",0x2202},
    {"nabla",0x2207},{"isin",0x2208},{"cap",0x2229},{"equiv",0x2261},{"lArr",0x21D0},
};

/* Convierte un caracter Unicode a lo que se ve en pantalla (0 = omitir). */
static int show_char(u32 cp) {
    if (cp == 0xA0) return 0xFF;                       /* espacio que no se parte */
    if (cp >= 0x1F000 || (cp >= 0x2600 && cp < 0x2800 && cp != 0x2605 && cp != 0x2606 &&
        cp != 0x2665 && cp != 0x2713 && cp != 0x2714 && cp != 0x2717 && cp != 0x2718) ||
        cp == 0xFE0F || cp == 0xFE0E || cp == 0x200D || cp == 0x200C || cp == 0x200B ||
        cp == 0xAD || cp == 0x2060 || (cp >= 0x2B00 && cp < 0x2C00) || (cp >= 0x1F3FB && cp <= 0x1F3FF))
        return 0;                                      /* emojis y caracteres invisibles */
    if (cp < 32) return cp == '\n' || cp == '\t' ? (int)cp : ' ';
    int c = uni_to_cp437(cp);
    if (c) return c;
    return cp < 0x80 ? (int)cp : '?';
}

/* Decodifica texto HTML (UTF-8 + entidades) a CP437. */
static int decode_text(const char *s, const char *end, char *out) {
    int n = 0;
    const u8 *p = (const u8 *)s;
    while (p < (const u8 *)end) {
        u32 cp;
        if (*p == '&') {
            const u8 *q = p + 1;
            cp = 0;
            if (*q == '#') {
                q++;
                int hex = (*q == 'x' || *q == 'X');
                if (hex) q++;
                while (q < (const u8 *)end && q - p < 12) {
                    int c = *q, d;
                    if (c >= '0' && c <= '9') d = c - '0';
                    else if (hex && lc(c) >= 'a' && lc(c) <= 'f') d = lc(c) - 'a' + 10;
                    else break;
                    cp = cp * (hex ? 16 : 10) + (u32)d;
                    q++;
                }
                if (q < (const u8 *)end && *q == ';') q++;
                if (cp) p = q; else cp = *p++;
            } else {
                int len = 0;
                while (q + len < (const u8 *)end && is_alnum(q[len]) && len < 10) len++;
                int found = 0;
                for (u32 i = 0; i < sizeof entities / sizeof entities[0]; i++) {
                    if ((int)strlen(entities[i].name) == len &&
                        !memcmp(entities[i].name, (const char *)q, (size_t)len)) {
                        cp = entities[i].uni; found = 1; break;
                    }
                }
                if (found) { p = q + len; if (p < (const u8 *)end && *p == ';') p++; }
                else cp = *p++;
            }
        } else {
            int c = utf8_decode(&p, (const u8 *)end);
            if (c < 0) break;
            cp = (u32)c;
        }
        int sc = show_char(cp);
        if (sc) out[n++] = (char)sc;
    }
    out[n] = 0;
    return n;
}

/* ====================================================================== */
/* Analisis del HTML                                                       */

static int lookup_tag(const char *s, int n) {
    char buf[16];
    if (n >= 16) return T_NONE;
    for (int i = 0; i < n; i++) buf[i] = (char)lc((u8)s[i]);
    buf[n] = 0;
    for (int t = 1; t < T_COUNT; t++) if (!strcmp(buf, tag_names[t])) return t;
    return T_NONE;
}

static struct node *new_node(struct html_doc *d, struct node *parent, int type, int tag) {
    struct node *n = aalloc(&d->a, sizeof *n);
    if (!n) return NULL;
    n->type = (u8)type;
    n->tag = (u8)tag;
    n->parent = parent;
    if (parent) {
        if (parent->last) parent->last->next = n; else parent->first = n;
        parent->last = n;
    }
    return n;
}

static void add_text(struct html_doc *d, struct node *parent, const char *s, const char *e) {
    if (e <= s) return;
    char *buf = aalloc(&d->a, (u32)(e - s) + 1);
    if (!buf) return;
    int n = decode_text(s, e, buf);
    if (!n) return;
    struct node *t = new_node(d, parent, N_TEXT, 0);
    if (t) { t->text = buf; t->tlen = n; }
}

static void parse_css(struct html_doc *d, const char *s, const char *end);

static void set_attr(struct html_doc *d, struct node *n, const char *name, int nl, char *val) {
    char k[16];
    if (nl >= 16) return;
    for (int i = 0; i < nl; i++) k[i] = (char)lc((u8)name[i]);
    k[nl] = 0;
    if (!strcmp(k, "id")) n->id = val;
    else if (!strcmp(k, "class")) n->cls = val;
    else if (!strcmp(k, "style")) n->style = val;
    else if (!strcmp(k, "href")) n->href = val;
    else if (!strcmp(k, "align")) n->align = val;
    else if (!strcmp(k, "bgcolor")) n->bgcolor = val;
    else if (!strcmp(k, "color")) n->color = val;
    else if (!strcmp(k, "width")) n->width = val;
    else if (!strcmp(k, "height")) n->height = val;
    else if (!strcmp(k, "alt")) n->alt = val;
    else if (!strcmp(k, "value")) n->value = val;
    else if (!strcmp(k, "placeholder")) n->placeholder = val;
    else if (!strcmp(k, "type")) n->itype = val;
    else if (!strcmp(k, "border")) n->border = val;
    else if (!strcmp(k, "start")) n->start = val;
    else if (!strcmp(k, "colspan")) n->colspan = val;
    else if (!strcmp(k, "name")) n->name = val;
    else if (!strcmp(k, "src")) n->src = val;
    else if (!strcmp(k, "action")) n->action = val;
    else if (!strcmp(k, "method")) n->method = val;
    else if (!strcmp(k, "hidden")) n->hidden_attr = 1;
    else if (!strcmp(k, "checked") || !strcmp(k, "selected")) n->checked = 1;
    else if (!strcmp(k, "data-calc")) n->calc = val;
    else if (!strcmp(k, "data-if")) n->cif = val;
    else if (!strcmp(k, "data-dec")) n->dec = atoi(val) + 1;
    else if (!strcmp(k, "http-equiv")) n->equiv = val;
    else if (!strcmp(k, "content")) n->content = val;
    (void)d;
}

static void parse_html(struct html_doc *d, const char *src, u32 len) {
    const char *p = src, *end = src + len;
    struct node *root = new_node(d, NULL, N_ELEM, T_NONE);
    struct node *cur = root;
    d->root = root;
    const char *text = p;

    while (p < end) {
        if (*p != '<') { p++; continue; }
        if (p + 1 < end && (p[1] == '/' || p[1] == '!' || p[1] == '?' ||
            (lc((u8)p[1]) >= 'a' && lc((u8)p[1]) <= 'z'))) {
            add_text(d, cur, text, p);
        } else { p++; continue; }

        if (starts_ci(p, end, "<!--")) {
            const char *q = find_ci(p + 4, end, "-->");
            p = q < end ? q + 3 : end;
            text = p;
            continue;
        }
        if (p[1] == '!' || p[1] == '?') {
            while (p < end && *p != '>') p++;
            text = ++p;
            continue;
        }
        if (p[1] == '/') {                               /* etiqueta de cierre */
            const char *q = p + 2;
            while (q < end && is_alnum((u8)*q)) q++;
            int tag = lookup_tag(p + 2, (int)(q - p - 2));
            for (struct node *a = cur; a && a != root; a = a->parent) {
                if (a->tag == tag) { cur = a->parent; break; }
                if (a->tag == T_TABLE && tag != T_TABLE) break;
            }
            while (q < end && *q != '>') q++;
            text = p = q + 1;
            continue;
        }

        /* Etiqueta de apertura */
        const char *q = p + 1;
        while (q < end && (is_alnum((u8)*q) || *q == '-')) q++;
        int tag = lookup_tag(p + 1, (int)(q - p - 1));

        /* Cierres implicitos */
        if (closes_p(tag)) while (cur->tag == T_P) cur = cur->parent;
        if (tag == T_LI)
            for (struct node *a = cur; a && a != root; a = a->parent) {
                if (a->tag == T_UL || a->tag == T_OL) break;
                if (a->tag == T_LI) { cur = a->parent; break; }
            }
        if (tag == T_DT || tag == T_DD)
            for (struct node *a = cur; a && a != root; a = a->parent) {
                if (a->tag == T_DL) break;
                if (a->tag == T_DT || a->tag == T_DD) { cur = a->parent; break; }
            }
        if (tag == T_TR)
            for (struct node *a = cur; a && a != root; a = a->parent) {
                if (a->tag == T_TABLE) break;
                if (a->tag == T_TR) { cur = a->parent; break; }
            }
        if (tag == T_TD || tag == T_TH)
            for (struct node *a = cur; a && a != root; a = a->parent) {
                if (a->tag == T_TR || a->tag == T_TABLE) break;
                if (a->tag == T_TD || a->tag == T_TH) { cur = a->parent; break; }
            }
        if (tag == T_OPTION && cur->tag == T_OPTION) cur = cur->parent;

        struct node *n = new_node(d, cur, N_ELEM, tag);
        if (!n) return;

        /* Atributos */
        int self_close = 0;
        while (q < end && *q != '>') {
            if (is_ws((u8)*q)) { q++; continue; }
            if (*q == '/') { self_close = 1; q++; continue; }
            const char *an = q;
            while (q < end && !is_ws((u8)*q) && *q != '=' && *q != '>' && *q != '/') q++;
            int anl = (int)(q - an);
            while (q < end && is_ws((u8)*q)) q++;
            char *val = "";
            if (q < end && *q == '=') {
                q++;
                while (q < end && is_ws((u8)*q)) q++;
                const char *vs, *ve;
                if (q < end && (*q == '"' || *q == '\'')) {
                    char quote = *q++;
                    vs = q;
                    while (q < end && *q != quote) q++;
                    ve = q;
                    if (q < end) q++;
                } else {
                    vs = q;
                    while (q < end && !is_ws((u8)*q) && *q != '>') q++;
                    ve = q;
                }
                val = aalloc(&d->a, (u32)(ve - vs) + 1);
                if (val) decode_text(vs, ve, val); else val = "";
            }
            if (anl) set_attr(d, n, an, anl, val);
            else q++;
        }
        p = q < end ? q + 1 : end;
        text = p;

        if (tag == T_SCRIPT || tag == T_STYLE || tag == T_TITLE || tag == T_TEXTAREA) {
            char closing[16];
            ksnprintf(closing, sizeof closing, "</%s", tag_names[tag]);
            const char *e = find_ci(p, end, closing);
            if (tag == T_SCRIPT) d->has_script = 1;
            else if (tag == T_STYLE) parse_css(d, p, e);
            else if (tag == T_TITLE && !d->title) {
                char *t = aalloc(&d->a, (u32)(e - p) + 1);
                if (t) { decode_text(p, e, t); d->title = trim(t); }
            } else if (tag == T_TEXTAREA) {
                char *t = aalloc(&d->a, (u32)(e - p) + 1);
                if (t) { decode_text(p, e, t); n->value = t; }
            }
            p = e;
            while (p < end && *p != '>') p++;
            if (p < end) p++;
            text = p;
            continue;
        }
        if (!is_void(tag) && !self_close) cur = n;
    }
    add_text(d, cur, text, end);
}

/* ====================================================================== */
/* CSS                                                                     */

static void push_rule(struct html_doc *d, struct sel *sel, int spec, char *decls) {
    if (d->nrules == d->caprules) {
        int cap = d->caprules ? d->caprules * 2 : 128;
        struct rule *r = aalloc(&d->a, (u32)cap * sizeof *r);
        if (!r) return;
        if (d->rules) memcpy(r, d->rules, (size_t)d->nrules * sizeof *r);
        d->rules = r;
        d->caprules = cap;
    }
    struct rule *r = &d->rules[d->nrules];
    r->sel = sel; r->spec = spec; r->order = d->nrules; r->decls = decls;
    d->nrules++;
}

static void set_var(struct html_doc *d, char *name, char *value) {
    for (int i = 0; i < d->nvars; i++)
        if (!strcmp(d->vars[i].name, name)) { d->vars[i].value = value; return; }
    if (d->nvars == d->capvars) {
        int cap = d->capvars ? d->capvars * 2 : 64;
        struct var *v = aalloc(&d->a, (u32)cap * sizeof *v);
        if (!v) return;
        if (d->vars) memcpy(v, d->vars, (size_t)d->nvars * sizeof *v);
        d->vars = v;
        d->capvars = cap;
    }
    d->vars[d->nvars].name = name;
    d->vars[d->nvars].value = value;
    d->nvars++;
}

/* Analiza un selector como "div.nota > p". Devuelve la especificidad, o -1. */
static int parse_selector(struct html_doc *d, char *s, struct sel *out) {
    memset(out, 0, sizeof *out);
    int spec = 0;
    char *p = s;
    while (*p) {
        while (is_ws((u8)*p) || *p == '>' || *p == '+' || *p == '~') p++;
        if (!*p) break;
        if (out->n == MAX_COMPOUND) return -1;
        struct compound *c = &out->c[out->n++];
        c->tag = -1;
        while (*p && !is_ws((u8)*p) && *p != '>' && *p != '+' && *p != '~') {
            if (*p == '*') { c->any = 1; p++; }
            else if (*p == '#' || *p == '.') {
                char kind = *p++;
                char *st = p;
                while (*p && (is_alnum((u8)*p) || *p == '-' || *p == '_')) p++;
                char *v = astrndup(&d->a, st, (int)(p - st));
                if (kind == '#') { c->id = v; spec += 100; }
                else if (c->nclass < 6) { c->cls[c->nclass++] = v; spec += 10; }
            } else if (*p == ':') {
                p++;
                if (*p == ':') { c->impossible = 1; p++; }
                char *st = p;
                while (*p && (is_alnum((u8)*p) || *p == '-')) p++;
                int nl = (int)(p - st);
                if (*p == '(') {                          /* :not(...), :nth-child(...) */
                    int depth = 0;
                    do { if (*p == '(') depth++; else if (*p == ')') depth--; p++; } while (*p && depth);
                    c->impossible = 1;
                } else if (nl == 4 && !strncasecmp(st, "root", 4)) {
                    c->tag = T_HTML;
                } else if (!(nl == 4 && !strncasecmp(st, "link", 4)) &&
                           !(nl == 7 && !strncasecmp(st, "visited", 7))) {
                    c->impossible = 1;                    /* :hover, :focus, :first-child... */
                }
                spec += 10;
            } else if (*p == '[') {
                while (*p && *p != ']') p++;
                if (*p) p++;
                c->impossible = 1;
            } else if (is_alnum((u8)*p)) {
                char *st = p;
                while (*p && (is_alnum((u8)*p) || *p == '-')) p++;
                c->tag = lookup_tag(st, (int)(p - st));
                if (!c->tag) c->impossible = 1;
                spec += 1;
            } else {
                return -1;
            }
        }
    }
    return out->n ? spec : -1;
}

/* Evalua de forma aproximada una condicion @media para una pantalla de
 * unos 800 px de ancho, sin impresora y en modo claro. */
static int media_ok(const char *s, const char *e, int width) {
    if (find_ci(s, e, "print") < e && find_ci(s, e, "screen") == e) return 0;
    if (find_ci(s, e, "prefers-color-scheme") < e && find_ci(s, e, "dark") < e) return 0;
    const char *m;
    if ((m = find_ci(s, e, "max-width")) < e) {
        while (m < e && !(*m >= '0' && *m <= '9')) m++;
        if (atoi(m) < width) return 0;
    }
    if ((m = find_ci(s, e, "min-width")) < e) {
        while (m < e && !(*m >= '0' && *m <= '9')) m++;
        if (atoi(m) > width) return 0;
    }
    return 1;
}

static const char *skip_block(const char *p, const char *end) {
    int depth = 0;
    for (; p < end; p++) {
        if (*p == '{') depth++;
        else if (*p == '}' && --depth <= 0) return p + 1;
    }
    return end;
}

static int css_width = 800;

static void parse_css(struct html_doc *d, const char *s, const char *end) {
    /* Copia sin comentarios */
    char *buf = aalloc(&d->a, (u32)(end - s) + 1);
    if (!buf) return;
    int n = 0;
    for (const char *p = s; p < end; p++) {
        if (p + 1 < end && p[0] == '/' && p[1] == '*') {
            const char *q = find_ci(p + 2, end, "*/");
            p = q < end ? q + 1 : end;
            continue;
        }
        buf[n++] = *p;
    }
    buf[n] = 0;
    char *p = buf, *e = buf + n;

    while (p < e) {
        while (p < e && (is_ws((u8)*p) || *p == '}' || *p == ';')) p++;
        if (p >= e) break;
        if (*p == '@') {
            char *st = p;
            while (p < e && *p != '{' && *p != ';') p++;
            if (p >= e) break;
            if (*p == ';') { p++; continue; }
            if (starts_ci(st, p, "@media") || starts_ci(st, p, "@supports")) {
                char *body = p + 1;
                char *bend = (char *)skip_block(p, e) - 1;
                if (starts_ci(st, p, "@supports") || media_ok(st, p, css_width))
                    parse_css(d, body, bend);
                p = bend + 1;
            } else {
                p = (char *)skip_block(p, e);           /* @font-face, @keyframes... */
            }
            continue;
        }
        char *sel_start = p;
        while (p < e && *p != '{') p++;
        if (p >= e) break;
        *p = 0;
        char *decl = p + 1;
        char *q = decl;
        while (q < e && *q != '}') q++;
        if (q < e) *q = 0;
        char *next = q + 1;

        /* Variables CSS globales (:root, html, body, *) */
        char *selt = trim(sel_start);
        int global = !strcmp(selt, ":root") || !strcasecmp(selt, "html") ||
                     !strcasecmp(selt, "body") || !strcmp(selt, "*");
        if (global) {
            char *v = decl;
            while ((v = (char *)find_ci(v, q, "--")) < q) {
                char *nm = v, *colon = v;
                while (colon < q && *colon != ':' && *colon != ';') colon++;
                if (colon >= q || *colon != ':') break;
                char *val = colon + 1, *vend = val;
                int depth = 0;
                while (vend < q && (*vend != ';' || depth)) {
                    if (*vend == '(') depth++; else if (*vend == ')') depth--;
                    vend++;
                }
                set_var(d, trim(astrndup(&d->a, nm, (int)(colon - nm))),
                        trim(astrndup(&d->a, val, (int)(vend - val))));
                v = vend;
            }
        }

        /* Un selector por cada parte separada por comas */
        char *part = selt;
        for (;;) {
            char *comma = part;
            int depth = 0;
            while (*comma && (*comma != ',' || depth)) {
                if (*comma == '(') depth++; else if (*comma == ')') depth--;
                comma++;
            }
            char saved = *comma;
            *comma = 0;
            struct sel *sel = aalloc(&d->a, sizeof *sel);
            int spec = sel ? parse_selector(d, part, sel) : -1;
            if (spec >= 0) push_rule(d, sel, spec, decl);
            if (!saved) break;
            part = comma + 1;
        }
        p = next;
    }
}

static int has_class(const char *list, const char *cls) {
    if (!list) return 0;
    int n = (int)strlen(cls);
    const char *p = list;
    while (*p) {
        while (is_ws((u8)*p)) p++;
        const char *st = p;
        while (*p && !is_ws((u8)*p)) p++;
        if (p - st == n && !memcmp(st, cls, (size_t)n)) return 1;
    }
    return 0;
}

static int match_compound(struct node *n, struct compound *c) {
    if (c->impossible || n->type != N_ELEM) return 0;
    if (c->tag >= 0 && c->tag != n->tag) return 0;
    if (c->id && (!n->id || strcmp(c->id, n->id))) return 0;
    for (int i = 0; i < c->nclass; i++) if (!has_class(n->cls, c->cls[i])) return 0;
    return 1;
}

static int match_sel(struct node *n, struct sel *s) {
    if (!match_compound(n, &s->c[s->n - 1])) return 0;
    struct node *a = n->parent;
    for (int i = s->n - 2; i >= 0; i--) {
        while (a && !match_compound(a, &s->c[i])) a = a->parent;
        if (!a) return 0;
        a = a->parent;
    }
    return 1;
}

/* --- valores ----------------------------------------------------------- */

static const struct { const char *name; u32 rgb; } named_colors[] = {
    {"black",0x000000},{"white",0xFFFFFF},{"red",0xFF0000},{"green",0x008000},{"blue",0x0000FF},
    {"yellow",0xFFFF00},{"orange",0xFFA500},{"purple",0x800080},{"gray",0x808080},{"grey",0x808080},
    {"silver",0xC0C0C0},{"navy",0x000080},{"teal",0x008080},{"maroon",0x800000},{"olive",0x808000},
    {"lime",0x00FF00},{"aqua",0x00FFFF},{"fuchsia",0xFF00FF},{"darkgray",0xA9A9A9},{"lightgray",0xD3D3D3},
    {"gold",0xFFD700},{"pink",0xFFC0CB},{"brown",0xA52A2A},{"crimson",0xDC143C},{"darkblue",0x00008B},
    {"darkgreen",0x006400},{"darkred",0x8B0000},{"whitesmoke",0xF5F5F5},{"gainsboro",0xDCDCDC},
    {"beige",0xF5F5DC},{"ivory",0xFFFFF0},{"coral",0xFF7F50},{"tomato",0xFF6347},{"salmon",0xFA8072},
    {"indigo",0x4B0082},{"violet",0xEE82EE},{"cyan",0x00FFFF},{"magenta",0xFF00FF},{"skyblue",0x87CEEB},
    {"steelblue",0x4682B4},{"royalblue",0x4169E1},{"slategray",0x708090},{"dimgray",0x696969},
    {"lightblue",0xADD8E6},{"lightgreen",0x90EE90},{"lightyellow",0xFFFFE0},{"orangered",0xFF4500},
    {"darkorange",0xFF8C00},{"seagreen",0x2E8B57},{"forestgreen",0x228B22},{"midnightblue",0x191970},
    {"darkslategray",0x2F4F4F},{"lavender",0xE6E6FA},{"linen",0xFAF0E6},{"wheat",0xF5DEB3},
    {"tan",0xD2B48C},{"chocolate",0xD2691E},{"firebrick",0xB22222},{"khaki",0xF0E68C},
    {"aliceblue",0xF0F8FF},{"honeydew",0xF0FFF0},{"mintcream",0xF5FFFA},{"snow",0xFFFAFA},
    {"ghostwhite",0xF8F8FF},{"cornsilk",0xFFF8DC},{"darkviolet",0x9400D3},{"turquoise",0x40E0D0},
};

/* Numero con decimales en una cadena, en centesimas. */
static int num100(const char **pp) {
    const char *p = *pp;
    int neg = 0, v = 0, frac = 0, fd = 0;
    while (*p == ' ') p++;
    if (*p == '-') { neg = 1; p++; } else if (*p == '+') p++;
    while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') { if (fd < 2) { frac = frac * 10 + (*p - '0'); fd++; } p++; }
    }
    while (fd < 2) { frac *= 10; fd++; }
    *pp = p;
    int r = v * 100 + frac;
    return neg ? -r : r;
}

static u32 blend(u32 fg, u32 bg, int alpha256) {
    u32 r = (((fg >> 16) & 0xFF) * (u32)alpha256 + ((bg >> 16) & 0xFF) * (u32)(256 - alpha256)) >> 8;
    u32 g = (((fg >> 8) & 0xFF) * (u32)alpha256 + ((bg >> 8) & 0xFF) * (u32)(256 - alpha256)) >> 8;
    u32 b = ((fg & 0xFF) * (u32)alpha256 + (bg & 0xFF) * (u32)(256 - alpha256)) >> 8;
    return r << 16 | g << 8 | b;
}

static int hexv(int c) {
    c = lc(c);
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static u32 hsl_rgb(int h, int s, int l) {           /* h 0-360, s/l 0-100 */
    int c = (100 - (2 * l - 100 < 0 ? 100 - 2 * l : 2 * l - 100)) * s / 100;  /* croma 0-100 */
    int hp = h % 360;
    int x = c * (60 - ((hp % 120) - 60 < 0 ? 60 - hp % 120 : hp % 120 - 60)) / 60;
    int r = 0, g = 0, b = 0;
    if (hp < 60) { r = c; g = x; } else if (hp < 120) { r = x; g = c; }
    else if (hp < 180) { g = c; b = x; } else if (hp < 240) { g = x; b = c; }
    else if (hp < 300) { r = x; b = c; } else { r = c; b = x; }
    int m = l - c / 2;
    r = (r + m) * 255 / 100; g = (g + m) * 255 / 100; b = (b + m) * 255 / 100;
    if (r < 0) r = 0; if (g < 0) g = 0; if (b < 0) b = 0;
    if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
    return (u32)(r << 16 | g << 8 | b);
}

/* Busca el primer color dentro de un valor. Devuelve 1 si lo encuentra;
 * *transparent se pone a 1 si es (casi) transparente. */
static int parse_color(const char *s, u32 base, u32 current, u32 *out, int *transparent) {
    *transparent = 0;
    for (const char *p = s; *p; p++) {
        if (*p == '#') {
            int h[8], n = 0;
            const char *q = p + 1;
            while (n < 8 && hexv((u8)*q) >= 0) h[n++] = hexv((u8)*q++);
            if (n == 3 || n == 4) {
                *out = (u32)(h[0] * 17) << 16 | (u32)(h[1] * 17) << 8 | (u32)(h[2] * 17);
                if (n == 4) { int a = h[3] * 17; if (a < 40) *transparent = 1; else *out = blend(*out, base, a + 1); }
                return 1;
            }
            if (n == 6 || n == 8) {
                *out = (u32)(h[0] << 20 | h[1] << 16 | h[2] << 12 | h[3] << 8 | h[4] << 4 | h[5]);
                if (n == 8) { int a = h[6] * 16 + h[7]; if (a < 40) *transparent = 1; else *out = blend(*out, base, a + 1); }
                return 1;
            }
            continue;
        }
        if ((starts_ci(p, p + 4, "rgb") || starts_ci(p, p + 4, "hsl")) && (p == s || !is_alnum((u8)p[-1]))) {
            int hsl = lc((u8)*p) == 'h';
            const char *q = p;
            while (*q && *q != '(') q++;
            if (!*q) return 0;
            q++;
            int v[4] = { 0, 0, 0, 100 }, k = 0;
            while (*q && *q != ')' && k < 4) {
                while (*q == ' ' || *q == ',' || *q == '/') q++;
                if (*q == ')' || !*q) break;
                int x = num100(&q);
                /* num100 da centesimas: "0.5" -> 50 y "50%" -> 5000 */
                if (*q == '%') { q++; if (k == 3) x /= 100; else if (!hsl) x = x * 255 / 100; }
                if (lc((u8)*q) == 'd') while (is_alnum((u8)*q)) q++;   /* "deg" */
                v[k++] = x;
            }
            u32 col;
            if (hsl) col = hsl_rgb(v[0] / 100, v[1] / 100, v[2] / 100);
            else {
                int r = v[0] / 100, g = v[1] / 100, b = v[2] / 100;
                if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
                if (r < 0) r = 0; if (g < 0) g = 0; if (b < 0) b = 0;
                col = (u32)(r << 16 | g << 8 | b);
            }
            int a = k == 4 ? v[3] : 100;                 /* alfa en centesimas (0..100) */
            if (a > 100) a = 100;
            if (a < 15) *transparent = 1;
            else if (a < 100) col = blend(col, base, a * 256 / 100);
            *out = col;
            return 1;
        }
        if ((p == s || !is_alnum((u8)p[-1])) && lc((u8)*p) >= 'a' && lc((u8)*p) <= 'z') {
            const char *q = p;
            while (is_alnum((u8)*q)) q++;
            int nl = (int)(q - p);
            if (nl == 11 && !strncasecmp(p, "transparent", 11)) { *transparent = 1; return 1; }
            if (nl == 12 && !strncasecmp(p, "currentcolor", 12)) { *out = current; return 1; }
            for (u32 i = 0; i < sizeof named_colors / sizeof named_colors[0]; i++)
                if ((int)strlen(named_colors[i].name) == nl && !strncasecmp(p, named_colors[i].name, (size_t)nl)) {
                    *out = named_colors[i].rgb;
                    return 1;
                }
            p = q - 1;
        }
    }
    return 0;
}

/* Longitud en pixeles. 'ref' es la referencia para porcentajes. */
static int parse_len(const char *s, int ref, int *ok) {
    const char *p = s;
    *ok = 0;
    while (*p == ' ') p++;
    if (starts_ci(p, p + 6, "clamp(") || starts_ci(p, p + 4, "min(") || starts_ci(p, p + 4, "max(")) {
        int which = lc((u8)p[1]) == 'l' ? 0 : lc((u8)p[1]) == 'i' ? 1 : 2;
        while (*p && *p != '(') p++;
        p++;
        int v[3], k = 0, o;
        while (*p && *p != ')' && k < 3) {
            v[k] = parse_len(p, ref, &o);
            if (!o) return 0;
            k++;
            int depth = 0;
            while (*p && ((*p != ',' && *p != ')') || depth)) {
                if (*p == '(') depth++; else if (*p == ')') depth--;
                p++;
            }
            if (*p == ',') p++;
        }
        if (!k) return 0;
        *ok = 1;
        if (which == 0 && k == 3) return v[1] < v[0] ? v[0] : v[1] > v[2] ? v[2] : v[1];
        int r = v[0];
        for (int i = 1; i < k; i++) r = which == 1 ? (v[i] < r ? v[i] : r) : (v[i] > r ? v[i] : r);
        return r;
    }
    if (starts_ci(p, p + 5, "calc(")) p += 5;
    if (!((*p >= '0' && *p <= '9') || *p == '.' || *p == '-')) return 0;
    int v = num100(&p);
    *ok = 1;
    if (starts_ci(p, p + 2, "px") || !*p || *p == ' ' || *p == ')' || *p == ',') return v / 100;
    if (starts_ci(p, p + 3, "rem") || starts_ci(p, p + 2, "em") || starts_ci(p, p + 2, "ch"))
        return v * 16 / 100;
    if (starts_ci(p, p + 2, "ex")) return v * 8 / 100;
    if (starts_ci(p, p + 2, "pt")) return v * 4 / 300;
    if (*p == '%') return ref * v / 10000;
    if (starts_ci(p, p + 2, "vw")) return css_width * v / 10000;
    if (starts_ci(p, p + 2, "vh")) return 600 * v / 10000;
    return v / 100;
}

static int is_auto(const char *v) { return starts_ci(v, v + 4, "auto"); }

/* Aplica una lista de cuatro valores tipo "10px 20px" a arr[4]. */
static void apply_box(short *arr, const char *v, int ref) {
    int vals[4], n = 0, ok;
    const char *p = v;
    while (*p && n < 4) {
        while (*p == ' ') p++;
        if (!*p) break;
        vals[n] = is_auto(p) ? 0 : parse_len(p, ref, &ok);
        n++;
        int depth = 0;
        while (*p && (*p != ' ' || depth)) { if (*p == '(') depth++; else if (*p == ')') depth--; p++; }
    }
    if (!n) return;
    if (n == 1) vals[1] = vals[2] = vals[3] = vals[0];
    else if (n == 2) { vals[2] = vals[0]; vals[3] = vals[1]; }
    else if (n == 3) vals[3] = vals[1];
    for (int i = 0; i < 4; i++) arr[i] = (short)(vals[i] < 0 ? 0 : vals[i] > 400 ? 400 : vals[i]);
}

static void apply_border(struct style *s, int side, const char *v, u32 base) {
    int w = 1, ok;
    const char *p = v;
    if (starts_ci(p, p + 4, "none") || starts_ci(p, p + 1, "0")) {
        if (side < 0) for (int i = 0; i < 4; i++) s->bw[i] = 0; else s->bw[side] = 0;
        return;
    }
    for (; *p; p++) {
        if ((*p >= '0' && *p <= '9') && (p == v || p[-1] == ' ')) { w = parse_len(p, 0, &ok); break; }
        if (starts_ci(p, p + 4, "thin")) { w = 1; break; }
        if (starts_ci(p, p + 5, "thick")) { w = 3; break; }
    }
    if (w > 6) w = 6;
    u32 col = 0x999999;
    int tr;
    if (!parse_color(v, base, s->color, &col, &tr)) col = s->color;
    if (tr) w = 0;
    for (int i = 0; i < 4; i++)
        if (side < 0 || side == i) { s->bw[i] = (u8)w; s->bc[i] = col; }
}

static void apply_font_size(struct style *s, const char *v) {
    int ok, px;
    if (starts_ci(v, v + 6, "xx-lar") || starts_ci(v, v + 6, "xxx-la")) px = 32;
    else if (starts_ci(v, v + 6, "x-larg")) px = 24;
    else if (starts_ci(v, v + 5, "large")) px = 18;
    else if (starts_ci(v, v + 6, "larger")) px = s->scale == 2 ? 36 : 20;
    else px = parse_len(v, s->scale == 2 ? 32 : 16, &ok);
    if (starts_ci(v, v + 5, "small") || starts_ci(v, v + 7, "x-small")) px = 13;
    if (px > 0) s->scale = px >= 26 ? 2 : 1;
}

/* Sustituye var(--x, alternativa) por su valor. */
static const char *resolve_vars(struct html_doc *d, const char *v, char *buf, int bufsz) {
    if (!find_ci(v, v + strlen(v), "var(")[0]) return v;
    for (int pass = 0; pass < 3; pass++) {
        const char *src = pass ? buf : v;
        const char *at = find_ci(src, src + strlen(src), "var(");
        if (!*at) break;
        char tmp[256];
        int n = 0;
        const char *p = src;
        while (*p && n < 250) {
            if (starts_ci(p, p + 4, "var(")) {
                const char *q = p + 4;
                while (*q == ' ') q++;
                const char *nm = q;
                while (*q && *q != ',' && *q != ')') q++;
                int nl = (int)(q - nm);
                while (nl && nm[nl - 1] == ' ') nl--;
                const char *fb = NULL; int fbl = 0;
                if (*q == ',') {
                    fb = ++q;
                    int depth = 0;
                    while (*q && (*q != ')' || depth)) { if (*q == '(') depth++; else if (*q == ')') depth--; q++; }
                    fbl = (int)(q - fb);
                }
                if (*q == ')') q++;
                const char *val = NULL;
                for (int i = 0; i < d->nvars; i++)
                    if ((int)strlen(d->vars[i].name) == nl && !memcmp(d->vars[i].name, nm, (size_t)nl)) {
                        val = d->vars[i].value; break;
                    }
                if (val) { while (*val && n < 250) tmp[n++] = *val++; }
                else if (fb) { for (int i = 0; i < fbl && n < 250; i++) tmp[n++] = fb[i]; }
                p = q;
            } else {
                tmp[n++] = *p++;
            }
        }
        tmp[n] = 0;
        strlcpy(buf, tmp, (size_t)bufsz);
    }
    return buf;
}

static void apply_decls(struct html_doc *d, struct node *n, struct style *s, const char *decls,
                        u32 parent_bg, int ref_w) {
    const char *p = decls;
    char name[40], valbuf[256], resolved[256];
    while (*p) {
        while (*p == ';' || is_ws((u8)*p)) p++;
        if (!*p) break;
        int nl = 0;
        while (*p && *p != ':' && *p != ';' && nl < 39) name[nl++] = (char)lc((u8)*p++);
        name[nl] = 0;
        while (*p && *p != ':' && *p != ';') p++;
        if (*p != ':') continue;
        p++;
        int vl = 0, depth = 0;
        while (*p && (*p != ';' || depth) && vl < 255) {
            if (*p == '(') depth++; else if (*p == ')') depth--;
            valbuf[vl++] = *p++;
        }
        while (*p && *p != ';') p++;
        valbuf[vl] = 0;
        char *nm = trim(name);
        char *imp = (char *)find_ci(valbuf, valbuf + vl, "!important");
        *imp = 0;
        const char *v = resolve_vars(d, trim(valbuf), resolved, sizeof resolved);
        int ok, tr;
        u32 col;
        u32 base = s->has_bg ? s->bg : parent_bg;

        if (nm[0] == '-' && nm[1] == '-') continue;
        if (!strcmp(nm, "color")) {
            if (parse_color(v, base, s->color, &col, &tr) && !tr) s->color = col;
        } else if (!strcmp(nm, "background-color") || !strcmp(nm, "background")) {
            if (starts_ci(v, v + 4, "none")) { s->has_bg = 0; continue; }
            if (parse_color(v, parent_bg, s->color, &col, &tr)) {
                if (tr) s->has_bg = 0; else { s->bg = col; s->has_bg = 1; }
            }
        } else if (!strcmp(nm, "font-size")) {
            apply_font_size(s, v);
        } else if (!strcmp(nm, "font-weight")) {
            s->bold = starts_ci(v, v + 4, "bold") || atoi(v) >= 600;
        } else if (!strcmp(nm, "font")) {
            if (find_ci(v, v + strlen(v), "bold")[0] || find_ci(v, v + strlen(v), "700")[0]) s->bold = 1;
        } else if (!strcmp(nm, "font-family")) {
            s->mono = find_ci(v, v + strlen(v), "mono")[0] || find_ci(v, v + strlen(v), "courier")[0];
        } else if (!strcmp(nm, "font-style")) {
            s->italic = starts_ci(v, v + 6, "italic");
        } else if (!strcmp(nm, "text-decoration") || !strcmp(nm, "text-decoration-line")) {
            s->underline = find_ci(v, v + strlen(v), "underline")[0] != 0;
            s->strike = find_ci(v, v + strlen(v), "line-through")[0] != 0;
        } else if (!strcmp(nm, "text-align")) {
            s->align = starts_ci(v, v + 6, "center") ? A_CENTER :
                       (starts_ci(v, v + 5, "right") || starts_ci(v, v + 3, "end")) ? A_RIGHT : A_LEFT;
        } else if (!strcmp(nm, "text-transform")) {
            s->upper = starts_ci(v, v + 9, "uppercase");
        } else if (!strcmp(nm, "display")) {
            if (starts_ci(v, v + 4, "none")) s->display = D_NONE;
            else if (starts_ci(v, v + 9, "list-item")) s->display = D_LISTITEM;
            else if (starts_ci(v, v + 6, "inline") && !starts_ci(v, v + 11, "inline-flex") &&
                     !starts_ci(v, v + 11, "inline-grid") && !starts_ci(v, v + 12, "inline-table"))
                s->display = D_INLINE;
            else if (starts_ci(v, v + 9, "table-row") && !starts_ci(v, v + 15, "table-row-group")) s->display = D_ROW;
            else if (starts_ci(v, v + 10, "table-cell")) s->display = D_CELL;
            else if (starts_ci(v, v + 5, "table")) s->display = n->tag == T_TABLE ? D_TABLE : D_BLOCK;
            else if (starts_ci(v, v + 8, "contents")) s->display = D_INLINE;
            else s->display = D_BLOCK;                      /* block, flex, grid... */
        } else if (!strcmp(nm, "visibility")) {
            s->hidden = starts_ci(v, v + 6, "hidden");
        } else if (!strcmp(nm, "white-space")) {
            s->pre = starts_ci(v, v + 3, "pre");
        } else if (!strcmp(nm, "list-style") || !strcmp(nm, "list-style-type")) {
            if (starts_ci(v, v + 4, "none")) s->list = 0;
            else if (find_ci(v, v + strlen(v), "decimal")[0]) s->list = 2;
            else s->list = 1;
        } else if (!strcmp(nm, "margin")) {
            apply_box(s->m, v, ref_w);
            const char *sp = v; int words = 0, autos = 0;
            while (*sp) { while (*sp == ' ') sp++; if (!*sp) break; words++; if (is_auto(sp)) autos++; while (*sp && *sp != ' ') sp++; }
            s->center_auto = (words <= 2 && autos && (words == 1 || !is_auto(v))) ? 1 : (words == 4 ? 0 : s->center_auto);
            if (words == 1 && autos) s->center_auto = 1;
        } else if (!strncmp(nm, "margin-", 7)) {
            int side = !strcmp(nm + 7, "top") ? 0 : !strcmp(nm + 7, "right") ? 1 :
                       !strcmp(nm + 7, "bottom") ? 2 : !strcmp(nm + 7, "left") ? 3 : -1;
            if (side >= 0) { int x = is_auto(v) ? 0 : parse_len(v, ref_w, &ok); s->m[side] = (short)(x < 0 ? 0 : x > 400 ? 400 : x); }
            if ((side == 1 || side == 3) && is_auto(v)) s->center_auto = 1;
        } else if (!strcmp(nm, "padding")) {
            apply_box(s->p, v, ref_w);
        } else if (!strncmp(nm, "padding-", 8)) {
            int side = !strcmp(nm + 8, "top") ? 0 : !strcmp(nm + 8, "right") ? 1 :
                       !strcmp(nm + 8, "bottom") ? 2 : !strcmp(nm + 8, "left") ? 3 : -1;
            if (side >= 0) { int x = parse_len(v, ref_w, &ok); s->p[side] = (short)(x < 0 ? 0 : x > 400 ? 400 : x); }
        } else if (!strcmp(nm, "border")) {
            apply_border(s, -1, v, base);
        } else if (!strcmp(nm, "border-top")) apply_border(s, 0, v, base);
        else if (!strcmp(nm, "border-right")) apply_border(s, 1, v, base);
        else if (!strcmp(nm, "border-bottom")) apply_border(s, 2, v, base);
        else if (!strcmp(nm, "border-left")) apply_border(s, 3, v, base);
        else if (!strcmp(nm, "border-color")) {
            if (parse_color(v, base, s->color, &col, &tr)) for (int i = 0; i < 4; i++) s->bc[i] = col;
        } else if (!strcmp(nm, "border-width")) {
            int x = parse_len(v, 0, &ok);
            if (ok) for (int i = 0; i < 4; i++) s->bw[i] = (u8)(x > 6 ? 6 : x);
        } else if (!strcmp(nm, "width")) {
            if (is_auto(v)) { s->width = 0; s->width_pct = 0; }
            else if (find_ci(v, v + strlen(v), "%")[0] && !find_ci(v, v + strlen(v), "(")[0]) {
                const char *q = v; s->width_pct = num100(&q) / 100; s->width = 0;
            } else { int x = parse_len(v, ref_w, &ok); if (ok && x > 0) { s->width = x; s->width_pct = 0; } }
        } else if (!strcmp(nm, "max-width")) {
            int x = parse_len(v, ref_w, &ok);
            if (ok && x > 0) s->max_width = x;
        } else if (!strcmp(nm, "min-height") || !strcmp(nm, "height")) {
            int x = parse_len(v, 0, &ok);
            if (ok && x > 0 && x < 300 && !find_ci(v, v + strlen(v), "%")[0] && !find_ci(v, v + strlen(v), "vh")[0])
                s->min_height = x;
        }
    }
}

/* Estilos por defecto de cada etiqueta (la "hoja del navegador"). */
static void ua_style(struct node *n, struct style *s) {
    switch (n->tag) {
    case T_HTML: case T_BODY: case T_DIV: case T_HEADER: case T_FOOTER: case T_NAV:
    case T_MAIN: case T_SECTION: case T_ARTICLE: case T_ASIDE: case T_FIGURE:
    case T_FIGCAPTION: case T_FORM: case T_ADDRESS: case T_DETAILS: case T_HGROUP:
    case T_DIALOG: case T_LEGEND: case T_PICTURE:
        s->display = D_BLOCK; break;
    case T_CENTER: s->display = D_BLOCK; s->align = A_CENTER; break;
    case T_P: s->display = D_BLOCK; s->m[0] = s->m[2] = 12; break;
    case T_H1: s->display = D_BLOCK; s->scale = 2; s->bold = 1; s->m[0] = 18; s->m[2] = 12; break;
    case T_H2: s->display = D_BLOCK; s->bold = 1; s->m[0] = 18; s->m[2] = 10; break;
    case T_H3: case T_H4: case T_H5: case T_H6:
        s->display = D_BLOCK; s->bold = 1; s->m[0] = 14; s->m[2] = 8; break;
    case T_UL: s->display = D_BLOCK; s->list = 1; s->p[3] = 26; s->m[0] = s->m[2] = 10; break;
    case T_OL: s->display = D_BLOCK; s->list = 2; s->p[3] = 30; s->m[0] = s->m[2] = 10; break;
    case T_LI: s->display = D_LISTITEM; s->m[0] = 2; break;
    case T_DL: s->display = D_BLOCK; s->m[0] = s->m[2] = 10; break;
    case T_DT: s->display = D_BLOCK; s->bold = 1; break;
    case T_DD: s->display = D_BLOCK; s->m[3] = 30; break;
    case T_BLOCKQUOTE:
        s->display = D_BLOCK; s->m[0] = s->m[2] = 10; s->m[3] = 20; s->p[3] = 10;
        s->bw[3] = 3; s->bc[3] = 0xBBBBBB; break;
    case T_PRE:
        s->display = D_BLOCK; s->pre = 1; s->mono = 1; s->m[0] = s->m[2] = 10;
        s->p[0] = s->p[1] = s->p[2] = s->p[3] = 8; s->has_bg = 1; s->bg = 0xF2F3F5;
        if (s->color == 0xFFFFFF) s->color = 0x1E2228;
        break;
    case T_HR: s->display = D_BLOCK; s->m[0] = s->m[2] = 10; break;
    case T_TABLE: s->display = D_TABLE; s->m[0] = s->m[2] = 10; break;
    case T_THEAD: case T_TBODY: case T_TFOOT: s->display = D_ROWGROUP; break;
    case T_TR: s->display = D_ROW; break;
    case T_TD: s->display = D_CELL; s->p[0] = s->p[2] = 3; s->p[1] = s->p[3] = 6; break;
    case T_TH: s->display = D_CELL; s->bold = 1; s->p[0] = s->p[2] = 3; s->p[1] = s->p[3] = 6; break;
    case T_CAPTION: s->display = D_BLOCK; s->align = A_CENTER; s->bold = 1; break;
    case T_A: s->color = 0x1A5FB4; s->underline = 1; break;
    case T_B: case T_STRONG: s->bold = 1; break;
    case T_I: case T_EM: case T_CITE: case T_VAR: s->italic = 1; break;
    case T_U: case T_INS: s->underline = 1; break;
    case T_S: case T_STRIKE: case T_DEL: s->strike = 1; break;
    case T_CODE: case T_KBD: case T_SAMP: case T_TT: s->mono = 1; break;
    case T_MARK: s->has_bg = 1; s->bg = 0xFFF176; s->color = 0x111111; break;
    case T_SUMMARY: s->display = D_BLOCK; s->bold = 1; break;
    case T_FIELDSET:
        s->display = D_BLOCK; s->m[0] = s->m[2] = 8; s->p[0] = s->p[1] = s->p[2] = s->p[3] = 8;
        for (int i = 0; i < 4; i++) { s->bw[i] = 1; s->bc[i] = 0xAAAAAA; }
        break;
    case T_HEAD: case T_TITLE: case T_STYLE: case T_SCRIPT: case T_META: case T_LINK:
    case T_BASE: case T_TEMPLATE: case T_SVG: case T_CANVAS:
    case T_IFRAME: case T_VIDEO: case T_AUDIO: case T_OPTION: case T_OPTGROUP:
    case T_COL: case T_COLGROUP: case T_SOURCE: case T_TRACK: case T_PARAM:
    case T_EMBED: case T_OBJECT: case T_MAP: case T_AREA: case T_WBR:
        s->display = D_NONE; break;
    }
}

static int apply_presentational(struct node *n, struct style *s, u32 parent_bg) {
    u32 col; int tr, ok;
    if (n->align) {
        if (!strcasecmp(n->align, "center")) s->align = A_CENTER;
        else if (!strcasecmp(n->align, "right")) s->align = A_RIGHT;
    }
    if (n->bgcolor && parse_color(n->bgcolor, parent_bg, s->color, &col, &tr) && !tr) { s->bg = col; s->has_bg = 1; }
    if (n->color && n->tag == T_FONT && parse_color(n->color, parent_bg, s->color, &col, &tr) && !tr) s->color = col;
    if (n->width && n->tag != T_IMG) {
        if (find_ci(n->width, n->width + strlen(n->width), "%")[0]) s->width_pct = atoi(n->width);
        else { int x = parse_len(n->width, 0, &ok); if (ok && x > 0) s->width = x; }
    }
    if (n->hidden_attr) s->display = D_NONE;
    return 0;
}

struct match { int spec, order; char *decls; };

static void compute_styles(struct html_doc *d, struct node *n, const struct style *parent, u32 parent_bg) {
    for (; n; n = n->next) {
        if (n->type == N_TEXT) { n->s = *parent; n->s.display = D_INLINE; continue; }
        struct style s;
        memset(&s, 0, sizeof s);
        /* Heredadas */
        s.color = parent->color; s.bold = parent->bold; s.italic = parent->italic;
        s.underline = parent->underline; s.strike = parent->strike; s.scale = parent->scale;
        s.align = parent->align; s.pre = parent->pre; s.list = parent->list;
        s.upper = parent->upper; s.hidden = parent->hidden; s.mono = parent->mono;
        s.display = D_INLINE;
        if (n->tag != T_UL && n->tag != T_OL && n->tag != T_LI) s.list = parent->list;
        ua_style(n, &s);
        apply_presentational(n, &s, parent_bg);

        /* Reglas CSS que encajan, por especificidad y orden */
        struct match ms[48];
        int nm = 0;
        for (int i = 0; i < d->nrules && nm < 48; i++) {
            struct rule *r = &d->rules[i];
            if (match_sel(n, r->sel)) {
                int k = nm++;
                while (k > 0 && (ms[k - 1].spec > r->spec ||
                       (ms[k - 1].spec == r->spec && ms[k - 1].order > r->order))) {
                    ms[k] = ms[k - 1]; k--;
                }
                ms[k].spec = r->spec; ms[k].order = r->order; ms[k].decls = r->decls;
            }
        }
        int ref_w = d->width;
        for (int i = 0; i < nm; i++) apply_decls(d, n, &s, ms[i].decls, parent_bg, ref_w);
        if (n->style) apply_decls(d, n, &s, n->style, parent_bg, ref_w);
        if (s.hidden) s.display = D_NONE;
        n->s = s;
        if (s.display != D_NONE && n->first)
            compute_styles(d, n->first, &n->s, s.has_bg ? s.bg : parent_bg);
    }
}

static int is_blocky(int disp) { return disp != D_INLINE && disp != D_NONE; }

/* Un elemento en linea que contiene bloques se trata como bloque. */
static int promote_blocks(struct node *n) {
    int any = 0;
    for (struct node *c = n->first; c; c = c->next) {
        if (c->type != N_ELEM || c->s.display == D_NONE) continue;
        if (promote_blocks(c) && c->s.display == D_INLINE) c->s.display = D_BLOCK;
        if (is_blocky(c->s.display)) any = 1;
    }
    return any;
}

/* ====================================================================== */
/* Maquetacion                                                             */

static struct op *new_op(struct html_doc *d) {
    if (d->nops == d->capops) {
        int cap = d->capops ? d->capops * 2 : 256;
        struct op *o = kmalloc((size_t)cap * sizeof *o);
        if (!o) return NULL;
        if (d->ops) { memcpy(o, d->ops, (size_t)d->nops * sizeof *o); kfree(d->ops); }
        d->ops = o;
        d->capops = cap;
    }
    struct op *o = &d->ops[d->nops++];
    memset(o, 0, sizeof *o);
    return o;
}

static int rect_op(struct html_doc *d, int x, int y, int w, int h, u32 color) {
    struct op *o = new_op(d);
    if (!o) return -1;
    o->type = OP_RECT; o->x = x; o->y = y; o->w = w; o->h = h; o->color = color;
    return d->nops - 1;
}

/* Estado de una linea en construccion */
struct line {
    int x0, w, y;            /* origen y ancho del area de texto */
    int cx;                  /* posicion x actual */
    int first_op;            /* primera operacion de la linea actual */
    int maxh;                /* altura del elemento mas alto de la linea */
    int align;
    int space;               /* hay un espacio pendiente */
    int any;                 /* la linea tiene contenido */
};

static void line_start(struct line *l, int x0, int w, int y, int align, struct html_doc *d) {
    l->x0 = x0; l->w = w < 16 ? 16 : w; l->y = y; l->cx = x0;
    l->first_op = d->nops; l->maxh = 0; l->align = align; l->space = 0; l->any = 0;
}

static void line_break(struct line *l, struct html_doc *d, int force_empty) {
    if (!l->any) {
        if (force_empty) l->y += 18;
        l->first_op = d->nops; l->cx = l->x0; l->space = 0;
        return;
    }
    int lh = l->maxh + 4;
    int shift = 0, used = l->cx - l->x0;
    if (l->align == A_CENTER) shift = (l->w - used) / 2;
    else if (l->align == A_RIGHT) shift = l->w - used;
    if (shift < 0) shift = 0;
    for (int i = l->first_op; i < d->nops; i++) {
        struct op *o = &d->ops[i];
        o->y = l->y + (l->maxh - o->ih) + o->dy;
        o->x += shift;
    }
    l->y += lh;
    l->cx = l->x0; l->first_op = d->nops; l->maxh = 0; l->space = 0; l->any = 0;
}

static void emit_text(struct html_doc *d, struct line *l, const char *s, int len,
                      const struct style *st, char *href) {
    int cw = 8 * st->scale;
    while (len > 0) {
        int need = len * cw + (l->space && l->any ? cw : 0);
        if (l->any && l->cx + need > l->x0 + l->w) line_break(l, d, 0);
        if (l->space && l->any) l->cx += cw;
        l->space = 0;
        int fit = (l->x0 + l->w - l->cx) / cw;
        if (fit < 1) fit = 1;
        int n = len < fit ? len : fit;
        struct op *o = new_op(d);
        if (!o) return;
        o->type = OP_TEXT;
        o->x = l->cx; o->dy = 0; o->ih = 16 * st->scale;
        o->w = n * cw; o->h = 16 * st->scale;
        o->scale = st->scale; o->bold = st->bold; o->underline = st->underline;
        o->strike = st->strike; o->color = st->color; o->mono = st->mono;
        o->bg = st->has_bg ? st->bg : 0xFF000000;
        o->str = (char *)s; o->len = n;
        o->href = href;
        if (st->upper) {
            char *u = aalloc(&d->a, (u32)n + 1);
            if (u) {
                for (int i = 0; i < n; i++) u[i] = s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i];
                o->str = u;
            }
        }
        if (o->ih > l->maxh) l->maxh = o->ih;
        l->cx += n * cw;
        l->any = 1;
        s += n; len -= n;
        if (len > 0) line_break(l, d, 0);
    }
}

/* Caja "atomica" en linea (imagen, boton, campo de formulario). */
static void emit_box(struct html_doc *d, struct line *l, struct node *n, char *href) {
    const struct style *st = &n->s;
    char label[80];
    int w, h = 22, kind = 0;               /* 0 caja, 1 boton, 2 casilla, 3 imagen */
    label[0] = 0;
    if (n->tag == T_IMG) {
        int ok; u32 *ipx; int iw, ih;
        if (n->src && html_img_get && html_img_get(n->src, &ipx, &iw, &ih) && iw > 0 && ih > 0) {
            int aw = n->width ? parse_len(n->width, l->w, &ok) : 0, ah = n->height ? parse_len(n->height, 0, &ok) : 0;
            if (aw > 0 && ah <= 0) ah = ih * aw / iw; else if (ah > 0 && aw <= 0) aw = iw * ah / ih; else if (aw <= 0) { aw = iw; ah = ih; }
            if (aw > l->w) { ah = ah * l->w / aw; aw = l->w; } if (ah > 4000) ah = 4000; if (aw < 1) aw = 1; if (ah < 1) ah = 1;
            if (l->any && l->cx + aw + (l->space ? 8 : 0) > l->x0 + l->w) line_break(l, d, 0);
            if (l->space && l->any) l->cx += 8; l->space = 0;
            struct op *io = new_op(d); if (io) { io->type = OP_IMG; io->x = l->cx; io->w = aw; io->h = ah; io->ih = ah; io->px = ipx; io->iw = iw; io->ih2 = ih; io->href = href; io->bg = 0xFF000000; }
            l->cx += aw; l->any = 1; if (ah > l->maxh) l->maxh = ah; return;
        }
        if (n->src && d->nimgs < 96) { int dup = 0; for (int k = 0; k < d->nimgs; k++) if (!strcmp(d->imgs[k], n->src)) dup = 1; if (!dup) d->imgs[d->nimgs++] = n->src; }
        w = n->width ? parse_len(n->width, l->w, &ok) : 0;
        h = n->height ? parse_len(n->height, 0, &ok) : 0;
        if (w <= 0) w = 140;
        if (h <= 0) h = 70;
        if (w > l->w) { h = h * l->w / w; w = l->w; }
        if (h > 160) h = 160;
        if (h < 22) h = 22;
        ksnprintf(label, sizeof label, "[%s]", n->alt && *n->alt ? n->alt : "imagen");
        kind = 3;
    } else if (n->tag == T_BUTTON || (n->tag == T_INPUT && n->itype &&
               (!strcasecmp(n->itype, "submit") || !strcasecmp(n->itype, "button") || !strcasecmp(n->itype, "reset")))) {
        if (n->tag == T_BUTTON) {
            int k = 0;
            for (struct node *c = n->first; c; c = c->next ? c->next : NULL) {
                if (c->type == N_TEXT)
                    for (int i = 0; i < c->tlen && k < 60; i++) {
                        char ch = c->text[i];
                        if (is_ws((u8)ch)) { if (k && label[k - 1] != ' ') label[k++] = ' '; }
                        else label[k++] = ch;
                    }
                else if (c->first && c->first->type == N_TEXT)
                    for (int i = 0; i < c->first->tlen && k < 60; i++) label[k++] = c->first->text[i];
            }
            label[k] = 0;
            strlcpy(label, trim(label), sizeof label);
        } else {
            strlcpy(label, n->value ? n->value : "Enviar", sizeof label);
        }
        if (!label[0]) strlcpy(label, "Boton", sizeof label);
        w = (int)strlen(label) * 8 + 20;
        kind = 1;
    } else if (n->tag == T_INPUT && n->itype && (!strcasecmp(n->itype, "checkbox") || !strcasecmp(n->itype, "radio"))) {
        w = 14; h = 14; kind = 2;
    } else if (n->tag == T_INPUT && n->itype && !strcasecmp(n->itype, "hidden")) {
        return;
    } else if (n->tag == T_SELECT) {
        int count;
        nth_option(n, 0, &count);
        int widest = 0;
        for (int k = 0; k < count; k++) {
            int lw = (int)strlen(option_text(nth_option(n, k, NULL)));
            if (lw > widest) widest = lw;
        }
        strlcpy(label, trim(astrndup(&d->a, option_text(nth_option(n, n->selidx, NULL)), 60)), 60);
        w = (widest > 40 ? 40 : widest) * 8 + 34;
        if (w < 80) w = 80;
    } else if (n->tag == T_TEXTAREA) {
        w = 300; h = 64;
        strlcpy(label, n->edit && *n->edit ? n->edit : n->placeholder ? n->placeholder : "", sizeof label);
    } else {                                     /* input de texto, numero, fecha... */
        w = n->itype && !strcasecmp(n->itype, "number") ? 120 : 170;
        if (n->width) { int ok, x = parse_len(n->width, l->w, &ok); if (ok && x > 30) w = x; }
        strlcpy(label, n->edit && *n->edit ? n->edit : n->placeholder ? n->placeholder : "", sizeof label);
    }
    int focused = d->focus == n;
    if (w > l->w) w = l->w;
    if (l->any && l->cx + w + (l->space ? 8 : 0) > l->x0 + l->w) line_break(l, d, 0);
    if (l->space && l->any) l->cx += 8;
    l->space = 0;
    int x = l->cx;
    u32 face = kind == 1 ? (st->has_bg ? st->bg : 0xE4E7EB) : kind == 3 ? 0xD5DAE0 : 0xFFFFFF;
    struct op *o;
    if ((o = new_op(d))) {
        o->type = OP_RECT; o->x = x; o->w = w; o->h = h; o->ih = h;
        o->color = focused ? 0x2F9E80 : 0x8A939C; o->href = href;
        if ((n->tag != T_BUTTON && n->tag != T_IMG && kind != 1) || is_submit(n)) o->node = n;
    }
    if ((o = new_op(d))) { o->type = OP_RECT; o->x = x + 1; o->dy = 1; o->w = w - 2; o->h = h - 2; o->ih = h; o->color = face; }
    if (kind == 2 && n->checked && (o = new_op(d))) {
        o->type = OP_RECT; o->x = x + 4; o->dy = 4; o->w = 6; o->h = 6; o->ih = h; o->color = 0x2F6FB5;
    }
    if (label[0]) {
        int max = (w - 10) / 8;
        int len = (int)strlen(label);
        if (len > max) len = max < 0 ? 0 : max;
        if (len && (o = new_op(d))) {
            o->type = OP_TEXT; o->scale = 1; o->ih = h; o->h = 16; o->w = len * 8;
            o->x = kind == 1 ? x + (w - len * 8) / 2 : x + 5;
            o->dy = (h - 16) / 2;
            if (kind == 3 || n->tag == T_TEXTAREA) o->dy = 3;
            o->color = kind == 1 ? (st->has_bg ? st->color : 0x1F2630) :
                       (n->edit && *n->edit) || n->tag == T_SELECT || kind == 3 ? 0x333A42 : 0x99A0A8;
            o->bg = 0xFF000000;
            o->str = astrndup(&d->a, label, len);
            o->len = len;
            o->href = href;
        }
    }
    if (focused && (o = new_op(d))) {             /* cursor del campo */
        int len = n->edit ? (int)strlen(n->edit) : 0;
        int max = (w - 10) / 8;
        o->type = OP_RECT; o->ih = h; o->w = 2; o->h = 16; o->dy = n->tag == T_TEXTAREA ? 3 : (h - 16) / 2;
        o->x = x + 5 + (len < max ? len : max) * 8; o->color = 0x2F9E80;
    }
    if (n->tag == T_SELECT && (o = new_op(d))) {
        o->type = OP_TEXT; o->scale = 1; o->ih = h; o->h = 16; o->w = 8;
        o->x = x + w - 14; o->dy = 3; o->color = 0x555555; o->bg = 0xFF000000;
        o->str = "\x1F"; o->len = 1;
    }
    if (h > l->maxh) l->maxh = h;
    l->cx += w;
    l->any = 1;
}

static void inline_walk(struct html_doc *d, struct line *l, struct node *n, char *href) {
    if (n->type == N_TEXT) {
        const struct style *st = &n->s;
        const char *s = n->text, *e = n->text + n->tlen;
        if (st->pre) {
            while (s < e) {
                const char *q = s;
                while (q < e && *q != '\n') q++;
                if (q > s) {
                    /* espacios y tabuladores tal cual */
                    char *buf = aalloc(&d->a, (u32)(q - s) * 4 + 1);
                    int k = 0;
                    for (const char *c = s; c < q; c++) {
                        if (*c == '\t') { do buf[k++] = ' '; while (k % 4); }
                        else if (*c != '\r') buf[k++] = *c;
                    }
                    if (buf && k) emit_text(d, l, buf, k, st, href);
                }
                if (q < e) line_break(l, d, 1);
                s = q + 1;
            }
            return;
        }
        while (s < e) {
            if (is_ws((u8)*s)) { l->space = 1; s++; continue; }
            const char *w = s;
            while (s < e && !is_ws((u8)*s)) s++;
            emit_text(d, l, w, (int)(s - w), st, href);
        }
        return;
    }
    if (n->s.display == D_NONE || !visible_if(d, n)) return;
    if (n->calc) {
        char out[48];
        calc_eval(n->calc, doc_lookup, d, n->dec - 1, out, sizeof out);
        char *s = astrndup(&d->a, out, (int)strlen(out));
        if (s) emit_text(d, l, s, (int)strlen(s), &n->s, href);
        return;
    }
    switch (n->tag) {
    case T_BR: line_break(l, d, 1); return;
    case T_IMG: case T_INPUT: case T_BUTTON: case T_SELECT: case T_TEXTAREA:
        emit_box(d, l, n, href); return;
    case T_A: if (n->href) href = n->href; break;
    }
    if (n->tag == T_A && (n->id || n->name)) n->y = l->y;
    if (n->id) n->y = l->y;
    if (n->tag == T_SUP || n->tag == T_SUB) l->space = 0;
    for (struct node *c = n->first; c; c = c->next) inline_walk(d, l, c, href);
}

/* ---------------------------------------------------------------------- */
/* Formularios                                                             */

static int is_text_input(struct node *n) {
    if (n->tag == T_TEXTAREA) return 1;
    if (n->tag != T_INPUT) return 0;
    if (!n->itype) return 1;
    const char *t = n->itype;
    return strcasecmp(t, "checkbox") && strcasecmp(t, "radio") && strcasecmp(t, "submit") &&
           strcasecmp(t, "button") && strcasecmp(t, "reset") && strcasecmp(t, "hidden") &&
           strcasecmp(t, "file") && strcasecmp(t, "image") && strcasecmp(t, "color");
}

static int is_check(struct node *n) {
    return n->tag == T_INPUT && n->itype && (!strcasecmp(n->itype, "checkbox") || !strcasecmp(n->itype, "radio"));
}

static int is_radio(struct node *n) { return is_check(n) && !strcasecmp(n->itype, "radio"); }

static struct node *nth_option(struct node *sel, int idx, int *count) {
    struct node *found = NULL;
    int k = 0;
    for (struct node *c = sel->first; c; c = c->next) {
        if (c->tag == T_OPTION) { if (k == idx) found = c; k++; }
        else if (c->tag == T_OPTGROUP)
            for (struct node *o = c->first; o; o = o->next)
                if (o->tag == T_OPTION) { if (k == idx) found = o; k++; }
    }
    if (count) *count = k;
    return found;
}

static const char *option_text(struct node *o) {
    for (struct node *c = o ? o->first : NULL; c; c = c->next)
        if (c->type == N_TEXT) return c->text;
    return "";
}

static void collect_form(struct html_doc *d, struct node *n) {
    for (; n; n = n->next) {
        if (n->type == N_ELEM) {
            if (n->id && d->nids < 512) d->ids[d->nids++] = n;
            int ctrl = is_text_input(n) || is_check(n) || n->tag == T_SELECT;
            if (ctrl && d->nctrls < 256) d->ctrls[d->nctrls++] = n;
            if (is_text_input(n)) {
                n->edit = aalloc(&d->a, 72);
                if (n->edit) {
                    const char *v = n->tag == T_TEXTAREA ? n->value : n->value;
                    strlcpy(n->edit, v ? v : "", 72);
                    for (char *p = n->edit; *p; p++) if (*p == '\n' || *p == '\r') *p = ' ';
                }
            }
            if (n->tag == T_SELECT) {
                int count, k;
                nth_option(n, 0, &count);
                for (k = 0; k < count; k++) if (nth_option(n, k, NULL)->checked) { n->selidx = k; break; }
            }
        }
        if (n->first && n->tag != T_SELECT) collect_form(d, n->first);
    }
}

static char lookup_buf[96];

/* Valor de un campo o resultado para las formulas (ver calc.c). */
static int doc_lookup(void *user, const char *name, int len, const char **text, int *is_expr) {
    struct html_doc *d = user;
    struct node *n = NULL;
    for (int i = 0; i < d->nids; i++)
        if ((int)strlen(d->ids[i]->id) == len && !memcmp(d->ids[i]->id, name, (size_t)len)) { n = d->ids[i]; break; }
    if (!n) {                                         /* grupo de radios por name */
        for (int i = 0; i < d->nctrls; i++) {
            struct node *c = d->ctrls[i];
            if (is_radio(c) && c->name && (int)strlen(c->name) == len && !memcmp(c->name, name, (size_t)len)) {
                if (!text) return 1;
                if (c->checked) { *text = c->value ? c->value : "1"; *is_expr = 0; return 1; }
                n = c;
            }
        }
        if (n && text) { *text = "0"; *is_expr = 0; return 1; }
        return n != NULL;
    }
    if (!text) return 1;
    *is_expr = 0;
    if (n->calc) { *text = n->calc; *is_expr = 1; return 1; }
    if (n->edit) { *text = n->edit; return 1; }
    if (is_check(n)) { *text = n->checked ? (n->value && is_radio(n) ? n->value : "1") : "0"; return 1; }
    if (n->tag == T_SELECT) {
        struct node *o = nth_option(n, n->selidx, NULL);
        *text = o ? (o->value ? o->value : option_text(o)) : "0";
        return 1;
    }
    if (n->value) { *text = n->value; return 1; }
    /* texto del elemento */
    int k = 0;
    for (struct node *c = n->first; c && k < 90; c = c->next)
        if (c->type == N_TEXT) for (int i = 0; i < c->tlen && k < 90; i++) lookup_buf[k++] = c->text[i];
    lookup_buf[k] = 0;
    *text = lookup_buf;
    return 1;
}

static int visible_if(struct html_doc *d, struct node *n) {
    return n->type != N_ELEM || !n->cif || calc_truth(n->cif, doc_lookup, d);
}

static int pending_margin;

static int layout_block(struct html_doc *d, struct node *n, int x, int y, int w, int *bg_index);

static int box_width(const struct style *s, int avail) {
    int bw = avail - s->m[1] - s->m[3];
    if (s->width_pct > 0 && s->width_pct < 100) bw = avail * s->width_pct / 100;
    else if (s->width > 0 && s->width < bw) bw = s->width;
    if (s->max_width > 0 && bw > s->max_width) bw = s->max_width;
    if (bw < 24) bw = avail < 24 ? avail : 24;
    return bw;
}

/* Coloca los hijos de un bloque. Devuelve la nueva y. */
static int layout_children(struct html_doc *d, struct node *n, int x, int y, int w) {
    struct line l;
    int in_line = 0;
    int counter = n->start ? atoi(n->start) : 1;
    for (struct node *c = n->first; c; c = c->next) {
        int blocky = c->type == N_ELEM && is_blocky(c->s.display);
        if (c->type == N_ELEM && (c->s.display == D_NONE || !visible_if(d, c))) continue;
        if (!blocky) {
            if (!in_line) {
                /* No abrir una linea solo para espacios en blanco */
                if (c->type == N_TEXT && !c->s.pre) {
                    int only_ws = 1;
                    for (int i = 0; i < c->tlen && only_ws; i++) only_ws = is_ws((u8)c->text[i]);
                    if (only_ws) continue;
                }
                y += pending_margin;
                pending_margin = 0;
                line_start(&l, x, w, y, n->s.align, d);
                in_line = 1;
            }
            inline_walk(d, &l, c, NULL);
            continue;
        }
        if (in_line) {
            line_break(&l, d, 0);
            y = l.y;
            in_line = 0;
        }
        if (c->s.display == D_LISTITEM) {
            /* Marcador de la lista: punto o numero */
            int gap = pending_margin > c->s.m[0] ? pending_margin : c->s.m[0];
            int ly = y + gap;
            int list = n->s.list ? n->s.list : c->s.list;
            if (list) {
                struct op *o = new_op(d);
                if (o) {
                    char *buf = aalloc(&d->a, 8);
                    int len;
                    if (list == 2) len = ksnprintf(buf, 8, "%d.", counter);
                    else { buf[0] = 0x07; len = 1; }
                    o->type = OP_TEXT; o->scale = 1; o->h = 16; o->w = len * 8;
                    o->x = x - (list == 2 ? len * 8 + 6 : 14); o->y = ly;
                    o->color = c->s.color; o->bg = 0xFF000000;
                    o->str = buf; o->len = len;
                }
            }
            counter++;
        }
        int dummy;
        y = layout_block(d, c, x, y, w, &dummy);
    }
    if (in_line) { line_break(&l, d, 0); y = l.y; }
    return y;
}

static void draw_borders(struct html_doc *d, const struct style *s, int x, int y, int w, int h) {
    if (s->bw[0]) rect_op(d, x, y, w, s->bw[0], s->bc[0]);
    if (s->bw[2]) rect_op(d, x, y + h - s->bw[2], w, s->bw[2], s->bc[2]);
    if (s->bw[3]) rect_op(d, x, y, s->bw[3], h, s->bc[3]);
    if (s->bw[1]) rect_op(d, x + w - s->bw[1], y, s->bw[1], h, s->bc[1]);
}

static int layout_table(struct html_doc *d, struct node *t, int x, int y, int w);

static int layout_block(struct html_doc *d, struct node *n, int x, int y, int w, int *bg_index) {
    struct style *s = &n->s;
    int bw = box_width(s, w);
    int bx = x + s->m[3];
    if (s->center_auto && bw < w) bx = x + (w - bw) / 2;
    if (bx + bw > x + w) bw = x + w - bx;

    int gap = pending_margin > s->m[0] ? pending_margin : s->m[0];
    int top = y + gap;
    pending_margin = 0;
    n->y = top;

    int bg = s->has_bg ? rect_op(d, bx, top, bw, 0, s->bg) : -1;
    *bg_index = bg;
    int border_start = d->nops;
    (void)border_start;

    int ix = bx + s->bw[3] + s->p[3];
    int iw = bw - s->bw[3] - s->bw[1] - s->p[3] - s->p[1];
    int cy = top + s->bw[0] + s->p[0];
    if (s->bw[0] || s->p[0]) pending_margin = 0;

    if (n->tag == T_HR) {
        rect_op(d, bx, cy, bw, 1, s->bw[0] ? s->bc[0] : 0xB0B6BD);
        cy += 2;
    } else if (s->display == D_TABLE) {
        cy = layout_table(d, n, ix, cy, iw);
    } else if (n->calc) {
        struct line l;
        char out[48];
        calc_eval(n->calc, doc_lookup, d, n->dec - 1, out, sizeof out);
        char *txt = astrndup(&d->a, out, (int)strlen(out));
        line_start(&l, ix, iw, cy, s->align, d);
        if (txt) emit_text(d, &l, txt, (int)strlen(txt), s, NULL);
        line_break(&l, d, 0);
        cy = l.y;
    } else {
        cy = layout_children(d, n, ix, cy, iw);
    }
    if (s->bw[2] || s->p[2]) { cy += pending_margin; pending_margin = 0; }
    cy += s->p[2] + s->bw[2];
    if (cy - top < s->min_height) cy = top + s->min_height;
    int h = cy - top;
    if (bg >= 0) d->ops[bg].h = h;
    draw_borders(d, s, bx, top, bw, h);
    pending_margin = s->m[2] > pending_margin ? s->m[2] : pending_margin;
    return cy;
}

/* --- tablas ------------------------------------------------------------ */

static int collect_rows(struct node *t, struct node **rows, int max, int n) {
    for (struct node *c = t->first; c && n < max; c = c->next) {
        if (c->type != N_ELEM || c->s.display == D_NONE) continue;
        if (c->tag == T_TR || c->s.display == D_ROW) rows[n++] = c;
        else if (c->tag == T_THEAD || c->tag == T_TBODY || c->tag == T_TFOOT || c->s.display == D_ROWGROUP)
            n = collect_rows(c, rows, max, n);
    }
    return n;
}

static int layout_table(struct html_doc *d, struct node *t, int x, int y, int w) {
    static struct node *rows[1024];
    for (struct node *c = t->first; c; c = c->next)
        if (c->tag == T_CAPTION && c->s.display != D_NONE) { int bi; y = layout_block(d, c, x, y, w, &bi); }
    int nrows = collect_rows(t, rows, 1024, 0);
    int ncols = 1;
    for (int r = 0; r < nrows; r++) {
        int k = 0;
        for (struct node *c = rows[r]->first; c; c = c->next)
            if (c->type == N_ELEM && (c->tag == T_TD || c->tag == T_TH || c->s.display == D_CELL))
                k += c->colspan ? (atoi(c->colspan) > 0 ? atoi(c->colspan) : 1) : 1;
        if (k > ncols) ncols = k;
    }
    if (ncols > 24) ncols = 24;
    int grid = (t->border && atoi(t->border) > 0) || 1;
    u32 grid_col = 0xC9CED4;
    int colw = w / ncols;
    pending_margin = 0;
    struct node **saved = kmalloc((size_t)nrows * sizeof *saved);
    if (saved) memcpy(saved, rows, (size_t)nrows * sizeof *saved);
    for (int r = 0; r < nrows; r++) {
        struct node *row = saved ? saved[r] : rows[r];
        int row_bg = row->s.has_bg ? rect_op(d, x, y, w, 0, row->s.bg) : -1;
        int col = 0, rowh = 18;
        int cell_bg[24], cell_x[24], cell_w[24], ncell = 0;
        for (struct node *c = row->first; c && col < ncols; c = c->next) {
            if (c->type != N_ELEM || !(c->tag == T_TD || c->tag == T_TH || c->s.display == D_CELL)) continue;
            int span = c->colspan ? atoi(c->colspan) : 1;
            if (span < 1) span = 1;
            if (col + span > ncols) span = ncols - col;
            int cx = x + col * colw, cw = colw * span;
            if (col + span == ncols) cw = x + w - cx;
            int bi;
            int save = pending_margin;
            pending_margin = 0;
            int bottom = layout_block(d, c, cx, y, cw, &bi);
            pending_margin = save;
            int hh = bottom - y;
            if (hh > rowh) rowh = hh;
            if (ncell < 24) { cell_bg[ncell] = bi; cell_x[ncell] = cx; cell_w[ncell] = cw; ncell++; }
            col += span;
        }
        pending_margin = 0;
        if (row_bg >= 0) d->ops[row_bg].h = rowh;
        for (int i = 0; i < ncell; i++) {
            if (cell_bg[i] >= 0) d->ops[cell_bg[i]].h = rowh;
            if (grid) {
                rect_op(d, cell_x[i], y, cell_w[i], 1, grid_col);
                rect_op(d, cell_x[i], y, 1, rowh, grid_col);
                rect_op(d, cell_x[i] + cell_w[i] - 1, y, 1, rowh, grid_col);
                rect_op(d, cell_x[i], y + rowh - 1, cell_w[i], 1, grid_col);
            }
        }
        y += rowh;
    }
    kfree(saved);
    return y;
}

/* ====================================================================== */
/* Interfaz                                                                */

struct html_doc *html_load(const char *src, u32 len, int width) {
    struct html_doc *d = kmalloc(sizeof *d);
    if (!d) return NULL;
    memset(d, 0, sizeof *d);
    d->width = width;
    css_width = width;
    parse_html(d, src, len);
    if (!d->root) { html_free(d); return NULL; }

    struct style base;
    memset(&base, 0, sizeof base);
    base.color = 0x1F2328;
    base.scale = 1;
    base.display = D_BLOCK;
    compute_styles(d, d->root->first, &base, 0xFFFFFF);
    d->root->s = base;
    promote_blocks(d->root);
    d->ids = aalloc(&d->a, 512 * sizeof(struct node *));
    d->ctrls = aalloc(&d->a, 256 * sizeof(struct node *));
    if (d->ids && d->ctrls) collect_form(d, d->root->first);

    /* Fondo del documento: el de <body> o, si no, el de <html> */
    d->bg = 0xFFFFFF;
    for (struct node *h = d->root->first; h; h = h->next) {
        if (h->tag == T_HTML && h->s.has_bg) d->bg = h->s.bg;
        if (h->tag == T_HTML)
            for (struct node *b = h->first; b; b = b->next)
                if (b->tag == T_BODY && b->s.has_bg) d->bg = b->s.bg;
        if (h->tag == T_BODY && h->s.has_bg) d->bg = h->s.bg;
    }
    d->root->s.p[0] = d->root->s.p[2] = 10;
    d->root->s.p[1] = d->root->s.p[3] = 14;
    html_relayout(d);
    return d;
}

void html_relayout(struct html_doc *d) {
    int dummy;
    d->nops = 0;
    pending_margin = 0;
    css_width = d->width;
    d->height = layout_block(d, d->root, 0, 0, d->width, &dummy) + 10;
}

int html_has_focus(struct html_doc *d) { return d->focus != NULL; }

/* Posicion vertical del campo con el cursor (-1 si no hay). */
int html_focus_y(struct html_doc *d) {
    for (int i = 0; d->focus && i < d->nops; i++)
        if (d->ops[i].node == d->focus) return d->ops[i].y;
    return -1;
}

/* Texto del campo con el cursor, o NULL. */
const char *html_focus_text(struct html_doc *d) {
    return d->focus ? d->focus->edit : NULL;
}

static int is_submit(struct node *n) { if (n->tag == T_BUTTON) return !n->itype || !strcasecmp(n->itype, "submit"); return n->tag == T_INPUT && n->itype && (!strcasecmp(n->itype, "submit") || !strcasecmp(n->itype, "image")); }
static void enc_add(char *o, int cap, const char *s, int cp437) { int k = (int)strlen(o); static const char hx[] = "0123456789ABCDEF";
    for (const u8 *p = (const u8 *)s; *p && k < cap - 12; p++) { u32 c = *p; char u[8]; int ul = 1; u[0] = (char)c; if (cp437 && c >= 128) { u32 tl; char *x = cp437_to_utf8((const char *)p, 1, &tl); if (x) { ul = tl > 7 ? 7 : (int)tl; memcpy(u, x, ul); kfree(x); } }
        for (int i = 0; i < ul; i++) { u8 b = (u8)u[i]; if ((b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9') || b == '-' || b == '_' || b == '.' || b == '~') o[k++] = b; else if (b == ' ') o[k++] = '+'; else { o[k++] = '%'; o[k++] = hx[b >> 4]; o[k++] = hx[b & 15]; } } }
    o[k] = 0; }
static void do_submit(struct html_doc *d, struct node *from) {
    struct node *f = from; while (f && f->tag != T_FORM) f = f->parent; if (!f) return; char q[2800]; q[0] = 0;
    for (int i = 0; i < d->nctrls; i++) { struct node *c = d->ctrls[i], *a = c; while (a && a != f) a = a->parent; if (!a || !c->name) continue;
        const char *v = 0; int cp = 0;
        if (c->tag == T_INPUT && c->itype && (!strcasecmp(c->itype, "checkbox") || !strcasecmp(c->itype, "radio"))) { if (!c->checked) continue; v = c->value ? c->value : "on"; }
        else if (is_submit(c)) { if (c != from) continue; v = c->value ? c->value : ""; }
        else if (c->tag == T_SELECT) { struct node *op = nth_option(c, c->selidx, NULL); v = op && op->value ? op->value : op ? option_text(op) : ""; cp = !(op && op->value); }
        else if (c->edit) { v = c->edit; cp = 1; } else v = c->value ? c->value : "";
        if (q[0]) strcat(q, "&");
        enc_add(q, sizeof q, c->name, 0); strcat(q, "="); enc_add(q, sizeof q, v, cp); }
    if (from && is_submit(from) && from->name && !strstr(q, from->name)) { if (q[0]) strcat(q, "&"); enc_add(q, sizeof q, from->name, 0); strcat(q, "="); enc_add(q, sizeof q, from->value ? from->value : "", 0); }
    d->submit_post = f->method && !strcasecmp(f->method, "post"); ksnprintf(d->submit, sizeof d->submit, "%s%s%s", f->action ? f->action : "", d->submit_post ? "\n" : "?", q); }
const char *html_take_submit(struct html_doc *d, int *post) { if (!d->submit[0]) return 0; static char out[3072]; strlcpy(out, d->submit, sizeof out); d->submit[0] = 0; if (post) *post = d->submit_post; return out; }
int html_nimages(struct html_doc *d) { return d->nimgs; }
const char *html_image_src(struct html_doc *d, int i) { return i >= 0 && i < d->nimgs ? d->imgs[i] : 0; }
int html_click(struct html_doc *d, int x, int y) {
    for (int i = d->nops - 1; i >= 0; i--) {
        struct op *o = &d->ops[i];
        if (!o->node || x < o->x || x >= o->x + o->w || y < o->y || y >= o->y + o->h) continue;
        struct node *n = o->node;
        if (n->edit) { d->focus = n; return 1; }
        d->focus = NULL;
        if (is_submit(n)) { do_submit(d, n); return 1; }
        if (is_radio(n)) {
            for (int k = 0; k < d->nctrls; k++) {
                struct node *c = d->ctrls[k];
                if (is_radio(c) && c->name && n->name && !strcmp(c->name, n->name)) c->checked = 0;
            }
            n->checked = 1;
        } else if (is_check(n)) {
            n->checked = !n->checked;
        } else if (n->tag == T_SELECT) {
            int count;
            nth_option(n, 0, &count);
            if (count) n->selidx = (n->selidx + 1) % count;
        }
        return 1;
    }
    if (d->focus) { d->focus = NULL; return 1; }
    return 0;
}

int html_key(struct html_doc *d, int k) {
    if (!d->focus) {
        if (k == '\t' && d->nctrls) {               /* Tab: primer campo */
            for (int i = 0; i < d->nctrls; i++) if (d->ctrls[i]->edit) { d->focus = d->ctrls[i]; return 1; }
        }
        return 0;
    }
    struct node *n = d->focus;
    if (k == '\n' && n->tag == T_INPUT) { struct node *f = n; while (f && f->tag != T_FORM) f = f->parent; if (f) { do_submit(d, n); return 1; } }
    if (k == '\t' || k == '\n') {                   /* siguiente campo */
        int i = 0;
        while (i < d->nctrls && d->ctrls[i] != n) i++;
        for (int j = 1; j <= d->nctrls; j++) {
            struct node *c = d->ctrls[(i + j) % d->nctrls];
            if (c->edit) { d->focus = c; return 1; }
        }
        return 1;
    }
    if (k == 27) { d->focus = NULL; return 1; }
    int len = (int)strlen(n->edit);
    if (k == '\b' || k == KEY_DEL) { if (len) n->edit[len - 1] = 0; return 1; }
    if (k == CTRL('a') || k == CTRL('x')) { n->edit[0] = 0; return 1; }
    if (k == CTRL('v')) {
        int cl;
        const char *c = clip_get(&cl);
        for (int i = 0; c && i < cl && len < 70; i++) if ((u8)c[i] >= 32) n->edit[len++] = c[i];
        n->edit[len] = 0;
        return 1;
    }
    if (k >= 32 && k < 0x100 && len < 70) {
        if (n->itype && !strcasecmp(n->itype, "number") &&
            !((k >= '0' && k <= '9') || k == '.' || k == ',' || k == '-')) return 1;
        n->edit[len] = (char)k;
        n->edit[len + 1] = 0;
        return 1;
    }
    return k == KEY_LEFT || k == KEY_RIGHT || k == KEY_HOME || k == KEY_END;
}

void html_free(struct html_doc *d) {
    if (!d) return;
    kfree(d->ops);
    afree(&d->a);
    kfree(d);
}

int html_height(struct html_doc *d) { return d->height; }
u32 html_background(struct html_doc *d) { return d->bg; }
const char *html_title(struct html_doc *d) { return d->title && *d->title ? d->title : NULL; }
int html_has_script(struct html_doc *d) { return d->has_script; }
/* <base href> y <meta http-equiv="refresh" content="N; url=..."> */
static struct node *find_tag(struct node *n, int tag, int need_meta) { for (; n; n = n->next) { if (n->type == N_ELEM && n->tag == tag && (!need_meta ? n->href != NULL : (n->equiv && !strcasecmp(n->equiv, "refresh") && n->content))) return n; struct node *r = find_tag(n->first, tag, need_meta); if (r) return r; } return NULL; }
const char *html_base(struct html_doc *d) { struct node *n = find_tag(d->root, T_BASE, 0); return n ? n->href : NULL; }
const char *html_refresh(struct html_doc *d, int *secs) { struct node *n = find_tag(d->root, T_META, 1); if (!n) return NULL; const char *c = n->content; *secs = atoi(c); const char *u = strcasestr(c, "url="); if (!u) return ""; u += 4; while (*u == ' ' || *u == '\'' || *u == '"') u++; return u; }

void html_draw(struct html_doc *d, int x, int y, int w, int h, int scroll) {
    (void)w;
    for (int i = 0; i < d->nops; i++) {
        struct op *o = &d->ops[i];
        if (o->y + o->h < scroll || o->y > scroll + h) continue;
        int ox = x + o->x, oy = y + o->y - scroll;
        if (o->type == OP_RECT) {
            if (o->h > 0 && o->w > 0) gfx_fill(ox, oy, o->w, o->h, o->color);
            continue;
        }
        if (o->type == OP_IMG) { gfx_image(ox, oy, o->w, o->h, o->px, o->iw, o->ih2); continue; }
        if (o->bg != 0xFF000000) gfx_fill(ox - 1, oy, o->w + 2, o->h, o->bg);
        char buf[2] = { 0, 0 };
        for (int k = 0; k < o->len; k++) {
            int cx = ox + k * 8 * o->scale;
            if (o->scale == 1) {
                gfx_char(cx, oy, (u8)o->str[k], o->color);
                if (o->bold) gfx_char(cx + 1, oy, (u8)o->str[k], o->color);
            } else {
                buf[0] = o->str[k];
                gfx_text_big(cx, oy, buf, o->color, o->scale);
                if (o->bold) gfx_text_big(cx + 1, oy, buf, o->color, o->scale);
            }
        }
        if (o->underline) gfx_fill(ox, oy + 14 * o->scale, o->w, o->scale, o->color);
        if (o->strike) gfx_fill(ox, oy + 8 * o->scale, o->w, o->scale, o->color);
    }
}

/* recorre lo que se dibujaria (para imprimir): tipo 0 rectangulo, 1 texto (cp437), 2 imagen */
void html_walk(struct html_doc *d, void (*cb)(void *, const struct html_item *), void *ctx) {
    for (int i = 0; i < d->nops; i++) { struct op *o = &d->ops[i]; struct html_item it = {o->type, o->x, o->y, o->w, o->h, o->color, o->bg, o->str, o->len, o->scale, o->bold, o->underline, o->px, o->iw, o->ih2}; cb(ctx, &it); }
}

const char *html_link_at(struct html_doc *d, int x, int y) {
    for (int i = 0; i < d->nops; i++) {
        struct op *o = &d->ops[i];
        if (o->href && x >= o->x && x < o->x + o->w && y >= o->y && y < o->y + o->h) return o->href;
    }
    return NULL;
}

static struct node *find_id(struct node *n, const char *id) {
    for (; n; n = n->next) {
        if (n->type == N_ELEM && ((n->id && !strcmp(n->id, id)) || (n->name && n->tag == T_A && !strcmp(n->name, id))))
            return n;
        struct node *r = n->first ? find_id(n->first, id) : NULL;
        if (r) return r;
    }
    return NULL;
}

int html_anchor_y(struct html_doc *d, const char *id) {
    struct node *n = find_id(d->root, id);
    return n ? n->y : -1;
}
