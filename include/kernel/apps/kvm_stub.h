/**==============================================
 *             include/kvm_stub.h
 *  Minimal KVM definitions for DanOS
 *=============================================**/

#ifndef KVM_STUB_H
#define KVM_STUB_H

#include <stdint.h>

// KVM Exit Reasons
#define KVM_EXIT_UNKNOWN         0
#define KVM_EXIT_IO              2
#define KVM_EXIT_HLT             5
#define KVM_EXIT_MMIO            6
#define KVM_EXIT_FAIL_ENTRY      9

// IO Directions
#define KVM_EXIT_IO_IN  0
#define KVM_EXIT_IO_OUT 1

// Structs
struct kvm_regs {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rsp, rbp;
    uint64_t r8,  r9,  r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rip, rflags;
};

struct kvm_segment {
    uint64_t base;
    uint32_t limit;
    uint16_t selector;
    uint8_t  type;
    uint8_t  present, dpl, db, s, l, g, avl;
    uint8_t  unusable;
    uint8_t  padding;
};

struct kvm_sregs {
    struct kvm_segment cs, ds, es, fs, gs, ss;
    struct kvm_segment tr, ldt;
    uint64_t gdt_base, gdt_limit;
    uint64_t idt_base, idt_limit;
    uint64_t cr0, cr2, cr3, cr4, cr8;
    uint64_t efer;
    uint64_t apic_base;
    uint64_t interrupt_bitmap[4];
};

struct kvm_run {
    uint8_t request_interrupt_window;
    uint32_t exit_reason;
    uint32_t instruction_length;
    uint8_t  ready_for_interrupt_injection;
    uint8_t  if_flag;
    uint64_t flags;

    union {
        /* KVM_EXIT_IO */
        struct {
            uint8_t direction;
            uint8_t size;
            uint16_t port;
            uint32_t count;
            uint64_t data_offset; 
        } io;
        /* KVM_EXIT_FAIL_ENTRY */
        struct {
            uint64_t hardware_entry_failure_reason;
        } fail_entry;
        /* KVM_EXIT_MMIO */
        struct {
            uint64_t phys_addr;
            uint8_t  data[8];
            uint32_t len;
            uint8_t  is_write;
        } mmio;
    };
    /* Data area for exits that need to transfer data (emulator uses this).
       Ensures there's room to write IO bytes safely. */
    uint8_t data[256];
};

struct kvm_userspace_memory_region {
    uint32_t slot;
    uint32_t flags;
    uint64_t guest_phys_addr;
    uint64_t memory_size;
    uint64_t userspace_addr;
};

// VM structure for emulation
struct vm {
    int vm_fd;
    int vcpu_fd;
    uint8_t *ram;
    int ram_size;
    struct kvm_run *run;
    struct {
        uint16_t ax, bx, cx, dx, si, di, sp, bp;
        uint16_t ip;
        uint16_t cs, ds, es, ss;
        uint8_t flags;
    } cpu;
};
typedef struct vm vm_t;

// API Functions (Replacing ioctls)
int danos_kvm_create_vm(void);
int danos_kvm_create_vcpu(int vm_fd);
int danos_kvm_get_vcpu_mmap_size(void);
struct kvm_run* danos_kvm_map_run(int vcpu_fd);
int danos_kvm_set_user_memory_region(int vm_fd, struct kvm_userspace_memory_region *region);
int danos_kvm_get_sregs(int vcpu_fd, struct kvm_sregs *sregs);
int danos_kvm_set_sregs(int vcpu_fd, struct kvm_sregs *sregs);
int danos_kvm_set_regs(int vcpu_fd, struct kvm_regs *regs);
int danos_kvm_run(int vcpu_fd);

#endif /* KVM_STUB_H */