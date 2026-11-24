/* Colibri: corrutinas (cada generador o funcion async corre en su propia pila) y salto de pila.
   Cambio de contexto propio en ensamblador x86-64: guarda los registros que conserva quien llama, cambia rsp y vuelve.
   Las pilas se reutilizan. El recolector recorre todas las pilas vivas (cb_coro_scan). */
#include "cbi.h"
#define CSTACK (256 * 1024)
#if defined(__APPLE__)
#define SYM(x) "_" #x
#else
#define SYM(x) #x
#endif
/* void cb_ctx_switch(u64 *save, u64 to): guarda el contexto actual en *save y salta a "to" */
__asm__(".text\n.globl " SYM(cb_ctx_switch) "\n" SYM(cb_ctx_switch) ":\n"
        "  pushq %rbp\n  pushq %rbx\n  pushq %r12\n  pushq %r13\n  pushq %r14\n  pushq %r15\n"
        "  movq %rsp, (%rdi)\n  movq %rsi, %rsp\n"
        "  popq %r15\n  popq %r14\n  popq %r13\n  popq %r12\n  popq %rbx\n  popq %rbp\n  ret\n");
void cb_ctx_switch(u64 *save, u64 to);
struct cb_coro { u8 *stack; u64 sp, back; int done, started; void (*fn)(void *); void *arg; cb_coro *parent, *nextall, **prevall; };
static cb_coro *cur, *all, *pool[16]; static int npool;
static u64 main_sp;   /* rsp guardado de la pila principal mientras corre una corrutina */
int cb_in_coro(void) { return cur != 0; }
cb_coro *cb_coro_current(void) { return cur; }
static void trampoline(void) {
    cb_coro *c = cur; c->fn(c->arg); c->done = 1;
    cb_coro *p = c->parent; cur = p; cb_ctx_switch(&c->sp, p ? p->sp : main_sp); for (;;); }
cb_coro *cb_coro_new(void (*fn)(void *), void *arg) {
    cb_coro *c = calloc(1, sizeof *c); if (!c) return 0;
    c->stack = npool ? (u8 *)pool[--npool] : malloc(CSTACK); if (!c->stack) { free(c); return 0; }
    c->fn = fn; c->arg = arg;
    u64 *top = (u64 *)(((u64)(c->stack + CSTACK) & ~15ULL));
    *--top = 0;                       /* alineacion: al entrar en trampoline rsp+8 debe ser multiplo de 16 */
    *--top = (u64)trampoline;         /* direccion de vuelta del primer cambio */
    for (int i = 0; i < 6; i++) *--top = 0;   /* rbp rbx r12 r13 r14 r15 */
    c->sp = (u64)top;
    c->nextall = all; if (all) all->prevall = &c->nextall; c->prevall = &all; all = c;
    return c; }
int cb_coro_resume(cb_coro *c) {
    if (c->done) return 0;
    c->parent = cur; cur = c;
    u64 *save = c->parent ? &c->parent->sp : &main_sp;
    c->back = 0; cb_ctx_switch(save, c->sp);   /* vuelve aqui cuando c cede o termina (ver cb_coro_yield) */
    return !c->done; }
void cb_coro_yield(void) {
    cb_coro *c = cur; if (!c) return; cb_coro *p = c->parent; cur = p;
    u64 to = p ? p->sp : main_sp; cb_ctx_switch(&c->sp, to); }
void cb_coro_free(cb_coro *c) {
    if (!c) return; if (c == cur) return;
    *c->prevall = c->nextall; if (c->nextall) c->nextall->prevall = c->prevall;
    if (npool < 16) pool[npool++] = (cb_coro *)c->stack; else free(c->stack); free(c); }
void cb_coro_free_all(void) { while (all) { cb_coro *c = all; all = c->nextall; if (all) all->prevall = &all; free(c->stack); free(c); } while (npool) free(pool[--npool]); cur = 0; }
/* pilas para el recolector: las corrutinas suspendidas desde su rsp guardado; la actual desde el rsp de ahora;
   la principal desde su rsp guardado si estamos dentro de una corrutina */
void cb_coro_scan(void (*scan)(const void *, const void *)) {
    u64 here = (u64)&here;
    for (cb_coro *c = all; c; c = c->nextall) { if (c->done) continue; u8 *top = c->stack + CSTACK; if (c == cur) scan((void *)here, top); else if (c->started || c->sp) scan((void *)c->sp, top); }
    if (cur) { extern void *cb_stack_top(void); scan((void *)main_sp, cb_stack_top()); } }
/* limite de pila para la recursion de JavaScript */
char *cb_stack_limit_now(char *main_limit) { return cur ? (char *)cur->stack + 24 * 1024 : main_limit; }
