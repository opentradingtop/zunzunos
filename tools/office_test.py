#!/usr/bin/env python3
import subprocess, sys
FAM = [['rescate', 'files', 'discos', 'papelera', 'restaurar', 'instalar'], ['escritor', 'hoja', 'diapos', 'agenda', 'editor', 'calc', 'pdf'], ['browser', 'apps'], ['viewer', 'player', 'grabadora', 'paint'], ['snake', 'mines', 'blocks', 'enjambre'], ['config', 'impresoras', 'term', 'sysmon', 'acerca']]
def M(b):
    """Abre un programa desde el menu Inicio: familia y despues su subpersiana."""
    f = [i for i, l in enumerate(FAM) if b in l][0]; L = FAM[f]; i = L.index(b); MH = 42 + 9 * 28 + 14; MY = 800 - 36 - MH - 2
    y0 = max(42, min(42 + f * 28, MH - 6 - len(L) * 28)); fy = MY + 42 + 28 * f + 14; ay = MY + y0 + 28 * i + 14
    return ['click:50,782', 'sleep:0.6', f'move:100,{fy}', 'sleep:0.5', f'move:245,{fy}', 'sleep:0.3', f'move:350,{ay}', 'sleep:0.3', f'click:350,{ay}']
out, mode, q = sys.argv[1:4]
APPS = ['rescate','files','discos','escritor','hoja','diapos','agenda','apps','term']
s = ['wait:escritorio listo', 'wait:vfs: /ahci0', 'sleep:1']; k = [0]
def menu(i): s.extend(M(APPS[i]))
def app(n, casc=True):
    menu(APPS.index(n)); s.append('sleep:1.5')
    if casc: k[0] += 1
    return 60 + 28 * k[0], 50 + 26 * k[0]
app('escritor', False); X, Y = 20, 40; by = 680 - 146
s += [f'click:{X+100},{Y+83}', 'type:Mi Novela\n', 'type:Habia una vez un zunzun que escribia.\nSegunda linea del primer capitulo.\n', 'key:ctrl-s', 'wait:escritor guardo', f'click:{X+60},{Y+by+13}', 'sleep:0.3', 'type:Texto del segundo capitulo con ñ.\n', 'key:ctrl-s', 'sleep:0.5', 'shot:o-escritor', 'key:f11', 'sleep:0.8', 'type:Una frase en modo concentracion. ', 'sleep:0.3', 'shot:o-concentracion', 'key:esc', 'sleep:0.5']
for n, e in [(1, 'txt'), (2, 'md'), (3, 'html'), (4, 'rtf'), (5, 'docx')]: s += [f'click:{X+170},{Y+by+43}', 'sleep:0.6', f'key:{n}', f'wait:Mi Novela.{e}: OK', 'sleep:0.3']
s += ['key:alt-f4', 'sleep:0.8']
hx, hy = app('hoja'); s += ['type:Concepto\n', 'type:Ventas\n', 'type:Gastos\n', 'type:Saldo\n', 'key:up', 'key:up', 'key:up', 'key:up', 'key:right', 'type:Importe\n', 'type:1250\n', 'type:300,5\n', 'type:=B2-B3\n', 'type:=SUMA(B2:B3)*2\n', 'type:=SI(B4>900;"bien";"mal")\n', 'key:ctrl-s', 'sleep:0.6', 'key:ret', 'wait:hoja guardo', 'sleep:0.4', 'shot:o-hoja', 'key:alt-f4', 'sleep:0.6']
dx, dy = app('diapos'); s += [f'click:{dx+400},{dy+80}', 'sleep:0.2', 'type: de prueba\n', 'key:ctrl-s', 'sleep:0.6', 'key:ret', 'wait:diapos guardo', 'key:f5', 'wait:diapos presentando', 'sleep:0.8', 'shot:o-presentar', 'key:esc', 'sleep:0.4', 'key:alt-f4', 'sleep:0.6']
ax, ay = app('agenda'); s += [f'click:{ax+760},{ay+64}', 'sleep:0.5', 'key:ret', 'sleep:0.5', 'type:Revisar copias\n', 'wait:agenda guardo', 'sleep:0.4', 'shot:o-agenda', 'key:alt-f4', 'sleep:0.6']
wx, wy = app('apps'); s += [f'click:{wx+190},{wy+18}', 'wait:Calculadora' if False else 'sleep:3', f'click:{wx+200},{wy+56}', 'sleep:0.3', f'click:{wx+660},{wy+455}', 'wait:app instalada', f'click:{wx+60},{wy+18}', 'sleep:0.4', f'dclick:{wx+200},{wy+56}', 'wait:app web abierta', 'sleep:1', 'shot:o-webapp', 'key:alt-f4', 'sleep:0.5', 'key:alt-f4', 'sleep:0.5']; k[0] += 1
tx, ty = app('term'); s += ['type:abrir escritor /ahci0/prueba.docx\n', 'wait:escritor importo', 'sleep:0.8', 'key:alt-f4', 'sleep:0.8', 'type:abrir escritor /ahci0/prueba.odt\n', 'sleep:2', 'key:alt-f4', 'sleep:0.8', 'type:abrir hoja /sys/DOCS/hoja.xlsx\n', 'sleep:2', 'shot:o-hoja2', 'key:alt-f4', 'sleep:0.6', 'type:desbloquear /sd0\n', 'sleep:0.3', 'type:prueba-fs /sd0\n', 'wait:resultado fs /sd0', 'type:ls /sys/DOCS/ESCRITOR\n', 'sleep:0.5', 'type:apagar\n', 'exit:15']
sys.exit(subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', mode, '--out', out, '--timeout', '60', '--port', '4610', '--qemu', q] + s))
