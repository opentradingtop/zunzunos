/* Colibri: declaraciones internas compartidas por los archivos del motor */
#pragma once
#include "cb.h"
#ifdef CB_HOST
#include <stdio.h>
#include <stdarg.h>
#define cb_snprintf snprintf
#define cb_vsnprintf vsnprintf
#else
#define cb_snprintf snprintf
#define cb_vsnprintf vsnprintf
#endif

/* ---- memoria (cb_mem.c) ---- */
void *cb_alloc(int type, u64 n);
void *cb_blob(u64 bytes);
void *cb_raw(u64 bytes);
u64 big_size(cb_cell *c);
void cb_mem_init(cb_rt *R);
void cb_mem_free(void);
extern int cb_gc_off;
cb_str *cb_str_new(u32 len);
cb_str *cb_flat(cb_str *s);
cb_str *cb_str_ascii(const char *a, u32 n);
cb_str *cb_str_from_utf8(const char *a, long n);
cb_str *cb_concat(cb_str *a, cb_str *b);
cb_str *cb_substr(cb_str *s, u32 a, u32 b);
u32 cb_hash(cb_str *s);
int cb_str_eq(cb_str *a, cb_str *b);
int cb_str_cmp(cb_str *a, cb_str *b);
int cb_str_eqa(cb_str *a, const char *s);
cb_str *cb_atom_str(cb_str *s);
void cb_atoms_mark(void (*mark)(cb_cell *));
void cb_atoms_free(void);
int cb_in_coro(void);

/* ---- arbol del programa (cb_parse.c) ---- */
typedef struct node node;
typedef struct fninfo fninfo;
typedef struct scope scope;
enum {
    N_NUM = 1, N_STR, N_TMPL, N_REGEX, N_TRUE, N_FALSE, N_NULL, N_THIS, N_SUPER, N_NEWTARGET, N_ID, N_ARR, N_HOLE, N_OBJ, N_PROP,
    N_FUNC, N_CLASS, N_MEMBERDEF, N_UNARY, N_UPDATE, N_BIN, N_LOGIC, N_ASSIGN, N_COND, N_CALL, N_NEW, N_INDEX, N_DOT, N_PRIVDOT,
    N_SEQ, N_SPREAD, N_YIELD, N_AWAIT, N_TAGGED, N_OPTCHAIN, N_ARRPAT, N_OBJPAT, N_PROPPAT, N_DEFAULT, N_REST, N_PRIVIN, N_IMPORTMETA, N_DYNIMPORT,
    N_VAR, N_DECL, N_EXPR, N_BLOCK, N_IF, N_FOR, N_FORIN, N_FOROF, N_WHILE, N_DOWHILE, N_RETURN, N_BREAK, N_CONTINUE, N_THROW,
    N_TRY, N_SWITCH, N_CASE, N_LABEL, N_EMPTY, N_FUNCDECL, N_CLASSDECL, N_DEBUGGER, N_IMPORT, N_EXPORT, N_STATICBLOCK
};
/* operadores (op) */
enum { O_ADD = 1, O_SUB, O_MUL, O_DIV, O_MOD, O_EXP, O_SHL, O_SAR, O_SHR, O_AND, O_OR, O_XOR, O_EQ, O_NE, O_SEQ, O_SNE,
       O_LT, O_GT, O_LE, O_GE, O_IN, O_INSTANCEOF, O_LAND, O_LOR, O_NULLISH, O_NOT, O_NEG, O_PLUS, O_BITNOT, O_TYPEOF, O_VOID,
       O_DELETE, O_INC, O_DEC, O_ASSIGN };
enum { VK_VAR = 1, VK_LET, VK_CONST };
enum { FK_ARROW = 1, FK_METHOD = 2, FK_GET = 4, FK_SET = 8, FK_CTOR = 16, FK_DERIVED = 32, FK_ASYNC = 64, FK_GEN = 128, FK_CLASS = 256,
       FK_STATIC = 512, FK_EXPR = 1024, FK_STRICT = 2048, FK_FIELDINIT = 4096 };
