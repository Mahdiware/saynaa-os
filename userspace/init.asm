section .text
align 4

extern main
global _start
_start:
    mov eax, [esp]      ; argc
    mov ebx, [esp + 4]  ; argv
    push ebx
    push eax
    call main
    mov eax, 1
    int 0x30
