    .globl task_start_wrapper
    .type task_start_wrapper, @function
    .text
    .align 16
    .intel_syntax noprefix
task_start_wrapper:
    # When iretq jumps here, the CPU is in kernel code segment with interrupts enabled.
    # The fake saved registers area we built places the thread function pointer where rdi would be after 'pop' sequence
    # irq_common_stub restored registers before iretq, so general registers are already set from the saved area.
    # At this point, rdi contains the thread function pointer (we set it in the saved register area).
    push rbp
    mov rbp, rsp
    # Save the function pointer (rdi) in a callee-saved register before calling helpers
    mov r12, rdi
    # Print a debug message so we know the task started
    lea rdi, [rip + .Ltask_start_msg]
    call tty_putstr
    # Call the function using r12 (restores original pointer)
    mov rax, r12
    call rax
    # If it returns, just halt
1:  hlt
    jmp 1b

    .section .rodata
.Ltask_start_msg:
    .ascii "[TASK] start\n\0"
    .text
    .att_syntax
