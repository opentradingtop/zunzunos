/* Colibri: el motor JavaScript de ZunzunOS. Codigo propio, escrito desde cero.
   Diseno:
   - Valores de 64 bits con "NaN boxing": los numeros son double tal cual; el resto lleva una etiqueta en los 16 bits
     altos (indefinido, nulo, booleano, cadena, simbolo, objeto) y un puntero en los 48 bajos.
   - Memoria: celdas en paginas de 64 KB por tamanos; recolector de marcado y barrido que busca punteros en la pila de
     C de forma conservadora (asi el codigo en C no tiene que registrar sus variables).
   - Cadenas en UTF-16 (como pide el lenguaje), con cuerdas para que s += x no sea cuadratico; claves internadas.
   - Programa: analizador a arbol, resolucion de variables a (saltos, posicion) e interprete que recorre el arbol.
   - Generadores y funciones async: cada uno corre en su propia pila (corrutinas), asi el interprete sigue siendo
     recursivo y sencillo.
   Ver cb_*.c. Entrada publica: cb_new, cb_eval, cb_call, cb_free y las funciones cb_* de abajo. */
#pragma once
#ifdef CB_HOST
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64; typedef int64_t i64;
#else
#include "z.h"
typedef signed char int8_t; typedef short int16_t; typedef int int32_t; typedef long int64_t;
#endif

typedef u64 cbv;
typedef struct cb_cell { u8 type, mark, sc, flags; u32 aux; } cb_cell;
typedef struct cb_str cb_str;
typedef struct cb_obj cb_obj;
typedef struct cb_env cb_env;

enum { CT_FREE, CT_STR, CT_SYM, CT_OBJ, CT_ENV, CT_ACC, CT_BLOB, CT_BIG };
#define TAG_UNDEF 0xFFF9ULL
#define TAG_NULL  0xFFFAULL
#define TAG_BOOL  0xFFFBULL
#define TAG_STR   0xFFFCULL
#define TAG_SYM   0xFFFDULL
#define TAG_OBJ   0xFFFEULL
#define TAG_SPEC  0xFFFFULL
#define CB_TAG(v) ((v) >> 48)
#define CB_UNDEF ((cbv)TAG_UNDEF << 48)
#define CB_NULL  ((cbv)TAG_NULL << 48)
#define CB_FALSE ((cbv)TAG_BOOL << 48)
#define CB_TRUE  (((cbv)TAG_BOOL << 48) | 1)
#define CB_EMPTY ((cbv)TAG_SPEC << 48)          /* hueco de array, variable aun sin iniciar (TDZ) */
#define CB_PTR(v) ((void *)((v) & 0xFFFFFFFFFFFFULL))
static inline int cb_isnum(cbv v) { return CB_TAG(v) < TAG_UNDEF; }
static inline double cb_d(cbv v) { union { cbv u; double d; } c = { v }; return c.d; }
static inline cbv cb_num(double d) { union { double d; cbv u; } c = { d }; if (CB_TAG(c.u) >= TAG_UNDEF) c.u = 0x7FF8000000000000ULL; return c.u; }
static inline cbv cb_bool(int b) { return b ? CB_TRUE : CB_FALSE; }
static inline int cb_isstr(cbv v) { return CB_TAG(v) == TAG_STR; }
static inline int cb_issym(cbv v) { return CB_TAG(v) == TAG_SYM; }
static inline int cb_isobj(cbv v) { return CB_TAG(v) == TAG_OBJ; }
static inline int cb_isundef(cbv v) { return v == CB_UNDEF; }
static inline int cb_isnull(cbv v) { return v == CB_NULL; }
static inline int cb_isnullish(cbv v) { return v == CB_UNDEF || v == CB_NULL; }
static inline int cb_isbool(cbv v) { return CB_TAG(v) == TAG_BOOL; }
static inline cbv cb_strv(cb_str *s) { return ((cbv)TAG_STR << 48) | (u64)s; }
static inline cbv cb_objv(cb_obj *o) { return ((cbv)TAG_OBJ << 48) | (u64)o; }
static inline cb_str *cb_s(cbv v) { return (cb_str *)CB_PTR(v); }
static inline cb_obj *cb_o(cbv v) { return (cb_obj *)CB_PTR(v); }

