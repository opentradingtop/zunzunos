#!/usr/bin/env python3
"""ZunzunOS para la Raspberry Pi 2.

  mkpi.py kernel7 kernel.elf KERNEL.BIN INITRD.BIN kernel7.img
      kernel7.img = kernel + ceros hasta el final del BSS + cabecera "ZZINITRD" (16 bytes) + initrd.
      El firmware de la Pi lo carga entero en 0x8000; el kernel encuentra el initrd detras de __kend.

  mkpi.py sd kernel7.img DIR_FIRMWARE sd.img BYTES
      Tarjeta SD: tabla MBR con una particion FAT32 (tipo 0x0C) desde el sector 8192 hasta el final.
      En ella: bootcode.bin, start.elf y fixup.dat (del firmware, en DIR_FIRMWARE, no son nuestros),
      config.txt, kernel7.img, LEEME.TXT y la carpeta ZUNZUN. La etiqueta es ZUNZUN y el numero de serie
      el de todos los FAT de ZunzunOS (0x5A554E5A), asi el kernel monta la tarjeta como /sys.
"""
import os, struct, sys, time

def elf_symbol(path, name):
    d = open(path, 'rb').read()
    assert d[:4] == b'\x7fELF' and d[4] == 1, 'se esperaba un ELF de 32 bits'
    shoff, = struct.unpack_from('<I', d, 0x20)
    shentsize, shnum = struct.unpack_from('<HH', d, 0x2E)
    secs = [struct.unpack_from('<IIIIIIIIII', d, shoff + i * shentsize) for i in range(shnum)]
    for s in secs:
        if s[1] != 2:   # SHT_SYMTAB
            continue
        strtab = secs[s[6]]
        for o in range(s[4], s[4] + s[5], 16):
            nm, val = struct.unpack_from('<II', d, o)
            e = d.index(b'\0', strtab[4] + nm)
            if d[strtab[4] + nm:e].decode() == name:
                return val
    raise SystemExit(f'no encuentro {name} en {path}')

def kernel7(elf, kbin, initrd, out):
    kend = elf_symbol(elf, '__kend') - 0x80008000
    k = open(kbin, 'rb').read()
    assert len(k) <= kend, 'el kernel es mayor que __kend'
    ird = open(initrd, 'rb').read()
    img = k + bytes(kend - len(k)) + b'ZZINITRD' + struct.pack('<II', len(ird), 0) + ird
    open(out, 'wb').write(img)
    print(f'{out}: kernel {len(k)} bytes (BSS hasta {kend}), initrd {len(ird)} bytes, total {len(img)} bytes')

# ---------- FAT32 ----------
SERIAL = 0x5A554E5A
def short_name(n):
    base, _, ext = n.upper().partition('.')
    return (base[:8].ljust(8) + ext[:3].ljust(3)).encode('ascii')

