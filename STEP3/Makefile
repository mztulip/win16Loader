WATCOM  = /opt/watcom
PATH   := $(WATCOM)/binl64:$(PATH)
INCLUDE = $(WATCOM)/h
NASM    = nasm
WCL     = wcl
FDBOOT  = ../FD14-FloppyEdition/144m/x86BOOT.img

export WATCOM INCLUDE PATH

.PHONY: all run run-qemu debug-img clean

all: ne_app.exe loader.exe boot.img

# NE app: reczny binarny plik NE (16-bit kod)
ne_app.exe: ne_app.asm
	$(NASM) -f bin ne_app.asm -o ne_app.exe

# PM glue: NASM -> OMF object (format linkowany przez Watcom)
pm_call.obj: pm_call.asm
	$(NASM) -f obj pm_call.asm -o pm_call.obj

# PM glue z markerami debug (ABCD w lewym gornym rogu VGA)
pm_call_debug.obj: pm_call.asm
	$(NASM) -f obj -DDEBUG pm_call.asm -o pm_call_debug.obj

# Loader: C (Watcom large model DOS) + PM glue
loader.exe: loader.c pm_call.obj
	$(WCL) -ml -l=dos -q loader.c pm_call.obj -fe=loader.exe

boot.img: ne_app.exe loader.exe fdauto.bat $(FDBOOT)
	cp $(FDBOOT) boot.img
	mdel  -i boot.img ::fdauto.bat
	mcopy -i boot.img fdauto.bat  ::fdauto.bat
	mcopy -i boot.img ne_app.exe  ::ne_app.exe
	mcopy -i boot.img loader.exe  ::loader.exe
	@echo "[OK] boot.img gotowy"

run: boot.img
	bochs -f bochsrc -q

run-qemu: boot.img
	qemu-system-i386 -fda boot.img -boot a -m 16 \
	    -no-reboot -no-shutdown \
	    -d cpu_reset,int,guest_errors -D qemu.log

# Boot image z debug markerami (ABCD w wierszu 0 VGA)
debug-img: ne_app.exe pm_call_debug.obj
	$(WCL) -ml -l=dos -q loader.c pm_call_debug.obj -fe=loader.exe
	cp $(FDBOOT) boot.img
	mdel  -i boot.img ::fdauto.bat
	mcopy -i boot.img fdauto.bat  ::fdauto.bat
	mcopy -i boot.img ne_app.exe  ::ne_app.exe
	mcopy -i boot.img loader.exe  ::loader.exe
	@echo "[OK] boot.img (DEBUG) gotowy"

clean:
	rm -f ne_app.exe loader.exe pm_call.obj pm_call_debug.obj boot.img bochs.log qemu.log
