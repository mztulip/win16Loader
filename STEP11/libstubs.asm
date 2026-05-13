; libstubs.asm - exact symbol stubs for wlink windows_dll runtime
; Defines: ___win_alloc_flags, ___win_realloc_flags, __InitRtns, __FiniRtns
; Must be assembled as OMF (NASM -f obj)

bits 16

segment _DATA public class=DATA use16

global ___win_alloc_flags
global ___win_realloc_flags

___win_alloc_flags:
    dw 0

___win_realloc_flags:
    dw 0

segment _TEXT public class=CODE use16

global __InitRtns
global __FiniRtns

__InitRtns:
    ret

__FiniRtns:
    ret

; __U4M: Watcom 32-bit unsigned multiply runtime helper
; Input:  DX:AX = a,  CX:BX = b
; Output: DX:AX = a * b  (low 32 bits)
;
; a*b mod 2^32 = lo_a*lo_b + (lo_a*hi_b + hi_a*lo_b)*2^16  mod 2^32
global __U4M
__U4M:
    push  si
    push  di

    mov   si, ax        ; si = lo_a
    mov   di, dx        ; di = hi_a

    ; step1: lo_a * lo_b  (full 32-bit)
    mov   ax, si
    mul   bx            ; DX:AX = lo_a * lo_b
    push  dx            ; save carry (hi word of step1)
    push  ax            ; save result_lo

    ; step2: lo_a * hi_b  (only low word contributes to result_hi)
    mov   ax, si
    mul   cx            ; DX:AX = lo_a * hi_b
    mov   si, ax        ; si = lo(lo_a * hi_b)

    ; step3: hi_a * lo_b  (only low word contributes to result_hi)
    ; BX still = lo_b (unchanged), DI = hi_a
    mov   ax, di
    mul   bx            ; DX:AX = hi_a * lo_b

    ; combine: result_hi = carry_step1 + lo(lo_a*hi_b) + lo(hi_a*lo_b)
    pop   di            ; di = result_lo  (from step1 AX)
    pop   bx            ; bx = carry      (from step1 DX)
    add   ax, si        ; ax = lo(hi_a*lo_b) + lo(lo_a*hi_b)
    add   ax, bx        ; ax += carry → ax = result_hi

    mov   dx, ax
    mov   ax, di        ; DX:AX = result

    pop   di
    pop   si
    ret
