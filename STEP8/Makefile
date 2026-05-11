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

all: kernel.exe user.exe win16app.exe loader.exe boot.img

# PM glue (bez zmian vs STEP7)
pm_call.obj: pm_call.asm
	$(NASM) -f obj pm_call.asm -o pm_call.obj

# Serial
serial.obj: serial.c serial.h
	$(WCC) -ml -q $< -fo=$@

# Loader
loader.exe: loader.c pm_call.obj serial.obj
	$(WCL) -ml -l=dos -q loader.c pm_call.obj serial.obj -fe=loader.exe

# -------------------------------------------------------
# KERNEL.EXE (bez zmian vs STEP7)
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
# USER.EXE: RegisterClass, CreateWindow, SendMessage
# -------------------------------------------------------
user.obj: user.c
	$(WCC) -ms -q -zl -s $< -fo=$@

user.exe: user.obj libstubs.obj
	$(WLINK) system windows_dll name $@ file user.obj,libstubs.obj \
	    export REGISTERCLASS.1 \
	    export CREATEWINDOW.2 \
	    export SENDMESSAGE.3 \
	    export LIBMAIN.4 \
	    option nodefaultlibs option quiet

# -------------------------------------------------------
# WIN16APP.EXE: apka testowa
# Importuje KERNEL.1 (OutputDebugString) i USER.1/2 (RegisterClass/CreateWindow)
# Kolejnosc importow determinuje Module Reference Table:
#   mod 1 = KERNEL, mod 2 = USER
# -------------------------------------------------------
win16app.obj: win16app.c
	$(WCC) -ms -q -zl -s $< -fo=$@

win16app.exe: win16app.obj
	$(WLINK) system windows name $@ file $< \
	    import OUTPUTDEBUGSTRING KERNEL.1 \
	    import REGISTERCLASS USER.1 \
	    import CREATEWINDOW  USER.2 \
	    option nodefaultlibs option start=app_entry_ option quiet

# -------------------------------------------------------
boot.img: kernel.exe user.exe win16app.exe loader.exe fdauto.bat $(FDBOOT)
	cp $(FDBOOT) boot.img
	mdel  -i boot.img ::fdauto.bat 2>/dev/null || true
	mcopy -i boot.img fdauto.bat   ::fdauto.bat
	mcopy -i boot.img kernel.exe   ::kernel.exe
	mcopy -i boot.img user.exe     ::user.exe
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
	rm -f win16app.exe win16app.obj
	rm -f loader.exe loader.o
	rm -f libstubs.obj pm_call.obj serial.obj
	rm -f boot.img bochs.log qemu.log
