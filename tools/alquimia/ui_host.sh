#!/bin/sh
# Capturas de la interfaz de Alquimia en el anfitrion: tools/alquimia/ui_host.sh CARPETA_DE_PRUEBAS SALIDA
# (CARPETA_DE_PRUEBAS la deja tools/alquimia/verifica.py)
set -e
R=$(cd "$(dirname "$0")/../.." && pwd); T=${1:-/tmp/alquimia-pruebas}; O=${2:-$T/capturas}; B=$(mktemp -d)
python3 -I "$R/tools/alquimia/mkhost.py" "$R/user/lib/z.h" "$B/z.h" >/dev/null
for f in office.c extra.c zjson.c zcrypto.c zcrypto.h zmp3.c zmp3d.c zprint.c zvideo.c zp_metrics.h mp3tab.h gfx.c font8x16.h i18n.c i18n_tab.h; do cp "$R/user/lib/$f" "$B/"; done
CF="-ffunction-sections -fdata-sections -O1 -w -fno-builtin -fno-strict-aliasing -I$B -include $B/z.h"
cc $CF -c "$R/tools/alquimia/ui_host.c" -o "$B/ui.o"
for f in office extra zjson zcrypto zmp3 zmp3d zprint zvideo; do cc $CF -c "$B/$f.c" -o "$B/$f.o"; done
cc $CF -Dzw_new=zw_new_g -Dzw_close=zw_close_g -Dzw_present=zw_present_g -Dzw_present_rect=zw_present_rect_g -Dzw_resized=zw_resized_g -Dui_confirm=ui_confirm_g -c "$B/gfx.c" -o "$B/gfx.o"
cc $CF -Dtrl=trl_i18n -c "$B/i18n.c" -o "$B/i18n.o"
cc -O2 -w -DUI_HOST -c "$R/tools/alquimia/shim.c" -o "$B/shim.o"
if [ "$(uname)" = Darwin ]; then LF="-Wl,-dead_strip"; else LF="-no-pie -Wl,--gc-sections"; fi
cc $LF -o "$B/ui" "$B"/*.o -lm
mkdir -p "$T/ui" "$O"; "$B/ui" "$T" "$O"; rm -rf "$B"
