NASM    = nasm
export WATCOM  = /opt/watcom
export PATH   := /opt/watcom/binl64:$(PATH)
export INCLUDE = /opt/watcom/h

WCL    = wcl
WFLAGS = -l=dos -ms -q        # DOS target, small memory model, quiet
FDBOOT  = ../FD14-FloppyEdition/144m/x86BOOT.img

.PHONY: all disk run run-qemu clean

all: hello.com hello_c.exe boot.img

# --- hello.com (ASM) ---
hello.com: hello.asm
	$(NASM) -f bin hello.asm -o hello.com

# --- hello_c.exe (C, Open Watcom) ---
hello_c.exe: hello_c.c
	$(WCL) $(WFLAGS) hello_c.c -fe=hello_c.exe

# --- obraz dyskietki z FreeDOS + oba programy ---
boot.img: hello.com hello_c.exe fdauto.bat $(FDBOOT)
	cp $(FDBOOT) boot.img
	mdel  -i boot.img ::fdauto.bat
	mcopy -i boot.img fdauto.bat ::fdauto.bat
	mcopy -i boot.img hello.com   ::hello.com
	mcopy -i boot.img hello_c.exe ::hello_c.exe
	@echo "[OK] boot.img gotowy"

# --- uruchom w Bochs ---
run: boot.img
	bochs -f bochsrc -q

# --- uruchom w QEMU (szybszy test, bez GUI debuga) ---
run-qemu: boot.img
	qemu-system-i386 -fda boot.img -boot a -m 16

clean:
	rm -f hello.com boot.img bochs.log
