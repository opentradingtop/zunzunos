#!/usr/bin/env python3
"""Router falso para la prueba de la Red local: hace de switch entre equipos QEMU (-netdev socket,udp=127.0.0.1:PUERTO,
localaddr=127.0.0.1:PUERTO_DEL_EQUIPO), reenvia cada trama a los demas y contesta DHCP solo a una MAC (le da 192.168.50.20 con puerta 192.168.50.1), como el router de casa del X220.
Los demas equipos no reciben direccion y se ponen una 169.254. Tras dar la primera direccion deja de contestar (como si luego se
llevara el cable del router al otro equipo). Uso: fake_router.py PUERTO MAC DIR PUERTO_EQUIPO..."""
import os, socket, struct, sys
port, mac, out = int(sys.argv[1]), bytes.fromhex(sys.argv[2].replace(':', '')), sys.argv[3]; peers = [('127.0.0.1', int(p)) for p in sys.argv[4:]]
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(('127.0.0.1', port))
ME = bytes.fromhex('525400ff0001'); IP = socket.inet_aton('192.168.50.1'); YOU = socket.inet_aton('192.168.50.20')
def csum(b):
    if len(b) % 2: b += b'\0'
    t = sum(struct.unpack('!%dH' % (len(b) // 2), b)); t = (t >> 16) + (t & 0xFFFF); t += t >> 16; return ~t & 0xFFFF
log = open(os.path.join(out, 'router.txt'), 'a'); done = False
while True:
    f, src = s.recvfrom(4000)
    for p in peers:
        if p != src: s.sendto(f, p)
    if done or len(f) < 42 or f[6:12] != mac or f[12:14] != b'\x08\x00' or f[23] != 17: continue
    ip = f[14:]; hl = (ip[0] & 15) * 4; u = ip[hl:]
    if struct.unpack('!H', u[2:4])[0] != 67: continue
    d = u[8:]; xid = d[4:8]; opts = d[240:]; t = 0; i = 0
    while i < len(opts) and opts[i] != 255:
        if opts[i] == 0: i += 1; continue
        if opts[i] == 53: t = opts[i + 2]
        i += 2 + opts[i + 1]
    if t not in (1, 3): continue
    r = bytearray(240); r[0] = 2; r[1] = 1; r[2] = 6; r[4:8] = xid; r[16:20] = YOU; r[20:24] = IP; r[28:34] = mac; r[236:240] = b'\x63\x82\x53\x63'
    r += bytes([53, 1, 2 if t == 1 else 5, 54, 4]) + IP + bytes([51, 4, 0, 1, 0x51, 0x80, 1, 4, 255, 255, 255, 0, 3, 4]) + IP + bytes([6, 4]) + IP + b'\xff'
    ul = 8 + len(r); udp = struct.pack('!HHHH', 67, 68, ul, 0) + r
    iph = bytearray(struct.pack('!BBHHHBBH4s4s', 0x45, 0, 20 + ul, 0, 0, 64, 17, 0, IP, b'\xff\xff\xff\xff')); iph[10:12] = struct.pack('!H', csum(bytes(iph)))
    fr = b'\xff' * 6 + ME + b'\x08\x00' + bytes(iph) + udp
    for p in peers: s.sendto(fr, p)
    log.write(f'dhcp {"oferta" if t == 1 else "ack"}\n'); log.flush(); done = t == 3
