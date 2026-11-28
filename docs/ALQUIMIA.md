# Alquimia: comprimir, extraer y convertir

Alquimia junta en un solo programa las dos web apps de [tonyfluxa.top](https://tonyfluxa.top):

- **PRESSA / PRESSA PRO**: compresor y descompresor de archivos, con contraseña AES-256 opcional.
- **ALQUIMIA**: conversor de archivos de muchos formatos, al estilo de online-convert.com.

En ZunzúnOS no hay WebAssembly ni las API del navegador que usaban (CompressionStream, WebCrypto, MediaRecorder),
así que se han reescrito en C como programa nativo. Todo se hace en el equipo: no sale nada a Internet.
Está en la familia **Sistema** del menú Inicio. También se abre:

- con doble clic en un `.zip`, `.tar`, `.gz` o `.tgz` (va a la pestaña Extraer);
- desde Archivos, con el botón derecho → **Comprimir o convertir...**, con lo que haya marcado.

## Comprimir

| | |
|---|---|
| Formatos | **ZIP** (deflate), **TAR.GZ**, **TAR** y **GZ** (un solo archivo) |
| Contraseña | Opcional, solo en ZIP: **WinZip AES-256 (AE-2)**, PBKDF2-HMAC-SHA1 de 1000 vueltas, AES en modo CTR y HMAC-SHA1. Es el formato que abren 7-Zip, WinRAR, WinZip, Keka o The Unarchiver; el Explorador de Windows y la Utilidad de Archivo de macOS no abren ZIP con AES |
| Compresión | Rápida, Normal o Máxima. Deflate propio (LZ77 con cadenas hash y emparejamiento perezoso; cada bloque elige Huffman dinámico, fijo o guardado, el más corto) |
| Ya comprimidos | JPG, PNG, MP3, MP4, ZIP, DOCX… se guardan sin deflate: no ganan nada y se tarda menos |
| Memoria | Por flujo: cada archivo se lee en trozos de 1 MB, así que se pueden comprimir archivos más grandes que la memoria |
| Límites | ZIP de hasta 4 GB y 65 535 archivos (no se crea ZIP64) |

El ZIP lleva nombres en UTF-8 y un descriptor de datos detrás de cada archivo (no se vuelve atrás a escribir en el
archivo de salida). Se escribe aparte (`.parcial`) y se cambia de nombre al terminar: un corte no deja un ZIP a medias.
El manómetro de PRESSA dice cuánto espacio se ha ahorrado.

## Extraer

- **ZIP**: guardado y deflate; cifrado AES-128/192/256 (AE-1 y AE-2) y ZipCrypto (el antiguo); ZIP64; nombres en
  UTF-8 o en CP437 (los ZIP antiguos de Windows). Otros métodos (bzip2, LZMA, Deflate64, Zstandard, XZ) se avisan.
- **TAR** (ustar, nombres largos GNU y cabeceras pax), **TAR.GZ / TGZ** y **GZ**.
- Se comprueba el CRC de cada archivo (y el HMAC en AES). **Comprobar** hace lo mismo sin escribir nada.
- Se puede marcar qué extraer (una carpeta marca todo lo de dentro).
- Las rutas peligrosas (`../`, rutas absolutas, `C:`) se quedan dentro de la carpeta de destino, y los caracteres
  que FAT no admite (`: * ? " < > |`) pasan a `_`.

## Convertir

| Tipo | Entra | Sale |
|---|---|---|
| Imagen | PNG, JPG, BMP, GIF, PPM/PGM/PBM, TGA, ICO | PNG, JPG, BMP, GIF, ICO (16 a 256), TGA, PPM, PDF, SVG |
| Sonido | WAV (8, 16, 24 y 32 bits, coma flotante), MP3, AIFF/AIFF-C | MP3 (32 a 320 kbps), WAV, AIFF |
| Vídeo | AVI MJPEG (el que graba ZunzúnOS) | GIF animado, ZIP de fotogramas, PNG (un fotograma), WAV y MP3 (su sonido) |
| Documento | TXT, Markdown, HTML, RTF, DOCX, ODT, EPUB | PDF, DOCX, ODT, EPUB, HTML, Markdown, RTF, TXT |
| Datos | CSV (separador automático), TSV, JSON, XML, XLSX, YAML | XLSX, CSV, TSV, JSON, XML, YAML, HTML, Markdown, TXT en columnas |
| Comprimido | ZIP (también cifrado), TAR, TAR.GZ, GZ | ZIP, TAR.GZ, TAR |
| Cualquiera | — | Base64, volcado HEX, data URI, huella SHA-256 (y CRC-32); y un `.b64` de vuelta a su archivo |

Opciones según el tipo: tamaño en %, lado máximo, calidad del JPG, girar, escala de grises; canales, frecuencia,
kbps del MP3 y normalizar el volumen; fotogramas por segundo y ancho del GIF; papel A4 o Carta y tamaño de letra del
PDF; separador y cabecera de las tablas; contraseña para abrir un ZIP cifrado.

Se pueden poner varios archivos en la lista: cada uno sale al formato elegido para su tipo. Si el archivo de
salida ya existe, se añade `(2)`, `(3)`…

Lo que **no** hace (y por qué):

- No lee PDF ni escribe WEBP, OGG, M4A o MP4: harían falta otros decodificadores y codificadores enteros.
- El GIF y el ICO pierden la transparencia: los decodificadores del sistema entregan las imágenes ya sobre blanco.
- De los documentos se conserva la estructura (títulos, párrafos, listas, citas y código) y el texto, no los tipos
  de letra, las tablas con formato ni las imágenes.
- El vídeo solo es AVI con MJPEG, que es lo que graban **Grabar pantalla** y **Vídeos**.

## El código

| Archivo | Qué hace |
|---|---|
| `user/apps/alquimia.c` | La ventana: pestañas Comprimir, Extraer y Convertir; en pantallas bajas (la de 3,5" de la Raspberry Pi) la cabecera se estrecha y el contenido se desplaza con ▲ ▼ o la rueda |
| `user/apps/alquimia/dfl.c` | Compresor deflate por flujo |
| `user/apps/alquimia/cifra.c` | SHA-1, HMAC, PBKDF2, WinZip AES y ZipCrypto (el AES por bloques es el de `zcrypto.c`) |
| `user/apps/alquimia/arc.c` | ZIP, TAR, TAR.GZ y GZ: crear, abrir, comprobar y extraer |
| `user/apps/alquimia/img.c` | Imágenes: lectores y escritores, cambio de tamaño, GIF (corte por la mediana, difusión de error, LZW) |
| `user/apps/alquimia/aud.c` | Sonido: WAV, AIFF y MP3; canales, frecuencia y normalizar |
| `user/apps/alquimia/doc.c` | Documentos y los ZIP en memoria de DOCX, ODT y EPUB |
| `user/apps/alquimia/datos.c` | Tablas |
| `user/apps/alquimia/conv.c` | Qué se puede convertir en qué, y la conversión |

Además: `z_pick_dir` en `user/lib/zpick.c` (el selector de archivos sirve también para elegir carpetas),
`zc_aes_key`/`zc_aes_enc` en `user/lib/zcrypto.c`, la asociación de `.zip/.tar/.gz/.tgz` en `zmisc.c` y el menú de
Archivos.

## Pruebas

Sin arrancar ZunzúnOS, en el Mac o en Linux:

```bash
tools/alquimia_test.sh /tmp/alquimia
```

Compila el mismo motor para el anfitrión (`tools/alquimia/`) y comprueba más de 200 casos contra programas de
verdad: `unzip`, GNU `tar`, `gzip`, `zip -P`, el `zipfile` y `tarfile` de Python, un descifrado AES independiente con
la biblioteca `cryptography`, PIL, `ffmpeg`/`ffprobe`, `pdftotext`, python-docx, openpyxl y PyYAML. Al final saca
capturas de la interfaz (en español, en inglés y a 480x320) en `/tmp/alquimia/capturas`.

Necesita `python3` con `pillow numpy cryptography python-docx openpyxl pyyaml`, y `ffmpeg`, `poppler` (pdftotext),
`zip` y `unzip`.
