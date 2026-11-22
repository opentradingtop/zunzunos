#!/usr/bin/env python3
"""Prueba LUMEN (chat con IA) contra tools/fake_openrouter.py (puerto 8082 del anfitrion, sin cifrar):
clave y servidor en Ajustes, lista de modelos, elegir modelo, respuesta en vivo con Markdown, error 429,
detener, respuesta de una vez, exportar a Markdown, idioma ingles y chats guardados al volver a abrir.
Uso: lumen_test.py DIR_SALIDA DIR_SERVIDOR "args de qemu" """
import json, os, subprocess, sys
out, srv, q = sys.argv[1:4]
MH = 42 + 9 * 28 + 14; MY = 800 - 36 - MH - 2; f, i, n = 2, 3, 4   # Internet > LUMEN (cuarto programa)
y0 = max(42, min(42 + f * 28, MH - 6 - n * 28)); fy = MY + 42 + 28 * f + 14; ay = MY + y0 + 28 * i + 14
OPEN = ['click:50,782', 'sleep:0.6', f'move:100,{fy}', 'sleep:0.5', f'move:245,{fy}', 'sleep:0.3', f'move:350,{ay}', 'sleep:0.3', f'click:350,{ay}']
OX, OY = 88, 76
def at(x, y, k='click'): return f'{k}:{OX + x},{OY + y}'
s = ['wait:escritorio listo', 'wait:red: IP', 'sleep:1'] + OPEN + ['wait:lumen listo, 0 chats', 'sleep:1', 'shot:l-inicio',
     at(609, 270), 'sleep:0.8', 'type:sk-or-v1-prueba', at(342, 418), 'sleep:0.3', 'key:ctrl-x', 'type:http://10.0.2.2:8082/api/v1', at(114, 191), 'wait:lumen clave 2', 'sleep:0.6', 'shot:l-ajustes',
     at(306, 19), 'wait:lumen modelos 45 gratis 43', 'sleep:0.8', 'shot:l-modelos', at(300, 214), 'wait:lumen modelo deepseek/deepseek-r1:free', 'sleep:0.5',
     at(609, 316), 'sleep:2.2', 'shot:l-pensando', 'wait:lumen respuesta 442 letras, razonamiento 58, error: no', 'sleep:0.6', 'shot:l-respuesta',
     'type:error429\n', 'wait:error: Demasiadas peticiones', 'sleep:0.6', 'shot:l-error',
     'type:hola\n', 'sleep:1', 'key:esc', 'wait:lumen respuesta 0 letras, razonamiento 0, error: no', 'sleep:0.6', 'shot:l-detenida',
     at(347, 19), 'sleep:0.6', at(501, 432), 'sleep:0.3', 'shot:l-ajustes-chat', at(936, 67), 'sleep:0.5', 'type:hola sin prisa\n', 'wait:razonamiento 17, error: no', 'sleep:0.6', 'shot:l-una-vez',
     'key:ctrl-s', 'sleep:1', 'shot:l-exportar', 'key:ret', 'wait:lumen exportado', 'sleep:0.5',
     'click:1105,781', 'sleep:1.5', 'shot:l-ingles',
     'click:1075,63', 'wait:(lumen) termino: 0', 'sleep:1'] + OPEN + ['wait:lumen listo, 1 chats', 'sleep:1', 'click:200,112', 'sleep:1', 'shot:l-reabierto']
r = subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', 'bios', '--no-disk', '--abs', '--out', out, '--timeout', '60', '--port', '4699', '--qemu', q] + s, stdout=subprocess.DEVNULL)
log = open(os.path.join(out, 'serial-bios.log'), errors='replace').read(); ok = r == 0
for m in ('lumen clave 2', 'lumen modelos 45 gratis 43', 'lumen modelo deepseek/deepseek-r1:free', 'lumen respuesta 442 letras, razonamiento 58, error: no', 'error: Demasiadas peticiones',
          'lumen respuesta 0 letras, razonamiento 0, error: no', 'razonamiento 17, error: no', 'lumen exportado /sys/DOCS/Escribe un poema corto sobre el colibrí.md: 0', 'lumen listo, 1 chats'):
    if m not in log: print('FALLO: falta', repr(m)); ok = False
reqs = [l for l in open(os.path.join(srv, 'peticiones.txt'), encoding='utf-8', errors='replace').read().split('\n') if l.startswith('POST')]
bodies = [json.loads(l.split(' | ', 3)[3]) for l in reqs]
if len(bodies) < 4: print('FALLO: el servidor recibio', len(bodies), 'peticiones de chat'); ok = False
else:
    b = bodies[0]
    if b.get('model') != 'deepseek/deepseek-r1:free' or b.get('stream') is not True or b['messages'][-1] != {'role': 'user', 'content': 'Escribe un poema corto sobre el colibrí'}: print('FALLO: primera peticion incorrecta', b); ok = False
    if any(m['role'] not in ('user', 'assistant', 'system') for x in bodies for m in x['messages']): print('FALLO: un error se envio como mensaje'); ok = False
    if bodies[-1].get('stream') is not False or bodies[-1]['messages'][-1]['content'] != 'hola sin prisa': print('FALLO: la ultima peticion no es de una vez', bodies[-1]); ok = False
    if any('Bearer sk-or-v1-prueba' not in l for l in reqs): print('FALLO: falta la clave en alguna peticion'); ok = False
for x in ('PANICO', 'fallo de pagina', 'excepcion', 'termino: -'):
    if x in log: print('FALLO:', x); ok = False
print('lumen:', 'OK' if ok else 'FALLO'); sys.exit(0 if ok else 1)
