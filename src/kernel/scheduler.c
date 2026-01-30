#include <kernel/sys/scheduler.h>
#include <kernel/sys/kmalloc.h>
#include <kernel/arch/x86_64/pmm.h>
#include <kernel/arch/x86_64/vmm.h>
#include <kernel/sys/tty.h>
#include <cpu/ports.h>
#include <cpu/gdt.h>
#include <stdint.h>
#include <stddef.h>

// Simple round-robin scheduler for kernel threads and user processes

typedef enum { TASK_UNUSED = 0, TASK_RUNNABLE, TASK_RUNNING, TASK_ZOMBIE } task_state_t;

typedef enum { TASK_KERNEL = 0, TASK_USER } task_type_t;

typedef struct task_struct {
    struct task_struct *next;
    uint64_t rsp;           // saved stack pointer for context
    uint64_t cr3;           // page table base
    task_state_t state;
    task_type_t type;       // kernel thread or user process
    void *stack_base;       // allocated stack base (virtual/identity)
    uint64_t user_rsp;      // user stack pointer (for user processes)
    uint64_t user_rip;      // user instruction pointer (for user processes)
} task_struct_t;

static task_struct_t *task_list = NULL;
static task_struct_t *current = NULL;

// Registers layout: matches pushes in irq_common_stub before calling C handler
// We will store rsp pointing to where the first pushed register (rax) is located.

void scheduler_init(void) {
    // nothing now; tasks will be allocated on demand
}

// helper to allocate a stack (one page)
static void *alloc_stack(void) {
    void *p = pmm_alloc_page();
    if (!p) return NULL;
    // Use top of page as initial stack pointer
    return p;
}

int scheduler_add_task(void (*func)(void)) {
    // allocate task struct from kmalloc (or simple static pool)
    task_struct_t *t = (task_struct_t *)kmalloc(sizeof(task_struct_t));
    if (!t) {
        return -1;
    }
    for (size_t i = 0; i < sizeof(task_struct_t); ++i) ((char*)t)[i] = 0;
    void *stack = alloc_stack();
    if (!stack) {
        tty_putstr("[SCHED] alloc_stack failed\n");
        return -1;
    }
    t->stack_base = stack;
    // Build initial stack frame such that iretq will return into the function
    // But simpler: we will craft a context with saved registers and return into a small trampoline that calls the function.

    // Build initial stack so that when the IRQ return sequence runs (restores registers, cleans error+int, then iretq)
    // the iretq will pop RIP/CS/RFLAGS from the stack and start executing our thread wrapper.
    uint64_t *stack_top = (uint64_t *)((char*)stack + 4096);
    // align
    stack_top = (uint64_t *)((uintptr_t)stack_top & ~0xF);
    // We need space for: saved regs (15*8=120), error(8), int_no(8), RIP(8), CS(8), RFLAGS(8) = 160 bytes -> 20 uint64_t
    stack_top -= 20;
    uint64_t *sp = stack_top;
    // zero saved registers
    for (int i = 0; i < 15; ++i) sp[i] = 0;
    // place function pointer into rdi position (index 5) so wrapper can pick it up
    sp[5] = (uint64_t)func; // rdi
    // error code and int_no
    sp[15] = 0; // error code
    sp[16] = 32; // int_no - we mark as timer so handler logic is consistent
    // RIP, CS, RFLAGS for iretq
    extern void task_start_wrapper(void);
    sp[17] = (uint64_t)task_start_wrapper; // RIP
    sp[18] = 0x08; // kernel code segment
    sp[19] = 0x202; // RFLAGS with interrupts enabled

    t->type = TASK_KERNEL;
    t->rsp = (uint64_t)sp;
    tty_putstr("[SCHED] task rsp=0x");
    tty_puthex64(t->rsp);
    tty_putstr("\n");
    t->cr3 = vmm_get_cr3();
    t->state = TASK_RUNNABLE;

    // insert into circular list
    if (!task_list) {
        task_list = t;
        t->next = t;
    } else {
        t->next = task_list->next;
        task_list->next = t;
    }

    return 0;
}

