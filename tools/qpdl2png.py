#!/usr/bin/env python3
"""Decodifica un trabajo Samsung SPL2/QPDL y guarda cada pagina como PNG (para las pruebas).
Escrito desde la descripcion publica del formato (SpliX, doc/specs-en). Comprueba cabeceras,
sumas de control y que cada banda se descomprima al tamano exacto.
Uso: qpdl2png.py TRABAJO PREFIJO   ->  PREFIJO-1.png, PREFIJO-2.png ..."""
import struct, sys, zlib

def png(path, w, h, rows):
    raw = b''.join(b'\0' + r for r in rows)
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 1, 0, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))

def band(data, size):
    magic = struct.unpack('<I', data[:4])[0]; le = magic == 0x09ABCDEF
    if not le and struct.unpack('>I', data[:4])[0] != 0x09ABCDEF: raise ValueError('firma de compresion incorrecta %08x' % magic)
    e = '<' if le else '>'; rl = struct.unpack(e + 'I', data[4:8])[0]; tab = struct.unpack(e + '64H', data[8:136])
    out = bytearray(data[136:136 + rl]); p = 136 + rl
    while len(out) < size:
        b1 = data[p]; p += 1
        if b1 & 0x80:
            b2 = data[p]; p += 1; n = (b1 & 0x7F) + ((b2 & 0xC0) << 1) + 3; off = tab[b2 & 0x3F]
            if off == 0 or off > len(out): raise ValueError('puntero fuera de rango')
            for _ in range(n): out.append(out[-off])
        else:
            n = b1 + 1; out += data[p:p + n]; p += n
    if len(out) != size: raise ValueError(f'banda de {len(out)} bytes, se esperaban {size}')
    if p != len(data): raise ValueError(f'sobran {len(data) - p} bytes en la banda')
    return bytes(out)

def main(job, prefix):
    d = open(job, 'rb').read(); k = d.index(b'ENTER LANGUAGE = QPDL\n') + 22; pages = 0
    while k < len(d) and d[k] == 0:
        res, copies, paper = d[k + 1], struct.unpack('>H', d[k + 2:k + 4])[0], d[k + 4]; k += 17; rows = []; width = 0; num = 0
        while d[k] == 0x0C:
            bn, w, h, comp, ln = d[k + 1], *struct.unpack('>HHBI', d[k + 2:k + 11]); k += 11
            if comp != 0x11 or h != 128 or w % 8: raise ValueError(f'banda {bn}: cabecera rara w={w} h={h} comp={comp:#x}')
            if bn != num & 255: raise ValueError(f'banda {bn} fuera de orden (esperaba {num & 255})')
            blk, ck = d[k:k + ln - 4], struct.unpack('>I', d[k + ln - 4:k + ln])[0]; k += ln
            if sum(blk) & 0xFFFFFFFF != ck: raise ValueError(f'banda {bn}: suma de control incorrecta')
            wb = w // 8; col = band(blk, wb * 128); width = w; num += 1
            for r in range(128): rows.append(bytes(col[c * 128 + r] for c in range(wb)))  # 1 = blanco, igual que en PNG
        if d[k] != 1: raise ValueError(f'falta el pie de pagina en {k}')
        k += 3; pages += 1; png(f'{prefix}-{pages}.png', width, len(rows), rows)
        ink = sum(8 - bin(b).count('1') for r in rows for b in r)
        print(f'pagina {pages}: {res * 100} ppp, {width}x{len(rows)}, papel {paper}, copias {copies}, {num} bandas, {ink} puntos negros')
    if not d[k:].startswith(b'\t\x1b%-12345X'): raise ValueError('falta el final del trabajo (tabulador + ESC%-12345X)')
    print('QPDL correcto:', pages, 'paginas')

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
