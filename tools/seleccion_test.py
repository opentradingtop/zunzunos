#!/usr/bin/env python3
"""Prueba: Lugares de Archivos, acceso directo en el escritorio, seleccionar arrastrando y copiar en
Navegador, Imagenes (zona de la foto) y Terminal, pegar la imagen en Pintar, y que el Editor
no se cuelga con un archivo binario. Uso: seleccion_test.py DIR_SALIDA "args de qemu" """
import os, subprocess, sys
out, q = sys.argv[1:3]
MH = 42 + 9 * 28 + 14; MY = 800 - 36 - MH - 2
FAM = [['rescate', 'files', 'discos', 'papelera', 'restaurar', 'instalar'], ['escritor', 'hoja', 'diapos', 'agenda', 'editor', 'calc', 'pdf'], ['browser', 'apps', 'correo'], ['viewer', 'player', 'grabadora', 'paint'], ['snake', 'mines', 'blocks', 'enjambre'], ['config', 'impresoras', 'term', 'sysmon', 'acerca']]
k = [0]
def M(b):
    f = [i for i, l in enumerate(FAM) if b in l][0]; L = FAM[f]; i = L.index(b); y0 = max(42, min(42 + f * 28, MH - 6 - len(L) * 28)); fy = MY + 42 + 28 * f + 14; ay = MY + y0 + 28 * i + 14
    k[0] += 1; c = k[0] % 8; return (60 + 28 * c, 50 + 26 * c), ['click:50,782', 'sleep:0.6', f'move:100,{fy}', 'sleep:0.5', f'move:245,{fy}', 'sleep:0.3', f'move:350,{ay}', 'sleep:0.3', f'click:350,{ay}', 'sleep:2']
s = ['wait:escritorio listo', 'wait:vfs: /sys', 'wait:HID puntero', 'sleep:1', 'key:print', 'wait:[prueba] captura', 'sleep:0.8']   # crea Documentos/Capturas
(fx, fy), st = M('files'); s += st
pl = fy + 66 + 26 + 2 * 38 + 34 + 28 - 3 + 10      # Lugares > Documentos (2 unidades: rd y sys)
s += [f'click:{fx + 60},{pl}', 'sleep:0.8', 'shot:s-documentos', f'rclick:{fx + 300},{fy + 66 + 20 + 11}', 'sleep:0.6', 'shot:s-menu', f'click:{fx + 340},{fy + 66 + 20 + 11 + 14 + 2 * 26}', 'sleep:0.6', f'click:{fx + 360},{fy + 66 + 20 + 11 + 20 + 14}', 'wait:acceso directo', 'sleep:0.5']
s += [f'dclick:{fx + 60},{fy + 33}', 'sleep:0.3', 'key:alt-f4', 'sleep:0.6', 'shot:s-escritorio']
(bx, by), st = M('browser'); s += st + ['sleep:1', f'drag:{bx + 30},{by + 38 + 60},{bx + 500},{by + 38 + 150}', 'sleep:0.5', 'shot:s-navegador', 'key:ctrl-c', 'wait:navegador copio', 'sleep:0.3', 'key:alt-f4', 'sleep:0.6']
(vx, vy), st = M('viewer'); s += st + ['sleep:1.5', f'click:{vx + 60},{vy + 36 + 50}', 'sleep:1.5', f'drag:{vx + 250},{vy + 150},{vx + 500},{vy + 350}', 'sleep:0.5', f'rclick:{vx + 350},{vy + 250}', 'sleep:0.5', 'shot:s-imagenes', f'click:{vx + 400},{vy + 250 + 14}', 'wait:visor copio seleccion', 'sleep:0.3', 'key:alt-f4', 'sleep:0.6']
(px, py), st = M('paint'); s += st + ['sleep:1', f'rclick:{px + 96 + 300},{py + 200}', 'sleep:0.5', f'click:{px + 96 + 340},{py + 200 + 14}', 'wait:paint pego imagen', 'sleep:0.5', 'shot:s-pintar', 'key:alt-f4', 'sleep:0.4', 'key:tab', 'key:ret', 'sleep:0.6']
(tx, ty), st = M('term'); s += st + ['sleep:1', 'type:echo hola mundo\\n', 'sleep:0.5', f'drag:{tx + 4},{ty + 3 + 8},{tx + 4 + 8 * 40},{ty + 3 + 16 * 3 + 8}', 'sleep:0.4', 'shot:s-terminal', 'key:ctrl-c', 'wait:term copio', 'sleep:0.3', 'key:alt-f4', 'sleep:0.6']
(ex, ey), st = M('editor'); s += st + ['sleep:1', 'shot:s-editor0']
r = subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', 'bios', '--no-disk', '--abs', '--out', out, '--timeout', '30', '--port', '4701', '--qemu', q] + s, stdout=subprocess.DEVNULL)
log = open(os.path.join(out, 'serial-bios.log'), errors='replace').read(); ok = r == 0
for m in ('acceso directo /sys/ESCRITORIO/Capturas.acceso -> /sys/DOCS/Capturas: 0', 'navegador copio', 'visor copio seleccion', 'paint pego imagen', 'term copio'):
    if m not in log: print('FALLO: falta', repr(m)); ok = False
for x in ('PANICO', 'fallo de pagina', 'excepcion', 'termino: -'):
    if x in log: print('FALLO:', x); ok = False
print('lugares, accesos y seleccion:', 'OK' if ok else 'FALLO'); sys.exit(0 if ok else 1)
