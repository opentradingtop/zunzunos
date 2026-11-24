#!/bin/bash
# Crea los discos de prueba de la regresion (macOS: usa hdiutil y mount_msdos).
#   tools/mkpruebas.sh CARPETA      crea los que falten
#   tools/mkpruebas.sh CARPETA dst  vuelve a crear solo el destino del rescate (se llena tras unas pasadas)
# Discos: dst (destino FAT32), fat (FAT16 con 2 borrados) + fat.sum/del.sum, bad (copia de fat) + blk.cfg (error de lectura
# en raiz.bin), ntfs (mkntfs.py), carve (mkrescue.py), lost (FAT32 sin tabla de particiones), docs y sdc (ofimatica).
set -euo pipefail
cd "$(dirname "$0")/.."
K="$1"; mkdir -p "$K"; K="$(cd "$K" && pwd)"; M="$K/m"; mkdir -p "$M"
mkdst() { rm -f "$K/dst2.cdr"; hdiutil create -quiet -size 512m -fs "MS-DOS FAT32" -volname DESTINO -layout NONE -type UDTO -ov -o "$K/dst2"; mv "$K/dst2.cdr" "$K/dst.img"; }
if [ "${2:-}" = dst ]; then mkdst; exit 0; fi
# borrados: cuantas entradas borradas tiene fat.img (macOS deja tambien archivos ocultos ._ borrados; depende del Mac)
if [ "${2:-}" = borrados ]; then python3 -I - "$K/fat.img" <<'PY'
import struct, sys
d = open(sys.argv[1], 'rb').read(); bps = struct.unpack_from('<H', d, 11)[0]; spc = d[13]; rs = struct.unpack_from('<H', d, 14)[0]; nf = d[16]; re_ = struct.unpack_from('<H', d, 17)[0]; fz = struct.unpack_from('<H', d, 22)[0]
root = (rs + nf * fz) * bps; data = root + re_ * 32; cs = spc * bps; n = 0
def chain(c):
    out = []
    while 2 <= c < 0xFFF8 and len(out) < 10000: out.append(c); c = struct.unpack_from('<H', d, rs * bps + 2 * c)[0]
    return out
def scan(b):
    global n
    for o in range(0, len(b), 32):
        e = b[o:o + 32]
        if e[0] == 0: break
        if e[11] == 0x0F or e[11] & 8: continue
        if e[0] == 0xE5: n += not e[11] & 0x10 and struct.unpack_from('<I', e, 28)[0] > 0; continue   # como Rescate: archivos con datos, no carpetas
        if e[11] & 0x10 and e[0] != 0x2E: scan(b''.join(d[data + (x - 2) * cs:data + (x - 1) * cs] for x in chain(struct.unpack_from('<H', e, 26)[0])))
scan(d[root:data]); print(n)
PY
exit 0; fi
attach() { hdiutil attach -nomount -imagekey diskimage-class=CRawDiskImage "$1" | head -1 | awk '{print $1}'; }
limpia() { rm -rf "$M/.fseventsd" "$M/.Spotlight-V100" "$M/.Trashes"; find "$M" -name '._*' -delete; }
[ -f "$K/dst.img" ] || mkdst
if [ ! -f "$K/fat.img" ] || [ ! -f "$K/blk.cfg" ]; then
  rm -f "$K/fat.cdr"; hdiutil create -quiet -size 64m -fs "MS-DOS FAT16" -volname ORIGEN -layout NONE -type UDTO -o "$K/fat"
  d=$(attach "$K/fat.cdr"); mount -t msdos "$d" "$M"; trap 'umount "$M" 2>/dev/null; hdiutil detach -quiet "$d" 2>/dev/null' EXIT
  mkdir -p "$M/Fotos" "$M/Papeles"
  python3 -I -c "
