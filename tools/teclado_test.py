#!/usr/bin/env python3
"""Prueba del teclado: repeticion al mantener una tecla (navegador), deshacer y rehacer en un cuadro de texto
(LUMEN) y atajos del sistema: Win (menu Inicio), Win+E (Archivos), Ctrl+W (cerrar) y Win+D (escritorio).
Uso: teclado_test.py DIR_SALIDA "args de qemu" """
import os, re, subprocess, sys
out, q = sys.argv[1:3]
MH = 42 + 9 * 28 + 14; MY = 800 - 36 - MH - 2
FAM = [['rescate', 'files', 'discos', 'papelera', 'restaurar', 'instalar'], ['escritor', 'hoja', 'diapos', 'agenda', 'editor', 'calc', 'pdf'], ['browser', 'apps', 'correo', 'lumen', 'redlocal', 'navjs', 'jsconsola', 'nube'], ['viewer', 'player', 'grabadora', 'paint', 'pantalla', 'video', 'radio'], ['snake', 'mines', 'blocks', 'enjambre'], ['config', 'impresoras', 'term', 'sysmon', 'acerca']]
k = [0]
def M(b, h=500):
    f = [i for i, l in enumerate(FAM) if b in l][0]; L = FAM[f]; i = L.index(b); y0 = max(42, min(42 + f * 28, MH - 6 - len(L) * 28)); fy = MY + 42 + 28 * f + 14; ay = MY + y0 + 28 * i + 14
    k[0] += 1; c = k[0] % 8; y = 50 + 26 * c; y = 26 if y + h + 40 > 800 else y
    return (60 + 28 * c, y), ['click:50,782', 'sleep:0.6', f'move:100,{fy}', 'sleep:0.5', f'move:245,{fy}', 'sleep:0.3', f'move:350,{ay}', 'sleep:0.3', f'click:350,{ay}', 'sleep:2']
s = ['wait:escritorio listo', 'sleep:1']
(bx, by), st = M('browser', 720)
s += st + ['key:ctrl-l', 'sleep:0.3', 'type:/rd/', 'hold:a,1.6', 'sleep:0.3', 'key:ret', 'wait:navegador /rd/aa', 'sleep:0.8', 'shot:k-repeticion', 'key:alt-f4', 'sleep:0.8']
_, st = M('lumen', 680)
s += st + ['wait:lumen listo', 'sleep:0.8', 'type:hola ', 'sleep:3', 'type:mundo', 'sleep:0.3', 'shot:k-escrito', 'key:ctrl-z', 'wait:deshacer: 5 letras', 'sleep:0.3', 'shot:k-deshacer', 'key:ctrl-y', 'wait:rehacer: 10 letras', 'key:ctrl-z', 'key:ctrl-z', 'wait:deshacer: 0 letras', 'sleep:0.3', 'key:alt-f4', 'sleep:0.8']
s += ['key:meta_l', 'wait:atajo 100', 'sleep:0.6', 'shot:k-inicio', 'key:meta_l', 'sleep:0.5', 'key:meta_l-e', 'wait:atajo 101', 'wait:bin/files', 'sleep:1.5', 'shot:k-archivos', 'key:ctrl-w', 'wait:(files) termino: 0', 'sleep:0.5',
      'key:meta_l-e', 'sleep:1.5', 'key:meta_l-d', 'sleep:0.6', 'shot:k-escritorio', 'key:meta_l-d', 'sleep:0.6', 'shot:k-restaurado']
r = subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', 'bios', '--no-disk', '--abs', '--out', out, '--timeout', '30', '--port', '4711', '--qemu', q] + s, stdout=subprocess.DEVNULL)
log = open(os.path.join(out, 'serial-bios.log'), errors='replace').read(); ok = r == 0
m = re.search(r'navegador /rd/(a+):', log); n = len(m.group(1)) if m else 0
if not 12 <= n <= 60: print('FALLO: repeticion de teclas:', n, 'letras en 1,6 s (se esperaban entre 12 y 60; QEMU va mas lento que el equipo)'); ok = False
for x in ('deshacer: 5 letras', 'rehacer: 10 letras', 'deshacer: 0 letras', 'atajo 100', 'atajo 101', '(files) termino: 0'):
    if x not in log: print('FALLO: falta', repr(x)); ok = False
for x in ('PANICO', 'fallo de pagina', 'excepcion', 'termino: -'):
    if x in log: print('FALLO:', x); ok = False
print('teclado:', n, 'repeticiones,', 'OK' if ok else 'FALLO'); sys.exit(0 if ok else 1)
