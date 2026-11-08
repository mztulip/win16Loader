BITS 16
CPU 486

SECTION stack stack use16 class=STACK align=16
	ALIGN 16,DB 0
	GLOBAL ___stack
___stack:
	GLOBAL ___stackblock0
___stackblock0:	RESB	8192
	GLOBAL ___stacktop
___stacktop:
; End asmlist al_globals
; Begin asmlist al_typedconsts

SECTION NOTEPAD_DATA use16 class=FAR_DATA align=2

