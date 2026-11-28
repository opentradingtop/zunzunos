#!/usr/bin/env python3
"""Prueba de la Red local con dos equipos en QEMU unidos por un cable directo (sin router ni DHCP):
direccion local 169.254, descubrimiento, crear la red en A, invitar a B (B acepta tras ver el codigo), compartir
una carpeta de B con escritura, subir desde A un archivo a la carpeta de B, listar y descargar, mensaje de A a B,
y sacar a B de la red. Con "router", A recibe 192.168.50.20 de un router falso y B no (se pone 169.254), como el X220 en la red de casa
y el Edge con cable directo; despues A pierde el cable y lo recupera sin router y debe pasar a 169.254.
Uso: red_test.py DIR_SALIDA [explorar|router]"""
import os, re, shutil, subprocess, sys, threading, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import imgfs
out = sys.argv[1]; mode = sys.argv[2] if len(sys.argv) > 2 else ''; explore = mode == 'explorar'; router = mode == 'router'; otra = mode == 'otra'; os.makedirs(out, exist_ok=True)
MH = 42 + 9 * 28 + 14; MY = 800 - 36 - MH - 2
FAM = [['rescate', 'files', 'discos', 'papelera', 'restaurar', 'instalar'], ['escritor', 'hoja', 'diapos', 'agenda', 'editor', 'calc', 'pdf'], ['browser', 'apps', 'correo', 'lumen', 'redlocal', 'navjs', 'jsconsola', 'nube'], ['viewer', 'player', 'grabadora', 'paint', 'pantalla', 'video', 'radio'], ['snake', 'mines', 'blocks', 'enjambre'], ['config', 'impresoras', 'term', 'sysmon', 'acerca', 'alquimia']]
def M(b):
    f = [i for i, l in enumerate(FAM) if b in l][0]; L = FAM[f]; i = L.index(b); y0 = max(42, min(42 + f * 28, MH - 6 - len(L) * 28)); fy = MY + 42 + 28 * f + 14; ay = MY + y0 + 28 * i + 14
    return ['click:50,782', 'sleep:0.6', f'move:100,{fy}', 'sleep:0.5', f'move:245,{fy}', 'sleep:0.3', f'move:350,{ay}', 'sleep:0.3', f'click:350,{ay}', 'sleep:2']
def stick(name, docs=None):
    p = os.path.join(out, name); shutil.copy('build/zunzun.img', p)
    if docs: imgfs.put(p, {(n if '/' in n else 'DOCS/' + n): d for n, d in docs.items()})
    return p
B_OLD = {'ZUNZUN/RED-ID.TXT': 'id=aaaaaaaa\nnombre=Equipo-COPIA\nmac=525400aa0001\n', 'ZUNZUN/RED.TXT': 'ZRED1\nred=12345678\nnombre=Copiada\nclave=' + '00' * 32 + '\nadmin=aaaaaaaa\nversion=1\nequipo=aaaaaaaa|Equipo-COPIA\n'}
A = stick('a.img', {'nota.txt': 'Nota de prueba de la red local de ZunzunOS.\n', 'ZUNZUN/IMPRESORAS.TXT': 'Samsung ML-1640|file:/sys/DOCS/impreso.qpdl|qpdl|0|0|1|600|Samsung ML-1640 Series\n'}); B = stick('b.img', B_OLD if otra else None)
def q(img, mac, net): return (f'-device usb-ehci,id=e -device usb-kbd,bus=e.0 -device usb-tablet,bus=e.0 -drive if=none,id=st,format=raw,file={img} -device usb-storage,bus=e.0,drive=st,bootindex=0 '
                              f'-netdev socket,id=n1,{net} -device e1000,netdev=n1,mac={mac}')
# ventana de Red local (920x640): marco en (88, 50), contenido 26 mas abajo; los menus emergentes no tienen barra
RX, RY = 88, 76
def at(x, y, k='click'): return f'{k}:{RX + x},{RY + y}'
def menu_item(sx, sy, i): return f'click:{RX + sx + 30},{RY + sy + 15 + i * 26}'
def LOGIN(addr='wait:sin DHCP, direccion local'): return ['wait:escritorio listo', 'wait:HID puntero', 'wait:vfs: /sys', 'sleep:2', 'move:640,400', 'wait:sesion: escritorio', addr, 'wait:red servicio listo', 'sleep:1']
SA = LOGIN('wait:red: IP 192.168.50.20' if router else 'wait:sin DHCP, direccion local') + M('redlocal') + ['wait:red local abierta', 'sleep:6', 'shot:a-inicio']
SB = LOGIN() + ['sleep:1', 'shot:b-inicio']
if otra:
    # B arranca con la identidad copiada de A (otra MAC): tiene que hacerse una propia; crea su red y A lo anade a la suya
    SB += M('redlocal') + ['wait:red local abierta', 'sleep:1', at(106, 195), 'sleep:0.8', 'key:ctrl-x', 'type:RedB\n', 'wait:red creada', 'sleep:1', 'key:alt-f4', 'wait:red invitacion recibida', 'sleep:1.5', 'shot:b-otra', 'click:700,480', 'wait:red unido', 'sleep:2']
    SA += [at(106, 195), 'sleep:0.8', 'key:ctrl-x', 'type:Casa\n', 'wait:red creada', 'wait:red encontrado', 'sleep:12', 'shot:a-otra', at(825, 320), 'wait:red invitado', 'sleep:2']
if router:
    SA += [at(106, 195), 'sleep:0.8', 'key:ctrl-x', 'type:Casa\n', 'wait:red creada', 'wait:red encontrado', 'sleep:7', 'shot:a-creada', at(825, 320), 'wait:red codigo', 'wait:red invitado', 'sleep:1', at(229, 20), 'sleep:0.8', 'clicklog:nueva en (\\d+),(\\d+)', 'sleep:0.8', 'clicklog:equipo0 en (\\d+),(\\d+)', 'sleep:0.5', 'clicklog:campo en (\\d+),(\\d+)', 'sleep:0.3', 'type:Hola\n', 'wait:red chat enviado 1/1', 'sleep:0.5',
           'hmp:set_link n1 off', 'sleep:3', 'hmp:set_link n1 on', 'wait:cable conectado de nuevo', 'wait:sin DHCP, direccion local', 'sleep:1']
    SB += ['wait:red invitacion recibida', 'sleep:1.5', 'click:700,480', 'wait:red unido', 'wait:red CHAT de', 'sleep:1']
if not explore and not router and not otra:
    # A crea la red "Casa" e invita a B
    SA += [at(106, 195), 'sleep:0.8', 'key:ctrl-x', 'type:Casa\n', 'wait:red creada', 'wait:red encontrado', 'sleep:1', 'shot:a-creada', at(825, 320), 'wait:red codigo', 'sleep:1', 'shot:a-codigo', 'wait:red invitado', 'sleep:2', 'shot:a-invitado']
    # B ve la invitacion con el mismo codigo y acepta
    SB += ['wait:red invitacion recibida', 'sleep:1.5', 'shot:b-invitacion', 'click:700,480', 'wait:red unido', 'sleep:1']
    # A comparte una carpeta de B con escritura, sube un archivo, lo lista y lo descarga
    SA += [at(137, 20), 'sleep:1', at(805, 64), 'sleep:1', menu_item(200, 120, 1), 'sleep:0.8', 'key:ctrl-x', 'type:Fotos\n', 'sleep:0.8', 'key:ret', 'sleep:0.8', menu_item(200, 160, 1), 'wait:red configuracion enviada', 'sleep:1', 'shot:a-carpetas',
           at(300, 106), 'wait:red lista Fotos', 'sleep:0.5', at(430, 96), 'sleep:0.8', 'type:nota.txt\n', 'wait:red subido nota.txt', 'wait:red lista Fotos/: 1', 'sleep:0.5', 'shot:a-subido',
           at(200, 132), 'sleep:0.8', menu_item(200, 132, 1), 'wait:red descargado /sys/DOCS/Red/nota.txt', 'sleep:0.5']
    SB += ['wait:red configuracion', 'wait:red ESCRIBE de', 'sleep:1']
    # A empieza una conversacion con B
    SA += [at(229, 20), 'sleep:0.8', 'clicklog:nueva en (\\d+),(\\d+)', 'sleep:0.8', 'clicklog:equipo0 en (\\d+),(\\d+)', 'sleep:0.5', 'clicklog:campo en (\\d+),(\\d+)', 'sleep:0.3', 'type:Hola desde el administrador\n', 'wait:red chat enviado 1/1', 'sleep:0.5', 'shot:a-chat']
    SB += ['wait:red CHAT de', 'sleep:1', 'shot:b-mensaje']
    # B abre Red local y ve la carpeta que tiene compartida (sin red, es suya)
    SB += M('redlocal') + ['wait:red local abierta', 'sleep:1', at(137, 20), 'sleep:0.8', at(300, 106), 'wait:red lista Fotos/: 1', 'sleep:0.5', 'shot:b-carpeta',
           # B contesta en la misma conversacion: la respuesta va al grupo del primer mensaje
           at(229, 20), 'sleep:1', 'clicklog:conv0 en (\\d+),(\\d+)', 'sleep:0.8', 'clicklog:campo en (\\d+),(\\d+)', 'sleep:0.3', 'type:Recibido, gracias\n', 'wait:red chat enviado 1/1', 'sleep:1', 'shot:b-chat', 'key:alt-f4', 'sleep:1']
    # A saca a B de la red
    SA += [at(329, 20), 'sleep:1', at(797, 64), 'sleep:1', menu_item(200, 120, 0), 'wait:red impresora compartida', 'sleep:1', 'shot:a-impresoras', 'wait:red impresion de', 'sleep:2',
           at(229, 20), 'wait:red CHAT de', 'sleep:1', 'clicklog:conv0 en (\\d+),(\\d+)', 'sleep:1', 'shot:a-dialogo']
    SA += ['sleep:8', at(80, 20), 'sleep:1', at(829, 250), 'sleep:0.8', 'key:ret', 'sleep:3', 'shot:a-sacado']
    SB += ['wait:(1 impresoras)', 'sleep:1'] + M('agenda') + ['sleep:1', 'key:ctrl-p', 'wait:dialogo imprimir abierto', 'sleep:1.2', 'shot:b-dialogo', 'click:955,625', 'wait:red trabajo enviado', 'sleep:1', 'key:alt-f4', 'sleep:0.5']
    SB += ['wait:red sacado de la red', 'sleep:1']
