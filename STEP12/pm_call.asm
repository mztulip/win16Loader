; pm_call.asm - PM glue (STEP12)
;
; Nowe vs STEP11:
;   - SEL_KCB=0x98: Kernel Control Block (16-bit data, base=g_kcb_phys)
;     Struktura KCB (16 bajtow):
;       [0] WORD  app_hinstance  = SEL_APP_DATA (0x40)
;       [2] WORD  next_dyn_sel   = GDYN_FIRST (0x128)
;       [4] DWORD heap_phys      = baza globalnego heapa
;       [8] DWORD heap_next      = aktualny bump pointer (phys)
;       [12] DWORD heap_end      = koniec heapa (phys)
;   - SEL_GDT_ACCESS=0x120: samoodniesienie do GDT (dla dynamicznych deskryptorow)
;     GlobalAlloc w kernel.c pisze przez ten selektor nowe deskryptory.
;   - gdt_dyn (0x128..0x527): 128 slotow dla GlobalAlloc
;
; Selektory GDT:
;   SEL_CODE32=0x08, SEL_DATA32=0x10, SEL_DATASEG=0x18
;   SEL_CODE16=0x20, SEL_DATA16=0x28
;   SEL_APP_CODE=0x30, SEL_PSP=0x38, SEL_APP_DATA=0x40
;   0x48..0x80: DLL code/data (4 DLL max)
;   SEL_THUNK=0x88, SEL_VESA=0x90 (32-bit 1MB alias)
;   SEL_KCB=0x98 (Kernel Control Block)
;   0xA0..0x110: 15 okien VESA po 64KB
;   0x118: SEL_FONT
;   0x120: SEL_GDT_ACCESS (samoodniesienie GDT, dla GlobalAlloc)
;   0x128..0x527: 128 dynamicznych slotow (GlobalAlloc)
;
; Kompilacja: nasm -f obj pm_call.asm -o pm_call.obj

bits 16

SEL_CODE32      equ 0x08
SEL_DATA32      equ 0x10
SEL_DATASEG     equ 0x18
SEL_CODE16      equ 0x20
SEL_DATA16      equ 0x28
SEL_APP_CODE    equ 0x30
SEL_PSP         equ 0x38   ; fake PSP segment (256 B zeroed); ES at app startup
SEL_APP_DATA    equ 0x40
SEL_THUNK       equ 0x88
SEL_VESA        equ 0x90
SEL_KCB         equ 0x98
SEL_VESA_BASE   equ 0xA0
VESA_WIN_COUNT  equ 15
SEL_FONT        equ 0x118
SEL_GDT_ACCESS  equ 0x120
GDYN_FIRST      equ 0x128
GDYN_COUNT      equ 128

; GDT size: 0x128 + 128*8 = 0x528 bytes
GDT_TOTAL_SIZE  equ 0x528
GDT_LIMIT       equ GDT_TOTAL_SIZE - 1

INT3F_MAX_DEPTH equ 8

VESA_COLS       equ 640
VESA_ROWS       equ 480
VESA_BPP        equ 3
VESA_PITCH      equ VESA_COLS * VESA_BPP
VESA_FB_SIZE    equ VESA_COLS * VESA_ROWS * VESA_BPP

FONT_W          equ 8
FONT_H          equ 16

segment _TEXT public class=CODE use16

global pm_call_app_
global get_int3f_off_
global get_int21_off_

extern _g_app_phys
extern _g_code_size
extern _g_entry_ip
extern _g_cs_phys
extern _g_orig_cs
extern _g_orig_ss
extern _g_orig_sp
extern _g_app_data_phys
extern _g_data_size
extern _g_has_data

extern _g_dll_code_phys
extern _g_dll_code_size
extern _g_dll_data_phys
extern _g_dll_data_size
extern _g_dll_has_data
extern _g_ndll

extern _g_thunk_phys
extern _g_thunk_size
extern _g_idt_phys

extern _g_lfb_phys
extern _g_font_phys
extern _g_kcb_phys
extern _g_psp_phys

; =====================================================================
get_int3f_off_:
    mov  ax, int3f_handler
    ret

get_int21_off_:
    mov  ax, int21_handler
    ret

; =====================================================================
pm_call_app_:
    mov  ax, ss
    mov  [cs:saved_ss], ax
    mov  ax, sp
    mov  [cs:saved_sp], ax
    mov  [cs:saved_ds], ds

    mov  ax, [_g_entry_ip]
    mov  [cs:local_entry_ip], ax

    call patch_gdt

    mov  eax, [_g_cs_phys]
    add  eax, pm32_entry
    mov  [cs:jmp32_off], eax

    mov  eax, [_g_cs_phys]
    add  eax, gdt
    mov  [cs:gdtr + 2], eax

    mov  eax, [_g_idt_phys]
    mov  [cs:idtr + 2], eax

    in   al, 0x92
    or   al, 0x02
    and  al, 0xFE
    out  0x92, al

    cli
    lgdt [cs:gdtr]
    lidt [cs:idtr]

    mov  eax, cr0
    or   eax, 0x00000001
    mov  cr0, eax

    db 0x66, 0xEA
jmp32_off: dd 0
           dw SEL_CODE32

; =====================================================================
patch_gdt:
    mov  eax, [_g_cs_phys]
    mov  [cs:gdt_dataseg + 2], ax
    shr  eax, 16
    mov  [cs:gdt_dataseg + 4], al
    mov  [cs:gdt_dataseg + 7], ah

    mov  eax, [_g_cs_phys]
    mov  [cs:gdt_code16 + 2], ax
    shr  eax, 16
    mov  [cs:gdt_code16 + 4], al
    mov  [cs:gdt_code16 + 7], ah

    mov  eax, [_g_cs_phys]
    mov  [cs:gdt_data16 + 2], ax
    shr  eax, 16
    mov  [cs:gdt_data16 + 4], al
    mov  [cs:gdt_data16 + 7], ah

    ; SEL_APP_CODE
    mov  eax, [_g_app_phys]
    mov  [cs:gdt_app_code + 2], ax
    shr  eax, 16
    mov  [cs:gdt_app_code + 4], al
    mov  [cs:gdt_app_code + 7], ah
    mov  ax, [_g_code_size]
    dec  ax
    mov  [cs:gdt_app_code + 0], ax

    ; SEL_PSP (0x38): fake PSP, 256 B zeroed; [PSP:0x2C]=0 -> jcxz skips env scan
    mov  eax, [_g_psp_phys]
    mov  [cs:gdt_psp + 2], ax
    shr  eax, 16
    mov  [cs:gdt_psp + 4], al
    mov  [cs:gdt_psp + 7], ah

    ; rm_jmp
    mov  ax, [_g_orig_cs]
    mov  [cs:rm_jmp + 2], ax

    ; SEL_APP_DATA
    mov  ax, [_g_has_data]
    mov  [cs:local_has_data], ax
    test ax, ax
    jz   .no_app_data
    mov  eax, [_g_app_data_phys]
    mov  [cs:gdt_app_data + 2], ax
    shr  eax, 16
    mov  [cs:gdt_app_data + 4], al
    mov  [cs:gdt_app_data + 7], ah
    mov  word [cs:gdt_app_data + 0], 0xFFFF
.no_app_data:

    ; SEL_THUNK
    mov  eax, [_g_thunk_phys]
    mov  [cs:gdt_thunk + 2], ax
    shr  eax, 16
    mov  [cs:gdt_thunk + 4], al
    mov  [cs:gdt_thunk + 7], ah
    mov  ax, [_g_thunk_size]
    dec  ax
    mov  [cs:gdt_thunk + 0], ax

    ; SEL_VESA
    mov  eax, [_g_lfb_phys]
    mov  [cs:gdt_vesa + 2], ax
    shr  eax, 16
    mov  [cs:gdt_vesa + 4], al
    mov  [cs:gdt_vesa + 7], ah

    ; SEL_KCB (0x98): base = g_kcb_phys, limit=31 statyczny
    mov  eax, [_g_kcb_phys]
    mov  [cs:gdt_kcb + 2], ax
    shr  eax, 16
    mov  [cs:gdt_kcb + 4], al
    mov  [cs:gdt_kcb + 7], ah

    ; SEL_GDT_ACCESS (0x120): base = g_cs_phys + gdt, limit=GDT_LIMIT statyczny
    ; MK_FP(SEL_GDT_ACCESS, sel) -> byte offset 'sel' w GDT = deskryptor dla selektora sel.
    mov  eax, [_g_cs_phys]
    add  eax, gdt
    mov  [cs:gdt_gdt_access + 2], ax
    shr  eax, 16
    mov  [cs:gdt_gdt_access + 4], al
    mov  [cs:gdt_gdt_access + 7], ah

    ; SEL_FONT (0x118)
    mov  eax, [_g_font_phys]
    mov  [cs:gdt_font + 2], ax
    shr  eax, 16
    mov  [cs:gdt_font + 4], al
    mov  [cs:gdt_font + 7], ah

    ; 15 okien VESA
    mov  ebx, [_g_lfb_phys]
    xor  si, si
.vwin_patch_loop:
    cmp  si, VESA_WIN_COUNT
    jge  .vwin_patch_done

    mov  ax, si
    shl  ax, 3
    add  ax, gdt_vwin0
    mov  di, ax

    xor  eax, eax
    mov  ax, si
    shl  eax, 16
    add  eax, ebx

    mov  [cs:di + 2], ax
    shr  eax, 16
    mov  [cs:di + 4], al
    mov  [cs:di + 7], ah

    cmp  si, 14
    je   .vwin_last
    mov  word [cs:di + 0], 0xFFFF
    jmp  .vwin_next
.vwin_last:
    mov  word [cs:di + 0], 0x0FFF
.vwin_next:
    inc  si
    jmp  .vwin_patch_loop
.vwin_patch_done:

    ; DLL entries
    xor  si, si
.dll_patch_loop:
    cmp  si, [_g_ndll]
    jge  .dll_patch_done

    mov  ax, si
    shl  ax, 4
    add  ax, gdt_dll0_code
    mov  di, ax

    mov  bx, si
    shl  bx, 1
    mov  bx, [_g_dll_code_size + bx]
    dec  bx
    mov  [cs:di+0], bx

    mov  bx, si
    shl  bx, 2
    mov  eax, [_g_dll_code_phys + bx]
    mov  [cs:di+2], ax
    shr  eax, 16
    mov  [cs:di+4], al
    mov  byte [cs:di+5], 10011010b
    mov  byte [cs:di+6], 00000000b
    mov  [cs:di+7], ah

    add  di, 8

    mov  bx, si
    shl  bx, 1
    mov  ax, [_g_dll_has_data + bx]
    test ax, ax
    jz   .dll_patch_no_data

    mov  word [cs:di+0], 0xFFFF
    mov  bx, si
    shl  bx, 2
    mov  eax, [_g_dll_data_phys + bx]
    mov  [cs:di+2], ax
    shr  eax, 16
    mov  [cs:di+4], al
    mov  byte [cs:di+5], 10010010b
    mov  byte [cs:di+6], 00000000b
    mov  [cs:di+7], ah
    jmp  .dll_patch_data_ok

.dll_patch_no_data:
    mov  word [cs:di+0], 0
    mov  word [cs:di+2], 0
    mov  word [cs:di+4], 0
    mov  word [cs:di+6], 0

.dll_patch_data_ok:
    inc  si
    jmp  .dll_patch_loop
.dll_patch_done:
    ret

; =====================================================================
bits 32
pm32_entry:
    mov  ax, SEL_DATASEG
    mov  ds, ax
    mov  ax, SEL_DATA32
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax
    mov  esp, 0x0009FC00

    mov  eax, [_g_lfb_phys]
    test eax, eax
    jz   .skip_draw

    mov  ax, SEL_VESA
    mov  es, ax

    xor  edi, edi
    xor  eax, eax
    mov  ecx, VESA_FB_SIZE / 4
    rep  stosd

    xor  edi, edi
    mov  eax, 0xFFFFFFFF
    mov  ecx, (VESA_PITCH * 20) / 4
    rep  stosd

    mov  edi, VESA_PITCH * 20
    mov  eax, 0x00226600
    mov  ecx, (VESA_PITCH * 20) / 4
    rep  stosd

    mov  eax, [_g_font_phys]
    test eax, eax
    jz   .skip_draw

    lea  esi, [str_title]
    mov  ebx, 10
    mov  ecx, 2
    mov  edx, 0x00000000
    call draw_str_32

    lea  esi, [str_mode]
    mov  ebx, 10
    mov  ecx, 22
    mov  edx, 0x00FFFFFF
    call draw_str_32

    lea  esi, [str_font]
    mov  ebx, 10
    mov  ecx, 50
    mov  edx, 0x00FFFFFF
    call draw_str_32

.skip_draw:
    mov  ax, SEL_DATA16
    mov  ss, ax
    mov  esp, 0x0000FFF0

    db 0xEA
    dd pm16_call_app
    dw SEL_CODE16

; =====================================================================
bits 32
draw_char_32:
    push esi
    push edi
    push ebx
    push ecx
    push edx
    push ebp

    and  eax, 0xFF
    shl  eax, 4
    add  eax, [_g_font_phys]
    mov  esi, eax

    imul ecx, VESA_PITCH
    lea  eax, [ebx + ebx*2]
    add  ecx, eax
    mov  edi, ecx

    mov  ebp, edx
    shr  ebp, 16
    and  ebp, 0xFF

    xor  ecx, ecx

.dc_row:
    cmp  ecx, FONT_H
    jge  .dc_done

    movzx eax, byte [fs:esi + ecx]

    push ecx
    push edi

    mov  ecx, 7

.dc_col:
    bt   eax, ecx
    jnc  .dc_bg

    mov  [es:edi], dl
    mov  [es:edi+1], dh
    push eax
    mov  eax, ebp
    mov  [es:edi+2], al
    pop  eax

.dc_bg:
    add  edi, VESA_BPP
    dec  ecx
    jns  .dc_col

    pop  edi
    pop  ecx
    add  edi, VESA_PITCH
    inc  ecx
    jmp  .dc_row

.dc_done:
    pop  ebp
    pop  edx
    pop  ecx
    pop  ebx
    pop  edi
    pop  esi
    ret

bits 32
draw_str_32:
    push esi
    push ebx
    push ecx
    push edx

.ds_loop:
    movzx eax, byte [esi]
    test  al, al
    jz    .ds_done

    call  draw_char_32
    add   ebx, FONT_W
    inc   esi
    jmp   .ds_loop

.ds_done:
    pop  edx
    pop  ecx
    pop  ebx
    pop  esi
    ret

str_title   db "STEP12: GlobalAlloc + KCB + Module Reference Table", 0
str_mode    db "SEL_KCB=0x98  SEL_GDT_ACCESS=0x120  heap=256KB", 0
str_font    db "128 dyn GDT slots (0x128..0x527)  SKI.EXE loader", 0

; =====================================================================
bits 16
pm16_call_app:
    mov  ax, SEL_DATA16
    mov  ds, ax
    mov  ax, SEL_PSP
    mov  es, ax

    mov  ax, [local_entry_ip]
    mov  [call_ptr], ax
    mov  word [call_ptr + 2], SEL_APP_CODE

    cmp  word [local_has_data], 0
    je   .call_app
    mov  ax, SEL_APP_DATA
    mov  ds, ax
    ; Win16: SS=DS=DGROUP before entering WinMain
    mov  ss, ax          ; SS = app data segment (DGROUP)
    mov  sp, 0xFFFE      ; SP = top of 64KB data segment

.call_app:
    call far [cs:call_ptr]

    push ax
    push dx
.dbg_app:
    mov  dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .dbg_app
    mov  dx, 0x3F8
    mov  al, 0x41
    out  dx, al
    pop  dx
    pop  ax

    mov  ax, SEL_DATA16
    mov  ds, ax

    mov  eax, cr0
    and  eax, ~1
    mov  cr0, eax

    jmp  far [rm_jmp]

; =====================================================================
rm_real:
    push ax
    push dx
.dbg_rm:
    mov  dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .dbg_rm
    mov  dx, 0x3F8
    mov  al, 0x52
    out  dx, al
    pop  dx
    pop  ax
    mov  ax, cs
    mov  ds, ax
    mov  ax, [saved_ss]
    mov  ss, ax
    mov  sp, [saved_sp]
    mov  ax, [saved_ds]
    mov  ds, ax
    push ax
    push dx
