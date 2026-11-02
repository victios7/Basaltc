/* Basalt bare-metal runtime — freestanding x86-64
 * No libc, no OS syscalls. VGA text (0xB8000) + COM1 (0x3F8) + static heap.
 * Entry: _start → main → halt
 */
    .intel_syntax noprefix
    .global main
    .global basalt_print_int
    .global basalt_print_str
    .global basalt_print_nl
    .global basalt_panic
    .global basalt_alloc
    .global basalt_free
    .global basalt_strlen
    .global basalt_char_at
    .global basalt_strcmp
    .global basalt_substr
    .global basalt_str_concat
    .global basalt_itoa
    .global basalt_exit
    .global basalt_set_args
    .global basalt_argc
    .global basalt_argv
    .global basalt_system
    .global basalt_read_file
    .global basalt_write_file
    .global basalt_file_size

    .section .bss
    .align 16
heap_space:
    .space 1024*1024          /* 1 MiB bump heap */
heap_ptr:
    .quad 0
vga_pos:
    .quad 0                   /* char index 0..80*25-1 */
itoa_buf:
    .space 32
substr_buf:
    .space 4096
concat_buf:
    .space 8192
    .space 8192

    .section .text
    .align 16

/* ---- tiny helpers: outb / inb ---- */
/* outb dx=port, al=val — use as: mov dx,port; mov al,val; out dx,al */

/* serial COM1 init (optional) */
serial_init:
    mov dx, 0x3F8+1
    mov al, 0x00
    out dx, al                /* disable interrupts */
    mov dx, 0x3F8+3
    mov al, 0x80
    out dx, al                /* DLAB on */
    mov dx, 0x3F8
    mov al, 0x03
    out dx, al                /* baud lo */
    mov dx, 0x3F8+1
    mov al, 0x00
    out dx, al
    mov dx, 0x3F8+3
    mov al, 0x03
    out dx, al                /* 8N1 */
    mov dx, 0x3F8+2
    mov al, 0xC7
    out dx, al
    mov dx, 0x3F8+4
    mov al, 0x0B
    out dx, al
    ret

serial_putchar:               /* al = char */
    push rdx
    push rax
    mov dx, 0x3F8+5
1:
    in al, dx
    test al, 0x20
    jz 1b
    pop rax
    mov dx, 0x3F8
    out dx, al
    pop rdx
    ret

/* VGA putchar: al = char, attribute 0x0F */
vga_putchar:
    push rax
    push rbx
    push rcx
    push rdx
    cmp al, 10                /* newline */
    je .vga_nl
    mov rbx, [rip+vga_pos]
    cmp rbx, 80*25
    jb 1f
    xor rbx, rbx              /* wrap */
1:
    mov rcx, 0xB8000
    mov rdx, rbx
    shl rdx, 1
    add rcx, rdx
    mov ah, 0x0F
    mov [rcx], ax
    inc rbx
    mov [rip+vga_pos], rbx
    jmp .vga_done
.vga_nl:
    mov rbx, [rip+vga_pos]
    mov rax, rbx
    xor rdx, rdx
    mov rcx, 80
    div rcx
    inc rax
    imul rax, 80
    cmp rax, 80*25
    jb 2f
    xor rax, rax
2:
    mov [rip+vga_pos], rax
.vga_done:
    pop rdx
    pop rcx
    pop rbx
    pop rax
    ret

/* putchar both serial + VGA */
bare_putchar:                 /* al = char */
    push rax
    call vga_putchar
    pop rax
    call serial_putchar
    ret

/* _start lives in bootloader.s */

basalt_exit:
    /* rdi = code — show on VGA then halt */
    cli
.hlt:
    hlt
    jmp .hlt

basalt_set_args:
    ret

basalt_argc:
    xor rax, rax
    ret

basalt_argv:
    xor rax, rax
    ret

basalt_system:
    mov rax, -1
    ret

basalt_read_file:
    xor rax, rax
    ret

basalt_write_file:
    mov rax, -1
    ret

basalt_file_size:
    mov rax, -1
    ret

/* print_str: rdi = const char* */
basalt_print_str:
    push rbx
    mov rbx, rdi
    test rbx, rbx
    jz .ps_done
.ps_loop:
    mov al, [rbx]
    test al, al
    jz .ps_done
    call bare_putchar
    inc rbx
    jmp .ps_loop
.ps_done:
    pop rbx
    ret

basalt_print_nl:
    mov al, 10
    call bare_putchar
    ret

/* print_int: rdi = int64 */
basalt_print_int:
    push rbx
    push r12
    mov r12, rdi
    test r12, r12
    jns .pi_pos
    mov al, '-'
    call bare_putchar
    neg r12
.pi_pos:
    lea rbx, [rip+itoa_buf+31]
    mov byte ptr [rbx], 0
    mov rax, r12
    mov rcx, 10
    /* at least one digit */
    test rax, rax
    jnz .pi_div
    dec rbx
    mov byte ptr [rbx], '0'
    jmp .pi_out
