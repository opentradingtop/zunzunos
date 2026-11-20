#!/usr/bin/env python3
"""Prueba las aplicaciones graficas a traves del menu Inicio."""
import subprocess, sys
FAM = [['rescate', 'files', 'discos', 'papelera', 'restaurar', 'instalar'], ['escritor', 'hoja', 'diapos', 'agenda', 'editor', 'calc', 'pdf'], ['browser', 'apps'], ['viewer', 'player', 'grabadora', 'paint'], ['snake', 'mines', 'blocks', 'enjambre'], ['config', 'impresoras', 'term', 'sysmon', 'acerca']]
def M(b):
    """Abre un programa desde el menu Inicio: familia y despues su subpersiana."""
    f = [i for i, l in enumerate(FAM) if b in l][0]; L = FAM[f]; i = L.index(b); MH = 42 + 9 * 28 + 14; MY = 800 - 36 - MH - 2
    y0 = max(42, min(42 + f * 28, MH - 6 - len(L) * 28)); fy = MY + 42 + 28 * f + 14; ay = MY + y0 + 28 * i + 14
    return ['click:50,782', 'sleep:0.6', f'move:100,{fy}', 'sleep:0.5', f'move:245,{fy}', 'sleep:0.3', f'move:350,{ay}', 'sleep:0.3', f'click:350,{ay}']
out, mode = sys.argv[1], sys.argv[2]; qemu = sys.argv[3] if len(sys.argv) > 3 else ''
APPS = ['rescate','files','discos','escritor','hoja','diapos','agenda','apps','term','editor','calc','paint','viewer','browser','player','sysmon','config','snake','mines','blocks','instalar']
s = ['wait:escritorio listo', 'wait:red: IP', 'sleep:1']; k = [0]
def app(name):
    i = APPS.index(name); s.extend(M(name) + ['sleep:1.5']); k[0] = (k[0] + 1) % 8
    return 60 + 28 * k[0], 50 + 26 * k[0]
def at(o, x, y): return f'{o[0] + x},{o[1] + y}'
c = app('calc'); s += ['type:sqrt(2)*10+2^3\n', 'wait:[prueba] calc', 'sleep:0.3']
e = app('editor'); s += ['type:Hola desde el editor de ZunzunOS\nSegunda linea\n', 'key:ctrl-s', 'sleep:0.6', 'key:ret', 'wait:editor guardo', 'sleep:0.3', 'shot:g1-calc-editor']
s += ['key:alt-f4', 'sleep:0.5']
c2 = None
s += ['click:' + at(c, 100, 30), 'sleep:0.3', 'key:alt-f4', 'sleep:0.5']
p = app('paint'); s += ['click:' + at(p, 20, 36), 'drag:' + f'{p[0]+150},{p[1]+80},{p[0]+420},{p[1]+300}', 'click:' + at(p, 30, 200), 'click:' + at(p, 20, 66), 'drag:' + f'{p[0]+160},{p[1]+300},{p[0]+500},{p[1]+90}', 'click:' + at(p, 48, 411), 'sleep:0.6', 'key:ret', 'wait:paint guardo', 'sleep:0.5']
v = app('viewer'); s += ['wait:[prueba] visor', 'sleep:0.5', 'shot:g2-paint-viewer', 'key:alt-f4', 'sleep:0.4', 'click:' + at(p, 300, -14), 'sleep:0.3', 'key:alt-f4', 'sleep:0.5']
b = app('browser'); s += ['wait:[prueba] navegador', 'click:' + at(b, 300, 18), 'sleep:0.2', 'type:http://10.0.2.2:8765/\n', 'sleep:2']
s += ['click:' + at(b, 200, 75), 'sleep:2', 'shot:g3-browser', 'key:alt-f4', 'sleep:0.5']
f = app('files'); s += ['sleep:1', 'dclick:' + at(f, 60, 36 + 11), 'sleep:0.8']
m = app('sysmon'); s += ['sleep:2.5', 'shot:g4-files-sysmon', 'key:alt-f4', 'sleep:0.4', 'click:' + at(f, 300, -14), 'sleep:0.3', 'key:alt-f4', 'sleep:0.5']
g = app('config'); s += ['click:' + at(g, 150, 18), 'sleep:0.6', 'shot:g5-config', 'key:alt-f4', 'sleep:0.4']
g = app('snake'); s += ['key:down', 'sleep:0.4', 'key:right', 'sleep:0.6', 'shot:g6-snake', 'key:alt-f4', 'sleep:0.4']
g = app('mines'); s += ['click:' + at(g, 200, 200), 'sleep:0.3', 'rclick:' + at(g, 20, 50), 'sleep:0.3', 'shot:g7-mines', 'key:alt-f4', 'sleep:0.4']
g = app('blocks'); s += ['key:left', 'key:up', 'key:spc', 'sleep:0.2', 'key:right', 'key:spc', 'sleep:0.3', 'shot:g8-blocks', 'key:alt-f4', 'sleep:0.4']
g = app('player'); s += ['click:' + at(g, 50, 116), 'sleep:0.5', 'wait:reproduccion terminada', 'sleep:0.3', 'key:alt-f4', 'sleep:0.5']
g = app('term'); s += ['type:ps\n', 'sleep:0.5', 'shot:g9-final', 'type:cat /sys/nota.txt\n', 'sleep:0.5', 'type:ls /sys\n', 'sleep:0.5', 'shot:g10-term', 'type:apagar\n', 'exit:10']
sys.exit(subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', mode, '--out', out, '--timeout', '60', '--qemu', qemu] + s))
