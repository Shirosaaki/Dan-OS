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
    ; Set up L4 -> L3 mapping
    mov eax, page_table_l3                 ; load the address of the L3 page table into eax
    or eax, 0b11                           ; set the present and read/write flags
    mov [page_table_l4], eax               ; store the address of the L3 page table in the L4 page table
    
    ; Set up L3 -> L2 mappings for 4GB (4 L2 tables, each covering 1GB)
    mov eax, page_table_l2_0
    or eax, 0b11
    mov [page_table_l3], eax               ; L3[0] -> L2_0 (0-1GB)
    
    mov eax, page_table_l2_1
    or eax, 0b11
    mov [page_table_l3 + 8], eax           ; L3[1] -> L2_1 (1-2GB)
    
    mov eax, page_table_l2_2
    or eax, 0b11
    mov [page_table_l3 + 16], eax          ; L3[2] -> L2_2 (2-3GB)
    
    mov eax, page_table_l2_3
    or eax, 0b11
    mov [page_table_l3 + 24], eax          ; L3[3] -> L2_3 (3-4GB)
    
    ; Fill L2_0: maps 0x00000000 - 0x3FFFFFFF (0-1GB)
    mov ecx, 0
.loop_l2_0:
    mov eax, 0x200000
    mul ecx
    or eax, 0b10000011                     ; present + read/write + huge page
    mov [page_table_l2_0 + ecx * 8], eax
    inc ecx
    cmp ecx, 512
    jne .loop_l2_0
    
    ; Fill L2_1: maps 0x40000000 - 0x7FFFFFFF (1-2GB)
    mov ecx, 0
.loop_l2_1:
    mov eax, 0x200000
    mul ecx
    add eax, 0x40000000                    ; offset by 1GB
    or eax, 0b10000011
    mov [page_table_l2_1 + ecx * 8], eax
    inc ecx
    cmp ecx, 512
    jne .loop_l2_1
    
    ; Fill L2_2: maps 0x80000000 - 0xBFFFFFFF (2-3GB)
    mov ecx, 0
.loop_l2_2:
    mov eax, 0x200000
    mul ecx
    add eax, 0x80000000                    ; offset by 2GB
    or eax, 0b10000011
    mov [page_table_l2_2 + ecx * 8], eax
    inc ecx
    cmp ecx, 512
    jne .loop_l2_2
    
    ; Fill L2_3: maps 0xC0000000 - 0xFFFFFFFF (3-4GB)
    mov ecx, 0
.loop_l2_3:
    mov eax, 0x200000
    mul ecx
    add eax, 0xC0000000                    ; offset by 3GB
    or eax, 0b10000011
    mov [page_table_l2_3 + ecx * 8], eax
    inc ecx
    cmp ecx, 512
    jne .loop_l2_3
    
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
    resb 4096                           ; 4 KiB page table (L4)
page_table_l3:
    resb 4096                           ; 4 KiB page table (L3)
page_table_l2_0:
    resb 4096                           ; L2 table for 0-1GB
page_table_l2_1:
    resb 4096                           ; L2 table for 1-2GB
page_table_l2_2:
    resb 4096                           ; L2 table for 2-3GB
page_table_l2_3:
    resb 4096                           ; L2 table for 3-4GB
stack_bottom:
    resb 4096 * 4                       ; 16 KiB stack
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