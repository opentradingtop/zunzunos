#!/usr/bin/env python3
"""Genera un volumen NTFS minimo para probar el lector y la recuperacion de borrados."""
import struct, sys
CS = 4096; RS = 1024; out = sys.argv[1]; TOTAL = 16384
img = bytearray(TOTAL * CS); MFT = 16
def u(s): return s.encode('utf-16le')
def fixup(b, sz):
    b = bytearray(b); uo, un = struct.unpack_from('<HH', b, 4); struct.pack_into('<H', b, uo, 0x4242)
    for i in range(1, un): o = i * 512 - 2; b[uo + i * 2:uo + i * 2 + 2] = b[o:o + 2]; b[o:o + 2] = b'\x42\x42'
    return bytes(b)
def res(t, v, name=b''):
    no = 24; vo = (no + len(name) + 7) & ~7; l = (vo + len(v) + 7) & ~7; a = bytearray(l); struct.pack_into('<IIBBHHH', a, 0, t, l, 0, len(name) // 2, no, 0, 0); struct.pack_into('<IHH', a, 16, len(v), vo, 0); a[no:no + len(name)] = name; a[vo:vo + len(v)] = v; return bytes(a)
def runs(rl):
    o = bytearray(); last = 0
    for lcn, n in rl:
        d = lcn - last; last = lcn; ln = n.to_bytes(4, 'little'); of = d.to_bytes(4, 'little', signed=True); o += bytes([0x44]) + ln + of
    return bytes(o + b'\0')
def nonres(t, size, rl, name=b''):
    r = runs(rl); no = 64; ro = (no + len(name) + 7) & ~7; l = (ro + len(r) + 7) & ~7; a = bytearray(l); vc = sum(n for _, n in rl); struct.pack_into('<IIBBHHH', a, 0, t, l, 1, len(name) // 2, no, 0, 0); struct.pack_into('<QQHH', a, 16, 0, vc - 1, ro, 0); struct.pack_into('<QQQ', a, 40, vc * CS, size, size); a[no:no + len(name)] = name; a[ro:ro + len(r)] = r; return bytes(a)
def fname(par, n, size, d=False):
    v = bytearray(66); struct.pack_into('<Q', v, 0, par | 1 << 48); struct.pack_into('<QQ', v, 40, size, size); struct.pack_into('<I', v, 56, 0x10000000 if d else 0x20); v[64] = len(n); v[65] = 1; return bytes(v) + u(n)
def rec(n, attrs, flags=1):
    b = bytearray(RS); b[:4] = b'FILE'; struct.pack_into('<HHQHHHH', b, 4, 48, 3, 0, 1, 1, 56, flags); p = 56
    for a in attrs: b[p:p + len(a)] = a; p += len(a)
    b[p:p + 8] = struct.pack('<II', 0xFFFFFFFF, 0); struct.pack_into('<II', b, 24, p + 8, RS); img[MFT * CS + n * RS:MFT * CS + (n + 1) * RS] = fixup(b, RS)
def entry(ref, fn, last=False, sub=None):
    if last: e = bytearray(16 + (8 if sub is not None else 0)); struct.pack_into('<QHHH', e, 0, 0, len(e), 0, 2 | (1 if sub is not None else 0)); 
    else: l = (16 + len(fn) + 7) & ~7; e = bytearray(l); struct.pack_into('<QHHH', e, 0, ref | 1 << 48, l, len(fn), 0); e[16:16 + len(fn)] = fn
    if last and sub is not None: struct.pack_into('<Q', e, len(e) - 8, sub)
    return bytes(e)
def iroot(ents, large):
    body = b''.join(ents); h = struct.pack('<IIII', 16, 16 + len(body), 16 + len(body), 1 if large else 0); return res(0x90, struct.pack('<IIII', 0x30, 1, CS, 1) + h + body, u('$I30'))
next_c = [200]
def put(data, split=False):
    n = (len(data) + CS - 1) // CS; rl = []
    if split and n > 1: a = next_c[0]; b = a + n // 2 + 7; h = n // 2; img[a * CS:(a + h) * CS] = data[:h * CS]; img[b * CS:b * CS + len(data) - h * CS] = data[h * CS:]; next_c[0] = b + n - h + 1; return [(a, h), (b, n - h)]
    a = next_c[0]; img[a * CS:a * CS + len(data)] = data; next_c[0] = a + n + 1; return [(a, n)]
def pat(seed, n): return bytes((i * seed + (i >> 7)) & 255 for i in range(n))
FILES = {'Informe anual.txt': pat(3, 20000), 'Foto boda.jpg': pat(5, 90000), 'nota.txt': b'nota corta residente\n'}
rec(0, [res(0x30, fname(5, '$MFT', 64 * RS)), nonres(0x80, 64 * RS, [(MFT, 16)])]); ents = []; n = 24
for name, data in sorted(FILES.items()):
    a = res(0x80, data) if len(data) < 300 else nonres(0x80, len(data), put(data, name.startswith('Foto'))); fn = fname(5, name, len(data)); rec(n, [res(0x30, fn), a]); ents.append(entry(n, fn)); n += 1
sub = n; n += 1; dfn = fname(5, 'Documentos', 0, True); ents.append(entry(sub, dfn)); ents.sort(key=lambda e: e[82:].decode('utf-16le', 'ignore').lower())
sd = []; 
for i in range(30):
    d = pat(i + 7, 5000 + i); nm = f'capitulo {i:02d}.doc'; fn = fname(sub, nm, len(d)); rec(n, [res(0x30, fn), nonres(0x80, len(d), put(d))]); sd.append(entry(n, fn)); n += 1; FILES['Documentos/' + nm] = d
blk = bytearray(CS); blk[:4] = b'INDX'; struct.pack_into('<HHQQ', blk, 4, 40, 9, 0, 0); body = b''.join(sd) + entry(0, b'', True); struct.pack_into('<IIII', blk, 24, 40, 40 + len(body), CS - 24, 0); blk[64:64 + len(body)] = body; rl = put(fixup(blk, CS))
rec(sub, [res(0x30, dfn), iroot([entry(0, b'', True, 0)], True), nonres(0xA0, CS, rl, u('$I30'))], 3)
rec(5, [res(0x30, fname(5, '.', 0, True)), iroot(ents + [entry(0, b'', True)], False)], 3)
DEL = pat(11, 33000); fn = fname(sub, 'borrado importante.xls', len(DEL)); rec(n, [res(0x30, fn), nonres(0x80, len(DEL), put(DEL))], 0); n += 1
b = bytearray(512); b[0:3] = b'\xeb\x52\x90'; b[3:11] = b'NTFS    '; struct.pack_into('<HB', b, 11, 512, 8); b[21] = 0xF8; struct.pack_into('<Q', b, 40, TOTAL * 8 - 1); struct.pack_into('<QQ', b, 48, MFT, MFT + 32); b[64] = 0xF6; b[68] = 1; b[510:512] = b'\x55\xaa'; img[:512] = b
open(out, 'wb').write(img)
import hashlib, json; json.dump({k: hashlib.sha1(v).hexdigest() for k, v in dict(FILES, **{'BORRADO': DEL}).items()}, open(out + '.json', 'w'))
