#!/bin/sh
# Pruebas de Alquimia en el anfitrion (Mac o Linux), sin arrancar ZunzunOS: tools/alquimia_test.sh CARPETA
# Compila el motor con tools/alquimia/build.sh, lo compara con unzip, tar, gzip, Python, PIL, ffmpeg... y saca
# capturas de la interfaz en CARPETA/capturas. Necesita: python3 con pillow numpy cryptography python-docx openpyxl
# pyyaml; ffmpeg, pdftotext (poppler), zip y unzip.
R=$(cd "$(dirname "$0")/.." && pwd); T=${1:-/tmp/alquimia}
python3 -c "import PIL, numpy, cryptography, docx, openpyxl, yaml" 2>/dev/null || { echo "FALLO: faltan modulos de Python: pip3 install pillow numpy cryptography python-docx openpyxl pyyaml"; exit 1; }
for p in ffmpeg ffprobe pdftotext pdfinfo zip unzip; do command -v $p >/dev/null || { echo "FALLO: falta $p"; exit 1; }; done
mkdir -p "$T"; "$R/tools/alquimia/build.sh" "$T/alquimia-prueba" >/dev/null || { echo "FALLO: no compila el motor"; exit 1; }
python3 -I "$R/tools/alquimia/verifica.py" "$T/alquimia-prueba" "$T/datos" > "$T/verifica.log" 2>&1; r=$?; grep -v "^  bien" "$T/verifica.log"
"$R/tools/alquimia/ui_host.sh" "$T/datos" "$T/capturas" >/dev/null && echo "capturas en $T/capturas" || { echo "FALLO: capturas de la interfaz"; r=1; }
exit $r
