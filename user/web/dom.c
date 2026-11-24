/* DOM para JavaScript (Navegador JS) con Colibri, el motor JavaScript de ZunzunOS. Se incluye al final de html.c
   cuando se compila con WEB_JS, asi ve los nodos por dentro. Aqui solo estan las operaciones basicas sobre el arbol,
   como funciones del objeto oculto __z; todo lo demas (Node, Element, eventos, selectores, classList, style, fetch,
   localStorage...) esta escrito en JavaScript en dom.js, que se ejecuta antes que los scripts de la pagina.
   El texto de los nodos esta en CP437 (la tipografia del motor); JavaScript ve siempre UTF-8. */
#include "cbhost.h"
#include "domjs.h"
cbv cb_new_host(cb_rt *R, cb_obj *proto, void *p);
void *cb_opaque(cbv v, int cls);
int cb_arr_set(cb_rt *R, cb_obj *a, u32 i, cbv v);
cb_obj *cb_new_array(cb_rt *R, u32 n);
cbv cb_mkstr(cb_rt *R, const char *s);
cbv cb_call_v(cb_rt *R, cbv f, cbv thisv, int argc, cbv *argv);
int cb_run_jobs(cb_rt *R);
void cb_arr_push(cb_rt *R, cb_obj *a, cbv v);
cbv cb_list_to_array(cb_rt *R, int n, cbv *v);
double cb_to_int(cb_rt *R, cbv v);
typedef struct { struct html_js_host h; int dirty; cbv wrappers, protos, ev, modules; struct node *cur; char *wbuf; long wlen; char host[128]; } dom_t;
u16 cp437_to_uni(u8 c);
static struct html_doc *JD;            /* documento con JavaScript (uno por programa) */
static dom_t *DM;
static cb_rt *JC;
static cbv KEEP;                       /* raiz: objetos que el recolector no debe soltar (envolturas, prototipos) */

/* ---- texto: CP437 <-> UTF-8 ---- */
static char *to437(cbv v, int *len) {
    long n = 0; char *s = cb_to_utf8(JC, v, &n); if (!s) { cb_take_exc(JC); n = 0; }
    char *o = aalloc(&JD->a, (u32)n + 1); int k = 0;
    if (o) { const u8 *p = (const u8 *)s, *e = p + n; while (p && p < e) { int cp = utf8_decode(&p, e); if (cp < 0) break; u8 c = cp == '\n' || cp == '\t' ? (u8)cp : uni_to_cp437((u32)cp); o[k++] = (char)(c ? c : '?'); } o[k] = 0; }
    free(s); if (len) *len = k; return o ? o : ""; }
static cbv from437(const char *s, int len) {
    if (!s) return CB_NULL; if (len < 0) len = (int)strlen(s); u32 ol; char *u = cp437_to_utf8(s, (u32)len, &ol);
    cbv r = u ? cb_str_utf8(JC, u, ol) : cb_mkstr(JC, ""); kfree(u); return r; }
static char *cstr(cbv v) { char *s = cb_to_utf8(JC, v, 0); if (!s) { cb_take_exc(JC); return strdup(""); } return s; }
static void arrset(cbv a, u32 i, cbv v) { cb_arr_set(JC, cb_o(a), i, v); }

/* ---- objetos de los nodos (siempre el mismo objeto para el mismo nodo; los guarda DM->wrappers) ---- */
static cbv wrap(struct node *n) {
    if (!n) return CB_NULL;
    if (n->js) return (cbv)n->js;
    cb_obj *pr = cb_o(cb_o(DM->protos)->e[n->type == N_TEXT ? 1 : n == JD->root ? 2 : 0]);
    cbv o = cb_new_host(JC, pr, n); n->js = (u64)o; cb_arr_push(JC, cb_o(DM->wrappers), o); return o; }
static struct node *NODE(cbv v) { return cb_opaque(v, 0); }
#define A(i) (i < argc ? argv[i] : CB_UNDEF)
#define FN(name) static cbv name(cb_rt *c, cbv this_val, int argc, cbv *argv, cb_obj *fnobj)
#define NEED(n, i) struct node *n = NODE(A(i)); if (!n) return cb_throw(JC, 1, "no es un nodo")
static void dirty(void) { DM->dirty = 1; }

static void detach(struct node *n) {
    struct node *p = n->parent; if (!p) return; struct node *prev = NULL, *x = p->first;
    while (x && x != n) { prev = x; x = x->next; } if (!x) { n->parent = NULL; return; }
    if (prev) prev->next = n->next; else p->first = n->next; if (p->last == n) p->last = prev;
    n->parent = NULL; n->next = NULL; }
static int inside(struct node *a, struct node *n) { for (; a; a = a->parent) if (a == n) return 1; return 0; }
static int put(struct node *p, struct node *n, struct node *ref) {   /* inserta n en p antes de ref (o al final) */
    if (inside(p, n) || (ref && ref->parent != p)) return -1;
    detach(n); n->parent = p;
    if (!ref) { n->next = NULL; if (p->last) p->last->next = n; else p->first = n; p->last = n; }
    else { struct node *prev = NULL; for (struct node *x = p->first; x && x != ref; x = x->next) prev = x; n->next = ref; if (prev) prev->next = n; else p->first = n; }
    dirty(); return 0; }

/* ---- atributos ---- */
static char **slot(struct node *n, const char *k) {
    static const char *nm[] = { "id", "class", "style", "href", "align", "bgcolor", "color", "width", "height", "alt", "value", "placeholder", "type", "border", "start", "colspan", "name", "src", "action", "method", "data-calc", "data-if", "http-equiv", "content" };
    char **sl[] = { &n->id, &n->cls, &n->style, &n->href, &n->align, &n->bgcolor, &n->color, &n->width, &n->height, &n->alt, &n->value, &n->placeholder, &n->itype, &n->border, &n->start, &n->colspan, &n->name, &n->src, &n->action, &n->method, &n->calc, &n->cif, &n->equiv, &n->content };
    for (u32 i = 0; i < sizeof nm / sizeof *nm; i++) if (!strcmp(k, nm[i])) return sl[i];
    return NULL; }
static int *bslot(struct node *n, const char *k) { if (!strcmp(k, "hidden")) return &n->hidden_attr; if (!strcmp(k, "checked") || !strcmp(k, "selected")) return &n->checked; return NULL; }
static void lower(char *s) { for (; *s; s++) if (*s >= 'A' && *s <= 'Z') *s += 32; }
FN(dz_attr) { NEED(n, 0); char *k = cstr(A(1)); lower(k); cbv r = CB_NULL; char **sl = slot(n, k); int *b = bslot(n, k);
    if (sl) r = *sl ? from437(*sl, -1) : CB_NULL; else if (b) r = *b ? cb_mkstr(JC, "") : CB_NULL;
    else for (struct xattr *x = n->xa; x; x = x->next) if (!strcmp(x->name, k)) { r = from437(x->val, -1); break; }
    free(k); return r; }
FN(dz_setattr) { NEED(n, 0); char *k = cstr(A(1)); lower(k); char *v = to437(A(2), 0); char **sl = slot(n, k); int *b = bslot(n, k);
    if (sl) *sl = v; else if (b) *b = 1;
    else { struct xattr *x = n->xa; while (x && strcmp(x->name, k)) x = x->next; if (!x) { x = aalloc(&JD->a, sizeof *x); char *nm = aalloc(&JD->a, (u32)strlen(k) + 1); if (x && nm) { strcpy(nm, k); x->name = nm; x->next = n->xa; n->xa = x; } } if (x) x->val = v; }
    if (!strcmp(k, "value") && n->edit) strlcpy(n->edit, v, 72);
    free(k); dirty(); return CB_UNDEF; }
FN(dz_rmattr) { NEED(n, 0); char *k = cstr(A(1)); lower(k); char **sl = slot(n, k); int *b = bslot(n, k);
    if (sl) *sl = NULL; else if (b) *b = 0; else for (struct xattr **x = &n->xa; *x; x = &(*x)->next) if (!strcmp((*x)->name, k)) { *x = (*x)->next; break; }
    free(k); dirty(); return CB_UNDEF; }
FN(dz_attrs) { NEED(n, 0); cbv a = cb_objv(cb_new_array(JC, 0)); int k = 0; static const char *nm[] = { "id", "class", "style", "href", "align", "bgcolor", "color", "width", "height", "alt", "value", "placeholder", "type", "border", "start", "colspan", "name", "src", "action", "method", "data-calc", "data-if", "http-equiv", "content" };
    for (u32 i = 0; i < sizeof nm / sizeof *nm; i++) { char **s = slot(n, nm[i]); if (*s) { cbv p = cb_objv(cb_new_array(JC, 0)); arrset(p, 0, cb_mkstr(JC, nm[i])); arrset(p, 1, from437(*s, -1)); arrset(a, k++, p); } }
    if (n->hidden_attr) { cbv p = cb_objv(cb_new_array(JC, 0)); arrset(p, 0, cb_mkstr(JC, "hidden")); arrset(p, 1, cb_mkstr(JC, "")); arrset(a, k++, p); }
    for (struct xattr *x = n->xa; x; x = x->next) { cbv p = cb_objv(cb_new_array(JC, 0)); arrset(p, 0, cb_mkstr(JC, x->name)); arrset(p, 1, from437(x->val, -1)); arrset(a, k++, p); }
    return a; }

