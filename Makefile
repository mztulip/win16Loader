WATCOM  = /opt/watcom
PATH   := $(WATCOM)/binl64:$(PATH)
INCLUDE = $(WATCOM)/h
NASM    = nasm
WCC     = wcc
WLINK   = wlink
WCL     = wcl
FDBOOT  = FD14-FloppyEdition/144m/x86BOOT.img

export WATCOM INCLUDE PATH

.PHONY: all run run-qemu clean

SKIFREE_DIR = SKIFREE

# Domyslnie nowe GDI/USER (gdi/, user/); OLDGUI=1: stare user.exe/gdi.exe (SkiFree-only)
ifeq ($(OLDGUI),1)
USER_BIN = user.exe
GDI_BIN  = gdi.exe
else
USER_BIN = user/USER.EXE
GDI_BIN  = gdi/GDI.EXE
endif

# Fonty bitmapowe z Wine (LGPL-2.1+, zrodlo i licencja: fonts/README.md);
# GDI laduje je jako moduly .FON z katalogu systemowego
WINE_FONTS = fonts
FONTS = sserife vgasys vgafix coure smalle

all: kernel.exe $(USER_BIN) $(GDI_BIN) loader.exe win16_example_app/Win1App.exe boot.img

# panic screen (32-bit C, gcc)
panic.bin: panic.c panic.ld
	gcc -m32 -ffreestanding -O0 -fno-builtin -fno-pic -fno-stack-protector \
	    -mno-sse -mno-sse2 -mno-mmx \
	    -nostdlib -Tpanic.ld panic.c -o panic.bin

# PM glue
# DEBUG_STACK=1: zrzut rejestrow/stosu/ramek BP przy wyjatku (make DEBUG_STACK=0 wylacza)
DEBUG_STACK ?= 1
pm_call.obj: pm_call.asm panic.bin
	$(NASM) -f obj -DDEBUG_STACK=$(DEBUG_STACK) pm_call.asm -o pm_call.obj

# Serial
serial.obj: serial.c serial.h
	$(WCC) -ml -q $< -fo=$@

# PM helpers (C implementation of pm_call.asm helpers)
pm_helpers.obj: pm_helpers.c module.h
	$(WCC) -ml -q $< -fo=$@

# Keyboard IRQ1 handler (C czesc; -ms zeby trafic do _TEXT razem z pm_call.asm)
keyboard.obj: keyboard.c
	$(WCC) -ms -q -zl -s $< -fo=$@

# Mouse IRQ12 handler (C czesc; -ms zeby trafic do _TEXT razem z pm_call.asm)
mouse.obj: mouse.c
	$(WCC) -ms -q -zl -s $< -fo=$@

# RC Loader (ETAP 18: generyczny loader zasobow NE)
rc_loader.obj: rc_loader.c rc_loader.h
	$(WCC) -ml -q $< -fo=$@

# Loader
shell.obj: shell.c
	$(WCC) -ml -q shell.c -fo=shell.obj

module.obj: module.c module.h ne.h serial.h
	$(WCC) -ml -q $< -fo=$@

loader.exe: loader.c module.h ne.h pm_call.obj serial.obj pm_helpers.obj keyboard.obj mouse.obj rc_loader.obj shell.obj module.obj
	$(WCL) -ml -l=dos -k32768 -q loader.c pm_call.obj serial.obj pm_helpers.obj keyboard.obj mouse.obj rc_loader.obj shell.obj module.obj -fe=loader.exe

# Baza sygnatur API Win16 (z .spec Wine) dla stubow niezaimplementowanych funkcji
WIN16API.DB: tools/gen_apidb.py tools/imports.py
	python3 tools/gen_apidb.py > $@

# -------------------------------------------------------
# KERNEL.EXE - prawdziwe Win16 ordynaly
# 1,3,4,5,6,7,15,16,17,23,24,30,49,88,90,91,113,115,128,129,137,200
# -------------------------------------------------------
kernel.obj: kernel.c
	$(WCC) -ms -q -zl -s $< -fo=$@

libstubs.obj: libstubs.asm
	$(NASM) -f obj $< -o $@

KOBJS = kernel/kmain.obj kernel/kmem.obj kernel/kdos.obj kernel/kmod.obj \
        kernel/kint.obj kernel/kasm.obj kernel/rt386.obj

kernel/%.obj: kernel/%.c kernel/kernel.h
	$(WCC) -ms -zu -zl -s -q -3 $< -fo=$@

