; gstubs.asm - symbole wymagane przez wlink system windows_dll (bez clib)
bits 16
segment _DATA public class=DATA use16
global ___win_alloc_flags
global ___win_realloc_flags
___win_alloc_flags:   dw 0
___win_realloc_flags: dw 0
segment _TEXT public class=CODE use16
global __InitRtns
global __FiniRtns
; wolane z libentry.obj near callem (E8) - musi byc ret, nie retf
__InitRtns:
    ret
__FiniRtns:
    ret
