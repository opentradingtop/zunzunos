# LUMEN — Chat IA para ZunzúnOS

Versión nativa de [LUMEN](https://tonyfluxa.top), el cliente de chat con los modelos de
[OpenRouter](https://openrouter.ai), escrita en C para ZunzúnOS. El navegador de ZunzúnOS no ejecuta
JavaScript, así que LUMEN no es una página web: es un programa del sistema (`user/apps/lumen.c`) que
habla directamente con la API por HTTPS (TLS 1.3/1.2 con certificados verificados), usando el
cliente HTTP del propio sistema.

**Autor:** [tonyfluxa.top](https://tonyfluxa.top)

---

## Inicio rápido

1. Conecta el equipo a la red (Ethernet o anclaje USB del móvil) y comprueba que la **fecha y hora**
   son correctas (Configuración → Fecha y hora): sin ellas no se pueden verificar los certificados.
2. Crea una API key gratuita en <https://openrouter.ai/keys> (empieza por `sk-or-v1-`).
3. Abre **Inicio → Internet → LUMEN** y pulsa **Añadir API key**.
4. Pega la clave (botón **Pegar** o `Ctrl+V`) y pulsa **Guardar y comprobar**.
5. Escribe abajo y pulsa `Intro`.

La clave se guarda **solo en esta memoria** (`/sys/ZUNZUN/LUMEN.TXT`) y las peticiones van directamente
a `https://openrouter.ai/api/v1`. No hay intermediarios ni telemetría.

---

## Estado de la clave

| Estado | Significado |
|---|---|
| **Clave válida** | OpenRouter la acepta (se comprueba con `/api/v1/key` y, si ese punto falla, con una petición real de 1 token). Se muestra lo gastado y el límite. |
| **Clave no válida** | OpenRouter la rechaza (401/403). Se ve el código HTTP y el mensaje del servidor. |
| **Sin verificar** | No se pudo contactar con el servidor (red, DNS, certificado o fecha del equipo). **No** significa que la clave esté mal. |

La clave se limpia al guardarla (espacios, comillas, caracteres invisibles o un `Bearer` delante).

---

## Modelos

- **Modelo ▼** abre la lista: pestañas **Gratis** y **Todos**, búsqueda por nombre o identificador,
  precio por millón de tokens y tamaño del contexto. Se descarga de `/api/v1/models` y se guarda en
  `/sys/ZUNZUN/LUMEN-MODELOS.TXT` (se renueva cada pocas horas o con **Actualizar lista**).
- El modelo por defecto es **Free Models Router** (`openrouter/free`), que elige un modelo gratuito
  disponible. Cada respuesta muestra el modelo que contestó de verdad.
- Sin conexión se muestra una lista básica para poder elegir igualmente.

---

## Funciones

**Chat**
- Respuestas en vivo (SSE), con **Detener** (`Esc`) y **Reintentar** (`Ctrl+R`).
- Markdown: títulos, negrita, código en línea, listas, citas, enlaces, separadores, **tablas con
  bordes** y bloques de código con **Copiar**.
- Bloque plegable de **razonamiento** (DeepSeek R1 y similares).
- Botón derecho en un mensaje: copiar, copiar el código, editar y reenviar, regenerar, borrar.
- Tokens de entrada/salida y tiempo en cada respuesta.
- Los emojis se quitan y los símbolos sin dibujo en la tipografía del sistema se sustituyen.

**Conversaciones**
- Se guardan solas en `/sys/ZUNZUN/LUMEN/` (un archivo de texto por chat), ordenadas por fecha.
- Botón derecho en la lista: renombrar, guardar como Markdown, copiar todo, borrar.
- **Exportar** (`Ctrl+S`) guarda el chat en Markdown en `/sys/DOCS` (se abre con Escritor o Editor).
- **Imprimir** (`Ctrl+P`) usa el diálogo de impresión del sistema (también "Guardar como PDF").

**Ajustes**
- Instrucciones de sistema para todos los chats y, aparte, solo para el chat abierto.
- Temperatura, máximo de tokens y respuesta en vivo (sí/no).
- **Servidor de la API**: cualquier servidor compatible con la API de OpenAI, también en la red local
  (por ejemplo `http://192.168.1.20:11434/v1` para Ollama o `http://192.168.1.20:1234/v1` para
  LM Studio). Con servidores locales la clave es opcional.
- **Borrar todos los chats**.

**Sistema**
- Bilingüe ES / EN (EE. UU.): sigue el idioma de la bandera del panel.
- Ventana redimensionable; la lista de chats se oculta con **Chats** o en ventanas estrechas.
- Atajos: `Intro` enviar, `Mayús+Intro` nueva línea, `Esc` detener o cerrar panel, `Ctrl+N` nuevo
  chat, `Ctrl+L` elegir modelo, `Ctrl+R` reintentar, `Ctrl+S` exportar, `Ctrl+P` imprimir,
  `RePág`/`AvPág` y la rueda para desplazarse.

---

## Diferencias con la versión web

| Web (`index.html`) | ZunzúnOS |
|---|---|
| JavaScript en el navegador | Programa nativo en C, sin JavaScript |
| `localStorage` | Archivos de texto en `/sys/ZUNZUN` (se ven también desde Windows, macOS y Linux) |
| Imágenes adjuntas, dictado y lectura en voz alta | No disponibles |
| Tipografías web y emojis | Tipografía VGA del sistema (CP437) |
| Exportar/importar JSON | Exportar a Markdown, copiar e imprimir |

---

## Si algo falla

- **"certificado no fiable" / "certificado caducado"**: revisa la fecha y hora del equipo.
- **"No hay conexión con el servidor de la API"**: revisa la red (el icono del panel) y el servidor en Ajustes.
- **429**: el modelo gratuito está saturado o has llegado al límite por minuto; espera o elige otro.
- **402**: el modelo es de pago y la cuenta no tiene saldo; usa la pestaña **Gratis**.
- Los errores también quedan en `DIAGNOST.TXT` (raíz de `/sys`) al apagar.

---

## Pruebas

`tools/fake_openrouter.py` imita la API de OpenRouter (modelos, clave, respuestas en vivo y de una vez,
error 429) y `tools/lumen_test.py` maneja LUMEN en QEMU de principio a fin; está incluida en
`tools/regresion.sh`.
