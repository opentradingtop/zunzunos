#!/usr/bin/env python3
"""Prueba del clonador de discos (Clonar).

Primera parte (QEMU, sistema en una memoria USB):
  - ahci0, un ZunzunOS instalado (MBR, con un archivo de datos al azar) -> ahci1, disco mayor;
  - ahci2, un disco GPT con dos particiones y un sector ilegible (blkdebug) -> ahci3, menor pero donde caben las
    particiones, y -> ahci4, mayor (la GPT se ajusta: copia de seguridad al final, MBR protector);
  - ahci2 -> ahci5 (no cabe) y ahci0 -> usb0 (el sistema en uso) se rechazan;
  - usb0, la memoria del sistema en uso -> usb1 (el origen queda en solo lectura mientras se copia).
En el Mac se comparan los discos sector a sector y se validan las GPT (CRC de cabeceras y entradas).
Despues se arranca cada copia de ZunzunOS sola: la de ahci1 con BIOS y con UEFI, y la de usb1 con BIOS.
Uso: clonar_test.py DIR_SALIDA (sin espacios: va dentro de las opciones de QEMU)"""
import os, random, re, shutil, struct, subprocess, sys, tempfile, zlib
out = os.path.abspath(sys.argv[1]); os.makedirs(out, exist_ok=True)
if ' ' in out: sys.exit('la carpeta de salida no puede tener espacios')
SS = 512; MB = 1 << 20; rnd = random.Random(7); bad = []
P = lambda n: os.path.join(out, n)


def mount_img(img, fn):
    dev = subprocess.check_output(['hdiutil', 'attach', '-nomount', '-imagekey', 'diskimage-class=CRawDiskImage', img]).decode().split()[0]; mnt = tempfile.mkdtemp()
    try:
        subprocess.check_call(['mount', '-t', 'msdos', dev + 's1', mnt]); r = fn(mnt)
        for junk in ['.fseventsd', '.Spotlight-V100', '.Trashes']: shutil.rmtree(os.path.join(mnt, junk), ignore_errors=True)
        return r
    finally:
        subprocess.call(['umount', mnt]); subprocess.call(['hdiutil', 'detach', '-quiet', dev]); os.rmdir(mnt)


def blank(name, size):
    with open(P(name), 'wb') as f: f.truncate(size)


# --- discos ---
shutil.copy('build/zunzun.img', P('stick.img'))
shutil.copy('build/zunzun.img', P('src.img'))
with open(P('src.img'), 'r+b') as f:   # otro numero de serie: si no, el sistema podria elegir este disco como /sys
    for s in (2048, 2048 + 6): f.seek(s * SS + 67); f.write(struct.pack('<I', 0x13572468))
AZAR = rnd.randbytes(3 * MB + 777)
mount_img(P('src.img'), lambda m: open(os.path.join(m, 'AZAR.BIN'), 'wb').write(AZAR))
blank('big.img', 320 * MB); blank('sysclone.img', 256 * MB)

GS = 64 * MB // SS   # disco GPT de 64 MiB: dos particiones de 16 MiB, datos sueltos en el espacio libre y un sector malo
PARTS = [(2048, 34815), (34816, 67583)]; BADLBA = 3000


def guid_le(s):
    import uuid; return uuid.UUID(s).bytes_le


def mkgpt():
    d = bytearray(GS * SS); ents = bytearray(128 * 128)
    for i, (a, b) in enumerate(PARTS):
        struct.pack_into('<16s16sQQQ', ents, i * 128, guid_le('EBD0A0A2-B9E5-4433-87C0-68B6B72699C7'), rnd.randbytes(16), a, b, 0)
        nm = ('Datos %d' % i).encode('utf-16-le'); ents[i * 128 + 56:i * 128 + 56 + len(nm)] = nm
        d[a * SS:(b + 1) * SS] = rnd.randbytes((b + 1 - a) * SS)
    d[70000 * SS:70100 * SS] = rnd.randbytes(100 * SS)   # datos en el espacio libre: la copia exacta tambien los lleva
    ec = zlib.crc32(ents); dg = rnd.randbytes(16)
    def hdr(my, alt, el):
        h = bytearray(SS); struct.pack_into('<8sIIIIQQQQ16sQIII', h, 0, b'EFI PART', 0x10000, 92, 0, 0, my, alt, 34, GS - 34, dg, el, 128, 128, ec)
        struct.pack_into('<I', h, 16, zlib.crc32(h[:92])); return h
    d[SS:2 * SS] = hdr(1, GS - 1, 2); d[2 * SS:34 * SS] = ents; d[(GS - 33) * SS:(GS - 1) * SS] = ents; d[(GS - 1) * SS:] = hdr(GS - 1, 1, GS - 33)
    struct.pack_into('<BBBBBBBBII', d, 446, 0, 0, 2, 0, 0xEE, 0xFF, 0xFF, 0xFF, 1, GS - 1); d[510:512] = b'\x55\xaa'
    return bytes(d)
