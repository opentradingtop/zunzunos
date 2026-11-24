#!/bin/bash
# Prueba en el Mac del MP3 de ZunzunOS (codificador user/lib/zmp3.c y decodificador user/lib/zmp3d.c, ambos propios):
# 1. codifica senales de prueba y las decodifica con zmp3d.c: retardo, ganancia y relacion senal/ruido (t.c);
# 2. el decodificador de macOS (afconvert/afinfo) tiene que aceptar esos archivos con su frecuencia, canales y duracion;
# 3. flujos.c genera flujos MPEG validos al azar (capas I, II y III, MPEG-1/2/2.5, estereo M/S e intensidad, bloques
#    cortos, deposito de bits...) y zmp3d.c tiene que dar lo mismo que macOS en todos (compara.c, 60 dB o mas).
# Uso: correr.sh DIR
set -e; cd "$(dirname "$0")"; D="${1:-/tmp/zz-mp3}"; mkdir -p "$D"
cp ../../../user/lib/zmp3.c ../../../user/lib/zmp3d.c ../../../user/lib/mp3tab.h z.h t.c flujos.c compara.c "$D/"   # con el z.h de aqui, no el del sistema
cc -O2 -w -std=gnu11 -I"$D" -o "$D/t" "$D/t.c" "$D/zmp3.c" "$D/zmp3d.c" -lm
cc -O2 -w -std=gnu11 -I"$D" -o "$D/flujos" "$D/flujos.c"
cc -O2 -w -std=gnu11 -I"$D" -o "$D/compara" "$D/compara.c" "$D/zmp3d.c" -lm
"$D/t" "$D"
if command -v afconvert >/dev/null; then
  for f in "$D"/s*.mp3; do
    afconvert -f WAVE -d LEI16 "$f" "$D/dec.wav" 2>/dev/null || { echo "FALLO: macOS no decodifica $(basename "$f")"; exit 1; }
    i=$(afinfo "$D/dec.wav"); d=$(echo "$i" | awk '/estimated duration/ {print $3}')
    awk -v d="$d" 'BEGIN { exit !(d > 2.95 && d < 3.1) }' || { echo "FALLO: duracion rara en $(basename "$f"): $d"; exit 1; }
  done; echo "macOS decodifica los $(ls "$D"/s*.mp3 | wc -l | tr -d ' ') archivos del codificador con su duracion"
  mkdir -p "$D/flujos.d"; rm -f "$D"/flujos.d/*; "$D/flujos" "$D/flujos.d"; n=0; mal=0
  for f in "$D"/flujos.d/*.mp3; do
    n=$((n + 1))
    if ! afconvert -f WAVE -d LEI16 "$f" "$D/ref.wav" 2>/dev/null; then echo "macOS no decodifica $(basename "$f")"; mal=$((mal + 1)); continue; fi
    "$D/compara" "$f" "$D/ref.wav" >/dev/null || { "$D/compara" "$f" "$D/ref.wav"; mal=$((mal + 1)); }
  done
  [ "$mal" = 0 ] || { echo "FALLO: $mal de $n flujos dan distinto que macOS"; exit 1; }
  echo "decodificador: los $n flujos al azar dan lo mismo que macOS"
fi
echo "mp3: OK"
