/* Navegador con JavaScript: lo que el navegador da al DOM (dom.c) y lo que el DOM le ofrece */
#pragma once
struct html_doc;
struct html_js_host {
    void *u;
    const char *url;                                   /* direccion de la pagina */
    /* peticion HTTP (scripts externos, fetch, XMLHttpRequest): 0 si fue bien; *out con malloc */
    int (*http)(void *u, const char *method, const char *url, const char *body, long blen, const char *ctype,
                char **out, long *outl, int *status, char *rtype, int rcap, char *final, int fcap);
    void (*go)(void *u, const char *url);              /* location.href = ... */
    int (*dialog)(void *u, int kind, const char *msg, char *ans, int acap);   /* 0 alert, 1 confirm, 2 prompt; 1 = aceptar */
    void (*console)(void *u, int level, const char *s);
    void (*scroll)(void *u, int y);                    /* window.scrollTo / scrollIntoView */
    int (*view)(void *u, int *w, int *h, int *sy);     /* tamano de la ventana y desplazamiento */
};
int html_js_start(struct html_doc *d, const struct html_js_host *h);   /* ejecuta los <script> y los eventos de carga */
void html_js_stop(struct html_doc *d);                                 /* antes de html_free */
long html_js_tick(struct html_doc *d);                                 /* temporizadores; ms hasta el siguiente (-1 ninguno) */
int html_js_update(struct html_doc *d);                                /* 1 si JavaScript cambio la pagina (ya maquetada) */
int html_js_click(struct html_doc *d, int x, int y);                   /* 2 si un manejador cancelo la accion por defecto */
int html_js_key(struct html_doc *d, int down, const char *key, int ch); /* keydown/keyup; 1 si se cancelo */
void html_js_typed(struct html_doc *d);                                /* tras escribir en un campo: evento input */
int html_js_eval(struct html_doc *d, const char *code);                /* enlaces javascript: */
int html_js_on(struct html_doc *d);
