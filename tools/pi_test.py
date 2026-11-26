#!/usr/bin/env python3
"""Prueba de ZunzunOS en la Raspberry Pi 2 emulada (QEMU -M raspi2b, 480x320).

  pi_test.py DIR [KERNEL7] [--sd IMAGEN] [--usb]

DIR es una carpeta de trabajo sin espacios (QEMU pone ahi su socket). Arranca kernel7.img como lo haria el
firmware (-bios lo carga en 0x8000), y por el puerto serie manda toques ("!T x y" / "!U x y") y teclas
("!K uso 1|0"), las mismas ordenes que entiende el kernel (kernel/pi/placa.c). Comprueba:
  1. el arranque hasta la pantalla de inicio, a 480x320;
  2. entrar como administrador tocando la pantalla y saltar el PIN ("Ahora no");
  3. el escritorio con su barra;
  4. el teclado en pantalla: abrirlo desde la barra, escribir en el Editor tocando teclas y ver el texto.
Con --usb pone en el controlador USB (DWC2) un teclado y una tarjeta de red USB, que QEMU cuelga de un hub, y
comprueba ademas que el teclado USB escribe en el Editor y que la red USB recibe direccion por DHCP.
Deja capturas PNG en DIR (pi-*.png).
"""
import os, socket, struct, subprocess, sys, threading, time, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

class Pi:
    def __init__(self, work, kernel, sd=None, extra=()):
        self.work = work; os.makedirs(work, exist_ok=True)
        self.log = []; self.lock = threading.Lock(); self.t0 = time.time()
        mon = os.path.join(work, 'm.sock')
        if os.path.exists(mon): os.unlink(mon)
        args = ['qemu-system-arm', '-M', 'raspi2b', '-bios', kernel, '-serial', 'stdio', '-display', 'none',
                '-monitor', 'unix:m.sock,server,nowait']
        if sd: args += ['-drive', f'if=sd,format=raw,file={sd}']
        args += list(extra)
        self.p = subprocess.Popen(args, cwd=work, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        threading.Thread(target=self._reader, daemon=True).start()
        try:
            for _ in range(100):
                if os.path.exists(mon): break
                time.sleep(0.05)
            os.chdir(work)   # la ruta del socket debe ser corta: se conecta con la relativa
            self.mon = socket.socket(socket.AF_UNIX); self.mon.connect('m.sock'); self.mon.settimeout(5)
        except Exception:
            self.p.kill(); raise
        time.sleep(0.2); self._drain()
    def _reader(self):
        buf = b''
        while True:
            c = self.p.stdout.read1(4096) if hasattr(self.p.stdout, 'read1') else self.p.stdout.read(1)
            if not c: break
            buf += c
            while b'\n' in buf:
                l, buf = buf.split(b'\n', 1)
                l = l.decode('utf-8', 'replace').rstrip('\r')
                with self.lock: self.log.append(l)
                if os.environ.get('PI_VERBOSE'): print('  | %6.1f' % (time.time() - self.t0), l, flush=True)
    def _drain(self):
        try:
            while self.mon.recv(65536): pass
        except Exception: pass
    def wait(self, text, timeout=60, start=0):
        t0 = time.time()
        while time.time() - t0 < timeout:
            with self.lock:
                for i, l in enumerate(self.log[start:]):
                    if text in l: return start + i
                if any('PANICO' in l for l in self.log): break
            if self.p.poll() is not None: break
            time.sleep(0.1)
        raise SystemExit(f'FALLO: no aparece "{text}"\n--- registro ---\n' + '\n'.join(self.log[-40:]))
    def mark(self):
        with self.lock: return len(self.log)
    def send(self, line):
        self.p.stdin.write((line + '\n').encode()); self.p.stdin.flush()
    def tap(self, x, y, hold=0.15):
        self.send(f'!T {x} {y}'); time.sleep(hold); self.send(f'!U {x} {y}'); time.sleep(0.35)
    def sendkey(self, k):   # tecla por el teclado USB de QEMU (monitor)
        self.mon.sendall(f'sendkey {k}\n'.encode()); time.sleep(0.25); self._drain()
    def key(self, usage):
        self.send(f'!K {usage} 1'); time.sleep(0.05); self.send(f'!K {usage} 0'); time.sleep(0.15)
    def shot(self, name):
        path = os.path.join(self.work, name + '.ppm')
        if os.path.exists(path): os.unlink(path)
        self.mon.sendall(f'screendump {name}.ppm\n'.encode())
        for _ in range(50):
            if os.path.exists(path) and os.path.getsize(path) > 100: break
            time.sleep(0.1)
        time.sleep(0.2); self._drain()
        d = open(path, 'rb').read().split(b'\n', 3); w, h = map(int, d[1].split()); px = d[3]
        os.unlink(path)
        raw = b''.join(b'\0' + px[y * w * 3:(y + 1) * w * 3] for y in range(h))
        ch = lambda t, b: struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b))
        png = os.path.join(self.work, name + '.png')
        open(png, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw)) + ch(b'IEND', b''))
        return w, h, px
    def close(self):
        try: self.mon.sendall(b'quit\n')
        except Exception: pass
        try: self.p.wait(5)
        except Exception: self.p.kill()

