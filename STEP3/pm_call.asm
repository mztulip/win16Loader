; pm_call.asm - PM glue dla loader.c (STEP3)
;
; Kluczowe zalozenia dotyczace segmentow (Watcom large model EXE):
;   - Przy wejsciu do pm_call_app_: CS = segment kodu (_TEXT), DS = DGROUP (dane C)
;   - Zewnetrzne symbole C (_g_*) sa w DGROUP -> dostep przez DS bez prefixu
;   - Dane lokalne (gdt, gdtr, pm_msg itd.) sa w _TEXT -> dostep przez [cs:label]
;   - W realmode mozna pisac do _TEXT przez [cs:label] (brak ochrony pamieci)
;   - W PM: DS=SEL_DATASEG (base=cs_phys) -> dostep do _TEXT bez prefixu ✓
;
; Kompilacja: nasm -f obj pm_call.asm -o pm_call.obj

bits 16

SEL_CODE32   equ 0x08
SEL_DATA32   equ 0x10
SEL_DATASEG  equ 0x18   ; _TEXT segment (base=cs_phys, 64KB)
SEL_CODE16   equ 0x20   ; 16-bit kod powrotu (base=cs_phys)
SEL_DATA16   equ 0x28   ; 16-bit dane powrotu (base=cs_phys)
SEL_APP_CODE equ 0x30   ; 16-bit kod NE app  (base=app_phys)
SEL_VGA      equ 0x38   ; okno VGA           (base=0xB8000)

segment _TEXT public class=CODE use16

global pm_call_app_

extern _g_app_phys          ; unsigned long  (4B, w DGROUP)
extern _g_code_size         ; unsigned short (2B, w DGROUP)
extern _g_entry_ip          ; unsigned short (2B, w DGROUP)
extern _g_cs_phys           ; unsigned long  (4B, w DGROUP)
extern _g_orig_cs           ; unsigned short (2B, w DGROUP)
extern _g_orig_ss           ; unsigned short (2B, w DGROUP)
extern _g_orig_sp           ; unsigned short (2B, w DGROUP)

; =====================================================================
pm_call_app_:
    ; DS = DGROUP (ustawione przez Watcom przed wywolaniem)

    ; --- Zlap SS:SP NA WEJSCIU (przed jakimikolwiek push/pop) ---
    ; g_orig_sp z C jest chwycone przed wywolaniem far call (SP sie zmienia),
    ; wiec lapmy tu wewnatrz zeby miec dokladna wartosc.
    mov  ax, ss
    mov  [cs:saved_ss], ax
    mov  ax, sp
    mov  [cs:saved_sp], ax

    ; --- Skopiuj pozostale dane C do lokalnych zmiennych w _TEXT ---
    mov  [cs:saved_ds], ds          ; zapamietaj segment DGROUP do przywrocenia
    mov  ax, [_g_entry_ip]
    mov  [cs:local_entry_ip], ax

    ; --- Popraw GDT (dane GDT w _TEXT, pisz przez [cs:]) ---
    call patch_gdt

    ; --- Oblicz adres pm32_entry i wpisz do jmp32_off PRZED PE=1 ---
    ; (po PE=1 pisanie do segmentu kodu przez CS jest zabronione)
    mov  eax, [_g_cs_phys]          ; DGROUP -> DS ✓
    add  eax, pm32_entry            ; + offset pm32_entry w _TEXT
    mov  [cs:jmp32_off], eax        ; wpisz do instrukcji far jump ✓

    ; --- Ustaw GDTR: baza = cs_phys + offset(gdt w _TEXT) ---
    mov  eax, [_g_cs_phys]
    add  eax, gdt                   ; gdt jest w _TEXT, cs_phys = baza _TEXT
    mov  [cs:gdtr + 2], eax         ; wpisz baze do gdtr ✓

    ; --- A20 ---
    in   al, 0x92
    or   al, 0x02
    and  al, 0xFE
    out  0x92, al

    cli

    lgdt [cs:gdtr]                  ; zaladuj GDTR z _TEXT przez CS: ✓

    ; --- CR0 PE=1 ---
    mov  eax, cr0
    or   eax, 0x00000001
    mov  cr0, eax

    ; --- Far jump do flat 32-bit PM ---
    ; jmp32_off zostal juz wypeleniony powyzej (przed PE=1)
    db 0x66, 0xEA                   ; operand-size prefix + far jmp opcode
jmp32_off: dd 0                     ; 32-bit offset (patched)
           dw SEL_CODE32

; =====================================================================
patch_gdt:
    ; DATASEG, CODE16, DATA16 <- cs_phys (baza = fizyczny adres _TEXT)
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

    ; APP_CODE <- app_phys, limit = code_size - 1
    mov  eax, [_g_app_phys]
    mov  [cs:gdt_app_code + 2], ax
    shr  eax, 16
    mov  [cs:gdt_app_code + 4], al
    mov  [cs:gdt_app_code + 7], ah
    mov  ax, [_g_code_size]
    dec  ax
    mov  [cs:gdt_app_code + 0], ax

    ; VGA: base=0xB8000, limit=0xF9F (4000 bajtow - 1)
    mov  word [cs:gdt_vga + 0], 0x0F9F
    mov  word [cs:gdt_vga + 2], 0x8000
    mov  byte [cs:gdt_vga + 4], 0x0B
    mov  byte [cs:gdt_vga + 7], 0x00

    ; rm_jmp segment <- orig_cs (dla powrotu do real mode)
    mov  ax, [_g_orig_cs]
    mov  [cs:rm_jmp + 2], ax

    ret

