; pm_call.asm - PM glue dla loader.c (STEP4)
;
; Rozszerzenie STEP3:
;   - Nowy selektor SEL_APP_DATA (0x40) dla segmentu danych apki (DGROUP)
;   - patch_gdt ustawia gdt_app_data na podstawie g_app_data_phys
;   - pm16_call_app: jesli g_has_data, przed call far ustawia DS=SEL_APP_DATA
;   - call far [cs:call_ptr] - CS: override zapewnia dostep do call_ptr
;     nawet gdy DS=SEL_APP_DATA (inny segment niz _TEXT loadera)
;
; Kompilacja: nasm -f obj pm_call.asm -o pm_call.obj

bits 16

SEL_CODE32   equ 0x08
SEL_DATA32   equ 0x10
SEL_DATASEG  equ 0x18   ; _TEXT loadera (base=cs_phys, 64KB)
SEL_CODE16   equ 0x20   ; 16-bit kod powrotu (base=cs_phys)
SEL_DATA16   equ 0x28   ; 16-bit dane powrotu (base=cs_phys)
SEL_APP_CODE equ 0x30   ; 16-bit kod NE app  (base=app_phys)
SEL_VGA      equ 0x38   ; okno VGA           (base=0xB8000)
SEL_APP_DATA equ 0x40   ; 16-bit dane NE app (base=app_data_phys) <- NOWE

segment _TEXT public class=CODE use16

global pm_call_app_

extern _g_app_phys
extern _g_code_size
extern _g_entry_ip
extern _g_cs_phys
extern _g_orig_cs
extern _g_orig_ss
extern _g_orig_sp

; Nowe w STEP4
extern _g_app_data_phys
extern _g_data_size
extern _g_has_data

; =====================================================================
pm_call_app_:
    ; DS = DGROUP (ustawione przez Watcom przed wywolaniem)

    ; Zlap SS:SP przed jakimikolwiek push/pop
    mov  ax, ss
    mov  [cs:saved_ss], ax
    mov  ax, sp
    mov  [cs:saved_sp], ax

    ; Skopiuj dane C do lokalnych zmiennych w _TEXT
    mov  [cs:saved_ds], ds
    mov  ax, [_g_entry_ip]
    mov  [cs:local_entry_ip], ax

    ; Popraw GDT
    call patch_gdt

    ; Oblicz adres pm32_entry i wpisz do jmp32_off
    mov  eax, [_g_cs_phys]
    add  eax, pm32_entry
    mov  [cs:jmp32_off], eax

    ; Ustaw baze GDTR
    mov  eax, [_g_cs_phys]
    add  eax, gdt
    mov  [cs:gdtr + 2], eax

    ; A20
    in   al, 0x92
    or   al, 0x02
    and  al, 0xFE
    out  0x92, al

    cli

    lgdt [cs:gdtr]

    ; CR0 PE=1
    mov  eax, cr0
    or   eax, 0x00000001
    mov  cr0, eax

    ; Far jump do flat 32-bit PM
    db 0x66, 0xEA
jmp32_off: dd 0
           dw SEL_CODE32

; =====================================================================
patch_gdt:
    ; Patche SEL_DATASEG, SEL_CODE16, SEL_DATA16 <- cs_phys
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

    ; SEL_APP_CODE <- app_phys, limit = code_size - 1
    mov  eax, [_g_app_phys]
    mov  [cs:gdt_app_code + 2], ax
    shr  eax, 16
    mov  [cs:gdt_app_code + 4], al
    mov  [cs:gdt_app_code + 7], ah
    mov  ax, [_g_code_size]
    dec  ax
    mov  [cs:gdt_app_code + 0], ax

    ; VGA: base=0xB8000, limit=0xF9F
    mov  word [cs:gdt_vga + 0], 0x0F9F
    mov  word [cs:gdt_vga + 2], 0x8000
    mov  byte [cs:gdt_vga + 4], 0x0B
    mov  byte [cs:gdt_vga + 7], 0x00

    ; rm_jmp segment <- orig_cs
    mov  ax, [_g_orig_cs]
    mov  [cs:rm_jmp + 2], ax

    ; SEL_APP_DATA <- app_data_phys (tylko jesli g_has_data != 0)
    mov  ax, [_g_has_data]
    mov  [cs:local_has_data], ax
    test ax, ax
    jz   .no_data

    mov  eax, [_g_app_data_phys]
    mov  [cs:gdt_app_data + 2], ax
    shr  eax, 16
    mov  [cs:gdt_app_data + 4], al
    mov  [cs:gdt_app_data + 7], ah
    ; limit = 0xFFFF (pelne 64KB - obejmuje kod + BSS + stos w DGROUP)
    mov  word [cs:gdt_app_data + 0], 0xFFFF

