#!/usr/bin/env python3
"""Routers falsos de tres casas para la prueba de la Red local por Internet (tools/red_internet_test.py).

Cada equipo de la prueba va en QEMU con red de usuario (slirp), en una subred propia: es su "casa", detras de un NAT.
El anfitrion hace de Internet: cada casa tiene una IP "publica" (198.51.100.x, puesta en lo del anfitrion) y lo que el
router abre se abre de verdad en QEMU con hostfwd_add por el monitor del equipo. Desde las casas, el router es la
puerta de enlace (x.x.x.2), que slirp lleva al 127.0.0.1 del anfitrion, donde escucha este programa:
  - PCP y NAT-PMP en UDP 5351 (PCP trae la IP del equipo: asi se sabe de que casa es; NAT-PMP no, va a la casa natpmp)
  - UPnP IGD: SSDP en UDP 1900 y la descripcion y el control SOAP en HTTP 5000 (casa upnp)
  - la casa cgnat contesta PCP con una IP externa 100.64.x.x: el puerto se "abre" en su router pero no sirve
Ordenes (archivo DIR/casas-orden.txt, una linea): "ip NOMBRE NUEVA_IP" cambia la IP publica de una casa (se quita el
reenvio de la IP vieja y se pone en la nueva, como al reconectar el router).
Uso: fake_casas.py DIR nombre:subred:ip_publica:modo:puerto_monitor ..."""
import http.server, os, re, socket, struct, sys, threading, time
out = sys.argv[1]; LOG = open(os.path.join(out, 'casas.txt'), 'a')
def log(m): LOG.write(time.strftime('%H:%M:%S ') + m + '\n'); LOG.flush()
class Casa:
    def __init__(s, spec):
        s.name, s.net, s.pub, s.mode, mon = spec.split(':'); s.mon = int(mon); s.gw = s.net + '.2'; s.maps = {}   # puerto externo -> (ip interna, puerto)
    def hmp(s, cmd):
        for _ in range(20):
            try:
                c = socket.create_connection(('127.0.0.1', s.mon), 3); c.settimeout(3)
                buf = b''
                while b'(qemu)' not in buf: buf += c.recv(4096)
                c.sendall(cmd.encode() + b'\n'); time.sleep(0.4); r = c.recv(4096).decode(errors='replace'); c.close(); log(f'{s.name} hmp {cmd} -> {r.strip()[-80:]}'); return r
            except OSError: time.sleep(0.5)
        log(f'{s.name} hmp sin conexion: {cmd}')
    def open_(s, eport, iip, iport):
        if s.mode == 'cgnat': return
        if eport in s.maps: s.hmp(f'hostfwd_remove n1 tcp:{s.pub}:{eport}')
        s.hmp(f'hostfwd_add n1 tcp:{s.pub}:{eport}-{iip}:{iport}'); s.maps[eport] = (iip, iport)
    def close_(s, eport):
        if eport in s.maps: s.hmp(f'hostfwd_remove n1 tcp:{s.pub}:{eport}'); del s.maps[eport]
    def ext(s): return '100.64.7.7' if s.mode == 'cgnat' else s.pub
C = [Casa(x) for x in sys.argv[2:]]
def by_ip(ip): return next((c for c in C if ip.startswith(c.net + '.')), None)
def by_mode(m): return next((c for c in C if c.mode == m), None)
EPOCH = time.time()
def pcp_natpmp():
    u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); u.bind(('127.0.0.1', 5351))
    while True:
        d, a = u.recvfrom(2000)
        if len(d) >= 60 and d[0] == 2 and d[1] == 1:   # PCP MAP
            cip = socket.inet_ntoa(d[20:24]); c = by_ip(cip); life = struct.unpack('!I', d[4:8])[0]; iport, sug = struct.unpack('!HH', d[40:44])
            log(f'PCP MAP de {cip} ({c and c.name}) puerto {iport} vida {life}')
            if not c or c.mode == 'upnp': continue                      # ese router no sabe PCP: no contesta
            if c.mode == 'natpmp': u.sendto(bytes([0, 128 + 1, 0, 1]) + struct.pack('!I', int(time.time() - EPOCH)), a); continue   # UNSUPP_VERSION
            ep = sug or iport
            if life: c.open_(ep, cip, iport)
            else: c.close_(ep)
            r = bytearray(60); r[0] = 2; r[1] = 0x81; r[3] = 0; r[4:8] = struct.pack('!I', life); r[8:12] = struct.pack('!I', int(time.time() - EPOCH))
            r[24:36] = d[24:36]; r[36] = 6; r[40:42] = struct.pack('!H', iport); r[42:44] = struct.pack('!H', ep); r[54:56] = b'\xff\xff'; r[56:60] = socket.inet_aton(c.ext()); u.sendto(bytes(r), a)
        elif len(d) >= 2 and d[0] == 0:                                 # NAT-PMP: sin IP del equipo, es la casa natpmp
            c = by_mode('natpmp')
            if not c: continue
            ep_ = int(time.time() - EPOCH)
            if d[1] == 0: u.sendto(struct.pack('!BBHI', 0, 128, 0, ep_) + socket.inet_aton(c.ext()), a); log(f'NAT-PMP direccion {c.ext()}')
            elif d[1] == 2 and len(d) >= 12:
                iport, sug, life = struct.unpack('!HHI', d[4:12]); ep = sug or iport; iip = c.net + '.15'
                if life: c.open_(ep, iip, iport)
                else: c.close_(ep)
                u.sendto(struct.pack('!BBHIHHI', 0, 130, 0, ep_, iport, ep, life), a); log(f'NAT-PMP {c.name} {ep}->{iip}:{iport} vida {life}')
