; ne_c_wrap.asm - naglowek NE dla aplikacji C
;
; Layout identyczny jak ne_app.asm:
;   offset   0 : MZ header
;   offset  64 : NE header
;   offset 128 : Segment Table
;   offset 512 : Segment 1 - zawartosc ne_c_code.com (skompilowane C)
;
; Kompilacja: nasm -f bin ne_c_wrap.asm -o ne_c_app.exe
; (wymaga ze ne_c_code.com istnieje - zbudowany przez wlink)

org 0

; =====================================================================
;  MZ HEADER (offset 0)
; =====================================================================
    dw 0x5A4D           ; e_magic  = 'MZ'
    dw 0x0040           ; e_cblp
    dw 0x0001           ; e_cp
    dw 0x0000           ; e_crlc
    dw 0x0004           ; e_cparhdr = 4 paragrafy = 64 B
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
    dw (entry_table - ne_header)
    dw (entry_table_end - entry_table)
    dd 0                ; ne_crc
    dw 0x0100           ; ne_flags: single data, Windows app
    dw 0                ; ne_autodata
    dw 0                ; ne_heap
    dw 0                ; ne_stack
    dw 0                ; ne_ip = 0  (call SEL_APP_CODE:0 = poczatek app_entry)
    dw 1                ; ne_cs = segment #1
    dw 0                ; ne_sp
    dw 0                ; ne_ss
    dw 1                ; ne_cseg = 1 segment
    dw 0                ; ne_cmod
    dw 0                ; ne_cbnrestab
    dw (seg_table - ne_header)
    dw (entry_table - ne_header)    ; ne_rsrctab
    dw (name_table - ne_header)     ; ne_restab
    dw (name_table - ne_header)     ; ne_modtab
    dw (name_table - ne_header)     ; ne_imptab
    dd 0                ; ne_nrestab
    dw 0                ; ne_cmovent
    dw 9                ; ne_align = 9  (sektor = 512 B)
    dw 0                ; ne_cres
    db 0x02             ; ne_exetyp: Windows
    db 0x00             ; ne_addflags
    dw 0                ; ne_gangstart
    dw 0                ; ne_ganglength
    dw 0                ; ne_swaparea
    dw 0x030A           ; ne_expver: Windows 3.10

; =====================================================================
;  SEGMENT TABLE
; =====================================================================
seg_table:
    dw 1                            ; ns_sector = 1  (offset 512 w pliku)
    dw (code_end - code_start)      ; ns_cbseg: rozmiar
    dw 0x0000                       ; ns_flags: kod
    dw (code_end - code_start)      ; ns_minalloc

; =====================================================================
;  ENTRY TABLE / NAME TABLE
; =====================================================================
entry_table:
    db 0
entry_table_end:

name_table:
    db 7, "NECAPP1"
    dw 0
    db 0

; =====================================================================
;  Padding do offset 512
; =====================================================================
times (512 - ($-$$)) db 0

; =====================================================================
;  Segment 1: flat binary z wlink (ne_c_code.com = skompilowane C)
; =====================================================================
code_start:
    incbin "ne_c_code.com"
code_end:
