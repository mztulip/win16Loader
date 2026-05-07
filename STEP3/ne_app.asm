; ne_app.asm - reczne zbudowany minimalny plik NE (New Executable)
;              16-bit kod, jak prawdziwa aplikacja Win16/Win3.1
;
; Layout pliku:
;   offset   0 : MZ header (64 B)
;   offset  64 : NE header (64 B)
;   offset 128 : Segment Table (1 wpis x 8 B)
;   offset 136 : Entry Table
;   offset 144 : Resident Name Table
;   offset 512 : Segment 1 - 16-bit kod (wywolywany far call z loadera)
;
; ne_align = 9  ->  rozmiar sektora = 2^9 = 512 B
; Kompilacja: nasm -f bin ne_app.asm -o ne_app.exe

org 0

; =====================================================================
;  MZ HEADER (offset 0)
; =====================================================================
    dw 0x5A4D           ; e_magic  = 'MZ'
    dw 0x0040           ; e_cblp   = 64 (bajty w ostatniej stronie)
    dw 0x0001           ; e_cp     = 1 strona
    dw 0x0000           ; e_crlc   = 0 relokacji
    dw 0x0004           ; e_cparhdr= 4 paragrafy (64 B) naglowek
    dw 0x0000           ; e_minalloc
    dw 0xFFFF           ; e_maxalloc
    dw 0x0000           ; e_ss
    dw 0x0080           ; e_sp
    dw 0x0000           ; e_csum
    dw 0x0000           ; e_ip
    dw 0x0000           ; e_cs
    dw 0x0040           ; e_lfarlc = 64 (relocation table offset)
    dw 0x0000           ; e_ovno
    times 4 dw 0        ; e_res
    dw 0x0000           ; e_oemid
    dw 0x0000           ; e_oeminfo
    times 10 dw 0       ; e_res2
    dd 64               ; e_lfanew = 64 (offset NE headera)

; =====================================================================
;  NE HEADER (offset 64)
; =====================================================================
ne_header:
    dw 0x454E           ; ne_magic      = 'NE'
    db 5                ; ne_ver        = 5 (linker version)
    db 10               ; ne_rev        = 10
    dw (entry_table - ne_header)        ; ne_enttab  offset entry table
    dw (entry_table_end - entry_table)  ; ne_cbenttab
    dd 0                ; ne_crc
    dw 0x0100           ; ne_flags: single data, Windows app
    dw 0                ; ne_autodata: brak segmentu danych
    dw 0                ; ne_heap
    dw 0                ; ne_stack
    dw 0                ; ne_ip:   entry IP = 0
    dw 1                ; ne_cs:   entry CS = segment #1
    dw 0                ; ne_sp
    dw 0                ; ne_ss
    dw 1                ; ne_cseg: 1 segment
    dw 0                ; ne_cmod
    dw 0                ; ne_cbnrestab
    dw (seg_table - ne_header)          ; ne_segtab
    dw (entry_table - ne_header)        ; ne_rsrctab (brak = wskazuje na enttab)
    dw (name_table - ne_header)         ; ne_restab
    dw (name_table - ne_header)         ; ne_modtab
    dw (name_table - ne_header)         ; ne_imptab
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
;  SEGMENT TABLE (offset 128)
;  Wpis: ns_sector(2), ns_cbseg(2), ns_flags(2), ns_minalloc(2)
; =====================================================================
seg_table:
    dw 1                            ; ns_sector: offset = 1 * 512 = 512
    dw (code_end - code_start)      ; ns_cbseg:  rozmiar kodu
    dw 0x0000                       ; ns_flags:  kod, fixed, not preload
    dw (code_end - code_start)      ; ns_minalloc

; =====================================================================
;  ENTRY TABLE
; =====================================================================
entry_table:
    db 0                ; pusta (entry point podany przez ne_cs:ne_ip)
entry_table_end:

; =====================================================================
;  RESIDENT NAME TABLE
; =====================================================================
name_table:
    db 6, "NEAPP1"      ; nazwa modulu (length-prefixed)
    dw 0                ; ordinal 0
    db 0                ; koniec

; =====================================================================
;  Padding do 512 (poczatek sektora 1)
; =====================================================================
times (512 - ($-$$)) db 0

; =====================================================================
;  SEGMENT 1: 16-bit kod (offset 512 w pliku)
;
;  Wywolanie przez loader: far call SEL_APP_CODE:0
;  Przed wywolaniem loader ustawia:
;    ES = SEL_VGA (base=0xB8000, pisanie na ekran)
;    DS = SEL_DATA16 lub SEL_APP_CODE (ustawiamy sami przez push cs/pop ds)
;  Powrot: retf (far return do loadera)
; =====================================================================
code_start:
bits 16

    push si
    push di
    push ax

    ; Ustaw DS = CS (zeby moc czytac dane z segmentu kodu)
    push cs
    pop  ds

    ; Wypisz komunikat na VGA przez ES (base=0xB8000, limit=4000)
    ; Pozycja: linia 13, kolumna 13  ->  offset = (13*80 + 13) * 2 = 2106
    mov  di, (13 * 80 + 13) * 2
    mov  si, msg - code_start   ; offset WZGLEDNY w segmencie (nie absolutny w pliku!)
    mov  ah, 0x4E           ; atrybut: zolty na czerwonym
.loop:
    lodsb                   ; al = DS:[SI++]  (DS=CS)
    test al, al
    jz   .done
    stosw                   ; ES:[DI++] = AX  (ES=VGA selector)
    jmp  .loop
.done:

    pop  ax
    pop  di
    pop  si

    retf                    ; far return do loadera

msg:
    db " *** HELLO FROM 16-BIT NE APP! WIN3.1 STYLE! *** ", 0

code_end:
