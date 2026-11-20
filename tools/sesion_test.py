#!/usr/bin/env python3
"""Prueba las sesiones: crear PIN, PIN que no coincide, bloquear, PIN incorrecto, cerrar sesion,
invitado (programas de administracion ocultos, archivos solo en RAM) y segundo arranque con PIN.
Uso: sesion_test.py DIR_SALIDA DIR_DISCOS"""
import os, shutil, subprocess, sys
out, K = sys.argv[1], sys.argv[2]; os.makedirs(out, exist_ok=True)
img = os.path.join(K, 'sesion.img'); shutil.copy('build/zunzun.img', img)
Q = f'-device usb-ehci,id=e -device usb-kbd,bus=e.0 -device usb-tablet,bus=e.0 -drive if=none,id=st,format=raw,file={img} -device usb-storage,bus=e.0,drive=st,bootindex=0'
MENU = ['click:50,782', 'sleep:0.6', 'click:170,474', 'sleep:0.8']  # Inicio y el chip de sesion
def run(name, steps):
    o = os.path.join(out, name); r = subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', 'bios', '--no-disk', '--abs', '--no-login', '--out', o, '--timeout', '60', '--port', '4670', '--qemu', Q] + steps, stdout=subprocess.DEVNULL)
    return r, open(os.path.join(o, 'serial-bios.log'), errors='replace').read()
def order(log, marks):
    i = 0
    for m in marks:
        j = log.find(m, i)
        if j < 0: print(f'FALLO: falta {m!r} (o esta fuera de orden)'); return False
        i = j + len(m)
    return True
BOOT = ['wait:pantalla de inicio', 'wait:vfs: /sys', 'wait:HID puntero', 'sleep:1']
ok = True
r, log = run('s1', BOOT + ['click:520,400', 'wait:crear pin', 'sleep:0.8', 'type:1234', 'sleep:0.5', 'type:1235', 'sleep:1', 'shot:s-no-coinciden', 'type:1234', 'sleep:0.5', 'type:1234', 'wait:pin guardado', 'sleep:1.5']
    + MENU + ['click:215,504', 'wait:sesion: bloqueada', 'sleep:0.8', 'type:0000', 'sleep:1.5', 'shot:s-bloqueo-mal', 'type:1234', 'sleep:1.5']
    + MENU + ['click:235,556', 'wait:sesion: cerrada', 'sleep:1.2', 'click:760,400', 'wait:sesion: invitado', 'sleep:2', 'click:50,782', 'sleep:0.6', 'shot:s-invitado-menu', 'key:esc', 'sleep:0.4',
    'rclick:900,300', 'sleep:0.6', 'click:950,340', 'sleep:0.8', 'type:secreto\\n', 'sleep:1.5', 'shot:s-invitado-doc']
    + MENU + ['click:235,530', 'wait:cerrada (invitado)', 'sleep:1', 'shot:s-fin'])
ok &= r == 0 and order(log, ['sesion: crear pin', 'PIN guardado (0)', 'sesion: bloqueada', 'sesion: cerrada (administrador)', 'sesion: invitado', 'sesion: cerrada (invitado)'])
d = open(img, 'rb').read()
if d.find(b'SESION  TXT') < 0: print('FALLO: el PIN no se guardo en la memoria'); ok = False
if d.find(b'SECRETO TXT') >= 0: print('FALLO: el documento del invitado llego a la memoria'); ok = False
MH = 42 + 9 * 28 + 14; MY = 800 - 36 - MH - 2; FY = MY + 42 + 28 * 5 + 14; AY = MY + max(42, min(42 + 5 * 28, MH - 6 - 5 * 28)) + 14  # Sistema > Configuracion
CONFIG = ['click:50,782', 'sleep:0.6', f'move:100,{FY}', 'sleep:0.5', f'move:245,{FY}', 'sleep:0.3', f'move:350,{AY}', 'sleep:0.3', f'click:350,{AY}', 'sleep:1.5']
r, log = run('s2', BOOT + ['click:520,400', 'sleep:1', 'type:9999', 'sleep:1.5', 'shot:s2-pin-mal', 'type:1234', 'wait:sesion: administrador', 'sleep:1', 'shot:s2-dentro']
    + CONFIG + ['click:138,390', 'sleep:0.8', 'click:388,375', 'wait:[zz] salvapantallas', 'sleep:2.5', 'shot:s2-salvapantallas', 'click:640,400', 'wait:sesion: bloqueada', 'sleep:0.8', 'shot:s2-pide-pin', 'type:1234', 'sleep:1.5'])
ok &= r == 0 and order(log, ['sesion: pantalla de inicio', 'sesion: administrador', 'sesion: escritorio', '[zz] salvapantallas', 'salvapantallas: fin', 'sesion: bloqueada', 'sesion: escritorio'])
if 'crear pin' in log: print('FALLO: volvio a pedir crear el PIN'); ok = False
for m in ('PANICO', 'fallo de pagina', 'excepcion'):
    if m in log: print('FALLO:', m); ok = False
print('sesiones: OK' if ok else 'sesiones: FALLO'); sys.exit(0 if ok else 1)
