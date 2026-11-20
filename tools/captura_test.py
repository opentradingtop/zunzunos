#!/usr/bin/env python3
"""Prueba la captura de pantalla (Impr Pant y Alt+Impr Pant) y Cancelar un trabajo de impresion
desde la barra (impresora RAW lenta de tools/fake_printer.py en el puerto 9101).
Las capturas se comprueban leyendo la memoria de QEMU en el Mac (hdiutil) y decodificando el PNG.
Uso: captura_test.py DIR_SALIDA IMAGEN_MEMORIA "args de qemu" """
import os, re, struct, subprocess, sys, zlib
out, img, q = sys.argv[1:4]
MH = 42 + 9 * 28 + 14; MY = 800 - 36 - MH - 2
def M(f, i, n):
    y0 = max(42, min(42 + f * 28, MH - 6 - n * 28)); fy = MY + 42 + 28 * f + 14; ay = MY + y0 + 28 * i + 14
    return ['click:50,782', 'sleep:0.6', f'move:100,{fy}', 'sleep:0.5', f'move:245,{fy}', 'sleep:0.3', f'move:350,{ay}', 'sleep:0.3', f'click:350,{ay}', 'sleep:2']
OX, OY = 88, 76
s = ['wait:escritorio listo', 'wait:vfs: /sys', 'wait:red: IP', 'sleep:1', 'key:print', 'wait:[prueba] captura', 'sleep:1', 'shot:k-aviso']
s += M(5, 1, 5) + ['key:alt-print', 'sleep:2']   # Impresoras abierta: captura solo de su ventana
s += [f'click:{OX+168},{OY+20}', 'sleep:0.6', 'type:10.0.2.2:9101\\n', 'sleep:2', 'shot:k-menu', f'click:{OX+180},{OY+40+15}', 'wait:impresora anadida', 'sleep:0.5']
lang = f'click:{OX+380+150},{OY+80+13}'   # PCL -> Texto -> Samsung SPL2 (trabajo de ~700 KB, unos 11 s a 64 KB/s)
s += [lang, 'sleep:0.4', lang, 'sleep:0.6', 'key:alt-f4', 'sleep:0.6']
# trabajo largo: toda la salida de la terminal a 600 ppp; se cancela mientras se prepara
s += M(5, 2, 5) + ['type:dmesg\\n', 'sleep:0.5', 'type:dmesg\\n', 'sleep:0.5', 'type:lspci\\n', 'sleep:0.5', 'key:ctrl-p', 'wait:dialogo imprimir abierto', 'sleep:1', 'click:955,625', 'sleep:0.8',
       'click:987,782', 'sleep:0.8', 'shot:k-cola', 'click:1238,533', 'wait:imprimir 10.0.2.2', 'sleep:1', 'click:987,782', 'sleep:0.8', 'shot:k-cancelado', 'key:esc', 'sleep:0.5']
r = subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', 'bios', '--no-disk', '--abs', '--out', out, '--timeout', '60', '--port', '4699', '--qemu', q] + s, stdout=subprocess.DEVNULL)
log = open(os.path.join(out, 'serial-bios.log'), errors='replace').read(); ok = r == 0
caps = re.findall(r'\[prueba\] captura (\S+) (\d+)x(\d+): (-?\d+)', log)
if len(caps) < 2 or any(c[3] != '0' for c in caps): print('FALLO: capturas', caps); ok = False
elif caps[0][1:3] != ('1280', '800') or caps[1][1:3] == ('1280', '800'): print('FALLO: tamanos de captura', caps); ok = False
if 'cancelar trabajo' not in log: print('FALLO: no se pulso Cancelar'); ok = False
jl = [l for l in log.splitlines() if 'imprimir 10.0.2.2' in l and '(qpdl' in l]
if not jl or not jl[-1].rstrip().endswith(': cancelado'): print('FALLO: el trabajo no quedo cancelado:', jl); ok = False
# lee los PNG de la memoria y comprueba que se decodifican
try:
    a = subprocess.run(['hdiutil', 'attach', '-readonly', '-imagekey', 'diskimage-class=CRawDiskImage', '-mountrandom', '/tmp', img], capture_output=True, text=True, timeout=60).stdout
    mnt = [l.split('\t')[-1].strip() for l in a.splitlines() if '/tmp' in l][0]; dev = a.split()[0]
    d = os.path.join(mnt, 'DOCS', 'Capturas'); files = sorted(os.listdir(d))
    for f in files:
        b = open(os.path.join(d, f), 'rb').read(); assert b[:8] == b'\x89PNG\r\n\x1a\n'; w, h = struct.unpack('>II', b[16:24]); p = 8; idat = b''
        while p < len(b):
            n = struct.unpack('>I', b[p:p+4])[0]; t = b[p+4:p+8]; c = b[p+8:p+8+n]; assert zlib.crc32(t + c) == struct.unpack('>I', b[p+8+n:p+12+n])[0]
            if t == b'IDAT': idat += c
            p += 12 + n
        raw = zlib.decompress(idat); assert len(raw) == (w * 3 + 1) * h, 'tamano'
        print(f'png {f}: {w}x{h}, {len(b)} bytes (sin comprimir {len(raw)})')
    if len(files) < 2: print('FALLO: faltan PNG'); ok = False
    subprocess.run(['hdiutil', 'detach', dev], capture_output=True)
except Exception as e: print('FALLO: PNG ilegible:', e); ok = False
finally:
    for l in subprocess.run(['hdiutil', 'info'], capture_output=True, text=True).stdout.split('================================================'):
        if os.path.basename(img) in l:
            for w in l.split():
                if w.startswith('/dev/disk') and w[9:].isdigit(): subprocess.run(['hdiutil', 'detach', w], capture_output=True)
print('captura y cancelar:', 'OK' if ok else 'FALLO'); sys.exit(0 if ok else 1)
