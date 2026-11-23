#!/bin/bash
# Prueba en el Mac de la lectura de la bateria (kernel/acpi.c) con un DSDT y un EC simulados. Uso: correr.sh DIR
set -e; cd "$(dirname "$0")"; D="${1:-/tmp/zz-bateria}"; mkdir -p "$D"; cp ../../../kernel/acpi.c "$D/acpi.c"; cp kernel.h t.c "$D/"
cc -w -std=gnu11 -I"$D" -o "$D/t" "$D/t.c" && "$D/t"
