#!/usr/bin/env python3
"""Arnes de pruebas: arranca QEMU y ejecuta pasos.

  vm.py --mode bios|uefi --img X --out DIR [--qemu "args"] paso paso ...

Pasos: wait:TEXTO  sleep:SEG  shot:NOMBRE  key:ctrl-a  hold:a,1.5  type:texto
       move:X,Y  click:X,Y  rclick:X,Y  dclick:X,Y  drag:X,Y,X2,Y2  hmp:COMANDO

Antes de la primera accion con teclado o raton, si el escritorio muestra la pantalla de inicio de
sesion, entra como administrador (y pulsa 'Ahora no' si pide crear un PIN). --no-login lo desactiva.
"""
import argparse, json, os, shlex, socket, subprocess, sys, time

OVMF = '/opt/homebrew/share/qemu/edk2-x86_64-code.fd'
ap = argparse.ArgumentParser()
ap.add_argument('--mode', default='bios')
ap.add_argument('--img', default='build/zunzun.img')
ap.add_argument('--out', required=True)
ap.add_argument('--qemu', default='')
ap.add_argument('--mem', default='512')
ap.add_argument('--timeout', type=float, default=120)
ap.add_argument('--port', type=int, default=4551)
ap.add_argument('--no-disk', action='store_true')
ap.add_argument('--abs', action='store_true')
ap.add_argument('--size', default='1280,800')
ap.add_argument('--no-login', action='store_true', help='no entrar solo en la pantalla de inicio de sesion')
ap.add_argument('steps', nargs='*')
a = ap.parse_args()
os.makedirs(a.out, exist_ok=True)
log = os.path.join(a.out, f'serial-{a.mode}.log')
open(log, 'w').close()
cmd = ['qemu-system-x86_64', '-m', a.mem, '-display', 'none', '-serial', f'file:{log}', '-no-reboot',
       '-rtc', 'base=localtime', '-qmp', f'tcp:127.0.0.1:{a.port},server,nowait', '-device', 'VGA,vgamem_mb=32']
if a.mode == 'uefi':
    cmd += ['-drive', f'if=pflash,format=raw,readonly=on,file={OVMF}']
if not a.no_disk:
    cmd += ['-drive', f'format=raw,file={a.img}']
cmd += shlex.split(a.qemu)
p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
s = None
for _ in range(100):
    try:
        s = socket.create_connection(('127.0.0.1', a.port), 1); break
    except OSError:
        time.sleep(0.1)
if not s:
    print(p.stdout.read().decode()); sys.exit('QEMU no arranco')
f = s.makefile('rw')
f.readline()
def qmp(cmd, **args):
    f.write(json.dumps({'execute': cmd, 'arguments': args}) + '\n'); f.flush()
    while True:
        r = json.loads(f.readline())
        if 'return' in r or 'error' in r:
            return r
qmp('qmp_capabilities')
def serial():
    return open(log, errors='replace').read()
W, H = map(int, a.size.split(','))
mx, my = W // 2, H // 2
def rel(dx, dy):
    global mx, my
    while dx or dy:
        sx = max(-100, min(100, dx)); sy = max(-100, min(100, dy))
        qmp('input-send-event', events=[{'type': 'rel', 'data': {'axis': 'x', 'value': sx}}, {'type': 'rel', 'data': {'axis': 'y', 'value': sy}}])
        dx -= sx; dy -= sy; mx += sx; my += sy
        time.sleep(0.03)
