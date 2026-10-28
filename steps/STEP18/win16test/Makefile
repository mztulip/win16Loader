WATCOM  = /opt/watcom
PATH   := $(WATCOM)/binl64:$(PATH)
INCLUDE = $(WATCOM)/h;$(WATCOM)/h/win

export WATCOM INCLUDE PATH

CC     = wcc
CFLAGS = -ms -w4 -zW -fpc -d1 -bt=windows -zq
LINK   = wlink
RC     = wrc
EXE    = w16test.exe

.PHONY: all clean

all: $(EXE)

WinMain.obj: WinMain.c Resource.h
	$(CC) $(CFLAGS) -fo=$@ $<

Resource.res: Resource.rc Resource.h
	$(RC) -r -i. -i$(WATCOM)/h -i$(WATCOM)/h/win Resource.rc

$(EXE): WinMain.obj Resource.res
	$(LINK) @win16test.lnk
	$(RC) Resource.res $(EXE)

clean:
	rm -f *.obj *.res *.exe *.map
