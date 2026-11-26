# ZunzúnOS en la Raspberry Pi 2

ZunzúnOS funciona en la **Raspberry Pi 2 Model B** (v1.1, BCM2836: cuatro Cortex-A7, ARMv7-A de 32 bits, 1 GB) con
la pantalla táctil de 3,5" y 480x320 que se enchufa en el conector GPIO («3.5inch RPi LCD», HUGA y clones de la
Waveshare (A)). El kernel y los programas son los mismos que en el PC; lo propio de la Pi está en `kernel/pi`.

## La pantalla de 3,5"

| Patilla | GPIO | Señal | Para qué |
|---|---|---|---|
| 11 | GPIO17 | TP_IRQ | el táctil la pone a 0 mientras se toca |
| 18 | GPIO24 | LCD_RS | orden o dato del ILI9486 |
| 19 | GPIO10 | MOSI | SPI0 |
| 21 | GPIO9 | MISO | SPI0 (lo usa el táctil) |
| 22 | GPIO25 | RST | reinicio del ILI9486 |
| 23 | GPIO11 | SCLK | SPI0 |
| 24 | GPIO8 | CE0 | pantalla |
| 26 | GPIO7 | CE1 | táctil XPT2046 |

Es la tabla de Waveshare para su pantalla (A). Si la tuya va cableada de otra forma, cambia los números en
`kernel/pi/pantalla.h` y vuelve a hacer `make pi`. Ahí están también la velocidad del SPI (16 MHz para la pantalla,
2 MHz para el táctil) y la orientación (`LCD_MADCTL`).

- La pantalla recibe la imagen por SPI en RGB565. El gestor de ventanas compone en memoria y un hilo envía solo las
  zonas que cambian (una pantalla entera tarda unos 0,2 s).
- El táctil: la primera vez se piden tres toques sobre unas cruces. La calibración queda en `ZUNZUN/TACTIL.TXT`;
  si se borra, se vuelve a pedir.
- Imagen girada: crea `ZUNZUN/PANTALLA.TXT` con `madctl=0xE8` (180 grados); en espejo, prueba `0x68` o `0xA8`.
- Teclado: el botón del teclado de la barra abre el teclado en pantalla (español o inglés, según el idioma del
  teclado en Configuración). Mayúsculas: una pulsación para una letra, dos para dejarlas fijas.
- Sin la pantalla de 3,5" (no responde su táctil) se usa solo la salida del firmware, por HDMI.

## USB y red

Los cuatro puertos USB y el conector de red de la Pi 2 cuelgan del **LAN9514** de SMSC/Microchip: un hub USB 2.0
con una tarjeta de red 10/100 (de la familia LAN9512) en su puerto 1. El hub va al único puerto del controlador USB
del BCM2836, un Synopsys DesignWare USB 2.0 OTG («DWC2»), en `0x3F980000`. El firmware lo enciende cuando se le pide
por el buzón (dominio de energía 3).

- `kernel/pi/dwc.c`: el controlador en modo anfitrión, por sondeo y con DMA. Cada canal tiene un búfer intermedio de
  64 KB sin caché (el USB ve la RAM por el alias `0xC0000000` del bus de la GPU). Hace la enumeración, los hubs, las
  transacciones divididas (split) para teclados y ratones de baja/plena velocidad detrás de un hub de alta velocidad,
  HID (teclado, ratón, tableta y mandos con `kernel/hid.c`), memorias USB, impresoras (`kernel/usbprn.c`) y red.
- La red va por `kernel/usbnet.c`, el mismo código que en el PC (CDC-ECM, RNDIS, iPhone) más el **LAN95xx**
  (`0424:ec00`): sus registros por peticiones de fabricante, la PHY interna por MII, tramas con dos palabras de
  orden delante al enviar y una de estado delante al recibir. Se llama `eth0` y su dirección MAC es la del número
  de serie de la placa (`kernel/pi/placa.c`); una tarjeta de red USB enchufada no cambia la identidad del equipo.
- El registro cuenta lo que pasa con líneas `dwc:` y `usb-red:`.

### Probado y por probar

En la **Pi 2 de verdad** (26/11/2026): la red de la placa (LAN9514) recibe dirección por DHCP, y un teclado y un ratón
USB funcionan a través del hub (transacciones divididas). Al principio el USB no arrancaba porque el firmware no
contestaba a la orden de encenderlo: la petición llevaba el código de petición a 8 en vez de 0 (QEMU lo aceptaba).

En QEMU (`tools/pi_test.py DIR --usb`): un hub de plena velocidad, el teclado USB que escribe en el Editor y la red
USB (RNDIS) que recibe dirección por DHCP. A mano también el ratón, la tableta, una memoria USB (se monta en `/usb0`)
y quitar y volver a poner el teclado en marcha (`device_del`/`device_add` en el monitor). Quitar la red USB de QEMU
en marcha hace saltar un `assert` del propio QEMU (la recepción queda pendiente en el canal), no del sistema.

Falta comprobar en la Pi: una memoria USB, una impresora y desenchufar y volver a enchufar cada cosa en marcha.
Si algo falla, `ARRANQUE.TXT` de la tarjeta trae las líneas `dwc:`.

## Construir

```
make pi        # build/pi/kernel7.img: kernel ARM con el disco en memoria (programas y recursos) dentro
make pi-sd     # build/pi/sd.img: tarjeta SD (necesita el firmware, abajo); SD_BYTES=... para otro tamaño
make pi-run    # en QEMU (-M raspi2b); con USB: tools/pi_test.py DIR --usb (teclado y red USB)
```

