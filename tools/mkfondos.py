#!/usr/bin/env python3
"""Genera los fondos de escritorio originales de ZunzunOS (PNG, sin dependencias)."""
import math, random, struct, sys, zlib
W, H = 1366, 768

def png(path, rows):
    raw = bytearray()
    prev = bytes(W * 3)
    for r in rows:
        r = bytes(r)
        up = bytes((r[i] - prev[i]) & 255 for i in range(len(r)))  # filtro Up: comprime los degradados
        raw += b'\x02' + up; prev = r
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    data = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b'')
    open(path, 'wb').write(data); print(path, len(data))

def mix(a, b, t): return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))
def clamp(v): return 0 if v < 0 else 255 if v > 255 else int(v)

def amanecer():
    sky = [(18, 32, 74), (196, 84, 96), (250, 176, 102)]
    sun = (W * 0.68, H * 0.62)
    hills = [(0.70, 0.05, 3.1, (34, 70, 78)), (0.80, 0.04, 5.3, (20, 48, 58)), (0.90, 0.03, 8.7, (10, 28, 38))]
    for y in range(H):
        t = y / H; c = mix(sky[0], sky[1], t / 0.6) if t < 0.6 else mix(sky[1], sky[2], (t - 0.6) / 0.4)
        row = bytearray()
        for x in range(W):
            d = math.hypot(x - sun[0], y - sun[1]); g = max(0.0, 1 - d / 420) ** 2
            p = [c[0] + 90 * g, c[1] + 70 * g, c[2] + 30 * g]
            if d < 70: p = [255, 230, 170]
            for base, amp, fr, col in hills:
                if y > H * (base + amp * math.sin(x / W * fr + base * 7)): p = list(col)
            row += bytes(clamp(v) for v in p)
        yield row

def noche():
    random.seed(7); stars = {(random.randrange(W), random.randrange(int(H * 0.8))) for _ in range(700)}
    for y in range(H):
        t = y / H; c = mix((6, 10, 30), (24, 36, 84), t)
        row = bytearray()
        for x in range(W):
            p = list(c)
            if (x, y) in stars: p = [230, 236, 255]
            wave = H * (0.82 + 0.05 * math.sin(x / 140.0) + 0.02 * math.sin(x / 37.0))
            if y > wave: p = mix((10, 60, 80), (4, 20, 34), min(1, (y - wave) / 120))
            row += bytes(clamp(v) for v in p)
        yield row

def selva():
    for y in range(H):
        row = bytearray()
        for x in range(W):
            v = math.sin(x / 210.0 + math.sin(y / 160.0) * 1.7) + math.sin((x + y) / 330.0)
            t = (v + 2) / 4; band = int(t * 7) / 7
            row += bytes(mix((8, 60, 48), (120, 200, 130), band))
        yield row

if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else 'user/res'
    png(out + '/fondo-amanecer.png', amanecer())
    png(out + '/fondo-noche.png', noche())
    png(out + '/fondo-selva.png', selva())
