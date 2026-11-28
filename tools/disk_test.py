#!/usr/bin/env python3
"""Prueba Discos, Instalar y Archivos a traves de la interfaz."""
import subprocess, sys
FAM = [['rescate', 'files', 'discos', 'papelera', 'restaurar', 'instalar'], ['escritor', 'hoja', 'diapos', 'agenda', 'editor', 'calc', 'pdf'], ['browser', 'apps', 'correo', 'lumen', 'redlocal', 'navjs', 'jsconsola', 'nube'], ['viewer', 'player', 'grabadora', 'paint', 'pantalla', 'video', 'radio'], ['snake', 'mines', 'blocks', 'enjambre'], ['config', 'impresoras', 'term', 'sysmon', 'acerca', 'alquimia']]
def M(b):
    """Abre un programa desde el menu Inicio: familia y despues su subpersiana."""
    f = [i for i, l in enumerate(FAM) if b in l][0]; L = FAM[f]; i = L.index(b); MH = 42 + 9 * 28 + 14; MY = 800 - 36 - MH - 2
    y0 = max(42, min(42 + f * 28, MH - 6 - len(L) * 28)); fy = MY + 42 + 28 * f + 14; ay = MY + y0 + 28 * i + 14
    return ['click:50,782', 'sleep:0.6', f'move:100,{fy}', 'sleep:0.5', f'move:245,{fy}', 'sleep:0.3', f'move:350,{ay}', 'sleep:0.3', f'click:350,{ay}']
out, mode, qemu = sys.argv[1:4]
APPS = ['rescate','files','discos','escritor','hoja','diapos','agenda','apps','term','editor','calc','paint','viewer','browser','player','sysmon','config','snake','mines','blocks','instalar']
s = ['wait:escritorio listo', 'wait:vfs: /sys', 'wait:HID puntero', 'sleep:1']; k = [0]
def menu(i): s.extend(M(APPS[i]))
def app(n): menu(APPS.index(n)); s.append('sleep:1.5'); k[0] += 1; return 60 + 28 * k[0], 50 + 26 * k[0]
o = app('discos'); ox, oy = o
for i, (fs, nm) in enumerate([(0, 'FAT32'), (1, 'FAT16'), (2, 'exFAT'), (3, 'ext2')], 1):
    s += [f'click:{ox+60},{oy+34+i*44+10}', 'sleep:0.3', f'click:{ox+254},{oy+262+fs*30+10}', 'sleep:0.2', f'click:{ox+284},{oy+441}', 'sleep:0.6', 'type:BORRAR\n', f'wait:formato ahci{i} {nm}: OK', 'sleep:0.5']
s += ['shot:discos', 'key:alt-f4', 'sleep:0.5']
ix, iy = app('instalar'); s += ['shot:instalar', f'click:{ix+620-80},{iy+400-30}', 'sleep:0.6', 'type:BORRAR\n', 'wait:instalacion en ahci0: OK', 'sleep:1', 'shot:instalado', 'key:alt-f4', 'sleep:0.5']
tx, ty = app('term'); s += ['type:df\n', 'sleep:1', 'type:prueba-fs /ahci1p1\n', 'wait:resultado fs /ahci1p1', 'type:prueba-fs /ahci2p1\n', 'wait:resultado fs /ahci2p1', 'type:limpiar\n', 'type:ls /ahci3p1\n', 'type:ls /ahci4p1\n', 'type:df\n', 'sleep:1', 'shot:volumenes', 'key:alt-f4', 'sleep:0.5']
fx, fy = app('files')
def side(i): return f'click:{fx+60},{fy+66+26+i*38+15}'
def row(i): return f'click:{fx+300},{fy+66+20+i*22+10}'
s += [side(0), 'sleep:0.5', row(2), 'sleep:0.3', 'key:ctrl-c', side(2), 'sleep:0.5', 'key:ctrl-v', 'wait:pegar en /ahci1p1: 1 ok, error 0', 'sleep:0.5', 'shot:pegado', row(0), 'sleep:0.3', 'key:ctrl-c', side(3), 'sleep:0.5', 'key:ctrl-v', 'wait:pegar en /ahci2p1: 1 ok, error 0', 'sleep:0.5', row(0), 'sleep:0.3', 'key:ctrl-x', side(1), 'sleep:0.5', 'key:ctrl-v', 'wait:pegar en /sys: 1 ok, error 0', 'sleep:0.5', 'click:1090,782', 'sleep:0.8', 'shot:archivos-brillo', 'key:esc', 'sleep:0.3']
s.extend(['click:50,782', 'sleep:0.6', 'move:100,742', 'sleep:0.3', 'click:100,742']); s += ['sleep:0.8', 'key:ret', 'exit:15']
sys.exit(subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', mode, '--no-disk', '--abs', '--out', out, '--timeout', '120', '--port', '4590', '--qemu', qemu] + s))