kernel/%.obj: kernel/%.asm
	$(NASM) -f obj $< -o $@

# rt386 w wersji far dla modulow -mm (GDI, USER)
kernel/rt386f.obj: kernel/rt386.asm
	$(NASM) -f obj -DFARCODE $< -o $@

kernel.exe: $(KOBJS) kernel/kernel.lnk
	$(WLINK) system windows_dll name $@ file {$(KOBJS)} \
	    @kernel/kernel.lnk option nodefaultlibs option quiet option map=kernel/kernel.map

# -------------------------------------------------------
# USER.EXE - prawdziwe Win16 ordynaly
# 1,5,6,13,22,28,31,33,37,39,40,41,42,44,50,53,56,57,
# 66,68,81,83,87,88,107,108,109,113,114,124,125,156,173,174,175,176,177,178,232,411,420,500
# -------------------------------------------------------
user.obj: user.c
	$(WCC) -ms -zu -q -zl -s $< -fo=$@

# Mouse message dispatch (WM_MOUSEMOVE etc.) w kontekscie USER.EXE (-zu jak user.c)
mouse_msg.obj: mouse_msg.c
	$(WCC) -ms -zu -q -zl -s $< -fo=$@

user.exe: user.obj mouse_msg.obj libstubs.obj
	$(WLINK) system windows_dll name $@ file user.obj,mouse_msg.obj,libstubs.obj \
	    export MESSAGEBOX.1 \
	    export INITAPP.5 \
	    export POSTQUITMESSAGE.6 \
	    export GETTICKCOUNT.13 \
	    export SETFOCUS.22 \
	    export POSTMESSAGE.28 \
	    export ISICONIC.31 \
	    export GETCLIENTRECT.33 \
	    export SETWINDOWTEXT.37 \
	    export BEGINPAINT.39 \
	    export ENDPAINT.40 \
	    export CREATEWINDOW.41 \
	    export SHOWWINDOW.42 \
	    export OPENICON.44 \
	    export FINDWINDOW.50 \
	    export DESTROYWINDOW.53 \
	    export MOVEWINDOW.56 \
	    export REGISTERCLASS.57 \
	    export GETDC.66 \
	    export RELEASEDC.68 \
	    export FILLRECT.81 \
	    export FRAMERECT.83 \
	    export DIALOGBOX.87 \
	    export ENDDIALOG.88 \
	    export DEFWINDOWPROC.107 \
	    export GETMESSAGE.108 \
	    export PEEKMESSAGE.109 \
	    export TRANSLATEMESSAGE.113 \
	    export DISPATCHMESSAGE.114 \
	    export UPDATEWINDOW.124 \
	    export INVALIDATERECT.125 \
	    export LOADCURSOR.173 \
	    export LOADICON.174 \
	    export LOADBITMAP.175 \
	    export LOADSTRING.176 \
	    export LOADACCELERATORS.177 \
	    export TRANSLATEACCELERATOR.178 \
	    export SETWINDOWPOS.232 \
	    export GETSYSTEMMENU.156 \
	    export APPENDMENU.411 \
	    export WSPRINTF.420=_Wsprintf \
	    export LIBMAIN.500 \
	    option nodefaultlibs option quiet

# -------------------------------------------------------
# GDI.EXE - prawdziwe Win16 ordynaly
# 29,33,34,45,48,51,52,68,69,80,82,87,91,93,200
# -------------------------------------------------------
GOBJS = gdi/gobj.obj gdi/gdc.obj gdi/gsurf.obj gdi/grgn.obj gdi/gdraw.obj \
        gdi/gbmp.obj gdi/gtext.obj gdi/gmisc.obj gdi/gstubs.obj kernel/rt386f.obj

gdi/%.obj: gdi/%.c gdi/gdi.h
	$(WCC) -mm -zu -zl -s -q -3 -ox $< -fo=$@

gdi/%.obj: gdi/%.asm
	$(NASM) -f obj $< -o $@

