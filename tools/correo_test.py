#!/usr/bin/env python3
"""Prueba la app Correo contra tools/fake_mail.py (IMAP 1143 y SMTP 1025 en el anfitrion, sin cifrar):
configurar la cuenta, recibir, leer texto, latin-1 con adjunto y HTML, responder, enviar e imprimir.
Uso: correo_test.py DIR_SALIDA DIR_CORREO "args de qemu" """
import os, subprocess, sys
out, mail, q = sys.argv[1:4]
MH = 42 + 9 * 28 + 14; MY = 800 - 36 - MH - 2; f, i, n = 2, 2, 8   # Internet > Correo (tercer programa)
y0 = max(42, min(42 + f * 28, MH - 6 - n * 28)); fy = MY + 42 + 28 * f + 14; ay = MY + y0 + 28 * i + 14
OPEN = ['click:50,782', 'sleep:0.6', f'move:100,{fy}', 'sleep:0.5', f'move:245,{fy}', 'sleep:0.3', f'move:350,{ay}', 'sleep:0.3', f'click:350,{ay}', 'sleep:2']
OX, OY = 88, 76
def fld(k): return f'click:{OX + 400},{OY + 128 + 36 * k + 13}'
s = ['wait:escritorio listo', 'wait:vfs: /sys', 'wait:red: IP', 'sleep:1'] + OPEN + ['shot:c-cuenta', f'click:{OX + 396},{OY + 84 + 14}', 'sleep:0.5',
     fld(0), 'type:Ana Prueba', fld(1), 'type:prueba@zunzun.test', fld(2), 'type:clave de prueba', fld(3), 'type:sin-tls:10.0.2.2:1143', fld(4), 'type:sin-tls:10.0.2.2:1025', 'sleep:0.3', 'shot:c-form',
     f'click:{OX + 90},{OY + 425}', 'wait:correo bandeja', 'sleep:1', 'shot:c-bandeja']
ROW = lambda r: f'click:{OX + 300},{OY + 40 + 4 + 22 + r * 20 + 9}'
s += [ROW(0), 'wait:correo leido: Solo HTML', 'sleep:0.8', 'shot:c-html', ROW(1), 'wait:correo leido: Reuni', 'sleep:0.8', 'shot:c-latin1', ROW(2), 'wait:correo leido: Hola desde', 'sleep:0.8', 'shot:c-texto']
s += ['key:ctrl-p', 'wait:dialogo imprimir abierto', 'sleep:1', 'shot:c-imprimir', 'key:esc', 'sleep:0.6']
bx = 8 + sum(len(w) * 8 + 20 for w in ['Recibir', 'Escribir']) + 10   # boton Responder
s += [f'click:{OX + bx},{OY + 20}', 'sleep:1.2', 'type:Gracias, recibido.', 'sleep:0.3', 'shot:c-responder', 'click:166,122', 'wait:correo enviado', 'sleep:1', 'shot:c-enviado']
# adjuntos: guardar el PDF del mensaje de Jose y enviarlo adjunto en un mensaje nuevo
s += [ROW(1), 'wait:correo leido: Reuni', 'sleep:0.8', 'shot:c-adjunto', 'clicklog:correo adjunto boton (\\d+),(\\d+)', 'wait:correo adjunto guardado', 'sleep:0.5']
s += [f'click:{OX + 120},{OY + 20}', 'wait:correo escribir', 'sleep:0.8', 'clicklog:para (\\d+),(\\d+)', 'type:Luis <luis@ejemplo.es>', 'clicklog:asunto (\\d+),(\\d+)', 'type:Con adjunto', 'clicklog:texto (\\d+),(\\d+)', 'type:Te mando el orden del dia.',
      'clicklog:adjuntar (\\d+),(\\d+)', 'wait:selector abierto', 'sleep:0.6', 'shot:c-selector', 'type:/sys/DOCS/Correo/orden del dia.pdf\\n', 'wait:correo adjuntado', 'sleep:0.5', 'shot:c-con-adjunto', 'clicklog:enviar (\\d+),(\\d+)', 'wait:correo enviado a Luis', 'sleep:1']
