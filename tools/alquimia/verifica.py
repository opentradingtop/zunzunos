#!/usr/bin/env python3
"""Pruebas de Alquimia en el anfitrion: lo que crea el motor se abre con herramientas de verdad y lo que crean ellas
se abre con el motor.  python3 -I tools/alquimia/verifica.py BINARIO [carpeta]"""
import os, sys, subprocess, random, zlib, gzip, tarfile, zipfile, shutil, hashlib, hmac, struct, io, filecmp
B = sys.argv[1]; T = sys.argv[2] if len(sys.argv) > 2 else '/tmp/alquimia-pruebas'
ok = fail = 0
def check(name, cond, extra=''):
    global ok, fail
    if cond: ok += 1; print('  bien  ', name)
    else: fail += 1; print('  FALLO ', name, extra)
def run(*a, inp=None):
    p = subprocess.run([B] + [str(x) for x in a], capture_output=True, text=True); return p.returncode, (p.stdout + p.stderr).strip()
def sh(cmd):
    # errors='replace': el unzip de macOS cambia por '?' los bytes de UTF-8 que no ve imprimibles (la Ü sale como \xc3?)
    p = subprocess.run(cmd, shell=True, capture_output=True, text=True, errors='replace'); return p.returncode, (p.stdout + p.stderr).strip()
def same_tree(a, b):
    for root, dirs, files in os.walk(a):
        rel = os.path.relpath(root, a)
        for d in dirs:
            if not os.path.isdir(os.path.join(b, rel, d)): return 'falta carpeta ' + os.path.join(rel, d)
        for f in files:
            q = os.path.join(b, rel, f)
            if not os.path.isfile(q): return 'falta ' + os.path.join(rel, f)
            if open(os.path.join(root, f), 'rb').read() != open(q, 'rb').read(): return 'distinto ' + os.path.join(rel, f)
    return ''

shutil.rmtree(T, ignore_errors=True); os.makedirs(T)
S = os.path.join(T, 'origen', 'Carpeta ñandú')
random.seed(7)
os.makedirs(os.path.join(S, 'vacía')); os.makedirs(os.path.join(S, 'sub', 'más hondo'))
open(os.path.join(S, 'texto.txt'), 'w').write(('El zunzún es un colibrí. ' * 40 + '\n') * 2000)
open(os.path.join(S, 'vacio.dat'), 'wb').close()
open(os.path.join(S, 'uno.bin'), 'wb').write(b'x')
open(os.path.join(S, 'azar.bin'), 'wb').write(bytes(random.getrandbits(8) for _ in range(1_500_000)))
open(os.path.join(S, 'foto.jpg'), 'wb').write(bytes(random.getrandbits(8) for _ in range(200_000)))
mix = bytearray()
for i in range(3000): mix += (b'registro %06d;' % i) * random.randint(1, 5) + bytes(random.getrandbits(8) for _ in range(random.randint(0, 60)))
open(os.path.join(S, 'sub', 'mezcla.csv'), 'wb').write(bytes(mix))
open(os.path.join(S, 'sub', 'más hondo', 'Über größe.md'), 'w').write('# Título\n\nCañón, pingüino, Straße.\n' * 300)
open(os.path.join(S, 'sub', 'ceros.img'), 'wb').write(bytes(3_000_000))
longd = os.path.join(S, 'una carpeta con un nombre bastante largo', 'y otra subcarpeta que tambien tiene un nombre largo')
os.makedirs(longd); open(os.path.join(longd, 'archivo con nombre largo para pasar de cien caracteres en tar.txt'), 'w').write('largo\n')

print('deflate: ida y vuelta y zlib de Python')
for f in ['texto.txt', 'azar.bin', 'vacio.dat', 'uno.bin', 'sub/mezcla.csv', 'sub/ceros.img']:
    for lv in (1, 2, 3):
        o = os.path.join(T, 'd.raw'); r, out = run('dfl', lv, os.path.join(S, f), o)
        good = r == 0
        if good:
            try: good = zlib.decompress(open(o, 'rb').read(), -15) == open(os.path.join(S, f), 'rb').read()
            except Exception as e: good = False; out += ' ' + str(e)
        check('deflate %s nivel %d (%s)' % (f, lv, out), good)
gz = len(zlib.compress(open(os.path.join(S, 'texto.txt'), 'rb').read(), 9)); r, out = run('dfl', 3, os.path.join(S, 'texto.txt'), os.path.join(T, 'd.raw'))
print('  texto: Alquimia nivel 3', out, '; zlib -9', gz)

print('ZIP creado por Alquimia')
z1 = os.path.join(T, 'a.zip'); r, out = run('zip', 2, '-', z1, S); check('crear ZIP (%s)' % out, r == 0)
r, out = sh('unzip -t "%s"' % z1); check('unzip -t', r == 0 and 'No errors' in out, out[-300:])
X = os.path.join(T, 'x1'); os.makedirs(X); r, out = sh('cd "%s" && unzip -q "%s"' % (X, z1)); check('unzip extrae', r == 0, out)
check('unzip: contenido identico', same_tree(S, os.path.join(X, 'Carpeta ñandú')) == '', same_tree(S, os.path.join(X, 'Carpeta ñandú')))
with zipfile.ZipFile(z1) as z:
    check('zipfile de Python: testzip', z.testzip() is None)
    check('nombres UTF-8', any('ñandú' in n for n in z.namelist()))
    check('foto.jpg guardada sin deflate', [i for i in z.infolist() if i.filename.endswith('foto.jpg')][0].compress_type == 0)
X = os.path.join(T, 'x1b'); r, out = run('x', z1, '-', X); check('Alquimia abre su ZIP (%s)' % out, r == 0 and same_tree(S, os.path.join(X, 'Carpeta ñandú')) == '')

print('ZIP con contrasena AES-256 (AE-2)')
z2 = os.path.join(T, 'aes.zip'); PW = 'Hamburg§2026!'
r, out = run('zip', 3, PW, z2, S); check('crear ZIP AES (%s)' % out, r == 0)
# descifrado independiente con la biblioteca cryptography de Python
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
def wz_decrypt(path, pw):
    res = {}
    with zipfile.ZipFile(path) as z:
        for inf in z.infolist():
            if inf.is_dir(): continue
            raw = open(path, 'rb').read(); o = inf.header_offset; nl, xl = struct.unpack('<HH', raw[o + 26:o + 30])
            data = raw[o + 30 + nl + xl:o + 30 + nl + xl + inf.compress_size]
            ex = inf.extra; i = 0; strength = meth = ver = None
            while i + 4 <= len(ex):
                hid, sz = struct.unpack('<HH', ex[i:i + 4])
                if hid == 0x9901: ver, _, strength, meth = struct.unpack('<H2sBH', ex[i + 4:i + 11])
                i += 4 + sz
            assert inf.compress_type == 99 and strength == 3 and ver == 2, (inf.compress_type, strength, ver)
            salt, pv, enc, tag = data[:16], data[16:18], data[18:-10], data[-10:]
            dk = hashlib.pbkdf2_hmac('sha1', pw.encode(), salt, 1000, 66)
            assert dk[64:] == pv, 'verificador'
            assert hmac.new(dk[32:64], enc, 'sha1').digest()[:10] == tag, 'HMAC'
            ecb = Cipher(algorithms.AES(dk[:32]), modes.ECB()).encryptor(); out = bytearray()
            for k in range(0, len(enc), 16):
                ks = ecb.update((k // 16 + 1).to_bytes(16, 'little')); out += bytes(a ^ b for a, b in zip(enc[k:k + 16], ks))
            res[inf.filename] = zlib.decompress(bytes(out), -15) if meth == 8 else bytes(out)
    return res
try:
    files = wz_decrypt(z2, PW); bad = [n for n, d in files.items() if d != open(os.path.join(T, 'origen', n), 'rb').read()]
    check('AES: descifrado independiente (PBKDF2, CTR, HMAC) y contenido', not bad, bad[:3])
except Exception as e: check('AES: descifrado independiente', False, repr(e))
X = os.path.join(T, 'x2'); r, out = run('x', z2, PW, X); check('Alquimia abre su ZIP AES', r == 0 and same_tree(S, os.path.join(X, 'Carpeta ñandú')) == '', out)
r, out = run('x', z2, 'mala', os.path.join(T, 'x2m')); check('contrasena mala rechazada (%s)' % out, r != 0 and 'incorrecta' in out)
r, out = run('x', z2, '-', os.path.join(T, 'x2n')); check('sin contrasena: la pide (%s)' % out, r != 0 and 'contraseña' in out)

print('TAR, TAR.GZ y GZ creados por Alquimia')
for kind, name in (('tar', 'a.tar'), ('tgz', 'a.tar.gz')):
    p = os.path.join(T, name); r, out = run(kind, 2, '-', p, S); check('crear %s (%s)' % (name, out), r == 0)
    X = os.path.join(T, 'x' + kind); os.makedirs(X); r, out = sh('tar -xf "%s" -C "%s"' % (p, X)); check('GNU tar extrae %s' % name, r == 0, out)
    check('%s: contenido identico' % name, same_tree(S, os.path.join(X, 'Carpeta ñandú')) == '', same_tree(S, os.path.join(X, 'Carpeta ñandú')))
    with tarfile.open(p) as t: check('tarfile de Python lee %s' % name, any(len(m.name) > 100 for m in t.getmembers()))
    X = os.path.join(T, 'y' + kind); r, out = run('x', p, '-', X); check('Alquimia abre su %s' % name, r == 0 and same_tree(S, os.path.join(X, 'Carpeta ñandú')) == '', out)
p = os.path.join(T, 'texto.txt.gz'); r, out = run('gz', 3, '-', p, os.path.join(S, 'texto.txt')); check('crear GZ (%s)' % out, r == 0)
r, out = sh('gzip -t "%s"' % p); check('gzip -t', r == 0, out)
check('gzip: contenido', gzip.open(p).read() == open(os.path.join(S, 'texto.txt'), 'rb').read())
r, out = run('gz', 2, '-', os.path.join(T, 'mal.gz'), S); check('GZ de una carpeta: se niega', r != 0)

print('Archivos creados por otros programas')
z3 = os.path.join(T, 'infozip.zip'); sh('cd "%s/origen" && zip -qr -9 "%s" .' % (T, z3))
X = os.path.join(T, 'x3'); r, out = run('x', z3, '-', X); check('ZIP de Info-ZIP (%s)' % out, r == 0 and same_tree(S, os.path.join(X, 'Carpeta ñandú')) == '', same_tree(S, os.path.join(X, 'Carpeta ñandú')))
z4 = os.path.join(T, 'zipcrypto.zip'); sh('cd "%s/origen" && zip -qr -P secreto "%s" .' % (T, z4))
X = os.path.join(T, 'x4'); r, out = run('x', z4, 'secreto', X); check('ZIP ZipCrypto (%s)' % out, r == 0 and same_tree(S, os.path.join(X, 'Carpeta ñandú')) == '')
r, out = run('t', z4, 'otra'); check('ZipCrypto: contrasena mala (%s)' % out, r != 0)
z5 = os.path.join(T, 'python.zip')
with zipfile.ZipFile(z5, 'w', zipfile.ZIP_DEFLATED) as z:
    for root, ds, fs in os.walk(os.path.join(T, 'origen')):
        for f in fs + ds: fp = os.path.join(root, f); z.write(fp, os.path.relpath(fp, os.path.join(T, 'origen')))
X = os.path.join(T, 'x5'); r, out = run('x', z5, '-', X); check('ZIP de Python (%s)' % out, r == 0 and same_tree(S, os.path.join(X, 'Carpeta ñandú')) == '')
z6 = os.path.join(T, 'zip64.zip')
with zipfile.ZipFile(z6, 'w', zipfile.ZIP_DEFLATED, allowZip64=True) as z:
    with z.open('grande.txt', 'w', force_zip64=True) as w: w.write(b'zip64 ' * 10000)
r, out = run('x', z6, '-', os.path.join(T, 'x6')); check('ZIP64 (%s)' % out, r == 0 and open(os.path.join(T, 'x6', 'grande.txt'), 'rb').read() == b'zip64 ' * 10000)
for comp, ext in (('', 'tar'), ('z', 'tgz')):
    p = os.path.join(T, 'gnu.' + ext); sh('tar -c%sf "%s" -C "%s/origen" .' % (comp, p, T))
    X = os.path.join(T, 'xg' + ext); r, out = run('x', p, '-', X); check('GNU tar %s (%s)' % (ext, out), r == 0 and same_tree(S, os.path.join(X, 'Carpeta ñandú')) == '', same_tree(S, os.path.join(X, 'Carpeta ñandú')))
p = os.path.join(T, 'pax.tar'); sh('tar --format=pax -cf "%s" -C "%s/origen" .' % (p, T))
X = os.path.join(T, 'xpax'); r, out = run('x', p, '-', X); check('tar pax (%s)' % out, r == 0 and same_tree(S, os.path.join(X, 'Carpeta ñandú')) == '')
p = os.path.join(T, 'g.gz'); sh('gzip -c "%s" > "%s"' % (os.path.join(S, 'azar.bin'), p))
X = os.path.join(T, 'xgz'); r, out = run('x', p, '-', X); check('gzip (%s)' % out, r == 0 and open(os.path.join(X, 'azar.bin'), 'rb').read() == open(os.path.join(S, 'azar.bin'), 'rb').read())
bad = os.path.join(T, 'malo.zip'); sh('cp "%s" "%s"' % (z1, bad)); d = bytearray(open(bad, 'rb').read()); d[len(d) // 3] ^= 0x55; open(bad, 'wb').write(d)
r, out = run('t', bad, '-'); check('ZIP dañado: se detecta (%s)' % out, r != 0)
evil = os.path.join(T, 'evil.zip')
with zipfile.ZipFile(evil, 'w') as z: z.writestr('../../fuera.txt', 'no'); z.writestr('/abs/raiz.txt', 'no'); z.writestr('ok:raro?.txt', 'si')
X = os.path.join(T, 'xevil'); r, out = run('x', evil, '-', X)
check('ZIP con rutas peligrosas: todo queda dentro', r == 0 and not os.path.exists(os.path.join(T, 'fuera.txt')) and os.path.isfile(os.path.join(X, 'fuera.txt')) and os.path.isfile(os.path.join(X, 'abs', 'raiz.txt')) and os.path.isfile(os.path.join(X, 'ok_raro_.txt')), out)

if '--solo-archivos' not in sys.argv and os.path.exists(os.path.join(os.path.dirname(__file__), 'verifica_conv.py')):
    sys.argv = [sys.argv[0], B, T]; g = {'check': check, 'run': run, 'sh': sh, 'T': T, '__file__': __file__}
    exec(open(os.path.join(os.path.dirname(__file__), 'verifica_conv.py'), encoding='utf-8').read(), g)
print('\n%d bien, %d fallos' % (ok, fail)); sys.exit(1 if fail else 0)
