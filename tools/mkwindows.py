#!/usr/bin/env python3
"""Genera un disco de Windows sintetico para probar Forense (no hay un Windows averiado real a mano).

Disco GPT con particion EFI (FAT), particion reservada de Microsoft y NTFS con un Windows 11.

El NTFS cubre lo que Forense tiene que saber leer:
- $MFT fragmentada y $UpCase de verdad;
- carpetas grandes con indices en arbol de varios niveles (bloques INDX);
- nombres largos con su nombre corto 8.3;
- archivos residentes, fragmentados y dispersos (pagefile.sys);
- un archivo tan fragmentado que necesita $ATTRIBUTE_LIST y un registro de extension;
- $Volume marcado como sucio (Windows no se apago bien).

SOFTWARE y SYSTEM son colmenas de registro reales: regf con celdas nk, vk, lh, ri y db.
La averia es creible: un driver de disco actualizado dos dias antes del ultimo apagado, y SrtTrail.txt
(la reparacion automatica de Windows) que lo culpa.

Uso: mkwindows.py SALIDA.img
Escribe SALIDA.img.json con lo que Forense deberia encontrar y la huella sha1 de cada archivo."""
import hashlib, json, os, shutil, struct, subprocess, sys, tempfile, uuid

OUT = sys.argv[1]
CS = 4096; RS = 1024
NTFS_MB = 96
T_INST = 1731200000                  # 10/11/2024: instalacion
T_SHUT = 1732437000                  # 24/11/2024 08:30 UTC: ultimo apagado limpio
T_DRV = T_SHUT - 2 * 86400 + 3600    # el driver nuevo


def ft(unix):
    return (unix + 11644473600) * 10000000


def pat(seed, n):
    return bytes(((i * seed) ^ (i >> 5) ^ (seed * 7)) & 255 for i in range(n))