DESC = '''<?xml version="1.0"?><root xmlns="urn:schemas-upnp-org:device-1-0"><device><deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType>
<friendlyName>Router falso</friendlyName><deviceList><device><deviceType>urn:schemas-upnp-org:device:WANDevice:1</deviceType><deviceList><device>
<deviceType>urn:schemas-upnp-org:device:WANConnectionDevice:1</deviceType><serviceList>
<service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType><serviceId>urn:upnp-org:serviceId:WANIPConn1</serviceId><controlURL>/ctl/IPConn</controlURL><eventSubURL>/evt</eventSubURL><SCPDURL>/scpd.xml</SCPDURL></service>
</serviceList></device></deviceList></device></deviceList></device></root>'''
def ssdp():
    u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); u.bind(('127.0.0.1', 1900))
    while True:
        d, a = u.recvfrom(2000); c = by_mode('upnp')
        if b'M-SEARCH' not in d or not c: continue
        u.sendto(f'HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=120\r\nST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\nUSN: uuid:falso::igd\r\nLOCATION: http://{c.gw}:5000/igd.xml\r\nSERVER: Falso/1.0 UPnP/1.0\r\n\r\n'.encode(), a); log('SSDP contestado')
class H(http.server.BaseHTTPRequestHandler):
    def log_message(s, *a): pass
    def send(s, code, body, ct='text/xml'):
        b = body.encode(); s.send_response(code); s.send_header('Content-Type', ct); s.send_header('Content-Length', str(len(b))); s.end_headers(); s.wfile.write(b)
    def do_GET(s): s.send(200, DESC) if s.path == '/igd.xml' else s.send(404, '')
    def do_POST(s):
        n = int(s.headers.get('Content-Length', 0)); b = s.rfile.read(n).decode(errors='replace'); act = (s.headers.get('SOAPAction') or '').strip('"').split('#')[-1]; c = by_mode('upnp')
        g = lambda t: (re.search(f'<{t}>(.*?)</{t}>', b, re.S) or [None, ''])[1]
        env = lambda x: f'<?xml version="1.0"?><s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"><s:Body>{x}</s:Body></s:Envelope>'
        log(f'SOAP {act} {g("NewExternalPort")} {g("NewInternalClient")}')
        if act == 'AddPortMapping':
            cc = by_ip(g('NewInternalClient')) or c; cc.open_(int(g('NewExternalPort')), g('NewInternalClient'), int(g('NewInternalPort')))
            s.send(200, env('<u:AddPortMappingResponse xmlns:u="urn:schemas-upnp-org:service:WANIPConnection:1"/>'))
        elif act == 'GetExternalIPAddress': s.send(200, env(f'<u:GetExternalIPAddressResponse xmlns:u="urn:schemas-upnp-org:service:WANIPConnection:1"><NewExternalIPAddress>{c.ext()}</NewExternalIPAddress></u:GetExternalIPAddressResponse>'))
        elif act == 'DeletePortMapping': c.close_(int(g('NewExternalPort'))); s.send(200, env('<u:DeletePortMappingResponse xmlns:u="urn:schemas-upnp-org:service:WANIPConnection:1"/>'))
        else: s.send(500, env('<s:Fault><detail><UPnPError><errorCode>401</errorCode></UPnPError></detail></s:Fault>'))
def orders():
    f = os.path.join(out, 'casas-orden.txt')
    while True:
        time.sleep(0.5)
        if not os.path.exists(f): continue
        l = open(f).read().split(); os.unlink(f)
        if len(l) == 3 and l[0] == 'ip':
            c = next(x for x in C if x.name == l[1]); old = c.pub; m = dict(c.maps)
            for ep in m: c.close_(ep)
            c.pub = l[2]
            for ep, (iip, ip) in m.items(): c.open_(ep, iip, ip)
            log(f'{c.name}: IP publica {old} -> {c.pub}')
for t in (pcp_natpmp, ssdp, orders): threading.Thread(target=t, daemon=True).start()
log('routers listos: ' + ', '.join(f'{c.name} {c.net}.0/24 {c.pub} {c.mode}' for c in C))
http.server.ThreadingHTTPServer(('127.0.0.1', 5000), H).serve_forever()
