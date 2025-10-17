#ifndef DANOS_PMM_H
#define DANOS_PMM_H

#include <stddef.h>
#include <stdint.h>

#define PMM_PAGE_SIZE 4096

void pmm_init(void *multiboot_info, size_t memory_size);
void *pmm_alloc_page(void);
void pmm_free_page(void *addr);
size_t pmm_total_pages(void);
size_t pmm_free_pages(void);

// Find first zero bit (free page) starting at start_idx. Returns index or (size_t)-1 if none.
size_t ffs64_find_zero(size_t start_idx);

#endif // DANOS_PMM_H
