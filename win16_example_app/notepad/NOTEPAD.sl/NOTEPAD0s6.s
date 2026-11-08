BITS 16
CPU 486

SECTION _main_TEXT use16 class=CODE align=1
	GLOBAL _main
_main:
	GLOBAL PASCALMAIN
PASCALMAIN:
		add	bp,1
		push	bp
		mov	bp,sp
		sub	sp,34
		push	ds
		mov	ax,NOTEPAD_DATA
		mov	ds,ax
		call	far fpc_initializeunits
		mov	word [bp-34],0
		mov	word [bp-32],0
		push	word 1
		push	ss
		lea	ax,[bp-28]
		push	ax
		push	ss
		lea	ax,[bp-10]
		push	ax
		call	far fpc_pushexceptaddr
		push	dx
		push	ax
		call	far fpc_setjmp
		push	ax
		cmp	ax,0
		jne	..@j38
		mov	ax,SEG __fpc_HInstance
		mov	es,ax
		mov	ax,word [es:__fpc_HInstance]
		mov	word [U_$P$NOTEPAD_$$_G_INSTANCE],ax
		push	ds
		mov	ax,word U_$P$NOTEPAD_$$_WNDCLASS
		push	ax
		push	word 26
		push	word 0
		call	far SYSTEM_$$_FILLCHAR$formal$WORD$BYTE
		mov	word [U_$P$NOTEPAD_$$_WNDCLASS],3
		mov	ax,word P$NOTEPAD_$$_WNDPROC$WORD$WORD$WORD$LONGINT$$LONGINT
		mov	dx,SEG P$NOTEPAD_$$_WNDPROC$WORD$WORD$WORD$LONGINT$$LONGINT
		mov	word [U_$P$NOTEPAD_$$_WNDCLASS+2],ax
		mov	word [U_$P$NOTEPAD_$$_WNDCLASS+4],dx
		mov	ax,word [U_$P$NOTEPAD_$$_G_INSTANCE]
		mov	word [U_$P$NOTEPAD_$$_WNDCLASS+10],ax
		push	word 0
		push	word 0
		push	word 32512
		call	far _$dll$user$LoadIcon
		mov	word [U_$P$NOTEPAD_$$_WNDCLASS+12],ax
		push	word 0
		push	word 0
		push	word 32512
		call	far _$dll$user$LoadCursor
		mov	word [U_$P$NOTEPAD_$$_WNDCLASS+14],ax
		mov	word [U_$P$NOTEPAD_$$_WNDCLASS+16],6
		mov	ax,word _$NOTEPAD$_Ld18
		mov	dx,SEG _$NOTEPAD$_Ld18
		mov	word [U_$P$NOTEPAD_$$_WNDCLASS+22],ax
		mov	word [U_$P$NOTEPAD_$$_WNDCLASS+24],dx
		push	ds
		mov	ax,word U_$P$NOTEPAD_$$_WNDCLASS
		push	ax
		call	far _$dll$user$RegisterClass
		test	ax,ax
		je	..@j40
		jmp	..@j41
..@j40:
		push	word 1
		call	far SYSTEM_$$_HALT$WORD
..@j41:
		mov	dx,word _$NOTEPAD$_Ld18
		mov	ax,SEG _$NOTEPAD$_Ld18
		push	ax
		push	dx
		mov	dx,word _$NOTEPAD$_Ld19
		mov	ax,SEG _$NOTEPAD$_Ld19
		push	ax
		push	dx
		push	word 207
		push	word 0
		push	word -32768
		push	word -32768
		push	word 520
		push	word 360
		push	word 0
		push	word 0
		push	word [U_$P$NOTEPAD_$$_G_INSTANCE]
		push	word 0
		push	word 0
		call	far _$dll$user$CreateWindow
		mov	word [U_$P$NOTEPAD_$$_G_MAINWND],ax
		cmp	word [U_$P$NOTEPAD_$$_G_MAINWND],0
		je	..@j42
		jmp	..@j43
..@j42:
		push	word 1
		call	far SYSTEM_$$_HALT$WORD
..@j43:
		push	word [U_$P$NOTEPAD_$$_G_MAINWND]
		call	far P$NOTEPAD_$$_CREATEMAINMENU$WORD
		push	word [U_$P$NOTEPAD_$$_G_MAINWND]
		push	word 5
		call	far _$dll$user$ShowWindow
		push	word [U_$P$NOTEPAD_$$_G_MAINWND]
		call	far _$dll$user$UpdateWindow
		call	far SYSTEM_$$_PARAMCOUNT$$LONGINT
		cmp	dx,0
		jg	..@j44
		jl	..@j45
		cmp	ax,1
		jae	..@j44
		jmp	..@j45