res = {}
def run(name, steps, img, mac, net, port):
    res[name] = subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', 'bios', '--no-disk', '--abs', '--out', os.path.join(out, name), '--timeout', '150', '--port', str(port), '--qemu', q(img, mac, net)] + steps, stdout=open(os.path.join(out, name + '.txt'), 'w'))
rt = None
if router: rt = subprocess.Popen(['python3', '-I', 'tools/fake_router.py', '7470', '52:54:00:aa:00:01', out, '7471', '7472']); time.sleep(1)
ta = threading.Thread(target=run, args=('a', SA, A, '52:54:00:aa:00:01', 'udp=127.0.0.1:7470,localaddr=127.0.0.1:7471' if router else 'listen=127.0.0.1:7461', 4721)); ta.start(); time.sleep(1.5)
tb = threading.Thread(target=run, args=('b', SB + ['sleep:20'], B, '52:54:00:bb:00:02', 'udp=127.0.0.1:7470,localaddr=127.0.0.1:7472' if router else 'connect=127.0.0.1:7461', 4722)); tb.start()
ta.join(); tb.join()
if rt: rt.kill()
la = open(os.path.join(out, 'a', 'serial-bios.log'), errors='replace').read(); lb = open(os.path.join(out, 'b', 'serial-bios.log'), errors='replace').read()
ok = res.get('a') == 0 and res.get('b') == 0
CHK = ((la, ('red creada', 'red invitado')), (lb, ('red creada', 'red invitacion recibida', 'red unido'))) if otra else ((la, ('red: IP 192.168.50.20', 'red creada', 'red invitado', 'red chat enviado 1/1', 'cable conectado de nuevo', 'sin DHCP, direccion local')), (lb, ('sin DHCP, direccion local', 'red unido', 'red CHAT de'))) if router else ((la, ('red creada', 'red codigo', 'red invitado', 'red carpeta Fotos', 'red subido nota.txt', 'red descargado /sys/DOCS/Red/nota.txt', 'red chat enviado 1/1', 'red CHAT de', 'red impresora compartida', 'red impresion de')), (lb, ('red invitacion recibida', 'red unido', 'red CHAT de', 'red chat enviado 1/1', 'red lista Fotos/: 1', 'red trabajo enviado', 'red sacado de la red')))
for log, what in CHK:
    for w in what:
        if w not in log: print('FALLO: falta', repr(w)); ok = False
if otra and ('red servicio listo: aaaaaaaa' in lb or 'servicio listo' not in lb): print('FALLO: B se quedo con la identidad copiada de A'); ok = False
if not router and not otra:
    m = re.search(r'red trabajo enviado a .*: (\d+) bytes, (-?\d+)', lb); m2 = re.search(r'red impresion de .* en Samsung ML-1640: (\d+) bytes, (-?\d+)', la)
    if not m or m.group(2) != '0' or not m2 or m2.group(2) != '0' or m.group(1) != m2.group(1): print('FALLO: impresion por la red', m and m.group(0), m2 and m2.group(0)); ok = False
    d = imgfs.get(A, 'DOCS/impreso.qpdl')
    if d is None: print('FALLO: no se encontro el trabajo en A'); ok = False
    elif b'QPDL' not in d[:200] or (m2 and len(d) != int(m2.group(1))): print('FALLO: el trabajo no es QPDL o no esta entero', len(d)); ok = False
    else: print('trabajo QPDL recibido en A:', len(d), 'bytes')
ca = re.search(r'red codigo (\d{3} \d{3})', la)
for x in ('PANICO', 'fallo de pagina', 'excepcion', 'termino: -'):
    if x in la or x in lb: print('FALLO:', x); ok = False
print('red local:', 'OK' if ok else 'FALLO', '(codigo', ca.group(1) if ca else '?', ')'); sys.exit(0 if ok else 1)