`kernel7.img` lleva el kernel, ceros hasta el final de su BSS y el initrd con su cabecera `ZZINITRD`
(`tools/mkpi.py`). El firmware de la Pi lo carga entero en la dirección 0x8000.

### El firmware

¿Por qué no uno propio? En la Pi no arranca primero el ARM sino la GPU (VideoCore IV): la ROM del chip, de Broadcom e
imposible de cambiar, lee `bootcode.bin` de la tarjeta, que carga `start.elf`, que pone en marcha la RAM y despierta al
ARM con `kernel7.img`. Hasta ahí los núcleos ARM están parados. Un firmware propio sería programar la VideoCore, sin
documentación oficial (registros de la DRAM incluidos) ni forma de probarla en QEMU, y la ROM seguiría debajo. Es como
la BIOS del PC: parte del aparato, no del sistema. ZunzúnOS usa lo mínimo (ni árbol de dispositivos ni nada más suyo)
y no lo lleva dentro.

La Pi no arranca sin tres archivos del firmware de Raspberry Pi Ltd: `bootcode.bin`, `start.elf` y `fixup.dat`
(carpeta `boot` de https://github.com/raspberrypi/firmware). No son de ZunzúnOS: no se suben al repositorio, se
descargan aparte a `build/pi/firmware/` (junto con `LICENCE.broadcom`) y `make pi-sd` los copia a la tarjeta.

### La tarjeta SD

`tools/mkpi.py sd` hace una tabla MBR con una partición FAT32 (tipo 0x0C) desde el sector 8192: firmware,
`config.txt` (en `tools/pi/config.txt`), `kernel7.img` y la carpeta `ZUNZUN`. El kernel monta esa partición como
`/sys`: ahí quedan los ajustes, el escritorio, los documentos y `DIAGNOST.TXT`.

Para grabarla desde el Mac: un lector de tarjetas, comprobar con `diskutil list` cuál es la tarjeta (tamaño y
nombre), `diskutil unmountDisk` y `sudo dd if=build/pi/sd.img of=/dev/rdiskN bs=4m`. Ojo con el número: `dd`
borra lo que haya.

## Lo que hay que saber del hardware

- El micro-USB de la Pi 2 **solo da corriente**: no hay modo de datos. Para probar, la tarjeta va y viene entre la
  Pi y el Mac.
- LED verde (ACT): se apaga en cuanto empieza el kernel; luego parpadea despacio (un destello por segundo) mientras
  todo va bien y deprisa si hay un error fatal, cuyo mensaje sale en la pantalla. Encendido fijo: el kernel no llegó
  a empezar (o se paró en el primer instante).
- Registro: el kernel escribe `ARRANQUE.TXT` en la tarjeta en cuanto la monta (y lo renueva), y el escritorio
  `DIAGNOST.TXT`. Con un cable USB-TTL de 3,3 V en GPIO14 (TX) y
  GPIO15 (RX), a 115200 baudios, se ve el registro en directo; por ese mismo cable se pueden mandar toques
  (`!T x y`, `!U x y`) y teclas (`!K uso 1`), que es lo que hacen las pruebas de QEMU.
- La Pi no tiene reloj con pila: la hora sale de la fecha de compilación y de la última guardada
  (`ZUNZUN/RELOJ.TXT`, al cambiarla en Configuración).
- «Apagar» deja el firmware parado: ya se puede desenchufar.

## Cómo está hecho

- `kernel/pi/start.S`: arranque. Aparca los núcleos 1-3, baja del modo HYP al SVC, enciende la MMU (secciones de 1 MB)
  y pasa a 0x80000000. Vectores de excepción y cambio de contexto (r4-r11, lr; la FPU aparte).
- `kernel/pi/mm.c`: tablas de dos niveles. Cada proceso tiene su TTBR0 (0-2 GB); el kernel, TTBR1 (RAM entera en
  0x80000000, ioremap en 0xC0000000, periféricos en 0xF2000000).
- `kernel/pi/cpu.c`: excepciones (las páginas del montículo y de la pila se crean al tocarlas, como en x86),
  temporizador genérico a 1000 Hz por el controlador local del BCM2836, hilos y reloj.
- `kernel/pi/placa.c`: UART PL011, GPIO, buzón del firmware (memoria y framebuffer), apagado y `pi_main`.
- `kernel/sdhci.c`: el mismo controlador SD que el PC; en la Pi, accesos de 32 bits y GPIO48-53 en ALT3.
- `kernel/pi/lcd.c`, `kernel/pi/tactil.c`: SPI0, ILI9486 y XPT2046.
- `kernel/pi/dwc.c`: USB (arriba, «USB y red»).
- Llamadas al sistema: `svc #0` con el número en r7 y los argumentos en r0-r4; el resultado vuelve en r0:r1.
- Programas: `-mfloat-abi=hard` con NEON. En la Pi `long` mide 64 bits en los programas (como en x86_64, ver
  `user/lib/z.h`); las funciones que pide el compilador (división de 64 bits, conversiones) son propias, en
  `user/lib/arm`, igual que las matemáticas (`mates.c`, series en doble precisión).

## Pendiente

- Navegador JS y Consola JS: Colibrí necesita su parte ARM (matemáticas en doble precisión en vez de x87 y la
  recogida de basura con punteros de 32 bits).
- USB: lo que falta probar en la Pi (arriba, «Probado y por probar»). Sin isócronas (micrófonos y tarjetas de
  sonido USB).
- Sonido (PWM por la salida de auriculares) y el generador de números aleatorios del BCM2835.
- Los cuatro núcleos (ahora solo trabaja el primero).
