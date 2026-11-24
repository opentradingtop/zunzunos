#!/usr/bin/env python3
"""Prueba de los clics en el texto: 1 clic pone el cursor donde se pulsa, 2 eligen la palabra y 3 todo el texto.
Lo comprueba en un campo de formulario del Navegador JS (tools/js/campo) leyendo su valor con JavaScript, y en la barra
de direcciones (capturas). Uso: clics_test.py DIR_SALIDA"""
import os, shutil, subprocess, sys, tempfile
out = sys.argv[1]; os.makedirs(out, exist_ok=True); img = os.path.join(out, 'stick.img'); shutil.copy('build/zunzun.img', img)
dev = subprocess.check_output(['hdiutil', 'attach', '-nomount', '-imagekey', 'diskimage-class=CRawDiskImage', img]).decode().split()[0]
mnt = tempfile.mkdtemp()
try:
    subprocess.check_call(['mount', '-t', 'msdos', dev + 's1', mnt]); shutil.copytree('tools/js/campo', os.path.join(mnt, 'pc'))
    for junk in ['.fseventsd', '.Spotlight-V100', '.Trashes']: shutil.rmtree(os.path.join(mnt, junk), ignore_errors=True)
finally:
    subprocess.call(['umount', mnt]); subprocess.call(['hdiutil', 'detach', '-quiet', dev]); os.rmdir(mnt)
MH = 42 + 9 * 28 + 14; MY = 800 - 36 - MH - 2
FAM = [['rescate', 'files', 'discos', 'papelera', 'restaurar', 'instalar'], ['escritor', 'hoja', 'diapos', 'agenda', 'editor', 'calc', 'pdf'], ['browser', 'apps', 'correo', 'lumen', 'redlocal'], ['viewer', 'player', 'grabadora', 'paint', 'pantalla', 'video'], ['snake', 'mines', 'blocks', 'enjambre'], ['config', 'impresoras', 'term', 'sysmon', 'acerca']]
def M(b):
    f = [i for i, l in enumerate(FAM) if b in l][0]; L = FAM[f]; i = L.index(b); y0 = max(42, min(42 + f * 28, MH - 6 - len(L) * 28)); fy = MY + 42 + 28 * f + 14; ay = MY + y0 + 28 * i + 14
    return ['click:50,782', 'sleep:0.6', f'move:100,{fy}', 'sleep:0.5', f'move:245,{fy}', 'sleep:0.3', f'move:350,{ay}', 'sleep:0.3', f'click:350,{ay}', 'sleep:2']
O = r'pagina en (\d+),(\d+)'
s = ['wait:escritorio listo', 'sleep:1'] + M('term') + ['type:abrir navjs /sys/pc/index.html\n', 'wait:navjs pagina lista', 'wait:letra4', 'sleep:1',
     f'clicklog:{O}|letra4 (\\d+),(\\d+)', 'sleep:0.4', 'shot:cl-cursor', 'type:X\n', 'wait:valor [holaX mundo]', 'sleep:0.6',   # 1 clic: cursor tras "hola"
     f'clicklog2:{O}|letra8 (\\d+),(\\d+)', 'sleep:0.4', 'shot:cl-palabra', 'type:Z\n', 'wait:valor [holaX Z]', 'sleep:0.6',  # 2 clics: la palabra "mundo"
     f'clicklog3:{O}|letra4 (\\d+),(\\d+)', 'sleep:0.4', 'shot:cl-todo', 'type:Y\n', 'wait:valor [Y]', 'sleep:0.6',            # 3 clics: todo
     'clicklog:barra en (\\d+),(\\d+)', 'sleep:0.6', 'shot:cl-barra-cursor', 'clicklog3:barra en (\\d+),(\\d+)', 'sleep:0.5', 'shot:cl-barra-todo', 'key:esc', 'sleep:0.5']
q = f'-device usb-ehci,id=e -device usb-kbd,bus=e.0 -device usb-tablet,bus=e.0 -drive if=none,id=st,format=raw,file={img} -device usb-storage,bus=e.0,drive=st,bootindex=0'
r = subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', 'bios', '--no-disk', '--abs', '--out', out, '--timeout', '60', '--port', '4646', '--qemu', q] + s)
log = open(os.path.join(out, 'serial-bios.log'), errors='replace').read(); os.remove(img)
miss = [v for v in ('valor [holaX mundo]', 'valor [holaX Z]', 'valor [Y]') if v not in log]
for x in ('PANICO', 'fallo de pagina', 'excepcion'):
    if x in log: miss.append(x)
print('clics:', 'OK (1 clic: cursor; 2: palabra; 3: todo)' if r == 0 and not miss else f'FALLO {miss}'); sys.exit(0 if r == 0 and not miss else 1)
