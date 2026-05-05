; hello.com - prosty program DOS (format COM)
; Kompilacja: nasm -f bin hello.asm -o hello.com

org 0x100           ; COM zaczyna sie od offsetu 0x100

section .text
start:
    mov  dx, msg    ; adres stringa
    mov  ah, 0x09   ; DOS: wypisz string zakonczony '$'
    int  0x21

    mov  ax, 0x4C00 ; DOS: zakoncz program (kod wyjscia 0)
    int  0x21

section .data
msg db "Hello, DOS!", 0x0D, 0x0A, "$"