/* ---- arbol ---- */
FN(dz_doc) { return wrap(JD->root); }
FN(dz_protos) { cbv l[4] = { A(0), A(1), A(2), A(3) }; DM->protos = cb_list_to_array(JC, 4, l); DM->ev = A(3); arrset(KEEP, 1, DM->protos); return CB_UNDEF; }
FN(dz_type) { NEED(n, 0); return cb_num(n == JD->root ? 9 : n->type == N_TEXT ? 3 : 1); }
FN(dz_tag) { NEED(n, 0); const char *t = n->tag ? tag_names[n->tag] : n->tname ? n->tname : "div"; char b[64]; int i = 0; for (; t[i] && i < 63; i++) b[i] = t[i] >= 'a' && t[i] <= 'z' ? t[i] - 32 : t[i]; b[i] = 0; return cb_mkstr(JC, b); }
FN(dz_parent) { NEED(n, 0); return wrap(n->parent); }
FN(dz_first) { NEED(n, 0); return wrap(n->first); }
FN(dz_last) { NEED(n, 0); return wrap(n->last); }
FN(dz_next) { NEED(n, 0); return wrap(n->next); }
FN(dz_prev) { NEED(n, 0); if (!n->parent) return CB_NULL; struct node *p = NULL; for (struct node *x = n->parent->first; x && x != n; x = x->next) p = x; return wrap(p); }
static void all_rec(struct node *n, cbv a, u32 *k) { for (struct node *x = n->first; x; x = x->next) if (x->type == N_ELEM) { arrset(a, (*k)++, wrap(x)); all_rec(x, a, k); } }
FN(dz_all) { NEED(n, 0); cbv a = cb_objv(cb_new_array(JC, 0)); u32 k = 0; all_rec(n, a, &k); return a; }
static void text_rec(struct node *n, char **b, int *len, int *cap) {
    for (struct node *x = n->first; x; x = x->next) { if (x->type == N_TEXT) { if (*len + x->tlen + 1 > *cap) { *cap = (*len + x->tlen) * 2 + 64; *b = realloc(*b, *cap); } memcpy(*b + *len, x->text, x->tlen); *len += x->tlen; }
        else if (x->tag != T_SCRIPT && x->tag != T_STYLE) text_rec(x, b, len, cap); } }
FN(dz_text) { NEED(n, 0); if (n->type == N_TEXT) return from437(n->text, n->tlen); if (n->tag == T_SCRIPT || n->tag == T_STYLE) { return n->text ? cb_str_utf8(JC, n->text, n->tlen) : cb_mkstr(JC, ""); }
    char *b = malloc(256); int len = 0, cap = 256; text_rec(n, &b, &len, &cap); cbv r = from437(b, len); free(b); return r; }
FN(dz_settext) { NEED(n, 0); int len; char *t = to437(A(1), &len);
    if (n->type == N_TEXT) { n->text = t; n->tlen = len; }
    else { while (n->first) detach(n->first); if (len) { struct node *x = new_node(JD, n, N_TEXT, 0); if (x) { x->text = t; x->tlen = len; } } }
    dirty(); return CB_UNDEF; }
FN(dz_create) { char *t = cstr(A(0)); lower(t); int tag = lookup_tag(t, (int)strlen(t)); struct node *n = new_node(JD, NULL, N_ELEM, tag); if (n && tag == T_NONE) { n->tname = aalloc(&JD->a, (u32)strlen(t) + 1); if (n->tname) strcpy(n->tname, t); } free(t); return wrap(n); }
FN(dz_createtext) { int len; char *t = to437(A(0), &len); struct node *n = new_node(JD, NULL, N_TEXT, 0); if (n) { n->text = t; n->tlen = len; } return wrap(n); }
FN(dz_append) { NEED(p, 0); NEED(n, 1); if (put(p, n, NULL)) return cb_throw(JC, 1, "HierarchyRequestError"); return CB_UNDEF; }
FN(dz_insert) { NEED(p, 0); NEED(n, 1); struct node *r = NODE(A(2)); if (put(p, n, r)) return cb_throw(JC, 1, "NotFoundError"); return CB_UNDEF; }
FN(dz_remove) { NEED(n, 0); detach(n); dirty(); return CB_UNDEF; }
static struct node *clone(struct node *n, int deep) {
    struct node *m = new_node(JD, NULL, n->type, n->tag); if (!m) return NULL;
    struct node *keep_parent = m->parent; *m = *n; m->parent = keep_parent; m->first = m->last = m->next = NULL; m->js = 0; m->edit = NULL; m->xa = NULL;
    for (struct xattr *x = n->xa; x; x = x->next) { struct xattr *y = aalloc(&JD->a, sizeof *y); if (y) { *y = *x; y->next = m->xa; m->xa = y; } }
    if (deep) for (struct node *c = n->first; c; c = c->next) { struct node *k = clone(c, 1); if (k) { k->parent = m; if (m->last) m->last->next = k; else m->first = k; m->last = k; } }
    return m; }
FN(dz_clone) { NEED(n, 0); return wrap(clone(n, cb_truthy(A(1)))); }

