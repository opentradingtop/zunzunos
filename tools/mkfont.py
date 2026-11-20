#!/usr/bin/env python3
"""Extrae la fuente VGA 8x16 (CP437) del VGABIOS de QEMU y genera font8x16.h"""
import sys
data = open(sys.argv[1], 'rb').read()
A = bytes.fromhex('00001038 6cc6c6fe c6c6c6c6 00000000'.replace(' ', ''))
pos = data.find(A)
alt = bytes.fromhex('0000103 86cc6c6fec6c6c6c600000000'.replace(' ', ''))
if pos < 0:
    pos = data.find(alt)
if pos < 0:
    sys.exit('no se encontro la fuente')
start = pos - 0x41 * 16
font = data[start:start + 4096]
with open(sys.argv[2], 'w') as f:
    f.write('/* Fuente VGA 8x16 CP437, extraida de SeaVGABIOS (LGPL) */\n')
    f.write('static const unsigned char font8x16[4096] = {\n')
    for i in range(0, 4096, 16):
        f.write(','.join(str(b) for b in font[i:i + 16]) + ',\n')
    f.write('};\n')
print('fuente en offset', hex(start))
