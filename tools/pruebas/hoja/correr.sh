#!/bin/bash
# Prueba las formulas de la Hoja en el Mac: compila solo la parte de calculo de user/apps/hoja.c
# (hasta setc) con una capa minima (z.h de esta carpeta) y comprueba unas 85 formulas.
# Uso: tools/pruebas/hoja/correr.sh DIR_TRABAJO
set -e; D="$1"; mkdir -p "$D"; cd "$(dirname "$0")/../../.."
python3 -I -c "import sys; s=open('user/apps/hoja.c',encoding='utf-8').read(); open(sys.argv[1]+'/hoja_core.c','w',encoding='utf-8').write(s[:s.index('static void setc(')])" "$D"
cp tools/pruebas/hoja/z.h tools/pruebas/hoja/t.c "$D/"
clang -O1 -w -I"$D" -o "$D/t" "$D/t.c" -lm && "$D/t" || echo "FALLO: formulas de la Hoja"
