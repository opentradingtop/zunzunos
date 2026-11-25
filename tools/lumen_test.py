#!/usr/bin/env python3
"""Prueba LUMEN (chat con IA) contra tools/fake_openrouter.py (puerto 8082 del anfitrion, sin cifrar):
clave y servidor en Ajustes, lista de modelos, elegir modelo, respuesta en vivo con Markdown, error 429,
detener, respuesta de una vez, exportar a Markdown, idioma ingles y chats guardados al volver a abrir.
Con IMAGEN (la memoria de la prueba) tambien adjunta archivos (txt largo, docx y pdf) y pregunta sobre ellos.
Uso: lumen_test.py DIR_SALIDA DIR_SERVIDOR "args de qemu" [IMAGEN] """
import json, os, random, subprocess, sys
out, srv, q = sys.argv[1:4]; img = sys.argv[4] if len(sys.argv) > 4 else ''
def put_docs(img):
    """copia en DOCS/RAG de la memoria un manual largo con un dato escondido, un docx y un pdf hechos en el Mac"""
    d = os.path.join(out, 'rag'); os.makedirs(d, exist_ok=True); r = random.Random(5); W = 'barco vela casco timon mastil cubierta proa popa quilla escota driza botavara foque mayor amarre puerto marea viento rumbo carta'.split()
    paras = [' '.join(r.choice(W) for _ in range(120)).capitalize() + '.' for _ in range(60)]
    paras.insert(37, 'Dato importante: el ancla principal del Zunzun pesa exactamente 37 kilos y se guarda en el pozo de proa.')
    open(os.path.join(d, 'Manual del velero.txt'), 'w').write('\n\n'.join(paras))
    open(os.path.join(d, 'receta.txt'), 'w').write('Receta familiar\n\nLa paella de Toni lleva azafran de La Mancha y judia ferraura.\n')
    subprocess.run(['textutil', '-convert', 'docx', '-output', os.path.join(d, 'Receta de paella.docx'), os.path.join(d, 'receta.txt')], check=True); os.remove(os.path.join(d, 'receta.txt'))
    open(os.path.join(d, 'bitacora.txt'), 'w').write('Bitacora del puerto\n\nEl faro de la bahia se enciende a las nueve.\nLa contrasena del pantalan es la palabra MARGARITA.\n')
    with open(os.path.join(d, 'Bitacora.pdf'), 'wb') as f: subprocess.run(['cupsfilter', '-m', 'application/pdf', os.path.join(d, 'bitacora.txt')], stdout=f, stderr=subprocess.DEVNULL, check=True)
    os.remove(os.path.join(d, 'bitacora.txt'))
    import zlib, struct   # una foto PNG de 64x48 hecha a mano
    raw = b''.join(b'\0' + bytes(v for x in range(64) for v in (x * 4, y * 5, 128)) for y in range(48))
    ch = lambda t, b: struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)
    open(os.path.join(d, 'foto.png'), 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', 64, 48, 8, 2, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw)) + ch(b'IEND', b''))
    a = subprocess.run(['hdiutil', 'attach', '-imagekey', 'diskimage-class=CRawDiskImage', '-mountrandom', '/tmp', img], capture_output=True, text=True, timeout=60).stdout
    dev = a.split()[0]
    try:
        mnt = [l.split('\t')[-1].strip() for l in a.splitlines() if '/tmp' in l][0]; t = os.path.join(mnt, 'DOCS', 'RAG'); os.makedirs(t, exist_ok=True)
        for f in os.listdir(d): open(os.path.join(t, f), 'wb').write(open(os.path.join(d, f), 'rb').read())
        for x in os.listdir(mnt):
            if x.startswith('.'): subprocess.run(['rm', '-rf', os.path.join(mnt, x)])
    finally: subprocess.run(['hdiutil', 'detach', dev], capture_output=True)
if img: put_docs(img)
MH = 42 + 9 * 28 + 14; MY = 800 - 36 - MH - 2; f, i, n = 2, 3, 8   # Internet > LUMEN (cuarto programa)
y0 = max(42, min(42 + f * 28, MH - 6 - n * 28)); fy = MY + 42 + 28 * f + 14; ay = MY + y0 + 28 * i + 14
OPEN = ['click:50,782', 'sleep:0.6', f'move:100,{fy}', 'sleep:0.5', f'move:245,{fy}', 'sleep:0.3', f'move:350,{ay}', 'sleep:0.3', f'click:350,{ay}']
OX, OY = 88, 76
def at(x, y, k='click'): return f'{k}:{OX + x},{OY + y}'
s = ['wait:escritorio listo', 'wait:red: IP', 'sleep:1'] + OPEN + ['wait:lumen listo, 0 chats', 'sleep:1', 'shot:l-inicio',
     at(609, 270), 'sleep:0.8', 'type:sk-or-v1-prueba', at(342, 418), 'sleep:0.3', 'key:ctrl-x', 'type:http://10.0.2.2:8082/api/v1', at(114, 191), 'wait:lumen clave 2', 'sleep:0.6', 'shot:l-ajustes',
     # claves que /key acepta pero el chat no: tienen que salir como no validas
     at(78, 131), 'key:ctrl-x', 'type:sk-or-v1-solokey\n', 'wait:lumen clave 3: /key 200, chat 401, gestion 0, sk-or-v1-sol...key', 'sleep:0.6', 'shot:l-clave-solokey',
     at(78, 131), 'key:ctrl-x', 'type:sk-or-v1-gestion\n', 'wait:lumen clave 3: /key 200, chat -1, gestion 1, sk-or-v1-ges...ion', 'sleep:0.6', 'shot:l-clave-gestion',
     at(78, 131), 'key:ctrl-x', 'type:sk-or-v1-prueba\n', 'wait:lumen clave 2: /key 200, chat 200, gestion 0, sk-or-v1-pru...eba (largo 15)', 'sleep:0.6',
     at(306, 19), 'wait:lumen modelos 45 gratis 43', 'sleep:0.8', 'shot:l-modelos', at(300, 214), 'wait:lumen modelo deepseek/deepseek-r1:free', 'sleep:0.5',
     at(609, 316), 'sleep:2.2', 'shot:l-pensando', 'wait:lumen respuesta 442 letras, razonamiento 58, error: no', 'sleep:0.6', 'shot:l-respuesta',
     # seleccionar con el raton en la conversacion, copiar y pegar en el cuadro de escribir
     f'drag:{OX + 250},{OY + 56},{OX + 700},{OY + 120}', 'wait:lumen] [prueba] seleccion ', 'sleep:0.4', 'shot:l-seleccion', 'key:ctrl-c', 'wait:[prueba] copiado ', 'key:ctrl-v', 'sleep:0.4',
     'type:error429\n', 'wait:error: Demasiadas peticiones', 'sleep:0.6', 'shot:l-error',
     'type:hola\n', 'sleep:1', 'key:esc', 'wait:lumen respuesta 0 letras, razonamiento 0, error: no', 'sleep:0.6', 'shot:l-detenida',
     at(347, 19), 'sleep:0.6', at(501, 432), 'sleep:0.3', 'shot:l-ajustes-chat', at(936, 67), 'sleep:0.5', 'type:hola sin prisa\n', 'wait:razonamiento 17, error: no', 'sleep:0.6', 'shot:l-una-vez',
     'key:ctrl-s', 'sleep:1', 'shot:l-exportar', 'key:ret', 'wait:lumen exportado', 'sleep:0.5',
     ] + ([at(940, 628), 'sleep:0.8', 'shot:l-archivos'] + [x for i, f in enumerate(['Manual del velero.txt', 'Receta de paella.docx', 'Bitacora.pdf']) for x in (at(470, 227 + 4 * (i > 0) + 30 * max(0, i - 1)), 'sleep:0.6', 'key:ctrl-x', f'type:/sys/DOCS/RAG/{f}\n', f'wait:lumen adjunto {f}', 'sleep:0.5')]
     + ['shot:l-adjuntos', 'key:esc', 'sleep:0.4', 'type:Cuanto pesa el ancla del Zunzun?\n', 'wait:lumen rag ', 'sleep:3', 'shot:l-rag']
     # una imagen para el proximo mensaje
     + [at(940, 628), 'sleep:0.8', at(470, 257 + 30), 'sleep:0.6', 'key:ctrl-x', 'type:/sys/DOCS/RAG/foto.png\n', 'wait:lumen imagen foto.png 64x48', 'sleep:0.5', 'key:esc', 'sleep:0.4', 'type:Que ves en la foto?\n', 'wait:lumen enviadas 1 imagenes', 'sleep:3', 'shot:l-imagen']
     # proveedor: Lyceum y vuelta a "Otro servidor" (el de la prueba) sin perder la clave
     + [at(347, 19), 'sleep:0.8', at(431, 68), 'wait:lumen proveedor 1', 'sleep:0.8', 'shot:l-lyceum', at(555, 68), 'wait:lumen proveedor 2', 'sleep:0.5', 'key:esc', 'sleep:0.5',
        'key:ctrl-l', 'sleep:1', 'type:prueba/modelo-oculto', 'sleep:0.6', 'shot:l-usar-modelo', at(80, 205), 'wait:lumen modelo prueba/modelo-oculto', 'sleep:0.5'] if img else []) + [
     'click:1105,781', 'sleep:1.5', 'shot:l-ingles',
     'click:1075,63', 'wait:(lumen) termino: 0', 'sleep:1'] + OPEN + ['wait:lumen listo, 1 chats', 'sleep:1', 'click:200,112', 'sleep:1', 'shot:l-reabierto']
r = subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', 'bios', '--no-disk', '--abs', '--out', out, '--timeout', '60', '--port', '4699', '--qemu', q] + s, stdout=subprocess.DEVNULL)
log = open(os.path.join(out, 'serial-bios.log'), errors='replace').read(); ok = r == 0
for m in ('lumen clave 2', 'lumen modelos 45 gratis 43', 'lumen modelo deepseek/deepseek-r1:free', 'lumen respuesta 442 letras, razonamiento 58, error: no', 'error: Demasiadas peticiones',
          'lumen respuesta 0 letras, razonamiento 0, error: no', 'razonamiento 17, error: no', 'lumen exportado /sys/DOCS/Escribe un poema corto sobre el colibrí.md: 0', 'lumen listo, 1 chats'):
    if m not in log: print('FALLO: falta', repr(m)); ok = False
reqs = [l for l in open(os.path.join(srv, 'peticiones.txt'), encoding='utf-8', errors='replace').read().split('\n') if l.startswith('POST') and '"content": "ping"' not in l and '"content":"ping"' not in l]
bodies = [json.loads(l.split(' | ', 3)[3]) for l in reqs]
if len(bodies) < 4: print('FALLO: el servidor recibio', len(bodies), 'peticiones de chat'); ok = False
else:
    b = bodies[0]
    if b.get('model') != 'deepseek/deepseek-r1:free' or b.get('stream') is not True or b['messages'][-1] != {'role': 'user', 'content': 'Escribe un poema corto sobre el colibrí'}: print('FALLO: primera peticion incorrecta', b); ok = False
    if any(m['role'] not in ('user', 'assistant', 'system') for x in bodies for m in x['messages']): print('FALLO: un error se envio como mensaje'); ok = False
    if not any(isinstance(m['content'], str) and 'Primero pienso en el zunz' in m['content'] and m['content'].endswith('error429') for x in bodies for m in x['messages'] if m['role'] == 'user'): print('FALLO: lo copiado de la conversacion no llego al cuadro de escribir'); ok = False
    hs = [b for b in bodies if b['messages'][-1]['content'] == 'hola sin prisa']
    if not hs or hs[0].get('stream') is not False: print('FALLO: la peticion "hola sin prisa" no es de una vez'); ok = False
if img:
    rg = [b for b in bodies if isinstance(b['messages'][-1]['content'], str) and b['messages'][-1]['content'].endswith('Cuanto pesa el ancla del Zunzun?')]
    t = rg[0]['messages'][-1]['content'] if rg else ''
    for m, why in (('37 kilos', 'el fragmento del manual con el dato no se mando'), ('Receta de paella.docx', 'falta el docx'), ('MARGARITA', 'no se leyo el texto del PDF'), ('[Manual del velero.txt, fragmento', 'el manual no se troceo')):
        if m not in t: print('FALLO: RAG:', why); ok = False
    if len(t) > 30000: print('FALLO: RAG: se mando demasiado texto', len(t)); ok = False
    im = [b for b in bodies if isinstance(b['messages'][-1]['content'], list)]
    if not im or not any(p.get('type') == 'image_url' and p['image_url']['url'].startswith('data:image/jpeg;base64,') for p in im[0]['messages'][-1]['content']): print('FALLO: la imagen no se mando como JPEG'); ok = False
    for m in ('lumen proveedor 1', 'lumen proveedor 2', 'lumen modelo prueba/modelo-oculto'):
        if m not in log: print('FALLO: falta', m); ok = False
    for m in ('lumen adjunto Manual del velero.txt', 'lumen adjunto Receta de paella.docx', 'lumen adjunto Bitacora.pdf'):
        if m + ': 0 letras' in log or m not in log: print('FALLO: no se leyo', m); ok = False
    if any(' | Authorization: Bearer sk-or-v1-prueba | ' not in l for l in reqs): print('FALLO: falta la clave en alguna peticion'); ok = False
for x in ('PANICO', 'fallo de pagina', 'excepcion', 'termino: -'):
    if x in log: print('FALLO:', x); ok = False
print('lumen:', 'OK' if ok else 'FALLO'); sys.exit(0 if ok else 1)
