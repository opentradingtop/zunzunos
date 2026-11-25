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
| Forense | Rastreador de desastres de sistemas: encuentra los Windows de los discos y, **sin modificar nada**, saca su versión, el nombre del equipo, el último apagado correcto, cómo arrancaba, si el volumen quedó dañado, los drivers cambiados justo antes del fallo, lo que dice la reparación automática de Windows y las actualizaciones a medio instalar. Lo guarda todo en un expediente en la memoria USB y lo manda a analizar a un **Lab** de IA. El Lab puede ser:
  - **local**: cualquier servidor compatible con la API de OpenAI y una API key, como el de una empresa o el de casa;
  - **Lyceum**: en la nube, con los datos personales anonimizados.
  El Lab puede pedir más pruebas de solo lectura, que el administrador aprueba una a una. Lector NTFS y lector del registro de Windows propios. Las reparaciones paso a paso llegarán más adelante ([docs/FORENSE.md](docs/FORENSE.md)) |
| Ofimática | Escritor (libros; TXT, MD, HTML, RTF y DOCX), Hoja (XLSX y CSV), Presentar y Agenda |
| Apps web | Instalador de programas HTML/CSS con formularios y cálculos, desde archivo, portapapeles o http |
| Chat con IA | **LUMEN**: chat con los modelos de OpenRouter (gratuitos o de pago), de **Lyceum** (servidores en la UE) o con un servidor compatible con OpenAI en la red local; respuestas en vivo por HTTPS, razonamiento, Markdown con tablas y código, chats guardados, exportar a Markdown e imprimir; **archivos adjuntos** (TXT, DOCX, ODT, XLSX, PDF…) de los que toma los fragmentos que vienen al caso para cada pregunta; imágenes para los modelos que las aceptan. Guía en [docs/LUMEN.md](docs/LUMEN.md) |
| Red local | **Intranet** entre equipos ZunzúnOS por cable, con router o con un cable directo (dirección local automática): un administrador crea la red e invita a los demás (código de 6 cifras en las dos pantallas), carpetas compartidas de solo lectura o de lectura y escritura en cualquier equipo, impresoras compartidas (una impresora USB de un equipo aparece al imprimir en todos), mensajes entre equipos; conexiones autenticadas y cifradas. Guía en [docs/RED.md](docs/RED.md) |
| Privacidad | **Guardia de red**: ningún programa sale a Internet sin permiso (Permitir, Preguntar o Bloquear por programa; la red local siempre se permite) y un registro visible de cada conexión con su programa, destino y bytes (Configuración → Conexiones). Lo hace el núcleo, en todas las salidas: TCP, UDP, DNS y ping |
| Cifrado y escrituras seguras | **Cifrado opcional** de los datos con frase de paso (AES-256 en el núcleo; documentos, escritorio, configuración y puntos de restauración). Guardar es seguro ante un corte de luz: se escribe aparte y se cambia al final |
| Teclado y texto | Seleccionar texto arrastrando con el ratón en todos los programas (también PDF, correo y LUMEN), cortar/copiar/pegar y deshacer/rehacer en todos los cuadros de texto, repetición de teclas al mantenerlas pulsadas, atajos (Win, Win+E, Win+D, Win+L, Ctrl+W, Ctrl+N/O/S/P…) |
| Batería | Porcentaje y estado de carga en la barra en los ThinkPad (lee el controlador del equipo con las posiciones del DSDT), aviso de batería baja |
| Restauración | Puntos en memoria USB, comprobación, vuelta atrás y actualización del sistema conservando los datos |
| Configuración | Una sola herramienta: apariencia y temas, fondo de escritorio (imagen o degradado), fecha y hora del reloj, idioma y teclado, mandos de juego, programas visibles o desinstalados, pantalla y sonido, información del sistema |
| Escritorio | Iconos agrupados por familias, menú Inicio con subpersianas, carpeta Escritorio (crear carpetas y documentos con el botón derecho); botón derecho en escritorio, iconos, menú Inicio y archivos; papelera de reciclaje; expulsión segura de memorias USB; todas las ventanas se redimensionan por bordes y esquinas |
| JavaScript | **Colibrí**, motor JavaScript propio escrito desde cero (`user/colibri`): JavaScript moderno (clases con campos privados, async/await, generadores, Proxy, BigInt, expresiones regulares Unicode con lookbehind y grupos con nombre, matrices tipadas), recolector de memoria propio y números exactos; da los mismos resultados que Node en sus dos baterías de pruebas. **Consola JavaScript** y **Navegador JS** (opcional, aparte del Navegador de siempre): ejecuta los `<script>` de las páginas (también externos y módulos) con DOM (crear y cambiar elementos, `innerHTML`, `classList`, `style`, `dataset`, selectores CSS), eventos (clic, teclado, formularios con `preventDefault`, `onclick="..."`), `fetch` y `XMLHttpRequest` (siempre por la guardia de red), temporizadores, `localStorage` por sitio, `URL`, `history` y `location`. Las aplicaciones web enormes (correo web de Google, Yahoo o iCloud, redes sociales, banca) necesitan mucho más; para el correo está el programa Correo |
| Grabadora y MP3 | Grabación en **estéreo** o mono a 48 kHz, en WAV (sin pérdida) o **MP3** de 64 a 320 kbps con un codificador propio (MPEG-1 Layer III; comprobado con el decodificador de macOS); convierte a MP3 grabaciones WAV que ya existan. Música toca WAV y MP3 con un **decodificador MP3 propio** (capas I, II y III; MPEG-1, 2 y 2.5; estéreo M/S e intensidad), escrito a partir de las normas ISO y comprobado contra el de macOS con canciones reales y cientos de archivos generados al azar |
| Discos | Instalador en disco (o actualización conservando los datos), formateo en FAT32, FAT16, exFAT y ext2 |
| Pantalla | Brillo por hardware en Intel HD 2000/3000/4000/Haswell; por software en el resto |
| Impresión | Diálogo Imprimir con vista previa y "Guardar como PDF" en Escritor, Editor, Hoja, Presentar, Imágenes y Lector PDF. Impresoras de red con AirPrint / IPP Everywhere (PDF o PWG Raster), láser PCL 5 o PostScript por USB o red (9100) y de agujas en modo texto. App Impresoras con detección USB (IEEE 1284), búsqueda en la red y página de prueba |
| Seguridad | Criptografía propia: SHA-2, AES-GCM, X25519, ECDH/ECDSA P-256 y P-384, RSA (PKCS#1 y PSS); cliente TLS 1.3 y 1.2 con las autoridades raíz de Mozilla |
| Red | Anclaje USB de iPhone (protocolo de Apple, en pruebas) y de móviles Android (RNDIS) y adaptadores Ethernet USB (CDC-ECM); e1000, e1000e (82574), Intel 82577/82578/82579 e I217/I218, RTL8139, RTL8168/8169, virtio. ARP, IPv4, ICMP, UDP, TCP, DHCP, DNS |
| Sonido | AC'97 e Intel HD Audio |
| Programas | Terminal, Archivos (todas las unidades, copiar, cortar y pegar), Discos, Instalar, Editor, Calculadora, Pintar, Imágenes (galería y visor de PNG, JPEG baseline y progresivo, BMP y GIF), Lector PDF, Navegador web con HTTPS (TLS 1.3 y 1.2, certificados verificados), imágenes, formularios, marcadores e historial (con botón propio; se pueden borrar las visitas que elijas), Correo, Nube (archivos de pCloud), LUMEN (chat con IA), Música (WAV y MP3), Radio (emisoras de todo el mundo por Internet, catálogo Radio Browser), Monitor, Configuración, Papelera, Serpiente, Campo minado, Bloques |

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