/* ---- HTML: innerHTML / outerHTML ---- */
typedef struct { char *p; long n, cap; } sbuf;
static void dsb_add(sbuf *b, const char *s, long n) { if (b->n + n + 1 > b->cap) { b->cap = (b->n + n) * 2 + 256; b->p = realloc(b->p, b->cap); } memcpy(b->p + b->n, s, n); b->n += n; b->p[b->n] = 0; }
static void dsb_cs(sbuf *b, const char *s) { dsb_add(b, s, strlen(s)); }
static void dsb_esc(sbuf *b, const char *s, int len, int attr) {
    if (len < 0) len = (int)strlen(s);
    for (int i = 0; i < len; i++) { u8 ch = (u8)s[i]; if (ch == '&') dsb_cs(b, "&amp;"); else if (ch == '<') dsb_cs(b, "&lt;"); else if (ch == '>') dsb_cs(b, "&gt;"); else if (attr && ch == '"') dsb_cs(b, "&quot;");
        else { char u[4]; int k = utf8_encode(cp437_to_uni(ch), u); dsb_add(b, u, k); } } }
static void ser(sbuf *b, struct node *n, int self) {
    if (n->type == N_TEXT) { dsb_esc(b, n->text, n->tlen, 0); return; }
    const char *t = n->tag ? tag_names[n->tag] : n->tname ? n->tname : "div";
    if (self && n != JD->root) {
        dsb_cs(b, "<"); dsb_cs(b, t);
        static const char *nm[] = { "id", "class", "style", "href", "align", "bgcolor", "color", "width", "height", "alt", "value", "placeholder", "type", "border", "start", "colspan", "name", "src", "action", "method", "data-calc", "data-if", "http-equiv", "content" };
        for (u32 i = 0; i < sizeof nm / sizeof *nm; i++) { char **s = slot(n, nm[i]); if (*s) { dsb_cs(b, " "); dsb_cs(b, nm[i]); dsb_cs(b, "=\""); dsb_esc(b, *s, -1, 1); dsb_cs(b, "\""); } }
        for (struct xattr *x = n->xa; x; x = x->next) { dsb_cs(b, " "); dsb_cs(b, x->name); dsb_cs(b, "=\""); dsb_esc(b, x->val, -1, 1); dsb_cs(b, "\""); }
        if (n->hidden_attr) dsb_cs(b, " hidden"); if (n->checked) dsb_cs(b, n->tag == T_OPTION ? " selected" : " checked");
        dsb_cs(b, ">"); if (is_void(n->tag)) return; }
    if ((n->tag == T_SCRIPT || n->tag == T_STYLE) && n->text) dsb_add(b, n->text, n->tlen);
    else for (struct node *c = n->first; c; c = c->next) ser(b, c, 1);
    if (self && n != JD->root) { dsb_cs(b, "</"); dsb_cs(b, t); dsb_cs(b, ">"); } }
FN(dz_html) { NEED(n, 0); sbuf b = { 0 }; dsb_cs(&b, ""); ser(&b, n, cb_truthy(A(1))); cbv r = cb_str_utf8(JC, b.p, b.n); free(b.p); return r; }
FN(dz_sethtml) { NEED(n, 0); long len; char *s = cb_to_utf8(JC, A(1), &len); if (!s) return CB_EMPTY;
    while (n->first) detach(n->first); parse_into(JD, n, s, (u32)len); free(s); dirty(); return CB_UNDEF; }

/* ---- formularios ---- */
FN(dz_value) { NEED(n, 0);
    if (n->edit) return from437(n->edit, -1);
    if (n->tag == T_SELECT) { struct node *o = nth_option(n, n->selidx, NULL); return o ? (o->value ? from437(o->value, -1) : from437(option_text(o), -1)) : cb_mkstr(JC, ""); }
    if (n->tag == T_TEXTAREA || n->value) return from437(n->value ? n->value : "", -1);
    return cb_mkstr(JC, ""); }
FN(dz_setvalue) { NEED(n, 0); char *v = to437(A(1), 0);
    if (n->edit) strlcpy(n->edit, v, 72);
    if (n->tag == T_SELECT) { int count; nth_option(n, 0, &count); for (int k = 0; k < count; k++) { struct node *o = nth_option(n, k, NULL); const char *ov = o->value ? o->value : option_text(o); if (!strcmp(ov, v)) { n->selidx = k; break; } } }
    else if (!n->edit || n->tag == T_TEXTAREA) n->value = v;
    dirty(); return CB_UNDEF; }
FN(dz_checked) { NEED(n, 0); return cb_bool(n->checked); }
FN(dz_setchecked) { NEED(n, 0); int v = cb_truthy(A(1)); if (v && is_radio(n)) for (int k = 0; k < JD->nctrls; k++) { struct node *o = JD->ctrls[k]; if (is_radio(o) && o->name && n->name && !strcmp(o->name, n->name)) o->checked = 0; } n->checked = v; dirty(); return CB_UNDEF; }
FN(dz_sel) { NEED(n, 0); return cb_num(n->selidx); }
FN(dz_setsel) { NEED(n, 0); int i = 0; (i = (int)cb_to_int(JC, A(1))); n->selidx = i < 0 ? 0 : i; dirty(); return CB_UNDEF; }
FN(dz_focus) { struct node *n = NODE(A(0)); JD->focus = n && n->edit ? n : NULL; dirty(); return CB_UNDEF; }
FN(dz_active) { return wrap(JD->focus); }
FN(dz_submit) { NEED(n, 0); do_submit(JD, n); return CB_UNDEF; }

