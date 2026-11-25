#!/usr/bin/env python3
"""Prueba del Lab de Forense en QEMU (fase 2): el Windows sintetico en un disco SATA, un Lab ya configurado en la
memoria USB y el Lab falso (tools/fake_lab.py) en el Mac. Forense manda el expediente anonimizado, el Lab pide
pruebas, el administrador las aprueba (Intro en el dialogo) y el Lab cierra el caso.

Comprueba:
- que al Lab no le llega ningun dato personal;
- que las pruebas se leen bien;
- que la conversacion queda en la memoria USB, legible y con los nombres reales.
Uso: forense_lab_test.py DIR_SALIDA"""
import os, re, shutil, subprocess, sys, tempfile, time
out = sys.argv[1]; os.makedirs(out, exist_ok=True); img = os.path.join(out, 'stick.img'); shutil.copy('build/zunzun.img', img)
win = os.path.join(out, 'win.img'); subprocess.check_call(['python3', '-I', 'tools/mkwindows.py', win], stdout=subprocess.DEVNULL)
labdir = os.path.join(out, 'lab'); shutil.rmtree(labdir, ignore_errors=True)


def stick(fn):
    dev = subprocess.check_output(['hdiutil', 'attach', '-nomount', '-imagekey', 'diskimage-class=CRawDiskImage', img]).decode().split()[0]; mnt = tempfile.mkdtemp()
    try:
        subprocess.check_call(['mount', '-t', 'msdos', dev + 's1', mnt]); r = fn(mnt)
        for junk in ['.fseventsd', '.Spotlight-V100', '.Trashes']: shutil.rmtree(os.path.join(mnt, junk), ignore_errors=True)
        return r
    finally:
        subprocess.call(['umount', mnt]); subprocess.call(['hdiutil', 'detach', '-quiet', dev]); os.rmdir(mnt)


def setup(m):
    os.makedirs(os.path.join(m, 'ZUNZUN'), exist_ok=True)
    open(os.path.join(m, 'ZUNZUN', 'FORENSE.TXT'), 'w').write('preferido=0\nlab0=Lab de pruebas\thttp://10.0.2.2:8090/v1\tzz-lab-prueba\tmodelo-prueba\t1\n')
stick(setup)
os.makedirs(labdir, exist_ok=True); lablog = open(os.path.join(out, 'fake_lab.log'), 'w')   # sus avisos (conexiones cortadas) van a su registro
lab = subprocess.Popen(['python3', '-I', 'tools/fake_lab.py', labdir], stdout=lablog, stderr=lablog); time.sleep(1)
MH = 42 + 9 * 28 + 14; MY = 800 - 36 - MH - 2
FAM = [['rescate', 'files', 'discos', 'papelera', 'restaurar', 'instalar', 'forense'], ['escritor', 'hoja', 'diapos', 'agenda', 'editor', 'calc', 'pdf'], ['browser', 'apps', 'correo', 'lumen', 'redlocal', 'navjs', 'jsconsola', 'nube'], ['viewer', 'player', 'grabadora', 'paint', 'pantalla', 'video', 'radio'], ['snake', 'mines', 'blocks', 'enjambre'], ['config', 'impresoras', 'term', 'sysmon', 'acerca']]
def M(b):
    f = [i for i, l in enumerate(FAM) if b in l][0]; L = FAM[f]; i = L.index(b); y0 = max(42, min(42 + f * 28, MH - 6 - len(L) * 28)); fy = MY + 42 + 28 * f + 14; ay = MY + y0 + 28 * i + 14
    return ['click:50,782', 'sleep:0.6', f'move:100,{fy}', 'sleep:0.5', f'move:245,{fy}', 'sleep:0.3', f'move:350,{ay}', 'sleep:0.3', f'click:350,{ay}', 'sleep:2']
s = ['wait:escritorio listo', 'sleep:1'] + M('forense') + ['wait:forense listo', 'sleep:1', 'clicklog:lab en (\\d+),(\\d+)', 'sleep:1', 'shot:fl-confirmar', 'key:ret',
     'wait:forense lab respuesta ronda 1', 'sleep:1.5', 'shot:fl-pide', 'key:ret', 'wait:forense lab fin', 'sleep:1', 'shot:fl-final', 'clicklog:labs en (\\d+),(\\d+)', 'sleep:1', 'shot:fl-labs']
q = (f'-nic user,model=e1000 -device usb-ehci,id=e -device usb-kbd,bus=e.0 -device usb-tablet,bus=e.0 -drive if=none,id=st,format=raw,file={img} -device usb-storage,bus=e.0,drive=st,bootindex=0 '
     f'-device ich9-ahci,id=a -drive if=none,id=w,format=raw,file={win} -device ide-hd,drive=w,bus=a.0')
try:
    r = subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', 'bios', '--no-disk', '--abs', '--out', out, '--timeout', '90', '--port', '4648', '--qemu', q] + s)
finally:
    lab.kill()
log = open(os.path.join(out, 'serial-bios.log'), errors='replace').read(); bad = []
for need in ['forense lab respuesta ronda 1: pide 4, fin 0', 'forense lab prueba p1 leer_registro ControlSet001\\Services\\iaStorAC', 'forense lab prueba p2 leer_archivo',
             'forense lab respuesta ronda 2: pide 0, fin 1', 'forense lab fin']:
    if need not in log: bad.append('falta en el registro: ' + need)
if os.path.exists(os.path.join(labdir, 'fuga.txt')): bad.append('AL LAB LE LLEGARON DATOS PERSONALES: ' + open(os.path.join(labdir, 'fuga.txt')).read().strip())
comp = open(os.path.join(labdir, 'comprobaciones.txt')).read() if os.path.exists(os.path.join(labdir, 'comprobaciones.txt')) else ''
bad += [l for l in comp.splitlines() if l.startswith('MAL')]
if comp.count('OK ') < 8: bad.append(f'el Lab no comprobo todo ({comp.count("OK ")} de 8)')
def read_case(m):
    root = os.path.join(m, 'FORENSE'); d = sorted(os.listdir(root)) if os.path.isdir(root) else []
    if not d: return '', ''
    c = os.path.join(root, d[-1]); rd = lambda f: open(os.path.join(c, f), encoding='utf-8', errors='replace').read() if os.path.exists(os.path.join(c, f)) else ''
    return rd('lab.txt'), rd('lab-enviado.txt')
conv, sent = stick(read_case)
open(os.path.join(out, 'lab.txt'), 'w').write(conv)
for need in ['PC-MARTA', 'iaStorAC', 'desactivar_driver', '90%', 'El Lab da el caso por cerrado']:
    if need not in conv: bad.append('falta en lab.txt: ' + need)
if 'PC-MARTA' in sent or '[EQUIPO]' not in sent: bad.append('lab-enviado.txt no esta anonimizado como deberia')
for x in ('PANICO', 'fallo de pagina', 'excepcion'):
    if x in log: bad.append(x)
print('forense lab:', 'OK (expediente anonimizado, pruebas aprobadas, caso cerrado)' if r == 0 and not bad else 'FALLO\n  ' + '\n  '.join(bad)); sys.exit(0 if r == 0 and not bad else 1)
