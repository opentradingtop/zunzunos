#!/usr/bin/env python3
"""Prueba Grabar pantalla (AVI MJPEG) y Videos: graba unos segundos, para, abre el video y lo
reproduce; despues lee el AVI de la memoria en el Mac, comprueba su estructura y decodifica un cuadro.
Uso: video_test.py DIR_SALIDA IMAGEN_MEMORIA "args de qemu" """
import os, re, struct, subprocess, sys
out, img, q = sys.argv[1:4]
MH = 42 + 9 * 28 + 14; MY = 800 - 36 - MH - 2
FAM = [['rescate', 'files', 'discos', 'papelera', 'restaurar', 'instalar'], ['escritor', 'hoja', 'diapos', 'agenda', 'editor', 'calc', 'pdf'], ['browser', 'apps', 'correo', 'lumen', 'redlocal', 'navjs', 'jsconsola', 'nube'], ['viewer', 'player', 'grabadora', 'paint', 'pantalla', 'video'], ['snake', 'mines', 'blocks', 'enjambre'], ['config', 'impresoras', 'term', 'sysmon', 'acerca']]
def M(b):
    f = [i for i, l in enumerate(FAM) if b in l][0]; L = FAM[f]; i = L.index(b); y0 = max(42, min(42 + f * 28, MH - 6 - len(L) * 28)); fy = MY + 42 + 28 * f + 14; ay = MY + y0 + 28 * i + 14
    return ['click:50,782', 'sleep:0.6', f'move:100,{fy}', 'sleep:0.5', f'move:245,{fy}', 'sleep:0.3', f'move:350,{ay}', 'sleep:0.3', f'click:350,{ay}', 'sleep:2']
OX, OY = 88, 76
s = ['wait:escritorio listo', 'wait:vfs: /sys', 'wait:HID puntero', 'sleep:1'] + M('pantalla') + ['shot:v-grabador', f'click:{OX + 235},{OY + 57}', 'sleep:0.3', f'click:{OX + 100},{OY + 142}', 'wait:grabando pantalla', 'sleep:1']
s += ['sleep:7', 'click:150,782', 'sleep:4', f'click:{OX + 100},{OY + 142}', 'wait:grabacion de pantalla', 'sleep:2', 'shot:v-parado', f'click:{OX + 256},{OY + 142}', 'wait:video reproduciendo', 'sleep:2', 'shot:v-reproduciendo', 'wait:video terminado', 'sleep:0.5']
r = subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', 'bios', '--no-disk', '--abs', '--out', out, '--timeout', '150', '--port', '4703', '--qemu', q] + s, stdout=subprocess.DEVNULL)
log = open(os.path.join(out, 'serial-bios.log'), errors='replace').read(); ok = r == 0
m = re.search(r'grabacion de pantalla (\d+) cuadros, (\d+) bytes', log)
if not m or int(m.group(1)) < 10: print('FALLO: pocos cuadros', m and m.group(0)); ok = False
v = re.search(r'\[prueba\] video (\S+): (\d+)x(\d+), (\d+) cuadros', log)
if not v or (m and v.group(4) != m.group(1)): print('FALLO: el reproductor no leyo el video', v and v.group(0)); ok = False
try:
    a = subprocess.run(['hdiutil', 'attach', '-readonly', '-imagekey', 'diskimage-class=CRawDiskImage', '-mountrandom', '/tmp', img], capture_output=True, text=True, timeout=60).stdout
    mnt = [l.split('\t')[-1].strip() for l in a.splitlines() if '/tmp' in l][0]; dev = a.split()[0]
    d = os.path.join(mnt, 'DOCS', 'Videos'); f = sorted(x for x in os.listdir(d) if x.endswith('.avi'))[-1]; b = open(os.path.join(d, f), 'rb').read()
    assert b[:4] == b'RIFF' and struct.unpack('<I', b[4:8])[0] == len(b) - 8, 'tamano RIFF'
    p = b.index(b'movi') - 8; sz = struct.unpack('<I', b[p + 4:p + 8])[0]; q2 = p + 12; n = 0; first = None
    while q2 < p + 8 + sz: cid = b[q2:q2 + 4]; l = struct.unpack('<I', b[q2 + 4:q2 + 8])[0]; first = first or (cid == b'00dc' and b[q2 + 8:q2 + 8 + l]); n += cid == b'00dc'; q2 += 8 + l + (l & 1)
    assert q2 == p + 8 + sz, 'lista movi'
    assert b[p + 8 + sz:p + 12 + sz] == b'idx1', 'falta idx1'
    open(os.path.join(out, 'cuadro.jpg'), 'wb').write(first); sp = subprocess.run(['sips', '-s', 'format', 'png', os.path.join(out, 'cuadro.jpg'), '--out', os.path.join(out, 'cuadro.png')], capture_output=True)
    assert sp.returncode == 0, 'el Mac no decodifica el JPEG'
    print(f'avi {f}: {len(b)} bytes, {n} cuadros, JPEG del primer cuadro valido')
    subprocess.run(['hdiutil', 'detach', dev], capture_output=True)
except Exception as e: print('FALLO: AVI:', e); ok = False
finally:
    for l in subprocess.run(['hdiutil', 'info'], capture_output=True, text=True).stdout.split('================================================'):
        if os.path.basename(img) in l:
            for w in l.split():
                if w.startswith('/dev/disk') and w[9:].isdigit(): subprocess.run(['hdiutil', 'detach', w], capture_output=True)
for x in ('PANICO', 'fallo de pagina', 'excepcion', 'termino: -'):
    if x in log: print('FALLO:', x); ok = False
print('grabar pantalla y videos:', 'OK' if ok else 'FALLO'); sys.exit(0 if ok else 1)
