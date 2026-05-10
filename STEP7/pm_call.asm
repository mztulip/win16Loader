; pm_call.asm - PM glue (STEP7)
;
; Rozszerzenie STEP6:
;   - SEL_THUNK (0x88): segment thunkow INT 3F (base=g_thunk_phys)
;   - IDT z handlerem wektora 0x3F (int3f_handler)
;   - LIDT przed wejsciem w PM
;   - int3f_handler: ustawia DS=DGROUP DLL, patchuje return addr -> trampoline
;   - int3f_trampoline: przywraca DS callera, JMP FAR do orig_ret
;
; GDT selektory DLL:
;   DLL i (0-based): code = 0x48 + i*0x10, data = 0x50 + i*0x10
;   SEL_THUNK = 0x88 (16-bit code, base=g_thunk_phys)
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

INT3F_MAX_DEPTH equ 8      ; maks. zagniezdzen wywolan DLL

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

    ; Patchuj IDTR: base = g_cs_phys + idt_int3f
    ; (IDT jest alokowane przez loader.c w DOS memory, g_idt_phys)
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

    mov  edi, 0x000B8000
    mov  ecx, 80 * 25
    mov  ax, 0x0720
    rep  stosw

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
;
; Stos na wejsciu (SS = SEL_DATA16, writable):
;   sp+0: IP_after_CD3F  (wskazuje na bajt dll_idx w thunku)
;   sp+2: CS             (= SEL_THUNK)
;   sp+4: FLAGS
;   sp+6: orig_ret_ip    (adres powrotu w callerze, np. ne_test_app)
;   sp+8: orig_ret_cs    (= SEL_APP_CODE)
;   sp+10: args...
;
; UWAGA: CS (SEL_CODE16) jest Execute/Read - nie mozna pisac przez CS:
; SS (SEL_DATA16) ma ten sam base co CS ale jest R/W -> uzywamy SS: do zapisu.
; =====================================================================
int3f_handler:
    push bp
    mov  bp, sp
    ; [bp+2] = IP_after_CD3F
    ; [bp+4] = CS = SEL_THUNK
    ; [bp+6] = FLAGS
    ; [bp+8] = orig_ret_ip
    ; [bp+10]= orig_ret_cs
    push ax
    push bx
    push cx
    push si
    push es

    ; --- Czytaj dane thunku przez ES=SEL_THUNK ---
    mov  si, [bp+2]             ; si = IP_after_CD3F (offset dll_idx w SEL_THUNK)
    mov  bx, [bp+4]             ; bx = SEL_THUNK
    mov  es, bx
    mov  bl, byte [es:si]       ; bl = dll_idx (0-based)
    xor  bh, bh                 ; bx = dll_idx
    mov  si, [es:si + 1]        ; si = func_off w DLL code seg

    ; --- Oblicz selektory DLL ---
    mov  ax, bx
    shl  ax, 4
    add  ax, 0x48               ; ax = SEL_DLL_CODE(dll_idx)
    mov  cx, bx
    shl  cx, 4
    add  cx, 0x50               ; cx = SEL_DLL_DATA(dll_idx)
    ; ax=CODE_SEL, cx=DATA_SEL, si=func_off

    ; --- Zapisz do int3f_stack[depth] przez SS: (writable, same base) ---
    push ax                     ; tymczasowo na stos
    push cx

    mov  ax, [ss:int3f_depth]   ; ax = current depth (SS: = SEL_DATA16 = writable)
    mov  bx, ax
    shl  bx, 1
    mov  cx, bx
    shl  cx, 1
    add  bx, cx                 ; bx = depth * 6 = offset w int3f_stack

    ; Zapisz DS callera (aktualny DS = SEL_APP_DATA lub SEL_DLL_DATA)
    mov  [ss:int3f_stack + bx + 0], ds
    ; Zapisz orig_ret_ip i orig_ret_cs z ramki bp
    mov  cx, [bp+8]
    mov  [ss:int3f_stack + bx + 2], cx
    mov  cx, [bp+10]
    mov  [ss:int3f_stack + bx + 4], cx

    inc  word [ss:int3f_depth]

    pop  cx                     ; cx = DATA_SEL
    pop  ax                     ; ax = CODE_SEL

    ; --- Patchuj IRET frame: CS=SEL_DLL_CODE, IP=func_off ---
    mov  [bp+2], si             ; IP = func_off (stos jest na SS = writable)
    mov  [bp+4], ax             ; CS = SEL_DLL_CODE

    ; --- Patchuj adres powrotu callera -> trampoline:SEL_CODE16 ---
    mov  word [bp+8],  int3f_trampoline
    mov  word [bp+10], SEL_CODE16

    ; --- Ustaw DS = DGROUP DLL ---
    mov  ds, cx                 ; cx = SEL_DLL_DATA

    pop  es
    pop  si
    pop  cx
    pop  bx
    pop  ax
    pop  bp
    iret

; =====================================================================
; INT 3F trampoline
;
; Wywolany przez RETF n z funkcji DLL.
; DS = DGROUP DLL (trzeba przywrocic DS callera).
; Stos jest czysty (RETF n usunelo argumenty).
; Uzywamy SS: do zapisu, CS: do odczytu (JMP FAR).
; =====================================================================
int3f_trampoline:
    push ax
    push bx
    push cx

    ; Dekrementuj depth i pobierz wpis (SS: = writable)
    dec  word [ss:int3f_depth]
    mov  ax, [ss:int3f_depth]

    mov  bx, ax
    shl  bx, 1
    mov  cx, bx
    shl  cx, 1
    add  bx, cx             ; bx = depth * 6

    ; Wczytaj saved_ds, ret_ip, ret_cs
    mov  cx, [ss:int3f_stack + bx + 0]   ; cx = saved_ds
    mov  ax, [ss:int3f_stack + bx + 2]   ; ax = orig_ret_ip
    mov  [ss:tramp_ret_ip], ax
    mov  ax, [ss:int3f_stack + bx + 4]   ; ax = orig_ret_cs
    mov  [ss:tramp_ret_cs], ax
    mov  [ss:tramp_saved_ds], cx          ; zachowaj saved_ds przed popami (pop cx nadpisze cx)

    pop  cx                               ; odrzuc stary cx (nadpisany podczas obliczen)
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

    ; Przywroc DS callera
    mov  cx, [ss:tramp_saved_ds]
    mov  ds, cx

    ; JMP FAR [cs:tramp_ret_ip] - czytamy z kodu (readable, nie wymaga write)
    db   0x2E, 0xFF, 0x2E  ; CS: JMP FAR [disp16]
    dw   tramp_ret_ip

; Dane dla trampoline (w _TEXT, dostepne przez SS: lub CS:)
tramp_ret_ip:   dw 0
tramp_ret_cs:   dw 0
tramp_saved_ds: dw 0

; =====================================================================
pm_msg          db "STEP7 PM: INT 3F DLL thunk...", 0
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

gdt_end:

gdtr:
    dw gdt_end - gdt - 1
    dd 0

idtr:
    dw 64 * 8 - 1           ; limit = 511 (64 wpisow)
    dd 0                     ; base = g_idt_phys (patchowane w pm_call_app_)