# ================================================================ registro (regf)
class Hive:
    """Celdas en bins 'hbin'; los desplazamientos de celda son relativos al primer bin, como en Windows."""

    def __init__(self, name):
        self.name = name
        self.b = bytearray()       # todos los bins
        self.cur = 0               # inicio del bin actual
        self.end = 0               # fin del bin actual

    def new_bin(self, need):
        size = (need + 32 + 4095) // 4096 * 4096
        self.cur = len(self.b)
        self.b += b'hbin' + struct.pack('<II', self.cur, size) + b'\0' * 20 + b'\0' * (size - 32)
        self.end = self.cur + size
        self.pos = self.cur + 32

    def cell(self, body):
        size = (len(body) + 4 + 7) & ~7
        if not self.b or self.pos + size > self.end:
            if self.b and self.end - self.pos >= 8:                      # lo que sobra del bin: celda libre
                struct.pack_into('<i', self.b, self.pos, self.end - self.pos)
            self.new_bin(size)
        off = self.pos
        struct.pack_into('<i', self.b, off, -size)
        self.b[off + 4:off + 4 + len(body)] = body
        self.pos += size
        return off

    def value(self, name, v):
        if isinstance(v, str):
            t, d = 1, v.encode('utf-16le') + b'\0\0'
        elif isinstance(v, int):
            t, d = 4, struct.pack('<I', v)
        elif v[0] == 'qword':
            t, d = 11, struct.pack('<Q', v[1])
        elif v[0] == 'expand':
            t, d = 2, v[1].encode('utf-16le') + b'\0\0'
        elif v[0] == 'multi':
            t, d = 7, b''.join(s.encode('utf-16le') + b'\0\0' for s in v[1]) + b'\0\0'
        else:
            t, d = 3, v[1]
        nb = name.encode('latin-1')
        if len(d) <= 4:                                                    # datos dentro del propio vk
            doff, dsize = struct.unpack('<I', d.ljust(4, b'\0'))[0], len(d) | 0x80000000
        elif len(d) > 16344:                                               # datos grandes: 'db' con segmentos
            segs = [self.cell(d[i:i + 16344]) for i in range(0, len(d), 16344)]
            sl = self.cell(b''.join(struct.pack('<I', s) for s in segs))
            doff, dsize = self.cell(b'db' + struct.pack('<HI', len(segs), sl)), len(d)
        else:
            doff, dsize = self.cell(d), len(d)
        return self.cell(b'vk' + struct.pack('<HIIIHH', len(nb), dsize, doff, t, 1, 0) + nb), len(nb), len(d)

    def key(self, node, name, parent, root=False):
        vals = node.get('@', {})
        subs = sorted(((k, v) for k, v in node.items() if k != '@'), key=lambda kv: kv[0].upper())
        nb = name.encode('latin-1')
        nk = self.cell(b'\0' * (0x4C + len(nb)))                           # se reserva antes que los hijos
        kids = [(self.key(v, k, nk), k) for k, v in subs]

        def lh(items):
            body = b'lh' + struct.pack('<H', len(items))
            for off, k in items:
                h = 0
                for c in k.upper():
                    h = (h * 37 + ord(c)) & 0xFFFFFFFF
                body += struct.pack('<II', off, h)
            return self.cell(body)

        if not kids:
            sl = 0xFFFFFFFF
        elif len(kids) > 40:                                               # muchas subclaves: 'ri' de listas 'lh'
            parts = [lh(kids[i:i + 40]) for i in range(0, len(kids), 40)]
            sl = self.cell(b'ri' + struct.pack('<H', len(parts)) + b''.join(struct.pack('<I', p) for p in parts))
        else:
            sl = lh(kids)
        vlist, mvn, mvd = 0xFFFFFFFF, 0, 0
        if vals:
            vs = [self.value(k, v) for k, v in vals.items()]
            vlist = self.cell(b''.join(struct.pack('<I', o) for o, _, _ in vs))
            mvn, mvd = max(n for _, n, _ in vs) * 2, max(d for _, _, d in vs)
        body = b'nk' + struct.pack('<HQIIIIIIIIIIIIIIIHH', 0x2C if root else 0x20, ft(T_SHUT - 600), 0, parent, len(kids), 0, sl,
                                   0xFFFFFFFF, len(vals), vlist, 0xFFFFFFFF, 0xFFFFFFFF, max([len(k) * 2 for _, k in kids] or [0]),
                                   0, mvn, mvd, 0, len(nb), 0) + nb
        self.b[nk + 4:nk + 4 + len(body)] = body
        return nk

    def build(self, tree):
        root = self.key(tree, 'ROOT', 0xFFFFFFFF, root=True)
        if self.end - self.pos >= 8:
            struct.pack_into('<i', self.b, self.pos, self.end - self.pos)
        base = bytearray(4096)
        base[0:4] = b'regf'
        struct.pack_into('<IIQIIIIIII', base, 4, 7, 7, ft(T_SHUT - 600), 1, 5, 0, 1, root, len(self.b), 1)
        nm = self.name.encode('utf-16le')[:64]
        base[0x30:0x30 + len(nm)] = nm
        x = 0
        for i in range(0, 508, 4):
            x ^= struct.unpack_from('<I', base, i)[0]
        struct.pack_into('<I', base, 0x1FC, x)
        return bytes(base) + bytes(self.b)


def hive(name, tree):
    return Hive(name).build(tree)


DRIVERS = ['1394ohci', 'acpi', 'afd', 'ahcache', 'amdsata', 'bam', 'basicdisplay', 'beep', 'bowser', 'cdrom', 'cng', 'disk',
           'dxgkrnl', 'e1i68x64', 'fltmgr', 'fvevol', 'hdaudbus', 'http', 'i8042prt', 'iastorac', 'intelppm', 'kbdclass', 'ks',
           'luafv', 'monitor', 'mouclass', 'mountmgr', 'msahci', 'mssmbios', 'ndis', 'netbt', 'npfs', 'ntfs', 'null', 'partmgr',
           'pci', 'rdyboost', 'serial', 'spaceport', 'srv2', 'storahci', 'stornvme', 'tcpip', 'tdx', 'usbehci', 'usbhub3',
           'usbxhci', 'volmgr', 'volsnap', 'wdf01000', 'wfplwfs', 'win32k'] + [f'oem{i:03d}' for i in range(260)]
SERVICES = {}
for d in DRIVERS:
    boot = d in ('acpi', 'disk', 'iastorac', 'ntfs', 'partmgr', 'pci', 'volmgr', 'storahci', 'mountmgr', 'fltmgr', 'cng')
    SERVICES[d if d != 'iastorac' else 'iaStorAC'] = {'@': {'ImagePath': ('expand', f'System32\\drivers\\{d}.sys'), 'Start': 0 if boot else 3,
                                                           'Type': 1, 'ErrorControl': 1, 'Group': 'SCSI miniport' if boot else 'Extended Base'}}
