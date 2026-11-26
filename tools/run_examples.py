#!/usr/bin/env python3
"""run_examples.py - uruchom aplikacje przykladowe (example_apps/*) w QEMU bez okna
i zbierz wyniki testow (linie "TEST ..." z OutputDebugString na porcie szeregowym).

Uzycie: tools/run_examples.py [--fd boot.img | --hd calmira.img] [--out DIR] [nazwa ...]
  --hd: testy z woluminu C: bez stacji dyskietek (kopia obrazu dysku, swiezy runtime
        i windir/WIN.INI w C:\\WINDOWS, aplikacje w C:\\TESTS); dosprompt pominiety.
  bez nazw - wszystkie aplikacje z APPS ponizej.
Dla kazdej apki: zrzut ekranu OUT/<nazwa>.png, log OUT/<nazwa>/serial.log.
Kod wyjscia 1, jesli ktorykolwiek test FAIL albo brak linii DONE.
"""
import argparse, os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# nazwa -> (katalog, plik .EXE, kroki qemu_headless po starcie)
# Dodatkowe pliki na dyskietke: EXTRA[nazwa] = [(zrodlo, nazwa na dyskietce)]
EXTRA = {'shell': [('example_apps/app.ico', 'SHELLTST.ICO')],
         'tasks': [('example_apps/taskchild/TASKCHLD.EXE', 'TASKCHLD.EXE')],
         'libdemo': [('example_apps/libdemo/' + f, 'LIBDEMO/' + f)
                     for f in ('LIBCHLD.EXE', 'DEMODLL.DLL', 'DEMODYN.DLL', 'DEMOBAD.DLL')]}
# programy zamykane Alt+F4 jako jedyne zadanie: loader musi wrocic do DOS
BACK_TO_DOS = ('shell', 'sysdll', 'fonts', 'clock')
# aplikacje bez wlasnego katalogu (testy dymne): skad wziac .EXE na wolumin (--hd)
SMOKE_SRC = {'ski': 'SKIFREE/SKI.EXE', 'win1app': 'win16_example_app/Win1App.exe',
             'clock': 'example_apps/clock/CLOCK.EXE'}
# testy samej stacji dyskietek (wyjmowanie dyskietki, B:) - tylko w trybie dyskietki
FLOPPY_ONLY = ('dosprompt',)
APPS = {
    'controls': ('example_apps/controls', 'CTLTEST.EXE', ['w4', 'pshot', 'kesc', 'w1']),
    'odmenu':   ('example_apps/odmenu', 'ODMENU.EXE', ['w4', 'pshot', 'kesc', 'w1', 'kesc', 'w1']),
    'shell':    ('example_apps/shell', 'SHELLTST.EXE', ['w4', 'pshot', 'kalt-f4', 'w1']),
    'sysdll':   ('example_apps/sysdll', 'SYSDLL.EXE', ['w4', 'pshot', 'kalt-f4', 'w1']),
    'fonts':    ('example_apps/fonts', 'FONTTEST.EXE', ['w4', 'pshot', 'kalt-f4', 'w1']),
    'dlgkeys':  ('example_apps/dlgkeys', 'DLGKEYS.EXE', ['d30', 'w1', 'pshot']),
    'firstsize': ('example_apps/firstsize', 'FIRSTSIZ.EXE', ['d30', 'w1', 'pshot']),
    # ruch mysza i klawisze w trakcie petli sprawdzajacej rejestry (IRQ12/IRQ1)
    'irqregs':  ('example_apps/irqregs', 'IRQTEST.EXE',
                 ['w3'] + ['m25,15', 'w0.03', 'm-25,-15', 'w0.03', 'kshift', 'w0.03'] * 100 + ['w2', 'pshot', 'kalt-f4', 'w1']),
    # ikona A czeka w (300,200) na dwuklik
    'iconic':   ('example_apps/iconic', 'ICONTEST.EXE', ['w6', 'pshot', 'a316,216', 'c', 'c', 'w2', 'kalt-f4', 'w1']),
    # poprawnosc szybkich sciezek GDI + regresja wydajnosci (konczy sie sama)
    'gdiperf':  ('example_apps/gdiperf', 'GDIPERF.EXE', ['d400', 'w1', 'pshot']),
    # ruch mysza i klawisz w trakcie ciaglych odczytow pliku przez DOS (konczy sie DONE)
    'dosirq':   ('example_apps/dosirq', 'DOSIRQ.EXE',
                 ['w3'] + ['m25,15', 'w0.02', 'm-25,-15', 'w0.02', 'kshift', 'w0.02'] * 70 + ['d60', 'pshot']),
    # Wiersze menu: A y~108, B y~126, C y~144; podmenu od x~209, drugi poziom od x~327.
    # 1: A (otwiera podmenu) -> w dol na B, C -> w gore na B; 2: A -> S1 -> T1 -> skok na C
    'menuhover': ('example_apps/menuhover', 'MENUHOV.EXE',
                  ['w4', 'a110,108', 'w0.5'] + ['m0,6', 'w0.1'] * 9 + ['w0.3'] + ['m0,-6', 'w0.1'] * 6
                  + ['w0.3', 'kesc', 'w1', 'a110,108', 'w0.5', 'm110,0', 'w0.5', 'm120,0', 'w0.5', 'm-230,38', 'w0.5']
                  + ['kesc', 'd5', 'pshot', 'kalt-f4', 'w1']),
    # dysk B: przy jednej stacji i wyjeta dyskietka: DOS nie moze prosic o klawisz (konczy sie DONE)
    'dosprompt': ('example_apps/dosprompt', 'DOSPROMP.EXE', ['Meject -f floppy0', 'w0.5', 'kshift', 'd40', 'pshot']),
    # zadania: WinExec, Yield, koniec zadania; Enter zamyka komunikat o wyjatku (konczy sie sama)
    'tasks':    ('example_apps/tasks', 'TASKTEST.EXE', ['w1', 'kret'] * 25 + ['d60', 'w1', 'pshot']),
    # LibEntry DLL ladowanych w trakcie pracy: WinExec i LoadLibrary
    'libdemo':  ('example_apps/libdemo', 'LIBDEMO.EXE', ['d30', 'w1', 'pshot', 'kalt-f4', 'w1']),
    # import po nazwie z KERNEL: stale i Catch/Throw (konczy sie sama)
    'nameimp':  ('example_apps/nameimp', 'NAMEIMP.EXE', ['d30', 'w1', 'pshot']),
    # testy dymne (aplikacje juz na boot.img, bez linii TEST): wystarczy brak wyjatku
    'ski':      (None, 'SKI.EXE', ['w4', 'kdown', 'w0.5', 'kdown', 'w2', 'kleft', 'w1', 'pshot']),
    'win1app':  (None, 'WIN1APP.EXE', ['w3', 'a66,32', 'w0.5', 'c', 'w1', 'a66,52', 'w0.3', 'c', 'w2', 'pshot']),
    'clock':    (None, 'CLOCK.EXE', ['w3', 'pshot1', 'w3', 'pshot2', 'kalt-f4', 'w1']),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fd', default=os.path.join(ROOT, 'boot.img'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'qemu_out', 'examples'))
    ap.add_argument('--hd', help='obraz dysku FreeDOS (np. calmira.img): testy z C:\\TESTS')
    ap.add_argument('--boot-wait', default='22')
    ap.add_argument('names', nargs='*')
    a = ap.parse_args()
    names = a.names or list(APPS)
    if a.hd:
        skip = [n for n in names if n in FLOPPY_ONLY]
        for n in skip:
            print(f'  {n}: pominiety w trybie --hd (test stacji dyskietek)')
        names = [n for n in names if n not in FLOPPY_ONLY]
    os.makedirs(a.out, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix='ex_')
    fd = None
    if not a.hd:
        fd = os.path.join(tmp, 'fd.img')
        shutil.copy(a.fd, fd)
    hd = None
    if a.hd:
        hd = os.path.join(tmp, 'hd.img')
        shutil.copy(a.hd, hd)
        img = hd + '@@1M'
        for src, dst in (('loader.exe', 'LOADER.EXE'), ('kernel.exe', 'KERNEL.EXE'),
                         ('user/USER.EXE', 'USER.EXE'), ('gdi/GDI.EXE', 'GDI.EXE'),
                         ('WIN16API.DB', 'WIN16API.DB')):
            if os.path.exists(os.path.join(ROOT, src)):
                subprocess.run(['mcopy', '-o', '-i', img, os.path.join(ROOT, src), '::WINDOWS/' + dst], check=True)
        for f in sorted(os.listdir(os.path.join(ROOT, 'dlls'))):
            if f.upper().endswith(('.DLL', '.DRV')):
                subprocess.run(['mcopy', '-o', '-i', img, os.path.join(ROOT, 'dlls', f), '::WINDOWS/' + f.upper()], check=True)
        subprocess.run(['mcopy', '-o', '-i', img, os.path.join(ROOT, 'windir', 'WIN.INI'), '::WINDOWS/WIN.INI'], check=True)
        bat = os.path.join(tmp, 'FDAUTO.BAT')
        with open(bat, 'w', newline='') as f:
            f.write('@echo off\r\nSET PATH=C:\\WINDOWS;C:\\FREEDOS\\BIN\r\nSET PROMPT=$P$G\r\nC:\r\ncd \\TESTS\r\n')
        subprocess.run(['mcopy', '-o', '-i', img, bat, '::FDAUTO.BAT'], check=True)
        subprocess.run(['mmd', '-D', 's', '-i', img, '::TESTS'], stderr=subprocess.DEVNULL)
        fd_target, fd = fd, img                       # aplikacje laduja na wolumin
        prefix = '::TESTS/'
        for n in names:
            if APPS[n][0] is None and n in SMOKE_SRC:
                subprocess.run(['mcopy', '-o', '-i', img, os.path.join(ROOT, SMOKE_SRC[n]),
                                prefix + APPS[n][1]], check=True)
    else:
        prefix = '::'
    for n in names:
        d, exe, _ = APPS[n]
        if d is None:
            continue
        subprocess.run(['make', '-s', '-C', os.path.join(ROOT, d)], check=True)
        subprocess.run(['mcopy', '-o', '-i', fd, os.path.join(ROOT, d, exe), prefix + exe], check=True)
        for src, dst in EXTRA.get(n, []):
            if '/' in dst:                                  # podkatalog
                subprocess.run(['mmd', '-D', 's', '-i', fd, prefix + dst.rsplit('/', 1)[0]], stderr=subprocess.DEVNULL)
            subprocess.run(['mcopy', '-o', '-i', fd, os.path.join(ROOT, src), prefix + dst], check=True)
    if hd:
        fd = fd_target
    bad = False
    summary = []
    for idx, n in enumerate(names):
        d, exe, steps = APPS[n]
        out = os.path.join(a.out, n)
        steps = [s if not s.startswith('p') else ('p' + n if s == 'pshot' or s == 'p' else s) for s in steps]
        disk = ['--fd', fd] if not hd else ['--hd', hd, '--boot', 'c']
        subprocess.run([sys.executable, os.path.join(HERE, 'qemu_headless.py')] + disk +
                       ['--run', 'LOADER.EXE ' + exe, '--out', out, '--port', str(4500 + idx),
                        '--boot-wait', a.boot_wait] + steps,
                       stdout=subprocess.DEVNULL)
        log = open(os.path.join(out, 'serial.log'), errors='replace').read()
        tests = re.findall(r'^TEST \S+ (.*?)\r?$', log, re.M)
        fails = [t for t in tests if ': FAIL' in t]
        if n == 'clock':
            p1 = os.path.join(out, 'shot1.png')
            p2 = os.path.join(out, 'shot2.png')
            if os.path.exists(p1) and os.path.exists(p2):
                from PIL import Image, ImageChops
                diff = ImageChops.difference(Image.open(p1), Image.open(p2)).getbbox()
                if not diff:
                    fails.append('sekundnik zegara nie porusza sie')
            else:
                fails.append('brak zrzutow ekranu zegara')
        done = [t for t in tests if t.startswith('DONE')]
        exc = re.findall(r'\*\*\* EXCEPTION.*', log)
        for t in fails:
            print(f'  {n}: {t}')
        for e in exc:
            print(f'  {n}: {e}')
        loop = log.count('koniec zadania') > 50          # zapetlony koniec ostatniego zadania
        nodos = n in BACK_TO_DOS and 'App zakonczyla' not in log
        if loop: print(f'  {n}: koniec zadania powtarza sie {log.count("koniec zadania")} razy')
        if nodos: print(f'  {n}: po zamknieciu brak powrotu do DOS (brak "App zakonczyla")')
        ok = not fails and (done or d is None) and not exc and not loop and not nodos
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
