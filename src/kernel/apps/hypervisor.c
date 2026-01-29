/**==============================================
 *             apps/hypervisor.c
 *  Ported VMM for DanOS
 *=============================================**/

#include <kernel/apps/kvm_stub.h>
#include <kernel/sys/tty.h>
#include <kernel/sys/kmalloc.h>
#include <kernel/fs/fat32.h>
#include <kernel/sys/string.h>
#include <kernel/sys/scheduler.h>

// Configuration
#define RAM_SIZE 0x8000 // 32KB

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
    vm->ram_size = RAM_SIZE;

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
        .rsp = 0x7000,  // Match payload's stack
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
    vm->cpu.sp = 0x7000; // match payload's stack (SS=0, SP=0x7000)
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
                if (vm->run->io.direction == KVM_EXIT_IO_OUT && vm->run->io.port == 0x10) {
                    // Output to host
                    uint8_t *data = (uint8_t *)vm->run + vm->run->io.data_offset;
                    uint32_t size = vm->run->io.size;
                    uint32_t count = vm->run->io.count;

                    for (uint32_t t = 0; t < count; ++t) {
                        uint8_t *transfer = data + t * size;
                        for (uint32_t b = 0; b < size; ++b) {
                            char c = (char)transfer[b];
                            tty_putchar(c);
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

// Task runner uses this buffer to receive filename when launched from IRQ context
static char vm_task_filename[256];

static void vm_task(void) {
    vm_t vm;
    const char *filename = vm_task_filename;
    vm_task_impl(&vm, filename);
    
    // --- FIX: Prevent return off the end of the stack ---
    tty_putstr("VM Task Finished. Halting thread.\n");
    while(1) {
        __asm__ volatile("hlt");
    }
}

void vm_task_direct_run(const char *filename) {
    vm_t vm;
    vm_task_impl(&vm, filename);
}

int vm_probe(const char *filename) {
    vm_t vm;
    vm.ram = (void*)0;
    tty_putstr("[VM PROBE] start\n");

    if (vm_init(&vm) != 0) {
        tty_putstr("[VM PROBE] vm_init failed\n");
        if (vm.ram) kfree(vm.ram);
        return -1;
    }
    tty_putstr("[VM PROBE] vm_init ok\n");

    if (vm_load_image(&vm, filename) != 0) {
        tty_putstr("[VM PROBE] vm_load_image failed\n");
        if (vm.ram) kfree(vm.ram);
        return -1;
    }
    tty_putstr("[VM PROBE] vm_load_image ok\n");

    if (vcpu_init(&vm) != 0) {
        tty_putstr("[VM PROBE] vcpu_init failed\n");
        if (vm.ram) kfree(vm.ram);
        return -1;
    }
    tty_putstr("[VM PROBE] vcpu_init ok\n");

    if (vm.ram) kfree(vm.ram);
    tty_putstr("[VM PROBE] all good\n");
    return 0;
}

void vm_task_impl(vm_t *vm, const char *filename) {
    current_vm = vm;

    // SAFETY: Initialize pointer to NULL so we don't double free if init fails
    vm->ram = (void*)0;

    tty_putstr("Starting DanOS VMM...\n");
    tty_putstr("[VM TASK] before vm_init\n");
    // Capture keyboard so TTY doesn't steal keys
    keyboard_set_capture(1);

    if (vm_init(vm) != 0) {
        if (vm->ram) kfree(vm->ram);
        tty_putstr("[VM TASK] vm_init failed\n");
        return;
    }
    tty_putstr("[VM TASK] vm_init ok\n");

    if (vm_load_image(vm, filename) != 0) {
        if (vm->ram) kfree(vm->ram);
        tty_putstr("[VM TASK] vm_load_image failed\n");
        return;
    }
    tty_putstr("[VM TASK] vm_load_image ok\n");

    if (vcpu_init(vm) != 0) {
        if (vm->ram) kfree(vm->ram);
        tty_putstr("[VM TASK] vcpu_init failed\n");
        return;
    }
    tty_putstr("[VM TASK] vcpu_init ok\n");

    tty_putstr("[VM TASK] entering vcpu_run\n");
    vcpu_run(vm);

    // Cleanup
    if (vm->ram) kfree(vm->ram);
    // Release keyboard capture
    keyboard_set_capture(0);

    tty_putstr("VM Terminated.\n");
}

void cmd_vm(const char* filename) {
    // Copy filename to task buffer and schedule the VM task so it runs
    // outside of IRQ context (tty_process_command may be called from IRQ)
    int i;
    for (i = 0; i < 255 && filename[i]; ++i) vm_task_filename[i] = filename[i];
    vm_task_filename[i] = '\0';
    // Schedule task
    int rc = scheduler_add_task(vm_task);
    if (rc == 0) {
        tty_putstr("VM scheduled as kernel task.\n");
    } else {
        tty_putstr("Failed to schedule VM (rc=");
        tty_putdec(rc);
        tty_putstr("). Running synchronously for debug...\n");
        /* Run directly for debugging (may be unsafe in IRQ contexts) */
        vm_task();
    }
}
