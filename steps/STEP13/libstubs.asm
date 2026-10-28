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

; =====================================================================
; INITTASK (ordinal 91) - pure assembly so we control all return registers.
;
; Win16 Watcom C startup (C0W.OBJ) saves all registers after InitTask
; and uses them to build WinMain's argument list:
;   DI -> hInstance   (pushed first = deepest = [bp+0xE] in WinMain)
;   SI -> hPrevInst   (pushed second = [bp+0xC]; 0x3D35 checks [bp+6]=SI for
;                      first-instance path: if SI==0 -> RegisterClass called)
;   ES -> lpCmdLine segment (PSP selector = SEL_PSP)
;   BX -> lpCmdLine offset  (0 = empty)
;   DX -> nCmdShow          ([bp+6] in WinMain = last push = DX from here = 0)
;   AX -> zero-check; startup jz-fails if 0; must equal hInst (non-zero)
;
; A C implementation would wrap with push/pop DI/SI, overwriting our values.
; Assembly gives us full control.
;
; Serial output is inline (no external refs) so this object links into any DLL.
; =====================================================================
SEL_KCB_VAL equ 0x98
SEL_PSP_VAL equ 0x38
COM1_DATA   equ 0x3F8
COM1_LSR    equ 0x3FD

global INITTASK
INITTASK:
    ; Read hInst from KCB field 0 (app_hinstance); keep in CX throughout.
    mov  ax, SEL_KCB_VAL
    mov  es, ax
    xor  bx, bx
    mov  cx, [es:bx]        ; CX = hInst (preserved across serial output)

    ; Print "IK\r\n" to COM1 for debug.
.it_w0: mov  dx, COM1_LSR
    in   al, dx
    test al, 0x20
    jz   .it_w0
    mov  dx, COM1_DATA
    mov  al, 'I'
    out  dx, al
.it_w1: mov  dx, COM1_LSR
    in   al, dx
    test al, 0x20
    jz   .it_w1
    mov  dx, COM1_DATA
    mov  al, 'K'
    out  dx, al
.it_w2: mov  dx, COM1_LSR
    in   al, dx
    test al, 0x20
    jz   .it_w2
    mov  dx, COM1_DATA
    mov  al, 0x0D
    out  dx, al
.it_w3: mov  dx, COM1_LSR
    in   al, dx
    test al, 0x20
    jz   .it_w3
    mov  dx, COM1_DATA
    mov  al, 0x0A
    out  dx, al

    ; Set all return registers per Win16 Watcom startup ABI:
    ;   DI = hInstance (1st WinMain arg via startup push)
    ;   SI = 0 (hPrevInst=0; SKI init3 [bp+6]=SI==0 → RegisterClass called)
    ;   BX = 0 (lpCmdLine near offset)
    ;   DX = 0 (nCmdShow)
    ;   ES = SEL_PSP (lpCmdLine segment)
    ;   AX = hInst (non-zero startup check)
    mov  ax, cx             ; AX = hInst (from CX, never corrupted)
    mov  di, cx             ; DI = hInst
    xor  si, si             ; SI = 0 (hPrevInst = 0)
    xor  bx, bx             ; BX = 0
    push cx                 ; save hInst across ES load
    mov  ax, SEL_PSP_VAL
    mov  es, ax             ; ES = SEL_PSP
    pop  ax                 ; AX = hInst
    xor  dx, dx             ; DX = 0 (nCmdShow)
    retf

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
