/* boot.s – Multiboot header, set up stack, call kernel_main */
.set MB_MAGIC,    0x1BADB002
.set MB_FLAGS,    (1<<0) | (1<<1) | (1<<2)   /* align, meminfo, vbe */
.set MB_CHECKSUM, -(MB_MAGIC + MB_FLAGS)

.section .multiboot
.align 4
.long MB_MAGIC
.long MB_FLAGS
.long MB_CHECKSUM
/* header_addr, load_addr, load_end_addr, bss_end_addr, entry_addr */
.long 0
.long 0
.long 0
.long 0
.long 0
/* video request: 640x480, 8 bits */
.long 1       /* mode_type = graphics */
.long 640
.long 480
.long 8

.section .text
.globl _start
.type _start, @function
_start:
    /* Set up stack */
    movl $stack_top, %esp

    /* Reset EFLAGS */
    pushl $0
    popf

    /* Save Multiboot info (GRUB passes magic in EAX, info struct in EBX) */
    pushl %ebx
    pushl %eax

    call kernel_main

    /* If kernel_main returns, halt */
    cli
    hlt
.Lhang:
    jmp .Lhang

.section .bss
.align 16
stack_bottom:
    .skip 16384   /* 16 KB stack */
stack_top:
