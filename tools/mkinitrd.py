#!/usr/bin/env python3
"""Empaqueta programas y recursos en INITRD.BIN (formato ZRD1)."""
import os, struct, sys
out, *dirs = sys.argv[1:]
files = []
prefixes = ['bin', 'res', 'inst']
for d, pre in zip(dirs, prefixes):
    if not os.path.isdir(d):
        continue
    for n in sorted(os.listdir(d)):
        p = os.path.join(d, n)
        if os.path.isfile(p) and not n.startswith('.'):
            files.append((f'{pre}/{n}', open(p, 'rb').read()))
hdr_size = 16 + 64 * len(files)
off = hdr_size
body = bytearray()
table = bytearray()
for name, data in files:
    table += struct.pack('<52sII I'.replace(' ', ''), name.encode(), off, len(data), 0)
    body += data + bytes(-len(data) % 16)
    off = hdr_size + len(body)
open(out, 'wb').write(b'ZRD1' + struct.pack('<III', len(files), off, 0) + table + body)
print(f'{out}: {len(files)} archivos, {off} bytes')
