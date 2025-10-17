#include "scheduler.h"
#include "kmalloc.h"
#include "pmm.h"
#include "vmm.h"
#include "tty.h"
#include "../cpu/ports.h"
#include <stdint.h>
#include <stddef.h>

// Simple round-robin scheduler for kernel threads

typedef enum { TASK_UNUSED = 0, TASK_RUNNABLE, TASK_RUNNING, TASK_ZOMBIE } task_state_t;

typedef struct task_struct {
    struct task_struct *next;
    uint64_t rsp;       // saved stack pointer for context
    uint64_t cr3;       // page table base
    task_state_t state;
    void *stack_base;   // allocated stack base (virtual/identity)
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
    if (!t) return -1;
    for (size_t i = 0; i < sizeof(task_struct_t); ++i) ((char*)t)[i] = 0;
    void *stack = alloc_stack();
    if (!stack) return -1;
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

    t->rsp = (uint64_t)sp;
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

// Minimal trampoline placed in C; will call the passed function then loop
__attribute__((noreturn)) void task_trampoline_c(void (*func)(void)) {
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

    t->state = TASK_RUNNING;
    current = t;

    // return pointer to next task's saved regs
    return (void *)t->rsp;
}
