#!/usr/bin/env python3
"""Crea la imagen de ZunzunOS: MBR + cargador BIOS + particion FAT32 con todo.

  mkimage.py boot.bin BOOTX64.EFI kernel.bin initrd.bin leeme.txt salida.img [--disk-bytes N] [--label NOMBRE] [--docs CARPETA]

Con --docs los archivos de CARPETA se copian en DOCS/<CARPETA> (Documentos), con nombres 8.3.

Con --disk-bytes la particion ocupa todo el disco, pero el archivo solo contiene
hasta el ultimo dato usado: grabar es rapido y el resto queda como espacio libre.
"""
import struct, sys, time

SECT = 512
PART_LBA = 2048

args = sys.argv[1:]
disk_bytes = 256 << 20
if '--disk-bytes' in args:
    i = args.index('--disk-bytes')
    disk_bytes = int(args[i + 1])
    del args[i:i + 2]
label = 'ZUNZUN'
if '--label' in args:
    i = args.index('--label')
    label = args[i + 1].upper()
    del args[i:i + 2]
if not label or len(label) > 11 or any(c in '"*+,./:;<=>?[\\]|' or not (32 <= ord(c) < 127) for c in label):
    sys.exit('etiqueta no valida: hasta 11 letras, cifras o espacios')
LABEL = label.ljust(11).encode()
docs_dir = None
if '--docs' in args:
    i = args.index('--docs')
    docs_dir = args[i + 1]
    del args[i:i + 2]
boot_p, efi_p, kern_p, ird_p, leeme_p, out_p = args
rd = lambda p: open(p, 'rb').read()
boot, efi, kern, ird, leeme = map(rd, (boot_p, efi_p, kern_p, ird_p, leeme_p))
if len(boot) > 2048 * SECT - 512:
    sys.exit('cargador demasiado grande')

total = disk_bytes // SECT
part_sectors = total - PART_LBA
gb = part_sectors * SECT
spc = 1 if gb <= 260 << 20 else 8 if gb <= 8 << 30 else 16 if gb <= 16 << 30 else 32 if gb <= 32 << 30 else 64
RESERVED = 32
clusters_est = part_sectors // spc
fat_sectors = (clusters_est + 2) * 4 // SECT + 1
data_start = RESERVED + 2 * fat_sectors
clusters = (part_sectors - data_start) // spc
if clusters < 65525:
    sys.exit('disco demasiado pequeno para FAT32')
csize = spc * SECT

fat = {0: 0x0FFFFFF8, 1: 0x0FFFFFFF}
blobs = []
next_cl = 2

def alloc(data):
    """reserva clusters contiguos y guarda el contenido"""
    global next_cl
    n = max(1, (len(data) + csize - 1) // csize)
    first = next_cl
    for c in range(first, first + n - 1):
        fat[c] = c + 1
    fat[first + n - 1] = 0x0FFFFFFF
    next_cl += n
    blobs.append((first, data))
    return first

now = time.localtime()
fdate = ((now.tm_year - 1980) << 9) | (now.tm_mon << 5) | now.tm_mday
ftime = (now.tm_hour << 11) | (now.tm_min << 5)

def ent(name, attr, cl, size):
    return struct.pack('<11sBBBHHHHHHHI', name, attr, 0, 0, ftime, fdate, fdate,
                       cl >> 16, ftime, fdate, cl & 0xFFFF, size)

def name83(s):
    b, _, e = s.partition('.')
    return (b.ljust(8) + e.ljust(3)).encode()

def mkdir(entries, parent):
    """entries: [(nombre, attr, cluster, tam)]; crea el directorio"""
    first = next_cl
    body = bytearray()
    body += ent(b'.          ', 0x10, first, 0)
    body += ent(b'..         ', 0x10, parent, 0)
    for n, a, c, s in entries:
        body += ent(name83(n), a, c, s)
    body += bytes(-len(body) % csize)
    return alloc(bytes(body))

# raiz en el cluster 2
root_cl = next_cl
fat[root_cl] = 0x0FFFFFFF
next_cl += 1

k_cl = alloc(kern)
i_cl = alloc(ird)
e_cl = alloc(efi)
l_cl = alloc(leeme)
zz = mkdir([('KERNEL.BIN', 0x05, k_cl, len(kern)), ('INITRD.BIN', 0x05, i_cl, len(ird))], 0)
ef_cl = next_cl                 # EFI ocupa un cluster y BOOT va justo detras
ef = mkdir([('BOOT', 0x10, ef_cl + 1, 0)], 0)
bt = mkdir([('BOOTX64.EFI', 0x20, e_cl, len(efi))], ef)

docs = []
if docs_dir:
    import os
    def ok83(n):
        b, _, e = n.partition('.')
        return n == n.upper() and 1 <= len(b) <= 8 and len(e) <= 3 and '.' not in e and all(c.isalnum() or c in '_-' for c in b + e)
    sub = os.path.basename(os.path.normpath(docs_dir)).upper()
    files = sorted(os.listdir(docs_dir))
    if not ok83(sub) or '.' in sub or not all(ok83(f) for f in files) or len(files) > 100:
        sys.exit('--docs: la carpeta y sus archivos necesitan nombres 8.3 en mayusculas')
    fents = [(f, 0x20, alloc(rd(os.path.join(docs_dir, f))), os.path.getsize(os.path.join(docs_dir, f))) for f in files]
    dd_cl = next_cl                 # DOCS ocupa un cluster y la subcarpeta va justo detras
    dd = mkdir([(sub, 0x10, dd_cl + 1, 0)], 0)
    mkdir(fents, dd)
    docs = [('DOCS', 0x10, dd, 0)]

root = bytearray()
root += ent(LABEL, 0x08, 0, 0)
root += ent(name83('EFI'), 0x10, ef, 0)
root += ent(name83('ZUNZUN'), 0x10, zz, 0)
root += ent(name83('LEEME.TXT'), 0x20, l_cl, len(leeme))
for n, a, c, sz in docs:
    root += ent(name83(n), a, c, sz)
root += bytes(-len(root) % csize)
blobs.append((root_cl, bytes(root)))

def cl_lba(c):
    return PART_LBA + data_start + (c - 2) * spc

used_end = cl_lba(next_cl)
img = bytearray(used_end * SECT)

# --- cargador y MBR ---
img[:len(boot)] = boot
if boot[512:520] != b'ZZBOOT01':
    sys.exit('cargador sin firma')
struct.pack_into('<IIII', img, 520, cl_lba(k_cl), (len(kern) + 511) // 512,
                 cl_lba(i_cl), (len(ird) + 511) // 512)
img[440:446] = struct.pack('<IH', 0x5A554E5A, 0)

def chs(lba):
    if lba >= 1024 * 255 * 63:
        return b'\xfe\xff\xff'
    c = lba // (255 * 63); h = (lba // 63) % 255; s = lba % 63 + 1
    return bytes([h, ((c >> 2) & 0xC0) | s, c & 0xFF])

img[446:462] = b'\x80' + chs(PART_LBA) + b'\x0c' + chs(total - 1) + struct.pack('<II', PART_LBA, part_sectors)
img[510:512] = b'\x55\xaa'

# --- sector de arranque FAT32 ---
b = bytearray(SECT)
b[0:3] = b'\xeb\x58\x90'
b[3:11] = b'ZUNZUNOS'
struct.pack_into('<HBHBHHBHHHII', b, 11, SECT, spc, RESERVED, 2, 0, 0, 0xF8, 0, 63, 255, PART_LBA, part_sectors)
struct.pack_into('<IHHIHH', b, 36, fat_sectors, 0, 0, root_cl, 1, 6)
b[64] = 0x80; b[66] = 0x29
struct.pack_into('<I', b, 67, 0x5A554E5A)
b[71:82] = LABEL
b[82:90] = b'FAT32   '
b[510:512] = b'\x55\xaa'
fsi = bytearray(SECT)
fsi[0:4] = b'RRaA'; fsi[484:488] = b'rrAa'
struct.pack_into('<II', fsi, 488, clusters - (next_cl - 2), next_cl)
fsi[510:512] = b'\x55\xaa'
base = PART_LBA * SECT
img[base:base + SECT] = b
img[base + SECT:base + 2 * SECT] = fsi
img[base + 6 * SECT:base + 7 * SECT] = b
img[base + 7 * SECT:base + 8 * SECT] = fsi

# --- FAT (dos copias) ---
for c, v in fat.items():
    for k in range(2):
        off = base + (RESERVED + k * fat_sectors) * SECT + c * 4
        struct.pack_into('<I', img, off, v)

# --- datos ---
for c, data in blobs:
    o = cl_lba(c) * SECT
    img[o:o + len(data)] = data

open(out_p, 'wb').write(img)
print(f'{out_p}: {len(img)/1048576:.1f} MiB escritos, particion de {gb/1048576:.0f} MiB, cluster {csize} B')
