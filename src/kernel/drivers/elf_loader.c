#include <kernel/drivers/elf.h>
#include <kernel/fs/fat32.h>
#include <kernel/arch/x86_64/vmm.h>
#include <kernel/arch/x86_64/pmm.h>
#include <kernel/sys/kmalloc.h>
#include <kernel/sys/string.h>
#include <kernel/sys/tty.h>
#include <stdint.h>
#include <stddef.h>

// ELF types and structures are provided by include/kernel/drivers/elf.h
// PT_LOAD remains defined here for program header type
#define PT_LOAD 1

#define ELFMAG0 0x7F
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define ET_EXEC 2
#define ET_DYN 3

#define AT_NULL 0
#define AT_PHDR 3
#define AT_PHENT 4
#define AT_PHNUM 5
#define AT_PAGESZ 6
#define AT_ENTRY 9

#define PAGE_SIZE 4096

// Protection flags mapping: we will map user pages with USER and optionally WRITE
static uint64_t elf_phdr_prot_to_vmm_flags(uint32_t p_flags) {
    uint64_t flags = VMM_PFLAG_USER | VMM_PFLAG_PRESENT;
    if (p_flags & 0x2) flags |= VMM_PFLAG_WRITE;
    return flags;
}

// Helper: round down/up to PAGE_SIZE
static inline uint64_t page_round_down(uint64_t v) { return v & ~(PAGE_SIZE - 1); }
static inline uint64_t page_round_up(uint64_t v) { return (v + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1); }

// Simple memcpy/memset/strlen implementations to avoid relying on libc
static void *local_memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}
static void *local_memset(void *s, int c, size_t n) {
    unsigned char *p = (unsigned char *)s;
    for (size_t i = 0; i < n; ++i) p[i] = (unsigned char)c;
    return s;
}
static size_t local_strlen(const char *s) {
    extern int strlength(const char* str);
    return (size_t)strlength(s);
}

// Build a user stack in kernel memory and map into newproc address space
// Returns virtual user stack pointer (rsp) on success, 0 on failure
static uint64_t build_user_stack(struct proc *newproc, char *const argv[], char *const envp[]) {
    // 1. Allocate a physical page for the stack
    void *stack_page = pmm_alloc_page();
    if (!stack_page) {
        tty_putstr("[ELF] pmm_alloc_page for stack failed\n");
        return 0;
    }

    // 2. Define the Virtual Address for the stack page.
    // We map the page at 0x7FFFFFFFF000. 
    // The top of the stack will be at 0x800000000000 (end of this page).
    uint64_t user_stack_page_base = 0x00007FFFFFFFF000ULL;
    uint64_t user_stack_top_limit = user_stack_page_base + PAGE_SIZE;

    // 3. Map it into the user process address space
    uint64_t orig_cr3 = vmm_get_cr3();
    vmm_set_cr3(newproc->cr3);
    if (vmm_map_page(user_stack_page_base, (uint64_t)(uintptr_t)stack_page, VMM_PFLAG_USER | VMM_PFLAG_WRITE) != 0) {
        vmm_set_cr3(orig_cr3);
        return 0;
    }
    vmm_set_cr3(orig_cr3);

    // 4. Populate stack data: map the physical page into a temporary kernel VA so we can write to it
    const uint64_t KERNEL_TEMP_VA = 0xFFFF800000F00000ULL;
    vmm_set_cr3(orig_cr3);
    if (vmm_map_page(KERNEL_TEMP_VA, (uint64_t)(uintptr_t)stack_page, VMM_PFLAG_WRITE) != 0) {
        // cleanup mapping in newproc
        vmm_set_cr3(newproc->cr3);
        vmm_unmap_page(user_stack_page_base);
        vmm_set_cr3(orig_cr3);
        return 0;
    }
    void *kstack_base = (void *)(uintptr_t)KERNEL_TEMP_VA;
    local_memset(kstack_base, 0, PAGE_SIZE);

    // sp represents the current stack pointer value (Virtual Address)
    // We start at the very top of the allocated page.
    uint64_t sp = user_stack_top_limit;

    // Helper macro to calculate offset within the kernel page
    #define KSTACK_PTR(virt_addr) ((char*)kstack_base + ((virt_addr) - user_stack_page_base))

    // Helper to push 64-bit value
    #define PUSH_U64(x) do { \
        sp -= 8; \
        *(uint64_t *)KSTACK_PTR(sp) = (uint64_t)(x); \
    } while(0)

    // Calculate counts
    int argc = 0;
    while (argv && argv[argc]) argc++;
    int envc = 0;
    while (envp && envp[envc]) envc++;

    // --- Push Strings (argv/envp data) ---
    // We allocate temporary arrays to hold the pointers to these strings
    uint64_t *argv_ptrs = (uint64_t *)kmalloc((argc + 1) * sizeof(uint64_t));
    uint64_t *envp_ptrs = (uint64_t *)kmalloc((envc + 1) * sizeof(uint64_t));
    
    // Push environment strings
    for (int i = envc - 1; i >= 0; --i) {
        size_t l = local_strlen(envp[i]) + 1;
        sp -= l;
        sp &= ~0xF; // Align strings? Not strictly required by ABI but good practice or just align 1
        local_memcpy(KSTACK_PTR(sp), envp[i], l);
        envp_ptrs[i] = sp;
    }
    
    // Push argument strings
    for (int i = argc - 1; i >= 0; --i) {
        size_t l = local_strlen(argv[i]) + 1;
        sp -= l;
        sp &= ~0xF; 
        local_memcpy(KSTACK_PTR(sp), argv[i], l);
        argv_ptrs[i] = sp;
    }

    // Align stack pointer to 16 bytes before pushing array pointers
    sp &= ~0xF;

    // --- Push Auxv ---
    // (AT_NULL must be at the bottom of aux array)
    PUSH_U64(0); PUSH_U64(AT_NULL);
    PUSH_U64(PAGE_SIZE); PUSH_U64(AT_PAGESZ);
    PUSH_U64(newproc->entry); PUSH_U64(AT_ENTRY);
    
    // --- Push Envp Pointers ---
    PUSH_U64(0); // NULL terminator
    for (int i = envc - 1; i >= 0; --i) PUSH_U64(envp_ptrs[i]);

    // --- Push Argv Pointers ---
    PUSH_U64(0); // NULL terminator
    for (int i = argc - 1; i >= 0; --i) PUSH_U64(argv_ptrs[i]);

    // --- Push Argc ---
    PUSH_U64(argc);

    // Cleanup
    if (argv_ptrs) kfree(argv_ptrs);
    if (envp_ptrs) kfree(envp_ptrs);

    // Unmap temporary kernel mapping
    vmm_unmap_page(KERNEL_TEMP_VA);

    return sp;
}

