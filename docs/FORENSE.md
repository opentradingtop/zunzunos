# Forense — rastreador de desastres de sistemas

**Forense** analiza el sistema operativo instalado en un equipo que ya no arranca. ZunzúnOS arranca desde la
memoria USB, Forense examina el disco del equipo (empezando por **Windows**), prepara un **expediente** con
las pruebas y lo manda a un **Lab**: una IA que busca las causas y propone cómo repararlo. **Ningún cambio se
hace sin que el administrador lo apruebe**, y antes de cada cambio Forense guarda una copia para poder
deshacerlo.

**Autor:** [tonyfluxa.top](https://tonyfluxa.top) · Licencia 0BSD, como todo ZunzúnOS.

---

## Cómo trabaja

1. **Reconocer.** Busca en los discos las particiones de Windows y lee, sin modificar nada:
   - la versión de Windows;
   - el nombre del equipo;
   - cuándo se apagó bien por última vez;
   - cómo arrancaba (UEFI o BIOS) y su configuración de arranque.
2. **Recoger pruebas (expediente).** Solo lectura:
   - los errores del registro de eventos;
   - los volcados de los pantallazos azules;
   - el informe de la reparación automática de Windows (`SrtTrail.txt`);
   - las actualizaciones a medio instalar;
   - los drivers cambiados hace poco;
   - el espacio libre;
   - si el disco quedó marcado como dañado;
   - la salud física del disco (SMART).
3. **Analizar en el Lab.** El expediente va al Lab elegido. El Lab responde con:
   - un diagnóstico y las causas posibles, ordenadas por probabilidad;
   - más pruebas que quiere ver (solo lectura);
   - pasos de reparación.
4. **Reparar, paso a paso.**
   - Cada paso propuesto se enseña con su explicación y su riesgo.
   - El administrador lo aprueba o lo rechaza.
   - Forense guarda una copia y lo ejecuta.
   - El resultado vuelve al Lab para el siguiente paso.
   - La IA **no ejecuta nada por su cuenta**: solo puede elegir acciones de un **catálogo cerrado** que
     ZunzúnOS sabe hacer y deshacer.
5. **Informe.** Todo el caso queda en la memoria USB en `FORENSE/<fecha>-<equipo>/`:
   - el expediente y la conversación con el Lab;
   - las copias de seguridad;
   - el informe final.

## Uso (fases 1 y 2)

1. **Inicio → Rescate → Forense** (solo el administrador). Forense busca los Windows de todos los discos y enseña el
   informe del primero.
2. **Guardar expediente** (o `Ctrl+S`) lo deja en `FORENSE/<fecha>-<equipo>/expediente.txt` en la memoria USB.
3. **Labs**: añade un Lab con su nombre, URL base, API key y modelo, y prueba la conexión.
   - Si LUMEN ya tiene una clave de Lyceum, un botón crea el Lab de Lyceum con ella.
   - El marcado con `*` es el preferido.
4. **Analizar en el Lab** (o `Ctrl+L`):
   - Forense manda el expediente, anonimizado si hace falta.
   - Si el Lab pide más pruebas, Forense las enseña y pregunta antes de dárselas.
   - La conversación queda en `lab.txt`, con los nombres reales, y lo que salió hacia el Lab en `lab-enviado.txt`,
     con las marcas.

## Labs

| Lab | Qué es | Cuándo |
|---|---|---|
| **Lab local** | Un servidor de IA propio en la red: el de casa, el de la empresa, el del técnico | Es el sugerido en cuanto hay uno configurado. Los datos no salen de la red |
| **Lyceum** | IA en la nube, con servidores en la UE | Cuando no hay Lab local. El expediente se anonimiza antes de enviarlo |

Se pueden guardar varios Labs; uno está marcado como el preferido.

---

## Protocolo de Lab ZunzúnOS, versión 1 (`zz-forense/1`)

El protocolo es **estándar y abierto** para que cualquiera pueda montar su propio Lab: una empresa, un servicio
técnico o un usuario en casa. No hace falta ningún programa especial de ZunzúnOS en el servidor.

### 1. Transporte: la API de chat compatible con OpenAI

Un Lab es cualquier servidor que atienda:

```
POST {URL base}/chat/completions
Authorization: Bearer {API key}
Content-Type: application/json
```

y, de forma opcional, `GET {URL base}/models` para listar los modelos. Así funciona cualquier servidor que ya
hable este formato:

| Servidor | URL base típica |
|---|---|
| Lyceum | `https://api.lyceum.technology/openai/v1` |
| Ollama | `http://equipo-del-lab:11434/v1` |
| llama.cpp (`llama-server`) | `http://equipo-del-lab:8080/v1` |
| vLLM, LM Studio y similares | la que indique el servidor, terminada en `/v1` |

### 2. Configurar un Lab en Forense

| Dato | Ejemplo | Notas |
|---|---|---|
| Nombre | `Lab de la oficina` | Para reconocerlo en la lista |
| URL base | `http://192.168.1.50:11434/v1` | `http://` solo se acepta en la red local; fuera de ella, `https://` |
| API key | `zz-lab-...` | La pone el administrador del Lab. Puede ir vacía si el servidor no la pide |
| Modelo | `qwen3:32b` | Se elige de la lista de `/models` o se escribe a mano |
| Anonimizar | sí / no | Siempre «sí» en la nube; en un Lab local lo decide el administrador |

La configuración se guarda solo en la memoria USB (`/sys/ZUNZUN/FORENSE.TXT`).

### 3. Lo que envía Forense

- **Mensaje de sistema.** Explica al Lab:
  - su papel;
  - el catálogo de acciones permitidas;
  - el formato obligatorio de la respuesta (punto 4).

  Empieza con la línea `zz-forense/1`, para que un Lab especializado pueda reconocer el protocolo.
- **Primer mensaje del usuario.** Es el expediente, en texto, con secciones fijas:
  `EQUIPO`, `DISCOS`, `ARRANQUE`, `WINDOWS`, `EVENTOS`, `PANTALLAZOS`, `REPARACION_AUTOMATICA`,
  `ACTUALIZACIONES`, `DRIVERS`, `ESPACIO`, `SALUD_DISCO`.
- **Mensajes siguientes.** Llevan los resultados de las pruebas y de los pasos aprobados, cada uno con su
  identificador.

### 4. Lo que responde el Lab

Un objeto JSON. Puede ir rodeado de texto; Forense busca el primer objeto que lleve
`"protocolo": "zz-forense/1"`.

```json
{
  "protocolo": "zz-forense/1",
  "diagnostico": "Windows no arranca porque el driver de almacenamiento instalado ayer falla al iniciar.",
  "hipotesis": [
    { "causa": "Driver iaStorAC.sys actualizado el 22/11", "probabilidad": 0.7,
      "pruebas": ["Evento 7026 del arranque", "SrtTrail: 'Boot critical file ... is corrupt'"] },
    { "causa": "Sector dañado en el disco", "probabilidad": 0.1, "pruebas": ["SMART sin reasignados: poco probable"] }
  ],
  "pedir": [
    { "id": "p1", "accion": "leer_archivo", "args": { "ruta": "\\Windows\\INF\\setupapi.dev.log", "max_kb": 64 } }
  ],
  "pasos": [
    { "id": "r1", "accion": "desactivar_driver", "args": { "servicio": "iaStorAC" },
      "explicacion": "Vuelve a usar el driver estándar de Windows para el disco.", "riesgo": "bajo" }
  ],
  "fin": false
}
```

- **`pedir`:** solo acciones de lectura. Forense las enseña y, si el administrador lo permite, las hace todas
  seguidas.
- **`pasos`:** cada uno necesita la aprobación del administrador. Forense los hace de uno en uno y siempre con
  copia previa.
- **`fin: true`:** el Lab da el caso por cerrado. Forense escribe el informe.

### 5. Catálogo de acciones, versión 1

**Lectura** (en `pedir`):

| Acción | Argumentos | Devuelve |
|---|---|---|
| `leer_archivo` | `ruta`, `max_kb` (64 como mucho) | Texto (el final, si es un registro largo) |
| `listar_carpeta` | `ruta` | Nombres, tamaños y fechas |
| `leer_registro` | `colmena` (`SYSTEM`, `SOFTWARE`…), `clave`, `valor` opcional | Valores de la clave |
| `eventos` | `registro` (`System`, `Application`…), `nivel`, `ultimos` | Eventos resumidos |
| `pantallazo` | `archivo` | Código de parada y módulo culpable |
| `salud_disco` | — | Resumen SMART |

**Reparación** (en `pasos`; llegarán en una fase posterior, cada una con copia y forma de deshacer):

- `reparar_arranque`;
- `desactivar_driver`;
- `deshacer_actualizacion_pendiente`;
- `liberar_espacio`;
- `restaurar_registro_copia`;
- `marcar_disco_revisado`.

Una acción que no esté en el catálogo se rechaza y se le explica al Lab.

### 6. Anonimizar

Antes de enviar el expediente a un Lab en la nube, Forense cambia por marcas como `[USUARIO1]` o `[EQUIPO]`:

- los nombres de usuario y del equipo;
- los correos y las IP;
- los números de serie.

La tabla de equivalencias se queda en la memoria USB, y las respuestas se traducen de vuelta antes de
enseñarlas.

### 7. Montar un Lab propio (empresa o casa)

1. Instala en un equipo de la red un servidor compatible (por ejemplo Ollama) con un modelo capaz de razonar
   sobre registros técnicos.
2. Protégelo con una API key. Si tu servidor no tiene claves, ponle delante un proxy que exija
   `Authorization: Bearer`.
3. En cada ZunzúnOS: **Forense → Labs → Añadir**, con la URL base, la clave y el modelo, y márcalo como
   preferido.

---

## Fases

| Fase | Contenido | Estado |
|---|---|---|
| 1 | Lector NTFS por rutas; reconocer Windows (versión, equipo, arranque); expediente básico | hecha |
| 2 | Labs (Lyceum y local), protocolo `zz-forense/1`, pruebas pedidas por el Lab, anonimizar | hecha |
| 3 | Más pruebas: eventos (EVTX), pantallazos (minidump), SMART | siguiente |
| 4 | Reparaciones del catálogo con copia y deshacer | pendiente |
| 5 | Forense a través de la Red local | pendiente |

### Pruebas

- `tools/mkwindows.py` crea un disco de Windows 11 sintético con una avería creíble:
  - GPT con partición EFI y NTFS;
  - colmenas SOFTWARE y SYSTEM de verdad;
  - un driver de disco cambiado dos días antes del fallo, y SrtTrail.txt culpándolo.
- `tools/pruebas/forense/correr.sh` hace dos cosas en el Mac:
  - lee el disco entero con los lectores propios y lo compara archivo por archivo;
  - lo estropea al azar miles de veces (el MFT y las colmenas) para comprobar que el lector nunca se cuelga.
- `tools/forense_test.py` hace en QEMU lo mismo que haría el administrador: abrir Forense y guardar el expediente.
- `tools/forense_lab_test.py` comprueba el protocolo con un Lab falso (`tools/fake_lab.py`), que hace de IA:
  - el expediente llega anonimizado;
  - el Lab pide pruebas, incluida una acción inventada que hay que rechazar;
  - el administrador las aprueba;
  - el Lab cierra el caso;
  - si al Lab le llega un solo dato personal, la prueba falla.

Las pruebas usan discos de Windows **sintéticos** que generan herramientas propias, porque no hay un Windows
averiado real a mano. Cuando aparezca uno de verdad, servirá para afinar.
