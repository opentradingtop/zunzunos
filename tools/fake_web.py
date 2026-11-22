#!/usr/bin/env python3
"""Web falsa para probar el navegador en QEMU (puerto 8081, sin cifrar).
/set pone cookies; /ver anota la cabecera Cookie recibida; /base tiene <base href>;
/noscript tiene un enlace solo dentro de <noscript>; /refresh redirige con <meta refresh>.
Cada peticion se anota en DIR/peticiones.txt como "RUTA | Cookie: ...". Uso: fake_web.py DIR"""
import http.server, os, sys
OUT = sys.argv[1]; os.makedirs(OUT, exist_ok=True); LOG = os.path.join(OUT, 'peticiones.txt'); open(LOG, 'w').close()
BIG = 'font-size: 28px'
PAGES = {
    '/set': ('<html><body><a href="/ver">COOKIESPUESTAS</a></body></html>', [('Set-Cookie', 'sesion=abc123; Max-Age=3600; Path=/'), ('Set-Cookie', 'temporal=1; Path=/')]),
    '/ver': ('<html><body>VER</body></html>', []),
    '/base': ('<html><head><base href="/destino/"></head><body><a href="pagina.html">IRIRIRIRIRIRIRIRIRIRIRIRIRIRIRIRIRIRIRIR</a></body></html>', []),
    '/destino/pagina.html': ('<html><body>DESTINO</body></html>', []),
    '/noscript': ('<html><body><script>document.write("x")</script><noscript><a href="/sinjs">SINJSSINJSSINJSSINJSSINJSSINJSSINJS</a></noscript></body></html>', []),
    '/sinjs': ('<html><body>SIN JAVASCRIPT</body></html>', []),
    '/refresh': ('<html><head><meta http-equiv="refresh" content="0; url=/refrescado"></head><body>espera</body></html>', []),
    '/refrescado': ('<html><body>REFRESCADO</body></html>', []),
}
class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        with open(LOG, 'a') as f: f.write(f"{self.path} | Cookie: {self.headers.get('Cookie', '')}\n")
        body, hs = PAGES.get(self.path, ('<html><body>404</body></html>', []))
        b = body.encode(); self.send_response(200 if self.path in PAGES else 404); self.send_header('Content-Type', 'text/html; charset=utf-8'); self.send_header('Content-Length', str(len(b)))
        for k, v in hs: self.send_header(k, v)
        self.end_headers(); self.wfile.write(b)
    def log_message(self, *a): pass
http.server.ThreadingHTTPServer(('127.0.0.1', 8081), H).serve_forever()
