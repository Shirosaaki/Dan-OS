#ifndef DANOS_VMM_H
#define DANOS_VMM_H

#include <stdint.h>
#include <stddef.h>

// Page flags
#define VMM_PFLAG_PRESENT  (1ULL << 0)
#define VMM_PFLAG_WRITE    (1ULL << 1)
#define VMM_PFLAG_USER     (1ULL << 2)

// Initialize virtual memory manager. Assumes paging already enabled by bootloader.
void vmm_init(void);

// Map a single 4KiB page: vaddr -> paddr with flags (combination of VMM_PFLAG_*)
int vmm_map_page(uint64_t vaddr, uint64_t paddr, uint64_t flags);

// Unmap a single page at vaddr (does not free the physical page)
int vmm_unmap_page(uint64_t vaddr);

// Clone the top-level page table (copy-on-write not implemented; this does a shallow copy of entries)
uint64_t vmm_clone_table(uint64_t src_cr3);

// Map a page in a different page table (without switching CR3)
int vmm_map_page_in_table(uint64_t target_cr3, uint64_t vaddr, uint64_t paddr, uint64_t flags);

// Get current CR3
uint64_t vmm_get_cr3(void);

// Set current CR3
void vmm_set_cr3(uint64_t cr3);

#endif // DANOS_VMM_H
