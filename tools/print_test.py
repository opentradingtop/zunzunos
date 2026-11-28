#!/usr/bin/env python3
"""Prueba la impresion: impresoras IPP (PDF y PWG) y de red 9100 (PCL y PostScript) contra tools/fake_printer.py,
y el dialogo Imprimir del Editor (imprimir y guardar como PDF)."""
import subprocess, sys
out, q = sys.argv[1:3]; part = sys.argv[3] if len(sys.argv) > 3 else 'all'
MH = 42 + 9 * 28 + 14; MY = 800 - 36 - MH - 2
def fam_y(f): return MY + 42 + 28 * f + 14
def app_y(f, i, n): y0 = max(42, min(42 + f * 28, MH - 6 - n * 28)); return MY + y0 + 28 * i + 14
def menu(f, i, n): return ['click:50,782', 'sleep:0.6', f'move:100,{fam_y(f)}', 'sleep:0.5', f'move:350,{app_y(f, i, n)}', 'sleep:0.3', f'click:350,{app_y(f, i, n)}', 'sleep:2']
s = ['wait:escritorio listo', 'wait:vfs: /sys', 'wait:red: IP', 'sleep:1']
OX, OY = 88, 76
if part in ('all', 'prn'):
    s += menu(5, 1, 6)
    def add(h, extra=()): return [f'click:{OX+168},{OY+20}', 'sleep:0.6', f'type:{h}\\n', 'sleep:1.5', *extra, 'wait:impresora anadida', 'sleep:0.5']
    test = [f'click:{OX+452},{OY+20}', 'sleep:0.5']
    s += add('10.0.2.2:8631') + test + ['wait:(pdf,', 'sleep:0.5', 'shot:pr-1']
    s += add('10.0.2.2:8632') + test + ['wait:(pwg,', 'sleep:0.5']
    s += add('10.0.2.2', ['shot:pr-menu', f'click:{OX+180},{OY+40+15}']) + test + ['wait:(pcl,', 'sleep:0.5']
    s += add('10.0.2.2', [f'click:{OX+180},{OY+40+41}']) + test + ['wait:(ps,', 'sleep:0.5', 'shot:pr-2']
    lang = f'click:{OX+380+150},{OY+80+13}'  # boton Lenguaje: PostScript -> PCL -> Texto -> Samsung SPL2
    s += [lang, 'sleep:0.4', lang, 'sleep:0.4', lang, 'sleep:0.6', 'shot:pr-qpdl'] + test + ['wait:(qpdl,', 'sleep:1', 'key:alt-f4', 'sleep:0.6']
if part in ('all', 'dlg'):
    s += menu(1, 4, 7) + ['type:Hola, impresora. Texto con acentos: canción, niño, pingüino y 25 €.\\n', 'type:Segunda línea del documento de prueba.\\n', 'key:ctrl-p', 'wait:dialogo imprimir abierto', 'sleep:1.5', 'shot:dlg-1']
    DX, DY = 260, 150
    s += [f'click:{DX+760-65},{DY+500-25}', 'wait:imprimir ', 'sleep:1', 'key:ctrl-p', 'wait:dialogo imprimir abierto', 'sleep:1', f'click:{DX+400},{DY+32+13}', 'sleep:0.8', 'shot:dlg-2', f'click:{DX+760-65},{DY+500-25}', 'sleep:0.8', 'key:ret', 'wait:pdf guardado', 'sleep:0.5', 'shot:dlg-3']
sys.exit(subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', 'bios', '--no-disk', '--abs', '--out', out, '--timeout', '60', '--port', '4690', '--qemu', q] + s))