SOFTWARE = {'Microsoft': {'Windows NT': {'CurrentVersion': {'@': {
    'ProductName': 'Windows 10 Pro',            # Windows 11 sigue diciendo "Windows 10" aqui: lo delata la compilacion
    'EditionID': 'Professional', 'DisplayVersion': '23H2', 'CurrentBuild': '22631', 'CurrentBuildNumber': '22631',
    'UBR': 4460, 'CurrentMajorVersionNumber': 10, 'CurrentMinorVersionNumber': 0, 'InstallDate': T_INST,
    'RegisteredOwner': 'Marta Garcia', 'SystemRoot': 'C:\\Windows', 'DigitalProductId': ('binary', pat(9, 164)),
    'BigBlob': ('binary', pat(13, 40000))}}},       # dato grande: celda 'db'
    'Windows': {'CurrentVersion': {'@': {'ProgramFilesDir': 'C:\\Program Files'}}}}}
SYSTEM = {'Select': {'@': {'Current': 1, 'Default': 1, 'LastKnownGood': 1, 'Failed': 0}},
          'ControlSet001': {'Control': {'ComputerName': {'ComputerName': {'@': {'ComputerName': 'PC-MARTA'}}},
                                        'Windows': {'@': {'ShutdownTime': ('binary', struct.pack('<Q', ft(T_SHUT)))}},
                                        'TimeZoneInformation': {'@': {'TimeZoneKeyName': 'Romance Standard Time', 'Bias': 0xFFFFFFC4}},
                                        'CrashControl': {'@': {'CrashDumpEnabled': 3, 'MinidumpDir': ('expand', '%SystemRoot%\\Minidump')}}},
                            'Services': SERVICES}}

# ================================================================ NTFS
UPCASE = []
for c in range(65536):
    u = chr(c).upper() if not 0xD800 <= c < 0xE000 else chr(c)
    UPCASE.append(ord(u) if len(u) == 1 and ord(u) < 65536 else c)


def ukey(name):
    w = name.encode('utf-16le')
    return [UPCASE[struct.unpack_from('<H', w, i)[0]] for i in range(0, len(w), 2)]


