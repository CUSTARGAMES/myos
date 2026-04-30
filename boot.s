section .multiboot
align 4

MAGIC    equ 0x1BADB002
FLAGS    equ (1 << 0) | (1 << 1)
CHECKSUM equ -(MAGIC + FLAGS)

dd MAGIC
dd FLAGS
dd CHECKSUM

section .text
global _start
_start:
    mov esp, stack_top
    
    ; Clear direction flag
    cld
    
    ; Save multiboot info
    push ebx
    push eax
    
    extern kernel_main
    call kernel_main
    
    cli
.halt:
    hlt
    jmp .halt

section .bss
align 16
stack_bottom:
    resb 32768
stack_top:
