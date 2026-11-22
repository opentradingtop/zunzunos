#!/usr/bin/env python3
"""Prueba: entrar con el teclado, copiar una carpeta en una ventana de Archivos y pegarla en otra y en el
escritorio, cookies, <base href>, <noscript> y <meta refresh> del navegador (con tools/fake_web.py),
pagina Sitios, quitar un marcador, borrar cookies y el boton fx de la Hoja.
Uso: tareas_test.py DIR_SALIDA DIR_WEB "args de qemu" """
import os, subprocess, sys
out, web, q = sys.argv[1:4]
MH = 42 + 9 * 28 + 14; MY = 800 - 36 - MH - 2
FAM = [['rescate', 'files', 'discos', 'papelera', 'restaurar', 'instalar'], ['escritor', 'hoja', 'diapos', 'agenda', 'editor', 'calc', 'pdf'], ['browser', 'apps', 'correo', 'lumen'], ['viewer', 'player', 'grabadora', 'paint', 'pantalla', 'video'], ['snake', 'mines', 'blocks', 'enjambre'], ['config', 'impresoras', 'term', 'sysmon', 'acerca']]
k = [0]
def M(b, h=500):
    f = [i for i, l in enumerate(FAM) if b in l][0]; L = FAM[f]; i = L.index(b); y0 = max(42, min(42 + f * 28, MH - 6 - len(L) * 28)); fy = MY + 42 + 28 * f + 14; ay = MY + y0 + 28 * i + 14
    k[0] += 1; c = k[0] % 8; y = 50 + 26 * c; y = 26 if y + h + 40 > 800 else y  # el gestor sube las ventanas que no caben
    return (60 + 28 * c, y), ['click:50,782', 'sleep:0.6', f'move:100,{fy}', 'sleep:0.5', f'move:245,{fy}', 'sleep:0.3', f'move:350,{ay}', 'sleep:0.3', f'click:350,{ay}', 'sleep:2']
# entrar sin raton: derecha (invitado), izquierda (administrador), Intro; crear PIN: Tab hasta "Ahora no" e Intro
s = ['wait:pantalla de inicio', 'wait:vfs: /sys', 'wait:HID puntero', 'wait:red: IP', 'sleep:1', 'key:right', 'sleep:0.3', 'shot:t-invitado-elegido', 'key:left', 'sleep:0.3', 'key:ret', 'wait:crear pin', 'sleep:0.8', 'key:tab', 'sleep:0.3', 'shot:t-ahora-no', 'key:ret', 'wait:sesion: administrador', 'sleep:1.5']
s += ['key:print', 'wait:[prueba] captura', 'sleep:0.8']
(fx, fy), st = M('files'); pl = fy + 66 + 26 + 2 * 38 + 34 + 28 - 3 + 10
s += st + [f'click:{fx + 60},{pl}', 'sleep:0.8', f'rclick:{fx + 300},{fy + 66 + 20 + 11}', 'sleep:0.6', f'click:{fx + 340},{fy + 66 + 20 + 11 + 14 + 3 * 26}', 'wait:copiar 1 elementos', 'sleep:0.5']
(gx, gy), st = M('files'); s += st + [f'rclick:{gx + 500},{gy + 250}', 'sleep:0.6', 'shot:t-menu-pegar', f'click:{gx + 540},{gy + 250 + 15 + 5 * 26}', 'wait:pegar en /sys:', 'sleep:1', 'key:alt-f4', 'sleep:0.5', 'key:alt-f4', 'sleep:0.6']
s += ['rclick:900,300', 'sleep:0.6', 'click:940,496', 'wait:pegar en /sys/ESCRITORIO', 'sleep:1.5', 'shot:t-escritorio']; k[0] += 1  # Pegar en el escritorio abre una ventana de Archivos
(bx, by), st = M('browser', 720); addr = f'click:{bx + 400},{by + 20}'
def go(u): return [addr, 'sleep:0.4', f'type:{u}\\n', 'sleep:3']
s += st + go('http://10.0.2.2:8081/set') + go('http://10.0.2.2:8081/ver') + go('http://10.0.2.2:8081/base') + [f'click:{bx + 40},{by + 38 + 4 + 14}', 'sleep:3', 'shot:t-base'] + go('http://10.0.2.2:8081/noscript') + [f'click:{bx + 40},{by + 38 + 4 + 14}', 'sleep:3'] + go('http://10.0.2.2:8081/refresh') + ['sleep:2']
s += [f'click:{bx + 190},{by + 20}', 'wait:navegador /rd/res/sitios.html: local', 'sleep:1.5', 'shot:t-sitios', f'click:{bx + 834},{by + 20}', 'sleep:1', f'click:{bx + 898},{by + 20}', 'sleep:0.8', 'shot:t-marcadores', f'click:{bx + 940},{by + 40 + 14 + 26}', 'sleep:0.8', f'click:{bx + 960},{by + 60 + 14}', 'sleep:0.8', 'key:ret', 'wait:marcador 0 eliminado', 'sleep:0.5']
s += [f'rclick:{bx + 1000},{by + 420}', 'sleep:0.6', 'shot:t-menu-pagina', f'click:{bx + 1040},{by + 420 + 15 + 2 * 26}', 'sleep:0.8', 'key:ret', 'wait:cookies borradas', 'sleep:0.5', 'key:alt-f4', 'sleep:0.6']
(hx, hy), st = M('hoja', 560); s += st + [f'click:{hx + 6 + 8 * 68 + 30},{hy + 16}', 'sleep:0.8', f'click:{hx + 6 + 8 * 68 + 60},{hy + 30 + 14}', 'sleep:0.8', f'click:{hx + 6 + 8 * 68 + 90},{hy + 60 + 14}', 'sleep:0.5', 'shot:t-fx', 'type:1;2;3)\\n', 'sleep:0.8', 'shot:t-hoja']
r = subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', 'bios', '--no-disk', '--abs', '--no-login', '--out', out, '--timeout', '40', '--port', '4707', '--qemu', q] + s, stdout=subprocess.DEVNULL)
log = open(os.path.join(out, 'serial-bios.log'), errors='replace').read(); ok = r == 0
for m in ('sesion: administrador', 'copiar 1 elementos', 'pegar en /sys: 1 ok', 'pegar en /sys/ESCRITORIO: 1 ok', 'marcador 0 eliminado: 0', 'cookies borradas: 2'):
    if m not in log: print('FALLO: falta', repr(m)); ok = False
req = open(os.path.join(web, 'peticiones.txt')).read() if os.path.exists(os.path.join(web, 'peticiones.txt')) else ''
for m, why in (('/ver | Cookie: sesion=abc123; temporal=1', 'el navegador no devolvio las cookies'), ('/destino/pagina.html', '<base href> no se aplico'), ('/sinjs', 'el enlace de <noscript> no funciono'), ('/refrescado', '<meta refresh> no se siguio')):
    if m not in req and not (m.startswith('/ver') and '/ver | Cookie: temporal=1; sesion=abc123' in req): print('FALLO:', why); ok = False
for x in ('PANICO', 'fallo de pagina', 'excepcion', 'termino: -'):
    if x in log: print('FALLO:', x); ok = False
print('teclado, copiar/pegar, navegador y hoja:', 'OK' if ok else 'FALLO'); sys.exit(0 if ok else 1)
