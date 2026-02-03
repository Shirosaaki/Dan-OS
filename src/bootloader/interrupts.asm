; interrupts.asm - Interrupt Service Routines
global idt_load

section .text
extern tty_putstr
extern tty_puthex64
bits 64

; Load IDT
idt_load:
    lidt [rdi]
    ret

; ============================================
; Syscall entry point (called via syscall instruction)
; ============================================
; On entry:
;   rax = syscall number
;   rdi = arg1
;   rsi = arg2
;   rdx = arg3
;   r10 = arg4 (rcx is clobbered by syscall)
;   r8  = arg5
;   r9  = arg6
;   rcx = return address (set by CPU)
;   r11 = saved RFLAGS (set by CPU)
; ============================================

; Macro for ISRs without error codes
%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    push 0              ; Push dummy error code
    push %1             ; Push interrupt number
    jmp isr_common_stub
%endmacro

; Macro for ISRs with error codes
%macro ISR_ERRCODE 1
global isr%1
isr%1:
    push %1             ; Push interrupt number
    jmp isr_common_stub
%endmacro

; Macro for IRQs
%macro IRQ 2
global irq%1
irq%1:
    push 0              ; Push dummy error code
    push %2             ; Push IRQ number
    jmp irq_common_stub
%endmacro

; CPU Exception handlers (0-31)
ISR_NOERRCODE 0     ; Divide by zero
ISR_NOERRCODE 1     ; Debug
ISR_NOERRCODE 2     ; Non-maskable interrupt
ISR_NOERRCODE 3     ; Breakpoint
ISR_NOERRCODE 4     ; Overflow
ISR_NOERRCODE 5     ; Bound range exceeded
ISR_NOERRCODE 6     ; Invalid opcode
ISR_NOERRCODE 7     ; Device not available
ISR_ERRCODE 8       ; Double fault
ISR_NOERRCODE 9     ; Coprocessor segment overrun
ISR_ERRCODE 10      ; Invalid TSS
ISR_ERRCODE 11      ; Segment not present
ISR_ERRCODE 12      ; Stack-segment fault
ISR_ERRCODE 13      ; General protection fault
ISR_ERRCODE 14      ; Page fault
ISR_NOERRCODE 15    ; Reserved
ISR_NOERRCODE 16    ; x87 floating-point exception
ISR_ERRCODE 17      ; Alignment check
ISR_NOERRCODE 18    ; Machine check
ISR_NOERRCODE 19    ; SIMD floating-point exception
ISR_NOERRCODE 20    ; Virtualization exception
ISR_NOERRCODE 21    ; Reserved
ISR_NOERRCODE 22    ; Reserved
ISR_NOERRCODE 23    ; Reserved
ISR_NOERRCODE 24    ; Reserved
ISR_NOERRCODE 25    ; Reserved
ISR_NOERRCODE 26    ; Reserved
ISR_NOERRCODE 27    ; Reserved
ISR_NOERRCODE 28    ; Reserved
ISR_NOERRCODE 29    ; Reserved
ISR_NOERRCODE 30    ; Security exception
ISR_NOERRCODE 31    ; Reserved

; IRQ handlers (32-47)
IRQ 0, 32       ; Timer
IRQ 1, 33       ; Keyboard
IRQ 2, 34       ; Cascade
IRQ 3, 35       ; COM2
IRQ 4, 36       ; COM1
IRQ 5, 37       ; LPT2
IRQ 6, 38       ; Floppy disk
IRQ 7, 39       ; LPT1
IRQ 8, 40       ; CMOS real-time clock
IRQ 9, 41       ; Free
IRQ 10, 42      ; Free
IRQ 11, 43      ; Free
IRQ 12, 44      ; PS/2 mouse
IRQ 13, 45      ; FPU
IRQ 14, 46      ; Primary ATA
IRQ 15, 47      ; Secondary ATA

extern isr_handler

; Common ISR stub
isr_common_stub:
    ; Save all registers
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Get interrupt number from stack (now at rsp+120)
    mov rdi, [rsp + 120]

    ; Get error code from stack (now at rsp+128)
    mov rsi, [rsp + 128]

    ; Pass pointer to CPU-saved frame (starts at rsp+136) as 3rd arg
    lea rdx, [rsp + 136]

    ; Call C handler
    call isr_handler

    ; Restore all registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    ; Clean up error code and interrupt number
    add rsp, 16

    ; Return from interrupt
    iretq

extern irq_handler
extern scheduler_switch

    ; External symbol for scheduler to set next CR3
    extern sched_next_cr3

; Common IRQ stub
irq_common_stub:
    ; Save all registers
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Get IRQ number from stack
    mov rdi, [rsp + 120]
    
    ; Call C handler
    call irq_handler

    ; Pass pointer to saved registers (current rsp) to scheduler and get new rsp in rax
    mov rdi, rsp
    call scheduler_switch
    ; If scheduler returned a different saved-registers pointer, switch stack to it
    cmp rax, rdi
    je no_switch
    mov rsp, rax

no_switch:

    ; Restore all registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    ; Don't pop rax yet - we'll use it for CR3

    ; Now stack has: [rax][int_no][error][RIP][CS][RFLAGS][RSP][SS]
    ; Load CR3 if needed (using rax which is still on stack)
    mov rax, [sched_next_cr3]
    test rax, rax
    jz .no_cr3_switch
    
    ; Load the new CR3
    mov cr3, rax
    
    ; Clear the global (use a register we've already restored)
    push rbx
    xor rbx, rbx
    mov [sched_next_cr3], rbx
    pop rbx
    
.no_cr3_switch:
    ; Now pop rax
    pop rax

    ; Clean up error code and IRQ number
    add rsp, 16

    ; Return from interrupt - stack now has: [RIP][CS][RFLAGS][RSP][SS]
    iretq

section .rodata
debug_msg: db '[IRETQ DEBUG] RIP about to restore: ',0
newline: db 10,0
