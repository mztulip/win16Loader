WATCOM  ?= /opt/watcom
PATH    := $(WATCOM)/binl64:$(PATH)
INCLUDE := $(WATCOM)/h;$(WATCOM)/h/win
EDPATH  := $(WATCOM)/eddat
export WATCOM PATH INCLUDE EDPATH

CC      = $(WATCOM)/binl64/wcc
WLINK   = $(WATCOM)/binl64/wlink
WRC     = $(WATCOM)/binl64/wrc

SRCDIR  = src
TARGET  = I486SS.SCR
EXE     = i486ss.exe
OBJ     = i486ss.obj
RES     = i486ss.res
LNK     = i486ss.lnk

SRC     = $(SRCDIR)/i486ss.c
RCFILE  = $(SRCDIR)/i486ss.rc
HDRS    = $(SRCDIR)/resource.h

CFLAGS  = -ms -w3 -zW -bt=windows -fpc -zq -fo=$(OBJ)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(EXE)
	cp $(EXE) $(TARGET)

$(EXE): $(OBJ) $(RES) $(LNK)
	$(WLINK) @$(LNK)
	$(WRC) -q $(RES) $(EXE)

$(OBJ): $(SRC) $(HDRS)
	$(CC) $(CFLAGS) $(SRC)

$(RES): $(RCFILE) $(HDRS) $(SRCDIR)/i486ss.ico
	$(WRC) -bt=windows -r -q -fo=$(RES) -I$(WATCOM)/h/win $(RCFILE)

$(LNK): Makefile
	echo "system windows"                                       > $(LNK)
	echo "name $(EXE)"                                          >> $(LNK)
	echo "file $(OBJ)"                                          >> $(LNK)
	echo "library {clibs maths windows}"                        >> $(LNK)
	echo "option quiet, map"                                    >> $(LNK)
	echo "option modname=I486SS"                                >> $(LNK)
	echo "option stack=8192"                                    >> $(LNK)
	echo "option heapsize=16384"                                >> $(LNK)
	echo "option description 'SCRNSAVE : i486 Flag Saver'"      >> $(LNK)

clean:
	rm -f $(OBJ) $(RES) $(EXE) $(LNK) $(TARGET) i486ss.map