struct node {
    u16 k, op; u32 line;
    node *a, *b, *c, *d, *next;
    cbv v;                        /* literal o nombre (clave internada) */
    int hops, slot;               /* variable resuelta: saltos de entorno y posicion (-1: global por nombre) */
    u32 flags;                    /* N_PROP/N_MEMBERDEF: 1 calculada, 2 corta, 4 estatica, 8 metodo, 16 get, 32 set, 64 privada, 128 campo, 256 ...expansion; N_CALL/N_INDEX/N_DOT: 1 opcional */
    fninfo *fn;                   /* N_FUNC / N_CLASS */
    scope *sc;                    /* bloques, for, catch, switch: su ambito (si crea entorno) */
};
struct scope {                    /* un ambito con nombres */
    scope *up; fninfo *fn; int kind;   /* 0 funcion, 1 bloque, 2 global, 3 catch, 4 clase */
    cbv *names; u8 *nkind; node **funcs; int n, cap, nf, capf;
    int env;                      /* crea entorno en tiempo de ejecucion */
};
struct fninfo {
    u32 flags; cbv name; node *params; node *body; scope *sc; int nparams, simple, uses_args, uses_this, has_eval;
    int this_slot, nt_slot, args_slot, home_slot;   /* posiciones ocultas en el entorno de la funcion */
    const char *src; u32 src_start, src_end; const char *file;
    node *fields;                 /* clases: campos de instancia (N_MEMBERDEF) */
    int length;                   /* parametros antes del primero con valor por defecto */
};
node *cb_parse(cb_rt *R, const char *src, long len, const char *file, int module, char *err, int ecap);

/* ---- objetos (cb_obj.c) ---- */
cb_prop *cb_own(cb_obj *o, cbv key);
cbv cb_key(cb_rt *R, cbv v);                         /* ToPropertyKey: clave internada o simbolo (CB_EMPTY si lanzo) */
cbv cb_idx_key(cb_rt *R, u32 i);
int cb_key_index(cbv key, u32 *i);                   /* la clave es un indice de array */
cbv cb_get_proto_chain(cb_rt *R, cb_obj *o, cbv key, cbv recv);
int cb_put(cb_rt *R, cbv o, cbv key, cbv v, cbv recv, int strict);
int cb_del(cb_rt *R, cbv o, cbv key, int strict);
int cb_has(cb_rt *R, cbv o, cbv key);
int cb_add_prop(cb_rt *R, cb_obj *o, cbv key, cbv v, u32 flags);
void cb_arr_push(cb_rt *R, cb_obj *a, cbv v);
int cb_arr_set(cb_rt *R, cb_obj *a, u32 i, cbv v);
void cb_arr_setlen(cb_rt *R, cb_obj *a, u32 n);
cbv cb_own_keys(cb_rt *R, cbv o, int which);         /* 1 cadenas, 2 simbolos, 3 ambos; 4: solo enumerables. Devuelve array */
cbv cb_to_prim(cb_rt *R, cbv v, int hint);           /* 0 default, 1 number, 2 string */
cb_obj *cb_to_obj(cb_rt *R, cbv v);
int cb_loose_eq(cb_rt *R, cbv a, cbv b);
int cb_strict_eq(cbv a, cbv b);
int cb_same_value(cbv a, cbv b, int zero);
cbv cb_typeof(cb_rt *R, cbv v);
cbv cb_num_to_str(cb_rt *R, double d);
cbv cb_num_to_str_radix(cb_rt *R, double d, int radix);
double cb_str_to_num(cb_str *s);
double cb_to_int(cb_rt *R, cbv v);                   /* ToIntegerOrInfinity */
int32_t cb_to_i32(double d);
u32 cb_to_u32(double d);
cbv cb_instance_of(cb_rt *R, cbv v, cbv c);
cbv cb_get_iter(cb_rt *R, cbv v, int async);         /* objeto iterador */
int cb_iter_next(cb_rt *R, cbv it, cbv *val);        /* 1 valor, 0 fin, -1 excepcion */
void cb_iter_close(cb_rt *R, cbv it);
cbv cb_species_array(cb_rt *R, u32 n);
cbv cb_new_func(cb_rt *R, node *fnode, cb_env *env, cbv home);
cbv cb_construct(cb_rt *R, cbv f, int argc, cbv *argv, cbv newtarget);
cbv cb_call_v(cb_rt *R, cbv f, cbv thisv, int argc, cbv *argv);   /* CB_EMPTY si lanzo */
cbv cb_invoke(cb_rt *R, cbv o, const char *m, int argc, cbv *argv);
int cb_is_ctor(cbv v);
cbv cb_list_to_array(cb_rt *R, int n, cbv *v);
cbv cb_array_like(cb_rt *R, cbv v, u32 *n, cbv **out);   /* para apply/Reflect */
cbv cb_str_from_cp(cb_rt *R, u32 cp);
cbv cb_mkstr(cb_rt *R, const char *s);                     /* cadena ASCII/UTF-8 normal (no internada) */
cbv cb_sym_new(cb_rt *R, cbv desc);
extern cbv S_iterator, S_asyncIterator, S_hasInstance, S_toPrimitive, S_toStringTag, S_species, S_isConcatSpreadable, S_unscopables, S_match, S_matchAll, S_replace, S_search, S_split;
extern cbv A_length, A_prototype, A_constructor, A_name, A_message, A_value, A_done, A_next, A_get, A_set, A_toString, A_valueOf, A_then, A_writable, A_enumerable, A_configurable, A_lastIndex, A_index, A_input, A_groups, A_stack, A_cause, A_callee, A_return, A_throw, A_raw, A_default;

