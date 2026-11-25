#!/usr/bin/env python3
"""Prueba que Imprimir (Ctrl+P) funciona en todos los programas y las ventanas de red e impresora de la barra.
Usa la impresora IPP falsa (PDF) de tools/fake_printer.py en el puerto 8631.
Uso: imprimir_test.py DIR_SALIDA DIR_TRABAJOS "args de qemu" """
import os, subprocess, sys
out, jobs, q = sys.argv[1:4]
MH = 42 + 9 * 28 + 14; MY = 800 - 36 - MH - 2
FAM = [['rescate', 'files', 'discos', 'papelera', 'restaurar', 'instalar'], ['escritor', 'hoja', 'diapos', 'agenda', 'editor', 'calc', 'pdf'], ['browser', 'apps', 'correo', 'lumen', 'redlocal', 'navjs', 'jsconsola', 'nube'], ['viewer', 'player', 'grabadora', 'paint'], ['snake', 'mines', 'blocks', 'enjambre'], ['config', 'impresoras', 'term', 'sysmon', 'acerca']]
def M(b):
    f = [i for i, l in enumerate(FAM) if b in l][0]; L = FAM[f]; i = L.index(b); y0 = max(42, min(42 + f * 28, MH - 6 - len(L) * 28)); fy = MY + 42 + 28 * f + 14; ay = MY + y0 + 28 * i + 14
    return ['click:50,782', 'sleep:0.6', f'move:100,{fy}', 'sleep:0.5', f'move:245,{fy}', 'sleep:0.3', f'move:350,{ay}', 'sleep:0.3', f'click:350,{ay}', 'sleep:2']
OX, OY = 88, 76
s = ['wait:escritorio listo', 'wait:vfs: /sys', 'wait:red: IP', 'sleep:1']
s += M('impresoras') + [f'click:{OX+168},{OY+20}', 'sleep:0.6', 'type:10.0.2.2:8631\\n', 'wait:impresora anadida', 'sleep:0.5', 'key:alt-f4', 'sleep:0.6']
APPS = ['agenda', 'calc', 'sysmon', 'term', 'paint', 'browser']
for a in APPS:
    s += M(a) + ['sleep:1', 'key:ctrl-p', 'wait:dialogo imprimir abierto', 'sleep:1.2', f'shot:d-{a}', 'click:955,625', 'sleep:5', 'key:alt-f4', 'sleep:0.8']
s += ['sleep:1', 'click:957,782', 'sleep:0.8', 'shot:red', 'key:esc', 'sleep:0.5', 'click:987,782', 'sleep:0.8', 'shot:cola', 'key:esc']
r = subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', 'bios', '--no-disk', '--abs', '--out', out, '--timeout', '60', '--port', '4695', '--qemu', q] + s, stdout=subprocess.DEVNULL)
log = open(os.path.join(out, 'serial-bios.log'), errors='replace').read(); ok = r == 0
n = log.count('[prueba] imprimir ') - log.count('impresora anadida')
done = [l for l in log.splitlines() if '] [prueba] imprimir ' in l]
if len(done) < len(APPS): print(f'FALLO: solo {len(done)} de {len(APPS)} programas imprimieron'); ok = False
for l in done:
    if not l.rstrip().endswith(': 0'): print('FALLO:', l); ok = False
for m in ('ventana de red', 'ventana de impresion'):
    if m not in log: print('FALLO: no se abrio la', m); ok = False
pdfs = sorted(f for f in os.listdir(jobs) if f.endswith('.pdf')) if os.path.isdir(jobs) else []
if len(pdfs) < len(APPS): print(f'FALLO: la impresora recibio {len(pdfs)} PDF'); ok = False
for m in ('PANICO', 'fallo de pagina', 'excepcion', 'termino: -'):
    if m in log: print('FALLO:', m); ok = False
print('imprimir en todos los programas:', 'OK' if ok else 'FALLO'); sys.exit(0 if ok else 1)
