global start
global multiboot_ptr
extern long_mode_start

section .text
bits 32
start:
    mov esp, stack_top                      ; set the stack pointer
    ; Save Multiboot2 info pointer (in EBX) so it survives across calls
    mov [multiboot_ptr], ebx
    call check_multiboot                    ; check the Multiboot2 header
    call check_cpuid                        ; check if the CPU supports CPUID
    call check_long_mode                    ; check if the CPU supports long mode

    call setup_page_tables                  ; setup page tables
    call enable_paging                      ; enable paging

    lgdt [gdt64.pointer]                    ; load the GDT
    jmp gdt64.code_segment:long_mode_start  ; jump to the long mode start

    hlt                                     ; halt the CPU

check_multiboot:
    cmp eax, 0x36d76289                    ; Multiboot2 magic number
    jne .no_multiboot                      ; if the magic number is not present, print an error message
    ret
.no_multiboot:
    mov al, "M"                            ; Store "M" in al register (error code)
    jmp error                              ; if the magic number is not present, print an error message

check_cpuid:
    pushfd                                 ; push the EFLAGS register onto the stack
    pop eax                                ; pop the EFLAGS register into the eax register
    mov ecx, eax                           ; save the EFLAGS register in ecx
    xor eax, 1 << 21                       ; toggle the ID bit in the EFLAGS register
    push eax                               ; push the modified EFLAGS register onto the stack
    popfd                                  ; pop the modified EFLAGS register into the EFLAGS register
    pushfd                                 ; push the modified EFLAGS register onto the stack
    pop eax                                ; pop the modified EFLAGS register into the eax register
    push ecx                               ; push the original EFLAGS register onto the stack
    popfd                                  ; pop the original EFLAGS register into the EFLAGS register
    cmp eax, ecx                           ; compare the original and modified EFLAGS registers
    je .no_cpuid                           ; if the ID bit is not toggled, the CPU does not support CPUID
    ret
.no_cpuid:
    mov al, "C"                            ; Store "C" in al register (error code)
    jmp error                              ; print an error message

check_long_mode:
    mov eax, 0x80000000                    ; CPUID function to check for long mode support
    cpuid                                  ; call the CPUID instruction
    cmp eax, 0x80000001                    ; check if the CPUID function is supported
    jb .no_long_mode                       ; if the CPUID function is not supported, the CPU does not support long mode
    mov eax, 0x80000001                    ; CPUID function to check for long mode support
    cpuid                                  ; call the CPUID instruction
    test edx, 1 << 29                      ; check if the long mode bit is set in the feature flags
    jz .no_long_mode                       ; if the long mode bit is not set, the CPU does not support long mode
    ret
.no_long_mode:
    mov al, "L"                            ; Store "L" in al register (error code)
    jmp error                             ; print an error message

setup_page_tables:
    mov eax, page_table_l3                 ; load the address of the L3 page table into eax
    or eax, 0b11                           ; set the present and read/write flags
    mov [page_table_l4], eax               ; store the address of the L3 page table in the L4 page table
    mov eax, page_table_l2                 ; load the address of the L2 page table into eax
    or eax, 0b11                           ; set the present and read/write flags
    mov [page_table_l3], eax               ; store the address of the L2 page table in the L3 page table
    mov ecx, 0                           ; clear the ecx register
.loop:
    mov eax, 0x200000                      ; load the address of the L1 page table into eax
    mul ecx                                ; multiply the ecx register by 2 MiB
    or eax, 0b10000011                     ; set the present, read/write, and user/supervisor flags
    mov [page_table_l2 + ecx * 8], eax     ; store the address of the L1 page table in the L2 page table
    inc ecx                                ; set the ecx register to 1
    cmp ecx, 512                           ; shift the ecx register left by 512 bits
    jne .loop                              ; loop until the ecx register is equal to 512
    ret

enable_paging:
    mov eax, page_table_l4                 ; load the address of the L4 page table into eax
    mov cr3, eax                           ; store the address of the L4 page table in the CR3 register
    mov eax, cr4                           ; load the CR4 register into eax
    or eax, 1 << 5                         ; set the PAE flag in the CR4 register
    mov cr4, eax                           ; store the modified CR4 register in the CR4 register
    mov ecx, 0xC0000080                    ; load the EFER MSR address into eax
    rdmsr                                  ; read the EFER MSR
    or eax, 1 << 8                         ; set the LME flag in the EFER MSR
    wrmsr                                  ; write the modified EFER MSR
    mov eax, cr0                           ; load the CR0 register into eax
    or eax, 1 << 31                        ; set the PG flag in the CR0 register
    mov cr0, eax                           ; store the modified CR0 register in the CR0 register
    ret

error:
    ; print "ERR: X" to the top left corner of the screen
    mov dword [0xb8000], 0x4f524f45         ; E
    mov dword [0xb8004], 0x4f3a4f52         ; RR
    mov dword [0xb8008], 0x4f204f20         ;
    mov byte [0xb800a], al                  ; print the error code
    hlt                                     ; halt the CPU

section .bss
align 4096
page_table_l4:
    resb 4096                           ; 4 KiB page table
page_table_l3:
    resb 4096                           ; 4 KiB page table
page_table_l2:
    resb 4096                           ; 4 KiB page table
stack_bottom:
    resb 4096 * 4                       ; 4 MiB stack
stack_top:

; Storage for multiboot info pointer (32-bit value)
multiboot_ptr:
    resd 1
section .rodata
gdt64:
    dq 0                                ; null descriptor
.code_segment: equ $ - gdt64
    dq (1 << 43) | (1 << 44) | (1 << 47) |  (1 << 53)   ; code descriptor
.pointer:
    dw $ - gdt64 - 1                    ; size of the GDT - 1
    dq gdt64                            ; address of the GDT