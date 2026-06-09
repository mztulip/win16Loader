WATCOM  = /opt/watcom
PATH   := $(WATCOM)/binl64:$(PATH)
INCLUDE = $(WATCOM)/h
NASM    = nasm
WCC     = wcc
WLINK   = wlink
WCL     = wcl
FDBOOT  = ../FD14-FloppyEdition/144m/x86BOOT.img

export WATCOM INCLUDE PATH

.PHONY: all run run-qemu clean

SKIFREE_DIR = ../SKIFREE

all: kernel.exe user.exe gdi.exe loader.exe boot.img

# panic screen (32-bit C, gcc)
panic.bin: panic.c panic.ld
	gcc -m32 -ffreestanding -O0 -fno-builtin -fno-pic -fno-stack-protector \
	    -mno-sse -mno-sse2 -mno-mmx \
	    -nostdlib -Tpanic.ld panic.c -o panic.bin

# PM glue
pm_call.obj: pm_call.asm panic.bin
	$(NASM) -f obj pm_call.asm -o pm_call.obj

# Serial
serial.obj: serial.c serial.h
	$(WCC) -ml -q $< -fo=$@

# PM helpers (C implementation of pm_call.asm helpers)
pm_helpers.obj: pm_helpers.c
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
loader.exe: loader.c pm_call.obj serial.obj pm_helpers.obj keyboard.obj mouse.obj rc_loader.obj
	$(WCL) -ml -l=dos -q loader.c pm_call.obj serial.obj pm_helpers.obj keyboard.obj mouse.obj rc_loader.obj -fe=loader.exe

# -------------------------------------------------------
# KERNEL.EXE - prawdziwe Win16 ordynaly
# 1,3,4,5,6,7,15,16,17,23,24,30,88,90,91,113,115,128,129,137,200
# -------------------------------------------------------
kernel.obj: kernel.c
	$(WCC) -ms -q -zl -s $< -fo=$@

libstubs.obj: libstubs.asm
	$(NASM) -f obj $< -o $@

kernel.exe: kernel.obj libstubs.obj
	$(WLINK) system windows_dll name $@ file kernel.obj,libstubs.obj \
	    export FATALEXIT.1 \
	    export KERNELSTUB3.3 \
	    export KERNELSTUB4.4 \
	    export LOCALALLOC.5 \
	    export LOCALREALLOC.6 \
	    export LOCALFREE.7 \
	    export GLOBALALLOC.15 \
	    export GLOBALREALLOC.16 \
	    export GLOBALFREE.17 \
	    export LOCKSEGMENT.23 \
	    export UNLOCKSEGMENT.24 \
	    export WAITEVENT.30 \
	    export LSTRCPY.88 \
	    export LSTRLEN.90 \
	    export INITTASK.91 \
	    export KERNELSTUB113.113 \
	    export OUTPUTDEBUGSTRING.115 \
	    export GETPRIVATEPROFILESTRING.128 \
	    export WRITEPRIVATEPROFILESTRING.129 \
	    export FATALAPPEXIT.137 \
	    export LIBMAIN.200 \
	    option nodefaultlibs option quiet

# -------------------------------------------------------
# USER.EXE - prawdziwe Win16 ordynaly
# 1,5,6,13,22,28,31,33,37,39,40,41,42,44,50,53,56,57,
# 66,68,81,83,107,108,109,113,114,124,125,173,174,175,176,232,420,500
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
	    export SETWINDOWPOS.232 \
	    export WSPRINTF.420=_Wsprintf \
	    export LIBMAIN.500 \
	    option nodefaultlibs option quiet

# -------------------------------------------------------
# GDI.EXE - prawdziwe Win16 ordynaly
# 29,33,34,45,48,51,52,68,69,80,82,87,91,93,200
# -------------------------------------------------------
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
boot.img: kernel.exe user.exe gdi.exe loader.exe fdauto.bat fdconfig.sys $(FDBOOT)
	cp $(FDBOOT) boot.img
	mcopy -i boot.img -o fdconfig.sys         ::fdconfig.sys
	mdel  -i boot.img ::fdauto.bat 2>/dev/null || true
	mcopy -i boot.img fdauto.bat              ::fdauto.bat
	mcopy -i boot.img kernel.exe              ::kernel.exe
	mcopy -i boot.img user.exe                ::user.exe
	mcopy -i boot.img gdi.exe                 ::gdi.exe
	mcopy -i boot.img $(SKIFREE_DIR)/SKI.EXE  ::SKI.EXE
	mcopy -i boot.img loader.exe              ::loader.exe
	@echo "[OK] boot.img gotowy"

run-qemu: boot.img
	qemu-system-i386 -fda boot.img -boot a -m 16 \
	    -no-reboot -no-shutdown \
	    -serial stdio \
	    -d cpu_reset,int,guest_errors -D qemu.log

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

clean:
	rm -f kernel.exe kernel.obj
	rm -f user.exe user.obj
	rm -f gdi.exe gdi.obj
	rm -f loader.exe loader.o
	rm -f libstubs.obj pm_call.obj serial.obj pm_helpers.obj keyboard.obj mouse.obj mouse_msg.obj rc_loader.obj
	rm -f panic.bin
	rm -f boot.img bochs.log qemu.log
