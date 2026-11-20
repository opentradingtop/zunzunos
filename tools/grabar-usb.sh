#!/bin/bash
# Graba ZunzunOS en una memoria USB (macOS). BORRA TODO su contenido.
#   tools/grabar-usb.sh              lista las memorias conectadas
#   tools/grabar-usb.sh /dev/diskN   graba y verifica
set -euo pipefail
cd "$(dirname "$0")/.."
DISK="${1:-}"
if [ -z "$DISK" ]; then diskutil list external physical; echo; echo "Uso: $0 /dev/diskN"; exit 0; fi
[[ "$DISK" =~ ^/dev/disk[0-9]+$ ]] || { echo "Indica el disco entero, por ejemplo /dev/disk4"; exit 1; }
INFO=$(diskutil info "$DISK")
echo "$INFO" | grep -Eq "Device Location: +External" || { echo "$DISK no es externo. Cancelado."; exit 1; }
echo "$INFO" | grep -Eq "Protocol: +USB" || { echo "$DISK no es USB. Cancelado."; exit 1; }
echo "$INFO" | grep -q "Internal: *Yes" && { echo "$DISK es interno. Cancelado."; exit 1; }
BYTES=$(echo "$INFO" | sed -n 's/.*(\([0-9]*\) Bytes).*/\1/p' | head -1)
[ "$BYTES" -lt $((256*1024*1024*1024)) ] || { echo "Disco demasiado grande para ser una memoria. Cancelado."; exit 1; }
make -s all
make -s usb-img DISK_BYTES="$BYTES"
IMG=build/usb.img; RAW="${DISK/\/dev\/disk//dev/rdisk}"; SIZE=$(stat -f%z "$IMG")
if [ "${2:-}" != "--si" ]; then
  echo "Se va a BORRAR:"; echo "$INFO" | grep -E "Device / Media Name|Disk Size"; diskutil list "$DISK"
  read -r -p "Escribe BORRAR para continuar: " OK; [ "$OK" = BORRAR ] || { echo Cancelado; exit 1; }
fi
diskutil unmountDisk force "$DISK"
echo "Grabando $((SIZE/1048576)) MiB (macOS pedira tu contrasena)..."
# La imagen se rellena hasta multiplo de 1 MiB: los dispositivos crudos exigen bloques enteros
dd if="$IMG" bs=1m conv=sync 2>/dev/null | /usr/libexec/authopen -w "$RAW"
sync; sleep 2
diskutil unmountDisk force "$DISK" >/dev/null 2>&1 || true
echo "Verificando..."
set +o pipefail
/usr/libexec/authopen "$RAW" 2>/dev/null | head -c "$SIZE" > build/verificacion.img
set -o pipefail
python3 -I tools/verifica.py "$IMG" build/verificacion.img
[ -n "${NO_EJECT:-}" ] || diskutil eject "$DISK" || true
