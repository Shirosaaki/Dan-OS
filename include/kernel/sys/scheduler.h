#ifndef DANOS_SCHEDULER_H
#define DANOS_SCHEDULER_H

#include <stdint.h>

// Initialize scheduler
void scheduler_init(void);

// Add a new kernel thread. func is the entry point (void func(void)).
int scheduler_add_task(void (*func)(void));

// Add a new user process. cr3 is the page table, entry is the RIP, user_rsp is the user stack.
int scheduler_add_user_process(uint64_t cr3, uint64_t entry, uint64_t user_rsp);

// Called from the IRQ stub. 'regs' points to saved register block on stack; returns pointer to registers of next task
void *scheduler_switch(void *regs);

#endif // DANOS_SCHEDULER_H
