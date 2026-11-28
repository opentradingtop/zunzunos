# Versiones de ZunzúnOS

La cifra está en `user/lib/version.h` y sube con cada actualización que se graba en la memoria USB o se publica.
Se ve en Acerca de y en la primera línea del registro de arranque (y por tanto en `DIAGNOST.TXT`).

## 2.8 — 28/11/2026

- **Red local por Internet** (`docs/RED.md`): los equipos de una misma red se conectan también entre casas
  distintas, con las mismas carpetas, impresoras y mensajes. Pestaña **Internet** en Red local:
  - el servicio abre el puerto TCP 7141 en el router con PCP, UPnP IGD (SSDP y SOAP) o NAT-PMP y lo renueva;
    detecta CGNAT y doble NAT; direcciones a mano con IP o nombre DNS (`*:7141` = la IP pública del momento);
    IP pública por el router, el relé o un servidor STUN opcional;
  - cada equipo anuncia sus direcciones con número de secuencia y los equipos se pasan las de los demás: un
    equipo que cambia de IP se reencuentra; comprobación cada minuto (punto verde «en línea por Internet»);
  - **invitación por Internet** con un código `ZRED1-…@dirección` (secreto de 80 bits, un solo uso, 2 días) y el
    mismo código de 6 cifras en las dos pantallas;
  - **relé** opcional `tools/zred-rele.py` (Python 3) para CGNAT y móviles: solo pasa bytes cifrados de extremo a
    extremo y no sabe de qué red es nadie;
  - seguridad: por Internet solo se atiende lo autenticado (saludo `HOLA2` sin datos en claro e invitaciones con
    código), bloqueo de direcciones que fallan, límite de saludos, plazo de 8 s por saludo; el acceso por Internet
    se activa y desactiva en cada equipo.
- **Adjuntos en los mensajes** (red local e Internet): hasta 10 archivos de 64 MB, por partes con SHA-256, barra
  de progreso, reanudación tras un corte y cola de mensajes pendientes que el servicio reintenta. Se guardan en
  Documentos/Red/Adjuntos y se abren con su programa desde la conversación.
- Núcleo: multidifusión IPv4 de salida (SSDP), dirección del otro extremo de una conexión TCP (`z_peer`), como
  mucho 16 conexiones a medio abrir por puerto y se liberan las que no terminan.
- Pruebas: `tools/red_internet_test.py` (tres casas con routers falsos UPnP, NAT-PMP y CGNAT, y el relé) y
  `tools/imgfs.py` para que las pruebas que miran dentro de la imagen funcionen también en Linux (mtools).
- **Alquimia** (`docs/ALQUIMIA.md`), familia Sistema: las web apps PRESSA / PRESSA PRO y ALQUIMIA en un solo
  programa nativo, con tres pestañas:
  - **Comprimir**: ZIP, TAR.GZ, TAR y GZ con deflate propio por flujo (Rápida, Normal, Máxima); contraseña opcional
    WinZip AES-256 (AE-2) en ZIP; manómetro del espacio ahorrado;
  - **Extraer**: ZIP (AES AE-1/AE-2, ZipCrypto, ZIP64, nombres UTF-8 o CP437), TAR (GNU y pax), TAR.GZ y GZ, con
    comprobación de CRC y HMAC, selección de lo que se extrae y rutas peligrosas contenidas en el destino;
  - **Convertir**: imágenes, sonido, vídeo AVI, documentos, tablas y comprimidos entre sí, y cualquier archivo a
    Base64, HEX, data URI o SHA-256.
  - Archivos: botón derecho → **Comprimir o convertir...**; doble clic en un `.zip/.tar/.gz/.tgz` lo abre en Alquimia.
  - Biblioteca: `z_pick_dir` (el selector sirve también para carpetas) y `zc_aes_key`/`zc_aes_enc`.
  - Pruebas en el Mac o Linux sin arrancar el sistema: `tools/alquimia_test.sh` (219 comprobaciones contra unzip,
    tar, gzip, Python, PIL, ffmpeg, pdftotext… y capturas de la interfaz).
- Raspberry Pi: `make pi` vuelve a compilar (`redlocal.c` usaba `long long`, que en ARM no se puede escribir).

## 2.7 — 26/11/2026

- ZunzúnOS arranca en la **Raspberry Pi 2 Model B** (BCM2836, ARMv7 de 32 bits) con la pantalla táctil de 3,5"
  de 480x320 del conector GPIO. Guía: `docs/RASPBERRY.md`.
  - Kernel ARM propio en `kernel/pi`: arranque desde el firmware (`kernel7.img`), MMU con tablas de dos niveles,
    excepciones, temporizador genérico, UART, buzón del firmware, tarjeta SD (EMMC) y apagado/reinicio. El resto
    del kernel (gestor de ventanas, VFS, FAT, planificador, llamadas al sistema...) es el mismo que en x86_64.
  - Pantalla ILI9486 por SPI (se envían solo las zonas que cambian) y táctil XPT2046 con calibración de tres
    puntos, que se guarda en `ZUNZUN/TACTIL.TXT`. Un toque es un clic.
  - Teclado en pantalla con la distribución española o inglesa: se abre con el botón del teclado de la barra y
    sirve en todas las ventanas.
  - Pantalla pequeña: la pantalla de inicio, el PIN y el menú Inicio caben en 480x320; las ventanas que no caben
    se muestran reducidas. Los grupos del escritorio van uno por línea, con iconos pequeños en filas. El
    Navegador ocupa toda la pantalla y pone su barra en dos filas: botones y menú «Más» (marcadores, historial,
    imprimir) arriba y la dirección debajo; al tocar la dirección sale el teclado en pantalla.
  - USB con un controlador propio para el DWC2 de la Pi: teclado, ratón, memorias, impresoras, hubs y la red de
    la placa (LAN9514), que recibe dirección por DHCP. Probado en la Pi 2 de verdad: red, teclado y ratón USB.
  - El LED verde late mientras todo va bien y parpadea rápido si hay un error fatal; el registro del arranque se
    guarda en `ARRANQUE.TXT` de la tarjeta. `tools/grabar-sd.sh` graba la tarjeta desde el Mac.
  - Todas las aplicaciones menos el Navegador JS y la Consola JS (el motor Colibrí aún no tiene su parte ARM).
    Sin sonido todavía.
  - `make pi` hace `build/pi/kernel7.img`; `make pi-sd` la tarjeta SD. Prueba en QEMU: `tools/pi_test.py`.
- El código de los programas usa sufijos `LL`/`ULL` en las constantes de 64 bits (igual en x86_64).

## 2.6 — 26/11/2026

- Clonar: copia exacta de una unidad entera en otra, sector a sector, para cambiar el disco duro o el SSD de un equipo
  o duplicar una memoria USB. Sirve para cualquier sistema (ZunzúnOS, Windows, Linux) y la copia arranca igual que
  el original.
  - Destino mayor: se copia todo y el espacio de más queda libre al final. Destino menor: solo si todas las
    particiones caben.
  - GPT: la cabecera, su copia de seguridad (que va al final del disco) y el MBR protector se ajustan al tamaño del
    destino.
  - Sectores ilegibles: se reintentan uno a uno y quedan a cero; se avisa de cuántos y dónde.
  - Se puede clonar el sistema en uso: mientras se copia, el origen queda en solo lectura.
  - Comprobación final opcional: se vuelve a leer el destino y se compara con lo escrito.
  - Con Clonar abierto, el equipo no se apaga solo por inactividad.

## 2.5 — 25/11/2026

- Correo: ya se puede enviar con Gmail. Su servidor de envío pide un certificado de cliente opcional y el TLS propio
  se paraba ahí; ahora contesta sin certificado y sigue (TLS 1.3 y 1.2).

## 2.4 — 25/11/2026

- Radio: emisoras de todo el mundo (catálogo abierto Radio Browser): populares, mi país, países, géneros, búsqueda
  y favoritas. Emisiones MP3 por HTTP y HTTPS con el título de lo que suena (metadatos ICY) y redirecciones.
- Traducciones con rutas de Windows (barra invertida) arregladas en inglés.

## 2.3 — 25/11/2026

- Nube: archivos de pCloud (entrar con correo y contraseña; en la memoria solo queda la llave de la sesión).
  Navegar, abrir, guardar en Documentos, subir, nueva carpeta, cambiar nombre y borrar.
- El lector JSON acepta valores ausentes sin fallar.

## 2.2 — 24/11/2026

- Mensajes de la Red local como conversaciones: el primer mensaje decide el grupo (uno o varios equipos) y las
  respuestas van a todo el grupo; diálogo con burbujas y aviso de mensajes sin leer.
- Escritorio con categorías plegables.
- Correo: guardar borradores y carpetas de borradores y enviados.
- Navegador JS: ya no se cierra al pasar de una página con JavaScript a otra. Botón Historial en los navegadores y
  borrar las visitas elegidas.
- Forense: rastreador de desastres de sistemas (Windows, NTFS y registro) con Labs de IA (protocolo zz-forense/1).
- Clics en el texto: 1 clic pone el cursor, 2 eligen la palabra, 3 todo.
- Colibrí, motor JavaScript propio; decodificador MP3 propio; adjuntos en Correo.
- Red local: impresoras compartidas, guardia de red, identidad por equipo, añadir equipos de otra red.
- LUMEN (chat con IA), copiar y pegar entre ventanas, cookies, Sitios y 80 funciones en la Hoja.

## 2.1 — 19/11/2026

- Primera versión grabada en el ThinkPad X220: kernel de 64 bits, escritorio, red, sonido y programas propios.
