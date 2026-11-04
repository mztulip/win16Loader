# LIBDEMO.EXE + program potomny LIBCHLD.EXE (import statyczny DEMODLL.DLL) + DLL-e
APP  = LIBDEMO
OBJS = libdemo.obj
LINKOPT =
all: LIBCHLD.EXE DEMODLL.DLL DEMODYN.DLL DEMOBAD.DLL
include ../common.mk

DLLFLAGS = -ms -w4 -bd -bt=windows -zW -zq -fpc -I..

demodll.obj: demodll.c demodll.h
	$(CC) $(DLLFLAGS) -fo=$@ demodll.c
demobad.obj: demodll.c demodll.h
	$(CC) $(DLLFLAGS) -DFAIL -fo=$@ demodll.c

DEMODLL.DLL: demodll.obj
	$(LINK) system windows_dll name $@ file demodll.obj option quiet option modname=DEMODLL option heapsize=1024
DEMODYN.DLL: demodll.obj
	$(LINK) system windows_dll name $@ file demodll.obj option quiet option modname=DEMODYN option heapsize=2048
DEMOBAD.DLL: demobad.obj
	$(LINK) system windows_dll name $@ file demobad.obj option quiet option modname=DEMOBAD option heapsize=1024

libchld.obj: libchld.c demodll.h ../testlog.h
	$(CC) $(CFLAGS) -fo=$@ libchld.c
LIBCHLD.res: LIBCHLD.rc
	$(RC) -r -q -i. -i.. -i$(WATCOM)/h -i$(WATCOM)/h/win LIBCHLD.rc -fo=$@
LIBCHLD.EXE: libchld.obj LIBCHLD.res
	$(LINK) system windows name $@ file libchld.obj library clibs import DemoInfo DEMODLL.DEMOINFO \
	    option quiet option modname=LIBCHLD option stack=8192 option heapsize=4096
	$(RC) -q LIBCHLD.res $@
