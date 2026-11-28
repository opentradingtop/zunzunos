#!/usr/bin/env python3
"""Genera un z.h para compilar el motor de Alquimia en el anfitrion (Linux o macOS, x86-64 o ARM): copia user/lib/z.h
cambiando las llamadas al sistema de ZunzunOS (funciones static inline) por funciones normales, que da shim.c con
la libc, y quitando el ensamblador de x86 (int 0x40, rdtsc) para que compile tambien en un Mac con Apple Silicon."""
import re, sys
src, dst = sys.argv[1], sys.argv[2]
txt = open(src, encoding='utf-8').read()
# el bloque de sc5 y z_cycles (ARM y x86) se cambia por uno que compila en cualquier procesador
a = txt.index('#ifdef __arm__\n/* llamada al sistema')
b = txt.index('#endif', txt.index('__builtin_ia32_rdtsc', a)) + len('#endif')
txt = txt[:a] + ('static inline long sc5(long n, long a, long b, long c, long d, long e) { (void)n; (void)a; (void)b; (void)c; (void)d; (void)e; return -38; }\n'
                 'u64 zh_cycles(void);\nstatic inline u64 z_cycles(void) { return zh_cycles(); }') + txt[b:]
n = 0
def proto(m):
    global n
    if m.group(2) in ('sc5', 'z_cycles', 'z_print_arg'): return m.group(0)
    n += 1; return m.group(1).strip() + ';'
# funciones static inline de una linea: se quedan en declaracion
txt = re.sub(r'static inline ([^{;\n]*?\b(\w+)\s*\([^{\n]*?\))\s*\{[^\n]*\}', proto, txt)
open(dst, 'w', encoding='utf-8').write('/* generado por tools/alquimia/mkhost.py a partir de user/lib/z.h */\n' + txt)
print(dst, n)