def goto(x, y):
    global mx, my
    if a.abs:
        mx, my = x, y
        qmp('input-send-event', events=[{'type': 'abs', 'data': {'axis': 'x', 'value': x * 32767 // (W - 1)}}, {'type': 'abs', 'data': {'axis': 'y', 'value': y * 32767 // (H - 1)}}]); time.sleep(0.2); return
    # se pega a la esquina para no acumular error y va al destino
    rel(x - mx, y - my); time.sleep(0.25)
def btn(b, down):
    qmp('input-send-event', events=[{'type': 'btn', 'data': {'button': b, 'down': down}}]); time.sleep(0.08)
def click(b='left'):
    btn(b, True); btn(b, False); time.sleep(0.12)
KEYMAP = {';': ('shift', 'comma'), 'ñ': 'semicolon', ' ': 'spc', '\n': 'ret', '.': 'dot', ',': 'comma', '-': 'slash', '/': ('shift', '7'), ':': ('shift', 'dot'), '_': ('shift', 'slash'), '=': ('shift', '0'), '?': ('shift', 'minus'), '"': ('shift', '2'), '*': ('shift', 'bracket_right'), '+': 'bracket_right', '(': ('shift', '8'), ')': ('shift', '9'), '<': 'less', '>': ('shift', 'less'), '@': ('alt_r', '2')}
def keys(names):
    qmp('send-key', keys=[{'type': 'qcode', 'data': n} for n in names]); time.sleep(0.09)
def typ(t):
    for c in t:
        if c == '^':
            keys(['shift', 'bracket_left']); keys(['spc']); continue
        k = KEYMAP.get(c)
        if k is None:
            k = c.lower() if not c.isupper() else ('shift', c.lower())
        keys(list(k) if isinstance(k, tuple) else [k])
logins = 0
def ensure_login():
    """Entra como administrador si hay una pantalla de inicio de sesion pendiente (sistemas sin PIN)."""
    global logins
    s = serial(); n = s.count('sesion: pantalla de inicio')
    if a.no_login or n <= logins: return
    logins = n; base = s.count('sesion: escritorio'); crear = s.count('sesion: crear pin')
    cx, cy = W // 2, H // 2; goto(cx - 120, cy); click(); t0 = time.time()
    while time.time() - t0 < 45:
        s = serial()
        if s.count('sesion: escritorio') > base: break
        if s.count('sesion: crear pin') > crear: crear += 1; time.sleep(0.4); goto(cx, cy + 203); click()
        time.sleep(0.2)
    else: print('FALLO: no se pudo entrar en la sesion')
    print(f'ok sesion iniciada ({time.time()-t0:.1f}s)'); time.sleep(0.8)
ok = True
try:
    for st in a.steps:
        k, _, v = st.partition(':')
        if k in ('key', 'hold', 'type', 'move', 'click', 'rclick', 'dclick', 'drag'): ensure_login()
        if k == 'wait':
            t0 = time.time()
            while v not in serial():
                if time.time() - t0 > a.timeout or p.poll() is not None:
                    print(f'FALLO: no aparecio {v!r}'); ok = False; break
                time.sleep(0.2)
            if not ok: break
            print(f'ok wait {v!r} ({time.time()-t0:.1f}s)')
        elif k == 'sleep': time.sleep(float(v))
        elif k == 'sh': print('sh>', subprocess.run(v, shell=True, capture_output=True, text=True, timeout=60).stdout.strip()[:300])
        elif k == 'shot':
            r = qmp('screendump', filename=os.path.abspath(os.path.join(a.out, v + '.png')), format='png'); print('shot', v, r.get('error', ''))
        elif k == 'key': keys(v.split('-'))
        elif k == 'hold':   # hold:TECLA,SEGUNDOS mantiene pulsada una tecla (para la repeticion)
            n, sec = v.split(','); qmp('send-key', keys=[{'type': 'qcode', 'data': x} for x in n.split('-')], **{'hold-time': int(float(sec) * 1000)}); time.sleep(float(sec) + 0.2)
        elif k == 'type': typ(v.replace('\\n', '\n'))
        elif k == 'move': goto(*map(int, v.split(',')))
        elif k in ('click', 'rclick', 'dclick'):
            goto(*map(int, v.split(',')))
            click('right' if k == 'rclick' else 'left')
            if k == 'dclick': click()
        elif k == 'drag':
            x, y, x2, y2 = map(int, v.split(',')); goto(x, y); btn('left', True); time.sleep(0.2); goto(x2, y2); time.sleep(0.2); btn('left', False)
        elif k == 'exit':
            t0 = time.time()
            while p.poll() is None and time.time() - t0 < float(v or 15): time.sleep(0.1)
            print('qemu termino' if p.poll() is not None else 'FALLO: qemu sigue vivo'); ok = ok and p.poll() is not None; break
        elif k == 'hmp':
            print(qmp('human-monitor-command', **{'command-line': v}).get('return', ''))
        else: sys.exit('paso desconocido ' + st)
finally:
    if p.poll() is None:
        try: qmp('quit')
        except Exception: pass
    time.sleep(0.3)
    if p.poll() is None: p.kill()
    out = serial()
    print('--- serie (final) ---'); print('\n'.join(out.replace('\r', '').split('\n')[-int(os.environ.get('TAIL', 25)):]))
    q = p.stdout.read().decode(errors='replace').strip()
    if q: print('--- qemu ---'); print(q[-800:])
sys.exit(0 if ok else 1)
