#!/usr/bin/env python3
import os, subprocess, sys
ND = os.environ.get('BORRADOS', '12')   # entradas borradas del disco FAT (las cuenta tools/mkpruebas.sh)
out, q = sys.argv[1:3]; o = (88, 76); W, H = 900, 590
def src(i): return f'click:{o[0]+80},{o[1]+60+36*i+16}'
dest = f'click:{o[0]+80},{o[1]+H-130+36+14}'
def tab(x): return f'click:{o[0]+x},{o[1]+18}'
go = f'click:{o[0]+W-60},{o[1]+H-20}'; rec = f'click:{o[0]+W-190},{o[1]+H-20}'; todos = f'click:{o[0]+W-60},{o[1]+50}'
s = ['wait:escritorio listo', 'wait:vfs: /ahci4', 'sleep:1', 'dclick:64,52', 'sleep:2',
 src(3), 'sleep:0.3', dest, 'sleep:0.2', go, 'wait:rescate: Terminado: 5 de 5 archivos', 'sleep:0.5', 'shot:r-copia',
 tab(180), 'sleep:0.2', go, f'wait:borrados encontrados {ND}', todos, 'sleep:0.2', rec, f'wait:rescate: Terminado: {ND} archivos', 'sleep:0.3', 'shot:r-borrados',
 tab(60), src(4), 'sleep:0.3', dest, go, 'wait:rescate: Terminado: 33 de 33 archivos', 'sleep:0.3',
 tab(180), go, 'wait:borrados encontrados 1', todos, rec, 'wait:rescate: Terminado: 1 archivos', 'sleep:0.3',
 tab(300), src(5), 'sleep:0.3', dest, go, 'wait:rescate: Terminado: 4 fotos, 5 documentos, 3 audio/', 'sleep:0.3', 'shot:r-profunda',
 tab(60), src(6), 'sleep:0.3', dest, go, 'wait:con problemas 1', 'sleep:0.3',
 tab(470), go, 'wait:rescate: Terminado: 64 MB en 1 trozos', 'sleep:0.3',
 tab(590), src(7), 'sleep:0.3', go, 'wait:particiones encontradas 1', 'sleep:0.3', f'click:{o[0]+500},{o[1]+36+10+46+12}', 'wait:vfs: /ahci5p1', 'sleep:0.5', 'shot:r-particiones', 'key:alt-f4', 'sleep:0.5', 'exit:0.1']
s[-1] = 'sleep:0.2'
sys.exit(subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', 'bios', '--out', out, '--timeout', '180', '--port', '4600', '--qemu', q] + s))