# borrador: escribir, guardarlo y cerrar; abrirlo desde Borradores, terminarlo y enviarlo; verlo en Enviados
s += [f'click:{OX + 120},{OY + 20}', 'wait:correo escribir', 'sleep:0.8', 'clicklog:para (\\d+),(\\d+)', 'type:Eva <eva@ejemplo.es>', 'clicklog:asunto (\\d+),(\\d+)', 'type:Borrador de prueba', 'clicklog:texto (\\d+),(\\d+)', 'type:Primera version.',
      'clicklog:borrador (\\d+),(\\d+)', 'wait:correo borrador guardado', 'sleep:0.5', 'shot:c-borrador', 'key:alt-f4', 'sleep:0.8',
      'clicklog:borradores (\\d+),(\\d+)', 'wait:correo carpeta borradores 1 mensajes', 'sleep:0.6', 'shot:c-borradores', ROW(0), 'wait:correo borrador abierto', 'sleep:0.8',
      'clicklog:texto (\\d+),(\\d+)', 'key:ctrl-end', 'type: Y segunda.', 'sleep:0.3', 'clicklog:enviar (\\d+),(\\d+)', 'wait:correo enviado a Eva', 'sleep:1',
      'clicklog:enviados (\\d+),(\\d+)', 'wait:correo carpeta enviados 3 mensajes', 'sleep:0.5', ROW(0), 'wait:correo enviado leido: Borrador de prueba', 'sleep:0.8', 'shot:c-enviados',
      'clicklog:borradores (\\d+),(\\d+)', 'wait:correo carpeta borradores 0 mensajes', 'sleep:0.5']
r = subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', 'bios', '--no-disk', '--abs', '--out', out, '--timeout', '60', '--port', '4697', '--qemu', q] + s, stdout=subprocess.DEVNULL)
log = open(os.path.join(out, 'serial-bios.log'), errors='replace').read(); ok = r == 0
for m in ('correo bandeja 3 mensajes', 'correo leido: Solo HTML', 'correo leido: Reunión del lunes', 'correo enviado a Ana <ana@ejemplo.es>: 0', 'dialogo imprimir abierto'):
    if m not in log: print('FALLO: falta', repr(m)); ok = False
if 'correo enviado' in log and not log.split('correo enviado')[1].split('\n')[0].rstrip().endswith(': 0'): print('FALLO: el envio dio error'); ok = False
sent = [os.path.join(mail, f) for f in os.listdir(mail) if f.startswith('enviado')] if os.path.isdir(mail) else []
if not sent: print('FALLO: el servidor no recibio el mensaje'); ok = False
else:
    import base64, email
    rep = [f for f in sorted(sent) if b'Re: Hola desde la prueba' in open(f, 'rb').read()] or sorted(sent)
    m = email.message_from_bytes(open(rep[0], 'rb').read().split(b'\r\n', 1)[1])
    body = m.get_payload(decode=True).decode('utf-8', 'replace')
    if m['Subject'] != 'Re: Hola desde la prueba' or 'Gracias, recibido.' not in body or '> Primera linea.' not in body or 'In-Reply-To' not in m:
        print('FALLO: mensaje enviado incorrecto:', m['Subject'], repr(body[:200])); ok = False
if 'correo adjunto guardado /sys/DOCS/Correo/orden del dia.pdf 9 bytes: 0' not in log: print('FALLO: el adjunto no se guardo'); ok = False
sent2 = [f for f in sent if b'Con adjunto' in open(f, 'rb').read()]
if not sent2: print('FALLO: no llego el mensaje con adjunto'); ok = False
else:
    import email
    m2 = email.message_from_bytes(open(sent2[0], 'rb').read().split(b'\r\n', 1)[1]); parts = [p for p in m2.walk() if p.get_filename()]
    if not m2.is_multipart() or not parts or parts[0].get_filename() != 'orden del dia.pdf' or parts[0].get_payload(decode=True) != b'%PDF-1.4\n' or parts[0].get_content_type() != 'application/pdf':
        print('FALLO: adjunto enviado incorrecto', [(p.get_filename(), p.get_content_type()) for p in parts]); ok = False
sent3 = [f for f in sent if b'Borrador de prueba' in open(f, 'rb').read()]
import email
m3 = email.message_from_bytes(open(sent3[0], 'rb').read().split(b'\r\n', 1)[1]) if sent3 else None
if not m3 or (m3.get_payload(decode=True) or b'').decode('utf-8', 'replace').strip() != 'Primera version. Y segunda.': print('FALLO: el borrador no se envio completo'); ok = False
for x in ('PANICO', 'fallo de pagina', 'excepcion', 'termino: -'):
    if x in log: print('FALLO:', x); ok = False
print('correo:', 'OK' if ok else 'FALLO'); sys.exit(0 if ok else 1)
