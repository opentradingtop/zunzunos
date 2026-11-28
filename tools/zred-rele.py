#!/usr/bin/env python3
"""Rele para la Red local de ZunzunOS por Internet (docs/RED.md).

Hace falta solo cuando dos equipos no se pueden llamar directamente (router sin UPnP/NAT-PMP/PCP, CGNAT del movil
o de la compania, doble NAT). Se pone en cualquier maquina con una direccion accesible desde Internet: un VPS, una
Raspberry Pi en una casa con el puerto abierto, etc. Solo necesita Python 3.

  python3 zred-rele.py [--puerto 7142] [--direccion 0.0.0.0] [--clave SECRETO]

En ZunzunOS: Red local -> Internet -> Rele de la red...  host:7142   (o host:7142/SECRETO si se usa --clave)

Que hace: cada equipo espera en un "buzon" (un nombre que solo saben calcular los equipos de su red, sacado de la
clave de la red: el rele no sabe ni que red es ni que equipo). Cuando otro equipo llama a ese buzon, el rele une las
dos conexiones y pasa los bytes tal cual. Todo va cifrado de extremo a extremo con la clave de la red (AES-GCM, con
autenticacion reto/respuesta dentro): el rele no puede leer nada ni hacerse pasar por nadie. Lo que si ve: las IP
de los equipos que se conectan, cuando y cuantos bytes.

Protocolo (una linea de texto y despues bytes):
  ZRELE1|ESPERA|buzon|clave    el equipo espera llamadas; el rele manda ZRELE1|SIGUE cada 25 s y ZRELE1|LLAMADA
                               cuando alguien llama (desde ahi la conexion es la del que llama)
  ZRELE1|CONECTA|buzon|clave   llamar: ZRELE1|UNIDO y desde ahi bytes con el que esperaba, o ZRELE1|NADIE
  ZRELE1|QUIENSOY|-|clave      ZRELE1|ERES|ip (la IP publica con la que se le ve)
La clave es "-" si el rele no tiene --clave. Limites: 4 esperas por buzon, 300 conexiones, 30 por IP, 10 s para
la primera linea, 10 minutos sin trafico en una conexion unida.
"""
import argparse, asyncio, hmac, re, sys, time

ap = argparse.ArgumentParser(description='Rele de la Red local de ZunzunOS')
ap.add_argument('--puerto', type=int, default=7142)
ap.add_argument('--direccion', default='0.0.0.0')
ap.add_argument('--clave', default='', help='si se pone, los equipos tienen que traerla (host:puerto/clave)')
ap.add_argument('--registro', default='', help='archivo donde anotar lo que pasa (por omision, la salida)')
a = ap.parse_args()
BOX = re.compile(r'^[0-9a-f]{16,64}$')
waiting = {}          # buzon -> lista de (reader, writer, evento, caja con la conexion que llama)
per_ip = {}
total = 0
logf = open(a.registro, 'a') if a.registro else sys.stdout
def log(m): logf.write(time.strftime('%Y-%m-%d %H:%M:%S ') + m + '\n'); logf.flush()

async def pipe(r, w):
    try:
        while True:
            d = await asyncio.wait_for(r.read(65536), 600)
            if not d: break
            w.write(d); await w.drain()
    except Exception: pass
    finally:
        try: w.close()
        except Exception: pass

async def handle(r, w):
    global total
    ip = (w.get_extra_info('peername') or ('?', 0))[0]
    if total >= 300 or per_ip.get(ip, 0) >= 30: w.close(); return
    total += 1; per_ip[ip] = per_ip.get(ip, 0) + 1
    try:
        w.get_extra_info('socket').setsockopt(6, 1, 1)   # TCP_NODELAY
    except Exception: pass
    try:
        try: l = await asyncio.wait_for(r.readline(), 10)
        except Exception: return
        f = l.decode('ascii', 'replace').strip().split('|')
        if len(f) != 4 or f[0] != 'ZRELE1' or not hmac.compare_digest((f[3] if a.clave else '-').encode(), (a.clave or '-').encode()):
            w.write(b'ZRELE1|NO\n'); await w.drain(); return
        verb, box = f[1], f[2]
        if verb == 'QUIENSOY':
            w.write(f'ZRELE1|ERES|{ip}\n'.encode()); await w.drain(); return
        if not BOX.match(box): w.write(b'ZRELE1|NO\n'); await w.drain(); return
        if verb == 'ESPERA':
            ev = asyncio.Event(); slot = [r, w, ev, None]; L = waiting.setdefault(box, []); L.append(slot)
            while len(L) > 4:
                old = L.pop(0)
                try: old[1].close()
                except Exception: pass
            log(f'espera {box[:8]}... desde {ip}')
            try:
                while not ev.is_set():
                    try: await asyncio.wait_for(ev.wait(), 25)
                    except asyncio.TimeoutError:
                        if w.is_closing(): break
                        w.write(b'ZRELE1|SIGUE\n'); await w.drain()
            except Exception: pass
            finally:
                if slot in waiting.get(box, []): waiting[box].remove(slot)
                if box in waiting and not waiting[box]: del waiting[box]
            if ev.is_set() and slot[3]:
                cr, cw = slot[3]; log(f'unido {box[:8]}... ({ip})')
                await asyncio.gather(pipe(r, cw), pipe(cr, w))
            return
        if verb == 'CONECTA':
            L = waiting.get(box, [])
            while L and L[0][1].is_closing(): L.pop(0)
            if not L: w.write(b'ZRELE1|NADIE\n'); await w.drain(); log(f'nadie en {box[:8]}... (llama {ip})'); return
            slot = L.pop(0)
            if not L: waiting.pop(box, None)
            slot[1].write(b'ZRELE1|LLAMADA\n'); w.write(b'ZRELE1|UNIDO\n')
            try: await slot[1].drain(); await w.drain()
            except Exception: return
            slot[3] = (r, w); slot[2].set()
            # la conexion del que llama la lleva ahora el que esperaba (pipe); esta tarea espera a que se cierre
            while not w.is_closing(): await asyncio.sleep(1)
            return
        w.write(b'ZRELE1|NO\n'); await w.drain()
    finally:
        total -= 1; per_ip[ip] -= 1
        if per_ip[ip] <= 0: per_ip.pop(ip, None)

async def main():
    s = await asyncio.start_server(handle, a.direccion, a.puerto)
    log(f'rele de ZunzunOS escuchando en {a.direccion}:{a.puerto}' + (' (con clave)' if a.clave else ''))
    async with s: await s.serve_forever()

try: asyncio.run(main())
except KeyboardInterrupt: pass
