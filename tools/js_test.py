#!/usr/bin/env python3
"""Prueba del motor JavaScript Colibri (propio): copia tools/js/prueba.js en la memoria USB virtual, la ejecuta con la
Consola JavaScript (jsconsola) y compara cada linea de salida con la de Node (tools/js/prueba.esperado.txt).
Uso: js_test.py DIR_SALIDA"""
import os, re, shutil, subprocess, sys, tempfile
out = sys.argv[1]; os.makedirs(out, exist_ok=True); img = os.path.join(out, 'stick.img'); shutil.copy('build/zunzun.img', img)
# meter el archivo en la particion FAT de la imagen (macOS: hdiutil + mount_msdos)
dev = subprocess.check_output(['hdiutil', 'attach', '-nomount', '-imagekey', 'diskimage-class=CRawDiskImage', img]).decode().split()[0]
mnt = tempfile.mkdtemp()
try:
    subprocess.check_call(['mount', '-t', 'msdos', dev + 's1', mnt]); shutil.copy('tools/js/prueba.js', os.path.join(mnt, 'prueba.js')); shutil.copy('tools/js/prueba2.js', os.path.join(mnt, 'prueba2.js'))
    for junk in ['.fseventsd', '.Spotlight-V100', '.Trashes']: shutil.rmtree(os.path.join(mnt, junk), ignore_errors=True)
finally:
    subprocess.call(['umount', mnt]); subprocess.call(['hdiutil', 'detach', '-quiet', dev]); os.rmdir(mnt)
MH = 42 + 9 * 28 + 14; MY = 800 - 36 - MH - 2
FAM = [['rescate', 'files', 'discos', 'papelera', 'restaurar', 'instalar'], ['escritor', 'hoja', 'diapos', 'agenda', 'editor', 'calc', 'pdf'], ['browser', 'apps', 'correo', 'lumen', 'redlocal'], ['viewer', 'player', 'grabadora', 'paint', 'pantalla', 'video'], ['snake', 'mines', 'blocks', 'enjambre'], ['config', 'impresoras', 'term', 'sysmon', 'acerca']]
def M(b):
    f = [i for i, l in enumerate(FAM) if b in l][0]; L = FAM[f]; i = L.index(b); y0 = max(42, min(42 + f * 28, MH - 6 - len(L) * 28)); fy = MY + 42 + 28 * f + 14; ay = MY + y0 + 28 * i + 14
    return ['click:50,782', 'sleep:0.6', f'move:100,{fy}', 'sleep:0.5', f'move:245,{fy}', 'sleep:0.3', f'move:350,{ay}', 'sleep:0.3', f'click:350,{ay}', 'sleep:2']
s = ['wait:escritorio listo', 'sleep:1'] + M('term') + ['type:abrir jsconsola /sys/prueba.js\n', 'wait:js terminado', 'sleep:1', 'shot:js-consola', 'type:Array.from("abc").map(c => c + c).join("-")\n', 'sleep:1', 'shot:js-repl', 'key:alt-f4', 'sleep:0.8', 'type:abrir jsconsola /sys/prueba2.js\n', 'sleep:2', 'wait:fin2', 'sleep:1', 'shot:js-prueba2']
q = f'-device usb-ehci,id=e -device usb-kbd,bus=e.0 -device usb-tablet,bus=e.0 -drive if=none,id=st,format=raw,file={img} -device usb-storage,bus=e.0,drive=st,bootindex=0'
r = subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', 'bios', '--no-disk', '--abs', '--out', out, '--timeout', '120', '--port', '4643', '--qemu', q] + s)
log = open(os.path.join(out, 'serial-bios.log'), errors='replace').read()
got = [l.split('[js] ', 1)[1] for l in log.splitlines() if '[js] ' in l]
exp = open('tools/js/prueba.esperado.txt').read().splitlines()
exp2 = open('tools/js/prueba2.esperado.txt').read().splitlines()
t = re.search(r'js terminado (\d+) lineas en (\d+) ms', log)
bad = [(i + 1, e, g) for i, (e, g) in enumerate(zip(exp, got)) if e != g]
for i, e, g in bad: print(f'linea {i}:\n  Node:     {e}\n  ZunzunOS: {g}')
repl = got[len(exp):len(exp) + 2]   # despues: la linea escrita a mano en la consola y su resultado
got2 = got[len(exp) + 2:]
bad += [(1000 + i + 1, e, g) for i, (e, g) in enumerate(zip(exp2, got2)) if e != g]
if len(got2) != len(exp2): bad.append((0, 'prueba2: %d lineas de %d' % (len(got2), len(exp2)), ''))
if repl != ['> Array.from("abc").map(c => c + c).join("-")', 'aa-bb-cc']: print('consola interactiva:', repl); bad.append((0, 'repl', repl))
if r or len(got) < len(exp) or bad:
    print(f'js: FALLO ({len(got)} lineas de {len(exp)}, {len(bad)} distintas, vm={r})'); sys.exit(1)
print(f'js: OK (motor Colibri: {len(exp)} + {len(exp2)} lineas iguales que Node, {t.group(2) if t else "?"} ms)')
