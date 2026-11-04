#!/usr/bin/env python3
"""qemu_prof.py - profiler probkujacy kodu goscia: CS:IP z monitora QEMU -> symbole z *.map.

Uzycie:
  tools/qemu_prof.py --fd boot.img --run "LOADER.EXE SKI.EXE" [--boot-wait 32] [--secs 20]
                     [--mouse] [--top 25]

Modul i segment rozpoznaje po liniach "Modul X ... sel=A-B" z logu COM1 loadera.
--mouse: najpierw przesun kursor na dol ekranu (np. zeby narciarz w SkiFree jechal).

Ograniczenia: QEMU podaje EIP tylko na granicy bloku TCG, wiec trafienia skupiaja sie
na poczatkach blokow (wejscia funkcji), a funkcje statyczne nie maja symboli w .map i sa
liczone do najblizszego symbolu przed nimi. Wynik pokazuje kierunek, nie dokladne procenty.
"""
import argparse, bisect, collections, os, re, shutil, socket, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAPS = {'KERNEL': 'kernel/kernel.map', 'USER': 'user/user.map', 'GDI': 'gdi/gdi.map'}


def load_map(path, seg):
    syms = []
    with open(path, errors='ignore') as f:
        for l in f:
            m = re.match(r'\s*0*(\d+):([0-9a-f]{4})[+*\s]+([A-Za-z_]\w+)', l, re.I)
            if m and int(m[1]) == seg:
                syms.append((int(m[2], 16), m[3]))
    return sorted(syms)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fd', default=os.path.join(ROOT, 'boot.img'))
    ap.add_argument('--run', required=True)
    ap.add_argument('--boot-wait', type=float, default=32)
    ap.add_argument('--secs', type=float, default=20)
    ap.add_argument('--port', type=int, default=4495)
    ap.add_argument('--mouse', action='store_true')
    ap.add_argument('--top', type=int, default=25)
    a = ap.parse_args()

    tmp = tempfile.mkdtemp(prefix='qprof_')
    fd = os.path.join(tmp, 'fd.img')
    shutil.copy(a.fd, fd)
    bat = os.path.join(tmp, 'fdauto.bat')
    with open(bat, 'wb') as f:
        f.write(b'@echo off\r\n' + a.run.encode() + b'\r\n')
    subprocess.run(['mcopy', '-o', '-i', fd, bat, '::fdauto.bat'], check=True)
    serial = os.path.join(tmp, 'serial.log')
    q = subprocess.Popen(['qemu-system-i386', '-drive', f'file={fd},if=floppy,format=raw', '-boot', 'a',
                          '-m', '16', '-display', 'none', '-no-reboot', '-serial', f'file:{serial}',
                          '-monitor', f'tcp:127.0.0.1:{a.port},server,nowait'])
    try:
        s = None
        for _ in range(50):
            try:
                s = socket.create_connection(('127.0.0.1', a.port))
                break
            except OSError:
                time.sleep(0.1)
        if not s:
            sys.exit('monitor QEMU niedostepny')
        s.settimeout(0.5)

        def mon(cmd):
            s.sendall((cmd + '\n').encode())
            buf = b''
            end = time.time() + 0.5
            while time.time() < end:
                try:
                    x = s.recv(65536)
                except OSError:
                    break
                if not x:
                    break
                buf += x
                if buf.endswith(b'(qemu) ') and buf.count(b'(qemu) ') >= 1 and b'EFL' in buf or \
                        (not cmd.startswith('info') and buf.endswith(b'(qemu) ')):
                    break
            return buf.decode('latin1')

        time.sleep(a.boot_wait)
        if a.mouse:
            for _ in range(10):
                mon('mouse_move -100 -100')
            for _ in range(10):
                mon('mouse_move 32 44')
            time.sleep(1)
        hist = collections.Counter()
        end = time.time() + a.secs
        while time.time() < end:
            m = re.search(r'EIP=([0-9a-f]{8}).*?CS =([0-9a-f]{4})', mon('info registers'), re.S)
            if m:
                hist[(int(m[2], 16), int(m[1], 16))] += 1
        s.sendall(b'quit\n')
        q.wait(timeout=5)

        segs = {}
        with open(serial, errors='replace') as f:
            for l in f:
                m = re.match(r'Modul (\w+) .*sel=([0-9A-Fa-f]{4})-([0-9A-Fa-f]{4})', l)
                if m:
                    first, last = int(m[2], 16), int(m[3], 16)
                    for n, sel in enumerate(range(first, last + 1, 8), 1):
                        segs[sel] = (m[1], n)
        res = collections.Counter()
        cache = {}
        for (cs, ip), n in hist.items():
            mod, seg = segs.get(cs, (f'sel_{cs:04X}', 0))
            name = '?'
            if mod in MAPS:
                syms = cache.setdefault((mod, seg), load_map(os.path.join(ROOT, MAPS[mod]), seg))
                i = bisect.bisect_right([x[0] for x in syms], ip) - 1
                if i >= 0:
                    name = syms[i][1]
            res[f'{mod}:{name}'] += n
        total = sum(res.values()) or 1
        print(f'probek: {total}')
        for k, v in res.most_common(a.top):
            print(f'{100 * v / total:5.1f}%  {k}')
    finally:
        if q.poll() is None:
            q.kill()
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == '__main__':
    main()