int scheduler_add_user_process(uint64_t cr3, uint64_t entry, uint64_t user_rsp) {
    // allocate task struct
    task_struct_t *t = (task_struct_t *)kmalloc(sizeof(task_struct_t));
    if (!t) {
        return -1;
    }
    memset_k(t, 0, sizeof(task_struct_t));

    // Allocate kernel stack for the process
    void *stack = alloc_stack();
    if (!stack) {
        tty_putstr("[SCHED] alloc_stack failed for user process\n");
        kfree(t);
        return -1;
    }
    t->stack_base = stack;

    // Build initial kernel stack for context switch
    // When we switch to this process, we need to iretq to user mode
    uint64_t *stack_top = (uint64_t *)((char*)stack + 4096);
    stack_top = (uint64_t *)((uintptr_t)stack_top & ~0xF);
    // Space for saved regs (15*8=120), error(8), int_no(8), RIP(8), CS(8), RFLAGS(8), RSP(8), SS(8) = 176 bytes -> 22 uint64_t
    stack_top -= 22;
    uint64_t *sp = stack_top;

    // Zero saved registers
    for (int i = 0; i < 15; ++i) sp[i] = 0;

    // Error code and int_no
    sp[15] = 0; // error code
    sp[16] = 32; // int_no

    // For iretq: RIP, CS, RFLAGS, RSP, SS
    sp[17] = entry;        // user RIP
    sp[18] = 0x18 | 3;     // user code segment (ring 3)
    sp[19] = 0x202;        // RFLAGS with interrupts enabled
    sp[20] = user_rsp;     // user RSP
    sp[21] = 0x20 | 3;     // user data segment (ring 3)

    t->type = TASK_USER;
    t->rsp = (uint64_t)sp;
    t->cr3 = cr3;
    t->user_rip = entry;
    t->user_rsp = user_rsp;
    t->state = TASK_RUNNABLE;

    tty_putstr("[SCHED] user process added, cr3=0x");
    tty_puthex64(cr3);
    tty_putstr(", entry=0x");
    tty_puthex64(entry);
    tty_putstr(", user_rsp=0x");
    tty_puthex64(user_rsp);
    tty_putstr("\n");

    // insert into circular list
    if (!task_list) {
        task_list = t;
        t->next = t;
    } else {
        t->next = task_list->next;
        task_list->next = t;
    }

    return 0;
}

// Minimal trampoline placed in C; will call the passed function then loop
static __attribute__((noreturn)) void task_trampoline_c(void (*func)(void)) {
    // call the function
    func();
    // if it returns, set state to zombie and halt
    current->state = TASK_ZOMBIE;
    while (1) __asm__ volatile ("hlt");
}

// The low-level entry `task_start_wrapper` is implemented in assembly in task_trampoline.S

// scheduler_switch: called with 'regs' pointing to saved registers area (rsp). Return pointer to regs for next task
void *scheduler_switch(void *regs) {
    // Determine IRQ number from saved stack: irq_common_stub passed irq number then regs; but we always call scheduler on every IRQ
    uint64_t irq_no = 0;
    if (regs) {
        // irq number was pushed after the saved registers and error code; calculate its location
        // In irq_common_stub, they did: push rax..push r15, then mov rdi, [rsp + 120] where 120 bytes offset used
        // Our regs pointer points at the saved rax (lowest pushed), so irq number is at regs + 120
        irq_no = *(uint64_t *)((char*)regs + 120);
    }

    // Only perform time-slice on timer IRQ (32)
    if (irq_no != 32) {
        // return same regs
        return regs;
    }

    // If no tasks, return same regs
    if (!task_list) return regs;

    // Save current task context: if current is NULL, create a fake current for the interrupted context
    if (!current) {
        current = (task_struct_t *)kmalloc(sizeof(task_struct_t));
        for (size_t i = 0; i < sizeof(task_struct_t); ++i) ((char*)current)[i] = 0;
        current->rsp = (uint64_t)regs;
        current->cr3 = vmm_get_cr3();
        current->state = TASK_RUNNING;
        current->next = task_list;
    } else {
        // Save regs into current->rsp
        current->rsp = (uint64_t)regs;
        current->cr3 = vmm_get_cr3();
        current->state = TASK_RUNNABLE;
    }

    // Select next runnable task
    task_struct_t *t = current->next ? current->next : task_list;
    size_t scanned = 0;
    while (scanned <  (1<<20)) { // prevent infinite loops
        if (t->state == TASK_RUNNABLE || t->state == TASK_RUNNING) break;
        t = t->next; scanned++;
    }
    if (!t) return regs;

    // switch CR3 if different
    if (t->cr3 != vmm_get_cr3()) {
        vmm_set_cr3(t->cr3);
    }

    // For user processes, set TSS RSP0 to the kernel stack
    if (t->type == TASK_USER) {
        uint64_t kernel_stack_top = (uint64_t)t->stack_base + 4096;
        tss_set_stack(kernel_stack_top);
    }

    t->state = TASK_RUNNING;
    current = t;

    // return pointer to next task's saved regs
    return (void *)t->rsp;
}
