#!/usr/bin/env python3
"""Lab falso para probar Forense (protocolo zz-forense/1) sin una IA real: API de chat compatible con OpenAI en el
puerto 8090, con la clave 'zz-lab-prueba'.

Ronda 1: recibe el expediente y pide cuatro pruebas:
- una clave del registro;
- un archivo dentro de la carpeta de un usuario (la ruta llega anonimizada);
- una prueba aun no disponible;
- una accion inventada, que Forense tiene que rechazar.

Ronda 2: comprueba los resultados y cierra el caso con un diagnostico y una reparacion propuesta.

Anota en DIR lo que recibe:
- fuga.txt si le llega algun dato personal sin anonimizar;
- comprobaciones.txt con lo que encontro bien o mal en cada ronda.
Uso: fake_lab.py DIR"""
import http.server, json, os, sys

DIR = sys.argv[1]; os.makedirs(DIR, exist_ok=True)
PERSONAL = ['PC-MARTA', 'Marta', 'Garcia']
n = [0]


def note(f, s):
    with open(os.path.join(DIR, f), 'a') as o:
        o.write(s + '\n')


def reply(obj, text=''):
    return {'id': 'zz', 'object': 'chat.completion', 'model': 'modelo-prueba',
            'choices': [{'index': 0, 'message': {'role': 'assistant', 'content': text + '\n```json\n' + json.dumps(obj, ensure_ascii=False) + '\n```'}, 'finish_reason': 'stop'}]}


class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass

    def send(self, code, obj):
        b = json.dumps(obj, ensure_ascii=False).encode()
        self.send_response(code); self.send_header('Content-Type', 'application/json'); self.send_header('Content-Length', str(len(b))); self.end_headers(); self.wfile.write(b)

    def auth(self):
        if self.headers.get('Authorization') != 'Bearer zz-lab-prueba':
            self.send(401, {'error': {'message': 'clave no valida'}}); return False
        return True

    def do_GET(self):
        if not self.auth(): return
        if self.path.endswith('/models'): self.send(200, {'object': 'list', 'data': [{'id': 'modelo-prueba', 'object': 'model'}]})
        else: self.send(404, {'error': {'message': 'no existe'}})

    def do_POST(self):
        if not self.auth(): return
        raw = self.rfile.read(int(self.headers.get('Content-Length', 0)))
        n[0] += 1; open(os.path.join(DIR, f'peticion-{n[0]}.json'), 'wb').write(raw)
        txt = raw.decode('utf-8', 'replace')
        for p in PERSONAL:
            if p.lower() in txt.lower(): note('fuga.txt', f'peticion {n[0]}: contiene {p}')
        req = json.loads(raw); msgs = req['messages']; rounds = sum(m['role'] == 'assistant' for m in msgs)
        ok = lambda c, what: note('comprobaciones.txt', ('OK ' if c else 'MAL ') + what)
        if rounds == 0:
            ok(req.get('model') == 'modelo-prueba', 'modelo')
            ok(msgs[0]['role'] == 'system' and msgs[0]['content'].startswith('zz-forense/1'), 'mensaje de sistema del protocolo')
            u = msgs[1]['content']
            ok(u.startswith('EXPEDIENTE') and '[EQUIPO]' in u and '== ARRANQUE ==' in u and 'iastorac.sys' in u, 'expediente anonimizado')
            self.send(200, reply({'protocolo': 'zz-forense/1', 'diagnostico': 'Parece un driver de disco dañado en [EQUIPO].',
                                  'hipotesis': [{'causa': 'iastorac.sys corrupto tras la actualizacion', 'probabilidad': 0.7, 'pruebas': ['SrtTrail.txt']}],
                                  'pedir': [{'id': 'p1', 'accion': 'leer_registro', 'args': {'colmena': 'SYSTEM', 'clave': 'CurrentControlSet\\Services\\iaStorAC'}},
                                            {'id': 'p2', 'accion': 'leer_archivo', 'args': {'ruta': '\\Users\\[USUARIO1]\\Desktop\\notas.txt', 'max_kb': 4}},
                                            {'id': 'p3', 'accion': 'eventos', 'args': {'registro': 'System'}},
                                            {'id': 'p4', 'accion': 'formatear_disco', 'args': {}}],
                                  'pasos': [], 'fin': False}, 'Necesito ver un par de cosas.'))
        else:
            u = msgs[-1]['content']
            ok(u.startswith('RESULTADOS'), 'resultados')
            ok('Start = 0' in u and 'ImagePath' in u, 'registro leido (CurrentControlSet resuelto)')
            ok('Llamar al tecnico' in u, 'archivo de la carpeta del usuario (ruta desanonimizada)')
            ok('disponible' in u or 'available' in u, 'prueba no disponible avisada')
            ok('rechazada' in u or 'rejected' in u, 'accion inventada rechazada')
            self.send(200, reply({'protocolo': 'zz-forense/1', 'diagnostico': 'El driver iaStorAC de [EQUIPO] esta dañado y Windows no arranca sin el.',
                                  'hipotesis': [{'causa': 'iastorac.sys corrupto', 'probabilidad': 0.9, 'pruebas': ['SrtTrail.txt', 'Start = 0 (arranque)']}],
                                  'pedir': [], 'pasos': [{'id': 'r1', 'accion': 'desactivar_driver', 'args': {'servicio': 'iaStorAC'},
                                                          'explicacion': 'Usar el driver estandar storahci en su lugar.', 'riesgo': 'bajo'}],
                                  'fin': True}))


http.server.ThreadingHTTPServer(('0.0.0.0', 8090), H).serve_forever()
