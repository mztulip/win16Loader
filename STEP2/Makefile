NASM   = nasm
FDBOOT = ../FD14-FloppyEdition/144m/x86BOOT.img

.PHONY: all run run-qemu clean

all: win16.com boot.img

win16.com: win16.asm
	$(NASM) -f bin win16.asm -o win16.com

boot.img: win16.com fdauto.bat $(FDBOOT)
	cp $(FDBOOT) boot.img
	mdel  -i boot.img ::fdauto.bat
	mcopy -i boot.img fdauto.bat ::fdauto.bat
	mcopy -i boot.img win16.com  ::win16.com
	@echo "[OK] boot.img gotowy"

run: boot.img
	bochs -f bochsrc -q

run-qemu: boot.img
	qemu-system-i386 -fda boot.img -boot a -m 16 \
	    -no-reboot -no-shutdown \
	    -d cpu_reset,int,guest_errors \
	    -D qemu.log

clean:
	rm -f win16.com boot.img bochs.log qemu.log
