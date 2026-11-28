#!/bin/sh
# Compila el banco de pruebas de Alquimia en el anfitrion (Linux o macOS): tools/alquimia/build.sh SALIDA
set -e
R=$(cd "$(dirname "$0")/../.." && pwd); O=${1:-/tmp/alquimia-prueba}; B=$(mktemp -d)
python3 -I "$R/tools/alquimia/mkhost.py" "$R/user/lib/z.h" "$B/z.h" >/dev/null
for f in office.c extra.c zjson.c zcrypto.c zcrypto.h zmp3.c zmp3d.c zprint.c zvideo.c zp_metrics.h mp3tab.h; do cp "$R/user/lib/$f" "$B/"; done
CF="-ffunction-sections -fdata-sections -O2 -w -fno-builtin -fno-strict-aliasing -I$B -include $B/z.h"
cc $CF -c "$R/tools/alquimia/prueba.c" -o "$B/prueba.o"
for f in office extra zjson zcrypto zmp3 zmp3d zprint zvideo; do cc $CF -c "$B/$f.c" -o "$B/$f.o"; done
cc -O2 -w -c "$R/tools/alquimia/shim.c" -o "$B/shim.o"
if [ "$(uname)" = Darwin ]; then LF="-Wl,-dead_strip"; else LF="-no-pie -Wl,--gc-sections"; fi
cc $LF -o "$O" "$B"/*.o -lm
rm -rf "$B"; echo "$O"
