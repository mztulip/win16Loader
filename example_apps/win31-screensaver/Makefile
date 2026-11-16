WATCOM  ?= /opt/watcom
PATH    := $(WATCOM)/binl64:$(PATH)
INCLUDE := $(WATCOM)/h;$(WATCOM)/h/win
EDPATH  := $(WATCOM)/eddat
export WATCOM PATH INCLUDE EDPATH

CC      = $(WATCOM)/binl64/wcc
WLINK   = $(WATCOM)/binl64/wlink
WRC     = $(WATCOM)/binl64/wrc

SRCDIR  = src
TARGET  = WIN31SS.SCR
EXE     = win31ss.exe
OBJ     = win31ss.obj
RES     = win31ss.res
LNK     = win31ss.lnk

SRC     = $(SRCDIR)/win31ss.c
RCFILE  = $(SRCDIR)/win31ss.rc
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

$(RES): $(RCFILE) $(HDRS)
	$(WRC) -bt=windows -r -q -fo=$(RES) -I$(WATCOM)/h/win $(RCFILE)

$(LNK): Makefile
	echo "system windows"                                       > $(LNK)
	echo "name $(EXE)"                                          >> $(LNK)
	echo "file $(OBJ)"                                          >> $(LNK)
	echo "library {clibs maths windows}"                        >> $(LNK)
	echo "option quiet, map"                                    >> $(LNK)
	echo "option modname=WIN31SS"                               >> $(LNK)
	echo "option stack=8192"                                    >> $(LNK)
	echo "option heapsize=16384"                                >> $(LNK)
	echo "option description 'SCRNSAVE : The Tim Saver 3000'"   >> $(LNK)

clean:
	rm -f $(OBJ) $(RES) $(EXE) $(LNK) $(TARGET) win31ss.map
