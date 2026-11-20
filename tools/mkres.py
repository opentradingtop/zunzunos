#!/usr/bin/env python3
"""Genera los recursos del sistema: imagen PNG, sonido WAV, pagina de inicio y ayuda."""
import math, os, struct, sys, zlib
d = sys.argv[1]; os.makedirs(d, exist_ok=True)
# PNG 480x300 con degradado y un sol
W, H = 480, 300
raw = bytearray()
for y in range(H):
    raw.append(0)
    for x in range(W):
        t = y / H; r, g, b = int(15 + 40 * t), int(118 + 80 * t), int(110 + 60 * (1 - t))
        if (x - 360) ** 2 + (y - 90) ** 2 < 2200: r, g, b = 253, 224, 71
        if y > 210 + 24 * math.sin(x / 40): r, g, b = 6, 78, 59
        raw += bytes((r, g, b))
def ch(t, c): return struct.pack('>I', len(c)) + t + c + struct.pack('>I', zlib.crc32(t + c))
png = b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0)) + ch(b'IDAT', zlib.compress(bytes(raw), 9)) + ch(b'IEND', b'')
open(f'{d}/muestra.png', 'wb').write(png)
# WAV: pequena melodia
rate = 22050; pcm = bytearray()
for f in (523, 659, 784, 1047, 784, 659, 523):
    n = rate // 4
    for i in range(n):
        env = min(1, i / 300) * (1 - i / n)
        pcm += struct.pack('<h', int(10000 * env * math.sin(2 * math.pi * f * i / rate)))
open(f'{d}/tono.wav', 'wb').write(b'RIFF' + struct.pack('<I', 36 + len(pcm)) + b'WAVEfmt ' + struct.pack('<IHHIIHH', 16, 1, 1, rate, rate * 2, 2, 16) + b'data' + struct.pack('<I', len(pcm)) + pcm)
open(f'{d}/inicio.html', 'w', encoding='utf-8').write('''<html><head><title>Inicio</title></head><body>
<h1>ZunzunOS</h1>
<p>Bienvenido al navegador de <b>ZunzunOS</b>, un sistema operativo de 64 bits escrito desde cero e inspirado en KolibriOS.</p>
<h2>Que puedes hacer</h2>
<ul><li>Escribir una direccion <code>http://</code> arriba y pulsar Intro.</li><li>Abrir archivos <i>.html</i> desde Archivos.</li><li>Leer la <a href="ayuda.html">ayuda del sistema</a>.</li></ul>
<h2>Limites</h2><p>No hay HTTPS, JavaScript ni imagenes dentro de las paginas.</p></body></html>''')
open(f'{d}/ayuda.html', 'w', encoding='utf-8').write('''<html><head><title>Ayuda</title></head><body><h1>Ayuda</h1>
<h2>Escritorio</h2><p>Doble clic en un icono abre el programa. El boton Inicio muestra todos los programas.</p>
<h2>Teclas</h2><ul><li><b>Alt+Tab</b>: cambiar de ventana</li><li><b>Alt+F4</b>: cerrar la ventana</li></ul>
<h2>Volumenes</h2><p><code>/rd</code> es el disco en memoria. <code>/sys</code> es la particion del sistema. Los discos aparecen como <code>/usb0</code>, <code>/ahci0p1</code> o <code>/nvme0p1</code>.</p>
<p><a href="inicio.html">Volver al inicio</a></p></body></html>''')
open(f'{d}/ayuda.txt', 'w', encoding='utf-8').write('ZunzunOS\n\nEscribe "ayuda" en la Terminal para ver las ordenes.\nLo que guardes en /sys queda en la memoria USB; /rd se borra al apagar.\n')
