#!/usr/bin/env python3
"""Impresoras falsas para probar ZunzunOS en QEMU: IPP en 8631 (PDF y PWG), IPP en 8632 (solo PWG) y RAW en 9100 (y 9101 lento, para cancelar).
Guarda cada trabajo recibido en el directorio indicado."""
import os, socket, struct, sys, threading
OUT = sys.argv[1]; os.makedirs(OUT, exist_ok=True); n = [0]; lock = threading.Lock()
def save(data, ext):
    with lock: n[0] += 1; p = os.path.join(OUT, 'trabajo-%02d.%s' % (n[0], ext))
    open(p, 'wb').write(data); print('recibido', p, len(data), flush=True)
def attr(tag, name, val):
    v = val if isinstance(val, bytes) else val.encode(); return bytes([tag]) + struct.pack('>H', len(name)) + name.encode() + struct.pack('>H', len(v)) + v
def ipp_resp(reqid, fmts, model, status=0):
    b = struct.pack('>HHI', 0x0200, status, reqid) + b'\x01' + attr(0x47, 'attributes-charset', 'utf-8') + attr(0x48, 'attributes-natural-language', 'en') + b'\x04'
    b += attr(0x41, 'printer-make-and-model', model); b += attr(0x49, 'document-format-supported', fmts[0])
    for f in fmts[1:]: b += attr(0x49, '', f)
    b += attr(0x22, 'color-supported', b'\x01') + attr(0x23, 'printer-state', struct.pack('>I', 3)) + b'\x03'; return b
def parse_ipp(body):
    op, rid = struct.unpack('>HI', body[2:8]); p = 8; fmt = None
    while p < len(body):
        tag = body[p]; p += 1
        if tag == 3: break
        if tag < 0x10: continue
        nl = struct.unpack('>H', body[p:p + 2])[0]; name = body[p + 2:p + 2 + nl].decode(); p += 2 + nl
        vl = struct.unpack('>H', body[p:p + 2])[0]; val = body[p + 2:p + 2 + vl]; p += 2 + vl
        if name == 'document-format': fmt = val.decode()
    return op, rid, fmt, body[p:]
def recv_http(c):
    d = b''
    while b'\r\n\r\n' not in d:
        x = c.recv(65536)
        if not x: return None
        d += x
    h, rest = d.split(b'\r\n\r\n', 1); ln = 0
    for l in h.split(b'\r\n'):
        if l.lower().startswith(b'content-length:'): ln = int(l.split(b':')[1])
    while len(rest) < ln:
        x = c.recv(1 << 20)
        if not x: break
        rest += x
    return rest
def ipp_server(port, fmts, model):
    s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); s.bind(('127.0.0.1', port)); s.listen(5)
    while True:
        c, _ = s.accept(); body = recv_http(c)
        if body:
            op, rid, fmt, doc = parse_ipp(body); ext = {'application/pdf': 'pdf', 'image/pwg-raster': 'pwg', 'application/postscript': 'ps', 'application/vnd.hp-PCL': 'pcl'}.get(fmt, 'bin')
            print('ipp', port, 'op', hex(op), fmt, len(doc), flush=True)
            if op == 2: save(doc, ext)
            r = ipp_resp(rid, fmts, model); c.sendall(b'HTTP/1.1 200 OK\r\nContent-Type: application/ipp\r\nContent-Length: %d\r\nConnection: close\r\n\r\n' % len(r) + r)
        c.close()
def raw_server(port, slow=False):
    s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); 
    if slow: s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
    s.bind(('127.0.0.1', port)); s.listen(5)
    while True:
        c, _ = s.accept(); d = b''
        while True:
            x = c.recv(4096 if slow else 1 << 20)
            if not x: break
            d += x
            if slow: import time; time.sleep(0.06)  # ~64 KB/s: para probar Cancelar
        c.close(); save(d, 'pcl' if d[:20].find(b'PJL') >= 0 or d[:2] == b'\x1bE' else 'ps' if d[:4] == b'%!PS' else 'raw')
threading.Thread(target=ipp_server, args=(8631, ['application/pdf', 'image/pwg-raster', 'image/jpeg'], 'Prueba PDF IPP Everywhere'), daemon=True).start()
threading.Thread(target=ipp_server, args=(8632, ['image/pwg-raster', 'image/jpeg'], 'Prueba PWG IPP Everywhere'), daemon=True).start()
threading.Thread(target=raw_server, args=(9101, True), daemon=True).start()  # RAW lento
raw_server(9100)
