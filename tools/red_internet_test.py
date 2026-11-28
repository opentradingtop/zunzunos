#!/usr/bin/env python3
"""Prueba de la Red local por Internet: tres equipos en QEMU, cada uno en su casa (red de usuario de QEMU, detras de
un NAT, sin difusion entre ellas), con los routers falsos de tools/fake_casas.py y el rele tools/zred-rele.py en el
anfitrion, que hace de Internet (IP publicas 198.51.100.x en lo; hace falta ser root para ponerlas).
  casa A (Casa-A, administrador): router con UPnP, IP publica 198.51.100.10
  casa B (Casa-B): router con NAT-PMP (contesta a PCP que no lo sabe), IP 198.51.100.20 y luego 198.51.100.21
  casa C (Casa-C): CGNAT (PCP le da 100.64.7.7: el puerto no sirve), solo se le llega por el rele (198.51.100.99:7142)
Recorre: activar Internet, abrir el puerto (UPnP / NAT-PMP / PCP con CGNAT), rele, invitar por Internet con codigo
(B directo, C con CGNAT), carpeta compartida de C usada desde A por el rele (subir, listar, descargar), impresora de A
usada desde B, mensaje a todos con dos adjuntos, cambio de IP publica de B (anuncio y reencuentro), adjunto grande
cortado a mitad (cable de B quitado 40 s) que se reanuda solo, sondas de un atacante desde Internet y desactivar Internet.
Uso: red_internet_test.py DIR_SALIDA"""
import hashlib, os, random, re, shutil, socket, subprocess, sys, threading, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import imgfs
if len(sys.argv) > 2 and sys.argv[1] == 'sonda':
    # un atacante en Internet contra el puerto abierto de A: nada sin autenticar debe pasar, y acaba bloqueado
    host, port = sys.argv[2], int(sys.argv[3]); res = []
    def tr(line, wait=4):
        s = socket.create_connection((host, port), 5); s.settimeout(wait); s.sendall(line); t0 = time.time()
        try: d = s.recv(200)
        except socket.timeout: d = b'(nada)'
        s.close(); return d, time.time() - t0
    res.append(('invitacion de la red local desde Internet', tr(b'ZRED1|INVITA|12345678|Red|aaaaaaaa|Falso|' + b'00' * 32 + b'\n')))
    res.append(('saludo antiguo (HOLA) desde Internet', tr(b'ZRED1|HOLA|12345678|aaaaaaaa|' + b'00' * 16 + b'\n')))
    res.append(('HOLA2 con etiqueta falsa', tr(b'ZRED1|HOLA2|' + os.urandom(16).hex().encode() + b'|' + os.urandom(16).hex().encode() + b'\n')))
    res.append(('UNIRSE con invitacion inventada', tr(b'ZRED1|UNIRSE|0011223344556677|' + b'11' * 32 + b'|bbbbbbbb|X|' + b'22' * 32 + b'\n')))
    # goteo (slowloris): una letra cada 2 s; el servicio tiene que cortar en unos 8 s
    s = socket.create_connection((host, port), 5); t0 = time.time(); cut = None
    try:
        for ch in b'ZRED1|HOLA2|' * 3:
            s.sendall(bytes([ch])); time.sleep(2)
    except OSError: cut = time.time() - t0
    s.close(); res.append(('goteo', (b'cortado' if cut else b'no cortado', cut or time.time() - t0)))
    for _ in range(4): tr(b'basura\n', 2)
    for n, (d, t) in res: print(f'sonda: {n}: {d!r} ({t:.1f}s)')
    sys.exit(0)
out = sys.argv[1]; os.makedirs(out, exist_ok=True)
for f in ('casas.txt', 'rele.txt'):
    if os.path.exists(os.path.join(out, f)): os.unlink(os.path.join(out, f))
PUB = {'a': '198.51.100.10', 'b': '198.51.100.20', 'b2': '198.51.100.21', 'rele': '198.51.100.99'}
for ip in PUB.values(): subprocess.run(['ip', 'addr', 'add', ip + '/32', 'dev', 'lo'], capture_output=True)
MH = 42 + 9 * 28 + 14; MY = 800 - 36 - MH - 2
FAM = [['rescate', 'files', 'discos', 'papelera', 'restaurar', 'instalar'], ['escritor', 'hoja', 'diapos', 'agenda', 'editor', 'calc', 'pdf'], ['browser', 'apps', 'correo', 'lumen', 'redlocal', 'navjs', 'jsconsola', 'nube'], ['viewer', 'player', 'grabadora', 'paint', 'pantalla', 'video', 'radio'], ['snake', 'mines', 'blocks', 'enjambre'], ['config', 'impresoras', 'term', 'sysmon', 'acerca']]
def M(b):
    f = [i for i, l in enumerate(FAM) if b in l][0]; L = FAM[f]; i = L.index(b); y0 = max(42, min(42 + f * 28, MH - 6 - len(L) * 28)); fy = MY + 42 + 28 * f + 14; ay = MY + y0 + 28 * i + 14
    return ['click:50,782', 'sleep:0.6', f'move:100,{fy}', 'sleep:0.5', f'move:245,{fy}', 'sleep:0.3', f'move:350,{ay}', 'sleep:0.3', f'click:350,{ay}', 'sleep:2']
random.seed(7); FOTO = bytes(random.getrandbits(8) for _ in range(300 * 1024)); GRANDE = os.urandom(12 * 1024 * 1024)
NOTA = 'Nota de prueba de la red local de ZunzunOS por Internet.\n'
PERM = 'browser=1\ncorreo=1\nlumen=1\napps=1\nredlocal=1\n'   # la guardia ya deja salir a Red local (si no, pregunta la primera vez)
def stick(name, files):
    p = os.path.join(out, name); shutil.copy('build/zunzun.img', p); imgfs.put(p, files); return p
A = stick('a.img', {'DOCS/nota.txt': NOTA, 'DOCS/foto.bin': FOTO, 'DOCS/grande.bin': GRANDE, 'ZUNZUN/PERMISOS-RED.TXT': PERM,
                    'ZUNZUN/IMPRESORAS.TXT': 'Samsung ML-1640|file:/sys/DOCS/impreso.qpdl|qpdl|0|0|1|600|Samsung ML-1640 Series\n'})
B = stick('b.img', {'ZUNZUN/PERMISOS-RED.TXT': PERM}); C = stick('c.img', {'ZUNZUN/PERMISOS-RED.TXT': PERM})
NET = {'a': '192.168.10', 'b': '192.168.20', 'c': '192.168.30'}; MON = {'a': 4731, 'b': 4732, 'c': 4733}
def q(img, mac, k): return (f'-device usb-ehci,id=e -device usb-kbd,bus=e.0 -device usb-tablet,bus=e.0 -drive if=none,id=st,format=raw,file={img} -device usb-storage,bus=e.0,drive=st,bootindex=0 '
                            f'-netdev user,id=n1,net={NET[k]}.0/24,host={NET[k]}.2,dhcpstart={NET[k]}.15 -device e1000,netdev=n1,mac={mac} -monitor tcp:127.0.0.1:{MON[k]},server,nowait')
RX, RY = 88, 76
def at(x, y, k='click'): return f'{k}:{RX + x},{RY + y}'
def menu_item(sx, sy, i): return f'click:{RX + sx + 30},{RY + sy + 15 + i * 26}'
def CL(name): return f'clicklog:{name} en (\\d+),(\\d+)'
LA, LB, LC = (os.path.join(out, k, 'serial-bios.log') for k in 'abc')
def LOGIN(k, name): return (['wait:escritorio listo', 'wait:HID puntero', 'wait:vfs: /sys', 'sleep:2', 'move:640,400', 'wait:sesion: escritorio', f'wait:red: IP {NET[k]}.15', 'wait:red servicio listo', 'sleep:1']
                            + M('redlocal') + ['wait:red local abierta', 'sleep:2', at(880, 104), 'sleep:0.8', 'key:ctrl-x', f'type:{name}\n', 'sleep:1'])
INET = [CL('internet'), 'sleep:1']
# --- A: crea la red, activa Internet (UPnP), pone el rele e invita a B y a C con codigos
SA = LOGIN('a', 'Casa-A') + [at(106, 195), 'sleep:0.8', 'key:ctrl-x', 'type:Familia\n', 'wait:red creada', 'sleep:1'] + INET + [
    CL('activar'), 'wait:red internet activado', 'wait:red internet: router UPnP 198.51.100.10:7141', 'wait:red direcciones propias 1: 198.51.100.10:7141', 'sleep:1.5', 'shot:a-internet',
    CL('rele'), 'sleep:1', 'type:198.51.100.99:7142\n', 'wait:red rele 198.51.100.99:7142', 'wait:red esperando en el rele', 'sleep:1',
    CL('invitar'), 'wait:red codigo de invitacion', 'sleep:1', 'shot:a-codigo',
    'wait:red union por internet de Casa-B', 'sleep:1.5', 'shot:a-union', 'click:700,480', 'wait:red invitado por internet', 'sleep:2',
    CL('invitar'), 'waitcount:red codigo de invitacion|2', 'wait:red union por internet de Casa-C', 'sleep:1.5', 'click:700,480', 'waitcount:red invitado por internet|2', 'sleep:2', 'shot:a-equipos',
    'wait:red PING de Casa-B por internet', 'wait:red PING de Casa-C por internet', 'sleep:4',
    # carpeta de C (con CGNAT: solo por el rele) con escritura; A sube, lista y descarga
    at(137, 20), 'sleep:1', at(805, 64), 'sleep:1', menu_item(200, 120, 2), 'sleep:0.8', 'key:ctrl-x', 'type:Fotos\n', 'sleep:0.8', 'key:ret', 'sleep:0.8', menu_item(200, 160, 1), 'wait:red configuracion enviada', 'sleep:1',
    at(300, 106), 'wait:red lista Fotos', 'sleep:0.5', at(430, 96), 'sleep:0.8', 'type:nota.txt\n', 'wait:red subido nota.txt', 'wait:red lista Fotos/: 1', 'sleep:0.5', 'shot:a-carpeta-c',
    at(200, 132), 'sleep:0.8', menu_item(200, 132, 1), 'wait:red descargado /sys/DOCS/Red/nota.txt', 'sleep:0.5',
    # la impresora de A para todos
    at(329, 20), 'sleep:1', at(797, 64), 'sleep:1', menu_item(200, 120, 0), 'wait:red impresora compartida', 'sleep:1',
    # mensaje a todos con dos adjuntos
    at(229, 20), 'sleep:1', CL('nueva'), 'sleep:0.8', CL('todos'), 'sleep:0.5', CL('adjuntar'), 'wait:selector abierto', 'sleep:1', 'type:foto.bin\n', 'wait:red adjunto preparado foto.bin',
    'sleep:0.5', CL('adjuntar'), 'waitcount:selector abierto|2', 'sleep:1', 'type:nota.txt\n', 'wait:red adjunto preparado nota.txt', 'sleep:0.5', 'shot:a-adjuntos',
    CL('campo'), 'sleep:0.3', 'type:Fotos de la familia\n', 'wait:red chat enviado', 'sleep:1', 'shot:a-chat',
    # B cambia de IP publica: A se entera por su anuncio y le sigue llegando
    'wait:red direccion de Casa-B: 198.51.100.21:7141', 'sleep:1', CL('campo'), 'sleep:0.3', 'type:Te llega con la IP nueva?\n', 'waitcount:red chat enviado|2', 'sleep:1',
    # adjunto grande solo a B: B quita el cable a mitad; la entrega se queda en la cola y el servicio la reanuda
    CL('nueva'), 'sleep:0.8', CL('equipo0'), 'sleep:0.5', CL('adjuntar'), 'waitcount:selector abierto|3', 'sleep:1', 'type:grande.bin\n', 'wait:red adjunto preparado grande.bin', 'sleep:0.5',
    CL('campo'), 'sleep:0.3', 'type:Archivo grande\n', 'wait:red mensaje en cola para Casa-B', 'wait:red chat enviado 0/1', 'shot:a-cola',
    'wait:red adjunto grande.bin reanudado desde', 'wait:red pendiente para Casa-B: entregado', 'sleep:2', at(229, 20), 'sleep:1', 'shot:a-entregado',
    'waitfile:' + LB + '|red trabajo enviado', 'wait:red impresion de', 'sleep:1',
    # un atacante desde Internet contra el puerto de A
    'waitfile:' + LB + '|red puerto del router cerrado', 'waitfile:' + LC + '|red adjunto abierto',
    f'sh:python3 -I tools/red_internet_test.py sonda {PUB["a"]} 7141', 'sleep:2', 'wait:red bloqueada', 'sleep:1']
# --- B: se une con el codigo (directo a A), NAT-PMP, recibe, cambia de IP, corte a mitad de un adjunto, imprime en A
SB = LOGIN('b', 'Casa-B') + INET + ['waitfile:' + LA + '|red codigo de invitacion', CL('unirse'), 'sleep:1', 'typefrom:' + LA + '|red codigo de invitacion (\\S+)', 'type:\n',
    'wait:red union: codigo', 'sleep:1.5', 'shot:b-union', 'click:700,480', 'wait:red unido', 'wait:red internet: router NAT-PMP 198.51.100.20:7141', 'wait:red direcciones propias 1: 198.51.100.20:7141', 'sleep:2', 'shot:b-internet',
    'wait:red mensaje de Casa-A con 2 adjuntos', 'sleep:1', 'shot:b-recibido',
    f'sh:echo "ip casaB {PUB["b2"]}" > {out}/casas-orden.txt', 'sleep:3', CL('comprobar'), 'wait:red direcciones propias 2: 198.51.100.21:7141', 'wait:red mensaje de Casa-A con 0 adjuntos', 'sleep:1',
    'wait:red recibiendo 1 adjuntos de Casa-A', 'wait:red adjunto grande.bin: 2 MB', 'hmp:set_link n1 off', 'sleep:40', 'hmp:set_link n1 on', 'wait:red adjunto grande.bin cortado en', 'wait:red mensaje de Casa-A con 1 adjuntos', 'sleep:1',
    'wait:(1 impresoras)', 'sleep:1', 'key:alt-f4', 'sleep:1'] + M('agenda') + ['sleep:1', 'key:ctrl-p', 'wait:dialogo imprimir abierto', 'sleep:1.2', 'shot:b-imprimir', 'click:955,625', 'wait:red trabajo enviado', 'sleep:1', 'key:alt-f4', 'sleep:1'] + M('redlocal') + ['wait:red local abierta', 'sleep:2'] + INET + [CL('activar'), 'wait:red internet desactivado', 'wait:red puerto del router cerrado', 'sleep:1']
# --- C: CGNAT, se une con el codigo, espera en el rele; su carpeta la usa A por el rele; abre un adjunto recibido
SC = LOGIN('c', 'Casa-C') + INET + ['waitfile:' + LA + '|red codigo de invitacion|2', CL('unirse'), 'sleep:1', 'typefrom:' + LA + '|red codigo de invitacion (\\S+)', 'type:\n',
    'wait:red union: codigo', 'sleep:1.5', 'click:700,480', 'wait:red unido', 'wait:red internet: router PCP 100.64.7.7:7141 (CGNAT)', 'wait:red esperando en el rele', 'sleep:2', 'shot:c-internet',
    'wait:red ESCRIBE de Casa-A por internet', 'wait:red mensaje de Casa-A con 2 adjuntos', 'sleep:1', at(229, 20), 'sleep:1.5', CL('conv0'), 'sleep:1.5', 'shot:c-mensaje', CL('adjunto0'), 'wait:red adjunto abierto', 'sleep:3', 'shot:c-adjunto', 'waitcount:red mensaje de Casa-A|2']
res = {}
def run(name, steps, img, mac, port):
    res[name] = subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', 'bios', '--no-disk', '--abs', '--out', os.path.join(out, name), '--timeout', '400', '--port', str(port), '--qemu', q(img, mac, name)] + steps, stdout=open(os.path.join(out, name + '.txt'), 'w'))
rt = subprocess.Popen(['python3', '-I', 'tools/fake_casas.py', out, f'casaA:{NET["a"]}:{PUB["a"]}:upnp:{MON["a"]}', f'casaB:{NET["b"]}:{PUB["b"]}:natpmp:{MON["b"]}', f'casaC:{NET["c"]}:100.64.7.7:cgnat:{MON["c"]}'])
rl = subprocess.Popen(['python3', '-I', 'tools/zred-rele.py', '--direccion', PUB['rele'], '--puerto', '7142', '--registro', os.path.join(out, 'rele.txt')]); time.sleep(1)
th = []
for k, st, img, mac, port in (('a', SA, A, '52:54:00:aa:00:01', 4741), ('b', SB + ['sleep:5'], B, '52:54:00:bb:00:02', 4742), ('c', SC + ['sleep:5'], C, '52:54:00:cc:00:03', 4743)):
    t = threading.Thread(target=run, args=(k, st, img, mac, port)); t.start(); th.append(t); time.sleep(2)
for t in th: t.join()
rt.kill(); rl.kill()
L = {k: open(os.path.join(out, k, 'serial-bios.log'), errors='replace').read() for k in 'abc'}
ok = all(res.get(k) == 0 for k in 'abc')
for k in 'abc':
    if res.get(k) != 0: print(f'FALLO: el equipo {k.upper()} no termino sus pasos (ver {out}/{k}.txt)')
CHK = {'a': ('red internet: router UPnP 198.51.100.10:7141', 'red invitado por internet', 'red subido nota.txt', 'red descargado /sys/DOCS/Red/nota.txt', 'red chat enviado 2/2', 'red direccion de Casa-B: 198.51.100.21:7141',
             'red adjunto grande.bin reanudado desde', 'red pendiente para Casa-B: entregado', 'red esperando en el rele', 'red llamada por el rele', 'red impresion de', 'red invitacion desde Internet rechazada', 'red bloqueada'),
       'b': ('red unido', 'router NAT-PMP 198.51.100.20:7141', 'red direcciones propias 2: 198.51.100.21:7141', 'red mensaje de Casa-A con 2 adjuntos', 'red mensaje de Casa-A con 1 adjuntos', 'red trabajo enviado', 'red puerto del router cerrado'),
       'c': ('red unido', '(CGNAT)', 'red esperando en el rele', 'red llamada por el rele', 'red ESCRIBE de Casa-A por internet', 'red mensaje de Casa-A con 2 adjuntos', 'red adjunto abierto')}
for k, what in CHK.items():
    for w in what:
        if w not in L[k]: print(f'FALLO: {k.upper()}: falta {w!r}'); ok = False
for k in 'abc':
    for x in ('PANICO', 'fallo de pagina', 'excepcion', 'termino: -'):
        if x in L[k]: print(f'FALLO: {k.upper()}: {x}'); ok = False
m = re.search(r'red trabajo enviado a .*: (\d+) bytes, (-?\d+)', L['b'])
if not m or m.group(2) != '0': print('FALLO: impresion de B en la impresora de A', m and m.group(0)); ok = False
def same(img, path, data, what):
    global ok
    d = imgfs.get(img, path)
    if d != data: print(f'FALLO: {what}: {path} {"no esta" if d is None else f"distinto ({len(d)} bytes)"}'); ok = False
    else: print(f'ok {what}: {path} ({len(d)} bytes, sha256 {hashlib.sha256(d).hexdigest()[:12]})')
same(B, 'DOCS/Red/Adjuntos/foto.bin', FOTO, 'adjunto en B'); same(B, 'DOCS/Red/Adjuntos/nota.txt', NOTA.encode(), 'adjunto en B'); same(B, 'DOCS/Red/Adjuntos/grande.bin', GRANDE, 'adjunto reanudado en B')
same(C, 'DOCS/Red/Adjuntos/foto.bin', FOTO, 'adjunto en C por el rele'); same(C, 'DOCS/Compartida/nota.txt', NOTA.encode(), 'subido a la carpeta de C por el rele')
d = imgfs.get(A, 'DOCS/impreso.qpdl')
if not d or b'QPDL' not in d[:200]: print('FALLO: el trabajo de B no llego a la impresora de A'); ok = False
son = open(os.path.join(out, 'a.txt'), errors='replace').read()
for n in ('invitacion de la red local desde Internet', 'saludo antiguo (HOLA)', 'UNIRSE con invitacion inventada', 'HOLA2 con etiqueta falsa'):
    mm = re.search(re.escape(n) + r".*?: b'(.*?)'", son)
    if not mm or mm.group(1) not in ('', 'NO\\n', '(nada)'): print(f'FALLO: sonda {n}: {mm and mm.group(1)}'); ok = False
mm = re.search(r"sonda: goteo: b'(\w+ ?\w*)' \(([\d.]+)s\)", son)
if not mm or mm.group(1) != 'cortado' or float(mm.group(2)) > 12: print('FALLO: el goteo no se corto a tiempo', mm and mm.group(0)); ok = False
casas = open(os.path.join(out, 'casas.txt')).read(); rele = open(os.path.join(out, 'rele.txt')).read()
if 'hostfwd_add n1 tcp:198.51.100.21:7141' not in casas: print('FALLO: el router de B no volvio a abrir el puerto en la IP nueva'); ok = False
if 'unido' not in rele: print('FALLO: el rele no unio ninguna llamada'); ok = False
print('red por Internet:', 'OK' if ok else 'FALLO'); sys.exit(0 if ok else 1)
