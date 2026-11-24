#!/usr/bin/env python3
"""OpenRouter falso para probar LUMEN en QEMU (puerto 8082, sin cifrar).
GET /api/v1/models (en gzip si se pide), GET /api/v1/key (clave buena: sk-or-v1-prueba) y
POST /api/v1/chat/completions en vivo (SSE por trozos, con razonamiento, Markdown, emojis y
comentarios de espera) o de una vez. Si el ultimo mensaje dice "error429" responde 429.
Cada peticion se anota en DIR/peticiones.txt (ruta, Authorization y cuerpo). Uso: fake_openrouter.py DIR"""
import gzip, http.server, json, os, sys, time
OUT = sys.argv[1]; os.makedirs(OUT, exist_ok=True); LOG = os.path.join(OUT, 'peticiones.txt'); open(LOG, 'w').close()
KEY = 'sk-or-v1-prueba'
# claves que /key conoce pero el chat rechaza con 401 "User not found" (desactivada o de gestion), como en OpenRouter
SOLOKEY = {'sk-or-v1-solokey': {'label': 'sk-or-v1-sol...key'}, 'sk-or-v1-gestion': {'label': 'sk-or-v1-ges...ion', 'is_management_key': True}}
MODELS = {'data': [
    {'id': 'openrouter/free', 'name': 'Free Models Router', 'context_length': 200000, 'pricing': {'prompt': '0', 'completion': '0'}, 'architecture': {'output_modalities': ['text']}},
    {'id': 'deepseek/deepseek-v4.1-flash', 'name': 'DeepSeek: DeepSeek V4.1 Flash', 'context_length': 1048576, 'pricing': {'prompt': '0.00000005', 'completion': '2e-7'}, 'architecture': {'output_modalities': ['text']}},
    {'id': 'deepseek/deepseek-r1:free', 'name': 'DeepSeek: R1 (free)', 'context_length': 163840, 'pricing': {'prompt': '0', 'completion': '0'}, 'architecture': {'output_modalities': ['text']}},
    {'id': 'google/gemma-4-31b-it:free', 'name': 'Google: Gemma 4 31B (free)', 'context_length': 262144, 'pricing': {'prompt': '0', 'completion': '0'}, 'architecture': {'output_modalities': ['text']}},
    {'id': 'openai/gpt-image', 'name': 'OpenAI: Solo imagenes', 'context_length': 8000, 'pricing': {'prompt': '0', 'completion': '0'}, 'architecture': {'output_modalities': ['image']}},
    {'id': 'anthropic/claude-x', 'name': 'Anthropic: Claude X', 'context_length': 200000, 'pricing': {'prompt': '0.000003', 'completion': '0.000015'}, 'architecture': {'output_modalities': ['text']}},
] + [{'id': f'relleno/modelo-{i}:free', 'name': f'Relleno: Modelo {i} áéí (free)', 'description': 'x' * 400, 'context_length': 32768, 'pricing': {'prompt': '0', 'completion': '0'}, 'architecture': {'output_modalities': ['text']}} for i in range(40)]}
ANSWER = ('# Colibríes \U0001F426\n\nEl **zunzún** es el colibrí más pequeño… y vive en Cuba.\n\n'
          '- Pesa menos de *2 gramos*\n- Bate las alas ~80 veces por segundo\n  - incluso más en vuelo nupcial\n\n'
          '1. Come néctar\n2. Come insectos\n\n> Una cita breve.\n\n'
          '| Especie | Peso |\n|---|---:|\n| Zunzún | 2 g |\n| Colibrí gigante | 20 g |\n\n'
          '```python\ndef hola():\n    print("¡Hola, ZunzúnOS!")\n```\n\nUsa `make run` para probar ✅. Más en [la web](https://ejemplo.org).')
def chunks(s, n):
    return [s[i:i + n] for i in range(0, len(s), n)]
class H(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    def log_req(self, body=b''):
        with open(LOG, 'a') as f: f.write(f"{self.command} {self.path} | Authorization: {self.headers.get('Authorization', '')} | X-Title: {self.headers.get('X-Title', '')} | {body.decode('utf-8', 'replace')}\n")
    def send_json(self, code, obj, gz=False):
        b = json.dumps(obj).encode()
        self.send_response(code); self.send_header('Content-Type', 'application/json')
        if gz: b = gzip.compress(b); self.send_header('Content-Encoding', 'gzip')
        self.send_header('Content-Length', str(len(b))); self.send_header('Connection', 'close'); self.end_headers(); self.wfile.write(b)
    def authed(self):
        return self.headers.get('Authorization', '') == 'Bearer ' + KEY
    def do_GET(self):
        self.log_req()
        if self.path == '/api/v1/models': return self.send_json(200, MODELS, 'gzip' in self.headers.get('Accept-Encoding', ''))
        if self.path == '/api/v1/key':
            if self.authed(): return self.send_json(200, {'data': {'label': 'sk-or-v1-pru...eba', 'usage': 0.0123, 'limit': None, 'is_free_tier': True, 'is_management_key': False}})
            k = self.headers.get('Authorization', '')[7:]
            if k in SOLOKEY: return self.send_json(200, {'data': dict(SOLOKEY[k], usage=0, limit=None)})
            return self.send_json(401, {'error': {'message': 'No auth credentials found', 'code': 401}})
        self.send_json(404, {'error': {'message': 'no existe', 'code': 404}})
    def do_POST(self):
        n = int(self.headers.get('Content-Length', '0')); body = self.rfile.read(n); self.log_req(body)
        if self.path != '/api/v1/chat/completions': return self.send_json(404, {'error': {'message': 'no existe', 'code': 404}})
        if not self.authed(): return self.send_json(401, {'error': {'message': 'User not found.', 'code': 401}})
        req = json.loads(body); last = req['messages'][-1]['content'] if req['messages'] else ''
        if isinstance(last, list):   # mensaje con imagenes: [{"type":"text"...},{"type":"image_url"...}]
            imgs = sum(1 for p in last if p.get('type') == 'image_url' and p['image_url']['url'].startswith('data:image/jpeg;base64,'))
            last = ' '.join(p.get('text', '') for p in last if p.get('type') == 'text') + f' [{imgs} imagenes]'
        if 'error429' in last: return self.send_json(429, {'error': {'message': 'Rate limit exceeded: free-models-per-min', 'code': 429}})
        if req.get('max_tokens') == 1: return self.send_json(200, {'choices': [{'message': {'role': 'assistant', 'content': 'p'}}]})
        model = req.get('model', '')
        answer = ANSWER if 'colibr' in last.lower() or 'colibr' in json.dumps(req).lower() else f'Recibido: {last}'
        if not req.get('stream'):
            return self.send_json(200, {'model': model, 'choices': [{'message': {'role': 'assistant', 'content': answer, 'reasoning': 'Pienso sin prisa.'}}], 'usage': {'prompt_tokens': 11, 'completion_tokens': 22}})
        self.send_response(200); self.send_header('Content-Type', 'text/event-stream'); self.send_header('Transfer-Encoding', 'chunked'); self.end_headers()
        def put(s):
            b = s.encode(); self.wfile.write(b'%x\r\n' % len(b) + b + b'\r\n'); self.wfile.flush()
        try:
            for _ in range(3): put(': OPENROUTER PROCESSING\n\n'); time.sleep(0.7)
            for r in chunks('Primero pienso en el zunzún. Luego respondo con Markdown.', 9):
                put('data: ' + json.dumps({'model': model, 'choices': [{'delta': {'reasoning': r}}]}) + '\n\n'); time.sleep(0.08)
            for c in chunks(answer, 7):
                ev = 'data: ' + json.dumps({'model': model, 'choices': [{'delta': {'content': c}}]}) + '\n\n'
                for half in (ev[:len(ev) // 2], ev[len(ev) // 2:]): put(half)   # eventos partidos entre trozos
                time.sleep(0.05)
            put('data: ' + json.dumps({'model': model, 'choices': [{'delta': {}, 'finish_reason': 'stop'}], 'usage': {'prompt_tokens': 42, 'completion_tokens': 99}}) + '\n\n')
            put('data: [DONE]\n\n'); self.wfile.write(b'0\r\n\r\n'); self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            with open(LOG, 'a') as f: f.write('cortado por el cliente\n')
        self.close_connection = True
    def log_message(self, *a): pass
http.server.ThreadingHTTPServer(('127.0.0.1', 8082), H).serve_forever()
