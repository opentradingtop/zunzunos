#!/usr/bin/env python3
"""Decodifica un PWG Raster (primera pagina) a PNG para revisar la salida de impresion."""
import struct, sys, zlib
d = open(sys.argv[1], 'rb').read(); assert d[:4] == b'RaS2', d[:4]; h = d[4:4 + 1796]
g = lambda o: struct.unpack('>I', h[o:o + 4])[0]
W, H, bpp, cs, dpi = g(372), g(376), g(388) // 8, g(400), g(276); print('pagina', W, 'x', H, 'bpp', bpp, 'cs', cs, 'dpi', dpi, 'tamano', g(352), g(356), h[1732:1760].split(b'\0')[0])
p = 4 + 1796; rows = []
while len(rows) < H:
    rep = d[p] + 1; p += 1; line = b''
    while len(line) < W * bpp:
        c = d[p]; p += 1
        if c < 128: line += d[p:p + bpp] * (c + 1); p += bpp
        else: n = 257 - c; line += d[p:p + n * bpp]; p += n * bpp
    rows += [line] * rep
def png(path, w, hh, rows, bpp):
    raw = b''.join(b'\0' + r for r in rows); ct = 2 if bpp == 3 else 0
    ch = lambda t, x: struct.pack('>I', len(x)) + t + x + struct.pack('>I', zlib.crc32(t + x) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, hh, 8, ct, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw)) + ch(b'IEND', b''))
png(sys.argv[2], W, H, rows[:H], bpp); print('ok', sys.argv[2], 'resto', len(d) - p)