import random, sys
r = random.Random(3); M = sys.argv[1]
for n, s in [('Fotos/playa.jpg', 70000), ('Fotos/casa.png', 40000), ('Papeles/contrato.docx', 30000), ('Papeles/notas largas de trabajo.txt', 5000), ('raiz.bin', 12345), ('borrar uno.txt', 9000), ('Papeles/borrar dos.pdf', 21000)]:
    open(M + '/' + n, 'wb').write(r.randbytes(s))" "$M"
  limpia; shasum "$M/borrar uno.txt" "$M/Papeles/borrar dos.pdf" > "$K/del.sum"
  (cd "$M" && find . -type f -not -path './.*' -not -name 'borrar*' -print0 | xargs -0 shasum) > "$K/fat.sum"
  rm "$M/borrar uno.txt" "$M/Papeles/borrar dos.pdf"; limpia; umount "$M"; hdiutil detach -quiet "$d"; trap - EXIT
  mv "$K/fat.cdr" "$K/fat.img"; cp "$K/fat.img" "$K/bad.img"
  python3 -I - "$K" <<'EOF'
import struct, sys
K = sys.argv[1]; d = open(K + '/fat.img', 'rb').read()
rs = struct.unpack_from('<H', d, 14)[0]; fz = struct.unpack_from('<H', d, 22)[0]; re_ = struct.unpack_from('<H', d, 17)[0]; root = rs + 2 * fz; data = root + (re_ * 32 + 511) // 512
for o in range(root * 512, data * 512, 32):
    if d[o:o + 11] == b'RAIZ    BIN':
        cl = struct.unpack_from('<H', d, o + 26)[0]; lba = data + (cl - 2) * d[13]
        open(K + '/blk.cfg', 'w').write('[inject-error]\nevent = "read_aio"\nerrno = "5"\nsector = "%d"\nonce = "off"\n' % (lba + 3)); break
else: sys.exit('no se encontro raiz.bin')
EOF
fi
[ -f "$K/ntfs.img" ] || python3 -I tools/mkntfs.py "$K/ntfs.img"
[ -f "$K/carve.img" ] || python3 -I tools/mkrescue.py "$K/carve.img" user/res
if [ ! -f "$K/lost.img" ]; then
  rm -f "$K/l.cdr"; hdiutil create -quiet -size 128m -fs "MS-DOS FAT32" -volname PERDIDA -layout MBRSPUD -type UDTO -o "$K/l"
  python3 -I -c "
import struct, sys
d = open(sys.argv[1] + '/l.cdr', 'rb').read(); st = struct.unpack_from('<I', d, 446 + 8)[0]
open(sys.argv[1] + '/lost.img', 'wb').write(bytes(2048 * 512) + d[st * 512:])" "$K"; rm -f "$K/l.cdr"
fi
if [ ! -f "$K/docs.img" ]; then
  rm -f "$K/docs.cdr"; hdiutil create -quiet -size 64m -fs "MS-DOS FAT16" -volname DOCS -layout NONE -type UDTO -o "$K/docs"
  d=$(attach "$K/docs.cdr"); mount -t msdos "$d" "$M"; trap 'umount "$M" 2>/dev/null; hdiutil detach -quiet "$d" 2>/dev/null' EXIT
  printf 'Titulo importado\nTexto de Word con acentos: canci\xc3\xb3n, ni\xc3\xb1o.\nTercera frase.\n' > "$K/in.txt"
  textutil -convert docx -output "$M/prueba.docx" "$K/in.txt"; textutil -convert odt -output "$M/prueba.odt" "$K/in.txt"; rm -f "$K/in.txt"
  cat > "$M/Calculadora nutricional.html" <<'EOF'
<!doctype html><html><head><meta charset="utf-8"><title>Calculadora nutricional</title></head><body>
<h1>Calculadora nutricional</h1><p>Calcula las calorias de una comida.</p>
<form><label>Gramos <input name="g" value="100"></label> <label>Kcal por 100 g <input name="k" value="250"></label>
<output name="r" for="g k" data-formula="g*k/100">250</output> kcal</form></body></html>
EOF
  limpia; umount "$M"; hdiutil detach -quiet "$d"; trap - EXIT; mv "$K/docs.cdr" "$K/docs.img"
fi
[ -f "$K/sdc.img" ] || cp "$K/fat.img" "$K/sdc.img"
rmdir "$M" 2>/dev/null || true
echo "discos de prueba listos en $K"
