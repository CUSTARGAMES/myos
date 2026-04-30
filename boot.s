section .multiboot
align 4

MAGIC    equ 0x1BADB002
FLAGS    equ (1 << 0) | (1 << 1) | (1 << 2)
CHECKSUM equ -(MAGIC + FLAGS)

dd MAGIC
dd FLAGS
dd CHECKSUM
dd 0
dd 0
dd 0
dd 0
dd 0
dd 1
dd 640
dd 480
dd 8

section .text
global _start
_start:
    mov esp, stack_top
    push 0
    popf
    push ebx
    push eax
    extern kernel_main
    call kernel_main
    cli
halt:
    hlt
    jmp halt

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:
