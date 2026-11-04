#!/usr/bin/env python3
"""run_examples.py - uruchom aplikacje przykladowe (example_apps/*) w QEMU bez okna
i zbierz wyniki testow (linie "TEST ..." z OutputDebugString na porcie szeregowym).

Uzycie: tools/run_examples.py [--fd boot.img] [--out DIR] [nazwa ...]
  bez nazw - wszystkie aplikacje z APPS ponizej.
Dla kazdej apki: zrzut ekranu OUT/<nazwa>.png, log OUT/<nazwa>/serial.log.
Kod wyjscia 1, jesli ktorykolwiek test FAIL albo brak linii DONE.
"""
import argparse, os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# nazwa -> (katalog, plik .EXE, kroki qemu_headless po starcie)
# Dodatkowe pliki na dyskietke: EXTRA[nazwa] = [(zrodlo, nazwa na dyskietce)]
EXTRA = {'shell': [('example_apps/app.ico', 'SHELLTST.ICO')]}
APPS = {
    'controls': ('example_apps/controls', 'CTLTEST.EXE', ['w4', 'pshot', 'kesc', 'w1']),
    'odmenu':   ('example_apps/odmenu', 'ODMENU.EXE', ['w4', 'pshot', 'kesc', 'w1', 'kesc', 'w1']),
    'shell':    ('example_apps/shell', 'SHELLTST.EXE', ['w4', 'pshot', 'kalt-f4', 'w1']),
    'sysdll':   ('example_apps/sysdll', 'SYSDLL.EXE', ['w4', 'pshot', 'kalt-f4', 'w1']),
    'fonts':    ('example_apps/fonts', 'FONTTEST.EXE', ['w4', 'pshot', 'kalt-f4', 'w1']),
    # ruch mysza i klawisze w trakcie petli sprawdzajacej rejestry (IRQ12/IRQ1)
    'irqregs':  ('example_apps/irqregs', 'IRQTEST.EXE',
                 ['w3'] + ['m25,15', 'w0.03', 'm-25,-15', 'w0.03', 'kshift', 'w0.03'] * 100 + ['w2', 'pshot', 'kalt-f4', 'w1']),
    # ikona A czeka w (300,200) na dwuklik
    'iconic':   ('example_apps/iconic', 'ICONTEST.EXE', ['w6', 'pshot', 'a316,216', 'c', 'c', 'w2', 'kalt-f4', 'w1']),
    # testy dymne (aplikacje juz na boot.img, bez linii TEST): wystarczy brak wyjatku
    'ski':      (None, 'SKI.EXE', ['w4', 'kdown', 'w0.5', 'kdown', 'w2', 'kleft', 'w1', 'pshot']),
    'win1app':  (None, 'WIN1APP.EXE', ['w3', 'a66,32', 'w0.5', 'c', 'w1', 'a66,52', 'w0.3', 'c', 'w2', 'pshot']),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fd', default=os.path.join(ROOT, 'boot.img'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'qemu_out', 'examples'))
    ap.add_argument('--boot-wait', default='22')
    ap.add_argument('names', nargs='*')
    a = ap.parse_args()
    names = a.names or list(APPS)
    os.makedirs(a.out, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix='ex_')
    fd = os.path.join(tmp, 'fd.img')
    shutil.copy(a.fd, fd)
    for n in names:
        d, exe, _ = APPS[n]
        if d is None:
            continue
        subprocess.run(['make', '-s', '-C', os.path.join(ROOT, d)], check=True)
        subprocess.run(['mcopy', '-o', '-i', fd, os.path.join(ROOT, d, exe), '::' + exe], check=True)
        for src, dst in EXTRA.get(n, []):
            subprocess.run(['mcopy', '-o', '-i', fd, os.path.join(ROOT, src), '::' + dst], check=True)
    bad = False
    summary = []
    for n in names:
        d, exe, steps = APPS[n]
        out = os.path.join(a.out, n)
        steps = [s if not s.startswith('p') else 'p' + n for s in steps]
        subprocess.run([sys.executable, os.path.join(HERE, 'qemu_headless.py'), '--fd', fd,
                        '--run', 'LOADER.EXE ' + exe, '--out', out, '--boot-wait', a.boot_wait] + steps,
                       stdout=subprocess.DEVNULL)
        log = open(os.path.join(out, 'serial.log'), errors='replace').read()
        tests = re.findall(r'^TEST \S+ (.*?)\r?$', log, re.M)
        fails = [t for t in tests if ': FAIL' in t]
        done = [t for t in tests if t.startswith('DONE')]
        exc = re.findall(r'\*\*\* EXCEPTION.*', log)
        for t in fails:
            print(f'  {n}: {t}')
        for e in exc:
            print(f'  {n}: {e}')
        ok = not fails and (done or d is None) and not exc
        bad |= not ok
        summary.append(f'{n:10s} {"OK  " if ok else "BLAD"} {done[0] if done else ("dymny" if d is None else "brak DONE")}'
                       f'{"  " + exc[0] if exc else ""}')
        if os.path.exists(os.path.join(out, n + '.png')):
            shutil.copy(os.path.join(out, n + '.png'), os.path.join(a.out, n + '.png'))
    shutil.rmtree(tmp, ignore_errors=True)
    print('\n'.join(summary))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
