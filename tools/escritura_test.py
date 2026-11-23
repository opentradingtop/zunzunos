#!/usr/bin/env python3
"""Prueba de las escrituras seguras: una memoria con dos cambios cortados por un apagon. DOCS/X.txt tiene el original y
un X.txt.zztmp a medias (debe quedar el original); DOCS/Y.txt.zztmp esta entero pero sin nombre (debe quedar como Y.txt).
Despues se guarda un documento con la Hoja para ver que guardar sigue funcionando. Uso: escritura_test.py DIR_SALIDA"""
import os, shutil, subprocess, sys
out = sys.argv[1]; os.makedirs(out, exist_ok=True); img = os.path.join(out, 'm.img'); shutil.copy('build/zunzun.img', img)
def mount(ro=False):
    a = subprocess.run(['hdiutil', 'attach'] + (['-readonly'] if ro else []) + ['-imagekey', 'diskimage-class=CRawDiskImage', '-mountrandom', '/tmp', img], capture_output=True, text=True, timeout=60).stdout
    return [l.split('\t')[-1].strip() for l in a.splitlines() if '/tmp' in l][0], a.split()[0]
mnt, dev = mount()
try:
    d = os.path.join(mnt, 'DOCS'); os.makedirs(d, exist_ok=True)
    open(os.path.join(d, 'X.txt'), 'w').write('ORIGINAL ENTERO\n'); open(os.path.join(d, 'X.txt.zztmp'), 'w').write('NUEVO A ME'); open(os.path.join(d, 'Y.txt.zztmp'), 'w').write('NUEVO ENTERO\n')
    subprocess.run(['find', mnt, '-name', '._*', '-delete']); subprocess.run(['rm', '-rf', os.path.join(mnt, '.fseventsd'), os.path.join(mnt, '.Spotlight-V100')])
finally: subprocess.run(['hdiutil', 'detach', dev], capture_output=True)
q = f'-device usb-ehci,id=e -device usb-kbd,bus=e.0 -device usb-tablet,bus=e.0 -drive if=none,id=st,format=raw,file={img} -device usb-storage,bus=e.0,drive=st,bootindex=0'
s = ['wait:escritorio listo', 'wait:HID puntero', 'wait:vfs: /sys', 'sleep:2', 'move:640,400', 'wait:sesion: escritorio', 'wait:escritura recuperada', 'sleep:3']
r = subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', 'bios', '--no-disk', '--abs', '--out', out, '--timeout', '30', '--port', '4751', '--qemu', q] + s, stdout=subprocess.DEVNULL)
log = open(os.path.join(out, 'serial-bios.log'), errors='replace').read(); ok = r == 0
if log.count('escritura recuperada') != 2: print('FALLO: se recuperaron', log.count('escritura recuperada'), 'escrituras (deberian ser 2)'); ok = False
mnt, dev = mount(True)
try:
    d = os.path.join(mnt, 'DOCS'); x = open(os.path.join(d, 'X.txt')).read(); y = open(os.path.join(d, 'Y.txt')).read() if os.path.exists(os.path.join(d, 'Y.txt')) else None
    left = [f for f in os.listdir(d) if f.endswith('.zztmp') and not f.startswith('._')]
    if x != 'ORIGINAL ENTERO\n': print('FALLO: X.txt no es el original:', repr(x)); ok = False
    if y != 'NUEVO ENTERO\n': print('FALLO: Y.txt no es el nuevo:', repr(y)); ok = False
    if left: print('FALLO: quedan temporales', left); ok = False
finally: subprocess.run(['hdiutil', 'detach', dev], capture_output=True)
for m in ('PANICO', 'fallo de pagina', 'excepcion', 'termino: -'):
    if m in log: print('FALLO:', m); ok = False
print('escrituras seguras:', 'OK' if ok else 'FALLO'); sys.exit(0 if ok else 1)
