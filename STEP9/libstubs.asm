; libstubs.asm - exact symbol stubs for wlink windows_dll runtime
; Defines: ___win_alloc_flags, ___win_realloc_flags, __InitRtns, __FiniRtns
; Must be assembled as OMF (NASM -f obj)

bits 16

segment _DATA public class=DATA use16

global ___win_alloc_flags
global ___win_realloc_flags

___win_alloc_flags:
    dw 0

___win_realloc_flags:
    dw 0

segment _TEXT public class=CODE use16

global __InitRtns
global __FiniRtns

__InitRtns:
    ret

__FiniRtns:
    ret
