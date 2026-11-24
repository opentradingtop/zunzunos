#!/usr/bin/env python3
"""Convierte un archivo JavaScript en una cabecera de C con una cadena: js2h.py ENTRADA.js SALIDA.h NOMBRE"""
import sys
src, out, name = sys.argv[1:4]
t = open(src, encoding='utf-8').read()
lines = ['/* Generado por tools/js2h.py a partir de %s. No editar. */' % src, 'static const char %s[] =' % name]
for l in t.split('\n'):
    e = l.replace('\\', '\\\\').replace('"', '\\"').replace('??', '?\\?')
    lines.append('    "%s\\n"' % ''.join(c if 32 <= ord(c) < 127 else ''.join('\\%03o' % b for b in c.encode('utf-8')) for c in e))
lines[-1] += ';'
open(out, 'w').write('\n'.join(lines) + '\n')
