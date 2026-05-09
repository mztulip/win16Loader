; pm_call.asm - PM glue (STEP5)
;
; Rozszerzenie STEP4:
;   - SEL_KERNEL_CODE (0x48): selektor segmentu kodu KERNEL.EXE
;   - patch_gdt patchuje gdt_kernel_code z g_kernel_phys/g_kernel_size
;   - KERNEL funkcje wywolywane przez far call patch (resolwowany przez loader)
;
; Kompilacja: nasm -f obj pm_call.asm -o pm_call.obj

bits 16

SEL_CODE32      equ 0x08
SEL_DATA32      equ 0x10
SEL_DATASEG     equ 0x18
SEL_CODE16      equ 0x20
SEL_DATA16      equ 0x28
SEL_APP_CODE    equ 0x30
SEL_VGA         equ 0x38
SEL_APP_DATA    equ 0x40
SEL_KERNEL_CODE equ 0x48   ; kod KERNEL.EXE <- NOWE

segment _TEXT public class=CODE use16

global pm_call_app_

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
extern _g_kernel_phys       ; NOWE
extern _g_kernel_size       ; NOWE

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

    in   al, 0x92
    or   al, 0x02
    and  al, 0xFE
    out  0x92, al

    cli
    lgdt [cs:gdtr]

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

    ; VGA
    mov  word [cs:gdt_vga + 0], 0x0F9F
    mov  word [cs:gdt_vga + 2], 0x8000
    mov  byte [cs:gdt_vga + 4], 0x0B
    mov  byte [cs:gdt_vga + 7], 0x00

    ; rm_jmp segment
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

    ; SEL_KERNEL_CODE <- g_kernel_phys, limit = kernel_size - 1
    mov  eax, [_g_kernel_phys]
    mov  [cs:gdt_kernel_code + 2], ax
    shr  eax, 16
    mov  [cs:gdt_kernel_code + 4], al
    mov  [cs:gdt_kernel_code + 7], ah
    mov  ax, [_g_kernel_size]
    dec  ax
    mov  [cs:gdt_kernel_code + 0], ax

    ret

; =====================================================================
bits 32
pm32_entry:
    mov  ax, SEL_DATASEG
    mov  ds, ax
    mov  ax, SEL_DATA32
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax
    mov  esp, 0x0009FC00

    ; Wyczysc ekran
    mov  edi, 0x000B8000
    mov  ecx, 80 * 25
    mov  ax, 0x0720
    rep  stosw

    ; Wypisz pm_msg
    mov  edi, 0x000B8000
    mov  esi, pm_msg
    mov  ah, 0x0F
.p: lodsb
    test al, al
    jz   .d
    stosw
    jmp  .p
.d:
    mov  ax, SEL_DATA16
    mov  ss, ax
    mov  esp, 0x0000FFF0

    db 0xEA
    dd pm16_call_app
    dw SEL_CODE16

; =====================================================================
bits 16
pm16_call_app:
    mov  ax, SEL_DATA16
    mov  ds, ax

    mov  ax, SEL_VGA
    mov  es, ax

    mov  ax, [local_entry_ip]
    mov  [call_ptr], ax
    mov  word [call_ptr + 2], SEL_APP_CODE

    cmp  word [local_has_data], 0
    je   .call_app
    mov  ax, SEL_APP_DATA
    mov  ds, ax

.call_app:
    call far [cs:call_ptr]

    ; Przywroc DS po powrocie z apki
    mov  ax, SEL_DATA16
    mov  ds, ax

%ifdef DEBUG
    mov  byte [es:0], 'A'
    mov  byte [es:1], 0x4F
%endif

    mov  eax, cr0
    and  eax, ~1
    mov  cr0, eax

    jmp  far [rm_jmp]

; =====================================================================
rm_real:
    mov  ax, cs
    mov  ds, ax

    mov  ax, [saved_ss]
    mov  ss, ax
    mov  sp, [saved_sp]

    mov  ax, [saved_ds]
    mov  ds, ax

    sti
    retf

; =====================================================================
pm_msg          db "STEP5 PM: calling NE app with imports...", 0
saved_ss        dw 0
saved_sp        dw 0
saved_ds        dw 0
local_entry_ip  dw 0
local_has_data  dw 0
call_ptr        dw 0, 0
rm_jmp          dw rm_real
                dw 0

; =====================================================================
align 8
gdt:
    dq 0                    ; 0x00 null

gdt_code32:                 ; 0x08
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 11001111b, 0x00

gdt_data32:                 ; 0x10
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 11001111b, 0x00

gdt_dataseg:                ; 0x18
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 01000000b, 0x00

gdt_code16:                 ; 0x20
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 00000000b, 0x00

gdt_data16:                 ; 0x28
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 00000000b, 0x00

gdt_app_code:               ; 0x30
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 00000000b, 0x00

gdt_vga:                    ; 0x38
    dw 0x0000, 0x0000
    db 0x00, 10010010b, 00000000b, 0x00

gdt_app_data:               ; 0x40
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 00000000b, 0x00

gdt_kernel_code:            ; 0x48 <- NOWE
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 00000000b, 0x00

gdt_end:

gdtr:
    dw gdt_end - gdt - 1
    dd 0