.pi_div:
    xor rdx, rdx
    div rcx
    add dl, '0'
    dec rbx
    mov [rbx], dl
    test rax, rax
    jnz .pi_div
.pi_out:
    mov rdi, rbx
    call basalt_print_str
    pop r12
    pop rbx
    ret

basalt_panic:
    mov rdi, rdi
    call basalt_print_str
    mov al, 10
    call bare_putchar
    mov rdi, 1
    call basalt_exit
    ret

/* strlen */
basalt_strlen:
    xor rax, rax
    test rdi, rdi
    jz .sl_done
.sl_loop:
    cmp byte ptr [rdi+rax], 0
    je .sl_done
    inc rax
    jmp .sl_loop
.sl_done:
    ret

basalt_char_at:
    test rdi, rdi
    jz .ca_z
    mov rax, rsi
    movzx eax, byte ptr [rdi+rax]
    ret
.ca_z:
    xor rax, rax
    ret

basalt_strcmp:
    push rbx
.sc_loop:
    mov al, [rdi]
    mov bl, [rsi]
    cmp al, bl
    jne .sc_diff
    test al, al
    jz .sc_eq
    inc rdi
    inc rsi
    jmp .sc_loop
.sc_diff:
    movzx eax, al
    movzx ebx, bl
    sub eax, ebx
    movsx rax, eax
    pop rbx
    ret
.sc_eq:
    xor rax, rax
    pop rbx
    ret

/* bump alloc */
basalt_alloc:
    mov rax, [rip+heap_ptr]
    mov rcx, rdi
    add rcx, 15
    and rcx, -16
    lea rdx, [rax+rcx]
    lea r8, [rip+heap_space]
    add r8, 1024*1024
    cmp rdx, r8
    ja .al_oom
    mov [rip+heap_ptr], rdx
    ret
.al_oom:
    xor rax, rax
    ret

basalt_free:
    /* bump allocator: free is no-op */
    ret

/* substr: rdi=s, rsi=start, rdx=len → malloc'd string */
basalt_substr:
    push rbx
    push r12
    push r13
    push r14
    mov r12, rdi
    mov r13, rsi
    mov r14, rdx
    /* size = len+1 */
    lea rdi, [r14+1]
    call basalt_alloc
    test rax, rax
    jz .ss_done
    mov rbx, rax
    xor rcx, rcx
.ss_copy:
    cmp rcx, r14
    jge .ss_nul
    mov al, [r12+r13]
    mov [rbx+rcx], al
    inc r13
    inc rcx
    jmp .ss_copy
.ss_nul:
    mov byte ptr [rbx+rcx], 0
    mov rax, rbx
.ss_done:
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

/* str_concat */
basalt_str_concat:
    push rbx
    push r12
    push r13
    push r14
    mov r12, rdi
    mov r13, rsi
    mov rdi, r12
    call basalt_strlen
    mov r14, rax
    mov rdi, r13
    call basalt_strlen
    lea rdi, [r14+rax+1]
    call basalt_alloc
    test rax, rax
    jz .cc_done
    mov rbx, rax
    /* copy a */
    xor rcx, rcx
    test r12, r12
    jz .cc_b
.cc_a:
    mov al, [r12+rcx]
    test al, al
    jz .cc_b
    mov [rbx+rcx], al
    inc rcx
    jmp .cc_a
.cc_b:
    xor rdx, rdx
    test r13, r13
    jz .cc_nul
.cc_b2:
    mov al, [r13+rdx]
    test al, al
    jz .cc_nul
    mov [rbx+rcx], al
    inc rcx
    inc rdx
    jmp .cc_b2
.cc_nul:
    mov byte ptr [rbx+rcx], 0
    mov rax, rbx
.cc_done:
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

/* itoa */
basalt_itoa:
    push rbx
    push r12
    mov r12, rdi
    mov rdi, 32
    call basalt_alloc
    test rax, rax
    jz .it_done
    mov rbx, rax
    lea rdi, [rbx+31]
    mov byte ptr [rdi], 0
    mov rax, r12
    test rax, rax
    jns .it_pos
    neg rax
.it_pos:
    mov rcx, 10
    test rax, rax
    jnz .it_div
    dec rdi
    mov byte ptr [rdi], '0'
    jmp .it_sign
.it_div:
    xor rdx, rdx
    div rcx
    add dl, '0'
    dec rdi
    mov [rdi], dl
    test rax, rax
    jnz .it_div
.it_sign:
    test r12, r12
    jns .it_copy
    dec rdi
    mov byte ptr [rdi], '-'
.it_copy:
    /* copy to start of buffer */
    mov rsi, rdi
    mov rdi, rbx
.it_cp:
    mov al, [rsi]
    mov [rdi], al
    inc rsi
    inc rdi
    test al, al
    jnz .it_cp
    mov rax, rbx
.it_done:
    pop r12
    pop rbx
    ret
