# Estudio de KolibriOS y decisiones para ZunzúnOS

## Cómo funciona KolibriOS

| Área | KolibriOS |
|---|---|
| Origen | Fork de MenuetOS (2004), licencia GPL-2.0 |
| Lenguaje | Kernel y drivers en ensamblador FASM |
| CPU / modo | x86 de 32 bits, modo protegido, i586 o superior |
| Kernel | Monolítico, multitarea apropiativa, hilos |
| Requisitos | Unos 12 MB de RAM, tarjeta VESA, pocos MB de disco |
| Arranque | Disquete, disco, USB, CD, GRUB, Coreboot y UEFI (`uefi4kos`) |
| Sistema inicial | `kolibri.img`: un disco RAM con el sistema y los programas |
| Programas | Binarios planos con cabecera `MENUET01` (entrada, tamaño, memoria, pila, parámetros) |
| Llamadas | `int 0x40`, función en `eax`, argumentos en `ebx ecx edx esi edi` |
| Modelo de app | Bucle de eventos: esperar (función 10), redibujar, tecla o botón |
| Gráficos | Ventanas, botones y texto dentro del kernel, sobre framebuffer VESA |
| Archivos | FAT12/16/32 completo; NTFS, exFAT, ISO9660 y ext2/3/4 según versión |
| USB | UHCI, OHCI, EHCI, hubs, teclado, ratón y memorias |
| Discos | PATA, SATA y AHCI |
| Red | Pila TCP/IP propia y varios drivers Ethernet |
| Sonido | AC'97 e Intel HD Audio |
| Aplicaciones | Más de 250: editor, visor, navegador, ofimática, más de 30 juegos, FASM |

Árbol del kernel (`kernel/trunk`): `boot`, `bootloader`, `core`, `gui`, `fs`, `blkdev`,
`bus`, `hid`, `network`, `sound`, `video`, `acpi`, `detect`, `posix`.

## Qué conserva ZunzúnOS

- Kernel monolítico pequeño con multitarea apropiativa.
- Disco RAM de arranque con el sistema y los programas.
- Programas como binarios planos con cabecera propia (`ZUNAPP01`).
- Llamadas al sistema por `int 0x40` y aplicaciones con bucle de eventos.
- Gestor de ventanas dentro del kernel y arranque a escritorio en segundos.

## Qué cambia para mejorarlo

| Tema | KolibriOS | ZunzúnOS |
|---|---|---|
| Arquitectura | 32 bits | 64 bits, más de 4 GB de RAM |
| Lenguaje | Ensamblador | C y algo de ensamblador |
| Protección | Anillo 3 | Anillo 3, un espacio de direcciones por proceso |
| Memoria de apps | Reservada al cargar | Bajo demanda (pila y heap) |
| Dibujo | Llamadas al kernel | Lienzo propio mapeado en cada proceso, sin parpadeo |
| USB | Hasta USB 2.0 | xHCI (USB 3), necesario en PCs modernos |
| Almacenamiento | PATA/SATA | IDE, AHCI, NVMe y memorias USB |

## Fuentes

- https://en.wikipedia.org/wiki/KolibriOS
- https://kolibrios.org/en
- https://wiki.kolibrios.org/wiki/Writing_applications_for_KolibriOS/en
- https://git.kolibrios.org/KolibriOS/kolibrios/src/branch/main/kernel/trunk
- https://docs.kolibrios.org/html/eng/INSTALL.HTML
