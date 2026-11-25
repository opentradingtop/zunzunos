#!/usr/bin/env python3
"""Prueba de Forense en QEMU: un Windows sintetico (tools/mkwindows.py) en un disco SATA. Forense tiene que
encontrarlo, reconocerlo (version, equipo, volumen sucio, driver cambiado, reparacion automatica, arranque UEFI) y
guardar el expediente en la memoria USB, que despues se lee desde el Mac. Uso: forense_test.py DIR_SALIDA"""
import os, re, shutil, subprocess, sys, tempfile
out = sys.argv[1]; os.makedirs(out, exist_ok=True); img = os.path.join(out, 'stick.img'); shutil.copy('build/zunzun.img', img)
win = os.path.join(out, 'win.img'); subprocess.check_call(['python3', '-I', 'tools/mkwindows.py', win], stdout=subprocess.DEVNULL)
MH = 42 + 9 * 28 + 14; MY = 800 - 36 - MH - 2
FAM = [['rescate', 'files', 'discos', 'papelera', 'restaurar', 'instalar', 'forense'], ['escritor', 'hoja', 'diapos', 'agenda', 'editor', 'calc', 'pdf'], ['browser', 'apps', 'correo', 'lumen', 'redlocal', 'navjs', 'jsconsola', 'nube'], ['viewer', 'player', 'grabadora', 'paint', 'pantalla', 'video'], ['snake', 'mines', 'blocks', 'enjambre'], ['config', 'impresoras', 'term', 'sysmon', 'acerca']]
def M(b):
    f = [i for i, l in enumerate(FAM) if b in l][0]; L = FAM[f]; i = L.index(b); y0 = max(42, min(42 + f * 28, MH - 6 - len(L) * 28)); fy = MY + 42 + 28 * f + 14; ay = MY + y0 + 28 * i + 14
    return ['click:50,782', 'sleep:0.6', f'move:100,{fy}', 'sleep:0.5', f'move:245,{fy}', 'sleep:0.3', f'move:350,{ay}', 'sleep:0.3', f'click:350,{ay}', 'sleep:2']
s = ['wait:escritorio listo', 'sleep:1'] + M('forense') + ['wait:forense listo', 'sleep:1', 'shot:fo-informe', 'key:ctrl-s', 'wait:forense expediente', 'sleep:1', 'shot:fo-guardado']
q = (f'-device usb-ehci,id=e -device usb-kbd,bus=e.0 -device usb-tablet,bus=e.0 -drive if=none,id=st,format=raw,file={img} -device usb-storage,bus=e.0,drive=st,bootindex=0 '
     f'-device ich9-ahci,id=a -drive if=none,id=w,format=raw,file={win} -device ide-hd,drive=w,bus=a.0')
r = subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', 'bios', '--no-disk', '--abs', '--out', out, '--timeout', '60', '--port', '4647', '--qemu', q] + s)
log = open(os.path.join(out, 'serial-bios.log'), errors='replace').read(); bad = []
for need in ['forense sistemas 1', 'Windows 11 Pro 23H2 (compilación 22631.4460)', ': PC-MARTA,', 'sucio 1', 'forense driver reciente iastorac.sys']:
    if need not in log: bad.append('falta en el registro: ' + need)
m = re.search(r'forense expediente (\S+): 0', log); exp = ''
if not m: bad.append('no se guardo el expediente')
else:
    dev = subprocess.check_output(['hdiutil', 'attach', '-readonly', '-nomount', '-imagekey', 'diskimage-class=CRawDiskImage', img]).decode().split()[0]; mnt = tempfile.mkdtemp()
    try:
        subprocess.check_call(['mount', '-t', 'msdos', '-o', 'rdonly', dev + 's1', mnt]); exp = open(os.path.join(mnt, m.group(1)[len('/sys/'):]), encoding='utf-8', errors='replace').read()
    finally:
        subprocess.call(['umount', mnt]); subprocess.call(['hdiutil', 'detach', '-quiet', dev]); os.rmdir(mnt)
    open(os.path.join(out, 'expediente.txt'), 'w').write(exp)
    for need in ['zz-forense/1', '== SISTEMA ==', 'Equipo: PC-MARTA', 'Último apagado correcto: 24/11/2024 08:30 UTC', 'Instalado: 10/11/2024', 'marcado como dañado',
                 'Boot critical file c:\\windows\\system32\\drivers\\iastorac.sys is corrupt', 'Number of repair attempts: 3', 'WinSxS\\pending.xml',
                 'iastorac.sys', 'EFI/Microsoft/Boot/bootmgfw.efi', 'Servicios y drivers: 312', 'Marta', 'RegBack están vacías', 'No hay volcados']:
        if need not in exp: bad.append('falta en el expediente: ' + need)
for x in ('PANICO', 'fallo de pagina', 'excepcion'):
    if x in log: bad.append(x)
print('forense:', 'OK (Windows reconocido y expediente guardado)' if r == 0 and not bad else 'FALLO\n  ' + '\n  '.join(bad)); sys.exit(0 if r == 0 and not bad else 1)
