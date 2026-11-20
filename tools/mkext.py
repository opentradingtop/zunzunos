#!/usr/bin/env python3
"""Crea una imagen ext4 minima (con extents) para probar el lector."""
import struct, sys
BS = 4096; out = sys.argv[1]; img = bytearray(BS * 2048)
msg = b'contenido en ext4 ' * 600
INO_TAB, ROOT, SUB, DATA = 4, 40, 41, 42
sb = bytearray(1024)
struct.pack_into('<13I6H', sb, 0, 256, 2048, 0, 1000, 240, 0, 2, 2, 32768, 32768, 256, 0, 0, 0, 0, 0xEF53, 1, 1, 0)
struct.pack_into('<I', sb, 76, 1); struct.pack_into('<I', sb, 84, 11); struct.pack_into('<H', sb, 88, 256); struct.pack_into('<III', sb, 92, 0, 0x42, 0)
sb[120:128] = b'PRUEBA\0\0'; img[1024:2048] = sb
struct.pack_into('<IIIHH', img, BS, 2, 3, INO_TAB, 1000, 240)
def inode(n, mode, size, blk):
    o = BS * INO_TAB + (n - 1) * 256; struct.pack_into('<HHI', img, o, mode, 0, size); struct.pack_into('<H', img, o + 26, 2); struct.pack_into('<I', img, o + 32, 0x80000)
    cnt = (size + BS - 1) // BS; struct.pack_into('<HHHHI', img, o + 40, 0xF30A, 1, 4, 0, 0); struct.pack_into('<IHHI', img, o + 52, 0, cnt, 0, blk)
def dirb(blk, ents):
    o = BS * blk; p = 0
    for i, (ino, name, t) in enumerate(ents):
        n = name.encode(); rl = (8 + len(n) + 3) & ~3
        if i == len(ents) - 1: rl = BS - p
        struct.pack_into('<IHBB', img, o + p, ino, rl, len(n), t); img[o + p + 8:o + p + 8 + len(n)] = n; p += rl
inode(2, 0x41ED, BS, ROOT); inode(12, 0x41ED, BS, SUB); inode(13, 0x81A4, len(msg), DATA)
dirb(ROOT, [(2, '.', 2), (2, '..', 2), (12, 'carpeta', 2)]); dirb(SUB, [(12, '.', 2), (2, '..', 2), (13, 'archivo.txt', 1)])
img[BS * DATA:BS * DATA + len(msg)] = msg
if len(sys.argv) > 2 and sys.argv[2] == 'borrado':
    # archivo borrado: inodo 14 con dtime y sin enlaces, y su entrada escondida tras archivo.txt
    gone = b'texto que se borro por error ' * 400; DEL = 43
    inode(14, 0x81A4, len(gone), DEL); o = BS * INO_TAB + 13 * 256; struct.pack_into('<I', img, o + 20, 1700000000); struct.pack_into('<H', img, o + 26, 0)
    img[BS * DEL:BS * DEL + len(gone)] = gone
    d = BS * SUB; p = 0
    for _ in range(2): p += struct.unpack_from('<H', img, d + p + 4)[0]
    q = p + ((8 + len('archivo.txt') + 3) & ~3); n = b'borrado.txt'
    struct.pack_into('<IHBB', img, d + q, 14, BS - q, len(n), 1); img[d + q + 8:d + q + 8 + len(n)] = n
    open(out + '.md5', 'w').write(__import__('hashlib').md5(gone).hexdigest())
open(out, 'wb').write(img)
