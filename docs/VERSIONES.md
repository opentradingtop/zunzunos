# Versiones de ZunzúnOS

La cifra está en `user/lib/version.h` y sube con cada actualización que se graba en la memoria USB o se publica.
Se ve en Acerca de y en la primera línea del registro de arranque (y por tanto en `DIAGNOST.TXT`).

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
