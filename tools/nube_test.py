#!/usr/bin/env python3
"""Prueba de Nube (pCloud) contra tools/fake_pcloud.py en el Mac (puerto 8095):
entrar con una contrasena mal y luego bien, listar, guardar un archivo en Documentos, subir otro, crear una carpeta,
entrar y salir de una carpeta y borrar un archivo. Comprueba lo que queda en el pCloud falso, que la contrasena solo
viaja al entrar y que en la memoria se guarda la llave de la sesion y no la contrasena.
Uso: nube_test.py DIR_SALIDA"""
import json, os, re, shutil, subprocess, sys, tempfile, time
out = sys.argv[1]; os.makedirs(out, exist_ok=True); img = os.path.join(out, 'stick.img'); shutil.copy('build/zunzun.img', img)
web = os.path.join(out, 'pcloud'); shutil.rmtree(web, ignore_errors=True)
SUBIR = 'Archivo subido desde ZunzunOS.\n' * 20


def stick(fn):
    dev = subprocess.check_output(['hdiutil', 'attach', '-nomount', '-imagekey', 'diskimage-class=CRawDiskImage', img]).decode().split()[0]; mnt = tempfile.mkdtemp()
    try:
        subprocess.check_call(['mount', '-t', 'msdos', dev + 's1', mnt]); r = fn(mnt)
        for junk in ['.fseventsd', '.Spotlight-V100', '.Trashes']: shutil.rmtree(os.path.join(mnt, junk), ignore_errors=True)
        return r
    finally:
        subprocess.call(['umount', mnt]); subprocess.call(['hdiutil', 'detach', '-quiet', dev]); os.rmdir(mnt)


def setup(m):
    os.makedirs(os.path.join(m, 'ZUNZUN'), exist_ok=True); os.makedirs(os.path.join(m, 'DOCS'), exist_ok=True)
    open(os.path.join(m, 'ZUNZUN', 'NUBE.TXT'), 'w').write('servidor_fijo=http://10.0.2.2:8095\n')
    open(os.path.join(m, 'DOCS', 'subir.txt'), 'w').write(SUBIR)
stick(setup)
lablog = open(os.path.join(out, 'fake_pcloud.log'), 'w')
fp = subprocess.Popen(['python3', '-I', 'tools/fake_pcloud.py', web], stdout=lablog, stderr=lablog); time.sleep(1)
MH = 42 + 9 * 28 + 14; MY = 800 - 36 - MH - 2
FAM = [['rescate', 'files', 'discos', 'papelera', 'restaurar', 'instalar', 'forense'], ['escritor', 'hoja', 'diapos', 'agenda', 'editor', 'calc', 'pdf'], ['browser', 'apps', 'correo', 'lumen', 'redlocal', 'navjs', 'jsconsola', 'nube'], ['viewer', 'player', 'grabadora', 'paint', 'pantalla', 'video', 'radio'], ['snake', 'mines', 'blocks', 'enjambre'], ['config', 'impresoras', 'term', 'sysmon', 'acerca']]
def M(b):
    f = [i for i, l in enumerate(FAM) if b in l][0]; L = FAM[f]; i = L.index(b); y0 = max(42, min(42 + f * 28, MH - 6 - len(L) * 28)); fy = MY + 42 + 28 * f + 14; ay = MY + y0 + 28 * i + 14
    return ['click:50,782', 'sleep:0.6', f'move:100,{fy}', 'sleep:0.5', f'move:245,{fy}', 'sleep:0.3', f'move:350,{ay}', 'sleep:0.3', f'click:350,{ay}', 'sleep:2']
steps = ['wait:escritorio listo', 'sleep:1'] + M('nube') + ['wait:nube formulario', 'sleep:1', 'shot:n-entrar',
         'clicklog:correo en (\\d+),(\\d+)', 'type:prueba@zunzun.test', 'clicklog:clave en (\\d+),(\\d+)', 'type:mal\n', 'wait:nube entrar: fallo', 'sleep:0.5',
         'clicklog:clave en (\\d+),(\\d+)', 'type:clave de prueba\n', 'wait:nube entrar: bien', 'wait:nube carpeta pCloud: 2', 'sleep:1', 'shot:n-raiz']
q = (f'-nic user,model=e1000 -device usb-ehci,id=e -device usb-kbd,bus=e.0 -device usb-tablet,bus=e.0 -drive if=none,id=st,format=raw,file={img} -device usb-storage,bus=e.0,drive=st,bootindex=0')
# primera parte: entrar. Nube anota en el registro donde quedan sus botones y la primera fila; la segunda parte
# (otro arranque, con la sesion ya guardada en la memoria) pulsa en esas posiciones
def vm(steps, tag):
    return subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', 'bios', '--no-disk', '--abs', '--out', os.path.join(out, tag), '--timeout', '60', '--port', '4699', '--qemu', q] + steps)
