# Pruebas del conversor de Alquimia (las carga verifica.py, que da check, run, sh y T)
import os, json, csv, io, zipfile, hashlib, base64, struct, subprocess, tarfile, wave, math
import xml.etree.ElementTree as ET
from PIL import Image
import numpy as np
C = os.path.join(T, 'conv'); os.makedirs(C, exist_ok=True)
def out(name): return os.path.join(C, name)
def conv(src, fmt, dst, *opts):
    r, o = run('conv', src, fmt, dst, *opts); return r == 0, o
def diff(a, b):
    a = np.asarray(a.convert('RGB'), dtype=np.int32); b = np.asarray(b.convert('RGB'), dtype=np.int32)
    return 999 if a.shape != b.shape else float(np.abs(a - b).mean())

print('Imagenes')
W, H = 173, 117   # tamanos impares a proposito
y, x = np.mgrid[0:H, 0:W]
arr = np.stack([(x * 255 // W), (y * 255 // H), ((x + y) * 3 % 256)], -1).astype(np.uint8); arr[20:50, 30:90] = [200, 30, 40]
base = Image.fromarray(arr)
srcs = {}
for ext, kw in (('png', {}), ('jpg', {'quality': 95}), ('bmp', {}), ('gif', {}), ('ppm', {}), ('tga', {}), ('ico', {'sizes': [(64, 64)]})):
    p = out('base.' + ext); (base.convert('P', palette=Image.ADAPTIVE) if ext == 'gif' else base).save(p, **kw); srcs[ext] = p
ref = {e: Image.open(p).convert('RGB') for e, p in srcs.items()}
for ie, ip in srcs.items():
    for oe in ('png', 'jpg', 'bmp', 'gif', 'ico', 'tga', 'ppm', 'pdf', 'svg'):
        dst = out('i-%s.%s' % (ie, oe)); good, o = conv(ip, oe, dst)
        msg = o
        if good:
            try:
                if oe == 'pdf': r2, info = sh('pdfinfo "%s"' % dst); good = r2 == 0 and any(l.split() == ['Pages:', '1'] for l in info.split('\n')); msg = [l for l in info.split('\n') if 'size' in l][:1]
                elif oe == 'svg':
                    t = ET.parse(dst).getroot(); href = [v for k, v in t[0].attrib.items() if k.endswith('href')][0]
                    im = Image.open(io.BytesIO(base64.b64decode(href.split(',', 1)[1]))); good = diff(im, ref[ie]) < (4 if ie == 'jpg' else 0.5)
                elif oe == 'ico':
                    im = Image.open(dst); good = (16, 16) in im.info.get('sizes', set()) and (32, 32) in im.info['sizes']; msg = str(sorted(im.info['sizes']))
                else:
                    im = Image.open(dst); im.load(); d = diff(im, ref[ie]); lim = {'jpg': 6, 'gif': 9}.get(oe, 4 if ie == 'jpg' else 0.5)   # el JPEG de ZunzunOS y el de PIL redondean distinto
                    good = d < lim; msg = 'dif %.2f %s' % (d, im.size)
            except Exception as e: good = False; msg = repr(e)
        check('imagen %s -> %s (%s)' % (ie, oe, msg), good)
good, o = conv(srcs['png'], 'png', out('mitad.png'), 'pct', '50'); im = Image.open(out('mitad.png')); check('reducir al 50 %% %s' % (im.size,), good and im.size == (86, 58))
good, o = conv(srcs['png'], 'jpg', out('caja.jpg'), 'ancho', '400'); im = Image.open(out('caja.jpg')); check('caja de 400: no amplia %s' % (im.size,), im.size == (W, H))
good, o = conv(srcs['png'], 'png', out('grande.png'), 'pct', '300'); im = Image.open(out('grande.png')); check('ampliar al 300 %% %s' % (im.size,), im.size == (519, 351) and diff(im.resize((W, H), Image.BILINEAR), ref['png']) < 6)
good, o = conv(srcs['png'], 'png', out('girada.png'), 'girar', '90'); im = Image.open(out('girada.png')); check('girar 90', good and im.size == (H, W) and diff(im, ref['png'].transpose(Image.ROTATE_270)) < 0.5)
good, o = conv(srcs['png'], 'png', out('gris.png'), 'gris', '1'); a = np.asarray(Image.open(out('gris.png')).convert('RGB')); check('escala de grises', good and (a[..., 0] == a[..., 1]).all() and (a[..., 1] == a[..., 2]).all())
foto = out('foto.png'); Image.fromarray((np.random.RandomState(3).rand(240, 320, 3) * 64 + arr[:1, :1] * 0 + np.mgrid[0:240, 0:320][1][..., None] * 0.6).clip(0, 255).astype(np.uint8)).save(foto)
good, o = conv(foto, 'gif', out('foto.gif')); im = Image.open(out('foto.gif')); check('GIF de una foto con miles de colores (dif %.2f)' % diff(im, Image.open(foto)), good and diff(im, Image.open(foto)) < 10)
r, o = sh('ffmpeg -v error -i "%s" -f null -' % out('foto.gif')); check('ffmpeg lee el GIF', r == 0, o)

print('Sonido')
rate = 44100; t = np.arange(rate * 2) / rate; L = (np.sin(2 * np.pi * 440 * t) * 0.5); R = (np.sin(2 * np.pi * 660 * t) * 0.3)
pcm = (np.stack([L, R], -1) * 32767).astype('<i2')
w = wave.open(out('tono.wav'), 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate); w.writeframes(pcm.tobytes()); w.close()
sh('ffmpeg -v error -y -i "%s" -c:a pcm_s24le "%s"' % (out('tono.wav'), out('tono24.wav'))); sh('ffmpeg -v error -y -i "%s" -c:a pcm_f32le "%s"' % (out('tono.wav'), out('tonof.wav')))
sh('ffmpeg -v error -y -i "%s" -b:a 192k "%s"' % (out('tono.wav'), out('tono.mp3'))); sh('ffmpeg -v error -y -i "%s" "%s"' % (out('tono.wav'), out('tono.aiff')))
def probe(p):
    r, o = sh('ffprobe -v error -show_entries stream=sample_rate,channels,codec_name -show_entries format=duration -of json "%s"' % p)
    j = json.loads(o) if r == 0 else {}; s = j.get('streams', [{}])[0]; return s.get('codec_name'), int(s.get('sample_rate', 0)), s.get('channels'), float(j.get('format', {}).get('duration', 0))
def decode(p, ch=2):
    r = subprocess.run(['ffmpeg', '-v', 'error', '-i', p, '-f', 's16le', '-ac', str(ch), '-ar', '44100', '-'], capture_output=True); return np.frombuffer(r.stdout, '<i2').reshape(-1, ch).astype(float)
def corr(a, b, lag=0):
    n = min(len(a), len(b)) - 3000; best = 0
    for k in range(0, 2400, 1) if lag else [0]:
        x = a[1000:1000 + n - 3000, 0]; yy = b[1000 + k:1000 + k + n - 3000, 0]
        if len(x) != len(yy) or not len(x): continue
        c = np.corrcoef(x, yy)[0, 1]; best = max(best, c)
        if best > 0.99: break
    return best
orig = decode(out('tono.wav'))
for src in ('tono.wav', 'tono24.wav', 'tonof.wav', 'tono.mp3', 'tono.aiff'):
    for fmt in ('wav', 'mp3', 'aiff'):
        dst = out('a-%s.%s' % (src.replace('.', '_'), fmt)); good, o = conv(out(src), fmt, dst)
        if good:
            cn, sr, chn, du = probe(dst); dec = decode(dst); c = corr(orig, dec, lag=fmt == 'mp3' or src.endswith('mp3'))
            good = sr == 44100 and chn == 2 and abs(du - 2.0) < (0.15 if src.endswith('mp3') and fmt == 'mp3' else 0.08) and c > 0.98; o = '%s %d Hz %s canales %.2f s corr %.3f' % (cn, sr, chn, du, c)
        check('sonido %s -> %s (%s)' % (src, fmt, o), good)
good, o = conv(out('tono.wav'), 'wav', out('mono22.wav'), 'canales', '1', 'hz', '22050'); cn, sr, chn, du = probe(out('mono22.wav')); check('a mono y 22050 Hz (%s %s %.2f)' % (sr, chn, du), good and sr == 22050 and chn == 1 and abs(du - 2) < 0.01)
good, o = conv(out('tono.wav'), 'mp3', out('k128.mp3'), 'kbps', '128', 'canales', '1'); r, o2 = sh('ffprobe -v error -show_entries stream=bit_rate,channels -of csv=p=0 "%s"' % out('k128.mp3')); check('MP3 mono a 128 kbps (%s)' % o2, good and o2.startswith('1,128'))
good, o = conv(out('tono.wav'), 'wav', out('norm.wav'), 'normalizar', '1'); pk = np.abs(decode(out('norm.wav'))).max(); check('normalizar al pico (-1 dBFS: %.0f)' % pk, good and 29000 < pk < 29300)

print('Video')
avi = out('clip.avi'); sh('ffmpeg -v error -y -f lavfi -i testsrc=size=320x240:rate=15 -f lavfi -i sine=frequency=500:sample_rate=48000 -t 2 -c:v mjpeg -q:v 4 -c:a pcm_s16le -ac 2 "%s"' % avi)
good, o = conv(avi, 'gif', out('clip.gif')); im = Image.open(out('clip.gif')) if good else None
check('AVI -> GIF animado (%s)' % (o if not good else '%d fotogramas %s' % (im.n_frames, im.size)), good and im.n_frames >= 18 and im.size == (320, 240))
good, o = conv(avi, 'gif', out('clip5.gif'), 'gif_fps', '5', 'gif_ancho', '160'); im = Image.open(out('clip5.gif')) if good else None; check('GIF a 5 fps y 160 px (%s)' % (str((im.n_frames, im.size, im.info.get('duration'))) if im else o), good and 9 <= im.n_frames <= 11 and im.size == (160, 120) and im.info.get('duration') == 200)
good, o = conv(avi, 'frames', out('clip-f.zip')); z = zipfile.ZipFile(out('clip-f.zip')) if good else None; check('AVI -> ZIP de fotogramas', good and z.testzip() is None and len(z.namelist()) == 30 and Image.open(z.open(z.namelist()[3])).size == (320, 240), o)
good, o = conv(avi, 'png', out('clip.png')); check('AVI -> PNG', good and Image.open(out('clip.png')).size == (320, 240), o)
for fmt in ('wav', 'mp3'):
    good, o = conv(avi, fmt, out('clip-a.' + fmt)); cn, sr, chn, du = probe(out('clip-a.' + fmt)) if good else (0, 0, 0, 0); check('AVI -> %s (%s %s %.2f)' % (fmt, sr, chn, du), good and sr == 48000 and abs(du - 2) < 0.1, o)

print('Documentos')
md = out('libro.md'); open(md, 'w', encoding='utf-8').write('''# El zunzún

Un **colibrí** cubano que pesa menos que una moneda. Cañón, pingüino, Straße y €.

## Lista de la compra

- Café
- Azúcar de caña
- Pan

1. Primero
2. Segundo

> Las cosas pequeñas también vuelan.

```
codigo   con  espacios
```

# Segundo capítulo

Texto del segundo capítulo con [un enlace](https://tonyfluxa.top).
''')
import docx as pdocx
dx = out('word.docx'); dd = pdocx.Document(); dd.add_heading('Informe técnico', 1); dd.add_paragraph('Párrafo normal con acentos: árbol, ñu.'); dd.add_paragraph('Elemento uno', style='List Bullet'); dd.add_heading('Conclusión', 2); dd.add_paragraph('Fin.'); dd.save(dx)
html = out('pagina.html'); open(html, 'w', encoding='utf-8').write('<html><head><title>Página</title><style>p{}</style></head><body><h1>Hola &amp; adiós</h1><p>Texto <b>con</b> negrita&nbsp;y &eacute;.</p><ul><li>uno</li><li>dos</li></ul><script>var x=1;</script></body></html>')
rtf = out('carta.rtf'); open(rtf, 'w').write(r'{\rtf1\ansi{\fonttbl{\f0 Times;}}\f0 Querido amigo:\par Te escribo desde Hamburgo, \u241?and\u250? incluido.\par}')
def words_of(p):
    ext = p.rsplit('.', 1)[1]
    if ext == 'pdf': return sh('pdftotext -enc UTF-8 "%s" -' % p)[1]
    if ext == 'docx': return '\n'.join(x.text for x in pdocx.Document(p).paragraphs)
    if ext in ('odt', 'epub'):
        z = zipfile.ZipFile(p); assert z.testzip() is None; assert z.namelist()[0] == 'mimetype' and z.getinfo('mimetype').compress_type == 0
        txt = ''.join(z.read(n).decode() for n in z.namelist() if n.endswith(('content.xml', '.xhtml')))
        return ET.tostring(ET.fromstring(txt), method='text', encoding='unicode') if ext == 'odt' else txt
    return open(p, encoding='utf-8', errors='replace').read()
docs = {md: ['zunzún', 'Cañón', 'Azúcar de caña', 'Segundo capítulo', 'un enlace'], dx: ['Informe técnico', 'árbol', 'Elemento uno', 'Conclusión'], html: ['Hola & adiós', 'negrita', 'dos'], rtf: ['Querido amigo', 'Hamburgo', 'ñandú']}
for src, ws in docs.items():
    for fmt in ('pdf', 'docx', 'odt', 'epub', 'html', 'md', 'rtf', 'txt'):
        dst = out('d-%s.%s' % (os.path.basename(src).replace('.', '_'), fmt)); good, o = conv(src, fmt, dst)
        if good:
            try:
                if fmt == 'rtf': rr, txt = run('conv', dst, 'txt', dst + '.txt'); txt = open(dst + '.txt', encoding='utf-8').read()
                else: txt = words_of(dst)
                if fmt in ('html', 'epub'): txt = txt.replace('&amp;', '&')
                miss = [w for w in ws if w not in txt]; good = not miss; o = 'faltan %s' % miss if miss else ''
            except Exception as e: good = False; o = repr(e)
        check('documento %s -> %s %s' % (os.path.basename(src), fmt, o), good)
good, o = conv(md, 'docx', out('estilos.docx')); d2 = pdocx.Document(out('estilos.docx')); st = [p.style.name for p in d2.paragraphs]
check('DOCX: títulos y listas con estilo (%s)' % st[:4], good and st[0].lower() == 'heading 1' and 'List Bullet' in st and 'heading 2' in [x.lower() for x in st])
good, o = conv(md, 'epub', out('cap.epub')); z = zipfile.ZipFile(out('cap.epub')); check('EPUB: un capítulo por cada título 1', good and 'OEBPS/cap002.xhtml' in z.namelist() and 'OEBPS/nav.xhtml' in z.namelist())
for f in ('d-libro_md.epub', 'd-libro_md.odt'):
    good, o = conv(out(f), 'md', out(f + '.md')); txt = open(out(f + '.md'), encoding='utf-8').read() if good else ''
    check('vuelta: %s -> Markdown conserva títulos y listas' % f, good and '# El zunzún' in txt and '- Café' in txt and '## Lista de la compra' in txt, txt[:200])
r, o = sh('file "%s"' % out('d-libro_md.rtf')); check('file reconoce el RTF', 'Rich Text' in o, o)

print('Datos')
rows = [['nombre', 'ciudad', 'edad', 'nota'], ['Ana', 'Hamburgo', '41', 'dice "hola"'], ['José, el de Cádiz', 'Cádiz', '35', 'línea 1\nlínea 2'], ['Ünal', 'Köln', '29', ''], ['Zoë', 'São Paulo', '3.5', 'x;y']]
cs = out('tabla.csv'); csv.writer(open(cs, 'w', newline='', encoding='utf-8')).writerows(rows)
sc = out('tabla-pc.csv'); csv.writer(open(sc, 'w', newline='', encoding='utf-8'), delimiter=';').writerows(rows)
def table_of(p):
    ext = p.rsplit('.', 1)[1]
    if ext == 'xlsx':
        import openpyxl; ws = openpyxl.load_workbook(p).active; return [[('' if c is None else (str(int(c)) if isinstance(c, (int, float)) and float(c).is_integer() and not isinstance(c, bool) else str(c))) for c in r] for r in ws.iter_rows(values_only=True)]
    if ext in ('csv', 'tsv'): return [r for r in csv.reader(open(p, newline='', encoding='utf-8'), delimiter='\t' if ext == 'tsv' else ',')]
    if ext == 'json': j = json.load(open(p, encoding='utf-8')); k = list(j[0].keys()); return [k] + [[('' if r.get(x) is None else (str(r[x]) if not isinstance(r[x], float) else repr(r[x]))) for x in k] for r in j]
    if ext == 'yaml':
        import yaml; j = yaml.safe_load(open(p, encoding='utf-8')); k = list(j[0].keys()); return [k] + [['' if r.get(x) is None else str(r[x]) for x in k] for r in j]
    if ext == 'xml':
        rt = ET.parse(p).getroot(); k = [c.tag for c in rt[0]]; return [k] + [[(c.text or '') for c in row] for row in rt]
src_tab = [r for r in rows]
for src in (cs, sc):
    for fmt in ('xlsx', 'csv', 'tsv', 'json', 'xml', 'yaml', 'html', 'md', 'txt'):
        dst = out('t-%s.%s' % (os.path.basename(src).replace('.', '_'), fmt)); good, o = conv(src, fmt, dst)
        if good and fmt in ('xlsx', 'csv', 'json', 'xml', 'yaml'):
            try:
                tb = table_of(dst); exp = [[c.replace('\n', ' ') if fmt == 'tsv' else c for c in r] for r in src_tab]
                if fmt == 'xml': exp[0] = ['nombre', 'ciudad', 'edad', 'nota']
                good = tb == exp; o = '' if good else str(tb)[:300]
            except Exception as e: good = False; o = repr(e)
        elif good and fmt == 'tsv': good = open(dst, encoding='utf-8', newline='').read().split('\r\n')[2].split('\t')[3] == 'línea 1 línea 2'
        elif good: txt = open(dst, encoding='utf-8').read(); good = 'José, el de Cádiz' in txt and 'São Paulo' in txt
        check('datos %s -> %s %s' % (os.path.basename(src), fmt, o), good)
for fmt in ('xlsx', 'json', 'xml', 'yaml', 'tsv'):
    src = out('t-tabla_csv.' + fmt); dst = out('vuelta-' + fmt + '.csv'); good, o = conv(src, 'csv', dst)
    exp = [[c.replace('\n', ' ') if fmt == 'tsv' else c for c in r] for r in src_tab]
    check('vuelta %s -> CSV' % fmt, good and table_of(dst) == exp, o if not good else str(table_of(dst))[:200])
import openpyxl
wb = openpyxl.Workbook(); ws = wb.active; ws.append(['producto', 'precio', 'activo']); ws.append(['tornillo', 0.25, True]); ws.append(['tuerca', 3, False]); ws['E5'] = 'suelto'; wb.save(out('excel.xlsx'))
good, o = conv(out('excel.xlsx'), 'csv', out('excel.csv')); tb = table_of(out('excel.csv')) if good else []
check('XLSX de openpyxl -> CSV (%s)' % tb, good and tb[0][:3] == ['producto', 'precio', 'activo'] and tb[1][:3] == ['tornillo', '0.25', 'true'] and tb[4][4] == 'suelto')

print('Codificaciones y archivos comprimidos')
blob = os.urandom(5000); bp = out('blob.bin'); open(bp, 'wb').write(blob)
good, o = conv(bp, 'base64', out('blob.b64')); check('Base64', good and base64.b64decode(open(out('blob.b64')).read()) == blob)
good, o = conv(out('blob.b64'), 'decode', out('blob-vuelta.bin')); check('decodificar Base64', good and open(out('blob-vuelta.bin'), 'rb').read() == blob, o)
good, o = conv(srcs['png'], 'datauri', out('uri.txt')); u = open(out('uri.txt')).read().strip(); check('data URI', good and u.startswith('data:image/png;base64,') and base64.b64decode(u.split(',')[1]) == open(srcs['png'], 'rb').read())
good, o = conv(bp, 'sha256', out('blob.sha')); check('SHA-256', good and open(out('blob.sha')).read().split()[0] == hashlib.sha256(blob).hexdigest())
good, o = conv(bp, 'hex', out('blob.hex')); l0 = open(out('blob.hex')).readline(); check('volcado HEX', good and l0.startswith('00000000  ' + ' '.join('%02x' % b for b in blob[:8])), l0)
good, o = conv(os.path.join(T, 'a.zip'), 'tgz', out('re.tar.gz')); check('ZIP -> TAR.GZ', good and len(tarfile.open(out('re.tar.gz')).getnames()) > 8, o)
good, o = conv(os.path.join(T, 'aes.zip'), 'zip', out('re.zip'), 'clave', 'Hamburg§2026!'); check('ZIP AES -> ZIP sin clave', good and zipfile.ZipFile(out('re.zip')).testzip() is None, o)
good, o = conv(os.path.join(T, 'aes.zip'), 'zip', out('re2.zip')); check('ZIP AES sin clave: la pide', not good and 'contraseña' in o, o)