.no_data:
    ret

; =====================================================================
;  32-BIT PM
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
    ; Przelacz na 16-bit
    mov  ax, SEL_DATA16
    mov  ss, ax
    mov  esp, 0x0000FFF0

    db 0xEA
    dd pm16_call_app
    dw SEL_CODE16

; =====================================================================
;  16-BIT PM: wywoluje aplikacje NE
; =====================================================================
bits 16
pm16_call_app:
    ; Upewnij sie ze DS = SEL_DATA16 (dostep do zmiennych _TEXT loadera)
    mov  ax, SEL_DATA16
    mov  ds, ax

    mov  ax, SEL_VGA
    mov  es, ax

    ; Przygotuj far pointer do aplikacji (call_ptr jest w _TEXT)
    mov  ax, [local_entry_ip]
    mov  [call_ptr], ax
    mov  word [call_ptr + 2], SEL_APP_CODE

    ; Przelacz DS na segment danych apki (jesli zaladowany)
    ; Po tej operacji [zmienne _TEXT] sa niedostepne przez DS - uzywamy CS:
    cmp  word [local_has_data], 0
    je   .call_app
    mov  ax, SEL_APP_DATA
    mov  ds, ax

.call_app:
    ; CS: override - czytaj call_ptr z _TEXT niezaleznie od DS
    call far [cs:call_ptr]

    ; Apka wrocila. Przywroc DS=SEL_DATA16 (apka mogla zmienic DS).
    mov  ax, SEL_DATA16
    mov  ds, ax

%ifdef DEBUG
    mov  byte [es:0], 'A'
    mov  byte [es:1], 0x4F
%endif

    ; Powrot do Real Mode
    mov  eax, cr0
    and  eax, ~1
    mov  cr0, eax

%ifdef DEBUG
    mov  byte [es:2], 'B'
    mov  byte [es:3], 0x4F
%endif

    jmp  far [rm_jmp]

; =====================================================================
;  REAL MODE - powrot do C
; =====================================================================
rm_real:
    mov  ax, cs
    mov  ds, ax

%ifdef DEBUG
    push es
    push ax
    mov  ax, 0xB800
    mov  es, ax
    mov  byte [es:4], 'C'
    mov  byte [es:5], 0x4F
    pop  ax
    pop  es
%endif

    mov  ax, [saved_ss]
    mov  ss, ax
    mov  sp, [saved_sp]

%ifdef DEBUG
    push es
    push ax
    mov  ax, 0xB800
    mov  es, ax
    mov  byte [es:6], 'D'
    mov  byte [es:7], 0x4F
    pop  ax
    pop  es
%endif

    mov  ax, [saved_ds]
    mov  ds, ax

    sti

    retf

; =====================================================================
;  DANE LOKALNE (w _TEXT)
; =====================================================================
pm_msg          db "STEP4 PM: calling 16-bit NE app...", 0
saved_ss        dw 0
saved_sp        dw 0
saved_ds        dw 0
local_entry_ip  dw 0
local_has_data  dw 0        ; kopia g_has_data (zapisana w realmode)
call_ptr        dw 0, 0
rm_jmp          dw rm_real
                dw 0        ; cs <- patched

; =====================================================================
;  GDT
; =====================================================================
align 8
gdt:
    dq 0                    ; 0x00 null

gdt_code32:                 ; 0x08 flat 32-bit kod
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 11001111b, 0x00

gdt_data32:                 ; 0x10 flat 32-bit dane
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 11001111b, 0x00

gdt_dataseg:                ; 0x18 _TEXT loadera (base=cs_phys, 64KB)
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 01000000b, 0x00

gdt_code16:                 ; 0x20 16-bit kod powrotu (base=cs_phys)
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 00000000b, 0x00

gdt_data16:                 ; 0x28 16-bit dane powrotu (base=cs_phys)
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 00000000b, 0x00

gdt_app_code:               ; 0x30 16-bit kod NE app (base/limit patched)
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 00000000b, 0x00

gdt_vga:                    ; 0x38 VGA (base=0xB8000, limit=4000)
    dw 0x0000, 0x0000
    db 0x00, 10010010b, 00000000b, 0x00

gdt_app_data:               ; 0x40 16-bit dane NE app (base/limit patched) <- NOWE
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 00000000b, 0x00

gdt_end:

gdtr:
    dw gdt_end - gdt - 1
    dd 0                    ; base <- patched
