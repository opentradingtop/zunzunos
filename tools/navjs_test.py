#!/usr/bin/env python3
"""Prueba del Navegador JS: abre una pagina local con JavaScript (tools/js/pagina) desde la memoria USB virtual y
comprueba el DOM, el script externo, fetch, temporizadores, localStorage, clics (addEventListener y onclick="...") y
un formulario con preventDefault. Las posiciones de los botones las dice la propia pagina (getBoundingClientRect).
Uso: navjs_test.py DIR_SALIDA"""
import os, re, shutil, subprocess, sys, tempfile
out = sys.argv[1]; os.makedirs(out, exist_ok=True); img = os.path.join(out, 'stick.img'); shutil.copy('build/zunzun.img', img)
dev = subprocess.check_output(['hdiutil', 'attach', '-nomount', '-imagekey', 'diskimage-class=CRawDiskImage', img]).decode().split()[0]
mnt = tempfile.mkdtemp()
try:
    subprocess.check_call(['mount', '-t', 'msdos', dev + 's1', mnt]); shutil.copytree('tools/js/pagina', os.path.join(mnt, 'pjs'))
    for junk in ['.fseventsd', '.Spotlight-V100', '.Trashes']: shutil.rmtree(os.path.join(mnt, junk), ignore_errors=True)
finally:
    subprocess.call(['umount', mnt]); subprocess.call(['hdiutil', 'detach', '-quiet', dev]); os.rmdir(mnt)
MH = 42 + 9 * 28 + 14; MY = 800 - 36 - MH - 2
FAM = [['rescate', 'files', 'discos', 'papelera', 'restaurar', 'instalar'], ['escritor', 'hoja', 'diapos', 'agenda', 'editor', 'calc', 'pdf'], ['browser', 'apps', 'correo', 'lumen', 'redlocal'], ['viewer', 'player', 'grabadora', 'paint', 'pantalla', 'video'], ['snake', 'mines', 'blocks', 'enjambre'], ['config', 'impresoras', 'term', 'sysmon', 'acerca']]
def M(b):
    f = [i for i, l in enumerate(FAM) if b in l][0]; L = FAM[f]; i = L.index(b); y0 = max(42, min(42 + f * 28, MH - 6 - len(L) * 28)); fy = MY + 42 + 28 * f + 14; ay = MY + y0 + 28 * i + 14
    return ['click:50,782', 'sleep:0.6', f'move:100,{fy}', 'sleep:0.5', f'move:245,{fy}', 'sleep:0.3', f'move:350,{ay}', 'sleep:0.3', f'click:350,{ay}', 'sleep:2']
O = r'pagina en (\d+),(\d+)'
s = ['wait:escritorio listo', 'sleep:1'] + M('term') + ['type:abrir navjs /sys/pjs/index.html\n', 'wait:navjs pagina lista', 'wait:OK load', 'sleep:1', 'shot:nj-cargada',
     f'clicklog:{O}|boton (\\d+),(\\d+)', 'wait:OK clic 1', 'sleep:0.3', f'clicklog:{O}|boton (\\d+),(\\d+)', 'wait:OK clic 2', 'sleep:0.3',
     f'clicklog:{O}|boton2 (\\d+),(\\d+)', 'wait:OK onclick', 'sleep:0.3',
     f'clicklog:{O}|nombre (\\d+),(\\d+)', 'sleep:0.4', 'type:Ana\n', 'wait:OK formulario Ana', 'sleep:1', 'shot:nj-final']
q = f'-device usb-ehci,id=e -device usb-kbd,bus=e.0 -device usb-tablet,bus=e.0 -drive if=none,id=st,format=raw,file={img} -device usb-storage,bus=e.0,drive=st,bootindex=0'
r = subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', 'bios', '--no-disk', '--abs', '--out', out, '--timeout', '60', '--port', '4645', '--qemu', q] + s)
log = open(os.path.join(out, 'serial-bios.log'), errors='replace').read()
need = ['OK dom', 'OK innerHTML', 'OK externo', 'OK fetch hola red', 'OK temporizador', 'OK localStorage', 'OK URL', 'OK errores', 'OK load', 'OK clic 2', 'OK onclick', 'OK formulario Ana']
miss = [n for n in need if n not in log]
errs = [l for l in log.splitlines() if '[navjs] error' in l]
for e in errs: print(e)
if r or miss or errs or 'no-debe-ir' in log: print('navjs: FALLO, falta', miss); sys.exit(1)
print('navjs: OK (' + ', '.join(n[3:] for n in need) + ')')
