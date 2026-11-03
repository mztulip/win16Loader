; rt386.asm - pomocnicze procedury runtime Open Watcom (16-bit) na instrukcjach 386.
; Arytmetyka 32-bit: argumenty DX:AX (a) i CX:BX (b), wynik DX:AX (reszta CX:BX).
; Kompilacja: nasm -f obj rt386.asm            (model -ms: wywolania near, KERNEL)
;             nasm -f obj -DFARCODE rt386.asm (model -mm/-ml: wywolania far, GDI/USER)

bits 16
cpu 386

%ifdef FARCODE
%define RET retf
%else
%define RET ret
%endif

segment _TEXT public class=CODE use16

global __U4M
global __I4M
global __U4D
global __I4D

; EAX = DX:AX, EBX = CX:BX
%macro LOAD32 0
    movzx eax, ax
    shl   edx, 16
    or    eax, edx
    movzx ebx, bx
    shl   ecx, 16
    or    ebx, ecx
%endmacro

; DX:AX = EAX, CX:BX = reszta (EDX przed wywolaniem makra)
%macro STORE32 0
    mov   ebx, edx
    mov   ecx, edx
    shr   ecx, 16
    mov   edx, eax
    shr   edx, 16
%endmacro

__U4M:
__I4M:
    push  ecx
    push  ebx
    LOAD32
    imul  eax, ebx
    mov   edx, eax
    shr   edx, 16
    pop   ebx
    pop   ecx
    RET

__U4D:
    LOAD32
    xor   edx, edx
    div   ebx
    STORE32
    RET

__I4D:
    LOAD32
    cdq
    idiv  ebx
    STORE32
    RET
