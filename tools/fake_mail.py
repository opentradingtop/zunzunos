#!/usr/bin/env python3
"""Servidor de correo falso para probar la app Correo en QEMU (sin cifrar): IMAP en 1143 y SMTP en 1025.
Usuario prueba@zunzun.test, clave "clave de prueba" (con espacios, como las de Google).
Los mensajes enviados se guardan en DIR/enviado-N.eml. Uso: fake_mail.py DIR"""
import base64, os, socketserver, sys, threading
OUT = sys.argv[1]; os.makedirs(OUT, exist_ok=True)
USER, PASS = 'prueba@zunzun.test', 'clavedeprueba'
def msg(n, frm, subj, body, extra=b''):
    return (f'From: {frm}\r\nTo: {USER}\r\nSubject: {subj}\r\nDate: Thu, 19 Nov 2026 10:{n:02d}:00 +0100\r\nMessage-ID: <m{n}@fake>\r\n').encode() + extra + b'\r\n' + body
BOX = [
    [101, False, msg(1, 'Ana <ana@ejemplo.es>', 'Hola desde la prueba', b'Primera linea.\r\nSegunda linea con acentos: canci\xc3\xb3n, ni\xc3\xb1o.\r\n', b'Content-Type: text/plain; charset=utf-8\r\n')],
    [102, True, msg(2, '=?UTF-8?B?Sm9zw6kgUMOpcmV6?= <jose@ejemplo.es>', '=?ISO-8859-1?Q?Reuni=F3n_del_lunes?=', b'--b1\r\nContent-Type: text/plain; charset=iso-8859-1\r\nContent-Transfer-Encoding: quoted-printable\r\n\r\nNos vemos el lunes a las 9. Caf=E9 incluido.\r\n--b1\r\nContent-Type: application/pdf; name="orden del dia.pdf"\r\nContent-Transfer-Encoding: base64\r\n\r\nJVBERi0xLjQK\r\n--b1--\r\n', b'MIME-Version: 1.0\r\nContent-Type: multipart/mixed; boundary="b1"\r\n')],
    [103, False, msg(3, 'Boletin <news@ejemplo.es>', 'Solo HTML', b'<html><head><style>p{color:red}</style></head><body><h1>Titular</h1><p>Texto en <b>HTML</b> &amp; entidades &aacute;.</p><ul><li>uno</li><li>dos</li></ul></body></html>\r\n', b'Content-Type: text/html; charset=utf-8\r\n')],
]
lock = threading.Lock()
class Imap(socketserver.StreamRequestHandler):
    def w(self, s): self.wfile.write(s if isinstance(s, bytes) else s.encode()); self.wfile.flush()
    def handle(self):
        self.w('* OK fake IMAP ready\r\n'); auth = False
        for raw in self.rfile:
            line = raw.decode(errors='replace').rstrip('\r\n'); tag, _, rest = line.partition(' '); cmd = rest.split(' ')[0].upper()
            if cmd == 'LOGIN':
                import shlex; a = shlex.split(rest[6:])
                if a == [USER, PASS] or a == [USER.split('@')[0], PASS]: auth = True; self.w(f'{tag} OK LOGIN done\r\n')
                else: self.w(f'{tag} NO [AUTHENTICATIONFAILED] bad\r\n')
            elif not auth and cmd not in ('NOOP', 'LOGOUT', 'CAPABILITY'): self.w(f'{tag} NO login first\r\n')
            elif cmd == 'SELECT': self.w(f'* {len(BOX)} EXISTS\r\n* 0 RECENT\r\n{tag} OK [READ-WRITE] SELECT done\r\n')
            elif cmd == 'FETCH':
                a, b = rest.split(' ')[1].split(':'); a, b = int(a), int(b)
                for i in range(a, min(b, len(BOX)) + 1):
                    uid, seen, m = BOX[i - 1]; h = m.split(b'\r\n\r\n')[0]; h = b'\r\n'.join(l for l in h.split(b'\r\n') if l.split(b':')[0] in (b'From', b'Subject', b'Date')) + b'\r\n\r\n'
                    fl = '\\Seen' if seen else ''; self.w(f'* {i} FETCH (UID {uid} FLAGS ({fl}) BODY[HEADER.FIELDS (FROM SUBJECT DATE)] {{{len(h)}}}\r\n'); self.w(h); self.w(')\r\n')
                self.w(f'{tag} OK FETCH done\r\n')
            elif cmd == 'UID':
                p = rest.split(' '); sub, uid = p[1].upper(), int(p[2]); k = [i for i, x in enumerate(BOX) if x[0] == uid]
                if sub == 'FETCH' and k: m = BOX[k[0]][2]; self.w(f'* {k[0] + 1} FETCH (UID {uid} BODY[] {{{len(m)}}}\r\n'); self.w(m); self.w(')\r\n')
                elif sub == 'STORE' and k:
                    with lock:
                        if 'Deleted' in rest: BOX[k[0]].append('del')
                        if 'Seen' in rest: BOX[k[0]][1] = True
                self.w(f'{tag} OK UID done\r\n')
            elif cmd == 'EXPUNGE':
                with lock: BOX[:] = [x for x in BOX if 'del' not in x]
                self.w(f'{tag} OK EXPUNGE done\r\n')
            elif cmd == 'LOGOUT': self.w(f'* BYE\r\n{tag} OK\r\n'); return
            else: self.w(f'{tag} OK\r\n')
class Smtp(socketserver.StreamRequestHandler):
    def w(self, s): self.wfile.write(s.encode()); self.wfile.flush()
    def handle(self):
        self.w('220 fake ESMTP\r\n'); rcpt = []; auth = False
        for raw in self.rfile:
            line = raw.decode(errors='replace').rstrip('\r\n'); u = line.upper()
            if u.startswith('EHLO'): self.w('250-fake\r\n250 AUTH PLAIN LOGIN\r\n')
            elif u.startswith('AUTH PLAIN '):
                p = base64.b64decode(line[11:]).split(b'\0'); auth = p[1:] == [USER.encode(), PASS.encode()]; self.w('235 ok\r\n' if auth else '535 bad\r\n')
            elif u.startswith('MAIL FROM'): self.w('250 ok\r\n' if auth else '530 auth\r\n')
            elif u.startswith('RCPT TO'): rcpt.append(line[8:]); self.w('250 ok\r\n')
            elif u == 'DATA':
                self.w('354 go\r\n'); data = b''
                for r in self.rfile:
                    if r in (b'.\r\n', b'.\n'): break
                    data += r
                n = len([f for f in os.listdir(OUT) if f.startswith('enviado')]) + 1
                open(os.path.join(OUT, f'enviado-{n}.eml'), 'wb').write(b'X-Rcpt: ' + ','.join(rcpt).encode() + b'\r\n' + data); self.w('250 queued\r\n')
            elif u == 'QUIT': self.w('221 bye\r\n'); return
            else: self.w('250 ok\r\n')
class T(socketserver.ThreadingTCPServer): allow_reuse_address = True; daemon_threads = True
threading.Thread(target=T(('0.0.0.0', 1143), Imap).serve_forever, daemon=True).start()
T(('0.0.0.0', 1025), Smtp).serve_forever()
