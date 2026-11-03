# common.mk - wspolne reguly budowania aplikacji przykladowych (Open Watcom, Win16)
#
# W Makefile aplikacji ustaw:
#   APP  = NAZWA          (nazwa modulu i pliku .EXE, wielkie litery, max 8 znakow)
#   OBJS = plik.obj ...
#   LIBS = shell keyboard (opcjonalnie: dodatkowe biblioteki importow z lib286/win)
# i dolacz:  include ../common.mk

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

%.obj: %.c ../testlog.h
	$(CC) $(CFLAGS) -fo=$@ $<

$(APP).res: $(APP).rc $(wildcard *.h) $(wildcard *.ico)
	$(RC) -r -q -i. -i.. -i$(WATCOM)/h -i$(WATCOM)/h/win $(APP).rc -fo=$@

$(APP).EXE: $(OBJS) $(APP).res
	$(LINK) system windows name $@ file {$(OBJS)} library {clibs $(LIBS)} \
	    option quiet option map option modname=$(APP) option stack=8192 option heapsize=4096
	$(RC) -q $(APP).res $@

clean:
	rm -f *.obj *.res *.map *.EXE *.err