/* ---- interprete (cb_run.c) ---- */
cbv cb_call_func(cb_rt *R, cb_obj *f, cbv thisv, int argc, cbv *argv, cbv newtarget);
cbv cb_run_script(cb_rt *R, node *prog);
void cb_enqueue(cb_rt *R, cbv fn, cbv a, cbv b);
cbv cb_promise_new(cb_rt *R, cbv *resolve, cbv *reject);
void cb_promise_settle(cb_rt *R, cbv p, int ok, cbv v);
cbv cb_promise_resolve(cb_rt *R, cbv v);
cbv cb_promise_then(cb_rt *R, cbv p, cbv onok, cbv onerr);
cbv cb_gen_resume(cb_rt *R, cb_obj *g, int mode, cbv v);  /* mode 0 next, 1 throw, 2 return; devuelve {value,done} */
extern cbv (*cb_import_hook)(cb_rt *R, cbv spec);
cbv cb_ctor_this(cb_rt *R, cbv newtarget, cb_obj *defproto);   /* objeto nuevo con el prototipo de newtarget */

/* ---- biblioteca (cb_lib*.c) ---- */
void cb_lib_init(cb_rt *R);
void cb_lib2_init(cb_rt *R);
void cb_lib3_init(cb_rt *R);
cbv cb_regexp_new(cb_rt *R, cbv pattern, cbv flags);
double cb_date_now(void);
double cb_make_time(double y, double mo, double d, double h, double mi, double s, double ms);
cbv cb_err_obj(cb_rt *R, int kind, const char *msg);
int cb_throwing(cb_rt *R);
void cb_print(cb_rt *R, int level, const char *s, long n);

/* ---- BigInt (cb_big.c) ---- */
int cb_isbig(cbv v);
cbv cb_big_from_str(cb_rt *R, cb_str *s, int allow_ws);
cbv cb_big_from_double(cb_rt *R, double d);
double cb_big_to_double(cbv v);
cbv cb_big_to_str(cb_rt *R, cbv v, int radix);
int cb_big_cmp(cbv a, cbv b);
int cb_big_cmp_num(cbv a, double d);
int cb_big_truthy(cbv v);
u32 cb_big_hash(cbv v);
cbv cb_big_op(cb_rt *R, int op, cbv a, cbv b);
cbv cb_to_big(cb_rt *R, cbv v);
void cb_big_init(cb_rt *R);

/* ---- corrutinas (cb_coro.c) ---- */
typedef struct cb_coro cb_coro;
cb_coro *cb_coro_new(void (*fn)(void *), void *arg);
int cb_coro_resume(cb_coro *c);        /* 1 si sigue suspendida, 0 si termino */
void cb_coro_yield(void);
void cb_coro_free(cb_coro *c);
cb_coro *cb_coro_current(void);
void cb_coro_free_all(void);

/* ---- excepciones de C: try/catch con setjmp propio ---- */
typedef struct { u64 r[8]; } cb_jmp;
int cb_setjmp(cb_jmp *j) __attribute__((returns_twice));
__attribute__((noreturn)) void cb_longjmp(cb_jmp *j, int v);
struct cb_try { cb_try *up; cb_jmp j; };
