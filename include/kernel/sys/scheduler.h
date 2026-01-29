#ifndef DANOS_SCHEDULER_H
#define DANOS_SCHEDULER_H

#include <stdint.h>

// Initialize scheduler
void scheduler_init(void);

// Add a new kernel thread. func is the entry point (void func(void)).
int scheduler_add_task(void (*func)(void));

// Called from the IRQ stub. 'regs' points to saved register block on stack; returns pointer to registers of next task
void *scheduler_switch(void *regs);

#endif // DANOS_SCHEDULER_H
