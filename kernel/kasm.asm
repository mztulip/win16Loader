; kasm.asm - KERNEL: wejscia API oparte na rejestrach (INITTASK, DOS3CALL)
; oraz symbole wymagane przez wlink system windows_dll.
; Kompilacja: nasm -f obj kasm.asm

bits 16

SEL_KCB        equ 0x98
KCB_CALLER_DS  equ 520

segment _DATA public class=DATA use16

global ___win_alloc_flags
global ___win_realloc_flags
___win_alloc_flags:   dw 0
___win_realloc_flags: dw 0

segment _TEXT public class=CODE use16

global __InitRtns
global __FiniRtns
__InitRtns:
    ret
__FiniRtns:
    ret

extern inittask_c_
extern dos3call_c_
extern int_dispatch_c_
extern _g_it_ax, _g_it_bx, _g_it_cx, _g_it_dx, _g_it_si, _g_it_di, _g_it_es
extern _g_d3

; ---------------------------------------------------------------------
; INITTASK (ordinal 91): rejestry wyjsciowe Win16 (patrz kmain.c inittask_c)
; ---------------------------------------------------------------------
global INITTASK
INITTASK:
    push bp
    call inittask_c_
    pop  bp
    mov  ax, [_g_it_es]
    mov  es, ax
    mov  ax, [_g_it_ax]
    mov  bx, [_g_it_bx]
    mov  cx, [_g_it_cx]
    mov  dx, [_g_it_dx]
    mov  si, [_g_it_si]
    mov  di, [_g_it_di]
    retf

; ---------------------------------------------------------------------
; DOS3CALL (ordinal 102): INT 21h z rejestrow wolajacego. DS wolajacego jest
; w KCB (int3f_handler podmienil DS na DGROUP KERNEL). Wynik: rejestry + CF.
; g_d3: ax bx cx dx si di ds es flags
; ---------------------------------------------------------------------
global DOS3CALL
DOS3CALL:
    mov  [_g_d3 + 0], ax
    mov  [_g_d3 + 2], bx
    mov  [_g_d3 + 4], cx
    mov  [_g_d3 + 6], dx
    mov  [_g_d3 + 8], si
    mov  [_g_d3 + 10], di
    mov  [_g_d3 + 14], es
    mov  ax, SEL_KCB
    mov  es, ax
    mov  ax, [es:KCB_CALLER_DS]
    mov  [_g_d3 + 12], ax
    push bp
    call dos3call_c_
    pop  bp
    mov  ax, [_g_d3 + 14]
    mov  es, ax
    mov  bx, [_g_d3 + 2]
    mov  cx, [_g_d3 + 4]
    mov  dx, [_g_d3 + 6]
    mov  si, [_g_d3 + 8]
    mov  di, [_g_d3 + 10]
    test byte [_g_d3 + 16], 1
    mov  ax, [_g_d3 + 0]
    jz   .nc
    stc
    retf
.nc:
    clc
    retf

; ---------------------------------------------------------------------
; int_entry: dyspozytor przerwan programowych z PM (wolany przez softint_common
; w pm_call.asm, adres w KCB[538]). AX=wektor, DX:BX = SS:SP ramki rejestrow.
; Zwraca AX = akcja (0 = IRET, 1 = RETF z FLAGS na stosie).
; ---------------------------------------------------------------------
global int_entry_
int_entry_:
    push ds
    push si
    push di
    push bp
    push cx
    push ax
    mov  ax, seg _g_d3          ; DGROUP KERNEL (fixup SEG16)
    mov  ds, ax
    pop  ax
    call int_dispatch_c_        ; (AX=wektor, DX=ss, BX=sp)
    pop  cx
    pop  bp
    pop  di
    pop  si
    pop  ds
    retf
