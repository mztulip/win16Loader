#!/usr/bin/env python3
"""qemu_headless.py - uruchom loader w QEMU bez okna, sterowanie przez monitor TCP.

Uzycie:
  tools/qemu_headless.py --fd boot.img [--fdb b.img] [--hd calmira.img] [--run "LOADER.EXE X.EXE"]
                         [--out DIR] [--port N] [--boot-wait S] [--mem MB] KROKI...

  --run CMD    nadpisz fdauto.bat na kopii dyskietki (oryginal nietkniety);
               CMD moze zawierac kilka linii rozdzielonych ';'
  KROKI:
    w<sek>        czekaj (np. w2.5)
    d<sek>        czekaj na linie 'TEST ... DONE' w logu COM1, najdluzej <sek>
    p<nazwa>      screendump -> OUT/<nazwa>.png
    M<polecenie>  polecenie monitora QEMU (np. 'Meject -f floppy0')
    k<klawisz>    sendkey (np. kdown, kalt-f4, kret)
    m<dx>,<dy>    przesun mysz wzglednie
    a<x>,<y>      ustaw mysz bezwzglednie (najpierw do rogu 0,0)
    c             klik lewym przyciskiem
    u<tekst>      wpisz tekst (litery/cyfry)
Serial loguje sie do OUT/serial.log (OUT domyslnie ./qemu_out).
Przed KROKAMI czeka --boot-wait sekund (domyslnie 25).
"""
import argparse, os, re, shutil, socket, subprocess, sys, tempfile, time


def mon(sock, cmd):
    sock.sendall((cmd + '\n').encode())
    time.sleep(0.15)
    try:
        sock.recv(65536)
    except OSError:
        pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fd', required=True)
    ap.add_argument('--hd')
    ap.add_argument('--fdb', help='druga dyskietka (B:)')
    ap.add_argument('--run')
    ap.add_argument('--out', default='qemu_out')
    ap.add_argument('--port', type=int, default=4460)
    ap.add_argument('--boot-wait', type=float, default=25)
    ap.add_argument('--mem', type=int, default=16)
    ap.add_argument('steps', nargs='*')
    a = ap.parse_args()

    os.makedirs(a.out, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix='qh_')
    fd = os.path.join(tmp, 'fd.img')
    shutil.copy(a.fd, fd)
    if a.run:
        bat = os.path.join(tmp, 'fdauto.bat')
        with open(bat, 'wb') as f:
            f.write(b'@echo off\r\n' + b''.join(l.strip().encode() + b'\r\n'
                                                for l in a.run.split(';')))
        subprocess.run(['mcopy', '-o', '-i', fd, bat, '::fdauto.bat'], check=True)
    cmd = ['qemu-system-i386', '-drive', f'file={fd},if=floppy,format=raw']
    if a.fdb:
        fdb = os.path.join(tmp, 'fdb.img')
        shutil.copy(a.fdb, fdb)
        cmd += ['-drive', f'file={fdb},if=floppy,index=1,format=raw']
    if a.hd:
        hd = os.path.join(tmp, 'hd.img')
        shutil.copy(a.hd, hd)
        cmd += ['-drive', f'file={hd},if=ide,format=raw']
    serial = os.path.join(a.out, 'serial.log')
    cmd += ['-boot', 'a', '-m', str(a.mem), '-display', 'none',
            '-serial', f'file:{serial}', '-no-reboot',
            '-monitor', f'tcp:127.0.0.1:{a.port},server,nowait']
    q = subprocess.Popen(cmd)
    try:
        sock = None
        for _ in range(50):
            try:
                sock = socket.create_connection(('127.0.0.1', a.port))
                break
            except OSError:
                time.sleep(0.1)
        if not sock:
            sys.exit('monitor QEMU niedostepny')
        sock.settimeout(0.3)
        time.sleep(a.boot_wait)
        for st in a.steps:
            k, v = st[0], st[1:]
            if k == 'w':
                time.sleep(float(v))
            elif k == 'd':
                end = time.time() + float(v)
                while time.time() < end:
                    with open(serial, errors='replace') as f:
                        if re.search(r'^TEST \S+ DONE', f.read(), re.M):
                            break
                    time.sleep(0.5)
            elif k == 'p':
                ppm = os.path.join(tmp, v + '.ppm')
                mon(sock, f'screendump {ppm}')
                time.sleep(0.5)
                subprocess.run(['magick', ppm, os.path.join(a.out, v + '.png')])
            elif k == 'M':
                mon(sock, v)
            elif k == 'k':
                mon(sock, f'sendkey {v}')
            elif k == 'm':
                dx, dy = v.split(',')
                mon(sock, f'mouse_move {dx} {dy}')
            elif k == 'a':                       # pozycja bezwzgledna: do rogu, potem malymi krokami
                x, y = (int(t) for t in v.split(','))
                for _ in range(10):
                    mon(sock, 'mouse_move -100 -100')
                while x > 0 or y > 0:
                    sx, sy = min(x, 40), min(y, 40)
                    mon(sock, f'mouse_move {sx} {sy}')
                    x -= sx; y -= sy
            elif k == 'c':
                mon(sock, 'mouse_button 1'); time.sleep(0.1); mon(sock, 'mouse_button 0')
            elif k == 'u':
                for ch in v:
                    mon(sock, f'sendkey {ch.lower() if ch.isalnum() else "spc"}')
        mon(sock, 'quit')
    finally:
        try:
            q.wait(timeout=10)
        except subprocess.TimeoutExpired:
            q.kill()
        shutil.rmtree(tmp, ignore_errors=True)
    print(serial)


if __name__ == '__main__':
    main()
