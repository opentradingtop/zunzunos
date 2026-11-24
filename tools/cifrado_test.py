#!/usr/bin/env python3
"""Prueba del cifrado opcional. Arranque 1: en Configuracion se activa con una frase y los datos se reescriben cifrados
(desde el Mac, DOCS/secreto.txt ya no se lee). Arranque 2: al entrar se pide la frase; una mala se rechaza, la buena
abre los datos y la Terminal copia el documento descifrado fuera de la zona cifrada. Uso: cifrado_test.py DIR_SALIDA"""
import os, shutil, subprocess, sys
out = sys.argv[1]; os.makedirs(out, exist_ok=True); img = os.path.join(out, 'm.img'); shutil.copy('build/zunzun.img', img)
SECRET = 'texto secreto 123\n'
def mount(ro=False):
    a = subprocess.run(['hdiutil', 'attach'] + (['-readonly'] if ro else []) + ['-imagekey', 'diskimage-class=CRawDiskImage', '-mountrandom', '/tmp', img], capture_output=True, text=True, timeout=60).stdout
    return [l.split('\t')[-1].strip() for l in a.splitlines() if '/tmp' in l][0], a.split()[0]
mnt, dev = mount()
try: os.makedirs(os.path.join(mnt, 'DOCS'), exist_ok=True); open(os.path.join(mnt, 'DOCS', 'secreto.txt'), 'w').write(SECRET); subprocess.run(['find', mnt, '-name', '._*', '-delete'])
finally: subprocess.run(['hdiutil', 'detach', dev], capture_output=True)
q = f'-device usb-ehci,id=e -device usb-kbd,bus=e.0 -device usb-tablet,bus=e.0 -drive if=none,id=st,format=raw,file={img} -device usb-storage,bus=e.0,drive=st,bootindex=0'
MH = 42 + 9 * 28 + 14; MY = 800 - 36 - MH - 2
def M(f, i, n):
    y0 = max(42, min(42 + f * 28, MH - 6 - n * 28)); fy = MY + 42 + 28 * f + 14; ay = MY + y0 + 28 * i + 14
    return ['click:50,782', 'sleep:0.6', f'move:100,{fy}', 'sleep:0.5', f'move:245,{fy}', 'sleep:0.3', f'move:350,{ay}', 'sleep:0.3', f'click:350,{ay}', 'sleep:2']
CX, CY = 88, 76   # contenido de Configuracion (primera ventana)
P = 'frase de prueba 1'
s1 = ['wait:escritorio listo', 'wait:vfs: /sys', 'sleep:1'] + M(5, 0, 5) + ['sleep:1', f'click:{CX + 80},{CY + 377}', 'sleep:1', 'shot:c-seccion', f'click:{CX + 346},{CY + 275}', 'sleep:1', f'type:{P}\\n', 'sleep:1', f'type:{P}\\n', 'wait:cifrado activado', 'sleep:2', 'shot:c-activado', 'key:alt-f4', 'sleep:1']
r1 = subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', 'bios', '--no-disk', '--abs', '--out', os.path.join(out, '1'), '--timeout', '60', '--port', '4771', '--qemu', q] + s1, stdout=subprocess.DEVNULL)
log1 = open(os.path.join(out, '1', 'serial-bios.log'), errors='replace').read(); ok = r1 == 0
mnt, dev = mount(True)
try:
    d = open(os.path.join(mnt, 'DOCS', 'secreto.txt'), 'rb').read(); cf = os.path.exists(os.path.join(mnt, 'ZUNZUN', 'CIFRADO.TXT'))
    if not d.startswith(b'ZZCIFRA1') or b'secreto' in d: print('FALLO: el documento no quedo cifrado', d[:20]); ok = False
    if not cf: print('FALLO: no se guardo CIFRADO.TXT'); ok = False
finally: subprocess.run(['hdiutil', 'detach', dev], capture_output=True)
s2 = ['wait:escritorio listo', 'wait:HID puntero', 'wait:vfs: /sys', 'sleep:2', 'move:640,400', 'wait:cifrado pide la frase', 'sleep:1', 'shot:c-frase', 'type:frase equivocada\\n', 'sleep:3', f'type:{P}\\n', 'wait:cifrado abierto', 'sleep:1']
s2 += M(5, 2, 5) + ['sleep:1', 'type:cp /sys/DOCS/secreto.txt /sys/COPIA.TXT\\n', 'sleep:2', 'key:alt-f4', 'sleep:1']
r2 = subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', 'bios', '--no-disk', '--abs', '--out', os.path.join(out, '2'), '--timeout', '60', '--port', '4772', '--qemu', q] + s2, stdout=subprocess.DEVNULL)
log2 = open(os.path.join(out, '2', 'serial-bios.log'), errors='replace').read(); ok = ok and r2 == 0
if 'cifrado: frase incorrecta' not in log2: print('FALLO: la frase equivocada no se rechazo'); ok = False
mnt, dev = mount(True)
try:
    c = open(os.path.join(mnt, 'COPIA.TXT')).read() if os.path.exists(os.path.join(mnt, 'COPIA.TXT')) else None
    if c != SECRET: print('FALLO: con la frase buena no se lee el documento:', repr(c)); ok = False
finally: subprocess.run(['hdiutil', 'detach', dev], capture_output=True)
for m in ('PANICO', 'fallo de pagina', 'excepcion', 'termino: -'):
    if m in log1 or m in log2: print('FALLO:', m); ok = False
print('cifrado de datos:', 'OK' if ok else 'FALLO'); sys.exit(0 if ok else 1)
