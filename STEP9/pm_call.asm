; pm_call.asm - PM glue (STEP9b)
;
; Rozszerzenia vs STEP9a:
;   - extern _g_font_phys: adres fizyczny tablicy fontow 8x16 (z loader.c)
;   - draw_char_32: rysuje znak 8x16 na LFB (bits 32)
;   - draw_str_32:  rysuje ciag znakow (bits 32)
;   - pm32_entry: bialy pasek + granatowy pasek + tekst diagnostyczny
;
; Selektory GDT:
;   SEL_CODE32=0x08, SEL_DATA32=0x10, SEL_DATASEG=0x18
;   SEL_CODE16=0x20, SEL_DATA16=0x28
;   SEL_APP_CODE=0x30, SEL_VGA=0x38, SEL_APP_DATA=0x40
;   0x48..0x80: DLL code/data (4 DLL max)
;   SEL_THUNK=0x88, SEL_VESA=0x90
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
SEL_THUNK       equ 0x88
SEL_VESA        equ 0x90
SEL_FONT        equ 0x98

INT3F_MAX_DEPTH equ 8

; Parametry ekranu (640x480x24bpp)
VESA_COLS       equ 640
VESA_ROWS       equ 480
VESA_BPP        equ 3          ; bajty na piksel (24bpp)
VESA_PITCH      equ VESA_COLS * VESA_BPP   ; = 1920
VESA_FB_SIZE    equ VESA_COLS * VESA_ROWS * VESA_BPP   ; = 921600

; Czcionka: 8x16 (8 pikseli szerokosci, 16 wierszy na znak)
FONT_W          equ 8
FONT_H          equ 16

segment _TEXT public class=CODE use16

global pm_call_app_
global get_int3f_off_

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

extern _g_lfb_phys          ; unsigned long - adres LFB VESA
extern _g_font_phys         ; unsigned long - adres tablicy fontow 8x16