..@j44:
		push	ds
		mov	ax,word U_$P$NOTEPAD_$$_PARAMS
		push	ax
		push	word 255
		push	ss
		lea	ax,[bp-34]
		push	ax
		push	word 1
		call	far _FPC_ParamStrA
		push	word [bp-32]
		push	word [bp-34]
		call	far fpc_ansistr_to_shortstr
		mov	ax,word U_$P$NOTEPAD_$$_CMDPARAM
		mov	dx,ds
		push	dx
		push	ax
		push	ds
		mov	ax,word U_$P$NOTEPAD_$$_PARAMS
		push	ax
		call	far STRINGS_$$_STRPCOPY$PANSICHAR$SHORTSTRING$$PANSICHAR
		mov	ax,word U_$P$NOTEPAD_$$_CMDPARAM
		mov	dx,ds
		push	dx
		push	ax
		call	far P$NOTEPAD_$$_LOADFILETOEDIT$PCHAR
		jmp	..@j46
..@j45:
		mov	ax,word _$NOTEPAD$_Ld8
		mov	dx,SEG _$NOTEPAD$_Ld8
		push	dx
		push	ax
		call	far P$NOTEPAD_$$_LOADFILETOEDIT$PCHAR
..@j46:
		jmp	..@j48
..@j47:
		push	ds
		mov	ax,word U_$P$NOTEPAD_$$_MSG
		push	ax
		call	far _$dll$user$TranslateMessage
		push	ds
		mov	ax,word U_$P$NOTEPAD_$$_MSG
		push	ax
		call	far _$dll$user$DispatchMessage
..@j48:
		push	ds
		mov	ax,word U_$P$NOTEPAD_$$_MSG
		push	ax
		push	word 0
		push	word 0
		push	word 0
		call	far _$dll$user$GetMessage
		cmp	ax,0
		jne	..@j47
		jmp	..@j49
..@j49:
		push	word [U_$P$NOTEPAD_$$_MSG+4]
		call	far SYSTEM_$$_HALT$WORD
..@j38:
		call	far fpc_popaddrstack
		push	ss
		lea	ax,[bp-34]
		push	ax
		call	far fpc_ansistr_decr_ref
		pop	ax
		cmp	ax,0
		je	..@j37
		call	far fpc_reraise
		pop	ax
		push	word 0
		jmp	..@j38
..@j37:
		call	far fpc_do_exit
		pop	ds
		leave
		sub	bp,1
		retf
; End asmlist al_procedures
; Begin asmlist al_globals
EXTERN	fpc_do_exit
EXTERN	fpc_reraise
EXTERN	fpc_ansistr_decr_ref
EXTERN	fpc_popaddrstack
EXTERN	_$dll$user$GetMessage
EXTERN	_$dll$user$DispatchMessage
EXTERN	_$dll$user$TranslateMessage
EXTERN	U_$P$NOTEPAD_$$_MSG
EXTERN	_$NOTEPAD$_Ld8
EXTERN	P$NOTEPAD_$$_LOADFILETOEDIT$PCHAR
EXTERN	STRINGS_$$_STRPCOPY$PANSICHAR$SHORTSTRING$$PANSICHAR
EXTERN	U_$P$NOTEPAD_$$_CMDPARAM
EXTERN	fpc_ansistr_to_shortstr
EXTERN	_FPC_ParamStrA
EXTERN	U_$P$NOTEPAD_$$_PARAMS
EXTERN	SYSTEM_$$_PARAMCOUNT$$LONGINT
EXTERN	_$dll$user$UpdateWindow
EXTERN	_$dll$user$ShowWindow
EXTERN	P$NOTEPAD_$$_CREATEMAINMENU$WORD
EXTERN	U_$P$NOTEPAD_$$_G_MAINWND
EXTERN	_$dll$user$CreateWindow
EXTERN	_$NOTEPAD$_Ld19
EXTERN	SYSTEM_$$_HALT$WORD
EXTERN	_$dll$user$RegisterClass
EXTERN	_$NOTEPAD$_Ld18
EXTERN	_$dll$user$LoadCursor
EXTERN	_$dll$user$LoadIcon
EXTERN	P$NOTEPAD_$$_WNDPROC$WORD$WORD$WORD$LONGINT$$LONGINT
EXTERN	SYSTEM_$$_FILLCHAR$formal$WORD$BYTE
EXTERN	U_$P$NOTEPAD_$$_WNDCLASS
EXTERN	U_$P$NOTEPAD_$$_G_INSTANCE
EXTERN	__fpc_HInstance
EXTERN	fpc_setjmp
EXTERN	fpc_pushexceptaddr
EXTERN	fpc_initializeunits

SECTION NOTEPAD_DATA use16 class=FAR_DATA align=2

