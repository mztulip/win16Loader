BITS 16
CPU 486

SECTION p$notepad_$$_wndpro$CRC530FDA50_TEXT use16 class=CODE align=1
	GLOBAL P$NOTEPAD_$$_WNDPROC$WORD$WORD$WORD$LONGINT$$LONGINT
P$NOTEPAD_$$_WNDPROC$WORD$WORD$WORD$LONGINT$$LONGINT:
		add	bp,1
		push	bp
		mov	bp,sp
		sub	sp,12
		push	ds
		mov	ax,NOTEPAD_DATA
		mov	ds,ax
		push	si
		push	di
		mov	ax,word [bp+12]
		sub	ax,1
		je	..@j20
		sub	ax,1
		je	..@j24
		sub	ax,3
		je	..@j21
		sub	ax,2
		je	..@j22
		sub	ax,266
		je	..@j23
		jmp	..@j19
..@j20:
		push	word [bp+14]
		push	ss
		lea	ax,[bp-12]
		push	ax
		call	far _$dll$user$GetClientRect
		mov	ax,word _$NOTEPAD$_Ld6
		mov	dx,SEG _$NOTEPAD$_Ld6
		push	dx
		push	ax
		mov	ax,word _$NOTEPAD$_Ld7
		mov	dx,SEG _$NOTEPAD$_Ld7
		push	dx
		push	ax
		push	word 20528
		push	word 196
		push	word 0
		push	word 0
		push	word [bp-8]
		push	word [bp-6]
		push	word [bp+14]
		push	word 101
		push	word [U_$P$NOTEPAD_$$_G_INSTANCE]
		push	word 0
		push	word 0
		call	far _$dll$user$CreateWindow
		mov	word [U_$P$NOTEPAD_$$_G_EDITWND],ax
		mov	word [bp-4],0
		mov	word [bp-2],0
		jmp	..@j18
..@j21:
		cmp	word [U_$P$NOTEPAD_$$_G_EDITWND],0
		jne	..@j25
		jmp	..@j26
..@j25:
		push	word [U_$P$NOTEPAD_$$_G_EDITWND]
		push	word 0
		push	word 0
		push	word [bp+6]
		mov	ax,word [bp+6]
		mov	ax,word [bp+8]
		mov	dx,0
		push	ax
		push	word -1
		call	far _$dll$user$MoveWindow
..@j26:
		mov	word [bp-4],0
		mov	word [bp-2],0
		jmp	..@j18
..@j22:
		cmp	word [U_$P$NOTEPAD_$$_G_EDITWND],0
		jne	..@j27
		jmp	..@j28
..@j27:
		push	word [U_$P$NOTEPAD_$$_G_EDITWND]
		call	far _$dll$user$SetFocus
..@j28:
		mov	word [bp-4],0
		mov	word [bp-2],0
		jmp	..@j18
..@j23:
		mov	ax,word [bp+10]
		sub	ax,1001
		je	..@j30
		sub	ax,1
		je	..@j31
		sub	ax,1
		je	..@j32
		sub	ax,1
		je	..@j33
		sub	ax,1
		je	..@j34
		jmp	..@j29
..@j30:
		mov	dx,word _$NOTEPAD$_Ld8
		mov	ax,SEG _$NOTEPAD$_Ld8
		push	ax
		push	dx
		call	far P$NOTEPAD_$$_LOADFILETOEDIT$PCHAR
		jmp	..@j29
..@j31:
		mov	dx,word _$NOTEPAD$_Ld9
		mov	ax,SEG _$NOTEPAD$_Ld9
		push	ax
		push	dx
		call	far P$NOTEPAD_$$_LOADFILETOEDIT$PCHAR
		jmp	..@j29
..@j32:
		mov	dx,word _$NOTEPAD$_Ld10
		mov	ax,SEG _$NOTEPAD$_Ld10
		push	ax
		push	dx
		call	far P$NOTEPAD_$$_LOADFILETOEDIT$PCHAR
		jmp	..@j29
..@j33:
		mov	ax,word _$NOTEPAD$_Ld11
		mov	dx,SEG _$NOTEPAD$_Ld11
		push	dx
		push	ax
		call	far P$NOTEPAD_$$_LOADFILETOEDIT$PCHAR
		jmp	..@j29
..@j34:
		push	word [bp+14]
		call	far _$dll$user$DestroyWindow
		jmp	..@j29
..@j29:
		mov	word [bp-4],0
		mov	word [bp-2],0
		jmp	..@j18
..@j24:
		push	word 0
		call	far _$dll$user$PostQuitMessage
		mov	word [bp-4],0
		mov	word [bp-2],0
		jmp	..@j18
..@j19:
		push	word [bp+14]
		push	word [bp+12]
		push	word [bp+10]
		push	word [bp+8]
		push	word [bp+6]
		call	far _$dll$user$DefWindowProc
		mov	word [bp-4],ax
		mov	word [bp-2],dx
..@j18:
		mov	ax,word [bp-4]
		mov	dx,word [bp-2]
		pop	di
		pop	si
		pop	ds
		leave
		sub	bp,1
		retf	10
EXTERN	_$dll$user$DefWindowProc
EXTERN	_$dll$user$PostQuitMessage
EXTERN	_$dll$user$DestroyWindow
EXTERN	_$NOTEPAD$_Ld11
EXTERN	_$NOTEPAD$_Ld10
EXTERN	_$NOTEPAD$_Ld9
EXTERN	P$NOTEPAD_$$_LOADFILETOEDIT$PCHAR
EXTERN	_$NOTEPAD$_Ld8
EXTERN	_$dll$user$SetFocus
EXTERN	_$dll$user$MoveWindow
EXTERN	U_$P$NOTEPAD_$$_G_EDITWND
EXTERN	_$dll$user$CreateWindow
EXTERN	U_$P$NOTEPAD_$$_G_INSTANCE
EXTERN	_$NOTEPAD$_Ld7
EXTERN	_$NOTEPAD$_Ld6
EXTERN	_$dll$user$GetClientRect

SECTION NOTEPAD_DATA use16 class=FAR_DATA align=2