; =====================================================================
get_int3f_off_:
    mov  ax, int3f_handler
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

    ; SEL_VGA (patchuj base dla 0xB8000)
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

    ; SEL_THUNK
    mov  eax, [_g_thunk_phys]
    mov  [cs:gdt_thunk + 2], ax
    shr  eax, 16
    mov  [cs:gdt_thunk + 4], al
    mov  [cs:gdt_thunk + 7], ah
    mov  ax, [_g_thunk_size]
    dec  ax
    mov  [cs:gdt_thunk + 0], ax

    ; SEL_VESA: base = g_lfb_phys, limit 1MB (0xFFFFF)
    mov  eax, [_g_lfb_phys]
    mov  [cs:gdt_vesa + 2], ax
    shr  eax, 16
    mov  [cs:gdt_vesa + 4], al
    mov  [cs:gdt_vesa + 7], ah

    ; SEL_FONT: base = g_font_phys, limit 4095 (256 znakow * 16 B)
    mov  eax, [_g_font_phys]
    mov  [cs:gdt_font + 2], ax
    shr  eax, 16
    mov  [cs:gdt_font + 4], al
    mov  [cs:gdt_font + 7], ah

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
; 32-bit protected mode entry
; DS = SEL_DATASEG  (loader code/data, dostep do globalnych)
; FS = SEL_DATA32   (flat 4GB, odczyt fontu z dowoln. adresu fiz.)
; ES = SEL_VESA     (LFB - zapis pikseli)
; SS = SEL_DATA32   (stos na 0x9FC00)
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

    ; --- Wypelnij framebuffer (jesli VESA dostepna) ---
    mov  eax, [_g_lfb_phys]
    test eax, eax
    jz   .skip_draw

    mov  ax, SEL_VESA
    mov  es, ax                     ; ES:0 = start LFB

    ; 1. Caly ekran: czarny (0x000000)
    xor  edi, edi
    xor  eax, eax
    mov  ecx, VESA_FB_SIZE / 4
    rep  stosd

    ; 2. Bialy pasek: wiersze 0..19 (top)
    ;    24bpp: fill 0xFFFFFFFF (nadpisuje 4 bajty naraz, 3 z nich to RGB piksele)
    ;    20 wierszy * 640 pikseli * 3 B = 38400 B = 9600 dwordow
    xor  edi, edi
    mov  eax, 0xFFFFFFFF
    mov  ecx, (VESA_PITCH * 20) / 4
    rep  stosd

    ; 3. Granatowy pasek: wiersze 20..39
    ;    BGR: B=0x66 G=0x22 R=0x00 -> wzor dword 0x66220066 (4B: 66 22 00 66)
    ;    (nieidealne wyrownanie 24bpp do 32-bit, ale wizualnie wystarczajace)
    mov  edi, VESA_PITCH * 20
    mov  eax, 0x00226600
    mov  ecx, (VESA_PITCH * 20) / 4
    rep  stosd

    ; --- Rysuj tekst (jesli font dostepny) ---
    mov  eax, [_g_font_phys]
    test eax, eax
    jz   .skip_draw

    ; Linia 1: czarny tekst na bialym pasku (y=2, x=10)
    lea  esi, [str_title]           ; "STEP9b: VESA font OK!"
    mov  ebx, 10                    ; x = 10 px
    mov  ecx, 2                     ; y = 2 px (na bialym pasku)
    mov  edx, 0x00000000            ; kolor: czarny (BGR: B=0 G=0 R=0)
    call draw_str_32

    ; Linia 2: bialy tekst na granatowym pasku (y=22, x=10)
    lea  esi, [str_mode]            ; "640x480 24bpp"
    mov  ebx, 10
    mov  ecx, 22                    ; y = 22 px (na granatowym pasku)
    mov  edx, 0x00FFFFFF            ; kolor: bialy
    call draw_str_32

    ; Linia 3: bialy tekst na czarnym tle (y=50, x=10)
    lea  esi, [str_font]            ; "8x16 BIOS font"
    mov  ebx, 10
    mov  ecx, 50
    mov  edx, 0x00FFFFFF
    call draw_str_32

.skip_draw:
    ; Przelacz na 16-bit
    mov  ax, SEL_DATA16
    mov  ss, ax
    mov  esp, 0x0000FFF0

    db 0xEA
    dd pm16_call_app
    dw SEL_CODE16

; =====================================================================
; draw_char_32 - rysuje jeden znak 8x16 na LFB
;
; Wejscie (registrowe):
;   AL  = kod ASCII znaku
;   EBX = x (piksel, lewy rog znaku)
;   ECX = y (piksel, gorny rog znaku)
;   EDX = kolor fg (0x00RRGGBB: R=bits[23:16] G=bits[15:8] B=bits[7:0])
;
; Uzywa:
;   DS = SEL_DATASEG  (dostep do _g_font_phys)
;   ES = SEL_VESA     (zapis do LFB)
;   FS = SEL_DATA32   (odczyt fontu z fizycznego adresu g_font_phys)
;
; Zachowuje: ESI, EDI, EBX, ECX, EDX, EBP
; Niszczy: EAX
; =====================================================================
bits 32
draw_char_32:
    push esi
    push edi
    push ebx
    push ecx
    push edx
    push ebp

    ; ESI = adres fontu dla znaku: g_font_phys + (char & 0xFF) * 16
    and  eax, 0xFF
    shl  eax, 4                     ; char * 16 (16 bajtow na znak)
    add  eax, [_g_font_phys]        ; + baza fizyczna (dostep przez FS=flat)
    mov  esi, eax

    ; EDI = offset poczatku znaku w LFB: y * VESA_PITCH + x * 3
    imul ecx, VESA_PITCH            ; ecx = y * 1920
    lea  eax, [ebx + ebx*2]        ; eax = x * 3
    add  ecx, eax
    mov  edi, ecx                   ; edi = poczatkowy offset w SEL_VESA

    ; EBP = skladowa R koloru (bits[23:16])
    mov  ebp, edx
    shr  ebp, 16
    and  ebp, 0xFF

    ; Petla po wierszach (0..15)
    xor  ecx, ecx                   ; ecx = row (0..15)

