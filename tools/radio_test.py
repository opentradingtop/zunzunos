#!/usr/bin/env python3
"""Prueba de la Radio contra tools/fake_radio.py (catalogo y emision en el puerto 8096):
la lista de populares (solo MP3: la emisora AAC no debe salir), tocar una emisora que redirige, el titulo de los
metadatos ICY, marcarla como favorita, pararla, y la lista de paises. Lo que sale por la tarjeta de sonido de QEMU
se graba en un WAV: tienen que oirse los dos tonos de la emision (440 Hz a la izquierda y 660 Hz a la derecha).
Uso: radio_test.py DIR_SALIDA"""
import os, re, shutil, struct, subprocess, sys, tempfile, time
out = sys.argv[1]; os.makedirs(out, exist_ok=True); img = os.path.join(out, 'stick.img'); shutil.copy('build/zunzun.img', img)
# el tono MP3, con el codificador propio (compilado en el Mac con el z.h de las pruebas de MP3)
b = os.path.join(out, 'mktono'); os.makedirs(b, exist_ok=True)
for f in ['user/lib/zmp3.c', 'user/lib/mp3tab.h', 'tools/pruebas/mp3/z.h', 'tools/pruebas/radio/mktono.c']: shutil.copy(f, b)
subprocess.check_call(['cc', '-O2', '-w', '-std=gnu11', '-I' + b, '-o', os.path.join(b, 'mktono'), os.path.join(b, 'mktono.c'), os.path.join(b, 'zmp3.c'), '-lm'])
tono = os.path.join(out, 'tono.mp3'); subprocess.check_call([os.path.join(b, 'mktono'), tono], stdout=subprocess.DEVNULL)


def stick(fn):
    dev = subprocess.check_output(['hdiutil', 'attach', '-nomount', '-imagekey', 'diskimage-class=CRawDiskImage', img]).decode().split()[0]; mnt = tempfile.mkdtemp()
    try:
        subprocess.check_call(['mount', '-t', 'msdos', dev + 's1', mnt]); r = fn(mnt)
        for junk in ['.fseventsd', '.Spotlight-V100', '.Trashes']: shutil.rmtree(os.path.join(mnt, junk), ignore_errors=True)
        return r
    finally:
        subprocess.call(['umount', mnt]); subprocess.call(['hdiutil', 'detach', '-quiet', dev]); os.rmdir(mnt)


def setup(m):
    os.makedirs(os.path.join(m, 'ZUNZUN'), exist_ok=True); open(os.path.join(m, 'ZUNZUN', 'RADIO.TXT'), 'w').write('servidor=http://10.0.2.2:8096\npais=ES\n')
stick(setup)
web = os.path.join(out, 'radio'); shutil.rmtree(web, ignore_errors=True)
log_fr = open(os.path.join(out, 'fake_radio.log'), 'w'); fr = subprocess.Popen(['python3', '-I', 'tools/fake_radio.py', web, tono], stdout=log_fr, stderr=log_fr); time.sleep(1)
SND = os.path.abspath(os.path.join(out, 'sonido.wav'))
MH = 42 + 9 * 28 + 14; MY = 800 - 36 - MH - 2
FAM = [['rescate', 'files', 'discos', 'papelera', 'restaurar', 'instalar', 'forense'], ['escritor', 'hoja', 'diapos', 'agenda', 'editor', 'calc', 'pdf'], ['browser', 'apps', 'correo', 'lumen', 'redlocal', 'navjs', 'jsconsola', 'nube'], ['viewer', 'player', 'grabadora', 'paint', 'pantalla', 'video', 'radio'], ['snake', 'mines', 'blocks', 'enjambre'], ['config', 'impresoras', 'term', 'sysmon', 'acerca']]
def M(b):
    f = [i for i, l in enumerate(FAM) if b in l][0]; L = FAM[f]; i = L.index(b); y0 = max(42, min(42 + f * 28, MH - 6 - len(L) * 28)); fy = MY + 42 + 28 * f + 14; ay = MY + y0 + 28 * i + 14
    return ['click:50,782', 'sleep:0.6', f'move:100,{fy}', 'sleep:0.5', f'move:245,{fy}', 'sleep:0.3', f'move:350,{ay}', 'sleep:0.3', f'click:350,{ay}', 'sleep:2']
