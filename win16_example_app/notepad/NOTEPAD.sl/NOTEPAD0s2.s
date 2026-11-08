BITS 16
CPU 486

SECTION p$notepad_$$_tryope$CRCB34B82D9_TEXT use16 class=CODE align=1
	GLOBAL P$NOTEPAD_$$_TRYOPENFILE$PCHAR$$SMALLINT
P$NOTEPAD_$$_TRYOPENFILE$PCHAR$$SMALLINT:
		add	bp,1
		push	bp
		mov	bp,sp
		sub	sp,140
		push	ds
		mov	ax,NOTEPAD_DATA
		mov	ds,ax
		push	word [bp+8]
		push	word [bp+6]
		lea	ax,[bp-140]
		mov	dx,ss
		push	dx
		push	ax
		push	word 0
		call	far _$dll$kernel$OpenFile
		mov	word [bp-4],ax
		cmp	word [bp-4],-1
		je	..@j5
		jmp	..@j6
..@j5:
		push	word [bp+8]
		push	word [bp+6]
		push	word 0
		call	far _$dll$kernel$_lopen
		mov	word [bp-4],ax
..@j6:
		mov	ax,word [bp-4]
		mov	word [bp-2],ax
		mov	ax,word [bp-2]
		pop	ds
		leave
		sub	bp,1
		retf	4
EXTERN	_$dll$kernel$_lopen
EXTERN	_$dll$kernel$OpenFile

SECTION NOTEPAD_DATA use16 class=FAR_DATA align=2