.dc_row:
    cmp  ecx, FONT_H
    jge  .dc_done

    ; Bajt fontu dla wiersza 'ecx' (FS=flat -> odczyt z adresu fizycznego)
    movzx eax, byte [fs:esi + ecx]  ; EAX[7:0] = wzor 8 pikseli

    push ecx                        ; zachowaj numer wiersza
    push edi                        ; zachowaj offset poczatku wiersza

    ; Petla po kolumnach: bit 7 = lewy piksel, bit 0 = prawy
    mov  ecx, 7                     ; bit index: 7..0

.dc_col:
    bt   eax, ecx                   ; testuj bit 'ecx' w EAX
    jnc  .dc_bg                     ; bit = 0 -> tlo (pomijamy)

    ; Rysuj piksel fg: zapis B, G, R do [ES:EDI]
    mov  [es:edi], dl               ; B = kolor & 0xFF
    mov  [es:edi+1], dh             ; G = (kolor >> 8) & 0xFF
    push eax
    mov  eax, ebp                   ; EBP = R = (kolor >> 16) & 0xFF
    mov  [es:edi+2], al
    pop  eax

.dc_bg:
    add  edi, VESA_BPP              ; nastepny piksel (3 bajty)
    dec  ecx
    jns  .dc_col                    ; powtarzaj dopoki ecx >= 0

    pop  edi                        ; przywroc offset poczatku wiersza
    pop  ecx                        ; przywroc numer wiersza
    add  edi, VESA_PITCH            ; przejdz do nastepnego wiersza
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

; =====================================================================
; draw_str_32 - rysuje null-terminated string
;
; Wejscie:
;   ESI = wskaznik do stringa (DS = SEL_DATASEG)
;   EBX = x startowe (piksel)
;   ECX = y (piksel)
;   EDX = kolor fg
;
; Zachowuje: ESI, EBX, ECX, EDX
; =====================================================================
bits 32
draw_str_32:
    push esi
    push ebx
    push ecx
    push edx

.ds_loop:
    movzx eax, byte [esi]           ; AL = kolejny znak
    test  al, al
    jz    .ds_done

    call  draw_char_32              ; AL=char, EBX=x, ECX=y, EDX=color
    add   ebx, FONT_W               ; przesuniecie x o szerokosc znaku (8px)
    inc   esi
    jmp   .ds_loop

.ds_done:
    pop  edx
    pop  ecx
    pop  ebx
    pop  esi
    ret

; =====================================================================
; Stringi diagnostyczne (w segmencie kodu, dostepne przez DS=DATASEG)
; =====================================================================
str_title   db "STEP9c: VESA + GDI.EXE", 0
str_mode    db "640x480  24bpp  BGR", 0
str_font    db "8x16 BIOS font  SEL_FONT=0x98  SEL_VESA=0x90", 0

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

    push ax
    push dx
.dbg_app:
    mov  dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .dbg_app
    mov  dx, 0x3F8
    mov  al, 0x41           ; 'A'
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
    mov  al, 0x52           ; 'R'
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
    mov  al, 0x46           ; 'F'
    out  dx, al
    pop  dx
    pop  ax
    sti
    retf