/* ---- posicion en la pagina ---- */
/* quien pide posiciones ve la pagina ya maquetada con los cambios (como el reflow de los navegadores) */
static void reflow(void) { if (DM->dirty) html_relayout(JD); }   /* html_relayout ya llama a dom_flush */
static void dom_flush(struct html_doc *d) { if (DM && d == JD && DM->dirty) { DM->dirty = 0; html_restyle_only(d); } }
static int under(struct node *o, struct node *n) { for (; o; o = o->parent) if (o == n) return 1; return 0; }
FN(dz_rect) { NEED(n, 0); reflow(); int x0 = 0, y0 = 0, x1 = 0, y1 = 0, any = 0;
    if (n->bh) { x0 = n->bx; y0 = n->y; x1 = n->bx + n->bw; y1 = n->y + n->bh; any = 1; }
    else for (int i = 0; i < JD->nops; i++) { struct op *o = &JD->ops[i]; if (!under(o->owner, n)) continue; if (!any) { x0 = o->x; y0 = o->y; x1 = o->x + o->w; y1 = o->y + o->h; any = 1; } else { if (o->x < x0) x0 = o->x; if (o->y < y0) y0 = o->y; if (o->x + o->w > x1) x1 = o->x + o->w; if (o->y + o->h > y1) y1 = o->y + o->h; } }
    cbv a = cb_objv(cb_new_array(JC, 0)); arrset(a, 0, cb_num(x0)); arrset(a, 1, cb_num(y0)); arrset(a, 2, cb_num(x1 - x0)); arrset(a, 3, cb_num(y1 - y0)); return a; }
FN(dz_view) { reflow(); int w = JD->width, h = 600, sy = 0; if (DM->h.view) DM->h.view(DM->h.u, &w, &h, &sy); cbv a = cb_objv(cb_new_array(JC, 0)); arrset(a, 0, cb_num(w)); arrset(a, 1, cb_num(h)); arrset(a, 2, cb_num(sy)); arrset(a, 3, cb_num(JD->height)); return a; }
FN(dz_scroll) { int y = 0; (y = (int)cb_to_int(JC, A(0))); if (DM->h.scroll) DM->h.scroll(DM->h.u, y); return CB_UNDEF; }

/* ---- documento, red y ventana ---- */
FN(dz_title) { return from437(JD->title ? JD->title : "", -1); }
FN(dz_settitle) { JD->title = to437(A(0), 0); dirty(); return CB_UNDEF; }
FN(dz_url) { return cb_mkstr(JC, DM->h.url ? DM->h.url : ""); }
FN(dz_fetch) {   /* [estado, texto, tipo, url final]; lanza error si no hubo respuesta */
    char *m = cstr(A(0)), *u = cstr(A(1)); long bl = 0; char *b = cb_isnullish(A(2)) ? 0 : cb_to_utf8(JC, A(2), &bl); char *ct = cb_isundef(A(3)) ? strdup("") : cstr(A(3));
    char *out = 0; long ol = 0; int st = 0; char rt[128] = "", fin[1100] = "";
    int e = DM->h.http ? DM->h.http(DM->h.u, m, u, b, (long)bl, ct, &out, &ol, &st, rt, sizeof rt, fin, sizeof fin) : -1;
    free(b); free(m); free(ct);
    if (e) { free(out); cbv r = cb_throw(JC, 1, "Failed to fetch %s (%d)", u, e); free(u); return r; }
    cbv a = cb_objv(cb_new_array(JC, 0)); arrset(a, 0, cb_num(st)); arrset(a, 1, cb_str_utf8(JC, out ? out : "", out ? ol : 0)); arrset(a, 2, cb_mkstr(JC, rt)); arrset(a, 3, cb_mkstr(JC, fin[0] ? fin : u));
    free(out); free(u); return a; }
FN(dz_go) { char *u = cstr(A(0)); if (DM->h.go) DM->h.go(DM->h.u, u); free(u); return CB_UNDEF; }
FN(dz_dialog) { int k = 0; (k = (int)cb_to_int(JC, A(0))); char *m = cstr(A(1)); char ans[512] = ""; if (k == 2 && !cb_isundef(A(2))) { char *d = cstr(A(2)); strncpy0(ans, d, sizeof ans); free(d); }
    int ok = DM->h.dialog ? DM->h.dialog(DM->h.u, k, m, ans, sizeof ans) : 1; free(m);
    if (k == 0) return CB_UNDEF; if (k == 1) return cb_bool(ok); return ok ? cb_mkstr(JC, ans) : CB_NULL; }
