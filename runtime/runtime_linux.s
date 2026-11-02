.intel_syntax noprefix
.global basalt_print_int
.global basalt_print_str
.global basalt_print_nl
.global basalt_set_args
.global basalt_argc
.global basalt_argv
.global basalt_panic
.global basalt_strlen
.global basalt_char_at
.global basalt_strcmp
.global basalt_substr
.global basalt_str_concat
.global basalt_itoa
.global basalt_alloc
.global basalt_free
.global basalt_exit
.global basalt_read_file
.global basalt_write_file
.global basalt_system
.global _start
.extern main

.section .bss
.align 16
heap: .space 1048576
heap_ptr: .quad 0
itoa_buf: .space 32
args_argc: .quad 0

.section .text
.align 16
_start:
    lea rax, [rip+heap]
    mov [rip+heap_ptr], rax
    xor edi, edi
    call basalt_set_args
    call main
    mov rdi, rax
    mov rax, 60
    syscall

basalt_set_args:
    ret
basalt_argc:
    xor eax, eax
    ret
basalt_argv:
    xor eax, eax
    ret
basalt_system:
    mov rax, -1
    ret
basalt_read_file:
    xor eax, eax
    ret
basalt_write_file:
    mov rax, -1
    ret
basalt_free:
    ret

basalt_print_nl:
    push rax
    mov rax, 1
    mov rdi, 1
    lea rsi, [rip+nl]
    mov rdx, 1
    syscall
    pop rax
    ret
nl: .byte 10

basalt_print_str:
    push rbx
    mov rbx, rdi
    test rbx, rbx
    jz .ps_done
    mov rdi, rbx
    call basalt_strlen
    mov rdx, rax
    mov rax, 1
    mov rdi, 1
    mov rsi, rbx
    syscall
.ps_done:
    pop rbx
    ret

basalt_print_int:
    push rbx
    push r12
    mov r12, rdi
    lea rbx, [rip+itoa_buf+31]
    mov byte ptr [rbx], 0
    mov rax, r12
    test rax, rax
    jns .pi_pos
    neg rax
    push rax
    mov rax, 1
    mov rdi, 1
    lea rsi, [rip+minus]
    mov rdx, 1
    syscall
    pop rax
.pi_pos:
    mov rcx, 10
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
minus: .byte '-'

basalt_strlen:
    xor rax, rax
    test rdi, rdi
    jz .sl_d
.sl:
    cmp byte ptr [rdi+rax], 0
    je .sl_d
    inc rax
    jmp .sl
.sl_d:
    ret

basalt_char_at:
    test rdi, rdi
    jz .ca0
    movzx eax, byte ptr [rdi+rsi]
    ret
.ca0:
    xor eax, eax
    ret

basalt_strcmp:
    mov al, [rdi]
    mov cl, [rsi]
    cmp al, cl
    jne .sc
    test al, al
    jz .sceq
    inc rdi
    inc rsi
    jmp basalt_strcmp
.sc:
    movzx eax, al
    movzx ecx, cl
    sub eax, ecx
    ret
.sceq:
    xor eax, eax
    ret

basalt_alloc:
    mov rax, [rip+heap_ptr]
    mov rcx, rdi
    add rcx, 15
    and rcx, -16
    lea rdx, [rax+rcx]
    lea r8, [rip+heap]
    add r8, 1048576
    cmp rdx, r8
    ja .oom
    mov [rip+heap_ptr], rdx
    ret
.oom:
    xor eax, eax
    ret

basalt_substr:
    push rbx
    push r12
    push r13
    push r14
    mov r12, rdi
    mov r13, rsi
    mov r14, rdx
    lea rdi, [r14+1]
    call basalt_alloc
    test rax, rax
    jz .ss
    mov rbx, rax
    xor ecx, ecx
.ssc:
    cmp rcx, r14
    jge .ssn
    mov al, [r12+r13]
    mov [rbx+rcx], al
    inc r13
    inc rcx
    jmp .ssc
.ssn:
    mov byte ptr [rbx+rcx], 0
    mov rax, rbx
.ss:
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

basalt_str_concat:
    push rbx
    push r12
    push r13
    push r14
    mov r12, rdi
    mov r13, rsi
    call basalt_strlen
    mov r14, rax
    mov rdi, r13
    call basalt_strlen
    lea rdi, [r14+rax+1]
    call basalt_alloc
    test rax, rax
    jz .cc
    mov rbx, rax
    xor ecx, ecx
.cc1:
    test r12, r12
    jz .cc2
    mov al, [r12+rcx]
    test al, al
    jz .cc2
    mov [rbx+rcx], al
    inc rcx
    jmp .cc1
.cc2:
    xor edx, edx
.cc3:
    test r13, r13
    jz .ccn
    mov al, [r13+rdx]
    test al, al
    jz .ccn
    mov [rbx+rcx], al
    inc rcx
    inc rdx
    jmp .cc3
.ccn:
    mov byte ptr [rbx+rcx], 0
    mov rax, rbx
.cc:
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

basalt_itoa:
    mov rdi, rdi
    push rbx
    push r12
    mov r12, rdi
    mov rdi, 32
    call basalt_alloc
    test rax, rax
    jz .it
    mov rbx, rax
    lea rdi, [rbx+31]
    mov byte ptr [rdi], 0
    mov rax, r12
    test rax, rax
    jns .itp
    neg rax
.itp:
    mov rcx, 10
    test rax, rax
    jnz .itd
    dec rdi
    mov byte ptr [rdi], '0'
    jmp .its
.itd:
    xor rdx, rdx
    div rcx
    add dl, '0'
    dec rdi
    mov [rdi], dl
    test rax, rax
    jnz .itd
.its:
    test r12, r12
    jns .itc
    dec rdi
    mov byte ptr [rdi], '-'
.itc:
    mov rsi, rdi
    mov rdi, rbx
.itcp:
    mov al, [rsi]
    mov [rdi], al
    inc rsi
    inc rdi
    test al, al
    jnz .itcp
    mov rax, rbx
.it:
    pop r12
    pop rbx
    ret

basalt_panic:
    call basalt_print_str
    call basalt_print_nl
    mov rdi, 1
    mov rax, 60
    syscall

basalt_exit:
    mov rax, 60
    syscall