GPT = mkgpt(); open(P('gpt.img'), 'wb').write(GPT)
open(P('blk.cfg'), 'w').write('[inject-error]\nevent = "read_aio"\nerrno = "5"\nsector = "%d"\nonce = "off"\n' % BADLBA)
blank('gsmall.img', 48 * MB); blank('gbig.img', 80 * MB); blank('tiny.img', 20 * MB)
SRC = open(P('src.img'), 'rb').read()

# --- primera parte: clonar ---
FAM0 = ['rescate', 'files', 'discos', 'papelera', 'restaurar', 'instalar', 'forense', 'clonar']
MH = 42 + 9 * 28 + 14; MY = 800 - 36 - MH - 2; i = FAM0.index('clonar'); fy = MY + 42 + 14; ay = MY + 42 + 28 * i + 14
OPEN = ['click:50,782', 'sleep:0.6', f'move:100,{fy}', 'sleep:0.5', f'move:245,{fy}', 'sleep:0.3', f'move:350,{ay}', 'sleep:0.3', f'click:350,{ay}', 'sleep:2']
def pick(o, d): return [f'clicklog:clonar origen {o} en (\\d+),(\\d+)', 'sleep:0.4', f'clicklog:clonar destino {d} en (\\d+),(\\d+)', 'sleep:0.6']
def clone(o, d): return pick(o, d) + ['clicklog:boton en (\\d+),(\\d+)', 'sleep:1', 'type:BORRAR\n', f'wait:clonar {o} -> {d}: ', 'sleep:1', f'shot:c-{o}-{d}']
s = ['wait:escritorio listo', 'sleep:1'] + OPEN + ['wait:clonar lista 8 discos', 'sleep:1', 'shot:c-lista']
s += clone('ahci0', 'ahci1') + clone('ahci2', 'ahci3') + clone('ahci2', 'ahci4')
s += pick('ahci2', 'ahci5') + ['wait:clonar no se puede ahci2 -> ahci5', 'shot:c-nocabe'] + pick('ahci0', 'usb0') + ['wait:clonar no se puede ahci0 -> usb0']
s += clone('usb0', 'usb1')
disks = [('src.img', None), ('big.img', None), ('gpt.img', 'blk'), ('gsmall.img', None), ('gbig.img', None), ('tiny.img', None)]
q = '-device ich9-ahci,id=a ' + ' '.join(f'-drive if=none,id=h{n},format=raw,file={"blkdebug:" + P("blk.cfg") + ":" if k else ""}{P(f)} -device ide-hd,drive=h{n},bus=a.{n}' for n, (f, k) in enumerate(disks))
q += f' -device usb-ehci,id=e -device usb-kbd,bus=e.0 -device usb-tablet,bus=e.0 -drive if=none,id=st,format=raw,file={P("stick.img")} -device usb-storage,bus=e.0,drive=st,bootindex=0'
q += f' -drive if=none,id=sc,format=raw,file={P("sysclone.img")} -device usb-storage,bus=e.0,drive=sc'
r = subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', 'bios', '--no-disk', '--abs', '--out', P('a'), '--timeout', '400', '--port', '4705', '--mem', '1024', '--qemu', q] + s)
log = open(P('a/serial-bios.log'), errors='replace').read()
if r: bad.append('primera parte: vm=%d' % r)
for need in ['clonar ahci0 -> ahci1: OK (524288 sectores, 0 ilegibles, comprobada 1, origen cambiado 0)',
             'clonar ahci2 -> ahci3: OK (%d sectores, 1 ilegibles, comprobada 1' % (48 * MB // SS - 33),
             'clonar ahci2 -> ahci4: OK (%d sectores, 1 ilegibles, comprobada 1' % GS,
             'clonar sector ilegible %d' % BADLBA, 'clonar no se puede ahci2 -> ahci5: No cabe', 'clonar no se puede ahci0 -> usb0: El destino es la unidad del sistema',
             'clonar usb0 -> usb1: OK', 'comprobada 1, origen cambiado 0)']:
    if need not in log: bad.append('falta en el registro: ' + need)
m = re.search(r'clonar usb0 -> usb1: OK \((\d+) sectores', log)
for x in ('PANICO', 'fallo de pagina', 'excepcion', 'termino: -'):
    if x in log: bad.append(x)

# --- en el Mac: sector a sector ---
BIG = open(P('big.img'), 'rb').read()
if BIG[:len(SRC)] != SRC: bad.append('ahci1 no es identico al origen')
if AZAR not in SRC: bad.append('el archivo de datos no llego al origen')


def gpt_check(img, T, name):
    d = open(P(img), 'rb').read(); e = []
    if len(d) != T * SS: return ['%s: tamano %d' % (name, len(d))], bytes(T * SS)
    def hdr(lba):
        h = d[lba * SS:lba * SS + 92]; f = struct.unpack('<8sIIIIQQQQ16sQIII', h); z = bytearray(h); z[16:20] = b'\0\0\0\0'
        if f[0] != b'EFI PART' or zlib.crc32(z) != f[3]: e.append('%s: cabecera GPT en %d no valida' % (name, lba))
        return f
    p, b = hdr(1), hdr(T - 1)
    if (p[5], p[6], p[8], p[10]) != (1, T - 1, T - 34, 2): e.append('%s: cabecera principal %s' % (name, (p[5], p[6], p[8], p[10])))
    if (b[5], b[6], b[8], b[10]) != (T - 1, 1, T - 34, T - 33): e.append('%s: copia de seguridad %s' % (name, (b[5], b[6], b[8], b[10])))
    if d[2 * SS:34 * SS] != GPT[2 * SS:34 * SS] or d[(T - 33) * SS:(T - 1) * SS] != GPT[2 * SS:34 * SS]: e.append('%s: entradas distintas' % name)
    if zlib.crc32(d[2 * SS:34 * SS]) != p[13]: e.append('%s: CRC de las entradas' % name)
    if struct.unpack_from('<I', d, 446 + 12)[0] != T - 1 or d[446 + 4] != 0xEE: e.append('%s: MBR protector' % name)
    if p[9] != struct.unpack('<8sIIIIQQQQ16sQIII', GPT[SS:SS + 92])[9]: e.append('%s: GUID del disco cambiado' % name)
    for a, z in PARTS:   # particiones identicas salvo el sector ilegible, que queda a cero
        for lba in (a, BADLBA - 1, BADLBA + 1, z):
            if d[lba * SS:(lba + 1) * SS] != GPT[lba * SS:(lba + 1) * SS]: e.append('%s: sector %d distinto' % (name, lba)); break
        x, y = a * SS, (z + 1) * SS
        if a <= BADLBA <= z:
            if d[x:BADLBA * SS] != GPT[x:BADLBA * SS] or d[(BADLBA + 1) * SS:y] != GPT[(BADLBA + 1) * SS:y]: e.append('%s: particion %d distinta' % (name, a))
            if d[BADLBA * SS:(BADLBA + 1) * SS] != bytes(SS): e.append('%s: el sector ilegible no quedo a cero' % name)
        elif d[x:y] != GPT[x:y]: e.append('%s: particion %d distinta' % (name, a))
    return e, d
e1, SM = gpt_check('gsmall.img', 48 * MB // SS, 'ahci3'); bad += e1
e2, BG = gpt_check('gbig.img', 80 * MB // SS, 'ahci4'); bad += e2
if BG[34 * SS:BADLBA * SS] + BG[(BADLBA + 1) * SS:(GS - 34) * SS] != GPT[34 * SS:BADLBA * SS] + GPT[(BADLBA + 1) * SS:(GS - 34) * SS]: bad.append('ahci4: el resto del disco no es identico')
if BG[(GS - 34) * SS:GS * SS] != bytes(34 * SS): bad.append('ahci4: la GPT vieja del origen sigue en medio del disco')
if SM[70000 * SS:70100 * SS] != GPT[70000 * SS:70100 * SS]: bad.append('ahci3: faltan los datos del espacio libre que caben')
if open(P('tiny.img'), 'rb').read() != bytes(20 * MB): bad.append('ahci5 (no cabe) se ha tocado')
SC = open(P('sysclone.img'), 'rb').read(); ST = open(P('stick.img'), 'rb').read()
if SC[:446] != ST[:446] or SC[2048 * SS:2049 * SS] != ST[2048 * SS:2049 * SS]: bad.append('usb1: el arranque no es el de la memoria del sistema')

# --- segunda parte: arrancar las copias solas ---
for tag, mode, img, extra in [('b', 'bios', 'big.img', ''), ('u', 'uefi', 'big.img', ''), ('s', 'bios', 'sysclone.img', 'usb')]:
    q = f'-drive format=raw,file={P(img)}' if not extra else f'-device usb-ehci,id=e -device usb-kbd,bus=e.0 -device usb-tablet,bus=e.0 -drive if=none,id=st,format=raw,file={P(img)} -device usb-storage,bus=e.0,drive=st,bootindex=0'
    rr = subprocess.call(['python3', '-I', 'tools/vm.py', '--mode', mode, '--no-disk', '--abs', '--out', P(tag), '--timeout', '90', '--port', '4706', '--qemu', q, 'wait:escritorio listo', 'sleep:1', f'shot:arranque-{tag}'])
    lg = open(P(f'{tag}/serial-{mode}.log'), errors='replace').read()
    if rr or 'vfs: /sys' not in lg: bad.append('la copia %s no arranca con %s (vm=%d)' % (img, mode, rr))
print('clonar:', 'OK (MBR y GPT, mayor y menor, sector ilegible, sistema en uso, y las copias arrancan con BIOS y UEFI)' if not bad else 'FALLO\n  ' + '\n  '.join(bad)); sys.exit(0 if not bad else 1)
