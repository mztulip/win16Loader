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
; task_switch(save_ss [ax], save_sp [dx], new_ss [bx], new_sp [cx]): near.
; Stan zadania lezy na jego stosie. FS/GS moga trzymac selektor zwolniony w czasie,
; gdy zadanie nie dzialalo: taki zastap zerem, zamiast #GP przy pop.
; ---------------------------------------------------------------------
global task_switch_
task_switch_:
    pushf
    push bp
    push si
    push di
    push ds
    push es
    push fs
    push gs
    mov  si, ax
    mov  [si], ss
    mov  si, dx
    mov  [si], sp
    cli
    mov  ss, bx
    mov  sp, cx
    mov  bp, sp
    mov  cx, 4
.check:
    verr word [ss:bp]
    jz   .ok
    mov  word [ss:bp], 0
.ok:
    add  bp, 2
    loop .check
    pop  gs
    pop  fs
    pop  es
    pop  ds
    pop  di
    pop  si
    pop  bp
    popf
    ret

; Pierwsze wejscie zadania (adres powrotu w ramce z WinExec). Rejestry jak z loadera:
; DS=DGROUP, DI=hInstance, SI=hPrevInstance, ES=PSP, BP=0. Powrot z wejscia = koniec zadania.
extern _g_boot_cs, _g_boot_ip, _g_boot_ds, _g_boot_psp
global task_boot_
task_boot_:
    mov  ax, [_g_boot_cs]
    mov  bx, [_g_boot_ip]
    mov  es, [_g_boot_psp]
    mov  di, [_g_boot_ds]
    xor  si, si
    xor  bp, bp
    push cs
    push task_returned
    push ax
    push bx
    mov  ds, di
    sti
    retf
task_returned:
    mov  ax, 0x4C00
    int  0x21

; libentry_call(cs [ax], ip [dx], ds [bx], heap [cx], psp [si]): near, wynik AX z LibEntry.
; Rejestry jak przy starcie systemu (pm_call.asm): DS=DGROUP, DI=hInstance, CX=sterta,
; ES:SI=linia polecen, BP=0. Wejscie po indirect far call przez ramke na stosie.
global libentry_call_
libentry_call_:
    push bp
    push di
    push si
    push ds
    push es
    push cs
    push .back
    push ax
    push dx
    mov  es, si
    mov  si, 0x80
    mov  di, bx
    mov  ds, bx
    xor  bp, bp
    retf
.back:
    pop  es
    pop  ds
    pop  si
    pop  di
    pop  bp
    ret

; Wejscie z exc_common (pm_call.asm): wyjatek przy wielu zadaniach, juz na stosie KCB_EXC_STACK.
extern task_fault_c_
global fault_entry_
fault_entry_:
    mov  ax, seg _g_d3
    mov  ds, ax
    mov  es, ax
    sti
    call task_fault_c_
.never:
    jmp  .never

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
