#!/usr/bin/env python3
"""Extrae anchuras (1/1000 em) de Arial y Times New Roman (metricamente iguales a Helvetica y Times)
para los codigos WinAnsi 32..255 y genera user/lib/zp_metrics.h. Solo se guardan numeros."""
import struct, sys
D = '/System/Library/Fonts/Supplemental/'
FONTS = [('Arial.ttf', 'Helvetica'), ('Arial Bold.ttf', 'Helvetica-Bold'), ('Times New Roman.ttf', 'Times-Roman'), ('Times New Roman Bold.ttf', 'Times-Bold')]
WIN = {0x80: 0x20AC, 0x82: 0x201A, 0x83: 0x0192, 0x84: 0x201E, 0x85: 0x2026, 0x86: 0x2020, 0x87: 0x2021, 0x88: 0x02C6, 0x89: 0x2030, 0x8A: 0x0160, 0x8B: 0x2039, 0x8C: 0x0152, 0x8E: 0x017D,
       0x91: 0x2018, 0x92: 0x2019, 0x93: 0x201C, 0x94: 0x201D, 0x95: 0x2022, 0x96: 0x2013, 0x97: 0x2014, 0x98: 0x02DC, 0x99: 0x2122, 0x9A: 0x0161, 0x9B: 0x203A, 0x9C: 0x0153, 0x9E: 0x017E, 0x9F: 0x0178}
def tables(b):
    n = struct.unpack('>H', b[4:6])[0]; t = {}
    for i in range(n):
        tag, _, off, ln = struct.unpack('>4sIII', b[12 + i * 16:28 + i * 16]); t[tag.decode()] = (off, ln)
    return t
def cmap(b, t):
    off = t['cmap'][0]; n = struct.unpack('>H', b[off + 2:off + 4])[0]
    for i in range(n):
        pid, eid, so = struct.unpack('>HHI', b[off + 4 + i * 8:off + 12 + i * 8])
        if pid == 3 and eid == 1:
            s = off + so; fmt = struct.unpack('>H', b[s:s + 2])[0]; assert fmt == 4
            segx2 = struct.unpack('>H', b[s + 6:s + 8])[0]; seg = segx2 // 2
            ends = struct.unpack('>%dH' % seg, b[s + 14:s + 14 + segx2]); st = s + 16 + segx2
            starts = struct.unpack('>%dH' % seg, b[st:st + segx2]); dl = struct.unpack('>%dh' % seg, b[st + segx2:st + 2 * segx2]); ro_off = st + 2 * segx2
            ros = struct.unpack('>%dH' % seg, b[ro_off:ro_off + segx2])
            def g(c):
                for k in range(seg):
                    if starts[k] <= c <= ends[k]:
                        if ros[k] == 0: return (c + dl[k]) & 0xFFFF
                        a = ro_off + k * 2 + ros[k] + (c - starts[k]) * 2; gi = struct.unpack('>H', b[a:a + 2])[0]
                        return (gi + dl[k]) & 0xFFFF if gi else 0
                return 0
            return g
def widths(path):
    b = open(D + path, 'rb').read(); t = tables(b)
    upem = struct.unpack('>H', b[t['head'][0] + 18:t['head'][0] + 20])[0]
    nh = struct.unpack('>H', b[t['hhea'][0] + 34:t['hhea'][0] + 36])[0]; hm = t['hmtx'][0]; g = cmap(b, t)
    def adv(gi): gi = min(gi, nh - 1); return struct.unpack('>H', b[hm + gi * 4:hm + gi * 4 + 2])[0]
    out = []
    for c in range(32, 256):
        u = WIN.get(c, c if c < 0x80 or c >= 0xA0 else 0x20)
        out.append(round(adv(g(u)) * 1000 / upem))
    return out
dst = sys.argv[1] if len(sys.argv) > 1 else 'user/lib/zp_metrics.h'
with open(dst, 'w') as o:
    o.write('/* Anchuras WinAnsi 32..255 en 1/1000 em de Helvetica, Helvetica-Bold, Times-Roman y Times-Bold (Courier = 600). Generado por tools/mkmetrics.py */\nstatic const unsigned short ZPW[4][224] = {\n')
    for f, name in FONTS:
        w = widths(f); o.write('    { /* %s */ %s },\n' % (name, ','.join(map(str, w))))
    o.write('};\n')
print(dst)
