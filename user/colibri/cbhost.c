/* Colibri para los programas de ZunzunOS: ver cbhost.h */
#include "cbi.h"
#include "cbhost.h"
cbv cb_unhandled_take(cb_rt *R);
int cb_fmt_num(double d, char *o);
typedef struct { cbh_print_fn print; void *user; } hostp;
static cbv timers;   /* array: [id, vence, cada, fn, ...] */
static int tm_ids;
static hostp *H(cb_rt *R) { return R->pu; }
void cbh_show(cb_rt *R, cbv v, char *out, int cap) {
    out[0] = 0; cbv s = CB_UNDEF;
    if (cb_isobj(v) && !cb_isfunc(v) && !cb_iserror(v)) { s = cb_json_str(R, v); if (s == CB_EMPTY) { cb_take_exc(R); s = CB_UNDEF; } }
    if (!cb_isstr(s)) { if (cb_iserror(v)) { s = cb_invoke(R, v, "toString", 0, 0); if (s == CB_EMPTY) { cb_take_exc(R); s = CB_UNDEF; } } }
    if (!cb_isstr(s)) { s = cb_to_str(R, v); if (s == CB_EMPTY) { cb_take_exc(R); strncpy0(out, "[?]", cap); return; } }
    long n; char *u = cb_str_utf8z(cb_s(s), &n); strncpy0(out, u ? u : "", cap); free(u);
    if (cb_isbig(v)) { int k = strlen(out); if (k + 1 < cap) { out[k] = 'n'; out[k + 1] = 0; } } }
static cbv con_out(cb_rt *R, int argc, cbv *argv, int level) {
    hostp *h = H(R); char *b = malloc(16384); if (!b) return CB_UNDEF; int n = 0; b[0] = 0;
    for (int i = 0; i < argc && n < 16000; i++) { if (i) b[n++] = ' '; cbh_show(R, argv[i], b + n, 16384 - n); n += strlen(b + n); }
    if (h && h->print) h->print(h->user, level, b, n); free(b); return CB_UNDEF; }
static cbv con_log(cb_rt *R, cbv t, int argc, cbv *argv, cb_obj *fn) { return con_out(R, argc, argv, 0); }
static cbv con_warn(cb_rt *R, cbv t, int argc, cbv *argv, cb_obj *fn) { return con_out(R, argc, argv, 1); }
static cbv con_err(cb_rt *R, cbv t, int argc, cbv *argv, cb_obj *fn) { return con_out(R, argc, argv, 2); }
static cbv set_timer(cb_rt *R, cbv t, int argc, cbv *argv, cb_obj *fn) {
    if (argc < 1 || !cb_isfunc(argv[0])) return cb_throw(R, 1, "se esperaba una funcion");
    double ms = argc > 1 ? cb_to_num(R, argv[1]) : 0; if (R->has_exc) return CB_EMPTY; if (!(ms >= 0)) ms = 0; if (ms > 1e9) ms = 1e9;
    cb_obj *L = cb_o(timers); int id = ++tm_ids; double every = fn->n2 ? (ms < 4 ? 4 : ms) : 0;
    cb_arr_push(R, L, cb_num(id)); cb_arr_push(R, L, cb_num((double)z_ms() + ms)); cb_arr_push(R, L, cb_num(every)); cb_arr_push(R, L, argv[0]);
    return cb_num(id); }
static cbv clear_timer(cb_rt *R, cbv t, int argc, cbv *argv, cb_obj *fn) {
    double id = argc ? cb_to_num(R, argv[0]) : 0; cb_obj *L = cb_o(timers);
    for (u32 i = 0; i + 3 < L->n1; i += 4) if (cb_d(L->e[i]) == id) { L->e[i] = cb_num(-1); L->e[i + 3] = CB_UNDEF; }
    return CB_UNDEF; }
