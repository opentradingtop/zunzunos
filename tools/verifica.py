#!/usr/bin/env python3
"""Compara la imagen con lo leido de la memoria.

macOS monta la unidad al terminar de grabar y anade sus carpetas ocultas. Solo se
aceptan esos cambios: FSInfo, clusters que estaban libres en la FAT y entradas
vacias del directorio raiz. Cualquier otra diferencia es un error.
"""
import struct, sys
a = open(sys.argv[1], 'rb').read(); b = open(sys.argv[2], 'rb').read()
if len(a) != len(b): sys.exit('ERROR: tamanos distintos')
P = 2048 * 512
rs = struct.unpack_from('<H', a, P + 14)[0]; spc = a[P + 13]; fz = struct.unpack_from('<I', a, P + 36)[0]
fat1 = P + rs * 512; fat2 = fat1 + fz * 512; data = fat2 + fz * 512; cs = spc * 512
root = data; bad = 0; extra = 0
for o in range(0, len(a), 512):
    x, y = a[o:o + 512], b[o:o + 512]
    if x == y: continue
    if o == P + 512 or o == P + 7 * 512: continue            # FSInfo y su copia
    if fat1 <= o < data:
        for k in range(0, 512, 4):
            if x[k:k + 4] != y[k:k + 4]:
                if struct.unpack_from('<I', x, k)[0] & 0x0FFFFFFF: bad += 1; print('FAT alterada en', hex(o + k))
                else: extra += 1
        continue
    if root <= o < root + cs:
        for k in range(0, 512, 32):
            if x[k:k + 32] == y[k:k + 32] or x[k] in (0, 0xE5): continue
            # la etiqueta del volumen solo puede cambiar en sus fechas (bytes 12 a 25)
            if x[k + 11] == 8 and x[k:k + 12] == y[k:k + 12] and x[k + 26:k + 32] == y[k + 26:k + 32]: continue
            bad += 1; print('entrada raiz alterada en', hex(o + k))
        continue
    bad += 1; print('sector distinto en', hex(o))
if bad: sys.exit(f'ERROR: {bad} diferencias no esperadas')
print(f'Verificacion correcta: sistema identico ({extra} clusters anadidos por el sistema anfitrion).')
