/*
 * src/kernel/syscall/syscall_entry.S
 * System Call Entry Point (GAS Syntax for x86_64-elf-gcc)
 */

.code64
.section .bss
.align 16
    /* 
     * Temporary storage for User RSP.
     * In a full OS, this should be per-CPU data accessed via GS base.
     */
    .global temp_user_rsp
    temp_user_rsp: .skip 8

    /* 
     * Temporary kernel stack for the syscall handler.
     * In a full OS, this should come from the TSS (RSP0).
     */
    .global temp_kernel_rsp
    temp_kernel_rsp: .skip 8
    
    /* Fallback stack if temp_kernel_rsp isn't set */
    .align 16
    fallback_stack_bottom:
    .skip 4096
    fallback_stack_top:

.section .text
.global syscall_entry
.extern syscall_handler

syscall_entry:
    /* 
     * 1. Save User Stack Pointer 
     * If you had swapgs set up, you would do: swapgs; movq %rsp, %gs:scratch_offset
     */
    movq %rsp, temp_user_rsp(%rip)

    /* 
     * 2. Load Kernel Stack Pointer 
     * We try to load a preset kernel stack. If 0, use fallback.
     */
    movq temp_kernel_rsp(%rip), %rsp
    cmpq $0, %rsp
    jne 1f
    leaq fallback_stack_top(%rip), %rsp
1:

    /* 
     * 3. Construct Trap Frame for IRETQ/SYSRETQ compatibility
     * Stack Layout: SS, RSP, RFLAGS, CS, RIP
     */
    pushq $0x23                 /* SS (User Data) */
    pushq temp_user_rsp(%rip)   /* User RSP */
    pushq %r11                  /* RFLAGS (saved by syscall) */
    pushq $0x1B                 /* CS (User Code) */
    pushq %rcx                  /* RIP (saved by syscall) */

    /* 
     * 4. Save General Purpose Registers 
     * Pushing in specific order to match standard struct pt_regs
     */
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* 
     * 5. Map Arguments
     * Syscall ABI: RAX(nr), RDI(a1), RSI(a2), RDX(a3), R10(a4), R8(a5), R9(a6)
     * C ABI:       RDI(a1), RSI(a2), RDX(a3), RCX(a4), R8(a5),  R9(a6)
     *
     * We map:
     * Handler(nr, arg1, arg2, arg3, arg4, arg5)
     */
    movq %r8,  %r9      /* Arg5 -> Arg6 reg (R9) */
    movq %r10, %r8      /* Arg4 -> Arg5 reg (R8) */
    movq %rdx, %rcx     /* Arg3 -> Arg4 reg (RCX) */
    movq %rsi, %rdx     /* Arg2 -> Arg3 reg (RDX) */
    movq %rdi, %rsi     /* Arg1 -> Arg2 reg (RSI) */
    movq %rax, %rdi     /* Nr   -> Arg1 reg (RDI) */

    /* 6. Call C Handler */
    call syscall_handler

    /* 
     * 7. Save Return Value
     * syscall_handler returns result in RAX. 
     * We overwrite the saved RAX on the stack (which is at offset 112)
     * 14 registers * 8 bytes = 112 bytes
     */
    movq %rax, 112(%rsp)

    /* 8. Restore Registers */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax  /* Pops the return value we saved earlier */

    /* 
     * 9. Return to User Mode (SYSRETQ)
     * Stack here: RIP, CS, RFLAGS, RSP, SS
     * SYSRETQ requires: RIP in RCX, RFLAGS in R11
     */
    popq %rcx       /* Restore RIP -> RCX */
    addq $8, %rsp   /* Skip CS */
    popq %r11       /* Restore RFLAGS -> R11 */
    popq %rsp       /* Restore User RSP -> RSP (Stack Switch) */
    
    /* We are now on the User Stack */
    sysretq
