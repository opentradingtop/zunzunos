#!/usr/bin/env python3
"""Genera user/lib/i18n_tab.h desde tools/traducciones.tsv (clave<TAB>espanol o -<TAB>ingles)."""
import re, sys
src = sys.argv[1] if len(sys.argv) > 1 else 'tools/traducciones.tsv'
dst = sys.argv[2] if len(sys.argv) > 2 else 'user/lib/i18n_tab.h'
fmt = lambda x: re.findall(r'%[-+0-9.]*l?[a-z%]', x)
seen = {}
for n, ln in enumerate(open(src, encoding='utf-8'), 1):
    ln = ln.rstrip('\n')
    if not ln: continue
    k, es, en = ln.split('\t')
    if fmt(k) != fmt(en) or (es != '-' and fmt(k) != fmt(es)): sys.exit('%s:%d formato distinto: %s' % (src, n, k))
    seen.setdefault(k, (es, en))
q = lambda s: '"' + s.replace('"', '\\"') + '"'
with open(dst, 'w', encoding='utf-8') as o:
    o.write('/* Generado por tools/mki18n.py: {clave, espanol con acentos o 0, ingles (EE. UU.)} */\nstatic const char *const TRT[][3] = {\n')
    for k, (es, en) in seen.items(): o.write('    {%s, %s, %s},\n' % (q(k), '0' if es == '-' else q(es), q(en)))
    o.write('};\n')
print(dst, len(seen))
