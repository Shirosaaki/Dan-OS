// Simple kernel heap allocator interface
#ifndef DANOS_KMALLOC_H
#define DANOS_KMALLOC_H

#include <stddef.h>

// Allocate `size` bytes from the kernel heap. Returns NULL on failure.
void *kmalloc(size_t size);

// Free a previously allocated pointer from kmalloc
void kfree(void *ptr);

#endif // DANOS_KMALLOC_H
