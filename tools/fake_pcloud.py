#!/usr/bin/env python3
"""pCloud falso para probar el programa Nube sin una cuenta de verdad: los metodos de la API HTTP JSON de pCloud
que usa Nube (userinfo con getauth, listfolder, getfilelink y la descarga, uploadfile por PUT,
createfolderifnotexists, renamefile, renamefolder, deletefile, deletefolderrecursive y logout), en el puerto 8095.
Cuenta de prueba: prueba@zunzun.test con la contrasena "clave de prueba".
Anota en DIR/peticiones.txt cada llamada (sin la contrasena) y en DIR/estado.json como queda la nube al final.
Uso: fake_pcloud.py DIR"""
import http.server, json, os, sys, urllib.parse

DIR = sys.argv[1]; os.makedirs(DIR, exist_ok=True)
USER, PASS, AUTH = 'prueba@zunzun.test', 'clave de prueba', 'zz-llave-de-prueba'
T = 1764000000
# id -> nodo; las carpetas tienen 'hijos'
nodes = {0: {'name': '/', 'dir': True, 'parent': None, 'hijos': [1, 10]},
         1: {'name': 'Documentos', 'dir': True, 'parent': 0, 'hijos': [11]},
         10: {'name': 'hola.txt', 'dir': False, 'parent': 0, 'data': b'Hola desde pCloud.\n'},
         11: {'name': 'informe.txt', 'dir': False, 'parent': 1, 'data': b'Informe de prueba.\n' * 50}}
nid = [100]


def note(s):
    with open(os.path.join(DIR, 'peticiones.txt'), 'a') as f: f.write(s + '\n')


def save():
    st = {str(k): {'name': v['name'], 'dir': v['dir'], 'parent': v['parent'], 'size': len(v.get('data', b''))} for k, v in nodes.items()}
    json.dump(st, open(os.path.join(DIR, 'estado.json'), 'w'), indent=1)


def meta(i):
    n = nodes[i]; m = {'name': n['name'], 'isfolder': n['dir'], 'modified': T, 'created': T, 'parentfolderid': n['parent'] or 0, 'id': ('d' if n['dir'] else 'f') + str(i)}
    if n['dir']: m['folderid'] = i
    else: m.update({'fileid': i, 'size': len(n['data']), 'contenttype': 'text/plain'})
    return m


class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass

    def send(self, obj, code=200, raw=None):
        b = raw if raw is not None else json.dumps(obj).encode()
        self.send_response(code); self.send_header('Content-Type', 'application/octet-stream' if raw is not None else 'application/json'); self.send_header('Content-Length', str(len(b))); self.end_headers(); self.wfile.write(b)

    def handle_any(self, body=b''):
        u = urllib.parse.urlparse(self.path); q = {k: v[0] for k, v in urllib.parse.parse_qs(u.query).items()}; m = u.path.strip('/')
        if m.startswith('dl/'):   # descarga: /dl/ID/nombre (el enlace de getfilelink no lleva la llave)
            i = int(m.split('/')[1]); note(f'descarga {i}'); return self.send(None, raw=nodes[i]['data']) if i in nodes else self.send({'result': 2009}, 404)
        safe = {k: ('***' if k == 'password' else v) for k, v in q.items()}; note(f'{self.command} {m} {json.dumps(safe, ensure_ascii=False)}')
        if 'password' in q and m != 'userinfo': note('MAL: contrasena fuera de userinfo')
        if m == 'userinfo' and 'username' in q:
            if q.get('username') != USER or q.get('password') != PASS: return self.send({'result': 2000, 'error': 'Log in failed.'})
            return self.send({'result': 0, 'auth': AUTH, 'email': USER, 'quota': 10 * 1024 ** 3, 'usedquota': 123456789, 'userid': 7})
        if q.get('auth') != AUTH: return self.send({'result': 1000, 'error': 'Log in required.'})
        if m == 'userinfo': return self.send({'result': 0, 'email': USER, 'quota': 10 * 1024 ** 3, 'usedquota': 123456789})
        if m == 'logout': note('salida'); return self.send({'result': 0, 'auth_deleted': True})
        if m == 'listfolder':
            i = int(q.get('folderid', 0))
            if i not in nodes or not nodes[i]['dir']: return self.send({'result': 2005, 'error': 'Directory does not exist.'})
            md = meta(i); md['contents'] = [meta(c) for c in nodes[i]['hijos']]; return self.send({'result': 0, 'metadata': md})
        if m == 'getfilelink':
            i = int(q.get('fileid', 0))
            if i not in nodes: return self.send({'result': 2009, 'error': 'File not found.'})
            return self.send({'result': 0, 'path': f'/dl/{i}/' + urllib.parse.quote(nodes[i]['name']), 'hosts': [self.headers.get('Host').split(':')[0] + ':8095'], 'expires': 'x'})
        if m == 'uploadfile':
            p = int(q.get('folderid', 0)); name = q.get('filename', 'archivo'); i = nid[0]; nid[0] += 1
            nodes[i] = {'name': name, 'dir': False, 'parent': p, 'data': body}; nodes[p]['hijos'].append(i); save()
            return self.send({'result': 0, 'fileids': [i], 'metadata': [meta(i)]})
        if m == 'createfolderifnotexists':
            p = int(q.get('folderid', 0)); name = q.get('name', '')
            for c in nodes[p]['hijos']:
                if nodes[c]['name'] == name and nodes[c]['dir']: return self.send({'result': 0, 'created': False, 'metadata': meta(c)})
            i = nid[0]; nid[0] += 1; nodes[i] = {'name': name, 'dir': True, 'parent': p, 'hijos': []}; nodes[p]['hijos'].append(i); save()
            return self.send({'result': 0, 'created': True, 'metadata': meta(i)})
        if m in ('renamefile', 'renamefolder'):
            i = int(q.get('fileid' if m == 'renamefile' else 'folderid', -1))
            if i not in nodes: return self.send({'result': 2009, 'error': 'File not found.'})
            nodes[i]['name'] = q.get('toname', nodes[i]['name']); save(); return self.send({'result': 0, 'metadata': meta(i)})
        if m in ('deletefile', 'deletefolderrecursive'):
            i = int(q.get('fileid' if m == 'deletefile' else 'folderid', -1))
            if i not in nodes or i == 0: return self.send({'result': 2009, 'error': 'File not found.'})
            nodes[nodes[i]['parent']]['hijos'].remove(i); del nodes[i]; save(); return self.send({'result': 0})
        return self.send({'result': 2000, 'error': 'metodo desconocido ' + m})

    def do_GET(self): self.handle_any()

    def do_PUT(self): self.handle_any(self.rfile.read(int(self.headers.get('Content-Length', 0))))


save()
http.server.ThreadingHTTPServer(('0.0.0.0', 8095), H).serve_forever()
