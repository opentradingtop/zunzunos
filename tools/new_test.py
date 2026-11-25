#!/usr/bin/env python3
"""Prueba bordes, apariencia, grabadora, Enjambre, Acerca y Restaurar."""
import subprocess, sys
FAM = [['rescate', 'files', 'discos', 'papelera', 'restaurar', 'instalar'], ['escritor', 'hoja', 'diapos', 'agenda', 'editor', 'calc', 'pdf'], ['browser', 'apps', 'correo', 'lumen', 'redlocal', 'navjs', 'jsconsola', 'nube'], ['viewer', 'player', 'grabadora', 'paint'], ['snake', 'mines', 'blocks', 'enjambre'], ['config', 'impresoras', 'term', 'sysmon', 'acerca']]
def M(b):
    """Abre un programa desde el menu Inicio: familia y despues su subpersiana."""
    f = [i for i, l in enumerate(FAM) if b in l][0]; L = FAM[f]; i = L.index(b); MH = 42 + 9 * 28 + 14; MY = 800 - 36 - MH - 2
    y0 = max(42, min(42 + f * 28, MH - 6 - len(L) * 28)); fy = MY + 42 + 28 * f + 14; ay = MY + y0 + 28 * i + 14
    return ['click:50,782', 'sleep:0.6', f'move:100,{fy}', 'sleep:0.5', f'move:245,{fy}', 'sleep:0.3', f'move:350,{ay}', 'sleep:0.3', f'click:350,{ay}']
out, mode, q = sys.argv[1:4]; IDX = dict(term=8, grabadora=21, restaurar=22, config=16, enjambre=24, acerca=25)
s = ['wait:escritorio listo', 'wait:vfs: /sys', 'wait:HID puntero', 'sleep:1']; k = [0]
def app(n): i = IDX[n]; s.extend(M(n) + ['sleep:1.5']); k[0] += 1; return 60 + 28 * k[0], 50 + 26 * k[0]
tx, ty = app('term'); R, B = tx - 1 + 714, ty - 26 + 449
s += [f'drag:{R-1},{B-1},{R+99},{B+79}', 'wait:term 812x502', 'sleep:0.3', f'drag:{tx-1},{ty+100},{tx-41},{ty+100}', 'wait:term 852x502', 'sleep:0.3', f'drag:{tx+200},{ty-26},{tx+200},{ty-16}', 'wait:term 852x492', 'sleep:0.4', f'drag:{tx-41},{ty-16},{tx+9},{ty+4}', 'wait:term 802x472', 'sleep:0.4', f'move:{tx+9-41+1},{ty+200}', 'sleep:0.3', 'shot:n-bordes', 'key:alt-f4', 'sleep:0.5']
gx, gy = app('grabadora'); s += ['key:spc', 'wait:grabacion iniciada', 'sleep:3', 'shot:n-grabando', 'key:spc', 'wait:grabacion guardada', 'sleep:0.4', 'key:alt-f4', 'sleep:0.5']
ax, ay = app('acerca'); s += ['sleep:0.5', 'shot:n-acerca', 'key:esc', 'sleep:0.5']
ex, ey = app('enjambre'); s += ['sleep:1']
for d in ['left', 'spc', 'right', 'spc', 'right', 'spc']: s += [f'key:{d}', 'sleep:0.25']
s += ['sleep:1', 'shot:n-enjambre', 'key:alt-f4', 'sleep:0.5']
vx, vy = app('config'); s += ['sleep:0.6', f'click:{vx+900-62},{vy+560-25}', 'wait:tema guardado', 'sleep:0.5', 'shot:n-tema', 'key:alt-f4', 'sleep:0.6']
rx, ry = app('restaurar'); X = rx + 264; BY = ry + 378
s += [f'click:{rx+80},{ry+44}', 'sleep:0.4', 'shot:n-rest0', f'click:{X+100},{BY+50}', 'sleep:0.6', 'type:ACTUALIZAR\n', 'wait:restaurar: Sistema actualizado', 'sleep:0.4', f'click:{X+55},{BY+15}', 'sleep:0.6', 'type:base\n', 'wait:restaurar: Punto creado', 'sleep:0.5', 'shot:n-rest1']
tx, ty = app('term'); s += ['type:desbloquear /ahci0p1\n', 'sleep:0.3', 'type:echo intruso > /ahci0p1/EXTRA.TXT\n', 'type:rm /ahci0p1/LEEME.TXT\n', 'type:cp /rd/res/ayuda.txt /ahci0p1/ZUNZUN/KERNEL.BIN\n', 'sleep:1', 'key:alt-f4', 'sleep:0.8', f'click:{X+100},{ry+46}', 'sleep:0.3', f'click:{X+290},{BY+15}', 'wait:restaurar: Punto correcto', 'sleep:0.3', f'click:{X+170},{BY+15}', 'sleep:0.6', 'type:RESTAURAR\n', 'wait:restaurar: Restaurado', 'sleep:0.5', 'shot:n-rest2', 'key:alt-f4', 'sleep:0.4']
sys.exit(subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', mode, '--no-disk', '--abs', '--out', out, '--timeout', '90', '--port', '4630', '--qemu', q] + s))