s = ['wait:escritorio listo', 'sleep:1'] + M('radio') + ['wait:radio emisoras', 'wait:radio lista', 'sleep:1', 'shot:r-lista',
     'clicklog:fila0 en (\\d+),(\\d+)', 'wait:radio sonando Radio Tono de Prueba', 'wait:radio titulo: Tono de prueba - ZunzunOS', 'sleep:7', 'shot:r-sonando',
     'clicklog:favorita en (\\d+),(\\d+)', 'wait:radio favoritas 1', 'sleep:0.5', 'clicklog:parar en (\\d+),(\\d+)', 'wait:radio parada Radio Tono', 'sleep:1',
     'clicklog:paises en (\\d+),(\\d+)', 'wait:radio paises: 2', 'sleep:0.5', 'shot:r-paises']
q = (f'-nic user,model=e1000 -audiodev wav,id=au,path={SND} -device intel-hda -device hda-output,audiodev=au -device usb-ehci,id=e -device usb-kbd,bus=e.0 -device usb-tablet,bus=e.0 '
     f'-drive if=none,id=st,format=raw,file={img} -device usb-storage,bus=e.0,drive=st,bootindex=0')
try:
    r = subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', 'bios', '--no-disk', '--abs', '--out', out, '--timeout', '60', '--port', '4702', '--qemu', q] + s)
finally:
    fr.kill()
log = open(os.path.join(out, 'serial-bios.log'), errors='replace').read(); bad = []
m = re.search(r'radio emisoras [^:]*: (\d+)', log)
if not m or m.group(1) != '2': bad.append('la lista inicial deberia tener 2 emisoras MP3 (la AAC fuera): %s' % (m and m.group(0)))
m = re.search(r'radio parada Radio Tono de Prueba: (\d+) bytes, (\d+) cuadros', log)
if not m or int(m.group(2)) < 200: bad.append('se decodificaron pocos cuadros: %s' % (m and m.group(0)))
for need in ['radio sonando Radio Tono de Prueba: 44100 Hz, 2 canales', 'radio metadatos cada 16000 bytes', 'radio titulo: Tono de prueba - ZunzunOS', 'radio favoritas 1', 'radio paises: 2']:
    if need not in log: bad.append('falta en el registro: ' + need)
pet = open(os.path.join(web, 'peticiones.txt')).read() if os.path.exists(os.path.join(web, 'peticiones.txt')) else ''
if 'codec=MP3' not in pet or '/json/url/uuid-tono' not in pet or '/emision | 1' not in pet: bad.append('peticiones al catalogo o a la emision: ' + pet[:300].replace('\n', ' / '))
# el sonido: tramo con senal y frecuencia de cada canal por cruces por cero
try:
    raw = open(SND, 'rb').read(); ch = struct.unpack('<H', raw[22:24])[0]; rate = struct.unpack('<I', raw[24:28])[0]; d = raw[44:]; d = d[:len(d) // (2 * ch) * 2 * ch]   # QEMU no cierra la cabecera si se corta
    a = struct.unpack('<%dh' % (len(d) // 2), d); L = a[0::ch]; R = a[1::ch] if ch > 1 else L
    loud = [i for i in range(0, len(L), rate // 10) if max(abs(x) for x in L[i:i + rate // 10] or [0]) > 2000]
    secs = len(loud) / 10
    def freq(x):
        if not loud: return 0
        i0 = loud[len(loud) // 4]; seg = x[i0:i0 + rate]; z = sum(1 for k in range(1, len(seg)) if (seg[k - 1] < 0) != (seg[k] < 0)); return z / 2 / (len(seg) / rate)
    fl, fr_ = freq(L), freq(R)
    print('sonido: %.1f s con senal, izquierda %.0f Hz, derecha %.0f Hz' % (secs, fl, fr_))
    if secs < 4 or abs(fl - 440) > 15 or abs(fr_ - 660) > 15: bad.append('el sonido no es el de la emision (%.1f s, %.0f Hz y %.0f Hz)' % (secs, fl, fr_))
except Exception as e: bad.append('no se pudo leer el sonido: %s' % e)
for x in ('PANICO', 'fallo de pagina', 'excepcion', 'termino: -'):
    if x in log: bad.append(x)
print('radio:', 'OK (catalogo, emision con redireccion y metadatos, favoritas y sonido)' if r == 0 and not bad else 'FALLO\n  ' + '\n  '.join(bad + ([] if r == 0 else ['vm=%d' % r]))); sys.exit(0 if r == 0 and not bad else 1)
