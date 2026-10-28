; kernel_wrap.asm - naglowek NE DLL dla KERNEL.EXE stub
;
; Layout:
;   offset   0 : MZ stub
;   offset  64 : NE header
;   offset 128 : Segment Table (1 wpis - segment kodu)
;   offset 136 : Entry Table (1 eksport: OutputDebugString, ordinal 1)
;   offset 512 : Segment 1 - kernel_code.com (incbin)
;
; Kompilacja: nasm -f bin kernel_wrap.asm -o kernel.exe
;             (wymaga ze kernel_code.com istnieje)

org 0

; =====================================================================
;  MZ HEADER (offset 0)
; =====================================================================
    dw 0x5A4D           ; e_magic = 'MZ'
    dw 0x0040           ; e_cblp
    dw 0x0001           ; e_cp
    dw 0x0000           ; e_crlc
    dw 0x0004           ; e_cparhdr
    dw 0x0000           ; e_minalloc
    dw 0xFFFF           ; e_maxalloc
    dw 0x0000           ; e_ss
    dw 0x0080           ; e_sp
    dw 0x0000           ; e_csum
    dw 0x0000           ; e_ip
    dw 0x0000           ; e_cs
    dw 0x0040           ; e_lfarlc
    dw 0x0000           ; e_ovno
    times 4 dw 0        ; e_res
    dw 0x0000           ; e_oemid
    dw 0x0000           ; e_oeminfo
    times 10 dw 0       ; e_res2
    dd 64               ; e_lfanew = 64

; =====================================================================
;  NE HEADER (offset 64)
; =====================================================================
ne_header:
    dw 0x454E           ; ne_magic = 'NE'
    db 5                ; ne_ver
    db 10               ; ne_rev
    dw (entry_table - ne_header)        ; ne_enttab
    dw (entry_table_end - entry_table)  ; ne_cbenttab
    dd 0                ; ne_crc
    dw 0x8000           ; ne_flags: LIBRARY (bit 15) - to jest DLL
    dw 0                ; ne_autodata = 0 (brak segmentu danych)
    dw 0                ; ne_heap
    dw 0                ; ne_stack
    dw 0                ; ne_ip (DLL - brak entry point)
    dw 0                ; ne_cs = 0 (DLL)
    dw 0                ; ne_sp
    dw 0                ; ne_ss
    dw 1                ; ne_cseg = 1 segment
    dw 1                ; ne_cmod = 1 (modul KERNEL sam siebie)
    dw 0                ; ne_cbnrestab
    dw (seg_table - ne_header)          ; ne_segtab
    dw (entry_table - ne_header)        ; ne_rsrctab (brak zasobow)
    dw (name_table - ne_header)         ; ne_restab
    dw (mod_table - ne_header)          ; ne_modtab
    dw (imp_table - ne_header)          ; ne_imptab
    dd 0                ; ne_nrestab
    dw 0                ; ne_cmovent
    dw 9                ; ne_align = 9 (sektor = 512 B)
    dw 0                ; ne_cres
    db 0x02             ; ne_exetyp: Windows
    db 0x00             ; ne_addflags
    dw 0                ; ne_gangstart
    dw 0                ; ne_ganglength
    dw 0                ; ne_swaparea
    dw 0x030A           ; ne_expver: Windows 3.10

; =====================================================================
;  SEGMENT TABLE (offset 128)
; =====================================================================
seg_table:
    dw 1                            ; ns_sector = 1 (offset 512 w pliku)
    dw (code_end - code_start)      ; ns_cbseg: rozmiar kodu
    dw 0x0000                       ; ns_flags: CODE (bez RELOCS)
    dw (code_end - code_start)      ; ns_minalloc

; =====================================================================
;  ENTRY TABLE
;  Ordinal 1: OutputDebugString na offset 0 segmentu 1
; =====================================================================
entry_table:
    db 1        ; 1 entry w tym bundlu
    db 1        ; segment 1 (fixed segment)
    db 0x01     ; flags: exported
    dw 0x0000   ; offset = 0 (OutputDebugString jest na poczatku kodu)
entry_table_end:
    db 0        ; koniec entry table

; =====================================================================
;  RESIDENT NAMES TABLE
; =====================================================================
name_table:
    db 6, "KERNEL"      ; nazwa modulu
    dw 0                ; ordinal 0 (module name)
    db 0                ; koniec tabeli

; =====================================================================
;  MODULE REFERENCE TABLE (puste - KERNEL nie importuje)
; =====================================================================
mod_table:

; =====================================================================
;  IMPORTED NAMES TABLE
; =====================================================================
imp_table:
    db 0

; =====================================================================
;  Padding do offset 512
; =====================================================================
times (512 - ($-$$)) db 0

; =====================================================================
;  Segment 1: flat binary OutputDebugString (kernel_code.com)
; =====================================================================
code_start:
    incbin "kernel_code.com"
code_end:
