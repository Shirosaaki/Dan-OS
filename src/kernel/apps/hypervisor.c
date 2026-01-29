/**==============================================
 *             apps/hypervisor.c
 *  Ported VMM for DanOS
 *=============================================**/

#include "kvm_stub.h"
#include "tty.h"
#include "kmalloc.h"
#include "fat32.h"
#include "string.h"

// Configuration
#define RAM_SIZE 0x1000 // 4KB

// --- Utils ---
void vm_err(const char *msg) {
    tty_putstr("[VM ERROR] ");
    tty_putstr(msg);
    tty_putstr("\n");
}

// --- VM Logic ---
// Changed return type to int to propagate errors
int vm_init(vm_t *vm) {
    // 1. Create VM
    vm->vm_fd = danos_kvm_create_vm();
    if (vm->vm_fd < 0) { vm_err("Create VM failed"); return -1; }

    // 2. Allocate RAM
    vm->ram = (uint8_t*)kmalloc(RAM_SIZE);
    if (!vm->ram) { vm_err("Alloc RAM failed"); return -1; }
    memset_k(vm->ram, 0, RAM_SIZE);

    // 3. Map RAM
    struct kvm_userspace_memory_region region = {
        .slot = 0,
        .flags = 0,
        .guest_phys_addr = 0x0,
        .memory_size = RAM_SIZE,
        .userspace_addr = (uint64_t)vm->ram
    };
    
    if (danos_kvm_set_user_memory_region(vm->vm_fd, &region) < 0) {
        vm_err("Set memory region failed");
        return -1;
    }
        
    tty_putstr("[VM] RAM allocated at ");
    tty_puthex64((uint64_t)vm->ram);
    tty_putstr("\n");
    
    return 0; // Success
}

// --- Image Loading ---
int vm_load_image(vm_t *vm, const char *filename) {
    fat32_file_t file;
    if (fat32_open_file(filename, &file) != 0) {
        tty_putstr("Failed to open file: ");
        tty_putstr(filename);
        tty_putstr("\n");
        return -1;
    }

    if (file.file_size > RAM_SIZE) {
        vm_err("File too large for RAM");
        return -1;
    }

    // Read directly into VM RAM
    int ret = fat32_read_file(&file, vm->ram, file.file_size);
    if (ret <= 0) {
        vm_err("Failed to read payload");
        return -1;
    }
    
    tty_putstr("[VM] Loaded ");
    tty_putdec(ret);
    tty_putstr(" bytes.\n");
    return 0;
}

// --- VCPU Init ---
int vcpu_init(vm_t *vm) {
    // 1. Create VCPU
    vm->vcpu_fd = danos_kvm_create_vcpu(vm->vm_fd);
    if (vm->vcpu_fd < 0) { vm_err("Create vCPU failed"); return -1; }

    // 2. Map shared structure
    vm->run = danos_kvm_map_run(vm->vcpu_fd);
    if (!vm->run) { vm_err("Map run failed"); return -1; }

    // 3. Configure Registers
    struct kvm_sregs sregs;
    danos_kvm_get_sregs(vm->vcpu_fd, &sregs);
    // ... setup sregs logic ...
    sregs.cs.base = 0; sregs.cs.selector = 0;
    // (You can keep your existing sregs setup here)
    
    struct kvm_regs regs = {
        .rip = 0,
        .rsp = 0x1000,
        .rflags = 0x2 | (1 << 9),
    };

    if (danos_kvm_set_sregs(vm->vcpu_fd, &sregs) < 0) return -1;
    if (danos_kvm_set_regs(vm->vcpu_fd, &regs) < 0) return -1;
    
    // Initialize CPU state for emulation
    vm->cpu.ip = 0;
    vm->cpu.cs = 0;
    vm->cpu.ds = 0;
    vm->cpu.es = 0;
    vm->cpu.ss = 0;
    vm->cpu.sp = 0x1000;
    vm->cpu.ax = 0;
    vm->cpu.bx = 0;
    vm->cpu.cx = 0;
    vm->cpu.dx = 0;
    vm->cpu.si = 0;
    vm->cpu.di = 0;
    vm->cpu.bp = 0;
    vm->cpu.flags = 0;
    
    return 0;
}

// --- Execution Loop ---
void vcpu_run(vm_t *vm) {
    tty_putstr("[VCPU] Starting execution...\n");
    
    while (1) {
        int ret = danos_kvm_run(vm->vcpu_fd);
        if (ret < 0) {
            tty_putstr("KVM_RUN returned error.\n");
            return;
        }

        switch (vm->run->exit_reason) {
            case KVM_EXIT_IO: {
                if (vm->run->io.port == 0x10 && vm->run->io.direction == KVM_EXIT_IO_OUT) {
                    // Safe now because kvm_run is 4096 bytes
                    uint8_t *data = (uint8_t *)vm->run + vm->run->io.data_offset;
                    uint32_t size = vm->run->io.size;
                    uint32_t count = vm->run->io.count;

                    for (uint32_t t = 0; t < count; ++t) {
                        uint8_t *transfer = data + t * size;
                        for (uint32_t b = 0; b < size; ++b) {
                            tty_putchar((char)transfer[b]); 
                        }
                    }
                } 
                break;
            }

            case KVM_EXIT_HLT:
                tty_putstr("\n[KVM] Guest Halted.\n");
                return;

            default:
                tty_putstr("Unhandled Exit: ");
                tty_putdec(vm->run->exit_reason);
                tty_putstr("\n");
                return;
        }
    }
}

// --- Main Command Entry ---
vm_t *current_vm = NULL;

void cmd_vm(const char* filename) {
    vm_t vm;
    current_vm = &vm;
    
    // SAFETY: Initialize pointer to NULL so we don't double free if init fails
    vm.ram = (void*)0; 

    tty_putstr("Starting DanOS VMM...\n");
    
    if (vm_init(&vm) != 0) {
        if (vm.ram) kfree(vm.ram); // Cleanup if partial fail
        return;
    }

    if (vm_load_image(&vm, filename) != 0) {
        kfree(vm.ram);
        return;
    }
    
    if (vcpu_init(&vm) != 0) {
        kfree(vm.ram);
        return;
    }

    vcpu_run(&vm);
    
    // Cleanup
    if (vm.ram) kfree(vm.ram);
    
    // NOTE: In a real implementation, you should also free vm->run 
    // but current_run in kvm_stub is static/globalish so we leave it for now
    // or add a danos_kvm_destroy_vcpu function.
    
    tty_putstr("VM Terminated.\n");
}
