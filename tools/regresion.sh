#!/bin/bash
# Regresion completa en QEMU. Uso: tools/regresion.sh DIRECTORIO_DE_TRABAJO
S="$1"; K="$S/k"; mkdir -p "$K"; cd "$(dirname "$0")/.." || exit 1
ok() { echo "== $1"; }
make >/dev/null 2>&1 || { echo "FALLO: make"; exit 1; }
STICK="-device usb-ehci,id=e -device usb-kbd,bus=e.0 -device usb-tablet,bus=e.0 -drive if=none,id=st,format=raw,file=$K/stick.img -device usb-storage,bus=e.0,drive=st,bootindex=0"
mkstick() { cp build/zunzun.img "$K/stick.img"; }
filt() { grep -E 'FALLO|PANICO|fallo de pagina|excepcion|Traceback|termino: -' | grep -v 'prueba-fs\|\[prueba\] .*: FALLO$' ; }
ok "apps"; mkstick; cp build/zunzun.img "$K/limpia.img"; python3 -I tools/gui_test.py "$S/r-gui" bios "-nic user,model=e1000 -audiodev none,id=au -device intel-hda -device hda-output,audiodev=au" 2>&1 | filt
cp "$K/limpia.img" build/zunzun.img
ok "ofimatica"; python3 -I tools/office_test.py "$S/r-of" bios "-device ich9-ahci,id=a -drive if=none,id=h0,format=raw,file=$K/docs.img -device ide-hd,drive=h0,bus=a.0 -device sdhci-pci -drive if=none,id=sd,format=raw,file=$K/sdc.img -device sd-card,drive=sd" 2>&1 | filt
ok "rescate"; Q="-device ich9-ahci,id=a"; i=0; for n in dst fat ntfs carve; do Q="$Q -drive if=none,id=h$i,format=raw,file=$K/$n.img -device ide-hd,drive=h$i,bus=a.$i"; i=$((i+1)); done
Q="$Q -drive if=none,id=h4,format=raw,file=blkdebug:$K/blk.cfg:$K/bad.img -device ide-hd,drive=h4,bus=a.4 -drive if=none,id=h5,format=raw,file=$K/lost.img -device ide-hd,drive=h5,bus=a.5"
python3 -I tools/rescue_test.py "$S/r-res" "$Q" 2>&1 | filt
ok "discos"; rm -f "$K"/inst.img "$K"/f32.img "$K"/f16.img "$K"/ex.img "$K"/e2.img; truncate -s 2048m "$K/inst.img"; truncate -s 512m "$K/f32.img"; truncate -s 256m "$K/f16.img"; truncate -s 512m "$K/ex.img"; truncate -s 512m "$K/e2.img"
make usb-img DISK_BYTES=4026531840 >/dev/null 2>&1; cp build/usb.img "$K/stick.img"; truncate -s 4026531840 "$K/stick.img"; Q="$STICK -device ich9-ahci,id=a"; i=0; for n in inst f32 f16 ex e2; do Q="$Q -drive if=none,id=h$i,format=raw,file=$K/$n.img -device ide-hd,drive=h$i,bus=a.$i"; i=$((i+1)); done
python3 -I tools/disk_test.py "$S/r-disk" bios "$Q" 2>&1 | filt
ok "bordes, tema, grabadora, restaurar"; cp build/usb.img "$K/stick.img"; truncate -s 4026531840 "$K/stick.img"; cp "$K/inst.img" "$K/hd.img"; hdiutil create -quiet -size 512m -fs "MS-DOS FAT32" -volname COPIAS -layout NONE -type UDTO -ov -o "$K/st2" && mv "$K/st2.cdr" "$K/store.img"
python3 -I tools/new_test.py "$S/r-new" bios "$STICK -device ich9-ahci,id=a -drive if=none,id=h0,format=raw,file=$K/hd.img -device ide-hd,drive=h0,bus=a.0 -drive if=none,id=h1,format=raw,file=$K/store.img -device ide-hd,drive=h1,bus=a.1 -audiodev none,id=au -device intel-hda -device hda-duplex,audiodev=au" 2>&1 | filt
ok "idioma, configuracion, imagenes, papelera"; mkstick; python3 -I tools/v21_test.py "$S/r-v21" bios "$STICK -device ich9-ahci,id=a" all 2>&1 | filt
ok "impresion"; mkstick; python3 -I tools/fake_printer.py "$S/r-jobs" > "$S/r-fake.log" 2>&1 & FP=$!; sleep 1
python3 -I tools/print_test.py "$S/r-prn" "-nic user,model=e1000 $STICK" all 2>&1 | filt; kill $FP
Q1=$(grep -l "LANGUAGE = QPDL" "$S"/r-jobs/* 2>/dev/null | tail -1); if [ -n "$Q1" ]; then python3 -I tools/qpdl2png.py "$Q1" "$S/r-prn/qpdl" 2>&1 | filt; else echo "FALLO: no llego el trabajo Samsung SPL2"; fi
ok "imprimir en todos los programas, red e impresora en la barra"; mkstick; rm -rf "$S/r-pj"; python3 -I tools/fake_printer.py "$S/r-pj" > "$S/r-fake2.log" 2>&1 & FP=$!; sleep 1
python3 -I tools/imprimir_test.py "$S/r-imp" "$S/r-pj" "-nic user,model=e1000 $STICK" 2>&1 | filt; kill $FP 2>/dev/null; wait $FP 2>/dev/null
ok "captura de pantalla y cancelar impresion"; mkstick; rm -rf "$S/r-pj3"; python3 -I tools/fake_printer.py "$S/r-pj3" > "$S/r-fake3.log" 2>&1 & FP=$!; sleep 1
python3 -I tools/captura_test.py "$S/r-cap" "$K/stick.img" "-nic user,model=e1000 $STICK" 2>&1 | filt; kill $FP 2>/dev/null; wait $FP 2>/dev/null
ok "lugares, accesos directos y seleccionar"; mkstick; python3 -I tools/seleccion_test.py "$S/r-sel" "$STICK" 2>&1 | filt
ok "grabar pantalla y videos"; mkstick; python3 -I tools/video_test.py "$S/r-vid" "$K/stick.img" "$STICK" 2>&1 | filt
ok "correo"; mkstick; rm -rf "$S/r-mail"; python3 -I tools/fake_mail.py "$S/r-mail" > "$S/r-mail.log" 2>&1 & FM=$!; sleep 1
python3 -I tools/correo_test.py "$S/r-cor" "$S/r-mail" "-nic user,model=e1000 $STICK" 2>&1 | filt; kill $FM 2>/dev/null; wait $FM 2>/dev/null
ok "sesiones"; python3 -I tools/sesion_test.py "$S/r-ses" "$K" 2>&1 | filt
ok "fin"
