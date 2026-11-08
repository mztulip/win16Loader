BITS 16
CPU 486

SECTION p$notepad_$$_loadfi$CRCA9B8A9A3_TEXT use16 class=CODE align=1
	GLOBAL P$NOTEPAD_$$_LOADFILETOEDIT$PCHAR
P$NOTEPAD_$$_LOADFILETOEDIT$PCHAR:
		add	bp,1
		push	bp
		mov	bp,sp
		sub	sp,134
		push	ds
		mov	ax,NOTEPAD_DATA
		mov	ds,ax
		mov	ax,word _$NOTEPAD$_Ld1
		mov	dx,SEG _$NOTEPAD$_Ld1
		push	dx
		push	ax
		call	far _$dll$kernel$OutputDebugString
		push	word [bp+8]
		push	word [bp+6]
		call	far _$dll$kernel$OutputDebugString
		mov	ax,word _$NOTEPAD$_Ld2
		mov	dx,SEG _$NOTEPAD$_Ld2
		push	dx
		push	ax
		call	far _$dll$kernel$OutputDebugString
		push	word [bp+8]
		push	word [bp+6]
		call	far P$NOTEPAD_$$_TRYOPENFILE$PCHAR$$SMALLINT
		mov	word [bp-2],ax
		cmp	word [bp-2],-1
		je	..@j9
		jmp	..@j10
..@j9:
		lea	ax,[bp-133]
		mov	dx,ss
		push	dx
		push	ax
		mov	ax,word _$NOTEPAD$_Ld3
		mov	dx,SEG _$NOTEPAD$_Ld3
		push	dx
		push	ax
		call	far STRINGS_$$_STRCOPY$PANSICHAR$PANSICHAR$$PANSICHAR
		lea	ax,[bp-133]
		mov	dx,ss
		push	dx
		push	ax
		push	word [bp+8]
		push	word [bp+6]
		call	far STRINGS_$$_STRCAT$PANSICHAR$PANSICHAR$$PANSICHAR
		lea	ax,[bp-133]
		mov	dx,ss
		push	dx
		push	ax
		call	far P$NOTEPAD_$$_TRYOPENFILE$PCHAR$$SMALLINT
		mov	word [bp-2],ax
..@j10:
		cmp	word [bp-2],-1
		je	..@j11
		jmp	..@j12
..@j11:
		push	word [U_$P$NOTEPAD_$$_G_EDITWND]
		mov	ax,word _$NOTEPAD$_Ld4
		mov	dx,SEG _$NOTEPAD$_Ld4
		push	dx
		push	ax
		call	far _$dll$user$SetWindowText
		jmp	..@j7
..@j12:
		push	word [bp-2]
		mov	ax,word U_$P$NOTEPAD_$$_G_FILEBUF
		mov	dx,ds
		push	dx
		push	ax
		push	word 16383
		call	far _$dll$kernel$_lread
		mov	word [bp-4],ax
		push	word [bp-2]
		call	far _$dll$kernel$_lclose
		cmp	word [bp-4],0
		ja	..@j13
		jmp	..@j14
..@j13:
		mov	bx,word [bp-4]
		mov	byte [U_$P$NOTEPAD_$$_G_FILEBUF+bx],0
		jmp	..@j15
..@j14:
		mov	byte [U_$P$NOTEPAD_$$_G_FILEBUF],0
..@j15:
		push	word [U_$P$NOTEPAD_$$_G_EDITWND]
		mov	ax,word U_$P$NOTEPAD_$$_G_FILEBUF
		mov	dx,ds
		push	dx
		push	ax
		call	far _$dll$user$SetWindowText
		mov	ax,word U_$P$NOTEPAD_$$_G_TITLEBUF
		mov	dx,ds
		push	dx
		push	ax
		mov	ax,word _$NOTEPAD$_Ld5
		mov	dx,SEG _$NOTEPAD$_Ld5
		push	dx
		push	ax
		call	far STRINGS_$$_STRCOPY$PANSICHAR$PANSICHAR$$PANSICHAR
		mov	ax,word U_$P$NOTEPAD_$$_G_TITLEBUF
		mov	dx,ds
		push	dx
		push	ax
		push	word [bp+8]
		push	word [bp+6]
		call	far STRINGS_$$_STRCAT$PANSICHAR$PANSICHAR$$PANSICHAR
		push	word [U_$P$NOTEPAD_$$_G_MAINWND]
		mov	ax,word U_$P$NOTEPAD_$$_G_TITLEBUF
		mov	dx,ds
		push	dx
		push	ax
		call	far _$dll$user$SetWindowText
..@j7:
		pop	ds
		leave
		sub	bp,1
		retf	4
EXTERN	U_$P$NOTEPAD_$$_G_MAINWND
EXTERN	_$NOTEPAD$_Ld5
EXTERN	U_$P$NOTEPAD_$$_G_TITLEBUF
EXTERN	_$dll$kernel$_lclose
EXTERN	_$dll$kernel$_lread
EXTERN	U_$P$NOTEPAD_$$_G_FILEBUF
EXTERN	_$dll$user$SetWindowText
EXTERN	_$NOTEPAD$_Ld4
EXTERN	U_$P$NOTEPAD_$$_G_EDITWND
EXTERN	STRINGS_$$_STRCAT$PANSICHAR$PANSICHAR$$PANSICHAR
EXTERN	STRINGS_$$_STRCOPY$PANSICHAR$PANSICHAR$$PANSICHAR
EXTERN	_$NOTEPAD$_Ld3
EXTERN	P$NOTEPAD_$$_TRYOPENFILE$PCHAR$$SMALLINT
EXTERN	_$NOTEPAD$_Ld2
EXTERN	_$dll$kernel$OutputDebugString
EXTERN	_$NOTEPAD$_Ld1

SECTION NOTEPAD_DATA use16 class=FAR_DATA align=2

