#!/usr/bin/env python3
"""Prueba del historial y del gestor de cookies del navegador (con tools/fake_web.py en el puerto 8081).
Uso: navegador_test.py DIR_SALIDA DIR_WEB "args de qemu" """
import os, re, subprocess, sys
out, web, q = sys.argv[1:4]
MH = 42 + 9 * 28 + 14; MY = 800 - 36 - MH - 2
def M(f, i, n):
    y0 = max(42, min(42 + f * 28, MH - 6 - n * 28)); fy = MY + 42 + 28 * f + 14; ay = MY + y0 + 28 * i + 14
    return ['click:50,782', 'sleep:0.6', f'move:100,{fy}', 'sleep:0.5', f'move:245,{fy}', 'sleep:0.3', f'move:350,{ay}', 'sleep:0.3', f'click:350,{ay}', 'sleep:2']
def go(u): return ['key:ctrl-l', 'sleep:0.3', f'type:{u}\\n', 'sleep:2.5']
W = 'http://10.0.2.2:8081'
s = ['wait:escritorio listo', 'wait:red: IP', 'sleep:1'] + M(2, 0, 8) + ['sleep:1']
s += go(W + '/set') + go(W + '/ver') + ['key:ctrl-h', 'wait:historial 2 paginas', 'sleep:1', 'shot:n-historial']
s += go('zunzun:cookies') + ['wait:navegador', 'sleep:0.5', 'shot:n-cookies'] + go('zunzun:cookies?politica=2') + go('zunzun:cookies?todas') + go(W + '/set') + go(W + '/ver') + ['sleep:0.5']
s += go('zunzun:cookies?politica=0') + go('zunzun:historial?no') + go(W + '/sinjs') + go('zunzun:historial') + ['wait:historial 4 paginas', 'sleep:0.5', 'shot:n-sin-historial'] + go(W + '/ver') + [
    # boton Historial de la barra; marcar las dos visitas mas recientes y borrarlas
    'clicklog:boton historial en (\\d+),(\\d+)', 'wait:boton historial', 'sleep:1.5', 'clicklog:casillas (\\d+),(\\d+)', 'sleep:0.4', 'clicklog:casillas \\d+,\\d+ (\\d+),(\\d+)', 'sleep:0.4',
    'shot:n-marcadas', 'clicklog:borrar en (\\d+),(\\d+)', 'wait:historial borradas', 'sleep:1', 'shot:n-borradas'] + go('zunzun:historial?borrar') + ['wait:historial borrado', 'sleep:0.5', 'key:alt-f4', 'sleep:0.5']
r = subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', 'bios', '--no-disk', '--abs', '--out', out, '--timeout', '60', '--port', '4761', '--qemu', q] + s, stdout=subprocess.DEVNULL)
log = open(os.path.join(out, 'serial-bios.log'), errors='replace').read(); ok = r == 0
req = open(os.path.join(web, 'peticiones.txt')).read().splitlines() if os.path.exists(os.path.join(web, 'peticiones.txt')) else []
vers = [l for l in req if l.startswith('/ver |')]
if len(vers) < 2 or 'sesion=abc123' not in vers[0]: print('FALLO: la primera visita no llevaba las cookies', vers[:1]); ok = False
elif vers[-1].rstrip() != '/ver | Cookie:': print('FALLO: con la politica "ninguna" se enviaron cookies:', vers[-1]); ok = False
for m in ('historial 2 paginas', 'cookies politica=2', 'cookies todas', 'historial 4 paginas', 'boton historial', 'historial borradas 2 visitas', 'historial borrado'):
    if m not in log: print('FALLO: falta', repr(m)); ok = False
if log.count('historial 2 paginas') < 2: print('FALLO: despues de borrar las marcadas no quedan 2 visitas'); ok = False
for x in ('PANICO', 'fallo de pagina', 'excepcion', 'termino: -'):
    if x in log: print('FALLO:', x); ok = False
print('historial y cookies:', 'OK' if ok else 'FALLO'); sys.exit(0 if ok else 1)
