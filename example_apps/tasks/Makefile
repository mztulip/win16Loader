APP  = TASKTEST
OBJS = tasktest.obj
LIBS = shell
all: child
.PHONY: child
child:
	$(MAKE) -s -C ../taskchild
include ../common.mk