; =====================================================================
;  32-BIT PM
; =====================================================================
bits 32
pm32_entry:
    ; DS = SEL_DATASEG (base=cs_phys) -> dostep do danych w _TEXT ✓
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

    ; Wypisz pm_msg (w _TEXT, DS=SEL_DATASEG base=cs_phys) ✓
    mov  edi, 0x000B8000
    mov  esi, pm_msg
    mov  ah, 0x0F
.p: lodsb
    test al, al
    jz   .d
    stosw
    jmp  .p
.d:
    ; Przelacz SS na 16-bit przed 16-bit far call
    mov  ax, SEL_DATA16
    mov  ss, ax
    mov  esp, 0x0000FFF0

    ; Far jump do 16-bit fazy
    db 0xEA
    dd pm16_call_app
    dw SEL_CODE16

; =====================================================================
;  16-BIT PM: wywoluje aplikacje NE
; =====================================================================
bits 16
pm16_call_app:
    ; DS = SEL_DATA16 (base=cs_phys) -> dostep do danych w _TEXT ✓
    mov  ax, SEL_VGA
    mov  es, ax

    ; Przygotuj far pointer do aplikacji NE
    ; local_entry_ip i call_ptr sa w _TEXT, dostepne przez DS=SEL_DATA16 ✓
    mov  ax, [local_entry_ip]
    mov  [call_ptr], ax
    mov  word [call_ptr + 2], SEL_APP_CODE

    call far [call_ptr]         ; 16-bit far call do aplikacji NE

    ; Aplikacja wrocila przez retf.
    ; UWAGA: aplikacja mogla zmienic DS (np. push cs / pop ds).
    ; Przywroc DS = SEL_DATA16 (base=cs_phys) przed dostepem do rm_jmp.
    mov  ax, SEL_DATA16
    mov  ds, ax

%ifdef DEBUG
    ; --- DEBUG marker A: ne_app zwrocilo (ES=SEL_VGA base=0xB8000) ---
    mov  byte [es:0], 'A'
    mov  byte [es:1], 0x4F      ; bialy na czerwonym
%endif

    ; --- Powrot do Real Mode ---
    mov  eax, cr0
    and  eax, ~1
    mov  cr0, eax

%ifdef DEBUG
    ; --- DEBUG marker B: PE wyczyszczone ---
    mov  byte [es:2], 'B'
    mov  byte [es:3], 0x4F
%endif

    jmp  far [rm_jmp]           ; rm_jmp w _TEXT, DS=SEL_DATA16 base=cs_phys ✓

; =====================================================================
;  REAL MODE - koniec, powrot do C
; =====================================================================
rm_real:
    ; CS = orig_cs, DS ma zakeszowany deskryptor SEL_DATA16 (base=cs_phys)
    ; Ustaw DS = CS zeby moc czytac lokalne zmienne z _TEXT
    mov  ax, cs
    mov  ds, ax                 ; DS = _TEXT segment (base=cs_phys)

%ifdef DEBUG
    ; --- DEBUG marker C: dotarlismy do rm_real (real mode VGA) ---
    push es
    push ax
    mov  ax, 0xB800
    mov  es, ax
    mov  byte [es:4], 'C'
    mov  byte [es:5], 0x4F      ; bialy na czerwonym
    pop  ax
    pop  es
%endif

    ; Przywroc SS:SP z lokalnych kopii (w _TEXT, dostepne przez DS=CS) ✓
    mov  ax, [saved_ss]
    mov  ss, ax
    mov  sp, [saved_sp]

%ifdef DEBUG
    ; --- DEBUG marker D: SS:SP przywrocone ---
    push es
    push ax
    mov  ax, 0xB800
    mov  es, ax
    mov  byte [es:6], 'D'
    mov  byte [es:7], 0x4F
    pop  ax
    pop  es
%endif

    ; Przywroc DS = DGROUP (Watcom tego oczekuje po powrocie z funkcji)
    mov  ax, [saved_ds]
    mov  ds, ax

    sti                         ; wlacz przerwania DOPIERO po przywroceniu stosu

    retf                        ; powrot do Watcom C (large model: far return)

; =====================================================================
;  DANE LOKALNE (w _TEXT, dostepne przez CS: w realmode, przez SEL_DATA* w PM)
; =====================================================================
pm_msg          db "PM Loader: calling 16-bit NE app (Win3.1 style)...", 0
saved_ss        dw 0
saved_sp        dw 0
saved_ds        dw 0
local_entry_ip  dw 0
call_ptr        dw 0, 0     ; [ip][cs] dla call far [call_ptr]
rm_jmp          dw rm_real  ; [ip] dla jmp far [rm_jmp]
                dw 0        ; [cs] <- patched w patch_gdt

; =====================================================================
;  GDT (w _TEXT, patched w realmode przez [cs:label])
; =====================================================================
align 8
gdt:
    dq 0                    ; 0x00 null

gdt_code32:                 ; 0x08 flat 32-bit kod (base=0, limit=4GB)
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 11001111b, 0x00

gdt_data32:                 ; 0x10 flat 32-bit dane (base=0, limit=4GB)
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 11001111b, 0x00

gdt_dataseg:                ; 0x18 _TEXT segment (base=cs_phys, 64KB)
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

gdt_end:

gdtr:
    dw gdt_end - gdt - 1    ; limit = 63
    dd 0                    ; base <- patched w pm_call_app_
