# Red local de ZunzúnOS

Intranet entre equipos con ZunzúnOS unidos por cable de red: con un router o switch, o con un solo cable
directo entre dos equipos (no hace falta router: cada equipo se pone solo una dirección local 169.254.x.y).
Desde la versión 2.8 la misma red funciona también **por Internet**, entre casas distintas (ver
[Por Internet](#por-internet)), y los mensajes llevan **archivos adjuntos**.

**Programa:** Inicio → Internet → **Red local**. Funciona en la sesión del administrador de cada equipo.

---

## Montar la red la primera vez

1. Une los equipos con el cable y enciéndelos. En unos 15 segundos el icono de red de la barra pasa a verde
   (sin router verás «cable directo, sin router» al pulsarlo).
2. En el equipo que vaya a mandar (por ejemplo el ThinkPad X220): **Red local → Crear una red nueva** y ponle nombre.
   Ese equipo es el **administrador**.
3. En la lista **Equipos encontrados que no están en la red** aparecen los demás. Pulsa **Invitar a la red**.
4. En el otro equipo (por ejemplo el ThinkPad Edge) aparece la invitación con un **código de 6 cifras**.
   Comprueba que es el mismo que ve el administrador y pulsa **Aceptar**. Si el código no coincide, rechaza:
   alguien se está haciendo pasar por el administrador.

Se pueden invitar tantos equipos como haga falta: la red no está pensada solo para dos.

## Carpetas compartidas

El administrador decide qué se comparte: **Carpetas → Compartir una carpeta**, elige en qué equipo está,
el nombre que verán los demás, la ruta (por ejemplo `/sys/DOCS/Compartida`, se crea sola) y si es de
**solo lectura** o de **lectura y escritura**.

En cualquier equipo de la red, **Carpetas** muestra las carpetas compartidas (punto verde = su equipo está
encendido). Dentro de una carpeta:

- pulsa una carpeta para entrar; **Subir de nivel** para volver;
- pulsa un archivo para **Abrir** (se abre con su programa), **Guardar en Documentos** (va a `Documentos/Red`),
  **Cambiar nombre** o **Borrar** (estas dos solo si la carpeta es de lectura y escritura);
- **Subir un archivo...** copia un archivo de este equipo a la carpeta; **Nueva carpeta** crea una.

## Impresoras compartidas

La impresora se configura en el equipo al que está conectada por USB (o por red), con **Impresoras**, como siempre.
Después, el administrador va a **Red local → Impresoras → Compartir una impresora**, elige ese equipo y la impresora.

En los demás equipos aparece en el diálogo **Imprimir** de todos los programas como, por ejemplo,
«Samsung ML-1640 (en Equipo-X220)». Si el equipo no tiene impresoras propias, viene ya elegida.

Cada equipo prepara el trabajo en el idioma de la impresora (QPDL para la Samsung ML-1640, PCL, PostScript...) y lo
manda cifrado al equipo que la tiene, que lo pasa a la impresora y lo muestra en su cola de la barra de tareas.
El equipo con la impresora tiene que estar encendido y con la sesión del administrador abierta.

## Mensajes

**Mensajes**: escribe y pulsa Intro o **Enviar a...** para mandarlo a un equipo o a todos. Al que lo recibe le
aparece un aviso en la esquina de la pantalla y el mensaje queda en la lista.

### Archivos adjuntos

- **Adjuntar...** (a la izquierda del campo de escribir) abre el selector de archivos; se pueden poner hasta
  **10 adjuntos** por mensaje, de **64 MB** como mucho cada uno (256 MB en total). Salen encima del campo; la **x**
  quita uno. Se puede mandar un mensaje solo con adjuntos.
- Mientras se envían se ve una **barra de progreso** abajo («Enviando foto.jpg a Casa-B: 45 %»).
- El que lo recibe los tiene en **Documentos/Red/Adjuntos** (si ya hay uno con ese nombre se guarda como
  «foto (2).jpg»). En la conversación aparecen debajo del texto: al pulsarlos se **abren con su programa**.
- Cada adjunto viaja en trozos de 64 KB con su SHA-256: el que recibe guarda lo que va llegando y, si la conexión
  se corta, la siguiente vez el envío **sigue por donde se quedó** (no empieza de cero). Al final se comprueba el
  SHA-256; si no cuadra, se borra y se vuelve a enviar.
- Si un equipo está apagado o no se le puede llamar, el mensaje queda **pendiente** («entregado 1/2 · el resto,
  pendiente») y el servicio lo reintenta solo cuando vuelve a estar en línea (o cuando él se conecta, si está
  detrás de un CGNAT sin relé: ver más abajo). Funciona igual en la red local que por Internet.
- Un equipo con la versión anterior recibe el texto (sin los adjuntos y con una nota).

## Por Internet

La misma red sirve para equipos en **casas distintas**: carpetas compartidas, impresoras compartidas y mensajes
con adjuntos funcionan igual que en la red local, cifrados de extremo a extremo con la clave de la red. No hace
falta ninguna cuenta ni ningún servicio en la nube: los equipos se hablan directamente y, solo si no hay otra
forma, a través de un **relé** que pone el propio usuario (un VPS o una Raspberry Pi) y que no puede leer nada.

Todo está en la pestaña **Internet** de Red local. El acceso por Internet está **desactivado** hasta que se pulsa
**Activar el acceso por Internet** en cada equipo; desactivado, el equipo cierra el puerto del router, no atiende
nada que llegue de Internet y solo usa la red local.

### Conectar dos ThinkPad en casas distintas

En casa 1 (el ThinkPad X220, el **administrador**):

1. Red local → **Equipos → Crear una red nueva** (si no la tienes ya).
2. Pestaña **Internet → Activar el acceso por Internet**. En unos segundos debe salir «Puerto abierto en el router
   con UPnP (o NAT-PMP, o PCP): 83.45.12.7:7141». Si sale otra cosa, mira [Si el router no abre el puerto](#si-el-router-no-abre-el-puerto).
   La primera vez, la **guardia de red** pregunta si Red local puede salir a Internet: contesta **Permitir siempre**.
3. **Crear un código de invitación**. Sale algo así como
   `ZRED1-K7Q2M9XAHT4PW3ZB@83.45.12.7:7141` (ya copiado al portapapeles). Pásaselo a la otra casa por
   teléfono, correo o mensajería. Sirve **una sola vez** y **caduca en 2 días**.

En casa 2 (el ThinkPad Edge):

4. Red local → pestaña **Internet → Unirse con un código de invitación...** y escribe (o pega) el código.
5. Las **dos pantallas** muestran un **código de 6 cifras**. Por teléfono, comprobad que es el mismo y pulsad
   **Aceptar** en los dos equipos. Si no coincide, **Rechazar**: alguien está en medio.
6. Listo: el Edge ya es de la red. Su acceso por Internet se activa solo y abre también su puerto si su router lo
   permite. En **Equipos** y en **Internet** se ve cada equipo con su punto verde y por dónde se llega a él
   («por Internet (83.45.12.7:7141)», «por Internet, a través del relé», «por la red local»).

Desde ese momento, el administrador comparte carpetas e impresoras de cualquiera de los dos equipos como siempre, y
los mensajes van y vienen. Se pueden mezclar equipos de la red local y de Internet en la misma red.

### Cómo se encuentran los equipos

Los routers de casa hacen **NAT**: desde fuera solo se ve una dirección pública, y para que otro equipo pueda
llamar al tuyo el router tiene que reenviarle el puerto TCP **7141**. ZunzúnOS lo intenta todo, en este orden:

| Paso | Qué hace |
|---|---|
| **PCP** (RFC 6887) | pide al router (UDP 5351) que abra el puerto 7141 una hora y lo renueva cada 20 minutos |
| **UPnP IGD** | busca el router por SSDP (multidifusión 239.255.255.250:1900 y directo a la puerta de enlace), lee su descripción y pide `AddPortMapping` por SOAP; si el puerto está cogido (error 718) prueba el siguiente, y si el router solo admite reenvíos permanentes (725), lo pide permanente |
| **NAT-PMP** (RFC 6886) | el protocolo de los routers de Apple y de muchos otros; también sirve de último intento |
| **Dirección pública** | la que dice el router; si no la hay, la que ve el relé (`QUIENSOY`) o, solo si se configura, un servidor STUN |
| **A mano** | **Direcciones a mano...**: IP o nombres DNS con su puerto, separados por comas (`casa.duckdns.org:7141`, `83.45.12.7:7141`); `*:7141` significa «la IP pública que se vea en cada momento» (para un reenvío hecho a mano en un router con IP dinámica) |
| **Relé** | si nada de lo anterior sirve (ver abajo) |

Cada equipo **anuncia** sus direcciones a los demás con un **número de secuencia** que sube cada vez que cambian;
gana la secuencia más alta venga de quien venga, así que los equipos también se pasan las direcciones de los demás
(si el X220 cambió de IP y solo pudo avisar al administrador, los demás se enteran por él). El servicio comprueba el
router cada 20 minutos, cuando cambia la dirección local (otro DHCP, otro cable) y al pulsar **Comprobar ahora**, y
cada minuto, más o menos, comprueba que los equipos de Internet siguen ahí (de ahí el punto verde, y de paso intercambian
direcciones y mensajes pendientes). Con IP dinámica sin nombre DNS, el reencuentro funciona mientras al menos uno de
los dos conserve una dirección que el otro conozca, o haya relé; con un nombre DNS dinámico (DuckDNS, No-IP...)
escrito en **Direcciones a mano** funciona siempre.

Para conectar con otro equipo se prueba, por este orden: la red local (si se le oye por difusión), la última ruta
que funcionó, cada una de sus direcciones de Internet y, por último, el relé.

### Si el router no abre el puerto

El estado de la pestaña Internet lo dice:

- **«El router no deja abrir el puerto solo»**: UPnP, NAT-PMP y PCP están apagados en el router. Actívalo en la
  configuración del router (suele llamarse «UPnP») o reenvía a mano el puerto **TCP 7141** a la dirección local del
  equipo (se ve en la pestaña Equipos) y escribe en **Direcciones a mano** `*:7141` (o tu nombre DNS).
- **«El router no tiene una IP pública (CGNAT)»**: pasa con el anclaje del móvil y con algunas compañías. Desde
  fuera **no se puede** llegar a ese equipo; él sí puede llamar a los demás. Sin relé, sus mensajes salen bien y los
  que le mandan los recoge él solo cada vez que se conecta con el que los guardaba; pero sus carpetas e impresoras
  no se pueden usar desde fuera. Con un **relé**, todo funciona.
- **«Hay dos routers seguidos (doble NAT)»**: el router de la compañía y otro detrás. Abre el puerto en los dos o
  usa un relé.

Basta con que **uno** de los dos equipos se pueda alcanzar para que se unan y se manden mensajes; para usar las
carpetas o la impresora de un equipo, hay que poder llegar a **ese** equipo (directo o por el relé).

### El relé

Un programa pequeño, `tools/zred-rele.py` (Python 3, sin nada más), para cualquier máquina con una dirección
accesible desde Internet: un VPS, o una Raspberry Pi en una casa con el puerto abierto.

```
python3 zred-rele.py --puerto 7142                 # abierto a cualquiera
python3 zred-rele.py --puerto 7142 --clave SECRETO # solo quien traiga la clave
```

El administrador lo pone en **Internet → Relé de la red...** como `vps.ejemplo.org:7142` (o
`vps.ejemplo.org:7142/SECRETO`), y llega a todos los equipos con la configuración de la red. Desde entonces cada equipo
mantiene dos conexiones esperando en su **buzón** del relé, y los demás le llaman por ahí cuando no pueden directamente.
El código de invitación también lleva el relé, así que un equipo con CGNAT puede invitar o unirse a través de él.

Qué ve el relé y qué no:

- **No ve nada de lo que se manda**: pasa los bytes tal cual; dentro va la misma autenticación reto/respuesta y el
  mismo cifrado AES-GCM que en la red local, con la clave de la red, que el relé no tiene. No puede leer, cambiar
  (AES-GCM lo detecta) ni hacerse pasar por ningún equipo.
- **No sabe de qué red es nadie**: el buzón de cada equipo es un HMAC de la clave de la red y del identificador del
  equipo; para el relé son nombres sin sentido. El de una invitación sale del secreto del código.
- **Sí ve** las IP públicas que se conectan, cuándo y cuántos bytes. Y puede negarse a pasar el tráfico (dejaría de
  funcionar, nada más). Por eso lo pone el usuario y no hay ninguno por omisión.

Límites del relé: 4 esperas por buzón, 300 conexiones, 30 por IP, 10 s para la primera línea y 10 minutos sin
tráfico en una conexión unida.

### Seguridad por Internet

Lo que cambia al abrir un puerto a Internet, y cómo se ha resuelto:

- **Nada sin autenticar**: por Internet solo se atienden dos cosas: el saludo cifrado de un equipo de la red
  (`HOLA2`) y la llegada de un equipo con un código de invitación (`UNIRSE`). La invitación de la red local (la que
  abre una ventana en el otro equipo) y el saludo de la versión anterior solo se aceptan desde la red local: así
  nadie de fuera puede llenar la pantalla de ventanas. Una conexión es de la red local si viene de la misma subred
  (sin contar el router, por el que entra lo que llega del puerto abierto) o de una dirección 169.254.
- **Sin pistas**: el saludo por Internet no dice en claro ni la red ni el equipo; manda una etiqueta
  HMAC(clave de red, aleatorio ‖ equipo) que el servicio compara con la de cada equipo de la red. Quien no tenga la
  clave no puede averiguar de qué red es un equipo ni probar identificadores.
- **Repeticiones**: cada conexión usa dos números aleatorios nuevos (uno de cada lado) para el reto y para la clave
  de sesión; repetir una conexión grabada no sirve. Dentro de la conexión, cada trozo cifrado lleva su número de
  orden y su sentido en el IV de AES-GCM, así que no se pueden repetir, quitar ni reordenar trozos. Las invitaciones
  son de un solo uso y caducan en 2 días.
- **Fuerza bruta**: la clave de red es de 256 bits y el secreto de las invitaciones de 80 bits (y nunca viaja: los dos
  lados demuestran que lo saben con HMAC, ligado a sus claves X25519 de ese momento). Además, una dirección que falla
  la autenticación 6 veces en 10 minutos queda **bloqueada 10 minutos**, y se atienden como mucho 40 saludos por
  minuto desde Internet. El código de 6 cifras sigue comprobándose en las dos pantallas: aunque alguien robara el
  código de invitación por el camino, el administrador vería un equipo que nadie le ha confirmado.
- **Denegación de servicio**: cada saludo tiene 8 segundos en total (contra el goteo de bytes); el núcleo admite
  como mucho 16 conexiones a medio abrir por puerto y libera las que no terminan (contra la inundación de SYN);
  cada trozo cifrado es de 2 MB como mucho, un adjunto de 64 MB y un mensaje de 10 adjuntos. Una inundación de
  tráfico de verdad no se puede parar desde un equipo de casa: en ese caso, desactivar el acceso por Internet.
- **Rutas**: las carpetas compartidas siguen rechazando las rutas con `..`; los nombres de los adjuntos se quedan sin
  carpetas, sin `..` ni caracteres de control antes de guardarlos, y nunca pisan un archivo que ya exista.
- **Interruptor**: el acceso por Internet es de cada equipo y se activa y desactiva en la pestaña Internet.
  Desactivado, el puerto del router se cierra (`DeletePortMapping` o PCP/NAT-PMP con vida 0).

## Administrar

- **Sacar de la red** (en Equipos): el equipo deja de estar en la red y se cambia la clave de la red para los
  demás. Si algún equipo estaba apagado en ese momento, necesitará otra invitación.
- **Borrar la red**: la red deja de existir para todos.
- En un terminal, **Salir de la red** lo saca solo de ese equipo.
- **Cambiar nombre** (antes de unirse a una red) cambia cómo se ve el equipo en la red.

## Seguridad

- La red tiene una clave secreta de 32 bytes que solo conocen sus equipos. Se entrega al aceptar la invitación,
  cifrada con una clave acordada por X25519 en ese momento; el código de 6 cifras sale de esa clave, por eso
  tiene que coincidir en los dos equipos.
- Cada conexión entre equipos se autentica con reto y respuesta (HMAC-SHA256) y va cifrada (AES-GCM) con una
  clave distinta para cada conexión.
- Solo se puede entrar en las carpetas que el administrador ha compartido; las rutas con `..` se rechazan.
- Por Internet, ver [Seguridad por Internet](#seguridad-por-internet).

## Detalles técnicos

| | |
|---|---|
| Descubrimiento | UDP 7140, difusión: cada equipo se anuncia cada 15 s y responde a búsquedas |
| Peticiones | TCP 7141 (el mismo puerto se abre en el router para Internet) |
| Relé | TCP 7142 por omisión (`tools/zred-rele.py`) |
| Direcciones | `/sys/ZUNZUN/RED-DIR-<MAC>.TXT` (id\|secuencia\|direcciones de cada equipo) |
| Invitaciones por Internet | `/sys/ZUNZUN/RED-INVITA-<MAC>.TXT` (secretos pendientes y su caducidad, solo en el administrador) |
| Mensajes pendientes | `/sys/ZUNZUN/RED-SALIDA/` (uno por mensaje y destinatario; el servicio los reintenta) |
| Adjuntos | `Documentos/Red/Adjuntos` (a medio llegar, en `Adjuntos/.parcial`) |
| Estado de Internet | `/rd/tmp/red-internet.txt` (lo escribe el servicio y lo lee la ventana) |
| Configuración | `/sys/ZUNZUN/RED-<MAC>.TXT` (red, clave, equipos, carpetas, impresoras y relé; la reparte el administrador con número de versión) |
| Identidad | `/sys/ZUNZUN/RED-ID-<MAC>.TXT` (identificador y nombre del equipo, acceso por Internet, direcciones a mano y STUN opcional) |
| Mensajes | `/sys/ZUNZUN/RED-MENSAJES.TXT` |
| Impresión | los programas dejan el trabajo en `/rd/tmp/RED-IMPRIMIR` (impresora `red://EQUIPO/ID`) y el servicio lo entrega |
| Servicio | `redlocal --servicio`, lo arranca el escritorio al entrar el administrador |

### Protocolo por Internet

- Saludo: `ZRED1|HOLA2|nc|etiqueta` → `RETO|ns` → `PRUEBA|HMAC(clave, nc‖ns‖c)` → `BIEN|HMAC(clave, nc‖ns‖s)`; clave de
  sesión HKDF(nc‖ns, clave de red). Después, peticiones cifradas: las de siempre más `PING|secuencia|direcciones`
  (devuelve `OK|mensajes pendientes` y la libreta de direcciones), `RECOGE` (el otro manda sus mensajes pendientes para
  este equipo) y `MENSAJE2` (mensaje con adjuntos, se puede mandar en los dos sentidos de una conexión).
- Invitación: código `ZRED1-S@direcciones[,rele:host:puerto]`, S de 80 bits en base 32.
  `ZRED1|UNIRSE|SHA256("zred1 inv"‖S)[:8]|pubB|idB|nombreB|HMAC(S, b‖pubB‖idB|nombreB)` →
  `PUB|pubA|idA|nombreA|red|HMAC(S, a‖pubA‖pubB‖nombreA|red)`; código de 6 cifras de SHA-256(X25519‖pubA‖pubB);
  `ACEPTO` y la configuración cifrada con AES-GCM y la clave HKDF(S, X25519).
- Rele: `ZRELE1|ESPERA|buzón|clave`, `ZRELE1|CONECTA|buzón|clave` (`UNIDO`/`NADIE`), `ZRELE1|QUIENSOY|-|clave`;
  buzón = HMAC(clave de red, "zred1 buzon id")[:12] en hexadecimal.

Pruebas: `tools/red_test.py` arranca dos equipos en QEMU unidos por un cable virtual (sin router ni DHCP) y
recorre todo: crear la red, invitar, compartir, subir, descargar, mensaje, imprimir desde B en la impresora de A y
sacar de la red. Con `router`, A tiene la dirección de un router y B una 169.254, y A cambia de cable.

`tools/red_internet_test.py` (en Linux, como root) arranca **tres equipos en casas distintas**: cada uno en QEMU con
red de usuario en su propia subred, detrás de un NAT; el anfitrión hace de Internet con IP públicas 198.51.100.x.
`tools/fake_casas.py` hace de routers de verdad: A con UPnP, B con NAT-PMP y C con CGNAT (PCP le da una 100.64.x.x);
lo que abren lo abren en QEMU con `hostfwd_add`. El relé es `tools/zred-rele.py`. Recorre: abrir el puerto por UPnP y
NAT-PMP, detectar el CGNAT, poner el relé, invitar por Internet con código a B (directo) y a C (CGNAT), usar desde A
la carpeta de C por el relé (subir, listar, descargar), imprimir desde B en la impresora de A, mensaje a todos con
dos adjuntos (comprobados byte a byte en los discos), cambiar la IP pública de B (anuncio y reencuentro), un adjunto
de 6 MB cortado a mitad quitando el cable a B 40 s (queda pendiente y se reanuda desde donde se quedó), sondas de un
atacante contra el puerto abierto (invitación de red local, saludo antiguo, etiqueta falsa, invitación inventada,
goteo y bloqueo) y desactivar Internet (el router cierra el puerto). Lo que **no** se ha probado: Internet de verdad,
routers reales, SSDP por multidifusión real y STUN contra un servidor real.
