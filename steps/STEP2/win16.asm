; win16.com - STEP2: Real Mode <-> Protected Mode
;
; Uruchamiany jako program DOS (.COM pod FreeDOS).
; Wchodzi w 32-bit Protected Mode, drukuje na VGA (0xB8000),
; wraca do Real Mode i zamyka sie przez INT 21h.
;
; Kompilacja: nasm -f bin win16.asm -o win16.com

org 0x100
bits 16

; ============ Selektory GDT (index * 8) ============
SEL_CODE32   equ 0x08   ; flat 32-bit kod   (base=0, limit=4GB)
SEL_DATA32   equ 0x10   ; flat 32-bit dane  (base=0, limit=4GB) - dla VGA
SEL_DATASEG  equ 0x18   ; nasz segment COM  (base=cs_phys, limit=64KB)
SEL_CODE16   equ 0x20   ; 16-bit kod        (base=cs_phys, limit=64KB)
SEL_DATA16   equ 0x28   ; 16-bit dane       (base=cs_phys, limit=64KB)

; =====================================================================
;  REAL MODE - start programu DOS
; =====================================================================
start:
    mov  dx, msg_enter
    mov  ah, 0x09
    int  0x21

    ; Oblicz fizyczny adres bazowy segmentu: CS << 4
    xor  eax, eax
    mov  ax, cs
    shl  eax, 4
    mov  [cs_phys], eax

    ; Zapamietaj SS i SP do powrotu z PM
    mov  [orig_ss], ss
    mov  [orig_sp], sp

    ; Wpisz CS do wskaznika far jump powrotu (rm_jmp+2 = segment)
    mov  ax, cs
    mov  [rm_jmp + 2], ax

    ; Popraw bazy deskryptorow zaleznie od polozenia w pamieci
    call patch_descriptors

    ; Wlacz A20 (fast A20 przez port 0x92)
    in   al, 0x92
    or   al, 0x02
    and  al, 0xFE
    out  0x92, al

    cli

    ; Popraw GDTR: fizyczny adres tablicy GDT
    mov  eax, [cs_phys]
    add  eax, gdt
    mov  [gdtr + 2], eax
    lgdt [gdtr]

    ; Ustaw bit PE w CR0 -> wejscie w Protected Mode
    mov  eax, cr0
    or   eax, 0x00000001
    mov  cr0, eax

    ; Far jump: flush pipeline + CS = SEL_CODE32
    ; Offset = fizyczny adres pm32_entry (flat, base=0)
    mov  eax, [cs_phys]
    add  eax, pm32_entry
    mov  [jmp32_off], eax

    db 0x66, 0xEA           ; 0x66 = operand size prefix: far jmp z 32-bit offsetem
jmp32_off: dd 0             ; 32-bit offset (patched powyzej)
           dw SEL_CODE32    ; 16-bit selektor

; -----------------------------------------------------------------------
patch_descriptors:
    ; Wpisuje cs_phys jako baze deskryptorow DATASEG, CODE16, DATA16
    mov  eax, [cs_phys]

    ; DATASEG (0x18)
    mov  [gdt_dataseg + 2], ax
    shr  eax, 16
    mov  [gdt_dataseg + 4], al
    mov  [gdt_dataseg + 7], ah

    mov  eax, [cs_phys]

    ; CODE16 (0x20)
    mov  [gdt_code16 + 2], ax
    shr  eax, 16
    mov  [gdt_code16 + 4], al
    mov  [gdt_code16 + 7], ah

    mov  eax, [cs_phys]

    ; DATA16 (0x28)
    mov  [gdt_data16 + 2], ax
    shr  eax, 16
    mov  [gdt_data16 + 4], al
    mov  [gdt_data16 + 7], ah

    ret

; =====================================================================
;  32-BIT PROTECTED MODE
; =====================================================================
bits 32
pm32_entry:
    mov  ax, SEL_DATASEG    ; DS -> nasz segment (zmienne, stringi)
    mov  ds, ax
    mov  ax, SEL_DATA32     ; ES/FS/GS/SS -> flat (VGA, stos)
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax
    mov  esp, 0x0009FC00

    ; Wyczysc ekran VGA (szary na niebieskim, 0x17)
    mov  edi, 0x000B8000
    mov  ecx, 80 * 25
    mov  ax, 0x1720
    rep  stosw

    ; Wypisz komunikat PM na VGA (linia 11)
    mov  edi, 0x000B8000 + (11 * 160)
    mov  esi, pm_msg        ; DS:pm_msg -> fizyczny adres stringa
    mov  ah, 0x1F           ; bialy (bright) na niebieskim
