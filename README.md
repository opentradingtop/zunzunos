# ZunzúnOS

**Un OS 100% independiente, libre y realmente privado.** Creado por Tony Fluxa ([tonyfluxa.top](https://tonyfluxa.top)).

*A 100% independent, free and truly private OS. Bilingual: Spanish and US English, switched with one click on the flag in the panel.*

Sistema operativo de 64 bits para PC, escrito desde cero e inspirado en
[KolibriOS](https://kolibrios.org/en). El zunzún es un colibrí. Es una memoria USB
para **rescatar datos** de equipos averiados y una máquina de escritura sin distracciones.

El estudio de KolibriOS y las decisiones de diseño están en
[docs/ESTUDIO-KOLIBRIOS.md](docs/ESTUDIO-KOLIBRIOS.md).

## Qué incluye

| Área | Contenido |
|---|---|
| Arranque | BIOS (MBR propio) y UEFI (`BOOTX64.EFI`) en la misma memoria |
| Kernel | x86-64, multitarea apropiativa, hilos, procesos aislados en anillo 3, memoria bajo demanda, `int 0x40` |
| Gráficos | Compositor y gestor de ventanas: mover, redimensionar, maximizar, minimizar, Alt+Tab |
| Entrada | PS/2 y USB (teclado, ratón, tableta, mando de juego genérico), teclado español y estadounidense, teclado numérico con Bloq Num y luces sincronizadas |
| Idioma | Todo el sistema en español o inglés (EE. UU.); se cambia con un clic en la bandera del panel |
| USB | xHCI (USB 3) y EHCI (USB 2.0), hubs, conexión en caliente, teclados, ratones y memorias |
| Discos | IDE/ATAPI, AHCI, NVMe, USB y tarjetas SD. Particiones MBR y GPT |
| Archivos | FAT12/16/32 y exFAT con lectura y escritura. NTFS, ext2/3/4 e ISO9660 de solo lectura. Los volúmenes se montan protegidos contra escritura |
| Rescate | Copia tolerante a sectores dañados, archivos borrados (FAT, NTFS, exFAT y ext2; ext3/4 si conservan sus bloques), recuperación por firmas, imagen de disco y particiones perdidas |
| Ofimática | Escritor (libros; TXT, MD, HTML, RTF y DOCX), Hoja (XLSX y CSV), Presentar y Agenda |
| Apps web | Instalador de programas HTML/CSS con formularios y cálculos, desde archivo, portapapeles o http |
| Restauración | Puntos en memoria USB, comprobación, vuelta atrás y actualización del sistema conservando los datos |
| Configuración | Una sola herramienta: apariencia y temas, fondo de escritorio (imagen o degradado), fecha y hora del reloj, idioma y teclado, mandos de juego, programas visibles o desinstalados, pantalla y sonido, información del sistema |
| Escritorio | Iconos agrupados por familias, menú Inicio con subpersianas, carpeta Escritorio (crear carpetas y documentos con el botón derecho); botón derecho en escritorio, iconos, menú Inicio y archivos; papelera de reciclaje; expulsión segura de memorias USB; todas las ventanas se redimensionan por bordes y esquinas |
| Grabadora | Grabación de voz a WAV con Intel HD Audio y AC'97 |
| Discos | Instalador en disco (o actualización conservando los datos), formateo en FAT32, FAT16, exFAT y ext2 |
| Pantalla | Brillo por hardware en Intel HD 2000/3000/4000/Haswell; por software en el resto |
| Impresión | Diálogo Imprimir con vista previa y "Guardar como PDF" en Escritor, Editor, Hoja, Presentar, Imágenes y Lector PDF. Impresoras de red con AirPrint / IPP Everywhere (PDF o PWG Raster), láser PCL 5 o PostScript por USB o red (9100) y de agujas en modo texto. App Impresoras con detección USB (IEEE 1284), búsqueda en la red y página de prueba |
| Seguridad | Criptografía propia: SHA-2, AES-GCM, X25519, ECDH/ECDSA P-256 y P-384, RSA (PKCS#1 y PSS); cliente TLS 1.3 y 1.2 con las autoridades raíz de Mozilla |
| Red | Anclaje USB de iPhone (protocolo de Apple, en pruebas) y de móviles Android (RNDIS) y adaptadores Ethernet USB (CDC-ECM); e1000, e1000e (82574), Intel 82577/82578/82579 e I217/I218, RTL8139, RTL8168/8169, virtio. ARP, IPv4, ICMP, UDP, TCP, DHCP, DNS |
| Sonido | AC'97 e Intel HD Audio |
| Programas | Terminal, Archivos (todas las unidades, copiar, cortar y pegar), Discos, Instalar, Editor, Calculadora, Pintar, Imágenes (galería y visor de PNG, JPEG baseline y progresivo, BMP y GIF), Lector PDF, Navegador web con HTTPS (TLS 1.3 y 1.2, certificados verificados), imágenes y formularios, Música (WAV), Monitor, Configuración, Papelera, Serpiente, Campo minado, Bloques |

## Compilar y probar

Requisitos en macOS: `clang` del sistema y `brew install lld qemu`.

```bash
make
```

```bash
make run
```

```bash
make run-uefi
```

Pruebas automáticas de las aplicaciones (maneja QEMU con teclado y ratón):

```bash
python3 -I tools/gui_test.py /tmp/zunzun-pruebas bios
```

## Grabar en una memoria USB

```bash
tools/grabar-usb.sh
```

```bash
tools/grabar-usb.sh /dev/diskN
```

Borra toda la memoria indicada, crea una partición FAT32 que la ocupa entera y
verifica lo grabado. Los archivos que guardes en `/sys` quedan en la memoria y
también se ven desde Windows, macOS y Linux.

## Arrancar en un PC

1. Conecta la memoria y abre el menú de arranque (F12, F10, F9, F8 o Esc).
2. Elige la memoria USB.
3. En UEFI hay que **desactivar Secure Boot**: el cargador no está firmado.

Hace falta un procesador x86 de 64 bits y al menos 64 MB de RAM. No arranca en
Mac con Apple Silicon.

## Límites frente a KolibriOS

- KolibriOS trae más de 250 programas y 20 años de drivers; aquí hay 26 programas.
- No hay controladores UHCI ni OHCI (USB 1.1).
- No hay Wi-Fi, escritura en NTFS, XFS, HFS+, APFS, HTTPS, JavaScript, aceleración gráfica ni varios núcleos.
- No se incluye ensamblador ni compilador dentro del sistema.
- Las tarjetas Intel I219 de muchos portátiles modernos no están soportadas.
- El panel táctil solo funciona si el equipo lo expone por PS/2 o USB, no por I2C.
- Las apps web no ejecutan JavaScript: funcionan su contenido, CSS y formularios `data-calc`.
- NTFS solo se ha probado con un volumen sintético; los archivos comprimidos o cifrados no se leen.
- exFAT y ext2 se pueden crear, pero dentro del sistema solo se leen.
- Sin probar en emulador, porque QEMU no los emula: transacciones divididas de EHCI
  detrás de un hub interno, Intel 82579, RTL8168/8169, códec Conexant y brillo Intel.
- `DIAGNOST.TXT`, en la raíz de `/sys`, guarda el registro del arranque para depurar hardware real.

## Licencia

ZunzúnOS se publica bajo la licencia **0BSD** (Zero-Clause BSD), la más permisiva posible:
cualquiera puede usarlo, copiarlo, modificarlo y venderlo sin pedir permiso ni citar al autor.
Las excepciones son la tipografía VGA de SeaBIOS (`user/lib/font8x16.h`, LGPL 3.0) y los
certificados raíz de Mozilla (`tools/datos/cacert.pem` y `user/res/raices.bin`, MPL 2.0).
Los detalles están en [LICENSE](LICENSE); las excepciones de terceros (tipografía LGPL y certificados de Mozilla con MPL) están en [NOTICE](NOTICE).

Las traducciones al inglés están en `user/lib/i18n_tab.h` (generado desde `tools/traducciones.tsv`).
