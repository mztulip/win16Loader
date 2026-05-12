; pm_call.asm - PM glue (STEP9a)
;
; Rozszerzenie STEP8:
;   - SEL_VESA (0x90): 32-bit descriptor z base=g_lfb_phys (VESA LFB)
;   - pm32_entry: wypelnia framebuffer zamiast VGA text
;   - patch_gdt: patchuje gdt_vesa base z _g_lfb_phys
;
; GDT selektory DLL:
;   DLL i (0-based): code = 0x48 + i*0x10, data = 0x50 + i*0x10
;   SEL_THUNK = 0x88 (16-bit code, base=g_thunk_phys)
;   SEL_VESA  = 0x90 (32-bit data, base=g_lfb_phys, limit=1MB)
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
; 0x48..0x80: DLL code/data (dynamiczne, 4 DLL max)
SEL_THUNK       equ 0x88   ; segment thunkow INT 3F
SEL_VESA        equ 0x90   ; VESA LFB (32-bit data, base=g_lfb_phys)

INT3F_MAX_DEPTH equ 8      ; maks. zagniezdzen wywolan DLL

; rozmiar framebuffera: 640*480*3 = 921600 = 0xE1000 bajtow
; zaokraglamy do 1MB (0xFFFFF) - miesci sie w granicy 0xFFFFF (G=0, D/B=1)
VESA_FB_ROWS    equ 480
VESA_FB_COLS    equ 640
VESA_FB_BPP     equ 3       ; bajty na piksel (24bpp RGB)
VESA_FB_SIZE    equ VESA_FB_ROWS * VESA_FB_COLS * VESA_FB_BPP  ; 921600

segment _TEXT public class=CODE use16

global pm_call_app_
global get_int3f_off_         ; unsigned short get_int3f_off(void) - zwraca offset handlera

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

; Tablice dla DLL (z loader.c)
extern _g_dll_code_phys     ; unsigned long[4]
extern _g_dll_code_size     ; unsigned short[4]
extern _g_dll_data_phys     ; unsigned long[4]
extern _g_dll_data_size     ; unsigned short[4]
extern _g_dll_has_data      ; unsigned short[4]
extern _g_ndll              ; unsigned short

; Thunk / IDT (z loader.c)
extern _g_thunk_phys        ; unsigned long
extern _g_thunk_size        ; unsigned short
extern _g_idt_phys          ; unsigned long

; VESA (z loader.c)
extern _g_lfb_phys          ; unsigned long - adres fizyczny LFB

; =====================================================================
; get_int3f_off_(): zwraca offset int3f_handler w segmencie (dla IDT)
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

    ; Patchuj IDTR: base = g_idt_phys
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
    ; --- Stale selektory (loader, PM glue) ---
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

    ; --- SEL_APP_CODE ---
    mov  eax, [_g_app_phys]
    mov  [cs:gdt_app_code + 2], ax
    shr  eax, 16
    mov  [cs:gdt_app_code + 4], al
    mov  [cs:gdt_app_code + 7], ah
    mov  ax, [_g_code_size]
    dec  ax
    mov  [cs:gdt_app_code + 0], ax

    ; --- SEL_VGA ---
    mov  word [cs:gdt_vga + 0], 0x0F9F
    mov  word [cs:gdt_vga + 2], 0x8000
    mov  byte [cs:gdt_vga + 4], 0x0B
    mov  byte [cs:gdt_vga + 7], 0x00

    ; --- rm_jmp segment ---
    mov  ax, [_g_orig_cs]
    mov  [cs:rm_jmp + 2], ax

    ; --- SEL_APP_DATA ---
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

    ; --- SEL_THUNK (0x88): segment thunkow ---
    mov  eax, [_g_thunk_phys]
    mov  [cs:gdt_thunk + 2], ax
    shr  eax, 16
    mov  [cs:gdt_thunk + 4], al
    mov  [cs:gdt_thunk + 7], ah
    mov  ax, [_g_thunk_size]
    dec  ax
    mov  [cs:gdt_thunk + 0], ax

    ; --- SEL_VESA (0x90): VESA LFB (32-bit data) ---
    ; limit juz ustawiony na 0xFFFFF w gdt_vesa (1MB, byte granularity)
    mov  eax, [_g_lfb_phys]
    mov  [cs:gdt_vesa + 2], ax
    shr  eax, 16
    mov  [cs:gdt_vesa + 4], al
    mov  [cs:gdt_vesa + 7], ah

    ; --- DLL GDT entries (petla przez g_ndll DLL) ---
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
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax
    mov  esp, 0x0009FC00

    ; -------------------------------------------------------
    ; Wypelnij VESA framebuffer (jesli LFB zostal ustawiony)
    ; SEL_VESA = 32-bit segment z base=g_lfb_phys, limit=1MB
    ; -------------------------------------------------------
    mov  eax, [_g_lfb_phys]
    test eax, eax
    jz   .skip_vesa

    mov  ax, SEL_VESA
    mov  es, ax                 ; ES:0 = poczatek LFB

    ; Wypelnij caly ekran (640*480*3 = 921600 B) ciemnym granatem
    ; BGR: B=0x33 G=0x11 R=0x00 -> powtarzajacy sie wzor bajtow: 33 11 00
    ; Uzywamy rep stosd: wzor 4B dla zachowania ciaglosci BGR
    ; 4B = 33 11 00 33 (piksel1-B, piksel1-G, piksel1-R, piksel2-B)
    ; Nastepne 4B: 11 00 33 11 itd.
    ; To skomplikowane - zamiast tego uzyjmy rep stosb z jednym bajtem
    ; dla prostoty STEP9a: wypelnij zerem (czarny) + pasek bialy u gory
    xor  edi, edi
    xor  eax, eax
    mov  ecx, VESA_FB_SIZE / 4
    rep  stosd
    ; ostatnie bajty (VESA_FB_SIZE mod 4 = 921600 mod 4 = 0, brak)

    ; Bialy pasek: pierwsze 20 wierszy (20 * 640 * 3 = 38400 B = 9600 dwords)
    xor  edi, edi
    mov  eax, 0xFFFFFFFF
    mov  ecx, (VESA_FB_COLS * 20 * VESA_FB_BPP) / 4
    rep  stosd

    ; Granatowy pasek: wiersze 21..40 (oddolny gradient wizualny)
    ; BGR = 33 00 00 (ciemny niebieski)
    ; Bajty: 33 00 00 33 -> dword little-endian: 0x33000033
    mov  edi, VESA_FB_COLS * 20 * VESA_FB_BPP
    mov  eax, 0x33000033
    mov  ecx, (VESA_FB_COLS * 20 * VESA_FB_BPP) / 4
    rep  stosd

    ; Przywroc ES = SEL_DATA32
    mov  ax, SEL_DATA32
    mov  es, ax