r1 = vm(steps, 'a')
log1 = open(os.path.join(out, 'a', 'serial-bios.log'), errors='replace').read()
m = re.findall(r'nube botones (\d+),(\d+) (\d+),(\d+) (\d+),(\d+) (\d+),(\d+) (\d+),(\d+) \(fila0 en (\d+),(\d+)\)', log1)
bad = []
if r1 or not m: bad.append('primera parte (entrar): vm=%s, botones=%s' % (r1, bool(m)))
else:
    v = list(map(int, m[-1])); B = [(v[2 * i], v[2 * i + 1]) for i in range(5)]; fx, fy = v[10], v[11]
    rowp = lambda i: (fx + 60, fy + i * 24)
    def menu(i, k, right=False): x, y = rowp(i); return [f'{"rclick" if right else "click"}:{x},{y}', 'sleep:0.7', f'click:{x + 30},{y + 15 + k * 26}']
    # segunda parte: la sesion sigue guardada; guardar hola.txt (fila 1) en Documentos, subir, crear carpeta, entrar y volver, borrar
    s2 = ['wait:escritorio listo', 'sleep:1'] + M('nube') + ['wait:nube carpeta pCloud: 2', 'sleep:1'] + menu(1, 1) + ['wait:nube descargado /sys/DOCS/Nube/hola.txt', 'sleep:0.5',
          f'click:{B[2][0]},{B[2][1]}', 'wait:selector abierto', 'sleep:0.6', 'type:/sys/DOCS/subir.txt\n', 'wait:nube subido subir.txt', 'wait:nube carpeta pCloud: 3', 'sleep:0.5',
          f'click:{B[3][0]},{B[3][1]}', 'sleep:0.8', 'type:Fotos\n', 'wait:nube carpeta creada Fotos', 'wait:nube carpeta pCloud: 4', 'sleep:0.5', 'shot:n-subido',
          f'dclick:{rowp(0)[0]},{rowp(0)[1]}', 'wait:nube carpeta Documentos: 1', 'sleep:0.5', 'shot:n-documentos', f'click:{B[0][0]},{B[0][1]}', 'sleep:1.5']
    # orden tras subir y crear: Documentos, Fotos, hola.txt, subir.txt -> borrar hola.txt (fila 2), opcion Borrar (3)
    s2 += menu(2, 3, right=True) + ['sleep:0.8', 'key:ret', 'wait:nube borrado hola.txt', 'sleep:1', 'shot:n-borrado']
    r2 = vm(s2, 'b'); log2 = open(os.path.join(out, 'b', 'serial-bios.log'), errors='replace').read()
    if r2: bad.append('segunda parte: vm=%s' % r2)
    for need in ['nube descargado /sys/DOCS/Nube/hola.txt: 19 bytes', 'nube subido subir.txt: %d bytes' % len(SUBIR), 'nube carpeta creada Fotos', 'nube carpeta Documentos: 1', 'nube borrado hola.txt']:
        if need not in log2: bad.append('falta en el registro: ' + need)
    for x in ('PANICO', 'fallo de pagina', 'excepcion', 'termino: -'):
        if x in log1 or x in log2: bad.append(x)
fp.kill()
st = json.load(open(os.path.join(web, 'estado.json'))) if os.path.exists(os.path.join(web, 'estado.json')) else {}
names = {(v['name'], v['dir']) for v in st.values()}
if ('subir.txt', False) not in names or ('Fotos', True) not in names or ('hola.txt', False) in names: bad.append('estado de la nube: %s' % sorted(names))
sub = [v for v in st.values() if v['name'] == 'subir.txt']
if sub and sub[0]['size'] != len(SUBIR): bad.append('subir.txt llego con %d bytes' % sub[0]['size'])
pet = open(os.path.join(web, 'peticiones.txt')).read() if os.path.exists(os.path.join(web, 'peticiones.txt')) else ''
if 'MAL' in pet: bad.append('la contrasena viajo fuera de userinfo')
def cfg(mnt):
    p = os.path.join(mnt, 'ZUNZUN', 'NUBE.TXT'); return open(p).read() if os.path.exists(p) else ''
c = stick(cfg)
if 'llave=zz-llave-de-prueba' not in c or 'clave de prueba' in c: bad.append('NUBE.TXT: ' + c.replace('\n', ' | '))
print('nube:', 'OK (entrar, listar, bajar, subir, carpetas y borrar con pCloud)' if not bad else 'FALLO\n  ' + '\n  '.join(bad)); sys.exit(0 if not bad else 1)
