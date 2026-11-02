/* Basalt bare Multiboot2 bootloader
 * QEMU:  qemu-system-x86_64 -kernel kernel.elf
 * GRUB:  multiboot2 /kernel.elf
 */
    .intel_syntax noprefix
    .global _start
    .extern main
    .extern basalt_set_args

    /* ---- Multiboot2 header (must be in first 32K, 8-aligned) ---- */
    .section .multiboot
    .align 8
mb2_header_start:
    .long 0xE85250D6                 /* magic */
    .long 0                          /* arch i386 */
    .long mb2_header_end - mb2_header_start
    .long -(0xE85250D6 + 0 + (mb2_header_end - mb2_header_start))
    /* end tag */
    .align 8
    .word 0                          /* type */
    .word 0                          /* flags */
    .long 8                          /* size */
mb2_header_end:

    .section .bss
    .align 16
boot_stack:
    .space 65536
boot_stack_top:

    .section .text
    .code64
    .align 16
_start:
    /* GRUB/QEMU may enter in 32-bit; for ELF64 -kernel QEMU often jumps in long mode.
       Minimal path: set stack, zero argc, call main, halt. */
    lea rsp, [rip+boot_stack_top]
    and rsp, -16
    xor rbp, rbp

    /* clear direction, interrupts off until kernel enables */
    cld
    cli

    xor edi, edi
    xor esi, esi
    call basalt_set_args

    call main

    /* return code in rax → hang with code in VGA corner */
    mov rdi, rax
    /* fall into halt */
.hang:
    cli
    hlt
    jmp .hang
