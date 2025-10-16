global long_mode_start
extern kernel_main
extern multiboot_ptr

section .text
bits 64
long_mode_start:
    ; load null into all data segment registers
    mov ax, 0
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Load the multiboot pointer saved by the 32-bit entry (multiboot_ptr)
    ; Read the 32-bit value and zero-extend into RBX, then pass in RDI.
    mov eax, dword [rel multiboot_ptr]
    mov rbx, rax
    mov rdi, rbx
    call kernel_main
    hlt