cb_rt *cbh_new(cbh_print_fn print, void *user) {
    cb_rt *R = cb_new(); if (!R) return 0;
    hostp *h = calloc(1, sizeof *h); h->print = print; h->user = user; R->pu = h;
    R->print = 0; timers = cb_objv(cb_new_array(R, 0)); cb_root(&timers); tm_ids = 0;
    cbv con = cb_objv(cb_new_obj(R, R->ObjectP)); cb_setp(R, cb_objv(R->global), "console", con);
    cb_defn(R, cb_o(con), "log", con_log, 1); cb_defn(R, cb_o(con), "info", con_log, 1); cb_defn(R, cb_o(con), "debug", con_log, 1);
    cb_defn(R, cb_o(con), "warn", con_warn, 1); cb_defn(R, cb_o(con), "error", con_err, 1); cb_defn(R, cb_o(con), "trace", con_log, 1);
    cb_defn(R, R->global, "setTimeout", set_timer, 2); cbv si = cb_new_native(R, "setInterval", set_timer, 2); cb_o(si)->n2 = 1; cb_setp(R, cb_objv(R->global), "setInterval", si);
    cb_defn(R, R->global, "clearTimeout", clear_timer, 1); cb_defn(R, R->global, "clearInterval", clear_timer, 1);
    return R; }
void cbh_free(cb_rt *R) { if (!R) return; free(R->pu); timers = CB_UNDEF; cb_free(R); }
void cbh_exc_text(cb_rt *R, char *err, int ecap) {
    cbv e = cb_take_exc(R); cbh_show(R, e, err, ecap);
    if (cb_iserror(e)) { cbv st = cb_get(R, e, A_stack); if (st == CB_EMPTY) cb_take_exc(R); else if (cb_isstr(st)) { long n; char *u = cb_str_utf8z(cb_s(st), &n); char *nl = u ? strchr(u, '\n') : 0; if (nl) { int k = strlen(err); snprintf(err + k, ecap - k, "%s", nl); } free(u); } } }
static void report_unhandled(cb_rt *R) {
    cbv u = cb_unhandled_take(R); if (!cb_isobj(u)) return; cb_obj *L = cb_o(u); hostp *h = H(R);
    for (u32 i = 0; i < L->n1; i++) { cb_obj *p = cb_o(L->e[i]); char b[700] = "Uncaught (in promise) "; cbh_show(R, p->v1, b + 22, sizeof b - 22); if (h && h->print) h->print(h->user, 2, b, strlen(b)); } }
static void jobs(cb_rt *R) { cb_run_jobs(R); report_unhandled(R); }
int cbh_eval(cb_rt *R, const char *src, long len, const char *name, char *res, int rcap, char *err, int ecap) {
    if (err && ecap) err[0] = 0; if (res && rcap) res[0] = 0; cbv v;
    if (cb_eval(R, src, len, name, &v, 0)) { if (err) cbh_exc_text(R, err, ecap); else cb_take_exc(R); jobs(R); return -1; }
    if (res && !cb_isundef(v)) cbh_show(R, v, res, rcap);
    jobs(R); return 0; }
long cbh_tick(cb_rt *R) {
    cb_obj *L = cb_o(timers); hostp *h = H(R); double now = (double)z_ms();
    for (int round = 0; round < 50; round++) {
        long best = -1; for (u32 i = 0; i + 3 < L->n1; i += 4) if (cb_d(L->e[i]) > 0 && cb_d(L->e[i + 1]) <= now && (best < 0 || cb_d(L->e[i + 1]) < cb_d(L->e[best + 1]))) best = (long)i;
        if (best < 0) break; cbv fn = L->e[best + 3];
        if (cb_d(L->e[best + 2]) > 0) L->e[best + 1] = cb_num(now + cb_d(L->e[best + 2])); else { L->e[best] = cb_num(-1); L->e[best + 3] = CB_UNDEF; }
        cbv r = cb_call_v(R, fn, CB_UNDEF, 0, 0); if (r == CB_EMPTY) { char e[700]; cbh_exc_text(R, e, sizeof e); if (h && h->print) h->print(h->user, 2, e, strlen(e)); }
        jobs(R); now = (double)z_ms(); }
    jobs(R);
    /* compactar la lista de temporizadores */
    u32 k = 0; for (u32 i = 0; i + 3 < L->n1; i += 4) if (cb_d(L->e[i]) > 0) { for (int j = 0; j < 4; j++) L->e[k + j] = L->e[i + j]; k += 4; } L->n1 = k;
    long next = -1; for (u32 i = 0; i + 3 < L->n1; i += 4) { long d = (long)(cb_d(L->e[i + 1]) - now); if (d < 0) d = 0; if (next < 0 || d < next) next = d; }
    return next; }
