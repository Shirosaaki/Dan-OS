#ifndef DANOS_PMM_H
#define DANOS_PMM_H

#include <stddef.h>
#include <stdint.h>

#define PMM_PAGE_SIZE 4096

// memory_map is the multiboot2 info pointer (raw pointer to tags)
void pmm_init(void *multiboot_info, size_t memory_size);

// Allocate a single physical page (4096 bytes). Returns physical address or 0 on failure.
void *pmm_alloc_page(void);

// Free a previously allocated physical page at physical address 'addr'
void pmm_free_page(void *addr);

// Helpers for testing/inspection
size_t pmm_total_pages(void);
size_t pmm_free_pages(void);

#endif // DANOS_PMM_H
