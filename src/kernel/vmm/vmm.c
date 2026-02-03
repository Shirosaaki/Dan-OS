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
    // Create a fresh PML4 for user process with NO kernel mappings
    // This prevents user code from accessing kernel memory
    uint64_t *new_pml4 = alloc_table();
    if (!new_pml4) return 0;

    // Start with a completely empty page table
    // User processes will explicitly map what they need via ELF loader
    // We do NOT copy kernel mappings - this is more secure
    
    return (uint64_t)(uintptr_t)new_pml4;
}

// Map a page in a DIFFERENT page table (specified by target_cr3) without switching CR3
// This is needed because we can't switch to the new process's CR3 to modify its page tables
// (since the new page tables might not have identity mapping for themselves)
int vmm_map_page_in_table(uint64_t target_cr3, uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    // We'll walk the target page table using physical addresses
    // Since the kernel has identity mapping in low memory, we can access physical addresses directly
    
    uint64_t *pml4 = (uint64_t *)(uintptr_t)target_cr3;
    
    // Get or create PDP
    size_t i4 = idx_pml4(vaddr);
    uint64_t *pdp;
    if (pml4[i4] & VMM_PFLAG_PRESENT) {
        pml4[i4] |= (VMM_PFLAG_USER | VMM_PFLAG_WRITE);
        pdp = (uint64_t *)(uintptr_t)(pml4[i4] & ENTRY_ADDR_MASK);
    } else {
        pdp = alloc_table();
        if (!pdp) return -1;
        pml4[i4] = ((uint64_t)(uintptr_t)pdp & ENTRY_ADDR_MASK) | VMM_PFLAG_PRESENT | VMM_PFLAG_WRITE | VMM_PFLAG_USER;
    }
    
    // Get or create PD
    size_t i3 = idx_pdp(vaddr);
    uint64_t *pd;
    if (pdp[i3] & VMM_PFLAG_PRESENT) {
        pdp[i3] |= (VMM_PFLAG_USER | VMM_PFLAG_WRITE);
        pd = (uint64_t *)(uintptr_t)(pdp[i3] & ENTRY_ADDR_MASK);
    } else {
        pd = alloc_table();
        if (!pd) return -1;
        pdp[i3] = ((uint64_t)(uintptr_t)pd & ENTRY_ADDR_MASK) | VMM_PFLAG_PRESENT | VMM_PFLAG_WRITE | VMM_PFLAG_USER;
    }
    
    // Get or create PT
    size_t i2 = idx_pd(vaddr);
    uint64_t *pt;
    if (pd[i2] & VMM_PFLAG_PRESENT) {
        pd[i2] |= (VMM_PFLAG_USER | VMM_PFLAG_WRITE);
        pt = (uint64_t *)(uintptr_t)(pd[i2] & ENTRY_ADDR_MASK);
    } else {
        pt = alloc_table();
        if (!pt) return -1;
        pd[i2] = ((uint64_t)(uintptr_t)pt & ENTRY_ADDR_MASK) | VMM_PFLAG_PRESENT | VMM_PFLAG_WRITE | VMM_PFLAG_USER;
    }
    
    // Set the leaf PTE
    size_t i1 = idx_pt(vaddr);
    pt[i1] = (paddr & ENTRY_ADDR_MASK) | (flags & 0xFFF) | VMM_PFLAG_PRESENT;
    
    return 0;
}