def pixel(px, w, x, y): o = (y * w + x) * 3; return tuple(px[o:o + 3])

def main():
    a, sd, usb, args = [], None, False, sys.argv[1:]
    while args:
        x = args.pop(0)
        if x == '--sd': sd = os.path.abspath(args.pop(0))
        elif x == '--usb': usb = True
        else: a.append(x)
    if not a: raise SystemExit(__doc__)
    work = os.path.abspath(a[0]); kernel = os.path.abspath(a[1]) if len(a) > 1 else os.path.join(ROOT, 'build', 'pi', 'kernel7.img')
    extra = ['-device', 'usb-kbd', '-netdev', 'user,id=n0', '-device', 'usb-net,netdev=n0'] if usb else []
    pi = Pi(work, kernel, sd, extra)
    try:
        if sd: print('    (con tarjeta SD: ' + sd + ')')
        if usb: print('    (con teclado y red USB)')
        pi.wait('ZunzunOS', 20); pi.wait('[zz] sistema listo', 60)
        pi.wait('sesion: pantalla de inicio', 90)
        time.sleep(1.5)
        w, h, px = pi.shot('pi-1-inicio')
        assert (w, h) == (480, 320), f'la pantalla mide {w}x{h}, no 480x320'
        print('ok  arranque y pantalla de inicio a 480x320', flush=True)
        m = pi.mark(); pi.tap(130, 140)   # tarjeta "Administrador"
        t0 = time.time(); tapped = False
        while time.time() - t0 < 90:   # sin /sys no se puede guardar un PIN y entra directamente
            with pi.lock: new = pi.log[m:]
            if any('sesion: escritorio' in l for l in new): break
            if not tapped and any('crear pin' in l for l in new):
                time.sleep(1); pi.shot('pi-2-pin'); pi.tap(128, 228); tapped = True   # "Ahora no"
            time.sleep(0.2)
        pi.wait('sesion: escritorio', 5, m)
        time.sleep(2); w, h, px = pi.shot('pi-3-escritorio')
        print('ok  escritorio tras tocar Administrador y "Ahora no"')
        # grupos del escritorio en pantalla estrecha: abrir Oficina (segunda linea) y ver sus iconos en filas
        pi.tap(40, 36); time.sleep(1.5); w, h, opened = pi.shot('pi-3b-oficina-abierta')
        icons = sum(1 for y in range(60, 200, 2) for x in range(8, 470, 2) if pixel(opened, w, x, y) != pixel(px, w, x, y))
        pi.tap(40, 36); time.sleep(1)
        assert icons > 200, f'al abrir Oficina no aparecen sus iconos ({icons} pixeles cambian)'
        print('ok  grupos del escritorio en filas (pantalla estrecha)')
        # teclado en pantalla: boton de la barra (esquina inferior derecha, junto al reloj)
        m = pi.mark(); pi.tap(*KB_BUTTON)
        pi.wait('teclado en pantalla visible', 20, m)
        time.sleep(0.8); pi.shot('pi-4-teclado')
        print('ok  teclado en pantalla')
        m = pi.mark(); pi.tap(*OSK_HIDE); pi.wait('teclado en pantalla oculto', 20, m)
        # menu Inicio -> Oficina -> Editor
        pi.tap(50, 302); time.sleep(1.5); pi.shot('pi-5-menu'); pi.tap(60, 88); time.sleep(1.5); pi.shot('pi-6-oficina')
        m = pi.mark(); pi.tap(293, 183); pi.wait('/rd/bin/editor', 20, m); time.sleep(3)
        w, h, before = pi.shot('pi-7-editor')
        m = pi.mark(); pi.tap(*KB_BUTTON); pi.wait('teclado en pantalla visible', 20, m); time.sleep(0.5)
        for r, k in [(2, 5), (1, 9), (2, 8), (2, 0)]:   # h o l a
            pi.tap(*osk_pos(r, k), hold=0.1)
        time.sleep(1); w, h, after = pi.shot('pi-8-hola')
        y0 = 320 - OSK_H - 36
        diff = sum(1 for y in range(30, y0, 2) for x in range(0, 480, 2) if pixel(before, w, x, y) != pixel(after, w, x, y))
        assert diff > 20, f'el texto escrito con el teclado en pantalla no aparece (pixeles distintos: {diff})'
        print(f'ok  escribir "hola" en el Editor con el teclado en pantalla ({diff} pixeles cambian)')
        m = pi.mark(); pi.tap(*OSK_HIDE); pi.wait('teclado en pantalla oculto', 20, m)
        if usb:
            pi.wait('dwc: HID teclado', 5); time.sleep(0.5); w, h, before = pi.shot('pi-8b-antes-usb')
            for k in ['ret', 'u', 's', 'b', 'spc', 'shift-o', 'k']: pi.sendkey(k)
            time.sleep(1); w, h, after = pi.shot('pi-8c-usb')
            diff = sum(1 for y in range(30, 320 - 36, 2) for x in range(0, 480, 2) if pixel(before, w, x, y) != pixel(after, w, x, y))
            assert diff > 20, f'lo escrito con el teclado USB no aparece (pixeles distintos: {diff})'
            print(f'ok  teclado USB (DWC2 y hub): escribe "usb Ok" en el Editor ({diff} pixeles cambian)')
            pi.wait('red: IP', 30)
            with pi.lock: ip = [l for l in pi.log if 'red: IP' in l][-1]
            print('ok  red USB por DWC2: ' + ip.split('red: ', 1)[1])
        # navegador en pantalla estrecha: botones arriba y la direccion en una segunda fila
        pi.tap(50, 302); time.sleep(1.5); pi.tap(60, 112); time.sleep(1.5); pi.shot('pi-10-internet')
        m = pi.mark(); pi.tap(293, 112); pi.wait('/rd/bin/browser', 20, m); time.sleep(4); w, h, px = pi.shot('pi-11-navegador')
        m = pi.mark(); pi.tap(240, ADDR_Y); pi.wait('teclado en pantalla visible', 10, m); time.sleep(0.5)
        for r, k in [(3, 5), (2, 0)]:   # b a
            pi.tap(*osk_pos(r, k), hold=0.1)
        time.sleep(1); w, h, after = pi.shot('pi-12-direccion')
        diff = sum(1 for y in range(ADDR_Y - 10, ADDR_Y + 10) for x in range(30, 470, 2) if pixel(px, w, x, y) != pixel(after, w, x, y))
        assert diff > 10, f'lo escrito en la direccion del navegador no aparece ({diff} pixeles cambian)'
        m = pi.mark(); pi.tap(*OSK_HIDE); pi.wait('teclado en pantalla oculto', 20, m)
        print(f'ok  navegador: cabecera en dos filas, tocar la direccion saca el teclado y se escribe ({diff} pixeles cambian)')
        # calibracion del tactil con lecturas crudas inventadas: crudo = (4000 - 7*y, 300 + 8*x) (ejes cambiados)
        raw = lambda x, y: (4000 - 7 * y, 300 + 8 * x)
        m = pi.mark(); pi.send('!C'); pi.wait('tactil: calibrando', 10, m); time.sleep(0.8); pi.shot('pi-9-calibrar')
        for x, y in [(48, 32), (432, 160), (240, 288)]:
            rx, ry = raw(x, y); pi.send(f'!R {rx} {ry} 1'); time.sleep(0.3); pi.send(f'!R {rx} {ry} 0'); time.sleep(0.4)
        pi.wait('tactil: calibrado', 10, m)
        if sd: pi.wait('TACTIL.TXT', 10, m)
        rx, ry = raw(*KB_BUTTON); m = pi.mark(); pi.send(f'!R {rx} {ry} 1'); time.sleep(0.2); pi.send(f'!R {rx} {ry} 0')
        pi.wait('teclado en pantalla visible', 10, m)
        print('ok  calibracion del tactil (3 puntos) y toque calibrado' + (' guardada en /sys' if sd else ''))
    finally:
        pi.close()

# teclado en pantalla (kernel/wm.c): 5 filas de 28 px + 2 de separacion, encima de la barra (36 px); anchos en medias teclas
OSK_ROWS = [[2] * 12 + [3], [3] + [2] * 12, [2] * 11 + [4], [3] + [2] * 12, [3, 3, 9, 2, 2, 2, 2, 3, 3]]
OSK_H = 5 * 30 + 2
def osk_pos(r, k, sw=480, sh=320, ph=36):
    row = OSK_ROWS[r]; tot = sum(row); before = sum(row[:k])
    x = 2 + before * (sw - 4) // tot; x2 = 2 + (before + row[k]) * (sw - 4) // tot
    return (x + x2) // 2, sh - ph - OSK_H + 2 + r * 30 + 14
OSK_HIDE = None
ADDR_Y = 76          # direccion del navegador en pantalla estrecha (user/apps/browser.c: AY 38 + 13 bajo el titulo)
KB_BUTTON = (123, 302)   # boton del teclado en la barra (user/apps/desktop.c, KBX y KBW)
OSK_HIDE = osk_pos(4, 8)
if __name__ == '__main__':
    main()
