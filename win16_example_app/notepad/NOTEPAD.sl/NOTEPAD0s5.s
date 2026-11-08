BITS 16
CPU 486

SECTION p$notepad_$$_create$CRC725E0094_TEXT use16 class=CODE align=1
	GLOBAL P$NOTEPAD_$$_CREATEMAINMENU$WORD
P$NOTEPAD_$$_CREATEMAINMENU$WORD:
		add	bp,1
		push	bp
		mov	bp,sp
		sub	sp,4
		push	ds
		mov	ax,NOTEPAD_DATA
		mov	ds,ax
		call	far _$dll$user$CreateMenu
		mov	word [bp-2],ax
		call	far _$dll$user$CreatePopupMenu
		mov	word [bp-4],ax
		push	word [bp-4]
		push	word 0
		push	word 1001
		mov	ax,word _$NOTEPAD$_Ld12
		mov	dx,SEG _$NOTEPAD$_Ld12
		push	dx
		push	ax
		call	far _$dll$user$AppendMenu
		push	word [bp-4]
		push	word 0
		push	word 1002
		mov	ax,word _$NOTEPAD$_Ld13
		mov	dx,SEG _$NOTEPAD$_Ld13
		push	dx
		push	ax
		call	far _$dll$user$AppendMenu
		push	word [bp-4]
		push	word 0
		push	word 1003
		mov	ax,word _$NOTEPAD$_Ld14
		mov	dx,SEG _$NOTEPAD$_Ld14
		push	dx
		push	ax
		call	far _$dll$user$AppendMenu
		push	word [bp-4]
		push	word 0
		push	word 1004
		mov	dx,word _$NOTEPAD$_Ld15
		mov	ax,SEG _$NOTEPAD$_Ld15
		push	ax
		push	dx
		call	far _$dll$user$AppendMenu
		push	word [bp-4]
		push	word 2048
		push	word 0
		push	word 0
		push	word 0
		call	far _$dll$user$AppendMenu
		push	word [bp-4]
		push	word 0
		push	word 1005
		mov	ax,word _$NOTEPAD$_Ld16
		mov	dx,SEG _$NOTEPAD$_Ld16
		push	dx
		push	ax
		call	far _$dll$user$AppendMenu
		push	word [bp-2]
		push	word 16
		push	word [bp-4]
		mov	ax,word _$NOTEPAD$_Ld17
		mov	dx,SEG _$NOTEPAD$_Ld17
		push	dx
		push	ax
		call	far _$dll$user$AppendMenu
		push	word [bp+6]
		push	word [bp-2]
		call	far _$dll$user$SetMenu
		pop	ds
		leave
		sub	bp,1
		retf	2
EXTERN	_$dll$user$SetMenu
EXTERN	_$NOTEPAD$_Ld17
EXTERN	_$NOTEPAD$_Ld16
EXTERN	_$NOTEPAD$_Ld15
EXTERN	_$NOTEPAD$_Ld14
EXTERN	_$NOTEPAD$_Ld13
EXTERN	_$dll$user$AppendMenu
EXTERN	_$NOTEPAD$_Ld12
EXTERN	_$dll$user$CreatePopupMenu
EXTERN	_$dll$user$CreateMenu

SECTION NOTEPAD_DATA use16 class=FAR_DATA align=2