.print:
    lodsb                   ; al = DS:[ESI++]
    test al, al
    jz   .done
    stosw                   ; ES:[EDI++] = ax (char+atrybut)
    jmp  .print
.done:

    ; Krotka petla opozniajaca (~1s przy 10 MIPS)
    mov  ecx, 0x00F00000
.wait:
    loop .wait

    ; --- Powrot do Real Mode ---
    ; Krok 1: zaladuj 16-bit selektory
    mov  ax, SEL_DATA16
    mov  ds, ax
    mov  es, ax
    mov  ss, ax

    ; Krok 2: far jump do SEL_CODE16:rm_entry
    db 0xEA
    dd rm_entry             ; 32-bit offset (wzgledny, SEL_CODE16 ma base=cs_phys)
    dw SEL_CODE16

; =====================================================================
;  POWROT DO REAL MODE (16-bit, przez SEL_CODE16)
; =====================================================================
bits 16
rm_entry:
    ; Krok 3: wyczysc bit PE
    mov  eax, cr0
    and  eax, ~1
    mov  cr0, eax

    ; Krok 4: indirect far jump - przywroc prawdziwy Real Mode CS
    ; rm_jmp zawiera [offset rm_real][orig_cs] - patched w start:
    jmp  far [rm_jmp]

rm_real:
    ; CS = orig_cs (prawdziwy real mode)
    ; DS nadal ma keszowany SEL_DATA16 (base=cs_phys) - dziala
    mov  ax, cs
    mov  ds, ax
    mov  es, ax

    ; Przywroc stos
    mov  ax, [orig_ss]
    mov  ss, ax
    mov  sp, [orig_sp]

    sti

    mov  dx, msg_back
    mov  ah, 0x09
    int  0x21

    mov  ax, 0x4C00
    int  0x21

; Wskaznik far jump dla powrotu do real mode
; Format: [offset 16-bit][segment 16-bit]
rm_jmp:
    dw rm_real          ; offset (backward ref - stabilny)
    dw 0                ; segment (patched w start:)

; =====================================================================
;  DANE
; =====================================================================
msg_enter  db "Entering Protected Mode...", 0x0D, 0x0A, "$"
msg_back   db "Back in Real Mode! WIN16.COM OK.", 0x0D, 0x0A, "$"

pm_msg     db "  *** Win16 starting - Protected Mode active! ***  ", 0

cs_phys    dd 0
orig_ss    dw 0
orig_sp    dw 0

; =====================================================================
;  GDT - Global Descriptor Table
; =====================================================================
align 8
gdt:

; 0x00 - null (wymagany przez CPU)
    dq 0

; 0x08 - CODE32: flat 32-bit kod (base=0, limit=4GB)
gdt_code32:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10011010b            ; P=1 DPL=0 S=1 type=1010 (exec/read)
    db 11001111b            ; G=1 D/B=1 limit[19:16]=F
    db 0x00

; 0x10 - DATA32: flat 32-bit dane (base=0, limit=4GB) - dostep do VGA
gdt_data32:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b            ; P=1 DPL=0 S=1 type=0010 (read/write)
    db 11001111b
    db 0x00

; 0x18 - DATASEG: nasz segment COM (base=cs_phys, limit=64KB)
gdt_dataseg:
    dw 0xFFFF
    dw 0x0000               ; base[15:0]  <- patched
    db 0x00                 ; base[23:16] <- patched
    db 10010010b
    db 01000000b            ; G=0 D/B=1 (32-bit, byte gran.)
    db 0x00                 ; base[31:24] <- patched

; 0x20 - CODE16: 16-bit kod dla powrotu (base=cs_phys, limit=64KB)
gdt_code16:
    dw 0xFFFF
    dw 0x0000               ; base[15:0]  <- patched
    db 0x00                 ; base[23:16] <- patched
    db 10011010b
    db 00000000b            ; G=0 D/B=0 (16-bit!)
    db 0x00                 ; base[31:24] <- patched

; 0x28 - DATA16: 16-bit dane dla powrotu (base=cs_phys, limit=64KB)
gdt_data16:
    dw 0xFFFF
    dw 0x0000               ; base[15:0]  <- patched
    db 0x00                 ; base[23:16] <- patched
    db 10010010b
    db 00000000b            ; G=0 B=0 (16-bit)
    db 0x00                 ; base[31:24] <- patched

gdt_end:

gdtr:
    dw gdt_end - gdt - 1
    dd 0                    ; base <- patched w start:
