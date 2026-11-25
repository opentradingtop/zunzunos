#!/usr/bin/env python3
"""Prueba la version 2.1: idioma, escritorio, Configuracion, mando, imagenes, papelera, archivos y teclado numerico."""
import subprocess, sys
FAM = [['rescate', 'files', 'discos', 'papelera', 'restaurar', 'instalar'], ['escritor', 'hoja', 'diapos', 'agenda', 'editor', 'calc', 'pdf'], ['browser', 'apps', 'correo', 'lumen', 'redlocal', 'navjs', 'jsconsola', 'nube'], ['viewer', 'player', 'grabadora', 'paint', 'pantalla', 'video', 'radio'], ['snake', 'mines', 'blocks', 'enjambre'], ['config', 'impresoras', 'term', 'sysmon', 'acerca']]
def M(b):
    """Abre un programa desde el menu Inicio: familia y despues su subpersiana."""
    f = [i for i, l in enumerate(FAM) if b in l][0]; L = FAM[f]; i = L.index(b); MH = 42 + 9 * 28 + 14; MY = 800 - 36 - MH - 2
    y0 = max(42, min(42 + f * 28, MH - 6 - len(L) * 28)); fy = MY + 42 + 28 * f + 14; ay = MY + y0 + 28 * i + 14
    return ['click:50,782', 'sleep:0.6', f'move:100,{fy}', 'sleep:0.5', f'move:245,{fy}', 'sleep:0.3', f'move:350,{ay}', 'sleep:0.3', f'click:350,{ay}']
out, mode, q = sys.argv[1:4]; part = sys.argv[4] if len(sys.argv) > 4 else 'all'
IDX = dict(files=1, term=8, calc=10, viewer=12, config=16, papelera=23)
s = ['wait:escritorio listo', 'wait:vfs: /sys', 'wait:HID puntero', 'sleep:1.5']; k = [0]
def app(n): i = IDX[n]; s.extend(M(n) + ['sleep:1.8']); k[0] += 1; return 60 + 28 * k[0], 50 + 26 * k[0]
s += ['shot:v-es-escritorio', 'click:1104,782', 'wait:[prueba] idioma 1', 'sleep:1.2', 'shot:v-en-escritorio', 'click:50,782', 'sleep:0.8', 'shot:v-en-menu', 'key:esc', 'sleep:0.5']
s += ['rclick:640,300', 'sleep:0.6', 'shot:v-en-ctx-escritorio', 'key:esc', 'sleep:0.4']
if part in ('all', 'cfg'):
    tx, ty = app('term'); s += ['type:prueba-mando\n', 'sleep:0.5']
    cx, cy = app('config'); s += ['shot:v-en-config']
    s += [f'click:{cx+100},{cy+44+4*32+14}', 'sleep:0.6', f'click:{cx+216+75},{cy+560-46+15}', 'wait:[prueba] mando configurado', 'sleep:0.5', 'shot:v-en-mandos']
    for sec in (1, 2, 3, 5, 6, 7): s += [f'click:{cx+100},{cy+44+sec*32+14}', 'sleep:0.8', f'shot:v-en-cfg{sec}']
    s += ['key:alt-f4', 'sleep:0.5', 'key:alt-f4', 'sleep:0.5']
if part in ('all', 'img'):
    vx, vy = app('viewer'); s += ['sleep:2.5', 'shot:v-en-galeria', 'key:ret', 'sleep:0.8', 'key:right', 'sleep:0.5', 'key:right', 'sleep:0.5', 'key:right', 'wait:progresiva.jpg: OK', 'sleep:0.5', 'key:right', 'wait:muestra.gif: OK', 'sleep:0.6', 'shot:v-gif', 'key:right', 'wait:muestra.jpg: OK', 'sleep:0.6', 'shot:v-jpg', f'click:{vx+470},{vy+18}', 'wait:[prueba] fondo cambiado', f'click:{vx+566},{vy+18}', 'wait:visor guardo png: OK', 'sleep:0.5', 'key:alt-f4', 'sleep:2', 'shot:v-en-fondo']
if part in ('all', 'files'):
    fx, fy = app('files'); s += ['sleep:0.5', f'click:{fx+300},{fy+86+2*22+10}', 'sleep:0.3', f'rclick:{fx+300},{fy+86+2*22+10}', 'sleep:0.6', 'shot:v-en-ctx-archivo', 'key:esc', 'sleep:0.4', 'key:delete', 'wait:a la papelera: 1, error 0', 'sleep:0.5', 'shot:v-en-archivos', f'rclick:{fx+60},{fy+66+26+10}', 'sleep:0.6', 'shot:v-en-ctx-unidad', 'key:esc', 'sleep:0.4', 'key:alt-f4', 'sleep:0.5']
    px, py = app('papelera'); s += ['sleep:0.5', 'shot:v-en-papelera', f'click:{px+200},{py+64+11}', 'sleep:0.3', f'click:{px+60},{py+20}', 'wait:papelera restauro', 'sleep:0.4', 'key:alt-f4', 'sleep:0.5']
if part in ('all', 'kp'):
    tx, ty = app('term'); s += ['type:echo ', 'key:kp_1', 'key:kp_2', 'key:kp_3', 'key:kp_add', 'key:kp_4', 'key:kp_enter', 'sleep:0.5', 'key:num_lock', 'sleep:0.3', 'type:echo X', 'key:kp_4', 'key:kp_4', 'type:Y\n', 'sleep:0.5', 'shot:v-teclado-num', 'key:alt-f4', 'sleep:0.4']
    cx, cy = app('calc'); s += [f'drag:{cx+150},{cy-13},{cx+250},{cy+87}', 'sleep:0.5', f'move:{cx+250},{cy+87}', 'sleep:0.2', 'shot:v-calc-movida']
    s += ['key:alt-f4', 'sleep:0.4']
s += ['click:1104,782', 'wait:[prueba] idioma 0', 'sleep:1.2', 'shot:v-es-final']
sys.exit(subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', mode, '--no-disk', '--abs', '--out', out, '--timeout', '60', '--port', '4640', '--qemu', q] + s))
