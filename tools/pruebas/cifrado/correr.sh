#!/bin/bash
# Prueba en el Mac del AES-256 y el PBKDF2 del nucleo (kernel/cifrado.c) con los vectores oficiales. Uso: correr.sh DIR
set -e; cd "$(dirname "$0")"; D="${1:-/tmp/zz-cifrado}"; mkdir -p "$D"; cp ../../../kernel/cifrado.c kernel.h t.c "$D/"
sed -n '/^\/\* ---------- SHA-256/,/^\/\* hash del PIN/p' ../../../kernel/sesion.c | grep -v 'hash del PIN' | sed 's/^#define ROR.*/#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))/' > "$D/sha.inc"
cc -w -std=gnu11 -I"$D" -o "$D/t" "$D/t.c" && "$D/t"