.skip_vesa:
    ; Przelacz stos na SEL_DATA16 przed skokiem do 16-bit
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

    ; DEBUG: wyslij 'A' na COM1 po powrocie apki
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
    ; DEBUG: wyslij 'R' na COM1
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
    ; DEBUG: wyslij 'F' (przed retf)
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

    ; --- Czytaj dane thunku przez ES=SEL_THUNK ---
    mov  si, [bp+2]             ; si = IP_after_CD3F
    mov  bx, [bp+4]             ; bx = SEL_THUNK
    mov  es, bx
    mov  bl, byte [es:si]       ; bl = dll_idx (0-based)
    xor  bh, bh
    mov  si, [es:si + 1]        ; si = func_off w DLL code seg

    ; --- Oblicz selektory DLL ---
    mov  ax, bx
    shl  ax, 4
    add  ax, 0x48               ; ax = SEL_DLL_CODE(dll_idx)
    mov  cx, bx
    shl  cx, 4
    add  cx, 0x50               ; cx = SEL_DLL_DATA(dll_idx)

    push ax
    push cx

    mov  ax, [ss:int3f_depth]
    mov  bx, ax
    shl  bx, 1
    mov  cx, bx
    shl  cx, 1
    add  bx, cx                 ; bx = depth * 6

    mov  [ss:int3f_stack + bx + 0], ds
    mov  cx, [bp+8]
    mov  [ss:int3f_stack + bx + 2], cx
    mov  cx, [bp+10]
    mov  [ss:int3f_stack + bx + 4], cx

    inc  word [ss:int3f_depth]

    pop  cx                     ; cx = DATA_SEL
    pop  ax                     ; ax = CODE_SEL

    mov  [bp+2], si             ; IP = func_off
    mov  [bp+4], ax             ; CS = SEL_DLL_CODE

    mov  word [bp+8],  int3f_trampoline
    mov  word [bp+10], SEL_CODE16

    mov  ds, cx                 ; DS = SEL_DLL_DATA

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
    add  bx, cx             ; bx = depth * 6

    mov  cx, [ss:int3f_stack + bx + 0]   ; cx = saved_ds
    mov  ax, [ss:int3f_stack + bx + 2]   ; ax = orig_ret_ip
    mov  [ss:tramp_ret_ip], ax
    mov  ax, [ss:int3f_stack + bx + 4]   ; ax = orig_ret_cs
    mov  [ss:tramp_ret_cs], ax
    mov  [ss:tramp_saved_ds], cx

    pop  cx
    pop  bx
    pop  ax

    ; DEBUG: wyslij 'T' na COM1 przed JMP FAR
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

    db   0x2E, 0xFF, 0x2E  ; CS: JMP FAR [disp16]
    dw   tramp_ret_ip

; Dane dla trampoline
tramp_ret_ip:   dw 0
tramp_ret_cs:   dw 0
tramp_saved_ds: dw 0

; =====================================================================
pm_msg          db "STEP9a PM: VESA LFB fill...", 0
saved_ss        dw 0
saved_sp        dw 0
saved_ds        dw 0
local_entry_ip  dw 0
local_has_data  dw 0
call_ptr        dw 0, 0
rm_jmp          dw rm_real
                dw 0

; INT 3F save stack: INT3F_MAX_DEPTH wpisow po 6 bajtow (ds, ret_ip, ret_cs)
int3f_depth     dw 0
int3f_stack     times INT3F_MAX_DEPTH * 6  db 0

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

; DLL sloty (4 DLL max, kazdy: code + data = 2 x 8 bajtow)
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
    db 0x00                 ; base[23:16] = 0 (patchowane w patch_gdt)
    db 10010010b            ; P=1 DPL=0 S=1 E=0 W=1 A=0 (data r/w)
    db 01001111b            ; G=0 D/B=1 L=0 AVL=0 limit[19:16]=F -> limit=0xFFFFF=1MB
    db 0x00                 ; base[31:24] = 0 (patchowane w patch_gdt)

gdt_end:

gdtr:
    dw gdt_end - gdt - 1
    dd 0

idtr:
    dw 64 * 8 - 1           ; limit = 511 (64 wpisow)
    dd 0                     ; base = g_idt_phys (patchowane w pm_call_app_)
