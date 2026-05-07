WATCOM  = /opt/watcom
PATH   := $(WATCOM)/binl64:$(PATH)
INCLUDE = $(WATCOM)/h
NASM    = nasm
WCL     = wcl
WCC     = wcc
WLINK   = wlink
FDBOOT  = ../FD14-FloppyEdition/144m/x86BOOT.img

export WATCOM INCLUDE PATH

.PHONY: all run run-qemu debug-img clean

all: ne_test.exe loader.exe boot.img

# PM glue
pm_call.obj: pm_call.asm
	$(NASM) -f obj pm_call.asm -o pm_call.obj

pm_call_debug.obj: pm_call.asm
	$(NASM) -f obj -DDEBUG pm_call.asm -o pm_call_debug.obj

# Serial debug output
serial.obj: serial.c serial.h
	$(WCC) -ml -q $< -fo=$@

# Loader: C (Watcom large model DOS) + PM glue + serial
loader.exe: loader.c pm_call.obj serial.obj
	$(WCL) -ml -l=dos -q loader.c pm_call.obj serial.obj -fe=loader.exe

# -------------------------------------------------------
# NE test app: C -> obj -> NE EXE (system windows, 2 segmenty: _TEXT + DGROUP)
# -------------------------------------------------------
ne_test_app.obj: ne_test_app.c
	$(WCC) -ms -q -zl -s $< -fo=$@

ne_test.exe: ne_test_app.obj
	$(WLINK) system windows name $@ file $< \
	    option nodefaultlibs option start=app_entry_ option quiet

# -------------------------------------------------------
boot.img: ne_test.exe loader.exe fdauto.bat $(FDBOOT)
	cp $(FDBOOT) boot.img
	mdel  -i boot.img ::fdauto.bat
	mcopy -i boot.img fdauto.bat  ::fdauto.bat
	mcopy -i boot.img ne_test.exe ::ne_test.exe
	mcopy -i boot.img loader.exe  ::loader.exe
	@echo "[OK] boot.img gotowy"

run: boot.img
	bochs -f bochsrc -q

run-qemu: boot.img
	qemu-system-i386 -fda boot.img -boot a -m 16 \
	    -no-reboot -no-shutdown \
	    -serial stdio \
	    -d cpu_reset,int,guest_errors -D qemu.log

debug-img: ne_test.exe pm_call_debug.obj
	$(WCL) -ml -l=dos -q loader.c pm_call_debug.obj -fe=loader.exe
	cp $(FDBOOT) boot.img
	mdel  -i boot.img ::fdauto.bat
	mcopy -i boot.img fdauto.bat  ::fdauto.bat
	mcopy -i boot.img ne_test.exe ::ne_test.exe
	mcopy -i boot.img loader.exe  ::loader.exe
	@echo "[OK] boot.img (DEBUG) gotowy"

clean:
	rm -f ne_test.exe ne_test_app.obj loader.exe serial.obj
	rm -f pm_call.obj pm_call_debug.obj boot.img bochs.log qemu.log