; =====================================================================
; INT 3F handler (16-bit PM)
; =====================================================================
int3f_handler:
    push bp
    mov  bp, sp
    push ax
    push bx
    push cx
    push si
    push es

    mov  si, [bp+2]
    mov  bx, [bp+4]
    mov  es, bx
    mov  bl, byte [es:si]
    xor  bh, bh
    mov  si, [es:si + 1]

    mov  ax, bx
    shl  ax, 4
    add  ax, 0x48
    mov  cx, bx
    shl  cx, 4
    add  cx, 0x50

    push ax
    push cx

    mov  ax, [ss:int3f_depth]
    mov  bx, ax
    shl  bx, 1
    mov  cx, bx
    shl  cx, 1
    add  bx, cx

    mov  [ss:int3f_stack + bx + 0], ds
    mov  cx, [bp+8]
    mov  [ss:int3f_stack + bx + 2], cx
    mov  cx, [bp+10]
    mov  [ss:int3f_stack + bx + 4], cx

    inc  word [ss:int3f_depth]

    pop  cx
    pop  ax

    mov  [bp+2], si
    mov  [bp+4], ax

    mov  word [bp+8],  int3f_trampoline
    mov  word [bp+10], SEL_CODE16

    mov  ds, cx

    pop  es
    pop  si
    pop  cx
    pop  bx
    pop  ax
    pop  bp
    iret

; =====================================================================
; INT 3F trampoline
; =====================================================================
int3f_trampoline:
    push ax
    push bx
    push cx

    dec  word [ss:int3f_depth]
    mov  ax, [ss:int3f_depth]

    mov  bx, ax
    shl  bx, 1
    mov  cx, bx
    shl  cx, 1
    add  bx, cx

    mov  cx, [ss:int3f_stack + bx + 0]
    mov  ax, [ss:int3f_stack + bx + 2]
    mov  [ss:tramp_ret_ip], ax
    mov  ax, [ss:int3f_stack + bx + 4]
    mov  [ss:tramp_ret_cs], ax
    mov  [ss:tramp_saved_ds], cx

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
    mov  al, 0x54           ; 'T'
    out  dx, al
    pop  dx
    pop  ax

    mov  cx, [ss:tramp_saved_ds]
    mov  ds, cx

    db   0x2E, 0xFF, 0x2E
    dw   tramp_ret_ip

tramp_ret_ip:   dw 0
tramp_ret_cs:   dw 0
tramp_saved_ds: dw 0

; =====================================================================
; Dane
; =====================================================================
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

gdt_vga:                    ; 0x38 - VGA text buffer (patchowany)
    dw 0x0000, 0x0000
    db 0x00, 10010010b, 00000000b, 0x00

gdt_app_data:               ; 0x40 - 16-bit data, base=g_app_data_phys
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 00000000b, 0x00

; DLL sloty (4 DLL max)
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

gdt_thunk:                  ; 0x88 SEL_THUNK (16-bit code, base=g_thunk_phys)
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 00000000b, 0x00

gdt_vesa:                   ; 0x90 SEL_VESA (32-bit data, base=g_lfb_phys, limit=1MB)
    dw 0xFFFF, 0x0000       ; limit[15:0] = 0xFFFF
    db 0x00                 ; base[23:16] (patchowane w patch_gdt)
    db 10010010b            ; P=1 DPL=0 S=1 E=0 W=1 (data r/w)
    db 01001111b            ; G=0 D/B=1 L=0 AVL=0 limit[19:16]=F => limit=0xFFFFF=1MB
    db 0x00                 ; base[31:24] (patchowane w patch_gdt)

gdt_font:                   ; 0x98 SEL_FONT (16-bit data, base=g_font_phys, limit=4095)
    dw 0x0FFF, 0x0000       ; limit[15:0] = 0x0FFF (256 znakow * 16 B)
    db 0x00                 ; base[23:16] (patchowane w patch_gdt)
    db 10010010b            ; P=1 DPL=0 S=1 E=0 W=1 (data r/w)
    db 00000000b            ; G=0 D/B=0 (16-bit) limit[19:16]=0
    db 0x00                 ; base[31:24] (patchowane w patch_gdt)

gdt_end:

gdtr:
    dw gdt_end - gdt - 1
    dd 0

idtr:
    dw 64 * 8 - 1
    dd 0
