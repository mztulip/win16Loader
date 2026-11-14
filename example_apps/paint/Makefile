# Makefile dla PaintCraft (Win16 / Open Watcom & Pascal)

APP  = PAINT
OBJS = paint.obj
LIBS = commdlg

WATCOM  = /opt/watcom
PATH   := $(WATCOM)/binl64:$(PATH)
INCLUDE = $(WATCOM)/h;$(WATCOM)/h/win
export WATCOM INCLUDE PATH

CC     = wcc
CFLAGS = -ms -w4 -zW -bt=windows -zq -fpc -I..
RC     = wrc
LINK   = wlink

.PHONY: all clean
all: $(APP).EXE

PAINT.ICO: gen_paint_ico.py
	python3 gen_paint_ico.py

paint.obj: paint.c
	$(CC) $(CFLAGS) -fo=$@ $<

$(APP).res: $(APP).RC PAINT.ICO
	$(RC) -r -q -i. -i.. -i$(WATCOM)/h -i$(WATCOM)/h/win $(APP).RC -fo=$@

$(APP).EXE: $(OBJS) $(APP).res
	$(LINK) system windows name $@ file {$(OBJS)} library {clibs $(LIBS)} \
	    option quiet option map option modname=$(APP) option stack=8192 option heapsize=4096
	$(RC) -q $(APP).res $@

clean:
	rm -f *.obj *.res *.map *.EXE *.err
