#!/usr/bin/env python3
"""Lee y escribe archivos en la particion FAT de una imagen de ZunzunOS (build/zunzun.img) sin montarla.
En el Mac usa hdiutil; en Linux, mtools (apt install mtools). Las rutas son relativas a la raiz de la
particion, que en ZunzunOS es /sys: 'DOCS/nota.txt' es /sys/DOCS/nota.txt.
  put(img, {'DOCS/nota.txt': 'texto', ...})   get(img, 'DOCS/impreso.qpdl') -> bytes o None"""
import os, shutil, struct, subprocess, sys, tempfile

def _off(img):
    d = open(img, 'rb').read(512); return struct.unpack('<I', d[446 + 8:446 + 12])[0] * 512

def _mac(): return sys.platform == 'darwin' and shutil.which('hdiutil')

def _env(): e = dict(os.environ); e['MTOOLS_SKIP_CHECK'] = '1'; return e

def _attach(img, ro):
    a = subprocess.run(['hdiutil', 'attach'] + (['-readonly'] if ro else []) + ['-imagekey', 'diskimage-class=CRawDiskImage', '-mountrandom', '/tmp', img], capture_output=True, text=True, timeout=60).stdout
    return a.split()[0], [l.split('\t')[-1].strip() for l in a.splitlines() if '/tmp' in l][0]

def put(img, files):
    if _mac():
        dev, mnt = _attach(img, False)
        try:
            for n, d in files.items():
                f = os.path.join(mnt, n); os.makedirs(os.path.dirname(f), exist_ok=True); open(f, 'wb').write(d.encode() if isinstance(d, str) else d)
            for x in os.listdir(mnt):
                if x.startswith('.'): subprocess.run(['rm', '-rf', os.path.join(mnt, x)])
        finally: subprocess.run(['hdiutil', 'detach', dev], capture_output=True)
        return
    im = f'{img}@@{_off(img)}'; tmp = tempfile.mkdtemp()
    try:
        for n, d in files.items():
            parts = n.split('/')
            for i in range(1, len(parts)): subprocess.run(['mmd', '-D', 's', '-i', im, '::/' + '/'.join(parts[:i])], env=_env(), capture_output=True)
            src = os.path.join(tmp, 'f'); open(src, 'wb').write(d.encode() if isinstance(d, str) else d)
            subprocess.run(['mcopy', '-o', '-i', im, src, '::/' + n], env=_env(), check=True, capture_output=True)
    finally: shutil.rmtree(tmp)

def get(img, path):
    if _mac():
        dev, mnt = _attach(img, True)
        try:
            f = os.path.join(mnt, path); return open(f, 'rb').read() if os.path.exists(f) else None
        finally: subprocess.run(['hdiutil', 'detach', dev], capture_output=True)
    r = subprocess.run(['mtype', '-i', f'{img}@@{_off(img)}', '::/' + path], env=_env(), capture_output=True)
    return r.stdout if r.returncode == 0 else None

def ls(img, path):
    """nombres de una carpeta (lista vacia si no existe)"""
    if _mac():
        dev, mnt = _attach(img, True)
        try:
            f = os.path.join(mnt, path); return sorted(os.listdir(f)) if os.path.isdir(f) else []
        finally: subprocess.run(['hdiutil', 'detach', dev], capture_output=True)
    r = subprocess.run(['mdir', '-b', '-i', f'{img}@@{_off(img)}', '::/' + path], env=_env(), capture_output=True, text=True)
    return sorted(os.path.basename(l.rstrip('/')) for l in r.stdout.splitlines() if l.strip()) if r.returncode == 0 else []
