ROOT_DIR ?= $(abspath ../../..)
FPC16    ?= $(ROOT_DIR)/calmira_src/wuil/fpc16
WATCOM_BIN ?= $(wildcard /tmp/watcom/binl64/wrc /opt/watcom/binl64/wrc)
WRC      ?= $(firstword $(WATCOM_BIN))

all: NOTEPAD.exe

OUR_NOTE.ICO: gen_custom_ico.py
	python3 gen_custom_ico.py

NOTEPAD.RES: NOTEPAD.RC OUR_NOTE.ICO
	$(WRC) -q -bt=windows -r -fo=$@ NOTEPAD.RC

NOTEPAD.exe: NOTEPAD.PAS NOTEPAD.RES
	$(FPC16) -Mdelphi -CX -XX NOTEPAD.PAS
	$(WRC) -q -bt=windows NOTEPAD.RES $@

clean:
	rm -rf *.o *.a *.sl NOTEPAD.exe NOTEPAD.RES NOTEPAD.map ppas.sh
