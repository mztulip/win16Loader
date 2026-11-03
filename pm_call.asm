; pm_call.asm - PM glue (STEP17)
;
; Nowe vs STEP16:
;   - IRQ12 handler (mysz PS/2): 3-bajtowy pakiet -> abs X/Y/btn -> KCB
;   - get_irq12_off_: offset irq12_handler dla loadera (IDT[0x2C])
;   - PIC slave mask: 0xFF -> 0xEF (odblokowanie IRQ12, bit 4)
;
; Poprzednie (STEP16):
;   - IRQ1 handler (klawiatura): scan codes -> Virtual Keys -> WM_KEYDOWN
;
; Poprzednie zmiany (STEP12-15):
;   - SEL_KCB=0x98: Kernel Control Block (16-bit data, base=g_kcb_phys)
;     Struktura KCB (256 bajtow):
;       [0] WORD  app_hinstance  = SEL_APP_DATA (0x40)
;       [2] WORD  next_dyn_sel   = GDYN_FIRST (0x130)
;       [4] DWORD heap_phys      = baza globalnego heapa
;       [8] DWORD heap_next      = aktualny bump pointer (phys)
;       [12] DWORD heap_end      = koniec heapa (phys)
;   - SEL_GDT_ACCESS=0x120: samoodniesienie do GDT (dla dynamicznych deskryptorow)
;     GlobalAlloc w kernel.c pisze przez ten selektor nowe deskryptory.
;   - SEL_BITMAPS=0x128: bufor bitmap SKI.EXE (56KB, 86 sprite'ow 4bpp)
;   - gdt_dyn (0x130..0x527): 127 slotow dla GlobalAlloc
;
; Selektory GDT:
;   SEL_CODE32=0x08, SEL_DATA32=0x10, SEL_DATASEG=0x18
;   SEL_CODE16=0x20, SEL_DATA16=0x28
;   SEL_APP_CODE=0x30, SEL_PSP=0x38, SEL_APP_DATA=0x40
;   0x48..0x80: DLL code/data (4 DLL max)
;   SEL_THUNK=0x88, SEL_VESA=0x90 (32-bit 1MB alias)
;   SEL_KCB=0x98 (Kernel Control Block, 256B)
;   0xA0..0x110: 15 okien VESA po 64KB
;   0x118: SEL_FONT
;   0x120: SEL_GDT_ACCESS (samoodniesienie GDT, dla GlobalAlloc)
;   0x128: SEL_BITMAPS (bufor 86 sprite'ow 4bpp z SKI.EXE)
;   0x130..0x527: 127 dynamicznych slotow (GlobalAlloc)
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
SEL_BITMAPS     equ 0x128
GDYN_FIRST      equ 0x130
GDYN_COUNT      equ 127
SEL_HEAP        equ 0x528   ; 16-bit/32-bit data, base=0x100000 (XMS GlobalHeap)

; GDT: stala czesc 0x00..0x52F (jak dotad) + rozszerzenie do GDT_TOTAL_SIZE.
; Rozszerzenie (wypelniane przez loader, module.c):
;   0x530        SEL_MODTAB   tablica modulow (MT_SIZE B/modul) - DS dla thunkow
;   0x538..0x5F8 stale selektory (okna pamieci RM, bufory)
;   0x600..      segmenty modulow (kolejne selektory per modul), potem
;                dynamiczne GlobalAlloc (KCB[512] = max selektor)
GDT_TOTAL_SIZE  equ 0x4000
GDT_LIMIT       equ GDT_TOTAL_SIZE - 1

SEL_MODTAB      equ 0x530
PIC_BASE_M      equ 0x50    ; IRQ0-7  -> INT 50h-57h
PIC_BASE_S      equ 0x58    ; IRQ8-15 -> INT 58h-5Fh
KCB_INT_CB      equ 538     ; DWORD: far ptr dyspozytora przerwan programowych (KERNEL)
KCB_CALLER_DS   equ 520     ; WORD: DS wolajacego ostatnie API (int3f_handler)
KCB_TRACE       equ 536     ; WORD: !=0 -> int3f_handler loguje kazde wywolanie API
MT_SHIFT        equ 7       ; 128 B na wpis tablicy modulow
MT_DGROUP       equ 10      ; offset WORD dgroup (DS modulu) we wpisie

INT3F_MAX_DEPTH equ 32
MAX_DLLINIT     equ 16      ; max DLL z procedura inicjalizacji (LibEntry)

; DEBUG_STACK=1: przy wyjatku zrzuc rejestry, stos i lancuch ramek BP
; (serial + ekran paniki). Koszt tylko w chwili wyjatku.
; Wylaczenie: make DEBUG_STACK=0
%ifndef DEBUG_STACK
%define DEBUG_STACK 1
%endif
STACK_DUMP_WORDS equ 256
FRAME_DUMP_MAX   equ 8

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
global get_rmint_off_
global get_softint_off_
global get_iretstub_off_
global get_int21_off_
global get_irq0_off_
global get_irq1_off_
global get_irq12_off_
global get_gpf_off_
global get_exc00_off_
global get_exc06_off_
global get_exc08_off_
global get_exc0B_off_
global get_exc0C_off_
global get_exc0D_off_
global get_exc0E_off_

extern _g_app_phys
extern _g_code_size
extern _g_entry_ip
extern _g_entry_cs      ; selektor segmentu wejscia apki (module.c)
extern _g_rmbuf_seg     ; segment RM bufora transferowego (RMREGS + dane)
extern _g_app_ds        ; selektor DGROUP apki (= hInstance)
extern _g_ndllinit      ; liczba DLL do zainicjalizowania
extern _g_dllinit       ; {cs, ip, ds, heap} x MAX_DLLINIT
extern _g_cs_phys
extern _g_orig_cs
extern _g_orig_ss
extern _g_orig_sp
extern _g_app_data_phys
extern _g_data_size
extern _g_has_data
extern _g_init_sp

extern _g_thunk_phys
extern _g_thunk_size
extern _g_idt_phys

extern _g_lfb_phys
extern _g_font_phys
extern _g_kcb_phys
extern _g_ext_mem_kb
extern _g_mem_str
extern _g_psp_phys
extern _g_bitmaps_phys

extern patch_gdt_c_     ; C implementation of patch_gdt (pm_helpers.c)
extern _g_gdt_off_c     ; offset tablicy GDT w segmencie kodu (dla patch_gdt_c_)
extern irq1_c_          ; C near: irq1_c() w keyboard.c -> symbol 'irq1_c_'
extern mouse_c_         ; C near: mouse_c() w mouse.c  -> symbol 'mouse_c_'

; =====================================================================
get_int3f_off_:
    mov  ax, int3f_handler
    ret

get_rmint_off_:
    mov  ax, rm_int_service
    ret

get_softint_off_:
    mov  ax, softint_stubs
    ret

get_iretstub_off_:
    mov  ax, iret_stub
    ret

get_int21_off_:
    mov  ax, int21_handler
    ret

get_irq0_off_:
    mov  ax, irq0_handler
    ret

get_irq1_off_:
    mov  ax, irq1_handler
    ret

get_irq12_off_:
    mov  ax, irq12_handler
    ret

get_gpf_off_:
    mov  ax, gpf_handler
    ret

get_exc00_off_: mov ax, exc_stub_00
                ret
get_exc06_off_: mov ax, exc_stub_06
                ret
get_exc08_off_: mov ax, exc_stub_08
                ret
get_exc0B_off_: mov ax, exc_stub_0B
                ret
get_exc0C_off_: mov ax, exc_stub_0C
                ret
get_exc0D_off_: mov ax, exc_stub_0D
                ret
get_exc0E_off_: mov ax, exc_stub_0E
                ret

; =====================================================================
; dbg_printhex4: print AX as 4 hex digits via COM1. Clobbers AX, BX, CX.
dbg_printhex4:
    push cx
    push bx
    mov  bx, ax
    mov  cx, 4
.dph_loop:
    rol  bx, 4
    mov  al, bl
    and  al, 0x0F
    add  al, '0'
    cmp  al, '9'
    jbe  .dph_ok
    add  al, 7
.dph_ok:
    push ax               ; save char to print
    push dx               ; save COM port
    mov  dx, 0x3FD        ; LSR
.dph_wait: in al, dx
    test al, 0x20
    jz   .dph_wait
    pop  dx               ; restore COM data port
    pop  ax               ; restore char
    out  dx, al
    loop .dph_loop
    pop  bx
    pop  cx
    ret
; =====================================================================
pm_call_app_:
    ; Rejestry zachowywane przez callee w konwencji Watcom (main() trzyma w nich
    ; zmienne lokalne / BP ramki) - zdejmowane w rm_real przed RETF.
    push bp
    push si
    push di
    push bx
    push cx
    push dx
    push es

    ; Zachowaj maski PIC trybu rzeczywistego (przywracane w pm16_app_returned)
    in   al, 0x21
    mov  [cs:saved_pic1_mask], al
    in   al, 0xA1
    mov  [cs:saved_pic2_mask], al

    mov  ax, ss
    mov  [cs:saved_ss], ax
    mov  ax, sp
    mov  [cs:saved_sp], ax
    mov  [cs:saved_ds], ds

    ; Fix g_orig_cs / g_cs_phys: loader.c runs in a different code segment than
    ; pm_call.asm (Watcom -ml large model).  We need g_orig_cs = OUR CS so that
    ; patch_gdt_c_ accesses the GDT (which lives here) via the correct segment,
    ; and so that GDTR.base = g_cs_phys + gdt_offset = correct physical address.
    mov  ax, cs
    mov  [_g_orig_cs], ax
    movzx eax, ax
    shl  eax, 4
    mov  [_g_cs_phys], eax

    mov  ax, [_g_entry_ip]
    mov  [cs:local_entry_ip], ax
    mov  ax, [_g_entry_cs]
    mov  [cs:local_entry_cs], ax
    mov  ax, [_g_app_ds]
    mov  [cs:local_app_ds], ax
    mov  ax, [_g_ndllinit]
    mov  [cs:local_ndllinit], ax

    ; Tablica inicjalizacji DLL (DGROUP loadera -> segment kodu)
    push es
    push si
    push di
    push cx
    mov  ax, cs
    mov  es, ax
    mov  di, local_dllinit
    mov  si, _g_dllinit
    mov  cx, MAX_DLLINIT * 8
    rep  movsb
    pop  cx
    pop  di
    pop  si
    pop  es

    mov  ax, [_g_init_sp]
    mov  [cs:local_init_sp], ax
    mov  ax, [_g_has_data]
    mov  [cs:local_has_data], ax

    ; patch rm_jmp.CS = g_orig_cs so RETF to real mode works
    mov  ax, [_g_orig_cs]
    mov  [cs:rm_jmp + 2], ax
    mov  [cs:rmi_rm_ptr + 2], ax
    mov  ax, [_g_rmbuf_seg]
    mov  [cs:rmi_buf_seg], ax

    ; Przekaz offsets potrzebne przez patch_gdt_c_ (leza w _TEXT, nie DGROUP)
    ; g_gdt_off_c: offset tablicy GDT w segmencie kodu
    mov  ax, gdt
    mov  [_g_gdt_off_c], ax

    ; Lokalne kopie 32-bit globals dla pm32_entry (DS=SEL_DATASEG=code seg w PM)
    mov  eax, [_g_lfb_phys]
    mov  [cs:local_lfb_phys], eax
    mov  eax, [_g_font_phys]
    mov  [cs:local_font_phys], eax
    mov  eax, [_g_ext_mem_kb]
    mov  [cs:local_ext_mem_kb], eax

    ; Kopiuj g_mem_str (64B z DGROUP) do local_mem_str (w segmencie kodu)
    ; DS=DGROUP (poprawne), ES ustawiamy chwilowo na CS
    push es
    push si
    push di
    push cx
    mov  ax, cs
    mov  es, ax
    mov  di, local_mem_str
    mov  si, _g_mem_str
    mov  cx, 64
    rep  movsb
    pop  cx
    pop  di
    pop  si
    pop  es

    push ds                     ; Watcom far ptr writes may clobber DS
    call far patch_gdt_c_       ; GDT base/limit patching in C (pm_helpers.c)
    pop  ds                     ; restore DGROUP

    mov  eax, [_g_cs_phys]
    add  eax, pm32_entry
    mov  [cs:jmp32_off], eax

    ; Precompute adres panic_entry_32 i zachowaj kopie g_cs_phys
    mov  eax, [_g_cs_phys]
    mov  [cs:local_cs_phys], eax
    add  eax, panic_entry_32
    mov  [cs:exc_panic_jmp_dd], eax

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

; patch_gdt przeniesiony do pm_helpers.c (patch_gdt_c)

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

    mov  eax, [local_lfb_phys]
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

    mov  eax, [local_font_phys]
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

    lea  esi, [local_mem_str]
    mov  ebx, 10
    mov  ecx, 66
    mov  edx, 0x00FFFF00    ; zolty - wyroznia info o RAM
    call draw_str_32

    ; Ostrzezenie o malej pamieci: jesli ext_mem < 2MB (2048 KB) -> czerwony tekst
    cmp  dword [local_ext_mem_kb], 2048
    jae  .skip_draw
    lea  esi, [str_low_mem]
    mov  ebx, 10
    mov  ecx, 82
    mov  edx, 0x00FF4040    ; czerwony
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
    add  eax, [local_font_phys]
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

str_title   db "STEP17: klawiatura IRQ1 + mysz PS/2 IRQ12", 0
str_mode    db "SEL_KCB=0x98  SEL_GDT_ACCESS=0x120  SEL_HEAP=0x528 (XMS)", 0
str_font    db "SEL_BITMAPS=0x128 (86 sprites)  127 dyn slots (0x130..0x527)", 0
str_low_mem db "WARNING: Low memory (<2MB extended) - GlobalAlloc may fail!", 0

; =====================================================================
bits 16
pm16_call_app:
    mov  ax, SEL_DATA16
    mov  ds, ax
    mov  ax, SEL_PSP
    mov  es, ax

    mov  ax, [local_entry_ip]
    mov  [call_ptr], ax
    mov  ax, [local_entry_cs]
    mov  [call_ptr + 2], ax

    ; Win16: SS=DS=DGROUP apki przed wejsciem (DLL init tez biegnie na tym stosie)
    mov  ax, [local_app_ds]
    test ax, ax
    jz   .call_app           ; brak DGROUP: zostaje stos SEL_DATA16
    mov  ss, ax
    mov  sp, [cs:local_init_sp]  ; SP obliczony z ne_heap+ne_stack

.call_app:
    ; Przemapuj PIC: master IRQ0-7 -> INT 0x20-0x27 (jak Windows 3.1)
    ; Bez tego IRQ0 trafia do IDT[8] = double fault (konflikt CPU/PIC w PM).
    mov  al, 0x11       ; ICW1: cascade, ICW4 needed
    out  0x20, al
    out  0xA0, al
    mov  al, PIC_BASE_M ; ICW2 master: IRQ0-7 -> INT 50h-57h (jak Windows;
    out  0x21, al       ;   wektory 20h-2Fh wolne dla programowych INT 21h/25h/2Fh)
    mov  al, PIC_BASE_S ; ICW2 slave: IRQ8-15 -> INT 58h-5Fh
    out  0xA1, al
    mov  al, 0x04       ; ICW3 master: slave on IRQ2
    out  0x21, al
    mov  al, 0x02       ; ICW3 slave: cascade identity 2
    out  0xA1, al
    mov  al, 0x01       ; ICW4: 8086 mode
    out  0x21, al
    out  0xA1, al
    mov  al, 0xF8       ; maska master: wlacz IRQ0+IRQ1+IRQ2(cascade->slave PIC)
    out  0x21, al
    mov  al, 0xEF       ; maska slave: wlacz IRQ12 (mysz PS/2, bit 4=0)
    out  0xA1, al
    ; Ustaw CCB kontrolera PS/2: bit0=kbd_irq, bit1=aux_irq, bit6=translate.
    ; Robimy to po IDT (irq12_handler gotowy) i przed STI - bez odczytu CCB
    ; (unikamy problemu gdy BIOS INT9 zjada bajt odpowiedzi komendy Read CCB 0x20).
    ; 0x47 = 01000111b: kbd_irq(0) | aux_irq(1) | self_test(2) | translate(6)
.ccb_wait1:
    in   al, 0x64
    test al, 0x02       ; IBF: czekaj az bufor wejscia pusty
    jnz  .ccb_wait1
    mov  al, 0x60       ; Write CCB
    out  0x64, al
.ccb_wait2:
    in   al, 0x64
    test al, 0x02
    jnz  .ccb_wait2
    mov  al, 0x47       ; kbd_irq | aux_irq | translate
    out  0x60, al
    ; PIT: pozostaje domyslny 18.2 Hz (divisor=65536 -> ~55ms/tick)
    ; Przeprogramowanie do wyzszej czestotliwosci powoduje overflow w SKI.EXE.
    ; Zapamietaj SS:SP sprzed wywolania apki. INT 21h/4Ch (int21_handler .terminate)
    ; odtwarza je i skacze do pm16_app_returned - tak jakby entry apki zrobilo RETF.
    mov  ax, SEL_DATA16
    mov  ds, ax
    mov  [app_ret_ss], ss
    mov  [app_ret_sp], sp
    mov  word [int3f_depth], 0
    sti                 ; wlacz przerwania: IRQ0 -> IDT[0x20] -> irq0_handler

    ; Inicjalizacja DLL (LibEntry) w kolejnosci zaleznosci. Rejestry jak w Win16:
    ; DS=DGROUP DLL, DI=hInstance, CX=rozmiar local heap, ES:SI=linia polecen.
    xor  bx, bx
.dll_init_loop:
    cmp  bx, [local_ndllinit]
    jae  .dll_init_done
    push bx
    mov  si, bx
    shl  si, 3
    mov  ax, [local_dllinit + si + 2]   ; ip
    mov  [init_ptr], ax
    mov  ax, [local_dllinit + si]       ; cs
    mov  [init_ptr + 2], ax
    mov  cx, [local_dllinit + si + 6]   ; heap
    mov  di, [local_dllinit + si + 4]   ; DGROUP = hInstance
    mov  ax, SEL_PSP
    mov  es, ax
    mov  si, 0x80
    mov  ds, di
    xor  bp, bp
    call far [cs:init_ptr]
    mov  dx, SEL_DATA16
    mov  ds, dx
    pop  bx
    inc  bx
    jmp  .dll_init_loop
.dll_init_done:

    ; Wejscie apki: DS=DGROUP, DI=hInstance, SI=hPrevInstance=0, ES=PSP, BP=0
    mov  ax, SEL_PSP
    mov  es, ax
    mov  di, [local_app_ds]
    xor  si, si
    xor  bp, bp
    mov  ax, di
    test ax, ax
    jz   .no_app_ds
    mov  ds, ax
.no_app_ds:
    call far [cs:call_ptr]
pm16_app_returned:
    cli

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
    mov  es, ax
    mov  fs, ax
    mov  gs, ax

    ; Przywroc PIC do ukladu BIOS (IRQ0-7 -> INT 08h, IRQ8-15 -> INT 70h),
    ; inaczej po powrocie do DOS IRQ0 trafilby w INT 20h (terminate).
    mov  al, 0x11
    out  0x20, al
    out  0xA0, al
    mov  al, 0x08
    out  0x21, al
    mov  al, 0x70
    out  0xA1, al
    mov  al, 0x04
    out  0x21, al
    mov  al, 0x02
    out  0xA1, al
    mov  al, 0x01
    out  0x21, al
    out  0xA1, al
    mov  al, [saved_pic1_mask]
    out  0x21, al
    mov  al, [saved_pic2_mask]
    out  0xA1, al

    mov  eax, cr0
    and  eax, ~1
    mov  cr0, eax

    jmp  far [rm_jmp]

; =====================================================================
rm_real:
    lidt [cs:rm_idtr]   ; IVT trybu rzeczywistego (baza 0, limit 0x3FF)
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
    mov  es, ax
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
    pop  es
    pop  dx
    pop  cx
    pop  bx
    pop  di
    pop  si
    pop  bp
    sti
    retf

; =====================================================================
; int3f_handler: thunk dispatcher for Win16 DLL calls.
; SS=SEL_APP_DATA (DGROUP) when called from app; we use ES=SEL_DATA16
; to access loader data (int3f_depth, int3f_stack) without touching SS.
; =====================================================================
int3f_handler:
    ; Thunk (8 B, w SEL_THUNK): CD 3F | mod | off16 | sel16 | 0
    ;   mod < 0xFF: wywolanie funkcji modulu mod pod sel:off, DS = MODTAB[mod].dgroup
    ;   mod = 0xFF: stub niezaimplementowanej funkcji:
    ;               off16 = ordinal, sel16 lo = bajty argumentow, hi = modul docelowy
    ; Stos: [bp+2]=IP(thunk+2) [bp+4]=CS [bp+6]=FLAGS [bp+8]=ret IP [bp+10]=ret CS
    push bp
    mov  bp, sp
    push ax
    push bx
    push cx
    push si
    push es

    mov  si, [bp+2]
    mov  es, [bp+4]
    mov  bl, [es:si]
    cmp  bl, 0xFF
    je   .stub

    mov  ax, [es:si+1]
    mov  [bp+2], ax          ; nowy IP = funkcja modulu
    mov  ax, [es:si+3]
    mov  [bp+4], ax          ; nowy CS

    ; Trace API: "> mm off ret_cs:ret_ip\n" (KCB_TRACE)
    push es
    mov  ax, SEL_KCB
    mov  es, ax
    cmp  word [es:KCB_TRACE], 0
    pop  es
    je   .no_trace
    mov  al, '>'
    call ser_putc
    mov  al, bl
    call ser_hex8
    mov  al, ' '
    call ser_putc
    mov  ax, [bp+2]
    xchg al, ah
    call ser_hex8
    xchg al, ah
    call ser_hex8
    mov  al, ' '
    call ser_putc
    mov  ax, [bp+10]
    xchg al, ah
    call ser_hex8
    xchg al, ah
    call ser_hex8
    mov  al, ':'
    call ser_putc
    mov  ax, [bp+8]
    xchg al, ah
    call ser_hex8
    xchg al, ah
    call ser_hex8
    mov  al, 10
    call ser_putc
.no_trace:

    xor  bh, bh
    mov  ax, SEL_MODTAB
    mov  es, ax
    mov  cl, MT_SHIFT
    shl  bx, cl
    mov  cx, [es:bx + MT_DGROUP]   ; CX = DS modulu (0 = zostaw DS wolajacego)

    ; Zapisz DS wolajacego + adres powrotu na stosie int3f (trampolina odtworzy)
    mov  ax, SEL_DATA16
    mov  es, ax
    mov  bx, [es:int3f_depth]
    cmp  bx, INT3F_MAX_DEPTH
    jb   .depth_ok
    int  0x06                ; przepelnienie zagniezdzen -> #UD panic
.depth_ok:
    mov  ax, bx
    shl  bx, 1
    shl  ax, 2
    add  bx, ax              ; BX = depth * 6
    mov  [es:int3f_stack + bx + 0], ds
    mov  ax, [bp+8]
    mov  [es:int3f_stack + bx + 2], ax
    mov  ax, [bp+10]
    mov  [es:int3f_stack + bx + 4], ax
    inc  word [es:int3f_depth]
    mov  ax, SEL_KCB
    mov  es, ax
    mov  [es:KCB_CALLER_DS], ds    ; DS wolajacego (DOS3Call, LocalAlloc...)

    mov  word [bp+8],  int3f_trampoline
    mov  word [bp+10], SEL_CODE16

    jcxz .keep_ds
    mov  ds, cx
.keep_ds:
    pop  es
    pop  si
    pop  cx
    pop  bx
    pop  ax
    pop  bp
    iret

.stub:
    ; Log na COM1: "UNIMPL mm:oooo\n" (mm = indeks modulu, oooo = ordinal)
    push dx
    mov  al, 'U'
    call ser_putc
    mov  al, '!'
    call ser_putc
    mov  al, [es:si+4]
    call ser_hex8
    mov  al, ':'
    call ser_putc
    mov  ax, [es:si+1]
    xchg al, ah
    call ser_hex8
    xchg al, ah
    call ser_hex8
    mov  al, 10
    call ser_putc
    pop  dx

    ; Powrot do wolajacego z AX=DX=0, zdejmujac N bajtow argumentow (pascal)
    xor  ch, ch
    mov  cl, [es:si+3]       ; N
    mov  si, cx
    mov  ax, [bp+8]          ; przesun adres powrotu nad argumenty
    mov  cx, [bp+10]         ; (najpierw odczyt obu - obszary moga sie nakladac)
    mov  [bp+si+8], ax
    mov  [bp+si+10], cx
    mov  dx, si              ; DX = N (wartosc zwracana i tak zerowana nizej)
    pop  es
    pop  si
    pop  cx
    pop  bx
    pop  ax
    pop  bp                  ; SP -> ramka IRET (IP, CS, FLAGS)
    add  sp, 6
    add  sp, dx              ; SP -> przesuniety adres powrotu
    xor  ax, ax
    xor  dx, dx
    sti
    retf

; ser_hex8: AL -> 2 cyfry hex na COM1. Zachowuje wszystkie rejestry.
ser_hex8:
    push ax
    push ax
    shr  al, 4
    call .nib
    pop  ax
    and  al, 0x0F
    call .nib
    pop  ax
    ret
.nib:
    add  al, '0'
    cmp  al, '9'
    jbe  .nib_ok
    add  al, 7
.nib_ok:
    jmp  ser_putc

; ser_putc: AL -> COM1. Zachowuje wszystkie rejestry poza flagami.
ser_putc:
    push dx
    push ax
    mov  dx, 0x3FD
.sp_wait:
    in   al, dx
    test al, 0x20
    jz   .sp_wait
    pop  ax
    mov  dx, 0x3F8
    out  dx, al
    pop  dx
    ret

; =====================================================================
; Przerwania programowe z PM (INT 21h/25h/2Fh/10h... wolane przez apki).
; Stub wektora N (6 bajtow): push ax / mov al,N / jmp softint_common.
; softint_common buduje ramke i wola dyspozytor KERNEL (KCB_INT_CB):
;   AX=wektor, DX:BX = SS:SP ramki
;   ramka: BX CX DX SI DI ES DS BP AX IP CS FLAGS (rosnaco od SP)
;   wynik AX: 0 = IRET (FLAGS z ramki), 1 = RETF z FLAGS zostawionymi na stosie
;   (semantyka INT 25h/26h).
; Bez dyspozytora (KERNEL jeszcze nie zainicjalizowany): INT 21h -> stary
; int21_handler, pozostale -> CF=1.
; =====================================================================
softint_stubs:
%assign vec 0
%rep 256
    push ax
    mov  al, vec
    jmp  strict near softint_common
%assign vec vec+1
%endrep

softint_common:
    push bp
    mov  bp, sp
    push ds
    push es
    push di
    push si
    push dx
    push cx
    push bx

    cmp  al, 0x21
    jne  .not_exit
    cmp  byte [bp+3], 0x4C          ; AH oryginalnego AX
    je   .to_int21
.not_exit:
    mov  bx, SEL_KCB
    mov  es, bx
    cmp  word [es:KCB_INT_CB + 2], 0
    je   .no_cb
    xor  ah, ah
    mov  dx, ss
    mov  bx, sp
    call far [es:KCB_INT_CB]
    cmp  ax, 1
    je   .keep_flags
    pop  bx
    pop  cx
    pop  dx
    pop  si
    pop  di
    pop  es
    pop  ds
    pop  bp
    pop  ax
    iret

.keep_flags:                        ; INT 25h/26h: FLAGS zostaja na stosie
    pop  bx
    pop  cx
    pop  dx
    pop  si
    pop  di
    pop  es
    pop  ds
    pop  bp
    pop  ax
    push bp
    mov  bp, sp
    push word [bp+6]                ; FLAGS z ramki -> rejestr flag (CF, IF)
    popf
    pop  bp
    retf

.no_cb:
    cmp  al, 0x21
    je   .to_int21
    or   word [bp+8], 1             ; CF=1 w FLAGS
    pop  bx
    pop  cx
    pop  dx
    pop  si
    pop  di
    pop  es
    pop  ds
    pop  bp
    pop  ax
    iret

.to_int21:
    pop  bx
    pop  cx
    pop  dx
    pop  si
    pop  di
    pop  es
    pop  ds
    pop  bp
    pop  ax
    jmp  int21_handler

; IRQ zamaskowane / falszywe (IRQ7, IRQ15): nic nie rob
iret_stub:
    iret

; =====================================================================
; rm_int_service: wywolaj przerwanie w trybie rzeczywistym (DOS/BIOS) z PM.
; Far call z 16-bit PM; AL = numer przerwania.
; Rejestry przerwania w RMREGS na poczatku bufora RMBUF (segment RM rmi_buf_seg,
; w PM selektor SEL_RMBUF):
;   +0 AX +2 BX +4 CX +6 DX +8 SI +10 DI +12 BP +14 DS +16 ES +18 FLAGS
;   (DS/ES to segmenty RM; 0 = segment RMBUF). Po powrocie RMREGS = wynik.
; W RM: PIC w ukladzie BIOS (08h/70h), IRQ1 i IRQ12 zamaskowane (bajty klawiatury
; i myszy czekaja w kontrolerze na powrot do PM). Zachowuje rejestry PM.
; =====================================================================
rm_int_service:
    pushf
    push ds
    push es
    push fs
    push gs
    pusha
    cli
    mov  bx, SEL_DATA16
    mov  ds, bx
    mov  [rmi_int_op + 1], al
    mov  [rmi_pm_ss], ss
    mov  [rmi_pm_sp], sp
    mov  ss, bx
    mov  sp, rmi_stack_top
    mov  es, bx
    mov  fs, bx
    mov  gs, bx

    mov  al, 0x11
    out  0x20, al
    out  0xA0, al
    mov  al, 0x08
    out  0x21, al
    mov  al, 0x70
    out  0xA1, al
    mov  al, 0x04
    out  0x21, al
    mov  al, 0x02
    out  0xA1, al
    mov  al, 0x01
    out  0x21, al
    out  0xA1, al
    mov  al, [saved_pic1_mask]
    or   al, 0x02
    out  0x21, al
    mov  al, [saved_pic2_mask]
    or   al, 0x10
    out  0xA1, al

    mov  eax, cr0
    and  eax, ~1
    mov  cr0, eax
    jmp  far [rmi_rm_ptr]

rmi_rm_entry:
    mov  ax, cs
    mov  ds, ax
    mov  ss, ax
    lidt [rm_idtr]
    mov  es, [rmi_buf_seg]
    mov  ax, [es:14]
    test ax, ax
    jnz  .ds_ok
    mov  ax, [rmi_buf_seg]
.ds_ok:
    mov  [rmi_tmp_ds], ax
    mov  ax, [es:16]
    test ax, ax
    jnz  .es_ok
    mov  ax, [rmi_buf_seg]
.es_ok:
    push ax
    mov  ax, [es:0]
    mov  bx, [es:2]
    mov  cx, [es:4]
    mov  dx, [es:6]
    mov  si, [es:8]
    mov  di, [es:10]
    mov  bp, [es:12]
    pop  es
    mov  ds, [cs:rmi_tmp_ds]
    sti
rmi_int_op:
    int  0x21              ; numer przerwania patchowany na wejsciu
    cli
    pushf
    push es
    push ds
    push ax
    mov  ax, cs
    mov  ds, ax
    mov  es, [rmi_buf_seg]
    pop  ax
    mov  [es:0], ax
    mov  [es:2], bx
    mov  [es:4], cx
    mov  [es:6], dx
    mov  [es:8], si
    mov  [es:10], di
    mov  [es:12], bp
    pop  ax
    mov  [es:14], ax
    pop  ax
    mov  [es:16], ax
    pop  ax
    mov  [es:18], ax

    lidt [idtr]
    mov  eax, cr0
    or   eax, 1
    mov  cr0, eax
    jmp  SEL_CODE16:rmi_pm_entry

rmi_pm_entry:
    mov  ax, SEL_DATA16
    mov  ds, ax
    mov  al, 0x11
    out  0x20, al
    out  0xA0, al
    mov  al, PIC_BASE_M
    out  0x21, al
    mov  al, PIC_BASE_S
    out  0xA1, al
    mov  al, 0x04
    out  0x21, al
    mov  al, 0x02
    out  0xA1, al
    mov  al, 0x01
    out  0x21, al
    out  0xA1, al
    mov  al, 0xF8
    out  0x21, al
    mov  al, 0xEF
    out  0xA1, al
    mov  ss, [rmi_pm_ss]
    mov  sp, [rmi_pm_sp]
    popa
    pop  gs
    pop  fs
    pop  es
    pop  ds
    popf
    retf

rmi_rm_ptr      dw rmi_rm_entry, 0     ; CS = segment RM loadera (patch)
rmi_buf_seg     dw 0
rmi_tmp_ds      dw 0
rmi_pm_ss       dw 0
rmi_pm_sp       dw 0
rmi_stack       times 2048 db 0
rmi_stack_top:

; =====================================================================
; int3f_trampoline: called when a DLL function returns.
; Uses ES=SEL_DATA16 to access loader data (same base as CS=SEL_CODE16).
; tramp_ret_ip/cs/saved_ds are read back via CS prefix (CS is readable).
; =====================================================================
int3f_trampoline:
    ; Flagi z funkcji DLL (np. CF z DOS3Call) musza wrocic do wolajacego.
    pushf
    push ax
    push bx
    push cx
    push es

    mov  ax, SEL_DATA16
    mov  es, ax
    dec  word [es:int3f_depth]
    mov  bx, [es:int3f_depth]
    mov  ax, bx
    shl  bx, 1
    shl  ax, 2
    add  bx, ax            ; BX = depth * 6

    mov  cx, [es:int3f_stack + bx + 0]   ; DS wolajacego
    mov  ax, [es:int3f_stack + bx + 2]
    mov  [es:tramp_ret_ip], ax
    mov  ax, [es:int3f_stack + bx + 4]
    mov  [es:tramp_ret_cs], ax
    mov  [es:tramp_saved_ds], cx

    pop  es
    pop  cx
    pop  bx
    pop  ax

    ; Odtworz DS wolajacego bez psucia AX (wartosc zwracana)
    push ax
    mov  ax, [cs:tramp_saved_ds]
    mov  ds, ax
    pop  ax

    ; IF=1 przed powrotem do apki (funkcje DLL moga zostawic IF=0)
    popf
    sti
    jmp  far [cs:tramp_ret_ip]

tramp_ret_ip:   dw 0
tramp_ret_cs:   dw 0
tramp_saved_ds: dw 0

; =====================================================================
; gpf_handler: GP fault (exception 0xD).
; Drukuje "GPF IIII<RRRR\r\n" (I=fault IP, R=word below fault frame), potem HLT.
; Stack po wejsciu: [SP+0]=err [SP+2]=faultIP [SP+4]=faultCS [SP+6]=FLAGS
; =====================================================================
gpf_handler:
    push ax
    push bx
    push cx
    push dx
    ; Stack now: [SP+0]=DX [SP+2]=CX [SP+4]=BX [SP+6]=AX
    ;            [SP+8]=err [SP+10]=faultIP [SP+12]=faultCS [SP+14]=FLAGS
    ;            [SP+16]=word below fault (caller context)

    ; Print "GPF "
.g0: mov  dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .g0
    mov  dx, 0x3F8
    mov  al, 'G'
    out  dx, al
.g1: mov  dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .g1
    mov  dx, 0x3F8
    mov  al, 'P'
    out  dx, al
.g2: mov  dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .g2
    mov  dx, 0x3F8
    mov  al, 'F'
    out  dx, al
.g3: mov  dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .g3
    mov  dx, 0x3F8
    mov  al, ' '
    out  dx, al

    ; Print fault CS:IP as 4+4 hex digits
    mov  bx, sp            ; BX = current SP (after 4 pushes)
    mov  ax, [ss:bx+12]   ; fault CS
    call gpf_print_hex4

.g4cs: mov  dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .g4cs
    mov  dx, 0x3F8
    mov  al, ':'
    out  dx, al

    mov  ax, [ss:bx+10]   ; fault IP
    call gpf_print_hex4

.g4: mov  dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .g4
    mov  dx, 0x3F8
    mov  al, 'E'
    out  dx, al

.g4e: mov  dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .g4e
    mov  dx, 0x3F8
    mov  ax, [ss:bx+8]    ; error code
    call gpf_print_hex4

.g4f: mov  dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .g4f
    mov  dx, 0x3F8
    mov  al, 'S'
    out  dx, al

.g4s: mov  dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .g4s
    mov  dx, 0x3F8
    ; SP at fault = bx + 16 (4 saved regs * 2 + 4 CPU frame words * 2)
    mov  ax, bx
    add  ax, 16
    call gpf_print_hex4

.g4b: mov  dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .g4b
    mov  dx, 0x3F8
    mov  al, '<'
    out  dx, al

    ; Print word at BX+16 (below fault frame = first word below CPU pushed frame)
    mov  ax, [ss:bx+16]
    call gpf_print_hex4

    ; Print 12 more stack words (BX+18, BX+20, ...) = call chain
    push si
    push cx
    mov  si, bx
    add  si, 18
    mov  cx, 12
.gdmp:
.gsp:mov  dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .gsp
    mov  dx, 0x3F8
    mov  al, ' '
    out  dx, al
    mov  ax, [ss:si]
    call gpf_print_hex4
    add  si, 2
    loop .gdmp
    pop  cx
    pop  si

    ; Print "D" + int3f_depth
.gd0: mov  dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .gd0
    mov  dx, 0x3F8
    mov  al, 'D'
    out  dx, al
    mov  ax, SEL_DATA16
    mov  es, ax
    mov  ax, [es:int3f_depth]
    call gpf_print_hex4

.g5: mov  dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .g5
    mov  dx, 0x3F8
    mov  al, 0x0D
    out  dx, al
.g6: mov  dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .g6
    mov  dx, 0x3F8
    mov  al, 0x0A
    out  dx, al

    pop  dx
    pop  cx
    pop  bx
    pop  ax
    cli
    hlt

; =====================================================================
; Exception stubs -> exc_common -> panic_entry_32 -> panic_screen (C)
; Bez kodu bledu (CPU nie pushuje): push 0 (fake err), push exc#
; Z kodem bledu (CPU pushuje err):  push exc#
; Stos przy wejsciu do exc_common:
;   [SP+0]=exc# [SP+2]=err [SP+4]=faultIP [SP+6]=faultCS [SP+8]=FLAGS
; =====================================================================
bits 16
exc_stub_00:            ; #DE Divide Error (brak err code)
    push word 0
    push word 0x00
    jmp  exc_common
exc_stub_06:            ; #UD Invalid Opcode (brak err code)
    push word 0
    push word 0x06
    jmp  exc_common
exc_stub_08:            ; #DF Double Fault (z err code = 0)
    ; drukuj '!' natychmiast przed jakimikolwiek operacjami stosu
    push ax
    push dx
.df_w: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .df_w
    mov  al, '!'
    mov  dx, 0x3F8
    out  dx, al
    pop  dx
    pop  ax
    ; nie wywolujemy exc_common (stos moze byc zepsuty), just halt
    cli
    hlt
    jmp  $-1
exc_stub_0B:            ; #NP Segment Not Present (z err code)
    push word 0x0B
    jmp  exc_common
exc_stub_0C:            ; #SS Stack Fault (z err code)
    push word 0x0C
    jmp  exc_common
exc_stub_0D:            ; #GP General Protection (z err code)
    push word 0x0D
    jmp  exc_common
exc_stub_0E:            ; #PF Page Fault (z err code)
    push word 0x0E
    jmp  exc_common

; =====================================================================
bits 16
exc_common:
    ; Wczesny znacznik: '#' przed jakimikolwiek zmianami rejestrow/stosu
    push ax
    push dx
.ecm_w: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .ecm_w
    mov  al, '#'
    mov  dx, 0x3F8
    out  dx, al
    pop  dx
    pop  ax
    push es                  ; oryginalny ES (dla zrzutu rejestrow)
    push ax
    push bx
    push cx
    push dx
    mov  ax, SEL_DATA16      ; set ES = writable alias of code seg
    mov  es, ax
    ; Stos: [SP+0]=DX [SP+2]=CX [SP+4]=BX [SP+6]=AX [SP+8]=ES
    ;       [SP+10]=exc [SP+12]=err [SP+14]=faultIP [SP+16]=faultCS [SP+18]=FLAGS
    mov  bx, sp
    mov  ax, [ss:bx+10]
    mov  [es:local_fault_exc], ax
    mov  ax, [ss:bx+12]
    mov  [es:local_fault_err], ax
    mov  ax, [ss:bx+14]
    mov  [es:local_fault_ip], ax
    mov  ax, [ss:bx+16]
    mov  [es:local_fault_cs], ax
    mov  ax, bx
    add  ax, 20              ; SP przy wyjatku (przed pushami CPU)
    mov  [es:local_fault_sp], ax

%if DEBUG_STACK
    ; --- Zrzut rejestrow + stosu + lancucha BP do exc_dbg (czyta panic.c) ---
    ; SI, DI, BP, DS, SS sa nietkniete od chwili wyjatku.
    mov  ax, [ss:bx+6]
    mov  [es:dbg_ax], ax
    mov  ax, [ss:bx+4]
    mov  [es:dbg_bx], ax
    mov  ax, [ss:bx+2]
    mov  [es:dbg_cx], ax
    mov  ax, [ss:bx+0]
    mov  [es:dbg_dx], ax
    mov  [es:dbg_si], si
    mov  [es:dbg_di], di
    mov  [es:dbg_bp], bp
    mov  [es:dbg_ds], ds
    mov  ax, [ss:bx+8]
    mov  [es:dbg_es], ax
    mov  [es:dbg_ss], ss
    mov  ax, [ss:bx+18]
    mov  [es:dbg_flags], ax
    ; Limit SS (lsl) - czytamy stos tylko w jego granicach, inaczej #GP w handlerze
    mov  ax, ss
    lsl  cx, ax
    jz   .dbg_lim_ok
    xor  cx, cx
.dbg_lim_ok:
    mov  [es:dbg_sslim], cx
    xor  dx, dx
    mov  [es:dbg_nstack], dx
    cmp  word [es:local_fault_exc], 0x0C   ; #SS: stos niewiarygodny
    je   .dbg_frames_done
    ; Surowy zrzut: STACK_DUMP_WORDS slow od SS:faultSP w gore
    mov  si, [es:local_fault_sp]
    mov  di, dbg_stack
.dbg_sl:
    cmp  dx, STACK_DUMP_WORDS
    jae  .dbg_sd
    mov  ax, si
    inc  ax
    jz   .dbg_sd
    cmp  ax, cx
    ja   .dbg_sd
    mov  ax, [ss:si]
    mov  [es:di], ax
    add  di, 2
    inc  dx
    add  si, 2
    jnc  .dbg_sl
.dbg_sd:
    mov  [es:dbg_nstack], dx
    ; Lancuch ramek BP. Konwencja Win16: far frame = "inc bp; push bp; mov bp,sp"
    ; => zapisany BP nieparzysty oznacza ramke far (ret CS:IP), parzysty - near (ret IP).
    mov  si, [es:dbg_bp]
    mov  di, dbg_frames
    xor  dx, dx
.dbg_fl:
    cmp  dx, FRAME_DUMP_MAX
    jae  .dbg_frames_done
    cmp  si, [es:local_fault_sp]
    jb   .dbg_frames_done
    test si, 1
    jnz  .dbg_frames_done
    mov  ax, si
    add  ax, 5
    jc   .dbg_frames_done
    cmp  ax, cx
    ja   .dbg_frames_done
    mov  ax, [ss:si]         ; zapisany BP wywolujacego
    mov  [es:di], si
    mov  bx, [ss:si+2]
    mov  [es:di+2], bx       ; ret IP
    xor  bx, bx
    test ax, 1
    jz   .dbg_near
    mov  bx, [ss:si+4]       ; ret CS (far frame)
    dec  ax
.dbg_near:
    mov  [es:di+4], bx
    add  di, 6
    inc  dx
    cmp  ax, si              ; ramki musza rosnac w gore stosu
    jbe  .dbg_frames_done
    mov  si, ax
    jmp  .dbg_fl
.dbg_frames_done:
    mov  [es:dbg_nframes], dx
%endif

    ; Serial: "EX" + exc(4hex) + ":" + cs(4hex) + ":" + ip(4hex) + "\n"
    ; AX, BX, CX, DX juz na stosie (push na poczatku exc_common) - mozemy uzywac
.exc_sw0: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .exc_sw0
    mov  al, 'E'
    mov  dx, 0x3F8
    out  dx, al
.exc_sw1: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .exc_sw1
    mov  al, 'X'
    mov  dx, 0x3F8
    out  dx, al
    mov  ax, [cs:local_fault_exc]
    call gpf_print_hex4
.exc_sw2: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .exc_sw2
    mov  al, ':'
    mov  dx, 0x3F8
    out  dx, al
    mov  ax, [cs:local_fault_cs]
    call gpf_print_hex4
.exc_sw3: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .exc_sw3
    mov  al, ':'
    mov  dx, 0x3F8
    out  dx, al
    mov  ax, [cs:local_fault_ip]
    call gpf_print_hex4
.exc_sw4: mov dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .exc_sw4
    mov  al, 0x0A
    mov  dx, 0x3F8
    out  dx, al

    cli
    ; Skok do 32-bit panic_entry_32
    db   0x66, 0xEA          ; far jmp z 32-bit operandem
exc_panic_jmp_dd:
    dd   0                   ; uzupelniane w pm_call_app_: g_cs_phys + panic_entry_32
    dw   SEL_CODE32

; =====================================================================
bits 32
panic_entry_32:
    ; Jestesmy w 32-bit PM. Ustaw DS=SEL_DATASEG (base=g_cs_phys) -> lokale dostepne.
    mov  ax, SEL_DATASEG
    mov  ds, ax
    mov  ax, SEL_DATA32
    mov  ss, ax
    mov  esp, 0x0009F000     ; bezpieczny flat stos ponizej 640KB

    ; Wrzuc argumenty cdecl (prawo do lewa): dbg, sp, err, ip, cs, exc, font_a, lfb
%if DEBUG_STACK
    mov  eax, [local_cs_phys]
    add  eax, exc_dbg        ; fizyczny adres rekordu zrzutu
%else
    xor  eax, eax
%endif
    push eax
    movzx eax, word [local_fault_sp]
    push eax
    movzx eax, word [local_fault_err]
    push eax
    movzx eax, word [local_fault_ip]
    push eax
    movzx eax, word [local_fault_cs]
    push eax
    movzx eax, word [local_fault_exc]
    push eax
    mov  eax, [local_font_phys]
    push eax
    mov  eax, [local_lfb_phys]
    push eax

    ; Oblicz fizyczny adres panic_screen (= local_cs_phys + offset panic_bin_start)
    mov  ecx, [local_cs_phys]
    add  ecx, panic_bin_start

    ; Przelacz DS na flat (panic_screen uzywa wskaznikow fizycznych)
    mov  ax, SEL_DATA32
    mov  ds, ax
    mov  es, ax

    call ecx                 ; panic_screen(lfb, font_a, exc, cs, ip, err, sp)
.halt:
    cli
    hlt
    jmp  .halt

; =====================================================================
bits 32
panic_bin_start:
    incbin "panic.bin"

; =====================================================================
bits 16
; gpf_print_hex4: AX -> 4 hex digits to COM1; destroys AX,BX,CX
gpf_print_hex4:
    push cx
    mov  cx, 4
    ; rotate AX so most-significant nibble is in low bits first
    rol  ax, 4
.phl: push ax
    and  al, 0x0F
    cmp  al, 10
    jl   .phd
    add  al, 'A' - 10
    jmp  .phw
.phd:add  al, '0'
.phw:xchg bx, ax
.phs:mov  dx, 0x3FD
    in   al, dx
    test al, 0x20
    jz   .phs
    mov  dx, 0x3F8
    mov  al, bl
    out  dx, al
    pop  ax
    rol  ax, 4
    loop .phl
    pop  cx
    ret

; =====================================================================
; irq0_handler: IRQ0 (PIT, 100 Hz) w PM. Wpis w IDT[0x20] (po remapie PIC).
; Inkrementuje tick_ms (DWORD) w KCB o 10ms (1000ms / 100Hz).
; Wyslij EOI do master PIC (0x20).
; =====================================================================
KCB_TICK_OFF equ 28

irq0_handler:
    push ax
    push bx
    push ds
    mov  ax, SEL_KCB
    mov  ds, ax
    xor  bx, bx
    add  word [bx + KCB_TICK_OFF],     55  ; low word += 55ms (18.2 Hz: 1000/18.2 ~ 55ms)
    adc  word [bx + KCB_TICK_OFF + 2],  0  ; carry do high word
    mov  al, 0x20               ; EOI do master PIC
    out  0x20, al
    pop  ds
    pop  bx
    pop  ax
    iret

; =====================================================================
; irq1_handler: IRQ1 (klawiatura) w PM. Wpis wywolywany z int21_handler
; gdy PIC ISR bit1=1 (hardware keyboard interrupt).
; Wywoluje irq1_c_ (keyboard.c) do odczytu scan code i zapisu VK do KCB.
; Wysyla EOI do master PIC.
; Zapisuje/przywraca: AX, BX, CX, DX, ES (wszystko co moze zepsuc irq1_c_).
; =====================================================================
irq1_handler:
    push ax
    push bx
    push cx
    push dx
    push es
    call irq1_c_        ; C: czytaj scan code 0x60, przelicz VK, zapisz do KCB
    pop  es
    pop  dx
    pop  cx
    pop  bx
    pop  ax
    mov  al, 0x20       ; EOI do master PIC
    out  0x20, al
    iret

; =====================================================================
; irq12_handler: IRQ12 (mysz PS/2) w PM. Wpis w IDT[0x2C] (slave PIC).
; Wywoluje mouse_c_ (mouse.c) do odczytu bajtu z 0x60 i aktualizacji KCB.
; Wysyla EOI do slave PIC (0xA0) i master PIC (0x20).
; Zapisuje/przywraca: AX, BX, CX, DX, ES (wszystko co moze zepsuc mouse_c_).
; =====================================================================
irq12_handler:
    push ax
    push bx
    push cx
    push dx
    push es
    call mouse_c_       ; C: czytaj bajt 0x60, aktualizuj pakiet PS/2, zapisz do KCB
    pop  es
    pop  dx
    pop  cx
    pop  bx
    pop  ax
    mov  al, 0x20       ; EOI do slave PIC
    out  0xA0, al
    mov  al, 0x20       ; EOI do master PIC
    out  0x20, al
    iret

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
    ; Sprawdz czy to hardware IRQ1 (klawiatura) czy software INT 21h (DOS call).
    ; Odczytaj PIC In-Service Register: jezeli bit 1 ustawiony -> IRQ1 hardware.
    push ax
    push dx
    mov  dx, 0x20
    mov  al, 0x0B        ; komenda: czytaj ISR
    out  dx, al
    in   al, dx          ; AL = PIC ISR
    test al, 0x02        ; bit 1 = IRQ1
    pop  dx
    pop  ax
    jnz  irq1_handler    ; hardware keyboard -> obsluz i IRET bez debug output

    ; software INT 21h (DOS call z aplikacji)
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
    ; Odwin stos do stanu sprzed "call far [cs:call_ptr]" w pm16_call_app
    ; (IRET-frame INT 21h i ramki apki sa porzucane) i wroc do trybu rzeczywistego.
    cli
    mov  dx, SEL_DATA16
    mov  ds, dx
    mov  ss, [app_ret_ss]
    mov  sp, [app_ret_sp]
    add  sp, 2           ; zdejmij "push ds" sprzed zapisu SS:SP
    jmp  pm16_app_returned

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
local_entry_cs  dw 0
local_app_ds    dw 0
local_init_sp   dw 0xFFFE
local_has_data  dw 0
call_ptr        dw 0, 0
init_ptr        dw 0, 0
local_ndllinit  dw 0
local_dllinit   times MAX_DLLINIT * 8 db 0
local_lfb_phys  dd 0    ; kopia g_lfb_phys (dla pm32_entry w PM gdzie DS=SEL_DATASEG=code seg)
local_font_phys dd 0    ; kopia g_font_phys
local_ext_mem_kb dd 0   ; kopia g_ext_mem_kb (KB pamieci rozszerzonej)
local_mem_str   times 64 db 0  ; kopia g_mem_str (sformatowany string RAM)
rm_jmp          dw rm_real
                dw 0

int3f_depth     dw 0
app_ret_ss      dw 0    ; SS:SP przed wywolaniem entry apki (dla INT 21h/4Ch)
app_ret_sp      dw 0
saved_pic1_mask db 0    ; maski PIC trybu rzeczywistego
saved_pic2_mask db 0
rm_idtr         dw 0x03FF
                dd 0
int3f_stack     times INT3F_MAX_DEPTH * 6  db 0

; --- Exception panic screen locals ---
local_cs_phys   dd 0    ; kopia g_cs_phys (dla panic_entry_32)
local_fault_exc dw 0    ; numer wyjatku
local_fault_err dw 0    ; kod bledu
local_fault_ip  dw 0    ; IP w chwili wyjatku
local_fault_cs  dw 0    ; CS w chwili wyjatku
local_fault_sp  dw 0    ; SP w chwili wyjatku (po CPU push + stubpush)

%if DEBUG_STACK
; Rekord zrzutu wyjatku - uklad musi zgadzac sie z ExcDbg w panic.c
exc_dbg:
dbg_ax          dw 0
dbg_bx          dw 0
dbg_cx          dw 0
dbg_dx          dw 0
dbg_si          dw 0
dbg_di          dw 0
dbg_bp          dw 0
dbg_ds          dw 0
dbg_es          dw 0
dbg_ss          dw 0
dbg_flags       dw 0
dbg_sslim       dw 0    ; limit SS z lsl (0 = nieznany)
dbg_nstack      dw 0    ; ile slow w dbg_stack
dbg_nframes     dw 0    ; ile wpisow w dbg_frames
dbg_stack       times STACK_DUMP_WORDS dw 0
dbg_frames      times FRAME_DUMP_MAX * 3 dw 0   ; {bp, ret_ip, ret_cs(0=near)}
%endif

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

gdt_kcb:                    ; 0x98 SEL_KCB (16-bit data, base=g_kcb_phys, limit=2047)
    dw 0x07FF, 0x0000
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

gdt_bitmaps:                ; 0x128 SEL_BITMAPS (16-bit data, base=g_bitmaps_phys, limit=64KB-1)
    ; Bufor 86 sprite'ow 4bpp SKI.EXE: [count(2)][pad(2)][offsets[86]*2][dane DIB ~52KB]
    ; Dostep: MK_FP(0x128, off) = bajt 'off' w buforze bitmap.
    ; Patchowany w patch_gdt; access byte ustawiony w patch_gdt (nie tutaj).
    dw 0xFFFF, 0x0000
    db 0x00, 0x00, 00000000b, 0x00  ; access=0 (not-present) az patch_gdt go ustawi

gdt_dyn:                    ; 0x130 - 127 dynamicznych slotow dla GlobalAlloc
    times GDYN_COUNT dq 0   ; null descriptors; kernel.c zapelnia przez SEL_GDT_ACCESS

gdt_heap:                   ; 0x528 SEL_HEAP (data, base=0x100000, limit patchowany)
    ; XMS GlobalHeap: dostep do pamieci rozszerzonej >1MB z 16-bit PM kodu.
    ; D/B=1 (0x40 w bajcie 6) = big/32-bit segment.
    ; Baza i limit patchowane przez patch_gdt_c (pm_helpers.c).
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 01000000b, 0x00  ; P=1 DPL=0 S=1 data-RW, D/B=1

gdt_ext:                    ; 0x530.. rozszerzenie (patrz GDT_TOTAL_SIZE)
    times (GDT_TOTAL_SIZE - 0x530) db 0

gdtr:
    dw GDT_LIMIT
    dd 0

idtr:
    dw 256 * 8 - 1
    dd 0
