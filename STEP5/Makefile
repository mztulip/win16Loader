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
# KERNEL.EXE: stub DLL z OutputDebugString (ordinal 1)
# -------------------------------------------------------
kernel_code.obj: kernel.c
	$(WCC) -ms -q -zl -s $< -fo=$@

kernel_code.com: kernel_code.obj
	$(WLINK) format dos com name $@ file $< \
	    option nodefaultlibs option start=OutputDebugString_ option quiet

kernel.exe: kernel_wrap.asm kernel_code.com
	$(NASM) -f bin kernel_wrap.asm -o kernel.exe

# -------------------------------------------------------
# NE test app: importuje OutputDebugString z KERNEL
# -------------------------------------------------------
ne_test_app.obj: ne_test_app.c
	$(WCC) -ms -q -zl -s $< -fo=$@

ne_test.exe: ne_test_app.obj
	$(WLINK) system windows name $@ file $< \
	    import OutputDebugString_ KERNEL.1 \
	    option nodefaultlibs option start=app_entry_ option quiet

# -------------------------------------------------------
boot.img: kernel.exe ne_test.exe loader.exe fdauto.bat $(FDBOOT)
	cp $(FDBOOT) boot.img
	mdel  -i boot.img ::fdauto.bat
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
	rm -f kernel.exe kernel_code.com kernel_code.obj
	rm -f ne_test.exe ne_test_app.obj
	rm -f loader.exe pm_call.obj serial.obj
	rm -f boot.img bochs.log qemu.log