/* ---- cadenas ---- */
struct cb_str { cb_cell h; u32 len, hash; u8 rope, atom, ascii, pad; cb_str *l, *r; u16 c[]; };   /* rope 1: l+r; 2: reenviada a l (ya plana) */
typedef struct { cb_cell h; cb_str *desc; u32 id; u8 reg; } cb_sym;

/* ---- objetos ---- */
enum { PF_W = 1, PF_E = 2, PF_C = 4, PF_ACC = 8, PF_DEL = 16 };
#define PF_WEC (PF_W | PF_E | PF_C)
typedef struct { cbv key, val; u32 flags, next; } cb_prop;
typedef struct { cb_cell h; cbv get, set; } cb_acc;
enum { OC_OBJECT, OC_ARRAY, OC_FUNC, OC_NATIVE, OC_BOUND, OC_ERROR, OC_BOOL, OC_NUMBER, OC_STRING, OC_SYMBOL, OC_DATE, OC_REGEXP,
       OC_MAP, OC_SET, OC_WEAKMAP, OC_WEAKSET, OC_PROMISE, OC_PROXY, OC_ABUF, OC_TYPED, OC_DVIEW, OC_GEN, OC_ARGS, OC_ITER, OC_HOST };
typedef struct cb_rt cb_rt;
typedef cbv (*cb_native)(cb_rt *R, cbv thisv, int argc, cbv *argv, cb_obj *fn);
typedef struct cb_map cb_map;
struct cb_obj {
    cb_cell h;                   /* h.aux: clase (OC_*) */
    cb_obj *proto;
    cb_prop *props; u32 np, pcap; u32 *idx; u32 icap; u32 ndel;
    u8 cls, ext, sealed, fk;     /* fk: tipo de funcion o de matriz tipada */
    u32 n1, n2;                  /* array: longitud y capacidad; typed: desplazamiento y longitud... */
    cbv *e;                      /* array: elementos */
    void *p1, *p2;               /* funcion: arbol y entorno; nativo: puntero C; mapa; corrutina... */
    cbv v1, v2, v3;              /* datos de cada clase (this ligado, objeto casa, objetivo de un proxy...) */
    double d;
};
struct cb_env { cb_cell h; cb_env *up; u32 n; cbv s[]; };

/* ---- tiempo de ejecucion ---- */
typedef struct cb_try cb_try;
typedef struct cb_coro cb_coro;
struct cb_rt {
    cb_obj *global, *lexical_holder;
    cb_env *genv;                         /* entorno de let/const/class de nivel superior (compartido por los scripts) */
    cb_obj *ObjectP, *FunctionP, *ArrayP, *StringP, *NumberP, *BooleanP, *SymbolP, *ErrorP, *DateP, *RegExpP, *MapP, *SetP,
           *WeakMapP, *WeakSetP, *PromiseP, *IterP, *ArrayIterP, *GenP, *AsyncGenP, *ABufP, *TypedP, *DViewP, *ProxyC, *BigIntP;
    cb_obj *TypedPs[12];
    cb_obj *ErrP[8];                      /* Error, TypeError, RangeError, SyntaxError, ReferenceError, EvalError, URIError, AggregateError */
    cb_obj *Promise, *ArrayC, *ObjectC, *FunctionC;
    cbv exc; int has_exc;                 /* excepcion en curso */
    cb_try *tryp;
    char *stack_limit;                    /* para "Maximum call stack size exceeded" */
    cb_coro *co;                          /* corrutina actual (0: la pila principal) */
    void (*print)(void *u, int level, const char *s, int n); void *pu;
    void *user;
    /* tareas (promesas) */
    cbv *jobs; u32 njobs, capjobs, jhead;
    u32 depth;
    void *frames;                         /* pila de llamadas (para error.stack) */
    cbv nt;                               /* new.target de la llamada nativa en curso */
    void *curgen;                         /* generador o funcion async en curso */
    cbv exports;                          /* modulo en curso */
    cbv (*import_hook)(cb_rt *R, const char *spec, const char *base);   /* carga un modulo: devuelve su objeto de exportaciones */
};

