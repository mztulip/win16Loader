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

all: kernel.exe user.exe gdi.exe win16app.exe loader.exe boot.img

# PM glue
pm_call.obj: pm_call.asm
	$(NASM) -f obj pm_call.asm -o pm_call.obj

# Serial
serial.obj: serial.c serial.h
	$(WCC) -ml -q $< -fo=$@

# Loader
loader.exe: loader.c pm_call.obj serial.obj
	$(WCL) -ml -l=dos -q loader.c pm_call.obj serial.obj -fe=loader.exe

# -------------------------------------------------------
# KERNEL.EXE
# -------------------------------------------------------
kernel.obj: kernel.c
	$(WCC) -ms -q -zl -s $< -fo=$@

libstubs.obj: libstubs.asm
	$(NASM) -f obj $< -o $@

kernel.exe: kernel.obj libstubs.obj
	$(WLINK) system windows_dll name $@ file kernel.obj,libstubs.obj \
	    export OUTPUTDEBUGSTRING.1 \
	    export LIBMAIN.2 \
	    option nodefaultlibs option quiet

# -------------------------------------------------------
# USER.EXE
# -------------------------------------------------------
user.obj: user.c
	$(WCC) -ms -q -zl -s $< -fo=$@

user.exe: user.obj libstubs.obj
	$(WLINK) system windows_dll name $@ file user.obj,libstubs.obj \
	    export REGISTERCLASS.1 \
	    export CREATEWINDOW.2 \
	    export SENDMESSAGE.3 \
	    export LIBMAIN.4 \
	    export POSTMESSAGE.5 \
	    export POSTQUITMESSAGE.6 \
	    export GETMESSAGE.7 \
	    export TRANSLATEMESSAGE.8 \
	    export DISPATCHMESSAGE.9 \
	    export SHOWWINDOW.10 \
	    export UPDATEWINDOW.11 \
	    export BEGINPAINT.12 \
	    export ENDPAINT.13 \
	    export DEFWINDOWPROC.14 \
	    option nodefaultlibs option quiet

# -------------------------------------------------------
# GDI.EXE
# -------------------------------------------------------
gdi.obj: gdi.c
	$(WCC) -ms -q -zl -s $< -fo=$@

gdi.exe: gdi.obj libstubs.obj
	$(WLINK) system windows_dll name $@ file gdi.obj,libstubs.obj \
	    export GETDC.1 \
	    export RELEASEDC.2 \
	    export TEXTOUT.3 \
	    export LIBMAIN.4 \
	    option nodefaultlibs option quiet

# -------------------------------------------------------
# WIN16APP.EXE
# -------------------------------------------------------
win16app.obj: win16app.c
	$(WCC) -ms -q -zl -s $< -fo=$@

win16app.exe: win16app.obj
	$(WLINK) system windows name $@ file $< \
	    import OUTPUTDEBUGSTRING KERNEL.1 \
	    import REGISTERCLASS    USER.1 \
	    import CREATEWINDOW     USER.2 \
	    import POSTMESSAGE      USER.5 \
	    import POSTQUITMESSAGE  USER.6 \
	    import GETMESSAGE       USER.7 \
	    import TRANSLATEMESSAGE USER.8 \
	    import DISPATCHMESSAGE  USER.9 \
	    import SHOWWINDOW       USER.10 \
	    import UPDATEWINDOW     USER.11 \
	    import BEGINPAINT       USER.12 \
	    import ENDPAINT         USER.13 \
	    import DEFWINDOWPROC    USER.14 \
	    import TEXTOUT          GDI.3 \
	    option nodefaultlibs option start=app_entry_ option quiet

# -------------------------------------------------------
boot.img: kernel.exe user.exe gdi.exe win16app.exe loader.exe fdauto.bat $(FDBOOT)
	cp $(FDBOOT) boot.img
	mdel  -i boot.img ::fdauto.bat 2>/dev/null || true
	mcopy -i boot.img fdauto.bat   ::fdauto.bat
	mcopy -i boot.img kernel.exe   ::kernel.exe
	mcopy -i boot.img user.exe     ::user.exe
	mcopy -i boot.img gdi.exe      ::gdi.exe
	mcopy -i boot.img win16app.exe ::win16app.exe
	mcopy -i boot.img loader.exe   ::loader.exe
	@echo "[OK] boot.img gotowy"

run-qemu: boot.img
	qemu-system-i386 -fda boot.img -boot a -m 16 \
	    -no-reboot -no-shutdown \
	    -serial stdio \
	    -d cpu_reset,int,guest_errors -D qemu.log

run: boot.img
	bochs -f bochsrc -q

clean:
	rm -f kernel.exe kernel.obj
	rm -f user.exe user.obj
	rm -f gdi.exe gdi.obj
	rm -f win16app.exe win16app.obj
	rm -f loader.exe loader.o
	rm -f libstubs.obj pm_call.obj serial.obj
	rm -f boot.img bochs.log qemu.log
