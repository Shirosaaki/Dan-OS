    .globl task_start_wrapper
    .type task_start_wrapper, @function
    .text
    .align 16
    .intel_syntax noprefix
task_start_wrapper:
    push rbp
    mov rbp, rsp
    mov r12, rdi
    lea rdi, [rip + .Ltask_start_msg]
    call tty_putstr
    mov rax, r12
    call rax
1:  hlt
    jmp 1b

    .section .rodata
.Ltask_start_msg:
    .ascii "[TASK] start\n\0"
    .text
    .att_syntax