static void store_path(char *p, int cap) { zdirent e; const char *base = z_stat("/sys", &e) ? "/rd/tmp" : "/sys/ZUNZUN"; char dir[80]; snprintf(dir, 80, "%s/JS-DATOS", base); z_mkdir(dir); snprintf(p, cap, "%s/%s.json", dir, DM->host[0] ? DM->host : "local"); }
FN(dz_storeload) { char p[260]; store_path(p, sizeof p); long n; char *t = z_readfile(p, &n); cbv r = t ? cb_str_utf8(JC, t, n) : cb_mkstr(JC, "{}"); free(t); return r; }
FN(dz_storesave) { char p[260]; store_path(p, sizeof p); long n; char *s = cb_to_utf8(JC, A(0), &n); if (s) { z_writefile(p, s, n); free(s); } else cb_take_exc(JC); return CB_UNDEF; }
FN(dz_now) { return cb_num((double)z_ms()); }
FN(dz_setcur) { DM->cur = NODE(A(0)); return CB_UNDEF; }
FN(dz_cur) { return wrap(DM->cur); }
FN(dz_write) { long n; char *s = cb_to_utf8(JC, A(0), &n); if (!s) return CB_EMPTY; DM->wbuf = realloc(DM->wbuf, DM->wlen + n + 1); memcpy(DM->wbuf + DM->wlen, s, n); DM->wlen += n; DM->wbuf[DM->wlen] = 0; free(s); return CB_UNDEF; }
FN(dz_dirty) { dirty(); return CB_UNDEF; }

static const struct { const char *name; cb_native fn; int n; } NAT[] = {
    { "doc", dz_doc, 0 }, { "protos", dz_protos, 4 }, { "type", dz_type, 1 }, { "tag", dz_tag, 1 }, { "parent", dz_parent, 1 }, { "first", dz_first, 1 }, { "last", dz_last, 1 },
    { "next", dz_next, 1 }, { "prev", dz_prev, 1 }, { "all", dz_all, 1 }, { "text", dz_text, 1 }, { "setText", dz_settext, 2 }, { "attr", dz_attr, 2 }, { "setAttr", dz_setattr, 3 },
    { "rmAttr", dz_rmattr, 2 }, { "attrs", dz_attrs, 1 }, { "create", dz_create, 1 }, { "createText", dz_createtext, 1 }, { "append", dz_append, 2 }, { "insert", dz_insert, 3 },
    { "remove", dz_remove, 1 }, { "clone", dz_clone, 2 }, { "html", dz_html, 2 }, { "setHtml", dz_sethtml, 2 }, { "value", dz_value, 1 }, { "setValue", dz_setvalue, 2 },
    { "checked", dz_checked, 1 }, { "setChecked", dz_setchecked, 2 }, { "sel", dz_sel, 1 }, { "setSel", dz_setsel, 2 }, { "focus", dz_focus, 1 }, { "active", dz_active, 0 },
    { "submit", dz_submit, 1 }, { "rect", dz_rect, 1 }, { "view", dz_view, 0 }, { "scroll", dz_scroll, 1 }, { "title", dz_title, 0 }, { "setTitle", dz_settitle, 1 }, { "url", dz_url, 0 },
    { "fetch", dz_fetch, 4 }, { "go", dz_go, 1 }, { "dialog", dz_dialog, 3 }, { "storeLoad", dz_storeload, 0 }, { "storeSave", dz_storesave, 1 }, { "now", dz_now, 0 },
    { "setCurrent", dz_setcur, 1 }, { "current", dz_cur, 0 }, { "write", dz_write, 1 }, { "dirty", dz_dirty, 0 } };

#include "dom_js.h"   /* DOM_JS: dom.js como cadena (tools/js2h.py) */

/* ---- eventos desde el navegador ---- */
static void report(void) { char e[700]; cbh_exc_text(JC, e, sizeof e); if (DM->h.console) DM->h.console(DM->h.u, 2, e); }
static int fire(struct node *n, const char *type, cbv init) {   /* 1 si se cancelo */
    if (!DM || !n || !cb_isfunc(DM->ev)) return 0;
    cbv args[3] = { wrap(n), cb_mkstr(JC, type), init };
    cbv r = cb_call_v(JC, DM->ev, CB_UNDEF, 3, args); int prevented = 0;
    if (r == CB_EMPTY) report(); else prevented = cb_truthy(r);
    cbh_tick(JC); return prevented; }
static int on_submit(struct html_doc *d, struct node *form) { return fire(form, "submit", CB_UNDEF); }
static void on_change(struct html_doc *d, struct node *n) { fire(n, "input", CB_UNDEF); fire(n, "change", CB_UNDEF); }
static struct node *hit_block(struct node *n, int x, int y) {
    for (struct node *c = n->first; c; c = c->next) if (c->type == N_ELEM && c->s.display != D_NONE) { struct node *h = hit_block(c, x, y); if (h) return h; }
    return n->bh && x >= n->bx && x < n->bx + n->bw && y >= n->y && y < n->y + n->bh ? n : NULL; }
static cbv obj2(const char *k1, cbv v1, const char *k2, cbv v2) { cb_obj *o = cb_new_obj(JC, JC->ObjectP); cb_defp(JC, o, k1, v1, PF_WEC); if (k2) cb_defp(JC, o, k2, v2, PF_WEC); return cb_objv(o); }
int html_js_click(struct html_doc *d, int x, int y) {
    if (!DM || d != JD) return 0; struct node *t = NULL;
    for (int i = d->nops - 1; i >= 0 && !t; i--) { struct op *o = &d->ops[i]; if (o->owner && x >= o->x && x < o->x + o->w && y >= o->y && y < o->y + o->h) t = o->owner; }
    if (!t) t = hit_block(d->root, x, y); if (!t) t = d->root;
    char m[120]; snprintf(m, 120, "[prueba] navjs clic en %s", t->tag ? tag_names[t->tag] : t->tname ? t->tname : "?"); z_log(m);
    return fire(t, "click", obj2("clientX", cb_num(x), "clientY", cb_num(y))) ? 2 : 0; }
int html_js_key(struct html_doc *d, int down, const char *key, int ch) {
    if (!DM || d != JD) return 0; struct node *t = d->focus ? d->focus : find_tag(d->root, T_BODY, 0); if (!t) t = d->root;
    return fire(t, down ? "keydown" : "keyup", obj2("key", cb_mkstr(JC, key), "keyCode", cb_num(ch))); }
void html_js_typed(struct html_doc *d) { if (DM && d == JD && d->focus) fire(d->focus, "input", CB_UNDEF); }
int html_js_on(struct html_doc *d) { return DM && d == JD; }
int html_js_eval(struct html_doc *d, const char *code) {
    if (!DM || d != JD) return -1; char err[600]; int r = cbh_eval(JC, code, (long)strlen(code), "javascript:", 0, 0, err, sizeof err);
    if (r && DM->h.console) DM->h.console(DM->h.u, 2, err); return r; }
long html_js_tick(struct html_doc *d) { return DM && d == JD ? cbh_tick(JC) : -1; }
int html_js_update(struct html_doc *d) {
    if (!DM || d != JD || !DM->dirty) return 0;
    html_relayout(d); return 1; }

/* ---- arranque: scripts de la pagina en orden ---- */
static void print_cb(void *u, int level, const char *s, int n) { if (DM && DM->h.console) DM->h.console(DM->h.u, level, s); }
static void scripts(struct node *n, struct node **L, int *k, int cap) { for (struct node *c = n->first; c && *k < cap; c = c->next) { if (c->type != N_ELEM || c->tag == T_TEMPLATE) continue; if (c->tag == T_SCRIPT) L[(*k)++] = c; else scripts(c, L, k, cap); } }
static void resolve_url(const char *base, const char *rel, char *out, int cap) {
    if (strstr(rel, "://") || !base[0]) { strncpy0(out, rel, cap); return; }
    const char *p = strstr(base, "://"); const char *hs = p ? p + 3 : base; const char *pe = strchr(hs, '/'); int hl = pe ? (int)(pe - base) : (int)strlen(base);
    if (rel[0] == '/' && rel[1] == '/') { int sl = p ? (int)(p - base) + 1 : 0; snprintf(out, cap, "%.*s%s", sl, base, rel); return; }
    if (rel[0] == '/') { snprintf(out, cap, "%.*s%s", hl, base, rel); return; }
    const char *q = strchr(base, '?'); int bl = q ? (int)(q - base) : (int)strlen(base); int ls = bl; while (ls > hl && base[ls - 1] != '/') ls--;
    if (ls <= hl) snprintf(out, cap, "%.*s/%s", hl, base, rel); else snprintf(out, cap, "%.*s%s", ls, base, rel); }
/* modulos: import "url" -> objeto con lo exportado (se evaluan una vez por direccion) */
static cbv import_hook(cb_rt *R, const char *spec, const char *base) {
    char u[1100]; resolve_url(base && strstr(base, "://") ? base : DM->h.url, spec, u, sizeof u);
    cbv key = cb_intern(R, cb_s(cb_str_utf8(R, u, (long)strlen(u)))); cbv have = cb_get(R, DM->modules, key); if (have == CB_EMPTY) return have; if (!cb_isundef(have)) return have;
    char *out = 0; long ol = 0; int st = 0; char rt[128], fin[1100];
    if (!DM->h.http || DM->h.http(DM->h.u, "GET", u, 0, 0, "", &out, &ol, &st, rt, sizeof rt, fin, sizeof fin) || st >= 400) { free(out); return cb_throw(R, 1, "no se pudo cargar el modulo %s", u); }
    cbv ex = cb_objv(cb_new_obj(R, 0)); cb_set(R, DM->modules, key, ex);
    cbv saved = R->exports; R->exports = ex; cbv res; int e = cb_eval(R, out, ol, u, &res, 1); R->exports = saved; free(out);
    if (e) return CB_EMPTY; cb_run_jobs(R); return ex; }
static void run_code(const char *src, long n, const char *name, int module) {
    if (module) { cbv ex = cb_objv(cb_new_obj(JC, 0)); cbv saved = JC->exports; JC->exports = ex; cbv res; int e = cb_eval(JC, src, n, name, &res, 1); JC->exports = saved; if (e) report(); cbh_tick(JC); return; }
    char err[900]; if (cbh_eval(JC, src, n, name, 0, 0, err, sizeof err) && DM->h.console) DM->h.console(DM->h.u, 2, err); }
int html_js_start(struct html_doc *d, const struct html_js_host *h) {
    if (DM) return -1; JD = d; DM = calloc(1, sizeof *DM); DM->h = *h; DM->ev = CB_UNDEF;
    { const char *p = h->url ? strstr(h->url, "://") : 0; int i = 0; if (p) { p += 3; while (*p && *p != '/' && *p != ':' && i < 120) { char ch = *p++; DM->host[i++] = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' ? ch : '_'; } } DM->host[i] = 0; }
    JC = cbh_new(print_cb, 0); if (!JC) { free(DM); DM = 0; JD = 0; return -1; }
    KEEP = cb_objv(cb_new_array(JC, 4)); cb_root(&KEEP);
    DM->wrappers = cb_objv(cb_new_array(JC, 0)); DM->modules = cb_objv(cb_new_obj(JC, 0)); arrset(KEEP, 0, DM->wrappers); arrset(KEEP, 2, DM->modules);
    JC->import_hook = import_hook;
    cb_obj *z = cb_new_obj(JC, JC->ObjectP); for (u32 i = 0; i < sizeof NAT / sizeof *NAT; i++) cb_defn(JC, z, NAT[i].name, NAT[i].fn, NAT[i].n);
    cb_setp(JC, cb_objv(JC->global), "__z", cb_objv(z));
    d->jsd = DM; d->on_submit = on_submit; d->on_change = on_change;
    run_code(DOM_JS, (long)strlen(DOM_JS), "dom.js", 0);
    if (!cb_isfunc(DM->ev)) { if (h->console) h->console(h->u, 2, "dom.js no se pudo cargar"); }
    arrset(KEEP, 3, DM->ev);
    struct node **L = malloc(256 * sizeof *L); int k = 0; scripts(d->root, L, &k, 256);
    for (int i = 0; i < k; i++) {
        struct node *s = L[i]; const char *ty = s->itype ? s->itype : ""; int module = !strcasecmp(ty, "module");
        if (ty[0] && !module && !strstr(ty, "javascript") && !strstr(ty, "ecmascript")) continue;
        DM->cur = s; DM->wlen = 0;
        if (s->src && s->src[0]) {
            char u[1100]; resolve_url(h->url ? h->url : "", s->src, u, sizeof u); char *out = 0; long ol = 0; int st = 0; char rt[128], fin[1100];
            int e = h->http ? h->http(h->u, "GET", u, 0, 0, "", &out, &ol, &st, rt, sizeof rt, fin, sizeof fin) : -1;
            char m[300]; snprintf(m, 300, "[prueba] navjs script %s: %d estado %d %ld bytes", u, e, st, ol); z_log(m);
            if (!e && st < 400) run_code(out, ol, u, module); else if (h->console) { snprintf(m, 300, "No se pudo cargar el script %s", u); h->console(h->u, 2, m); }
            free(out);
        } else if (s->text) run_code(s->text, s->tlen, "<script>", module);
        if (DM->wlen) {   /* document.write: el HTML escrito va justo despues del script */
            struct node *tmp = new_node(d, NULL, N_ELEM, T_DIV); if (tmp) { parse_into(d, tmp, DM->wbuf, (u32)DM->wlen); struct node *ref = s->next; while (tmp->first) { struct node *x = tmp->first; put(s->parent, x, ref); } } DM->wlen = 0; }
    }
    free(L); DM->cur = 0;
    cbv fin = cb_getp(JC, cb_objv(JC->global), "__zloaded");
    if (fin != CB_EMPTY && cb_isfunc(fin)) { if (cb_call_v(JC, fin, CB_UNDEF, 0, 0) == CB_EMPTY) report(); } else if (fin == CB_EMPTY) cb_take_exc(JC);
    cbh_tick(JC);
    char m[120]; snprintf(m, 120, "[prueba] navjs listo: %d scripts", k); z_log(m);
    return 0; }
void html_js_stop(struct html_doc *d) {
    if (!DM || d != JD) return;
    cb_obj *w = cb_o(DM->wrappers); for (u32 i = 0; i < w->n1; i++) { struct node *n = NODE(w->e[i]); if (n) n->js = 0; }
    cb_unroot(&KEEP); KEEP = CB_UNDEF; cbh_free(JC); JC = 0; free(DM->wbuf); free(DM); DM = 0; d->jsd = 0; d->on_submit = 0; d->on_change = 0; JD = 0; }