.dbg_retf:
    mov  dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .dbg_retf
    mov  dx, 0x3F8
    mov  al, 0x46
    out  dx, al
    pop  dx
    pop  ax
    sti
    retf

; =====================================================================
; int3f_handler: thunk dispatcher for Win16 DLL calls.
; SS=SEL_APP_DATA (DGROUP) when called from app; we use ES=SEL_DATA16
; to access loader data (int3f_depth, int3f_stack) without touching SS.
; =====================================================================
int3f_handler:
    push bp
    mov  bp, sp
    push ax
    push bx
    push cx
    push si
    push es

    ; Read thunk: [BP+2]=IP in thunk, [BP+4]=CS of thunk
    mov  si, [bp+2]
    mov  bx, [bp+4]
    mov  es, bx            ; ES = CS of thunk (code seg with thunk bytes)
    mov  bl, byte [es:si]  ; BL = dll_idx
    xor  bh, bh
    mov  cx, [es:si + 3]   ; CX = ordinal (bytes at si+3,si+4; saved before SI overwrite)
    mov  si, [es:si + 1]   ; SI = DLL function offset (from thunk)

    ; Debug: print 'K'/'U'/'G' + ':' + ordinal(4 hex) + '\n'
    ; CX = ordinal, BX = dll_idx; both are already saved on stack at handler entry
    push ax
    push dx
    ; print DLL letter
.dbg_dw0: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .dbg_dw0
    mov  al, byte [cs:dbg_dll_letters + bx]
    mov  dx, 0x3F8
    out  dx, al
    ; print ':'
.dbg_dw1: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .dbg_dw1
    mov  al, ':'
    mov  dx, 0x3F8
    out  dx, al
    ; print ordinal high byte high nybble
.dbg_dw2: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .dbg_dw2
    mov  al, ch
    shr  al, 4
    add  al, '0'
    cmp  al, ':'
    jl   .dbg_n3
    add  al, 7
.dbg_n3:
    mov  dx, 0x3F8
    out  dx, al
    ; print ordinal high byte low nybble
.dbg_dw3: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .dbg_dw3
    mov  al, ch
    and  al, 0x0F
    add  al, '0'
    cmp  al, ':'
    jl   .dbg_n2
    add  al, 7
.dbg_n2:
    mov  dx, 0x3F8
    out  dx, al
    ; print ordinal low byte high nybble
.dbg_dw4: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .dbg_dw4
    mov  al, cl
    shr  al, 4
    add  al, '0'
    cmp  al, ':'
    jl   .dbg_n1
    add  al, 7
.dbg_n1:
    mov  dx, 0x3F8
    out  dx, al
    ; print ordinal low byte low nybble
.dbg_dw5: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .dbg_dw5
    mov  al, cl
    and  al, 0x0F
    add  al, '0'
    cmp  al, ':'
    jl   .dbg_n0
    add  al, 7
.dbg_n0:
    mov  dx, 0x3F8
    out  dx, al
    ; print ' ' + [bp+0] (saved app BP) as 4 hex + '=' + [ss:[bp+0]+4] (outer ret_CS) as 4 hex
    ; BX=dll_idx, CX=ordinal -- push CX to free it; use SI for outer_bp
    push cx
    push si
    ; print space separator
.dbg_sp: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .dbg_sp
    mov  al, ' '
    mov  dx, 0x3F8
    out  dx, al
    ; print [bp+0] (saved app BP) as 4 hex nibbles
    mov  ax, [bp+0]
    mov  cl, ah
    shr  cl, 4
    and  cl, 0x0F
    add  cl, '0'
    cmp  cl, ':'
    jl   .dbg_bp3
    add  cl, 7
.dbg_bp3:
.dbg_bpw3: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .dbg_bpw3
    mov  al, cl
    mov  dx, 0x3F8
    out  dx, al
    mov  ax, [bp+0]
    mov  cl, ah
    and  cl, 0x0F
    add  cl, '0'
    cmp  cl, ':'
    jl   .dbg_bp2
    add  cl, 7
.dbg_bp2:
.dbg_bpw2: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .dbg_bpw2
    mov  al, cl
    mov  dx, 0x3F8
    out  dx, al
    mov  ax, [bp+0]
    mov  cl, al
    shr  cl, 4
    and  cl, 0x0F
    add  cl, '0'
    cmp  cl, ':'
    jl   .dbg_bp1
    add  cl, 7
.dbg_bp1:
.dbg_bpw1: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .dbg_bpw1
    mov  al, cl
    mov  dx, 0x3F8
    out  dx, al
    mov  ax, [bp+0]
    mov  cl, al
    and  cl, 0x0F
    add  cl, '0'
    cmp  cl, ':'
    jl   .dbg_bp0
    add  cl, 7
.dbg_bp0:
.dbg_bpw0: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .dbg_bpw0
    mov  al, cl
    mov  dx, 0x3F8
    out  dx, al
    ; print space + [ss:[bp+0]+2] (ret_IP) as 4 hex nibbles
    mov  si, [bp+0]            ; SI = app's BP (function frame pointer)
    mov  ax, [ss:si+2]         ; AX = ret_IP
.dbg_ipsp: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .dbg_ipsp
    mov  al, ' '
    mov  dx, 0x3F8
    out  dx, al
    mov  cl, ah
    shr  cl, 4
    and  cl, 0x0F
    add  cl, '0'
    cmp  cl, ':'
    jl   .dbg_ipn3
    add  cl, 7
.dbg_ipn3:
.dbg_ipw3: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .dbg_ipw3
    mov  al, cl
    mov  dx, 0x3F8
    out  dx, al
    mov  si, [bp+0]
    mov  ax, [ss:si+2]
    mov  cl, ah
    and  cl, 0x0F
    add  cl, '0'
    cmp  cl, ':'
    jl   .dbg_ip2
    add  cl, 7
.dbg_ip2:
.dbg_ipw2: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .dbg_ipw2
    mov  al, cl
    mov  dx, 0x3F8
    out  dx, al
    mov  si, [bp+0]
    mov  ax, [ss:si+2]
    mov  cl, al
    shr  cl, 4
    and  cl, 0x0F
    add  cl, '0'
    cmp  cl, ':'
    jl   .dbg_ip1
    add  cl, 7
.dbg_ip1:
.dbg_ipw1: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .dbg_ipw1
    mov  al, cl
    mov  dx, 0x3F8
    out  dx, al
    mov  si, [bp+0]
    mov  ax, [ss:si+2]
    mov  cl, al
    and  cl, 0x0F
    add  cl, '0'
    cmp  cl, ':'
    jl   .dbg_ip0
    add  cl, 7
.dbg_ip0:
.dbg_ipw0: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .dbg_ipw0
    mov  al, cl
    mov  dx, 0x3F8
    out  dx, al
    ; now print '=' and [ss:[bp+0]+4]
    mov  si, [bp+0]            ; SI = app's BP (function frame pointer)
    mov  ax, [ss:si+4]         ; AX = outer function's ret_CS (what RETF will load as CS)
    ; nibble 3 (bits 15-12)
    mov  cl, ah
    shr  cl, 4
    and  cl, 0x0F
    add  cl, '0'
    cmp  cl, ':'
    jl   .dbg_csn3
    add  cl, 7
.dbg_csn3:
.dbg_cseq: mov dx, 0x3FD        ; print '=' first
    in   al, dx
    test al, 0x20
    jz   .dbg_cseq
    mov  al, '='
    mov  dx, 0x3F8
    out  dx, al
.dbg_csw3: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .dbg_csw3
    mov  al, cl
    mov  dx, 0x3F8
    out  dx, al
    ; nibble 2 (bits 11-8)
    mov  si, [bp+0]
    mov  ax, [ss:si+4]
    mov  cl, ah
    and  cl, 0x0F
    add  cl, '0'
    cmp  cl, ':'
    jl   .dbg_cs2
    add  cl, 7
.dbg_cs2:
.dbg_csw2: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .dbg_csw2
    mov  al, cl
    mov  dx, 0x3F8
    out  dx, al
    ; nibble 1 (bits 7-4)
    mov  si, [bp+0]
    mov  ax, [ss:si+4]
    mov  cl, al
    shr  cl, 4
    and  cl, 0x0F
    add  cl, '0'
    cmp  cl, ':'
    jl   .dbg_cs1
    add  cl, 7
.dbg_cs1:
.dbg_csw1: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .dbg_csw1
    mov  al, cl
    mov  dx, 0x3F8
    out  dx, al
    ; nibble 0 (bits 3-0)
    mov  si, [bp+0]
    mov  ax, [ss:si+4]
    mov  cl, al
    and  cl, 0x0F
    add  cl, '0'
    cmp  cl, ':'
    jl   .dbg_cs0
    add  cl, 7
.dbg_cs0:
.dbg_csw0: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .dbg_csw0
    mov  al, cl
    mov  dx, 0x3F8
    out  dx, al
    pop  si                    ; restore SI = DLL func offset
    pop  cx                    ; restore CX = ordinal
    ; print '/' + [bp+10] (CS inside 0x5910 at thunk call) as 4 hex
.dbg_bpcs_sl: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .dbg_bpcs_sl
    mov  al, '/'
    mov  dx, 0x3F8
    out  dx, al
    mov  ax, [bp+10]
    mov  cl, ah
    shr  cl, 4
    and  cl, 0x0F
    add  cl, '0'
    cmp  cl, ':'
    jl   .dbg_bpcs3
    add  cl, 7
.dbg_bpcs3:
.dbg_bpcsw3: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .dbg_bpcsw3
    mov  al, cl
    mov  dx, 0x3F8
    out  dx, al
    mov  ax, [bp+10]
    mov  cl, ah
    and  cl, 0x0F
    add  cl, '0'
    cmp  cl, ':'
    jl   .dbg_bpcs2
    add  cl, 7
.dbg_bpcs2:
.dbg_bpcsw2: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .dbg_bpcsw2
    mov  al, cl
    mov  dx, 0x3F8
    out  dx, al
    mov  ax, [bp+10]
    mov  cl, al
    shr  cl, 4
    and  cl, 0x0F
    add  cl, '0'
    cmp  cl, ':'
    jl   .dbg_bpcs1
    add  cl, 7
.dbg_bpcs1:
.dbg_bpcsw1: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .dbg_bpcsw1
    mov  al, cl
    mov  dx, 0x3F8
    out  dx, al
    mov  ax, [bp+10]
    mov  cl, al
    and  cl, 0x0F
    add  cl, '0'
    cmp  cl, ':'
    jl   .dbg_bpcs0
    add  cl, 7
.dbg_bpcs0:
.dbg_bpcsw0: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .dbg_bpcsw0
    mov  al, cl
    mov  dx, 0x3F8
    out  dx, al
    ; print newline