class Ntfs:
    def __init__(self, mb):
        self.total = mb * 1024 * 1024 // CS
        self.img = bytearray(self.total * CS)
        self.next = 64                                     # primer cluster libre (despues del MFT inicial)
        self.recs = {}
        self.nrec = 64                                     # registros 0..63 reservados (0-23 del sistema)
        self.used = bytearray(self.total)

    def alloc(self, n, frags=1, gap=3):
        runs, left = [], n
        per = max(1, (n + frags - 1) // frags)
        while left:
            k = min(per, left)
            runs.append((self.next, k))
            for i in range(k):
                self.used[self.next + i] = 1
            self.next += k + (gap if frags > 1 else 0)
            left -= k
        return runs

    def put(self, data, frags=1):
        n = (len(data) + CS - 1) // CS
        runs = self.alloc(n, frags)
        o = 0
        for lcn, k in runs:
            chunk = data[o * CS:(o + k) * CS]
            self.img[lcn * CS:lcn * CS + len(chunk)] = chunk
            o += k
        return runs


def fixup(b, unit=512):
    b = bytearray(b)
    uo, un = struct.unpack_from('<HH', b, 4)
    struct.pack_into('<H', b, uo, 0x4242)
    for i in range(1, un):
        o = i * unit - 2
        b[uo + i * 2:uo + i * 2 + 2] = b[o:o + 2]
        b[o:o + 2] = b'\x42\x42'
    return bytes(b)


def runlist(runs):
    o, last = bytearray(), 0
    for lcn, n in runs:
        ln = n.to_bytes((n.bit_length() + 7) // 8 or 1, 'little')
        if lcn is None:                                          # trozo disperso: sin desplazamiento
            o += bytes([len(ln)]) + ln
            continue
        d = lcn - last
        last = lcn
        k = 1
        while not -(1 << (8 * k - 1)) <= d < (1 << (8 * k - 1)):
            k += 1
        o += bytes([len(ln) | k << 4]) + ln + d.to_bytes(k, 'little', signed=True)
    return bytes(o + b'\0')


def a_res(t, v, name='', aid=0):
    nm = name.encode('utf-16le')
    no = 24
    vo = (no + len(nm) + 7) & ~7
    ln = (vo + len(v) + 7) & ~7
    a = bytearray(ln)
    struct.pack_into('<IIBBHHH', a, 0, t, ln, 0, len(nm) // 2, no, 0, aid)
    struct.pack_into('<IHH', a, 16, len(v), vo, 0)
    a[no:no + len(nm)] = nm
    a[vo:vo + len(v)] = v
    return bytes(a)


def a_nonres(t, size, runs, name='', vcn0=0, aid=0, alloc=None):
    r = runlist(runs)
    nm = name.encode('utf-16le')
    no = 64
    ro = (no + len(nm) + 7) & ~7
    ln = (ro + len(r) + 7) & ~7
    a = bytearray(ln)
    vc = sum(n for _, n in runs)
    struct.pack_into('<IIBBHHH', a, 0, t, ln, 1, len(nm) // 2, no, 0, aid)
    struct.pack_into('<QQHH', a, 16, vcn0, vcn0 + vc - 1, ro, 0)
    struct.pack_into('<QQQ', a, 40, alloc if alloc is not None else vc * CS, size, size)
    a[no:no + len(nm)] = nm
    a[ro:ro + len(r)] = r
    return bytes(a)


def std_info(t):
    return a_res(0x10, struct.pack('<QQQQI', ft(t), ft(t), ft(t), ft(t), 0x20) + b'\0' * 28)


def fn_val(parent, name, size, t, d=False, ns=1):
    w = name.encode('utf-16le')
    v = struct.pack('<QQQQQQQII', parent | (1 << 48), ft(t), ft(t), ft(t), ft(t), (size + CS - 1) // CS * CS, size,
                    0x10000000 if d else 0x20, 0) + bytes([len(w) // 2, ns]) + w
    return v


def dos_name(name, used):
    base, _, ext = name.upper().rpartition('.') if '.' in name else (name.upper(), '', '')
    base = ''.join(c for c in base if c.isalnum())[:6]
    ext = ''.join(c for c in ext if c.isalnum())[:3]
    for i in range(1, 10):
        s = f'{base}~{i}' + (f'.{ext}' if ext else '')
        if s not in used:
            used.add(s)
            return s
    return base + '~X'


def needs_dos(name):
    b, _, e = name.rpartition('.') if '.' in name else (name, '', '')
    return len(b) > 8 or len(e) > 3 or name.count('.') > 1 or not all(c.isalnum() or c in '._-$~' for c in name)


def idx_entry(ref, key, child=None):
    ln = (16 + len(key) + 7) & ~7
    if child is not None:
        ln += 8
    e = bytearray(ln)
    struct.pack_into('<QHHI', e, 0, ref | (1 << 48), ln, len(key), 1 if child is not None else 0)
    e[16:16 + len(key)] = key
    if child is not None:
        struct.pack_into('<Q', e, ln - 8, child)
    return bytes(e)


def idx_end(child=None):
    e = bytearray(16 + (8 if child is not None else 0))
    struct.pack_into('<QHHI', e, 0, 0, len(e), 0, 2 | (1 if child is not None else 0))
    if child is not None:
        struct.pack_into('<Q', e, len(e) - 8, child)
    return bytes(e)


def build_disk():
    nt = Ntfs(NTFS_MB)
    files = {}                       # ruta -> (datos, fecha, opciones)

    def add(path, data, t=T_INST, **kw):
        files[path] = (data, t, kw)

    add('Windows/System32/config/SOFTWARE', hive('SOFTWARE', SOFTWARE), T_SHUT - 600, frags=3)
    add('Windows/System32/config/SYSTEM', hive('SYSTEM', SYSTEM), T_SHUT - 600, frags=140)   # pide $ATTRIBUTE_LIST
    add('Windows/System32/config/RegBack/SOFTWARE', b'', T_INST)
    add('Windows/System32/config/RegBack/SYSTEM', b'', T_INST)
    for d in DRIVERS:
        add(f'Windows/System32/drivers/{d}.sys', pat(len(d) + 3, 3000 + len(d) * 97), T_DRV if d == 'iastorac' else T_INST - 86400 * (len(d) % 40))
    srt = ('Startup Repair diagnosis and repair log\r\n---------------------------\r\nLast successful boot time: 11/24/2024 8:30:12 AM (GMT)\r\n'
           'Number of repair attempts: 3\r\n\r\nSession details\r\n---------------------------\r\nSystem Disk = \\Device\\Harddisk0\r\n'
           'Windows directory = C:\\Windows\r\nAutoChk Run = 0\r\nNumber of root causes = 1\r\n\r\nTest Performed:\r\n---------------------------\r\n'
           'Name: Check for updates\r\nResult: Completed successfully. Error code =  0x0\r\n\r\nRoot cause found:\r\n---------------------------\r\n'
           'Boot critical file c:\\windows\\system32\\drivers\\iastorac.sys is corrupt.\r\n\r\nRepair action: File repair\r\n'
           'Result: Failed. Error code =  0x2\r\n')
    add('Windows/System32/LogFiles/Srt/SrtTrail.txt', b'\xff\xfe' + srt.encode('utf-16le'), T_SHUT + 300)
    add('Windows/WinSxS/pending.xml', b'<?xml version="1.0"?><PendingTransaction Identifier="x"/>\r\n', T_SHUT - 900)
    add('Windows/notepad.exe', pat(4, 201000), T_INST, frags=2)
    add('Windows/win.ini', b'; for 16-bit app support\r\n[fonts]\r\n', T_INST)
    add('Users/Marta/Documents/Presupuesto 2025.xlsx', pat(21, 18000), T_SHUT - 86400 * 3)
    add('Users/Marta/Desktop/notas.txt', b'Llamar al tecnico\r\n', T_SHUT - 86400)
    add('Users/Public/Desktop/desktop.ini', b'[.ShellClassInfo]\r\n', T_INST)
    add('Program Files/Common Files/placeholder.txt', b'x' * 900, T_INST)
    add('pagefile.sys', None, T_SHUT, sparse=64 * CS)

    # arbol de carpetas
    dirs = {''}
    for p in files:
        parts = p.split('/')
        for i in range(1, len(parts)):
            dirs.add('/'.join(parts[:i]))
    rec_of = {'': 5}
    for d in sorted(dirs - {''}):
        rec_of[d] = nt.nrec
        nt.nrec += 1
    for p in sorted(files):
        rec_of[p] = nt.nrec
        nt.nrec += 1
    mft_recs = nt.nrec + 8
    records = {}

    def write_rec(n, attrs, flags=1, base=0):
        b = bytearray(RS)
        b[:4] = b'FILE'
        struct.pack_into('<HHQHHHHIIQHHI', b, 4, 48, 3, 0, 1, 1, 56, flags, 0, RS, base, 0, 64, n)
        p = 56
        for a in attrs:
            b[p:p + len(a)] = a
            p += len(a)
        b[p:p + 8] = struct.pack('<II', 0xFFFFFFFF, 0)
        struct.pack_into('<I', b, 24, p + 8)
        assert p + 8 <= RS, f'registro {n} demasiado grande'
        records[n] = fixup(b)

    entries = {d: [] for d in dirs}
    expect = {}
    extension = [nt.nrec]
    for p, (data, t, kw) in sorted(files.items()):
        parent = p.rpartition('/')[0]
        name = p.rpartition('/')[2]
        n = rec_of[p]
        if kw.get('sparse'):
            size = kw['sparse']
            runs = [(None, size // CS)]
            dattr = a_nonres(0x80, size, runs)
            sha = hashlib.sha1(b'\0' * size).hexdigest()
        else:
            sha = hashlib.sha1(data).hexdigest()
            size = len(data)
            if size < 600:
                dattr = a_res(0x80, data)
            else:
                runs = nt.put(data, kw.get('frags', 1))
                dattr = a_nonres(0x80, size, runs)
        fnv = fn_val(rec_of[parent], name, size, t, ns=3)
        attrs = [std_info(t), a_res(0x30, fnv)]
        dn = None
        if needs_dos(name):
            dn = dos_name(name, set(e[2] for e in entries[parent] if e[3] == 2))
            dfn = fn_val(rec_of[parent], dn, size, t, ns=2)
            attrs.append(a_res(0x30, dfn))
            fnv = fn_val(rec_of[parent], name, size, t, ns=1)
            attrs[1] = a_res(0x30, fnv)
            entries[parent].append((ukey(dn), dfn, dn, 2, n))
        entries[parent].append((ukey(name), fnv, name, 1 if dn else 3, n))
        if len(dattr) + sum(map(len, attrs)) + 64 > RS:          # no cabe: $ATTRIBUTE_LIST + registro de extension
            ext = extension[0]
            extension[0] += 1
            al = b''
            for typ, a in ((0x10, attrs[0]), (0x30, attrs[1])):
                al += struct.pack('<IHBBQQH', typ, 32, 0, 26, 0, n | (1 << 48), 0) + b'\0' * 6
            al += struct.pack('<IHBBQQH', 0x80, 32, 0, 26, 0, ext | (1 << 48), 0) + b'\0' * 6
            write_rec(n, attrs[:2] + [a_res(0x20, al)] + attrs[2:])
            write_rec(ext, [dattr], 1, n | (1 << 48))
        else:
            write_rec(n, attrs + [dattr])
        expect[p] = {'sha1': sha, 'size': size, 'mtime': t}
    mft_recs = max(mft_recs, extension[0] + 4)

    # carpetas: indices en arbol (INDX de 4096) si no caben en el registro
    def write_dir(d):
        n = rec_of[d]
        name = d.rpartition('/')[2] if d else '.'
        parent = d.rpartition('/')[0] if d else ''
        kids = sorted(entries[d], key=lambda e: (e[0], e[3]))
        items = [(idx_entry(k[4], k[1]), None) for k in kids]
        blocks = []

        def node_bytes(its, end_child):
            return b''.join(idx_entry_child(e, c) for e, c in its) + idx_end(end_child)

        def idx_entry_child(e, c):
            if c is None:
                return e
            ln = struct.unpack_from('<H', e, 8)[0] + 8
            b = bytearray(e) + b'\0' * 8
            struct.pack_into('<HHI', b, 8, ln, struct.unpack_from('<H', e, 10)[0], 1)
            struct.pack_into('<Q', b, ln - 8, c)
            return bytes(b)

        def write_block(its, end_child):
            body = node_bytes(its, end_child)
            vcn = len(blocks)
            blk = bytearray(CS)
            blk[:4] = b'INDX'
            struct.pack_into('<HHQQ', blk, 4, 40, 9, 0, vcn)
            struct.pack_into('<IIII', blk, 24, 40, 40 + len(body), CS - 24, 1 if end_child is not None else 0)
            blk[64:64 + len(body)] = body
            blocks.append(fixup(blk))
            return vcn

        ROOT_CAP = 560
        end_child = None
        while len(node_bytes(items, end_child)) > ROOT_CAP:
            out, cur = [], []
            for it in items:
                if cur and len(node_bytes(cur + [it], it[1])) > CS - 64 - 16:
                    out.append((it[0], write_block(cur, it[1])))
                    cur = []
                else:
                    cur.append(it)
            end_child = write_block(cur, end_child)
            items = out
        body = node_bytes(items, end_child)
        large = end_child is not None
        root = a_res(0x90, struct.pack('<IIII', 0x30, 1, CS, 1) + struct.pack('<IIII', 16, 16 + len(body), 16 + len(body), 1 if large else 0) + body, '$I30')
        attrs = [std_info(T_INST), a_res(0x30, fn_val(rec_of[parent] if d else 5, name, 0, T_INST, True)), root]
        if blocks:
            runs = nt.put(b''.join(blocks))
            attrs.append(a_nonres(0xA0, len(blocks) * CS, runs, '$I30'))
            attrs.append(a_res(0xB0, bytes([255] * ((len(blocks) + 7) // 8)).ljust(8, b'\0'), '$I30'))
        write_rec(n, attrs, 3)
        return len(blocks)

    for d in sorted(dirs, key=lambda x: -x.count('/')):
        if d:
            parent = d.rpartition('/')[0]
            name = d.rpartition('/')[2]
            if needs_dos(name):                                 # "Program Files" tambien se llama PROGRA~1
                dn = dos_name(name, set(e[2] for e in entries[parent] if e[3] == 2))
                entries[parent].append((ukey(dn), fn_val(rec_of[parent], dn, 0, T_INST, True, ns=2), dn, 2, rec_of[d]))
                entries[parent].append((ukey(name), fn_val(rec_of[parent], name, 0, T_INST, True, ns=1), name, 1, rec_of[d]))
            else:
                entries[parent].append((ukey(name), fn_val(rec_of[parent], name, 0, T_INST, True, ns=3), name, 3, rec_of[d]))
    nblocks = {d: write_dir(d) for d in sorted(dirs)}

    # sistema: $MFT (2 trozos), $MFTMirr, $LogFile, $Volume (sucio), $AttrDef, $Bitmap, $Boot, $BadClus, $Secure, $UpCase, $Extend
    upc = b''.join(struct.pack('<H', c) for c in UPCASE)
    up_runs = nt.put(upc)
    mft_clusters = (mft_recs * RS + CS - 1) // CS
    half = mft_clusters // 2
    mft_runs = nt.alloc(half) + nt.alloc(mft_clusters - half, 1)
    nt.next += 5
    mirr = nt.alloc(1)
    log = nt.alloc(16)
    sysrec = {0: ('$MFT', [std_info(T_INST), None, a_nonres(0x80, mft_clusters * CS, mft_runs)]),
              1: ('$MFTMirr', [std_info(T_INST), None, a_nonres(0x80, CS, mirr)]),
              2: ('$LogFile', [std_info(T_INST), None, a_nonres(0x80, 16 * CS, log)]),
              3: ('$Volume', [std_info(T_INST), None, a_res(0x60, 'Windows'.encode('utf-16le')),
                              a_res(0x70, struct.pack('<QBBH', 0, 3, 1, 0x0001) + b'\0' * 4)]),
              4: ('$AttrDef', [std_info(T_INST), None, a_res(0x80, b'')]),
              6: ('$Bitmap', [std_info(T_INST), None, a_res(0x80, b'')]),
              7: ('$Boot', [std_info(T_INST), None, a_nonres(0x80, 8192, [(0, 2)])]),
              8: ('$BadClus', [std_info(T_INST), None, a_res(0x80, b'')]),
              9: ('$Secure', [std_info(T_INST), None]),
              10: ('$UpCase', [std_info(T_INST), None, a_nonres(0x80, len(upc), up_runs)]),
              11: ('$Extend', [std_info(T_INST), None])}
    for n, (name, attrs) in sysrec.items():
        attrs[1] = a_res(0x30, fn_val(5, name, 0, T_INST))
        write_rec(n, attrs, 1)
    for n in list(range(12, 24)):
        write_rec(n, [], 0)
    # el MFT a disco, en sus dos trozos
    mft = bytearray(mft_clusters * CS)
    for n, r in records.items():
        mft[n * RS:(n + 1) * RS] = r
    o = 0
    for lcn, k in mft_runs:
        nt.img[lcn * CS:(lcn + k) * CS] = mft[o * CS:(o + k) * CS]
        o += k
    nt.img[mirr[0][0] * CS:mirr[0][0] * CS + 4 * RS] = mft[:4 * RS]
    b = bytearray(512)
    b[0:3] = b'\xeb\x52\x90'
    b[3:11] = b'NTFS    '
    struct.pack_into('<HB', b, 11, 512, 8)
    b[21] = 0xF8
    struct.pack_into('<Q', b, 40, nt.total * 8 - 1)
    struct.pack_into('<QQ', b, 48, mft_runs[0][0], mirr[0][0])
    b[64] = 0xF6
    b[68] = 1
    struct.pack_into('<Q', b, 72, 0x1234ABCD5678EF00)
    b[510:512] = b'\x55\xaa'
    nt.img[:512] = b
    assert nt.next < nt.total, 'el NTFS no cabe'
    return nt.img, expect, nblocks


def make_esp(mb):
    """Particion EFI en FAT, hecha con las herramientas de macOS."""
    tmp = tempfile.mkdtemp()
    img = os.path.join(tmp, 'esp.img')
    with open(img, 'wb') as f:
        f.truncate(mb * 1024 * 1024)
    dev = subprocess.check_output(['hdiutil', 'attach', '-nomount', '-imagekey', 'diskimage-class=CRawDiskImage', img]).decode().split()[0]
    try:
        subprocess.check_call(['newfs_msdos', '-F', '16', '-v', 'SYSTEM', dev], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        mnt = os.path.join(tmp, 'm')
        os.mkdir(mnt)
        subprocess.check_call(['mount', '-t', 'msdos', dev, mnt])
        try:
            os.makedirs(os.path.join(mnt, 'EFI', 'Microsoft', 'Boot'))
            os.makedirs(os.path.join(mnt, 'EFI', 'Boot'))
            open(os.path.join(mnt, 'EFI', 'Microsoft', 'Boot', 'bootmgfw.efi'), 'wb').write(b'MZ' + pat(17, 4000))
            open(os.path.join(mnt, 'EFI', 'Boot', 'bootx64.efi'), 'wb').write(b'MZ' + pat(17, 4000))
            for junk in ['.fseventsd', '.Spotlight-V100', '.Trashes']:
                shutil.rmtree(os.path.join(mnt, junk), ignore_errors=True)
        finally:
            subprocess.call(['umount', mnt])
    finally:
        subprocess.call(['hdiutil', 'detach', '-quiet', dev])
    data = open(img, 'rb').read()
    shutil.rmtree(tmp)
    return data


def crc32(b):
    import zlib
    return zlib.crc32(b) & 0xFFFFFFFF


def gpt_disk(parts):
    """parts: [(tipo_guid, nombre, datos)] alineadas a 1 MiB. Devuelve el disco con MBR protector y GPT."""
    lba = 2048
    layout = []
    for t, name, data in parts:
        n = (len(data) + 511) // 512
        layout.append((t, name, lba, lba + n - 1, data))
        lba += (n + 2047) // 2048 * 2048
    total = lba + 2048
    disk = bytearray(total * 512)
    ents = bytearray(128 * 128)
    for i, (t, name, first, last, data) in enumerate(layout):
        e = uuid.UUID(t).bytes_le + uuid.uuid5(uuid.NAMESPACE_DNS, f'zz{i}').bytes_le + struct.pack('<QQQ', first, last, 0) + name.encode('utf-16le').ljust(72, b'\0')
        ents[i * 128:(i + 1) * 128] = e
        disk[first * 512:first * 512 + len(data)] = data

    def header(cur, alt, ent_lba):
        h = bytearray(92)
        struct.pack_into('<8sIIIIQQQQ16sQIII', h, 0, b'EFI PART', 0x10000, 92, 0, 0, cur, alt, 2048 - 0 if False else 34,
                         total - 34, uuid.uuid5(uuid.NAMESPACE_DNS, 'zzdisk').bytes_le, ent_lba, 128, 128, crc32(bytes(ents)))
        struct.pack_into('<I', h, 16, crc32(bytes(h)))
        return h
    disk[2 * 512:2 * 512 + len(ents)] = ents
    disk[512:512 + 92] = header(1, total - 1, 2)
    disk[(total - 33) * 512:(total - 33) * 512 + len(ents)] = ents
    disk[(total - 1) * 512:(total - 1) * 512 + 92] = header(total - 1, 1, total - 33)
    mbr = bytearray(512)
    struct.pack_into('<BBBBBBBBII', mbr, 446, 0, 0, 2, 0, 0xEE, 0xFF, 0xFF, 0xFF, 1, min(total - 1, 0xFFFFFFFF))
    mbr[510:512] = b'\x55\xaa'
    disk[:512] = mbr
    return disk, [(name, first, last) for _, name, first, last, _ in layout]


ntfs, expect, nblocks = build_disk()
esp = make_esp(34)
disk, layout = gpt_disk([('C12A7328-F81F-11D2-BA4B-00A0C93EC93B', 'EFI system partition', esp),
                         ('E3C9E316-0B5C-4DB8-817D-F92DF00215AE', 'Microsoft reserved partition', b'\0' * (16 * 1024 * 1024)),
                         ('EBD0A0A2-B9E5-4433-87C0-68B6B72699C7', 'Basic data partition', bytes(ntfs))])
open(OUT, 'wb').write(disk)
info = {'particiones': layout, 'windows': {'version': 'Windows 11 Pro', 'producto_registro': 'Windows 10 Pro', 'edicion': 'Professional',
        'version_mostrada': '23H2', 'compilacion': '22631.4460', 'equipo': 'PC-MARTA', 'propietario': 'Marta Garcia',
        'instalado': T_INST, 'ultimo_apagado': T_SHUT, 'volumen_sucio': True, 'etiqueta': 'Windows',
        'driver_reciente': 'iastorac.sys', 'driver_fecha': T_DRV, 'arranque': 'UEFI'},
        'carpetas_con_indx': {d: n for d, n in nblocks.items() if n}, 'archivos': expect}
json.dump(info, open(OUT + '.json', 'w'), indent=1)
print(f'{OUT}: disco GPT de {len(disk) // 1048576} MiB, {len(expect)} archivos, carpetas con INDX: '
      + ', '.join(f'{d or "/"} ({n})' for d, n in nblocks.items() if n))
