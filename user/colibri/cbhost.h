/* Colibri para los programas de ZunzunOS: motor con console y temporizadores, y ejecucion con el error en texto */
#pragma once
#include "cb.h"
typedef void (*cbh_print_fn)(void *user, int level, const char *s, int n);   /* level 0 log, 1 aviso, 2 error */
cb_rt *cbh_new(cbh_print_fn print, void *user);
void cbh_free(cb_rt *R);
/* 0 bien; -1 error (texto en err, con la pila). res: el valor final en texto (si no es undefined) */
int cbh_eval(cb_rt *R, const char *src, long len, const char *name, char *res, int rcap, char *err, int ecap);
void cbh_show(cb_rt *R, cbv v, char *out, int cap);
void cbh_exc_text(cb_rt *R, char *err, int ecap);           /* toma la excepcion en curso */
long cbh_tick(cb_rt *R);                                    /* temporizadores y promesas; ms al siguiente (-1: ninguno) */