/* ---- API ---- */
cb_rt *cb_new(void);
void cb_free(cb_rt *R);
/* evalua un script. 0 bien (*res con el valor), -1 excepcion (R->exc). */
int cb_eval(cb_rt *R, const char *src, long len, const char *name, cbv *res, int module);
int cb_call(cb_rt *R, cbv f, cbv thisv, int argc, cbv *argv, cbv *res);       /* 0 bien, -1 excepcion */
int cb_run_jobs(cb_rt *R);                                                  /* promesas pendientes; devuelve cuantas */
cbv cb_take_exc(cb_rt *R);
/* valores */
cbv cb_str_utf8(cb_rt *R, const char *s, long n);
char *cb_to_utf8(cb_rt *R, cbv v, long *n);                                 /* malloc; convierte con ToString (puede lanzar: 0) */
char *cb_str_utf8z(cb_str *s, long *n);                                     /* malloc; cadena tal cual */
double cb_to_num(cb_rt *R, cbv v);
cbv cb_to_str(cb_rt *R, cbv v);                                              /* CB_EMPTY si lanzo */
int cb_truthy(cbv v);
cb_obj *cb_new_obj(cb_rt *R, cb_obj *proto);
cb_obj *cb_new_array(cb_rt *R, u32 n);
cbv cb_new_native(cb_rt *R, const char *name, cb_native f, int nargs);
cbv cb_get(cb_rt *R, cbv o, cbv key);                                       /* CB_EMPTY si lanzo */
cbv cb_getp(cb_rt *R, cbv o, const char *name);
int cb_set(cb_rt *R, cbv o, cbv key, cbv v);                                /* -1 si lanzo */
int cb_setp(cb_rt *R, cbv o, const char *name, cbv v);
int cb_define(cb_rt *R, cb_obj *o, cbv key, cbv v, u32 flags);
void cb_defp(cb_rt *R, cb_obj *o, const char *name, cbv v, u32 flags);
void cb_defn(cb_rt *R, cb_obj *o, const char *name, cb_native f, int nargs);  /* metodo nativo (no enumerable) */
void cb_defgs(cb_rt *R, cb_obj *o, const char *name, cb_native get, cb_native set);
cbv cb_atom(cb_rt *R, const char *s);                                       /* clave internada */
cbv cb_intern(cb_rt *R, cb_str *s);
cbv cb_throw(cb_rt *R, int kind, const char *fmt, ...);                     /* kind: 0 Error, 1 Type, 2 Range, 3 Syntax, 4 Reference; devuelve CB_EMPTY */
cbv cb_throwv(cb_rt *R, cbv v);
int cb_isfunc(cbv v);
int cb_iserror(cbv v);
cbv cb_json_str(cb_rt *R, cbv v);                                           /* JSON.stringify(v) o undefined */
void cb_gc(cb_rt *R);
void cb_set_stack_top(void *top);
void cb_root(cbv *p);                                                       /* variable de C (global) que el recolector debe ver */
void cb_unroot(cbv *p);
void *cb_opaque(cbv v, int cls);                                            /* objeto de la aplicacion (OC_HOST) */
cbv cb_new_host(cb_rt *R, cb_obj *proto, void *p);
cb_obj *cb_new_err(cb_rt *R, int kind, cbv msg);