int elf_load_and_create_address_space(const char *path, char *const argv[], char *const envp[], struct proc *newproc) {
    if (!path || !newproc) return -1;

    fat32_file_t file;
    if (fat32_open_file((const char *)path, &file) != 0) {
        tty_putstr("elf: file open failed\n");
        return -2;
    }

    Elf64_Ehdr ehdr;
    if (fat32_read_file(&file, (uint8_t*)&ehdr, sizeof(Elf64_Ehdr)) != (int)sizeof(Elf64_Ehdr)) {
        return -3;
    }

    if (ehdr.e_ident[0] != ELFMAG0 || ehdr.e_ident[1] != ELFMAG1 || 
        ehdr.e_ident[2] != ELFMAG2 || ehdr.e_ident[3] != ELFMAG3) return -4;
    if (ehdr.e_ident[4] != ELFCLASS64) return -5;

    uint64_t orig_cr3 = vmm_get_cr3();
    uint64_t new_cr3 = vmm_clone_table(orig_cr3);
    if (!new_cr3) return -8;
    newproc->cr3 = new_cr3;

    uint64_t base = (ehdr.e_type == ET_DYN) ? 0x400000 : 0;

    size_t phdr_tbl_size = ehdr.e_phnum * ehdr.e_phentsize;
    Elf64_Phdr *phdrs = (Elf64_Phdr *)kmalloc(phdr_tbl_size);
    if (!phdrs) return -11;

    fat32_open_file(path, &file); // Reopen to reset offset
    
    // Skip to Program Headers
    if (ehdr.e_phoff) {
        uint8_t tmp[256];
        uint64_t skip = ehdr.e_phoff;
        while(skip > 0) {
            int r = (skip > 256) ? 256 : skip;
            fat32_read_file(&file, tmp, r);
            skip -= r;
        }
    }
    fat32_read_file(&file, (uint8_t*)phdrs, phdr_tbl_size);

    // Load Segments
    for (int i = 0; i < ehdr.e_phnum; ++i) {
        Elf64_Phdr *ph = (Elf64_Phdr *)((uintptr_t)phdrs + i * ehdr.e_phentsize);
        if (ph->p_type != PT_LOAD) continue;

        uint64_t seg_vaddr = ph->p_vaddr + base;
        uint64_t seg_start = page_round_down(seg_vaddr);
        uint64_t seg_end = page_round_up(seg_vaddr + ph->p_memsz);

        // Map pages
        vmm_set_cr3(newproc->cr3);
        for (uint64_t va = seg_start; va < seg_end; va += PAGE_SIZE) {
            void *page = pmm_alloc_page();
            vmm_map_page(va, (uint64_t)(uintptr_t)page, elf_phdr_prot_to_vmm_flags(ph->p_flags));
        }
        vmm_set_cr3(orig_cr3);

        // Copy Data
        if (ph->p_filesz > 0) {
            fat32_file_t segf;
            fat32_open_file(path, &segf);
            
            // Skip to p_offset
            uint64_t skip = ph->p_offset;
            uint8_t tmp[512];
            while(skip > 0) {
                int r = (skip > 512) ? 512 : skip;
                fat32_read_file(&segf, tmp, r);
                skip -= r;
            }

            uint64_t remaining = ph->p_filesz;
            uint64_t offset = 0;
            while (remaining > 0) {
                int r = (remaining > 512) ? 512 : remaining;
                int got = fat32_read_file(&segf, tmp, r);

                uint64_t target_va = seg_vaddr + offset;

                // Find the physical page backing target_va in the new proc page tables
                vmm_set_cr3(newproc->cr3);
                const uint64_t KERNEL_PHYS_OFFSET = 0xFFFF800000000000ULL;
                uint64_t *pml4 = (uint64_t*)(uintptr_t)(vmm_get_cr3() + KERNEL_PHYS_OFFSET);
                uint64_t pml4e = pml4[(target_va >> 39) & 0x1FF];
                uint64_t *pdp = (uint64_t*)(uintptr_t)(pml4e & 0xFFFFFFFFFF000ULL);
                uint64_t pdpe = pdp[(target_va >> 30) & 0x1FF];
                uint64_t *pd = (uint64_t*)(uintptr_t)(pdpe & 0xFFFFFFFFFF000ULL);
                uint64_t pde = pd[(target_va >> 21) & 0x1FF];
                uint64_t *pt = (uint64_t*)(uintptr_t)(pde & 0xFFFFFFFFFF000ULL);
                uint64_t pte = pt[(target_va >> 12) & 0x1FF];
                uint64_t phys_page = pte & 0xFFFFFFFFFF000ULL;
                vmm_set_cr3(orig_cr3);

                if (phys_page == 0) {
                    remaining -= got;
                    offset += got;
                    continue;
                }

                const uint64_t KERNEL_TEMP_VA = 0xFFFF800000F00000ULL;
                // Map the physical page into kernel virtual space so we can write into it
                vmm_set_cr3(orig_cr3);
                tty_putstr("[ELF DBG] mapping temp VA "); tty_puthex64(KERNEL_TEMP_VA);
                tty_putstr(" -> phys "); tty_puthex64(phys_page); tty_putstr("\n");
                if (vmm_map_page(KERNEL_TEMP_VA, phys_page, VMM_PFLAG_WRITE) != 0) {
                    tty_putstr("[ELF DBG] vmm_map_page failed\n");
                    remaining -= got;
                    offset += got;
                    continue;
                }
                // Verify mapping by walking kernel page tables
                {
                    const uint64_t KOFFSET = 0xFFFF800000000000ULL;
                    uint64_t cr3kv = vmm_get_cr3() + KOFFSET;
                    uint64_t *pml4 = (uint64_t *)(uintptr_t)cr3kv;
                    size_t i4 = (KERNEL_TEMP_VA >> 39) & 0x1FF;
                    uint64_t pml4e = pml4[i4];
                    tty_putstr("[ELF DBG] pml4e="); tty_puthex64(pml4e); tty_putstr("\n");
                }
                local_memcpy((void*)(uintptr_t)(KERNEL_TEMP_VA + (target_va & 0xFFF)), tmp, got);
                vmm_unmap_page(KERNEL_TEMP_VA);

                remaining -= got;
                offset += got;
            }
        }
        vmm_set_cr3(orig_cr3);
    }

    newproc->entry = ehdr.e_entry + base;

    // FIX: Use the RSP returned by build_user_stack, do NOT overwrite it with top-of-page.
    newproc->user_rsp = build_user_stack(newproc, argv, envp);
    
    if (newproc->user_rsp == 0) {
        kfree(phdrs);
        return -23;
    }

    kfree(phdrs);
    return 0;
}