def dos_time():
    t = time.localtime()
    return ((t.tm_hour << 11) | (t.tm_min << 5) | (t.tm_sec // 2)), (((t.tm_year - 1980) << 9) | (t.tm_mon << 5) | t.tm_mday)

class Fat32:
    def __init__(self, sectors):
        gb = sectors * 512 / (1 << 30)
        want = 8 if gb <= 8 else 16 if gb <= 16 else 32 if gb <= 32 else 64   # 4 KB hasta 8 GB, luego mayores
        self.spc = 1
        for p in (64, 32, 16, 8, 4, 2, 1):   # el mayor que deje al menos 65525 clusters (si no, no es FAT32)
            if p <= want and sectors // p >= 66000: self.spc = p; break
        self.rsv = 32
        n = sectors
        fatsz = 1
        while True:
            clusters = (n - self.rsv - 2 * fatsz) // self.spc
            need = ((clusters + 2) * 4 + 511) // 512
            if need <= fatsz: break
            fatsz = need
        self.fatsz, self.sectors, self.nclus = fatsz, sectors, clusters
        assert clusters >= 65525, 'particion demasiado pequena para FAT32 (minimo unos 260 MB)'
        self.fat = [0x0FFFFFF8, 0x0FFFFFFF]
        self.data = {}   # cluster -> bytes
        self.root = self.new_dir(None)
    def alloc(self, n):
        first = len(self.fat)
        for i in range(n):
            self.fat.append(first + i + 1 if i < n - 1 else 0x0FFFFFFF)
        return first
    def csize(self): return self.spc * 512
    def write_chain(self, first, blob):
        cs = self.csize()
        for i in range(0, max(len(blob), 1), cs):
            self.data[first + i // cs] = blob[i:i + cs]
    def new_dir(self, parent):
        d = {'entries': [], 'first': None, 'parent': parent}
        return d
    def add(self, d, name, content=None, is_dir=False):
        ent = {'name': name, 'dir': is_dir, 'content': content, 'sub': self.new_dir(d) if is_dir else None}
        d['entries'].append(ent)
        return ent['sub'] if is_dir else ent
    def lfn_entries(self, name, sn):
        csum = 0
        for c in sn: csum = ((csum >> 1) | ((csum & 1) << 7)) + c & 0xFF
        u = name.encode('utf-16-le') + b'\0\0'
        u += b'\xff' * (-len(u) % 26)
        parts = [u[i:i + 26] for i in range(0, len(u), 26)]
        out = []
        for i, p in enumerate(parts):
            seq = (i + 1) | (0x40 if i == len(parts) - 1 else 0)
            out.append(bytes([seq]) + p[:10] + bytes([0x0F, 0, csum]) + p[10:22] + b'\0\0' + p[22:26])
        return list(reversed(out))
    def layout(self, d, is_root):
        # primero los hijos, para conocer sus clusters
        tm, dt = dos_time()
        for e in d['entries']:
            if e['dir']:
                continue
            data = e['content']
            e['first'] = self.alloc((len(data) + self.csize() - 1) // self.csize()) if data else 0
            if data: self.write_chain(e['first'], data)
        nent = (0 if is_root else 2) + (1 if is_root else 0) + sum(1 + len(self.lfn_entries(e['name'], short_name(e['name']))) for e in d['entries'])
        nclus = max(1, (nent * 32 + self.csize() - 1) // self.csize())
        d['first'] = self.alloc(nclus)
        for e in d['entries']:
            if e['dir']:
                self.layout(e['sub'], False)
                e['first'] = e['sub']['first']
        raw = bytearray()
        if is_root:
            raw += b'ZUNZUN     ' + bytes([0x08]) + bytes(10) + struct.pack('<HHHI', tm, dt, 0, 0)
        else:
            par = d['parent']['first'] if d['parent'] and d['parent'] is not self.root else 0
            raw += b'.          ' + bytes([0x10, 0, 0]) + struct.pack('<HHHHHHHI', tm, dt, dt, d['first'] >> 16, tm, dt, d['first'] & 0xFFFF, 0)
            raw += b'..         ' + bytes([0x10, 0, 0]) + struct.pack('<HHHHHHHI', tm, dt, dt, par >> 16, tm, dt, par & 0xFFFF, 0)
        used = set()
        for e in d['entries']:
            sn = short_name(e['name']); k = 1
            while sn in used:
                b = e['name'].upper().split('.')[0][:6] + '~' + str(k); sn = (b[:8].ljust(8) + short_name(e['name'])[8:].decode()).encode(); k += 1
            used.add(sn)
            simple = e['name'] == e['name'].upper() and len(e['name'].split('.')[0]) <= 8 and short_name(e['name']).decode().strip() == e['name'].replace('.', '').strip()
            if not simple:
                for l in self.lfn_entries(e['name'], sn): raw += l
            size = 0 if e['dir'] else len(e['content'] or b'')
            raw += sn + bytes([0x10 if e['dir'] else 0x20, 0, 0]) + struct.pack('<HHHHHHHI', tm, dt, dt, e['first'] >> 16, tm, dt, e['first'] & 0xFFFF, size)
        self.write_chain(d['first'], bytes(raw))
    def image(self, label=b'ZUNZUN     '):
        self.layout(self.root, True)
        assert len(self.fat) <= self.nclus + 2, 'no cabe'
        bs = bytearray(512)
        bs[0:3] = b'\xEB\x58\x90'; bs[3:11] = b'ZUNZUNOS'
        struct.pack_into('<HBHBHHBHHHII', bs, 11, 512, self.spc, self.rsv, 2, 0, 0, 0xF8, 0, 63, 255, 8192, self.sectors)
        struct.pack_into('<IHHIHH', bs, 36, self.fatsz, 0, 0, self.root['first'], 1, 6)
        bs[64] = 0x80; bs[66] = 0x29; struct.pack_into('<I', bs, 67, SERIAL); bs[71:82] = label; bs[82:90] = b'FAT32   '
        bs[510:512] = b'\x55\xAA'
        fsi = bytearray(512)
        struct.pack_into('<I', fsi, 0, 0x41615252); struct.pack_into('<I', fsi, 484, 0x61417272)
        struct.pack_into('<II', fsi, 488, self.nclus + 2 - len(self.fat), len(self.fat)); fsi[510:512] = b'\x55\xAA'
        fat = bytearray(self.fatsz * 512)
        for i, v in enumerate(self.fat): struct.pack_into('<I', fat, i * 4, v)
        return bs, fsi, fat

def sd(kimg, fwdir, out, nbytes):
    nbytes = int(nbytes)
    need = ['bootcode.bin', 'start.elf', 'fixup.dat']
    miss = [n for n in need if not os.path.isfile(os.path.join(fwdir, n))]
    if miss:
        raise SystemExit(f'faltan los archivos del firmware de la Raspberry Pi en {fwdir}: {", ".join(miss)}\n'
                         '(son de Raspberry Pi Ltd, no de ZunzunOS; ver docs/RASPBERRY.md)')
    root = os.path.dirname(os.path.abspath(__file__))
    pstart = 8192
    psecs = nbytes // 512 - pstart
    f = Fat32(psecs)
    for n in need:
        f.add(f.root, n, open(os.path.join(fwdir, n), 'rb').read())
    f.add(f.root, 'config.txt', open(os.path.join(root, 'pi', 'config.txt'), 'rb').read())
    f.add(f.root, 'kernel7.img', open(kimg, 'rb').read())
    f.add(f.root, 'LEEME.TXT', open(os.path.join(root, '..', 'docs', 'LEEME.TXT'), 'rb').read())
    lic = os.path.join(fwdir, 'LICENCE.broadcom')
    if os.path.isfile(lic): f.add(f.root, 'LICENCE.broadcom', open(lic, 'rb').read())
    z = f.add(f.root, 'ZUNZUN', is_dir=True)
    f.add(z, 'LEEME-PI.TXT', open(os.path.join(root, 'pi', 'LEEME-PI.TXT'), 'rb').read())
    bs, fsi, fat = f.image()
    with open(out, 'wb') as o:
        o.truncate(nbytes)
        mbr = bytearray(512)
        struct.pack_into('<B3sB3sII', mbr, 446, 0x80, b'\xfe\xff\xff', 0x0C, b'\xfe\xff\xff', pstart, psecs)
        struct.pack_into('<I', mbr, 440, 0x5A554E5A); mbr[510:512] = b'\x55\xAA'
        o.seek(0); o.write(mbr)
        base = pstart * 512
        for copy in (0, 6):
            o.seek(base + copy * 512); o.write(bs)
            o.seek(base + (copy + 1) * 512); o.write(fsi)
        for k in range(2):
            o.seek(base + (f.rsv + k * f.fatsz) * 512); o.write(fat)
        dstart = base + (f.rsv + 2 * f.fatsz) * 512
        for c, blob in f.data.items():
            o.seek(dstart + (c - 2) * f.csize()); o.write(blob)
    print(f'{out}: {nbytes // (1 << 20)} MB, FAT32 ZUNZUN desde el sector {pstart}, {len(f.fat) - 2} clusters usados de {f.nclus}')

if __name__ == '__main__':
    a = sys.argv[1:]
    if a[:1] == ['kernel7'] and len(a) == 5: kernel7(*a[1:])
    elif a[:1] == ['sd'] and len(a) == 5: sd(*a[1:])
    else: raise SystemExit(__doc__)
