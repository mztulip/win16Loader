WATCOM  = /opt/watcom
PATH   := $(WATCOM)/binl64:$(PATH)
INCLUDE = $(WATCOM)/h;$(WATCOM)/h/win

export WATCOM INCLUDE PATH

CC     = wcc
CFLAGS = -ms -w4 -zW -fpc -d1 -bt=windows -zq
LINK   = wlink
RC     = wrc
EXE    = Win1App.exe

.PHONY: all clean

all: $(EXE)

%.obj: %.c
	$(CC) $(CFLAGS) -fo=$@ $<

AboutDlg.obj: AboutDlg.c AboutDlg.h Resource.h Globals.h

MainWnd.obj: MainWnd.c MainWnd.h AboutDlg.h Resource.h Globals.h

WinMain.obj: WinMain.c MainWnd.h Resource.h Globals.h

Resource.res: Resource.rc App.ico Resource.h
	$(RC) -r -i. -i$(WATCOM)/h -i$(WATCOM)/h/win Resource.rc

$(EXE): WinMain.obj MainWnd.obj AboutDlg.obj Resource.res
	$(LINK) @Win1App.lnk
	$(RC) Resource.res $(EXE)

clean:
	rm -f *.obj *.o *.res *.map *.exe *.sym
