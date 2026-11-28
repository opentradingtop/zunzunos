#!/usr/bin/env python3
"""Prueba de la guardia de red: un programa sin permiso (la Terminal) pide salir a Internet y el escritorio pregunta;
Bloquear lo deja bloqueado sin volver a preguntar; la red local se permite sin preguntar; en Configuracion, Conexiones
se cambia a Permitir y entonces sale sin preguntar. Uso: guardia_test.py DIR_SALIDA "args de qemu" """
import os, subprocess, sys
out, q = sys.argv[1:3]
MH = 42 + 9 * 28 + 14; MY = 800 - 36 - MH - 2
FAM = [['rescate', 'files', 'discos', 'papelera', 'restaurar', 'instalar'], ['escritor', 'hoja', 'diapos', 'agenda', 'editor', 'calc', 'pdf'], ['browser', 'apps', 'correo', 'lumen', 'redlocal', 'navjs', 'jsconsola', 'nube'], ['viewer', 'player', 'grabadora', 'paint', 'pantalla', 'video', 'radio'], ['snake', 'mines', 'blocks', 'enjambre'], ['config', 'impresoras', 'term', 'sysmon', 'acerca', 'alquimia']]
k = [0]
def M(b, h=500):
    f = [i for i, l in enumerate(FAM) if b in l][0]; L = FAM[f]; i = L.index(b); y0 = max(42, min(42 + f * 28, MH - 6 - len(L) * 28)); fy = MY + 42 + 28 * f + 14; ay = MY + y0 + 28 * i + 14
    k[0] += 1; c = k[0] % 8; y = 50 + 26 * c; y = 26 if y + h + 40 > 800 else y
    return (60 + 28 * c, y), ['click:50,782', 'sleep:0.6', f'move:100,{fy}', 'sleep:0.5', f'move:245,{fy}', 'sleep:0.3', f'move:350,{ay}', 'sleep:0.3', f'click:350,{ay}', 'sleep:2']
DLG = 360, 295   # dialogo del escritorio (560x210, centrado); botones: Permitir siempre, Solo esta vez, Bloquear
s = ['wait:escritorio listo', 'wait:red: IP', 'sleep:1']
_, st = M('term'); s += st + ['sleep:1', 'type:ping 1.1.1.1\n', 'wait:guardia pregunta: term -> 1.1.1.1', 'sleep:1.5', 'shot:g-pregunta', f'click:{DLG[0] + 499},{DLG[1] + 176}', 'wait:guardia respuesta 4', 'sleep:2',
      'type:ping 1.1.1.1\n', 'wait:guardia: term bloqueado', 'sleep:1', 'type:ping 10.0.2.2\n', 'sleep:3', 'shot:g-terminal', 'key:alt-f4', 'sleep:0.8']
_, st = M('config', 560); CX, CY = 116, 102   # contenido de Configuracion (900x560) tal como lo coloca el gestor
s += st + ['sleep:1', f'click:{CX + 55},{CY + 345}', 'sleep:1.2', 'shot:g-conexiones', f'click:{CX + 611},{CY + 263}', 'wait:guardia regla term 1', 'sleep:1', 'shot:g-permitido', 'key:alt-f4', 'sleep:0.8']
_, st = M('term'); s += st + ['sleep:1', 'type:ping 1.1.1.1\n', 'sleep:4', 'key:alt-f4', 'sleep:0.5']
r = subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', 'bios', '--no-disk', '--abs', '--out', out, '--timeout', '40', '--port', '4741', '--qemu', q] + s, stdout=subprocess.DEVNULL)
log = open(os.path.join(out, 'serial-bios.log'), errors='replace').read(); ok = r == 0
if log.count('guardia pregunta: term') != 1: print('FALLO: se pregunto', log.count('guardia pregunta: term'), 'veces (deberia ser 1)'); ok = False
if 'guardia pregunta: term -> 10.0.2.2' in log: print('FALLO: se pregunto por la red local'); ok = False
for m in ('guardia respuesta 4', 'guardia: term bloqueado', 'guardia regla term 1'):
    if m not in log: print('FALLO: falta', repr(m)); ok = False
for x in ('PANICO', 'fallo de pagina', 'excepcion', 'termino: -'):
    if x in log: print('FALLO:', x); ok = False
print('guardia de red:', 'OK' if ok else 'FALLO'); sys.exit(0 if ok else 1)
