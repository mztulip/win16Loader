APP  = TASKTEST
OBJS = tasktest.obj
LIBS = shell toolhelp
LINKOPT = import POSTEVENT KERNEL.31
all: child
.PHONY: child
child:
	$(MAKE) -s -C ../taskchild
include ../common.mk
