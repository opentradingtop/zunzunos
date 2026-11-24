#!/usr/bin/env python3
"""Compara la salida de t.c con lo que tools/mkwindows.py dice que puso en el disco. Uso: comprobar.py SALIDA JSON"""
import json, sys
out = open(sys.argv[1]).read().splitlines(); exp = json.load(open(sys.argv[2])); W = exp['windows']; bad = []
got = {}
for l in out:
    if l.startswith('ARCHIVO '):
        p, n, sha, sz, mt = l[8:].split('|'); got[p] = (int(n), sha, int(sz), int(mt))
for p, e in exp['archivos'].items():
    g = got.get(p)
    if not g: bad.append(f'falta {p}'); continue
    if g[1] != e['sha1'] or g[0] != e['size']: bad.append(f'contenido distinto: {p}')
    if g[2] != e['size']: bad.append(f'tamano (stat) distinto: {p} {g[2]} != {e["size"]}')
    if g[3] != e['mtime']: bad.append(f'fecha distinta: {p}')
for p in got:
    if p not in exp['archivos']: bad.append(f'sobra {p}')
txt = '\n'.join(out)
need = [f"SOFTWARE ProductName={W['producto_registro']}", f"SOFTWARE DisplayVersion={W['version_mostrada']}", 'SOFTWARE CurrentBuild=22631', 'SOFTWARE UBR=4460',
        f"SOFTWARE InstallDate={W['instalado']}", f"SYSTEM ComputerName={W['equipo']}", f"SYSTEM ShutdownTime={W['ultimo_apagado']}",
        'SYSTEM servicios=312', 'SYSTEM iaStorAC.ImagePath=System32\\drivers\\iastorac.sys', 'SYSTEM iaStorAC.Start=0', 'VOLUMEN Windows 3.1 sucio=1',
        'NOENCUENTRA -2 1', 'CORTO 1']
import hashlib
from importlib import util
for n in need:
    if n not in txt: bad.append(f'falta la linea: {n}')
if 'BigBlob.sha1=- ' in txt or ' 40000' not in [l for l in out if 'BigBlob.sha1' in l][0]: bad.append('dato grande (db) mal')
print(f"forense: {len(got)} archivos leidos; " + ('OK' if not bad else 'FALLO\n  ' + '\n  '.join(bad[:20])))
sys.exit(1 if bad else 0)
