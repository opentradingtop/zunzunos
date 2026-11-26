#!/bin/bash
# Graba ZunzunOS para la Raspberry Pi 2 en una tarjeta microSD (macOS). BORRA TODO su contenido.
#   tools/grabar-sd.sh              lista los discos externos
#   tools/grabar-sd.sh /dev/diskN   graba y verifica (la tarjeta en un lector USB)
# La imagen ocupa SD_BYTES (1 GB por defecto) desde el principio de la tarjeta: tabla MBR y una particion FAT32
# como la de Raspberry Pi OS (tipo 0x0C, sector 8192). Necesita el firmware en build/pi/firmware (docs/RASPBERRY.md).
set -euo pipefail
cd "$(dirname "$0")/.."
DISK="${1:-}"
if [ -z "$DISK" ]; then diskutil list external physical; echo; echo "Uso: $0 /dev/diskN"; exit 0; fi
[[ "$DISK" =~ ^/dev/disk[0-9]+$ ]] || { echo "Indica el disco entero, por ejemplo /dev/disk4"; exit 1; }
INFO=$(diskutil info "$DISK")
echo "$INFO" | grep -Eq "Device Location: +External" || { echo "$DISK no es externo. Cancelado."; exit 1; }
echo "$INFO" | grep -q "Internal: *Yes" && { echo "$DISK es interno. Cancelado."; exit 1; }
BYTES=$(echo "$INFO" | sed -n 's/.*(\([0-9]*\) Bytes).*/\1/p' | head -1)
[ "$BYTES" -lt $((128*1024*1024*1024)) ] || { echo "Disco demasiado grande para ser una tarjeta. Cancelado."; exit 1; }
SD_BYTES="${SD_BYTES:-1073741824}"
[ "$SD_BYTES" -le "$BYTES" ] || { echo "La tarjeta es menor que la imagen ($SD_BYTES bytes). Cancelado."; exit 1; }
make -s pi
make -s pi-sd SD_BYTES="$SD_BYTES"
IMG=build/pi/sd.img; RAW="${DISK/\/dev\/disk//dev/rdisk}"; SIZE=$(stat -f%z "$IMG")
if [ "${2:-}" != "--si" ]; then
  echo "Se va a BORRAR:"; echo "$INFO" | grep -E "Device / Media Name|Disk Size"; diskutil list "$DISK"
  read -r -p "Escribe BORRAR para continuar: " OK; [ "$OK" = BORRAR ] || { echo Cancelado; exit 1; }
fi
diskutil unmountDisk force "$DISK"
echo "Grabando $((SIZE/1048576)) MiB (macOS pedira tu contrasena)..."
dd if="$IMG" bs=1m conv=sync 2>/dev/null | /usr/libexec/authopen -w "$RAW"
sync; sleep 2
diskutil unmountDisk force "$DISK" >/dev/null 2>&1 || true
echo "Verificando..."
set +o pipefail
/usr/libexec/authopen "$RAW" 2>/dev/null | head -c "$SIZE" > build/pi/verificacion.img
set -o pipefail
python3 -I tools/verifica.py "$IMG" build/pi/verificacion.img
[ -n "${NO_EJECT:-}" ] || diskutil eject "$DISK" || true
