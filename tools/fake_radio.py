#!/usr/bin/env python3
"""Radio falsa para probar el programa Radio sin Internet, en el puerto 8096:
- el catalogo, como Radio Browser: /json/stations/search (con codec, countrycode, tag y name), /json/countries,
  /json/tags y /json/url/UUID (la cuenta de escuchas);
- /emision: un MP3 (tools/pruebas/radio/mktono.c) en bucle al ritmo de 128 kbps, con metadatos ICY cada 16000 bytes
  (StreamTitle='Tono de prueba - ZunzunOS') si el cliente manda Icy-MetaData: 1;
- /movida: redirige (302) a /emision, como hacen algunas emisoras.
Anota las peticiones en DIR/peticiones.txt.
Uso: fake_radio.py DIR TONO.mp3"""
import http.server, json, os, sys, time, urllib.parse

DIR, TONO = sys.argv[1], sys.argv[2]; os.makedirs(DIR, exist_ok=True); MP3 = open(TONO, 'rb').read()
B = 'http://10.0.2.2:8096'
ST = [{'stationuuid': 'uuid-tono', 'name': 'Radio Tono de Prueba', 'url': B + '/movida', 'url_resolved': B + '/movida', 'codec': 'MP3', 'bitrate': 128, 'countrycode': 'ES', 'tags': 'pruebas,tono', 'clickcount': 900, 'hls': 0},
      {'stationuuid': 'uuid-aac', 'name': 'Emisora AAC (no se puede oir)', 'url': B + '/aac', 'url_resolved': B + '/aac', 'codec': 'AAC', 'bitrate': 64, 'countrycode': 'ES', 'tags': 'pruebas', 'clickcount': 800, 'hls': 0},
      {'stationuuid': 'uuid-jazz', 'name': 'Jazz de Prueba', 'url': B + '/emision', 'url_resolved': B + '/emision', 'codec': 'MP3', 'bitrate': 128, 'countrycode': 'FR', 'tags': 'jazz', 'clickcount': 500, 'hls': 0}]


def note(s):
    with open(os.path.join(DIR, 'peticiones.txt'), 'a') as f: f.write(s + '\n')


class H(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.0'
    def log_message(self, *a): pass

    def js(self, obj):
        b = json.dumps(obj).encode(); self.send_response(200); self.send_header('Content-Type', 'application/json'); self.send_header('Content-Length', str(len(b))); self.end_headers(); self.wfile.write(b)

    def do_GET(self):
        u = urllib.parse.urlparse(self.path); q = {k: v[0] for k, v in urllib.parse.parse_qs(u.query).items()}; note(self.path + ' | ' + (self.headers.get('Icy-MetaData') or ''))
        if u.path == '/json/stations/search':
            r = [s for s in ST if (not q.get('codec') or s['codec'] == q['codec']) and (not q.get('countrycode') or s['countrycode'] == q['countrycode'])
                 and (not q.get('tag') or q['tag'] in s['tags'].split(',')) and (not q.get('name') or q['name'].lower() in s['name'].lower())]
            return self.js(r)
        if u.path == '/json/countries': return self.js([{'name': 'Spain', 'iso_3166_1': 'ES', 'stationcount': 2}, {'name': 'France', 'iso_3166_1': 'FR', 'stationcount': 1}])
        if u.path == '/json/tags': return self.js([{'name': 'jazz', 'stationcount': 1}, {'name': 'pruebas', 'stationcount': 2}])
        if u.path.startswith('/json/url/'): return self.js({'ok': 'true', 'message': 'retrieved station url', 'stationuuid': u.path[10:]})
        if u.path == '/movida':
            self.send_response(302); self.send_header('Location', B + '/emision'); self.send_header('Content-Length', '0'); self.end_headers(); return
        if u.path == '/emision':
            meta = self.headers.get('Icy-MetaData') == '1'; MI = 16000
            self.send_response(200); self.send_header('Content-Type', 'audio/mpeg'); self.send_header('icy-name', 'Radio Tono de Prueba')
            if meta: self.send_header('icy-metaint', str(MI))
            self.end_headers()
            t = 'Tono de prueba - ZunzunOS'; m = ("StreamTitle='%s';" % t).encode(); m += b'\0' * (-len(m) % 16); mb = bytes([len(m) // 16]) + m
            pos = 0; sent = 0; left = MI; t0 = time.time()
            try:
                while True:
                    want = 64 * 1024 + int((time.time() - t0) * 16000) - sent   # 64 KB de golpe y luego 128 kbps
                    if want <= 0: time.sleep(0.05); continue
                    n = min(want, 4096); chunk = MP3[pos:pos + n]
                    if len(chunk) < n: chunk += MP3[:n - len(chunk)]
                    pos = (pos + n) % len(MP3); out = b''
                    for b in [chunk]:
                        while b:
                            k = min(len(b), left) if meta else len(b); out += b[:k]; b = b[k:]
                            if meta: left -= k
                            if meta and left == 0: out += mb; left = MI
                    self.wfile.write(out); sent += n
            except (BrokenPipeError, ConnectionResetError): note('emision cortada tras %d bytes' % sent)
            return
        self.send_response(404); self.end_headers()


http.server.ThreadingHTTPServer(('0.0.0.0', 8096), H).serve_forever()