gdi/gdi.lnk: $(wildcard gdi/*.c) tools/gen_lnk.py
	python3 tools/gen_lnk.py GDI gdi/*.c > $@

# Nowy USER.EXE (user/) - para z nowym GDI (prywatne eksporty Gdi*)
UOBJS = user/umisc.obj user/uwnd.obj user/umsg.obj user/upaint.obj user/ures.obj \
        user/umenu.obj user/udlg.obj user/uctl.obj user/uicon.obj user/uasm.obj kernel/rt386f.obj

user/%.obj: user/%.c user/user.h
	$(WCC) -mm -zu -zl -s -q -3 -ox $< -fo=$@

user/%.obj: user/%.asm
	$(NASM) -f obj $< -o $@

user/user.lnk: $(wildcard user/*.c) tools/gen_lnk.py
	python3 tools/gen_lnk.py USER user/*.c > $@
	echo "export WSPRINTF.420=_wsprintf" >> $@

user/USER.EXE: $(UOBJS) user/user.lnk user/user_imp.lnk
	$(WLINK) system windows_dll name $@ file {$(UOBJS)} \
	    @user/user.lnk @user/user_imp.lnk option nodefaultlibs option quiet option map=user/user.map

# Male DLL systemowe (dlls/): SHELL, KEYBOARD.DRV, TOOLHELP, DDEML, VER, MMSYSTEM, COMMDLG
DLLCOMMON = dlls/w16lib.obj dlls/dllstub.obj kernel/rt386.obj
DLLS = dlls/SHELL.DLL dlls/KEYBOARD.DRV dlls/TOOLHELP.DLL dlls/DDEML.DLL \
       dlls/VER.DLL dlls/MMSYSTEM.DLL dlls/COMMDLG.DLL

dlls/%.obj: dlls/%.c dlls/w16.h
	$(WCC) -ms -zu -zl -s -q -3 -ox $< -fo=$@

dlls/%.obj: dlls/%.asm
	$(NASM) -f obj $< -o $@

# $(1) = plik wynikowy, $(2) = zrodlo (bez .c), $(3) = nazwa modulu NE
define DLL_RULE
dlls/$(2).lnk: dlls/$(2).c tools/gen_lnk.py
	python3 tools/gen_lnk.py $(3) $$< > $$@
dlls/$(1): dlls/$(2).obj $(DLLCOMMON) dlls/$(2).lnk dlls/dll_imp.lnk
	$(WLINK) system windows_dll name $$@ file {dlls/$(2).obj $(DLLCOMMON)} \
	    @dlls/$(2).lnk @dlls/dll_imp.lnk option modname=$(3) \
	    option nodefaultlibs option quiet option map=dlls/$(2).map
endef
$(eval $(call DLL_RULE,SHELL.DLL,shell,SHELL))
$(eval $(call DLL_RULE,KEYBOARD.DRV,keyboard,KEYBOARD))
$(eval $(call DLL_RULE,TOOLHELP.DLL,toolhelp,TOOLHELP))
$(eval $(call DLL_RULE,DDEML.DLL,ddeml,DDEML))
$(eval $(call DLL_RULE,VER.DLL,ver,VER))
$(eval $(call DLL_RULE,MMSYSTEM.DLL,mmsystem,MMSYSTEM))
$(eval $(call DLL_RULE,COMMDLG.DLL,commdlg,COMMDLG))

dlls: $(DLLS)
.PHONY: dlls

# Aplikacje przykladowe/testowe (example_apps/README.md); testy: python3 tools/run_examples.py
EXAMPLES = controls odmenu shell sysdll fonts iconic
example_apps:
	for d in $(EXAMPLES); do $(MAKE) -s -C example_apps/$$d || exit 1; done
.PHONY: example_apps

# Stare GDI (domyslne do czasu ukonczenia nowego USER)
gdi.obj: gdi.c
	$(WCC) -ms -q -zl -s $< -fo=$@

gdi.exe: gdi.obj libstubs.obj
	$(WLINK) system windows_dll name $@ file gdi.obj,libstubs.obj \
	    export PATBLT.29 \
	    export TEXTOUT.33 \
	    export BITBLT.34 \
	    export SELECTOBJECT.45 \
	    export CREATEBITMAP.48 \
	    export CREATECOMPATIBLEBITMAP.51 \
	    export CREATECOMPATIBLEDC.52 \
	    export DELETEDC.68 \
	    export DELETEOBJECT.69 \
	    export GETDEVICECAPS.80 \
	    export GETOBJECT.82 \
	    export GETSTOCKOBJECT.87 \
	    export GETTEXTEXTENT.91 \
	    export GETTEXTMETRICS.93 \
	    export LIBMAIN.200 \
	    option nodefaultlibs option quiet

# Nowe GDI (w budowie, wymaga nowego USER) - cel osobny
gdi/GDI.EXE: $(GOBJS) gdi/gdi.lnk gdi/gdi_imp.lnk
	$(WLINK) system windows_dll name $@ file {$(GOBJS)} \
	    @gdi/gdi.lnk @gdi/gdi_imp.lnk option nodefaultlibs option quiet option map=gdi/gdi.map

# -------------------------------------------------------
# WIN16APP.EXE - importy pod prawdziwymi Win16 numerami
# -------------------------------------------------------
win16app.obj: win16app.c
	$(WCC) -ms -q -zl -s $< -fo=$@

win16app.exe: win16app.obj
	$(WLINK) system windows name $@ file $< \
	    import OUTPUTDEBUGSTRING KERNEL.115 \
	    import REGISTERCLASS    USER.57 \
	    import CREATEWINDOW     USER.41 \
	    import POSTMESSAGE      USER.28 \
	    import POSTQUITMESSAGE  USER.6 \
	    import GETMESSAGE       USER.108 \
	    import TRANSLATEMESSAGE USER.113 \
	    import DISPATCHMESSAGE  USER.114 \
	    import SHOWWINDOW       USER.42 \
	    import UPDATEWINDOW     USER.124 \
	    import BEGINPAINT       USER.39 \
	    import ENDPAINT         USER.40 \
	    import DEFWINDOWPROC    USER.107 \
	    import TEXTOUT          GDI.33 \
	    option nodefaultlibs option start=app_entry_ option quiet

# -------------------------------------------------------
# TEST_EXC.EXE - test ekranu wyjatku (celowe #DE)
# -------------------------------------------------------
test_exc.obj: test_exc.c
	$(WCC) -ms -q -zl -s $< -fo=$@

test_exc.exe: test_exc.obj
	$(WLINK) system windows name $@ file $< \
	    import OUTPUTDEBUGSTRING KERNEL.115 \
	    import POSTQUITMESSAGE   USER.6 \
	    option nodefaultlibs option start=app_entry_ option quiet

# -------------------------------------------------------
# TEST14E.EXE - test ETAP 14e: GlobalAlloc > 64KB z XMS
# -------------------------------------------------------
test14e.obj: test14e.c
	$(WCC) -ms -q -zl -s $< -fo=$@

test14e.exe: test14e.obj
	$(WLINK) system windows name $@ file $< \
	    import OUTPUTDEBUGSTRING KERNEL.115 \
	    import GLOBALALLOC       KERNEL.15 \
	    import POSTQUITMESSAGE   USER.6 \
	    option nodefaultlibs option start=app_entry_ option quiet

# -------------------------------------------------------
# SKITEST.EXE - test renderowania sprite'ow (Case2B + CaseC fix)
# -------------------------------------------------------
skitest.obj: skitest.c
	$(WCC) -ms -zu -q -zl -s $< -fo=$@

skitest.exe: skitest.obj
	$(WLINK) system windows name $@ file $< \
	    import OUTPUTDEBUGSTRING  KERNEL.115 \
	    import POSTQUITMESSAGE    USER.6 \
	    import REGISTERCLASS      USER.57 \
	    import CREATEWINDOW       USER.41 \
	    import SHOWWINDOW         USER.42 \
	    import UPDATEWINDOW       USER.124 \
	    import GETMESSAGE         USER.108 \
	    import PEEKMESSAGE        USER.109 \
	    import TRANSLATEMESSAGE   USER.113 \
	    import DISPATCHMESSAGE    USER.114 \
	    import GETDC              USER.66 \
	    import RELEASEDC          USER.68 \
	    import FILLRECT           USER.81 \
	    import DEFWINDOWPROC      USER.107 \
	    import LOADBITMAP         USER.175 \
	    import PATBLT             GDI.29 \
	    import BITBLT             GDI.34 \
	    import SELECTOBJECT       GDI.45 \
	    import CREATECOMPATIBLEBITMAP GDI.51 \
	    import CREATECOMPATIBLEDC GDI.52 \
	    import DELETEDC           GDI.68 \
	    import DELETEOBJECT       GDI.69 \
	    import GETSTOCKOBJECT     GDI.87 \
	    option nodefaultlibs option start=app_entry_ option quiet

boot_skitest.img: kernel.exe user.exe gdi.exe loader.exe skitest.exe fdauto_skitest.bat fdconfig.sys $(FDBOOT)
	cp $(FDBOOT) boot_skitest.img
	mcopy -i boot_skitest.img -o fdconfig.sys          ::fdconfig.sys
	mdel  -i boot_skitest.img ::fdauto.bat 2>/dev/null || true
	mcopy -i boot_skitest.img fdauto_skitest.bat        ::fdauto.bat
	mcopy -i boot_skitest.img kernel.exe               ::kernel.exe
	mcopy -i boot_skitest.img user.exe                 ::user.exe
	mcopy -i boot_skitest.img gdi.exe                  ::gdi.exe
	mcopy -i boot_skitest.img $(SKIFREE_DIR)/SKI.EXE   ::SKI.EXE
	mcopy -i boot_skitest.img skitest.exe              ::skitest.exe
	mcopy -i boot_skitest.img loader.exe               ::loader.exe
	@echo "[OK] boot_skitest.img gotowy"

run-skitest: boot_skitest.img
	qemu-system-i386 -fda boot_skitest.img -boot a -m 16 \
	    -no-reboot -no-shutdown \
	    -serial stdio \
	    -d cpu_reset,int,guest_errors -D qemu.log

# -------------------------------------------------------
# boot_test.img - obraz do testowania (bez SKI.EXE)
# -------------------------------------------------------
boot_test.img: kernel.exe user.exe gdi.exe loader.exe test14e.exe fdauto_test.bat fdconfig.sys $(FDBOOT)
	cp $(FDBOOT) boot_test.img
	mcopy -i boot_test.img -o fdconfig.sys    ::fdconfig.sys
	mdel  -i boot_test.img ::fdauto.bat 2>/dev/null || true
	mcopy -i boot_test.img fdauto_test.bat ::fdauto.bat
	mcopy -i boot_test.img kernel.exe    ::kernel.exe
	mcopy -i boot_test.img user.exe      ::user.exe
	mcopy -i boot_test.img gdi.exe       ::gdi.exe
	mcopy -i boot_test.img test14e.exe   ::test14e.exe
	mcopy -i boot_test.img loader.exe    ::loader.exe
	@echo "[OK] boot_test.img gotowy"

run-test14e: boot_test.img
	qemu-system-i386 -fda boot_test.img -boot a -m 16 \
	    -no-reboot -no-shutdown \
	    -serial stdio \
	    -d cpu_reset,int,guest_errors -D qemu.log

# -------------------------------------------------------
# WIN1APP.EXE - Win16 example app (ETAP 19 - okno + chrome)
# -------------------------------------------------------
win16_example_app/Win1App.exe:
	$(MAKE) -C win16_example_app

# -------------------------------------------------------
boot.img: kernel.exe $(USER_BIN) $(GDI_BIN) $(DLLS) loader.exe WIN16API.DB win16_example_app/Win1App.exe fdauto.bat fdconfig.sys $(FDBOOT)
	cp $(FDBOOT) boot.img
	mcopy -i boot.img -o fdconfig.sys               ::fdconfig.sys
	mdel  -i boot.img ::fdauto.bat 2>/dev/null || true
	mcopy -i boot.img fdauto.bat                    ::fdauto.bat
	mcopy -i boot.img kernel.exe                    ::kernel.exe
	mcopy -i boot.img $(USER_BIN)                   ::user.exe
	mcopy -i boot.img $(GDI_BIN)                    ::gdi.exe
	mcopy -i boot.img loader.exe                    ::loader.exe
	mcopy -i boot.img WIN16API.DB                   ::win16api.db
	for f in $(DLLS); do mcopy -i boot.img $$f ::$$(basename $$f); done
	for f in $(FONTS); do mcopy -i boot.img $(WINE_FONTS)/$$f.fon ::$$(echo $$f | tr a-z A-Z).FON; done
	mcopy -i boot.img win16_example_app/Win1App.exe ::win1app.exe
	mcopy -i boot.img $(SKIFREE_DIR)/SKI.EXE        ::ski.exe
	@echo "[OK] boot.img gotowy"

run-qemu: boot.img
	qemu-system-i386 -fda boot.img -boot a -m 16 \
	    -no-reboot -no-shutdown \
	    -serial stdio \
	    -d cpu_reset,int,guest_errors -D qemu.log

# run-shell: tryb shell (bez argumentu LOADER.EXE) - tymczasowo podmienia fdauto.bat
run-shell: boot.img
	mcopy -oi boot.img -o fdauto_shell.bat ::fdauto.bat 2>/dev/null || true
	echo "@echo off" > /tmp/fdauto_shell_tmp.bat
	echo "LOADER.EXE" >> /tmp/fdauto_shell_tmp.bat
	mcopy -oi boot.img /tmp/fdauto_shell_tmp.bat ::fdauto.bat
	qemu-system-i386 -fda boot.img -boot a -m 16 \
	    -no-reboot -no-shutdown \
	    -serial stdio \
	    -d cpu_reset,int,guest_errors -D qemu.log ; \
	mcopy -oi boot.img fdauto.bat ::fdauto.bat

run: boot.img
	bochs -f bochsrc -q

# -------------------------------------------------------
# KBD_TEST.EXE - test klawiatury (WM_KEYDOWN -> OutputDebugString)
# -------------------------------------------------------
kbd_test.obj: kbd_test.c
	$(WCC) -ms -q -zl -s $< -fo=$@

kbd_test.exe: kbd_test.obj libstubs.obj
	$(WLINK) system windows name $@ file kbd_test.obj,libstubs.obj \
	    import OUTPUTDEBUGSTRING KERNEL.115 \
	    import REGISTERCLASS     USER.57 \
	    import CREATEWINDOW      USER.41 \
	    import SHOWWINDOW        USER.42 \
	    import UPDATEWINDOW      USER.124 \
	    import PEEKMESSAGE       USER.109 \
	    import GETMESSAGE        USER.108 \
	    import TRANSLATEMESSAGE  USER.113 \
	    import DISPATCHMESSAGE   USER.114 \
	    import DEFWINDOWPROC     USER.107 \
	    import POSTQUITMESSAGE   USER.6 \
	    option nodefaultlibs option start=app_entry_ option quiet

boot_kbd.img: kernel.exe user.exe gdi.exe loader.exe kbd_test.exe fdconfig.sys $(FDBOOT)
	cp $(FDBOOT) boot_kbd.img
	mcopy -i boot_kbd.img -o fdconfig.sys          ::fdconfig.sys
	mdel  -i boot_kbd.img ::fdauto.bat 2>/dev/null || true
	printf '@echo off\r\nLOADER.EXE KBD_TEST.EXE\r\n' | mcopy -i boot_kbd.img - ::fdauto.bat
	mcopy -i boot_kbd.img kernel.exe               ::kernel.exe
	mcopy -i boot_kbd.img user.exe                 ::user.exe
	mcopy -i boot_kbd.img gdi.exe                  ::gdi.exe
	mcopy -i boot_kbd.img kbd_test.exe             ::kbd_test.exe
	mcopy -i boot_kbd.img loader.exe               ::loader.exe
	@echo "[OK] boot_kbd.img gotowy"

# -------------------------------------------------------
# WIN16TEST.EXE - test ETAP 18g: WinMain + zasoby (RT_MENU/DIALOG/ACCEL)
# -------------------------------------------------------
win16test/w16test.exe:
	$(MAKE) -C win16test

boot_win16test.img: kernel.exe user.exe gdi.exe loader.exe win16test/w16test.exe fdauto_win16test.bat fdconfig.sys $(FDBOOT)
	cp $(FDBOOT) boot_win16test.img
	mcopy -i boot_win16test.img -o fdconfig.sys         ::fdconfig.sys
	mdel  -i boot_win16test.img ::fdauto.bat 2>/dev/null || true
	mcopy -i boot_win16test.img fdauto_win16test.bat    ::fdauto.bat
	mcopy -i boot_win16test.img kernel.exe              ::kernel.exe
	mcopy -i boot_win16test.img user.exe                ::user.exe
	mcopy -i boot_win16test.img gdi.exe                 ::gdi.exe
	mcopy -i boot_win16test.img loader.exe              ::loader.exe
	mcopy -i boot_win16test.img win16test/w16test.exe   ::w16test.exe
	@echo "[OK] boot_win16test.img gotowy"

run-win16test: boot_win16test.img
	qemu-system-i386 -fda boot_win16test.img -boot a -m 16 \
	    -no-reboot -no-shutdown \
	    -serial stdio \
	    -display none \
	    -d cpu_reset,guest_errors -D qemu.log 2>/dev/null

# -------------------------------------------------------
# TESTRES.NE - zasob testowy dla test_rc.exe (gen_testres.py)
# -------------------------------------------------------
TESTRES.NE: gen_testres.py
	python3 gen_testres.py

# -------------------------------------------------------
# TEST_RC.EXE - test jednostkowy rc_loader (ETAP 18h)
# -------------------------------------------------------
test_rc.obj: test_rc.c
	$(WCC) -ml -q $< -fo=$@

test_rc.exe: test_rc.obj serial.obj rc_loader.obj
	$(WCL) -ml -l=dos -q test_rc.obj serial.obj rc_loader.obj -fe=test_rc.exe

boot_test_rc.img: test_rc.exe TESTRES.NE win16test/w16test.exe fdauto_test_rc.bat fdconfig.sys $(FDBOOT)
	cp $(FDBOOT) boot_test_rc.img
	mcopy -i boot_test_rc.img -o fdconfig.sys      ::fdconfig.sys
	mdel  -i boot_test_rc.img ::fdauto.bat 2>/dev/null || true
	mcopy -i boot_test_rc.img fdauto_test_rc.bat   ::fdauto.bat
	mcopy -i boot_test_rc.img test_rc.exe          ::test_rc.exe
	mcopy -i boot_test_rc.img TESTRES.NE           ::testres.ne
	mcopy -i boot_test_rc.img win16test/w16test.exe ::w16test.exe
	@echo "[OK] boot_test_rc.img gotowy"

run-test-rc: boot_test_rc.img
	qemu-system-i386 -fda boot_test_rc.img -boot a -m 16 \
	    -no-reboot -no-shutdown \
	    -serial stdio \
	    -display none \
	    -d cpu_reset,guest_errors -D qemu.log 2>/dev/null

# -------------------------------------------------------
# Calmira: dysk twardy C: (64MB FAT16) z C:\CALMIRA, start z dyskietki boot.img
# Calmira II D 3.101.d (GPL-2.0+): http://gaby.de/ftp/pub/win3x/cal3101p.zip -> ../win31_apps/cal3101p
# zrodla: http://web.archive.org/web/20101101214316/http://www.vv-soft.de/calmira/cal3101q.zip
# Loader szuka systemowych DLL (KERNEL/USER/GDI) w swoim katalogu (A:\).
# -------------------------------------------------------
CALMIRA_DIR = ../win31_apps/cal3101p
HDD_MB      = 64

calmira.img: $(wildcard $(CALMIRA_DIR)/*) windir/WIN.INI windir/SYSTEM.INI
	rm -f $@
	truncate -s $(HDD_MB)M $@
	echo 'start=2048, type=6, bootable' | sfdisk -q $@
	mkfs.fat -F 16 -n CALMIRA --offset 2048 $@ $$(( ($(HDD_MB)*1024*1024 - 2048*512) / 1024 )) >/dev/null
	mmd   -i $@@@1M ::CALMIRA
	mcopy -i $@@@1M $(CALMIRA_DIR)/* ::CALMIRA/
	mmd   -i $@@@1M ::WINDOWS
	mcopy -i $@@@1M windir/WIN.INI windir/SYSTEM.INI ::WINDOWS/
	@echo "[OK] calmira.img gotowy"

boot_calmira.img: boot.img
	cp boot.img $@
	printf '@echo off\r\nC:\r\ncd \\CALMIRA\r\nA:\\LOADER.EXE CALMIRA.EXE\r\n' | mcopy -o -i $@ - ::fdauto.bat
	@echo "[OK] boot_calmira.img gotowy"

run-calmira: boot_calmira.img calmira.img
	qemu-system-i386 -drive file=boot_calmira.img,if=floppy,format=raw \
	    -drive file=calmira.img,if=ide,format=raw -boot a -m 16 \
	    -no-reboot -no-shutdown \
	    -serial stdio \
	    -d cpu_reset,guest_errors -D qemu.log

clean:
	rm -f kernel.exe kernel.obj
	rm -f user.exe user.obj
	rm -f gdi.exe gdi.obj
	rm -f loader.exe loader.o
	rm -f libstubs.obj pm_call.obj serial.obj pm_helpers.obj keyboard.obj mouse.obj mouse_msg.obj rc_loader.obj
	rm -f test_rc.exe test_rc.obj TESTRES.NE
	rm -f panic.bin
	rm -f boot.img boot_test_rc.img boot_win16test.img bochs.log qemu.log
	$(MAKE) -C win16test clean
