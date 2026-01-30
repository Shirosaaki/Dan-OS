/* src/kernel/arch/x86_64/vmm.c */
#include <kernel/arch/x86_64/vmm.h>
#include <kernel/arch/x86_64/pmm.h>
#include <stdint.h>
#include <stddef.h>
#include <kernel/sys/string.h>

// Each page table is 4096 bytes and contains 512 8-byte entries
#define ENTRIES_PER_TABLE 512

// Standard x86_64 paging flags
#ifndef VMM_PFLAG_PRESENT
#define VMM_PFLAG_PRESENT 0x1
#endif
#ifndef VMM_PFLAG_WRITE
#define VMM_PFLAG_WRITE   0x2
#endif
#ifndef VMM_PFLAG_USER
#define VMM_PFLAG_USER    0x4
#endif

// Allocate a fresh zeroed page table
static uint64_t *alloc_table(void) {
    void *p = pmm_alloc_page();
    if (!p) return NULL;
    // p is a physical address; we assume identity mapping at early boot so we can access it
    uint64_t *table = (uint64_t *)(uintptr_t)p;
    // zero it
    for (int i = 0; i < ENTRIES_PER_TABLE; ++i) table[i] = 0;
    return table;
}

// Helpers to index into the 4 levels
static inline size_t idx_pml4(uint64_t v) { return (v >> 39) & 0x1FF; }
static inline size_t idx_pdp(uint64_t v) { return (v >> 30) & 0x1FF; }
static inline size_t idx_pd(uint64_t v)  { return (v >> 21) & 0x1FF; }
static inline size_t idx_pt(uint64_t v)  { return (v >> 12) & 0x1FF; }

// Mask to extract physical address from entry
#define ENTRY_ADDR_MASK 0x000ffffffffff000ULL

// Read CR3
uint64_t vmm_get_cr3(void) {
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r" (cr3));
    return cr3;
}

// Write CR3
void vmm_set_cr3(uint64_t cr3) {
    __asm__ volatile ("mov %0, %%cr3" :: "r" (cr3));
}

void vmm_init(void) {
    // Nothing required if bootloader already created identity page tables and enabled paging
}

// Ensure the physical page for 'entry' exists and return pointer to its table
// FIX: Added VMM_PFLAG_USER to allow user-mode traversal of this table hierarchy.
static uint64_t *ensure_table(uint64_t *entry) {
    if ((*entry) & VMM_PFLAG_PRESENT) {
        // If entry exists, ensure it has User and Write permissions so user code can reach the leaves
        *entry |= (VMM_PFLAG_USER | VMM_PFLAG_WRITE);
        
        uint64_t pa = (*entry) & ENTRY_ADDR_MASK;
        return (uint64_t *)(uintptr_t)pa;
    }
    
    uint64_t *tbl = alloc_table();
    if (!tbl) return NULL;
    
    uint64_t pa = (uint64_t)(uintptr_t)tbl;
    // Map new directory entry with Present, Write, and User permissions
    *entry = (pa & ENTRY_ADDR_MASK) | VMM_PFLAG_PRESENT | VMM_PFLAG_WRITE | VMM_PFLAG_USER;
    
    return (uint64_t *)(uintptr_t)pa;
}

int vmm_map_page(uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    uint64_t *pml4 = (uint64_t *)(uintptr_t)vmm_get_cr3();
    
    size_t i4 = idx_pml4(vaddr);
    uint64_t *pdp = ensure_table(&pml4[i4]); 
    if (!pdp) return -1;
    
    size_t i3 = idx_pdp(vaddr);
    uint64_t *pd = ensure_table(&pdp[i3]); 
    if (!pd) return -1;
    
    size_t i2 = idx_pd(vaddr);
    uint64_t *pt = ensure_table(&pd[i2]); 
    if (!pt) return -1;
    
    size_t i1 = idx_pt(vaddr);
    
    // Set the leaf PTE with the specific flags requested (e.g., code might not be writable)
    uint64_t entry = (paddr & ENTRY_ADDR_MASK) | (flags & 0xFFF) | VMM_PFLAG_PRESENT;
    pt[i1] = entry;
    
    // Flush TLB for that page
    __asm__ volatile ("invlpg (%0)" :: "r" (vaddr) : "memory");
    return 0;
}

int vmm_unmap_page(uint64_t vaddr) {
    uint64_t *pml4 = (uint64_t *)(uintptr_t)vmm_get_cr3();
    
    size_t i4 = idx_pml4(vaddr);
    uint64_t pml4e = pml4[i4]; 
    if (!(pml4e & VMM_PFLAG_PRESENT)) return -1;
    
    uint64_t *pdp = (uint64_t *)(uintptr_t)(pml4e & ENTRY_ADDR_MASK);
    size_t i3 = idx_pdp(vaddr);
    uint64_t pdpe = pdp[i3]; 
    if (!(pdpe & VMM_PFLAG_PRESENT)) return -1;
    
    uint64_t *pd = (uint64_t *)(uintptr_t)(pdpe & ENTRY_ADDR_MASK);
    size_t i2 = idx_pd(vaddr);
    uint64_t pde = pd[i2]; 
    if (!(pde & VMM_PFLAG_PRESENT)) return -1;
    
    uint64_t *pt = (uint64_t *)(uintptr_t)(pde & ENTRY_ADDR_MASK);
    size_t i1 = idx_pt(vaddr);
    
    pt[i1] = 0;
    
    __asm__ volatile ("invlpg (%0)" :: "r" (vaddr) : "memory");
    return 0;
}

uint64_t vmm_clone_table(uint64_t src_cr3) {
    // Shallow clone: allocate new PML4 and copy entries. 
    // Does not clone lower levels (they remain shared).
    uint64_t *src = (uint64_t *)(uintptr_t)src_cr3;
    uint64_t *new_table = alloc_table();
    if (!new_table) return 0;

    for (int i = 0; i < ENTRIES_PER_TABLE; ++i) {
        new_table[i] = src[i];
    }

    return (uint64_t)(uintptr_t)new_table;
}
