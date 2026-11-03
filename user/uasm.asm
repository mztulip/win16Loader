; uasm.asm - USER: wywolania procedur aplikacji (pascal far) z C (cdecl far)
; Kompilacja: nasm -f obj uasm.asm

bits 16

segment uasm_TEXT public class=CODE use16

; LRESULT call_proc4(WNDPROC p, WORD ds, HWND h, WORD m, WORD w, LONG l)
;   [bp+6]=p.off [bp+8]=p.seg [bp+10]=ds [bp+12]=h [bp+14]=m [bp+16]=w
;   [bp+18]=l.lo [bp+20]=l.hi
; Procedura dostaje DS (i AX) = ds (instancja), gdy ds != 0 - prologi
; "mov ax,ds; nop" / "push ds; pop ax" przejmuja wtedy wlasciwy DS.
global _call_proc4
_call_proc4:
    push bp
    mov  bp, sp
    push ds
    push si
    push di
    push word [bp+12]
    push word [bp+14]
    push word [bp+16]
    push word [bp+20]
    push word [bp+18]
    mov  ax, [bp+10]
    test ax, ax
    jz   .keep
    mov  ds, ax
.keep:
    mov  ax, ds
    call far [bp+6]
    pop  di
    pop  si
    pop  ds
    pop  bp
    retf

; LRESULT call_hook3(LPVOID p, int code, WORD w, LONG l)
;   [bp+6]=p.off [bp+8]=p.seg [bp+10]=code [bp+12]=w [bp+14]=l.lo [bp+16]=l.hi
global _call_hook3
_call_hook3:
    push bp
    mov  bp, sp
    push ds
    push si
    push di
    push word [bp+10]
    push word [bp+12]
    push word [bp+16]
    push word [bp+14]
    mov  ax, ds
    call far [bp+6]
    pop  di
    pop  si
    pop  ds
    pop  bp
    retf

; LONG call_proc_args(LPVOID p, WORD n, WORD __far *args) - dowolna liczba slow
; (callbacki EnumXxx itd.): args[0] wypychany pierwszy (pascal: od lewej)
;   [bp+6]=p.off [bp+8]=p.seg [bp+10]=n [bp+12]=args.off [bp+14]=args.seg
global _call_proc_args
_call_proc_args:
    push bp
    mov  bp, sp
    push ds
    push si
    push di
    push es
    mov  cx, [bp+10]
    les  si, [bp+12]
.lp:
    jcxz .go
    push word [es:si]
    add  si, 2
    dec  cx
    jmp  .lp
.go:
    mov  ax, ds
    call far [bp+6]
    pop  es
    pop  di
    pop  si
    pop  ds
    pop  bp
    retf

segment _DATA public class=DATA use16
global ___win_alloc_flags
global ___win_realloc_flags
___win_alloc_flags:   dw 0
___win_realloc_flags: dw 0

segment _TEXT public class=CODE use16
global __InitRtns
global __FiniRtns
; wolane z libentry.obj near callem (E8) - musi byc ret, nie retf
__InitRtns:
    ret
__FiniRtns:
    ret
