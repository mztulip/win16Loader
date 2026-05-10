WATCOM  = /opt/watcom
PATH   := $(WATCOM)/binl64:$(PATH)
INCLUDE = $(WATCOM)/h
NASM    = nasm
WCL     = wcl
WCC     = wcc
WLINK   = wlink
FDBOOT  = ../FD14-FloppyEdition/144m/x86BOOT.img

export WATCOM INCLUDE PATH

.PHONY: all run run-qemu clean

all: kernel.exe ne_test.exe loader.exe boot.img

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
# KERNEL.EXE: prawdziwy DLL skompilowany przez wlink
# - ma segment danych (DGROUP) z g_call_count
# - wlink generuje fixupy INTERNALREF dla MOV AX, DGROUP_sel
# - nasz loader patchuje te fixupy prawdziwym selektorem danych
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
# NE test app: importuje OutputDebugString z KERNEL
# -------------------------------------------------------
ne_test_app.obj: ne_test_app.c
	$(WCC) -ms -q -zl -s $< -fo=$@

ne_test.exe: ne_test_app.obj
	$(WLINK) system windows name $@ file $< \
	    import OUTPUTDEBUGSTRING KERNEL.1 \
	    option nodefaultlibs option start=app_entry_ option quiet

# -------------------------------------------------------
boot.img: kernel.exe ne_test.exe loader.exe fdauto.bat $(FDBOOT)
	cp $(FDBOOT) boot.img
	mdel  -i boot.img ::fdauto.bat 2>/dev/null || true
	mcopy -i boot.img fdauto.bat   ::fdauto.bat
	mcopy -i boot.img kernel.exe   ::kernel.exe
	mcopy -i boot.img ne_test.exe  ::ne_test.exe
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
	rm -f kernel.exe kernel.obj libstubs.obj
	rm -f ne_test.exe ne_test_app.obj
	rm -f loader.exe pm_call.obj serial.obj loader.o
	rm -f boot.img bochs.log qemu.log
