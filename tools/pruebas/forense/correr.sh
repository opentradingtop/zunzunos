#!/bin/bash
# Prueba en el Mac de los lectores de Forense (user/lib/zntfs.c y zreg.c) con el disco de Windows sintetico
# de tools/mkwindows.py: todo lo que hay en el disco tiene que leerse igual, y un disco estropeado al azar
# (2000 pasadas) no puede colgar el lector. Uso: correr.sh DIR
set -e; cd "$(dirname "$0")"; D="${1:-/tmp/zz-forense}"; mkdir -p "$D"
cp ../../../user/lib/zntfs.c ../../../user/lib/zreg.c ../../../user/lib/zntfs.h z.h t.c "$D/"   # con el z.h de aqui, no el del sistema
cc -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -w -std=gnu11 -I"$D" -o "$D/t" "$D/t.c" "$D/zntfs.c" "$D/zreg.c"
python3 -I ../../mkwindows.py "$D/win.img"
"$D/t" "$D/win.img" > "$D/salida.txt"
python3 -I comprobar.py "$D/salida.txt" "$D/win.img.json"
"$D/t" "$D/win.img" danado 2000
