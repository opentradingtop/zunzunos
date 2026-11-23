# Red local de ZunzúnOS

Intranet entre equipos con ZunzúnOS unidos por cable de red: con un router o switch, o con un solo cable
directo entre dos equipos (no hace falta router: cada equipo se pone solo una dirección local 169.254.x.y).

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

## Mensajes

**Mensajes**: escribe y pulsa Intro o **Enviar a...** para mandarlo a un equipo o a todos. Al que lo recibe le
aparece un aviso en la esquina de la pantalla y el mensaje queda en la lista.

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

## Detalles técnicos

| | |
|---|---|
| Descubrimiento | UDP 7140, difusión: cada equipo se anuncia cada 15 s y responde a búsquedas |
| Peticiones | TCP 7141 |
| Configuración | `/sys/ZUNZUN/RED.TXT` (red, clave, equipos y carpetas; la reparte el administrador con número de versión) |
| Identidad | `/sys/ZUNZUN/RED-ID.TXT` (identificador y nombre del equipo) |
| Mensajes | `/sys/ZUNZUN/RED-MENSAJES.TXT` |
| Servicio | `redlocal --servicio`, lo arranca el escritorio al entrar el administrador |

Prueba: `tools/red_test.py` arranca dos equipos en QEMU unidos por un cable virtual (sin router ni DHCP) y
recorre todo: crear la red, invitar, compartir, subir, descargar, mensaje y sacar de la red.
