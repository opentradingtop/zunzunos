#!/usr/bin/env python3
"""Prueba del audio: la Grabadora graba en MP3 estereo, convierte a MP3 el tono de prueba (WAV de 22 kHz mono) y Musica
abre y toca ese MP3. QEMU guarda en un WAV lo que suena por la tarjeta; aqui se comprueba que es el tono (misma
frecuencia y duracion). Uso: audio_test.py DIR_SALIDA"""
import math, os, shutil, struct, subprocess, sys
out = sys.argv[1]; os.makedirs(out, exist_ok=True); SND = os.path.join(out, 'sonido.wav')
MH = 42 + 9 * 28 + 14; MY = 800 - 36 - MH - 2
FAM = [['rescate', 'files', 'discos', 'papelera', 'restaurar', 'instalar'], ['escritor', 'hoja', 'diapos', 'agenda', 'editor', 'calc', 'pdf'], ['browser', 'apps', 'correo', 'lumen', 'redlocal', 'navjs', 'jsconsola', 'nube'], ['viewer', 'player', 'grabadora', 'paint', 'pantalla', 'video', 'radio'], ['snake', 'mines', 'blocks', 'enjambre'], ['config', 'impresoras', 'term', 'sysmon', 'acerca', 'alquimia']]
k = [0]
def M(b, h=500):
    f = [i for i, l in enumerate(FAM) if b in l][0]; L = FAM[f]; i = L.index(b); y0 = max(42, min(42 + f * 28, MH - 6 - len(L) * 28)); fy = MY + 42 + 28 * f + 14; ay = MY + y0 + 28 * i + 14
    k[0] += 1; c = k[0] % 8; y = 50 + 26 * c; y = 26 if y + h + 40 > 800 else y
    return (60 + 28 * c, y), ['click:50,782', 'sleep:0.6', f'move:100,{fy}', 'sleep:0.5', f'move:245,{fy}', 'sleep:0.3', f'move:350,{ay}', 'sleep:0.3', f'click:350,{ay}', 'sleep:2']
def at(o, x, y): return f'click:{o[0] + x},{o[1] + y}'
def steps(record):
    k[0] = 0; s = ['wait:escritorio listo', 'sleep:1']
    _, st = M('term'); s += st + ['type:mkdir /sys/DOCS\n', 'sleep:0.3', 'type:mkdir /sys/DOCS/GRABACIONES\n', 'sleep:0.3', 'type:cp /rd/res/tono.wav /sys/DOCS/GRABACIONES/tono.wav\n', 'sleep:0.5', 'key:alt-f4', 'sleep:0.6']
    g, st = M('grabadora', 400); s += st; row = 156 + 8
    if record:   # MP3 (el boton WAV/MP3) y grabar 2,5 s; la lista va por nombre al reves: voz-... primero, tono.wav despues
        s += [at(g, 160, 92), 'sleep:0.4', 'shot:a-opciones', 'key:spc', 'wait:grabacion iniciada mp3 2', 'sleep:2.5', 'key:spc', 'wait:grabacion guardada', 'sleep:0.5']; row += 22
    s += [at(g, 60, row), 'sleep:0.3', at(g, 220, 400 - 22), 'wait:mp3 convertido', 'sleep:0.5', 'shot:a-convertido', at(g, 50, 400 - 22), 'wait:mp3 abierto', 'sleep:1.5', 'shot:a-musica', 'key:spc', 'wait:reproduccion terminada', 'sleep:3']   # lo ultimo aun esta en el bufer de la tarjeta
    return s
logs = ''
for name, q, rec in [('grabar', '-audiodev none,id=au -device intel-hda -device hda-duplex,audiodev=au', 1), ('sonido', f'-audiodev wav,id=au,path={SND} -device intel-hda -device hda-output,audiodev=au', 0)]:
    o = os.path.join(out, name); os.makedirs(o, exist_ok=True); shutil.copy('build/zunzun.img', os.path.join(o, 'stick.img'))
    q += f' -device usb-ehci,id=e -device usb-kbd,bus=e.0 -device usb-tablet,bus=e.0 -drive if=none,id=st,format=raw,file={o}/stick.img -device usb-storage,bus=e.0,drive=st,bootindex=0'
    r = subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', 'bios', '--no-disk', '--abs', '--out', o, '--timeout', '90', '--port', '4641', '--qemu', q] + steps(rec))
    if r: print('audio: FALLO (vm, %s)' % name); sys.exit(1)
    logs += open(os.path.join(o, 'serial-bios.log'), errors='replace').read()
log = logs
def need(t):
    if t not in log: print('audio: FALLO, falta:', t); sys.exit(1)
for t in ['grabacion iniciada mp3 2', 'mp3 convertido', 'mp3 abierto 44100 Hz 1 canales', 'reproduccion terminada']: need(t)
import re
g_ = re.search(r'grabacion guardada (\d+) bytes', log); kb = int(g_.group(1)) if g_ else 0
if kb < 30000 or kb % 384: print('audio: FALLO, grabacion MP3 de', kb, 'bytes (cuadros de 384 bytes a 128 kbps y 48 kHz)'); sys.exit(1)
# lo que sono: buscar el tramo con sonido y su frecuencia (cruces por cero) y compararlo con el original
def wav(p):
    d = open(p, 'rb').read(); o = 12; ch = rate = 0
    while o + 8 <= len(d):
        l = struct.unpack_from('<I', d, o + 4)[0]
        if d[o:o + 4] == b'fmt ': ch, rate = struct.unpack_from('<HI', d, o + 10)
        if d[o:o + 4] == b'data':   # QEMU deja la longitud a 0 si no termina limpio
            n = min(l or len(d), len(d) - o - 8) // 2; x = struct.unpack_from('<%dh' % n, d, o + 8); return rate, [x[i] for i in range(0, n, ch)]
        o += 8 + l + (l & 1)
def info(rate, x):
    th = max(abs(v) for v in x) * 0.1; idx = [i for i, v in enumerate(x) if abs(v) > th]; a, b = idx[0], idx[-1]
    seg = x[a:b]; zc = sum(1 for i in range(1, len(seg)) if (seg[i - 1] < 0) != (seg[i] < 0)); dur = (b - a) / rate
    return dur, zc / 2 / dur, math.sqrt(sum(v * v for v in seg) / len(seg))
d0, f0, r0 = info(*wav('user/res/tono.wav')); d1, f1, r1 = info(*wav(SND))
print('original: %.3f s, %.1f Hz, rms %.0f | sonado: %.3f s, %.1f Hz, rms %.0f' % (d0, f0, r0, d1, f1, r1))
if abs(d1 - d0) > 0.05 or abs(f1 - f0) > f0 * 0.02 or not 0.3 < r1 / r0 < 3: print('audio: FALLO, el sonido no coincide'); sys.exit(1)
print('audio: OK')