.dbg_dw6: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .dbg_dw6
    mov  al, 0x0A
    mov  dx, 0x3F8
    out  dx, al
    pop  dx
    pop  ax
    ; CX still holds ordinal; gets overwritten during DLL sel computation below (that's fine)

    ; Compute DLL selectors: code=dll_idx*16+0x48, data=dll_idx*16+0x50
    mov  ax, bx
    shl  ax, 4
    add  ax, 0x48          ; AX = DLL code selector
    mov  cx, bx
    shl  cx, 4
    add  cx, 0x50          ; CX = DLL data selector

    push ax                ; save DLL code sel
    push cx                ; save DLL data sel

    ; Switch ES to SEL_DATA16 for loader data (int3f_depth, int3f_stack)
    ; BX is free (was dll_idx, no longer needed for DLL sel computation)
    mov  bx, SEL_DATA16
    mov  es, bx

    ; Get current depth and compute offset into int3f_stack
    mov  ax, [es:int3f_depth]
    mov  bx, ax
    shl  bx, 1
    mov  cx, bx
    shl  cx, 1
    add  bx, cx            ; BX = depth * 6 (6 bytes per stack slot)

    ; Save: caller DS, caller return IP (after CALL FAR to thunk), caller CS
    mov  [es:int3f_stack + bx + 0], ds   ; DS = caller's DGROUP (SS=DS for app)
    mov  cx, [bp+8]                       ; [SS:BP+8] = return IP in app (after CALL FAR)
    mov  [es:int3f_stack + bx + 2], cx
    mov  cx, [bp+10]                      ; [SS:BP+10] = return CS in app
    mov  [es:int3f_stack + bx + 4], cx

    inc  word [es:int3f_depth]

    pop  cx                ; DLL data selector
    pop  ax                ; DLL code selector

    ; Patch IRET frame: redirect to DLL function, trampoline as return addr
    mov  [bp+2], si        ; new IP = DLL function offset
    mov  [bp+4], ax        ; new CS = DLL code selector
    mov  word [bp+8],  int3f_trampoline
    mov  word [bp+10], SEL_CODE16

    mov  ds, cx            ; DS = DLL data selector

    pop  es
    pop  si
    pop  cx
    pop  bx
    pop  ax
    pop  bp
    iret

; =====================================================================
; int3f_trampoline: called when a DLL function returns.
; Uses ES=SEL_DATA16 to access loader data (same base as CS=SEL_CODE16).
; tramp_ret_ip/cs/saved_ds are read back via CS prefix (CS is readable).
; =====================================================================
int3f_trampoline:
    push ax
    push bx
    push cx
    push es

    ; Load loader data segment into ES
    mov  ax, SEL_DATA16
    mov  es, ax

    dec  word [es:int3f_depth]
    mov  ax, [es:int3f_depth]

    mov  bx, ax
    shl  bx, 1
    mov  cx, bx
    shl  cx, 1
    add  bx, cx            ; BX = depth * 6

    ; Read saved state from int3f_stack
    mov  cx, [es:int3f_stack + bx + 0]   ; CX = caller's saved DS
    mov  ax, [es:int3f_stack + bx + 2]   ; AX = return IP
    mov  [es:tramp_ret_ip], ax
    mov  ax, [es:int3f_stack + bx + 4]   ; AX = return CS
    mov  [es:tramp_ret_cs], ax
    mov  [es:tramp_saved_ds], cx

    pop  es
    pop  cx
    pop  bx
    pop  ax

    push ax
    push dx
.dbg_tramp:
    mov  dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .dbg_tramp
    mov  dx, 0x3F8
    mov  al, 0x54
    out  dx, al
    pop  dx
    pop  ax

    ; Print tramp_ret_cs as 4 hex digits + newline (after the 'T')
    push ax
    push cx
    push dx
    mov  ax, [cs:tramp_ret_cs]
    mov  cl, ah
    shr  cl, 4
    and  cl, 0x0F
    add  cl, '0'
    cmp  cl, ':'
    jl   .tr_n3
    add  cl, 7
.tr_n3:
.tr_w3: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .tr_w3
    mov  al, cl
    mov  dx, 0x3F8
    out  dx, al
    mov  ax, [cs:tramp_ret_cs]
    mov  cl, ah
    and  cl, 0x0F
    add  cl, '0'
    cmp  cl, ':'
    jl   .tr_n2
    add  cl, 7
.tr_n2:
.tr_w2: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .tr_w2
    mov  al, cl
    mov  dx, 0x3F8
    out  dx, al
    mov  ax, [cs:tramp_ret_cs]
    mov  cl, al
    shr  cl, 4
    and  cl, 0x0F
    add  cl, '0'
    cmp  cl, ':'
    jl   .tr_n1
    add  cl, 7
.tr_n1:
.tr_w1: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .tr_w1
    mov  al, cl
    mov  dx, 0x3F8
    out  dx, al
    mov  ax, [cs:tramp_ret_cs]
    mov  cl, al
    and  cl, 0x0F
    add  cl, '0'
    cmp  cl, ':'
    jl   .tr_n0
    add  cl, 7
.tr_n0:
.tr_w0: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .tr_w0
    mov  al, cl
    mov  dx, 0x3F8
    out  dx, al
.tr_nl: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .tr_nl
    mov  al, 0x0A
    mov  dx, 0x3F8
    out  dx, al
    pop  dx
    pop  cx
    pop  ax

    ; Restore caller's DS without clobbering AX (which holds DLL return value).
    push ax
    mov  ax, [cs:tramp_saved_ds]
    mov  ds, ax
    pop  ax

    ; Jump to caller's return address (CS prefix: tramp_ret_ip/cs in loader seg)
    db   0x2E, 0xFF, 0x2E
    dw   tramp_ret_ip

tramp_ret_ip:   dw 0
tramp_ret_cs:   dw 0
tramp_saved_ds: dw 0

; =====================================================================
; INT 21h handler (protected mode)
; Handles DOS calls made by Win16 apps from PM:
;   AH=0x35: Get Interrupt Vector -> ES=SEL_DATA16, BX=0
;   AH=0x25: Set Interrupt Vector -> ignore
;   AH=0x4C: Terminate Process   -> CLI/HLT
;   AH=0x30: Get DOS Version     -> AX=0x1E03 (DOS 3.30)
;   others: IRET (ignore)
; =====================================================================
int21_handler:
    push cx
    push ax
    push dx
    mov  cl, ah          ; save AH for display
    ; print '#'
.i21_w0: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .i21_w0
    mov  al, '#'
    mov  dx, 0x3F8
    out  dx, al
    ; print high nybble of cl
.i21_w1: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .i21_w1
    mov  al, cl
    shr  al, 4
    add  al, '0'
    cmp  al, ':'
    jl   .i21_h1
    add  al, 7
.i21_h1:
    mov  dx, 0x3F8
    out  dx, al
    ; print low nybble of cl
.i21_w2: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .i21_w2
    mov  al, cl
    and  al, 0x0F
    add  al, '0'
    cmp  al, ':'
    jl   .i21_h2
    add  al, 7
.i21_h2:
    mov  dx, 0x3F8
    out  dx, al
    ; print newline
.i21_w3: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .i21_w3
    mov  al, 0x0A
    mov  dx, 0x3F8
    out  dx, al
    pop  dx
    pop  ax
    pop  cx
    cmp  ah, 0x35
    je   .get_vec
    cmp  ah, 0x25
    je   .set_vec
    cmp  ah, 0x4C
    je   .terminate
    cmp  ah, 0x30
    je   .get_ver
    iret

.get_vec:
    ; Return fake handler address: ES=SEL_DATA16, BX=0
    push ax
    mov  ax, SEL_DATA16
    mov  es, ax
    pop  ax
    xor  bx, bx
    iret

.set_vec:
    iret

.terminate:
    ; print '!' + exit code (AL) as hex to confirm terminate
    push ax
    push dx
    mov  ah, al          ; save exit code in AH
.term_w0: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .term_w0
    mov  al, '!'
    mov  dx, 0x3F8
    out  dx, al
    ; high nybble of exit code
.term_w1: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .term_w1
    mov  al, ah
    shr  al, 4
    add  al, '0'
    cmp  al, ':'
    jl   .term_h1
    add  al, 7
.term_h1:
    mov  dx, 0x3F8
    out  dx, al
    ; low nybble
.term_w2: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .term_w2
    mov  al, ah
    and  al, 0x0F
    add  al, '0'
    cmp  al, ':'
    jl   .term_h2
    add  al, 7
.term_h2:
    mov  dx, 0x3F8
    out  dx, al
    pop  dx
    pop  ax
    cli
.terminate_loop:
    hlt
    jmp  .terminate_loop

.get_ver:
    ; DOS 3.30: AL=3 (major), AH=0x1E=30 (minor)
    mov  ax, 0x1E03
    iret

; =====================================================================
dbg_dll_letters db 'K', 'U', 'G'   ; indexed by dll_idx (0=KERNEL,1=USER,2=GDI)

saved_ss        dw 0
saved_sp        dw 0
saved_ds        dw 0
local_entry_ip  dw 0
local_has_data  dw 0
call_ptr        dw 0, 0
rm_jmp          dw rm_real
                dw 0

int3f_depth     dw 0
int3f_stack     times INT3F_MAX_DEPTH * 6  db 0

; =====================================================================
; Pomocnicze rutyny seryjne (real mode) dla debugowania
; serial_out_al: wyslij al przez COM1 (0x3F8)
; serial_hex8: wyslij al jako 2 cyfry hex
; Wlacz przez zmiane 0 na 1 ponizej.
; =====================================================================
%define DEBUG_GDT_DUMP  0

%if DEBUG_GDT_DUMP
serial_out_al:
    push dx
    push ax
.wait: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .wait
    pop  ax
    mov  dx, 0x3F8
    out  dx, al
    pop  dx
    ret

serial_hex8:
    push ax
    push cx
    mov  cl, 4
    push ax
    shr  al, cl
    and  al, 0x0F
    add  al, '0'
    cmp  al, '9'
    jle  .hi_ok
    add  al, 7
.hi_ok:
    call serial_out_al
    pop  ax
    and  al, 0x0F
    add  al, '0'
    cmp  al, '9'
    jle  .lo_ok
    add  al, 7
.lo_ok:
    call serial_out_al
    pop  cx
    pop  ax
    ret
%endif  ; DEBUG_GDT_DUMP

; =====================================================================
; Wstawienie GDT dump przed lgdt (wlaczone gdy DEBUG_GDT_DUMP=1):
;   push si
;   mov  si, gdt_thunk
;   mov  cx, 8
; .dump_gdt_thunk:
;   mov  al, 'T'
;   call serial_out_al
;   mov  al, [cs:si]
;   call serial_hex8
;   inc  si
;   loop .dump_gdt_thunk
;   mov  al, 0x0A
;   call serial_out_al
;   pop  si
; =====================================================================
align 8
gdt:
    dq 0                    ; 0x00 null

gdt_code32:                 ; 0x08 - 32-bit flat code (base=0, limit=4GB)
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 11001111b, 0x00

gdt_data32:                 ; 0x10 - 32-bit flat data (base=0, limit=4GB)
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 11001111b, 0x00

gdt_dataseg:                ; 0x18 - 32-bit data, base=g_cs_phys, limit=64KB
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 01000000b, 0x00

gdt_code16:                 ; 0x20 - 16-bit code, base=g_cs_phys
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 00000000b, 0x00

gdt_data16:                 ; 0x28 - 16-bit data, base=g_cs_phys
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 00000000b, 0x00

gdt_app_code:               ; 0x30 - 16-bit code, base=g_app_phys
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 00000000b, 0x00

gdt_psp:                    ; 0x38 - fake PSP (256 B zeroed, [0x2C]=0)
    dw 0x00FF, 0x0000
    db 0x00, 10010010b, 00000000b, 0x00

gdt_app_data:               ; 0x40 - 16-bit data, base=g_app_data_phys
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 00000000b, 0x00

gdt_dll0_code:              ; 0x48
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 00000000b, 0x00
gdt_dll0_data:              ; 0x50
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 00000000b, 0x00

gdt_dll1_code:              ; 0x58
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 00000000b, 0x00
gdt_dll1_data:              ; 0x60
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 00000000b, 0x00

gdt_dll2_code:              ; 0x68
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 00000000b, 0x00
gdt_dll2_data:              ; 0x70
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 00000000b, 0x00

gdt_dll3_code:              ; 0x78
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 00000000b, 0x00
gdt_dll3_data:              ; 0x80
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 00000000b, 0x00

gdt_thunk:                  ; 0x88 SEL_THUNK
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 00000000b, 0x00

gdt_vesa:                   ; 0x90 SEL_VESA (32-bit, base=g_lfb_phys, limit=1MB)
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 01001111b, 0x00

gdt_kcb:                    ; 0x98 SEL_KCB (16-bit data, base=g_kcb_phys, limit=255)
    dw 0x00FF, 0x0000
    db 0x00, 10010010b, 00000000b, 0x00

; 15 okien VESA (0xA0..0x110)
gdt_vwin0:  dw 0xFFFF, 0x0000    ; 0xA0
            db 0x00, 10010010b, 00000000b, 0x00
gdt_vwin1:  dw 0xFFFF, 0x0000    ; 0xA8
            db 0x00, 10010010b, 00000000b, 0x00
gdt_vwin2:  dw 0xFFFF, 0x0000    ; 0xB0
            db 0x00, 10010010b, 00000000b, 0x00
gdt_vwin3:  dw 0xFFFF, 0x0000    ; 0xB8
            db 0x00, 10010010b, 00000000b, 0x00
gdt_vwin4:  dw 0xFFFF, 0x0000    ; 0xC0
            db 0x00, 10010010b, 00000000b, 0x00
gdt_vwin5:  dw 0xFFFF, 0x0000    ; 0xC8
            db 0x00, 10010010b, 00000000b, 0x00
gdt_vwin6:  dw 0xFFFF, 0x0000    ; 0xD0
            db 0x00, 10010010b, 00000000b, 0x00
gdt_vwin7:  dw 0xFFFF, 0x0000    ; 0xD8
            db 0x00, 10010010b, 00000000b, 0x00
gdt_vwin8:  dw 0xFFFF, 0x0000    ; 0xE0
            db 0x00, 10010010b, 00000000b, 0x00
gdt_vwin9:  dw 0xFFFF, 0x0000    ; 0xE8
            db 0x00, 10010010b, 00000000b, 0x00
gdt_vwin10: dw 0xFFFF, 0x0000    ; 0xF0
            db 0x00, 10010010b, 00000000b, 0x00
gdt_vwin11: dw 0xFFFF, 0x0000    ; 0xF8
            db 0x00, 10010010b, 00000000b, 0x00
gdt_vwin12: dw 0xFFFF, 0x0000    ; 0x100
            db 0x00, 10010010b, 00000000b, 0x00
gdt_vwin13: dw 0xFFFF, 0x0000    ; 0x108
            db 0x00, 10010010b, 00000000b, 0x00
gdt_vwin14: dw 0x0FFF, 0x0000    ; 0x110
            db 0x00, 10010010b, 00000000b, 0x00

gdt_font:                   ; 0x118 SEL_FONT (16-bit data, limit=4095)
    dw 0x0FFF, 0x0000
    db 0x00, 10010010b, 00000000b, 0x00

gdt_gdt_access:             ; 0x120 SEL_GDT_ACCESS (16-bit data, base=GDT_phys, limit=GDT_LIMIT)
    ; MK_FP(0x120, sel) -> bajt 'sel' w GDT = deskryptor dla selektora 'sel'.
    ; Limit=GDT_LIMIT=0x527 (statyczny); base patchowana w patch_gdt.
    dw GDT_LIMIT, 0x0000
    db 0x00, 10010010b, 00000000b, 0x00

gdt_dyn:                    ; 0x128 - 128 dynamicznych slotow dla GlobalAlloc
    times GDYN_COUNT dq 0   ; null descriptors; kernel.c zapelnia przez SEL_GDT_ACCESS

; GDT konczy sie na 0x128 + 128*8 = 0x528 bajtow -> GDT_LIMIT = 0x527

gdtr:
    dw GDT_LIMIT
    dd 0

idtr:
    dw 64 * 8 - 1
    dd 0
