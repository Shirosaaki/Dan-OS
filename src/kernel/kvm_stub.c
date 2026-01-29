/**==============================================
 *             kernel/kvm_stub.c
 *  Mock implementation of KVM for DanOS
 *=============================================**/

#include "kvm_stub.h"
#include "kmalloc.h"
#include "tty.h"
#include "string.h"

// Simple tracking for demo purposes
static struct kvm_run *current_run = 0;
extern vm_t *current_vm;

int danos_kvm_create_vm(void) {
    // tty_putstr("[KVM] Creating VM...\n");
    return 100; // Fake FD
}

int danos_kvm_create_vcpu(int vm_fd) {
    (void)vm_fd;
    tty_putstr("[KVM] Creating vCPU...\n");
    // Allocate the shared run structure
    current_run = (struct kvm_run*)kmalloc(sizeof(struct kvm_run));
    memset_k(current_run, 0, sizeof(struct kvm_run));
    return 101; // Fake FD
}

int danos_kvm_get_vcpu_mmap_size(void) {
    return sizeof(struct kvm_run);
}

struct kvm_run* danos_kvm_map_run(int vcpu_fd) {
    (void)vcpu_fd;
    return current_run;
}

int danos_kvm_set_user_memory_region(int vm_fd, struct kvm_userspace_memory_region *region) {
    (void)vm_fd;
    // tty_putstr("[KVM] Memory region mapped: Phys 0x");
    // tty_puthex64(region->guest_phys_addr);
    // tty_putstr(" -> Host 0x");
    // tty_puthex64(region->userspace_addr);
    // tty_putstr("\n");
    return 0;
}

int danos_kvm_get_sregs(int vcpu_fd, struct kvm_sregs *sregs) {
    (void)vcpu_fd;
    memset_k(sregs, 0, sizeof(struct kvm_sregs));
    return 0;
}

int danos_kvm_set_sregs(int vcpu_fd, struct kvm_sregs *sregs) {
    (void)vcpu_fd;
    (void)sregs;
    return 0;
}

int danos_kvm_set_regs(int vcpu_fd, struct kvm_regs *regs) {
    (void)vcpu_fd;
    (void)regs;
    tty_putstr("[KVM] vCPU Registers initialized.\n");
    return 0;
}

// Simple 16-bit x86 emulator for the payload
static int emulate_instruction(vm_t *vm) {
    if (!vm) return -1;
    uint8_t *ram = vm->ram;
    uint16_t ip = vm->cpu.ip;
    uint8_t opcode = ram[ip++];
    
    switch (opcode) {
        case 0x31: { // xor r/m, r
            uint8_t modrm = ram[ip++];
            if (modrm == 0xC0) { // xor ax, ax
                vm->cpu.ax = 0;
            }
            break;
        }
        case 0x8E: { // mov sreg, r
            uint8_t modrm = ram[ip++];
            if (modrm == 0xD8) { // mov ds, ax
                vm->cpu.ds = vm->cpu.ax;
            } else if (modrm == 0xC0) { // mov es, ax
                vm->cpu.es = vm->cpu.ax;
            } else if (modrm == 0xD0) { // mov ss, ax
                vm->cpu.ss = vm->cpu.ax;
            }
            break;
        }
        case 0xBC: { // mov sp, imm16
            vm->cpu.sp = *(uint16_t*)&ram[ip];
            ip += 2;
            break;
        }
        case 0xBE: { // mov si, imm16
            vm->cpu.si = *(uint16_t*)&ram[ip];
            ip += 2;
            break;
        }
        case 0xE8: { // call rel16
            int16_t rel = *(int16_t*)&ram[ip];
            ip += 2;
            vm->cpu.sp -= 2;
            *(uint16_t*)&ram[vm->cpu.ss * 16 + vm->cpu.sp] = ip;
            ip += rel;
            break;
        }
        case 0xAC: { // lodsb
            vm->cpu.ax = (vm->cpu.ax & 0xFF00) | ram[vm->cpu.ds * 16 + vm->cpu.si];
            vm->cpu.si++;
            break;
        }
        case 0x08: { // or al, al
            uint8_t modrm = ram[ip++];
            if (modrm == 0xC0) {
                uint8_t al = vm->cpu.ax & 0xFF;
                vm->cpu.flags = (al == 0) ? 0x40 : 0;
            }
            break;
        }
        case 0x74: { // jz rel8
            int8_t rel = ram[ip++];
            if (vm->cpu.flags & 0x40) ip += rel;
            break;
        }
        case 0xE4: { // in al, imm8
            uint8_t port = ram[ip++];
            static int in_count = 0;
            in_count++;
            if (port == 0x11) {
                uint8_t input = 0;
                if (in_count > 50) input = 'q'; // Simulate 'q' after some loops
                vm->cpu.ax = (vm->cpu.ax & 0xFF00) | input;
            }
            break;
        }
        case 0xE6: { // out imm8, al
            uint8_t port = ram[ip++];
            if (port == 0x10) {
                // IO exit
                current_run->exit_reason = KVM_EXIT_IO;
                current_run->io.direction = KVM_EXIT_IO_OUT;
                current_run->io.port = 0x10;
                current_run->io.size = 1;
                current_run->io.count = 1;
                current_run->io.data_offset = 100;
                ((uint8_t*)current_run)[100] = vm->cpu.ax & 0xFF;
                vm->cpu.ip = ip;
                return 1; // Exit
            }
            break;
        }
        case 0x3C: { // cmp al, imm8
            uint8_t imm = ram[ip++];
            uint8_t al = vm->cpu.ax & 0xFF;
            vm->cpu.flags = (al == imm) ? 0x40 : 0;
            break;
        }
        case 0x75: { // jnz rel8
            int8_t rel = ram[ip++];
            if (!(vm->cpu.flags & 0x40)) ip += rel;
            break;
        }
        case 0xEB: { // jmp rel8
            int8_t rel = ram[ip++];
            ip += rel;
            break;
        }
        case 0xC3: { // ret
            ip = *(uint16_t*)&ram[vm->cpu.ss * 16 + vm->cpu.sp];
            vm->cpu.sp += 2;
            break;
        }
        case 0xF4: { // hlt
            current_run->exit_reason = KVM_EXIT_HLT;
            vm->cpu.ip = ip;
            return 1; // Exit
        }
        default:
            // Unknown opcode, halt
            current_run->exit_reason = KVM_EXIT_HLT;
            vm->cpu.ip = ip;
            return 1;
    }
    vm->cpu.ip = ip;
    return 0; // Continue
}

// THIS IS WHERE THE MAGIC WOULD HAPPEN
// Since we don't have VMX support yet, we emulate the guest CPU
int danos_kvm_run(int vcpu_fd) {
    (void)vcpu_fd;
    if (!current_vm) return -1;
    
    static int instr_count = 0;
    instr_count++;
    if (instr_count > 10000) { // Prevent infinite loop
        current_run->exit_reason = KVM_EXIT_HLT;
        return 0;
    }
    
    // Emulate instructions until exit
    while (emulate_instruction(current_vm) == 0) {
        instr_count++;
        if (instr_count > 10000) {
            current_run->exit_reason = KVM_EXIT_HLT;
            return 0;
        }
    }
    return 0;
}
