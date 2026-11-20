#!/usr/bin/env python3
"""Disco con archivos incrustados para probar la recuperacion profunda."""
import hashlib, io, json, os, random, struct, sys, zipfile
out, res = sys.argv[1:3]; random.seed(9); SZ = 64 << 20; img = bytearray(random.randbytes(SZ)); F = {}; pos = 4096
def put(name, d):
    global pos
    img[pos * 512:pos * 512 + len(d)] = d; F[name] = hashlib.sha1(d).hexdigest(); pos += (len(d) + 511) // 512 + 257
def z(names):
    b = io.BytesIO()
    with zipfile.ZipFile(b, 'w', zipfile.ZIP_DEFLATED) as x:
        for n, c in names: x.writestr(n, c, zipfile.ZIP_STORED if n == 'mimetype' else zipfile.ZIP_DEFLATED)
    return b.getvalue()
sc = bytes(random.randrange(254) for _ in range(30000)); jpg = b'\xff\xd8\xff\xe0\x00\x10JFIF\0\x01\x01\0\0\x01\0\x01\0\0\xff\xdb\x00\x05\x00\x01\x02\xff\xda\x00\x04\x01\x02' + sc.replace(b'\xff', b'\xfe') + b'\xff\x00\x10\xff\xd0\x20\xff\xd9'
put('jpg', jpg); put('png', open(f'{res}/muestra.png', 'rb').read()); put('gif', b'GIF89a\x02\0\x02\0\x80\0\0\0\0\0\xff\xff\xff,\0\0\0\0\x02\0\x02\0\0\x02\x03D\x02\x05\0;'); hb = 54 + 3000; put('bmp', b'BM' + struct.pack('<IIII', hb, 0, 54, 40) + bytes(36) + bytes(range(256)) * 11 + bytes(184))
put('pdf', b'%PDF-1.4\n' + b'1 0 obj\n<<>>\nendobj\n' * 400 + b'startxref\n0\n%%EOF\n'); put('rtf', b'{\\rtf1\\ansi Hola {\\b mundo}\\par ' + b'texto ' * 500 + b'}')
put('docx', z([('[Content_Types].xml', '<x/>'), ('word/document.xml', '<d>' + 'a' * 3000 + '</d>')])); put('xlsx', z([('[Content_Types].xml', '<x/>'), ('xl/workbook.xml', '<w/>')])); put('odt', z([('mimetype', 'application/vnd.oasis.opendocument.text'), ('content.xml', '<c/>')])); put('zip', z([('leeme.txt', 'hola ' * 900)]))
put('wav', open(f'{res}/tono.wav', 'rb').read()); mp4 = struct.pack('>I', 24) + b'ftypisom' + bytes(12) + struct.pack('>I', 8008) + b'mdat' + bytes(8000) + struct.pack('>I', 108) + b'moov' + bytes(100); put('mp4', mp4)
fr = b'\xff\xfb\x90\x00' + bytes(413); put('mp3', b'ID3\x03\0\0\0\0\0\x0a' + bytes(10) + fr * 40)
open(out, 'wb').write(img); json.dump(F, open(out + '.json', 'w'))
