#include "elf.h"
#include "fat32.h"
#include "vmm.h"
#include "pmm.h"
#include "kmalloc.h"
#include "string.h"
#include "tty.h"
#include <stdint.h>
#include <stddef.h>

// Simplified ELF types
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t  Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t  Elf64_Sxword;
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;

#define EI_NIDENT 16

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    Elf64_Half    e_type;
    Elf64_Half    e_machine;
    Elf64_Word    e_version;
    Elf64_Addr    e_entry;
    Elf64_Off     e_phoff;
    Elf64_Off     e_shoff;
    Elf64_Word    e_flags;
    Elf64_Half    e_ehsize;
    Elf64_Half    e_phentsize;
    Elf64_Half    e_phnum;
    Elf64_Half    e_shentsize;
    Elf64_Half    e_shnum;
    Elf64_Half    e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    Elf64_Word p_type;
    Elf64_Word p_flags;
    Elf64_Off  p_offset;
    Elf64_Addr p_vaddr;
    Elf64_Addr p_paddr;
    Elf64_Xword p_filesz;
    Elf64_Xword p_memsz;
    Elf64_Xword p_align;
} Elf64_Phdr;

// Program header types
#define PT_NULL 0
#define PT_LOAD 1

// ELF identification indexes
#define EI_MAG0 0
#define EI_MAG1 1
#define EI_MAG2 2
#define EI_MAG3 3
#define EI_CLASS 4
#define EI_DATA 5
#define EI_OSABI 7

// e_ident values
#define ELFMAG0 0x7F
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'
#define ELFCLASS64 2
#define ELFDATA2LSB 1

// e_type
#define ET_NONE 0
#define ET_REL 1
#define ET_EXEC 2
#define ET_DYN 3

// Auxv types
#define AT_NULL 0
#define AT_PHDR 3
#define AT_PHENT 4
#define AT_PHNUM 5
#define AT_ENTRY 9
#define AT_PAGESZ 6

// Page size
#define PAGE_SIZE 4096

// Protection flags mapping: we will map user pages with USER and optionally WRITE
static uint64_t elf_phdr_prot_to_vmm_flags(uint32_t p_flags) {
    uint64_t flags = VMM_PFLAG_USER | VMM_PFLAG_PRESENT;
    if (p_flags & 0x2) flags |= VMM_PFLAG_WRITE; // PF_W
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
    // use kernel-provided strlength if available
    extern int strlength(const char* str);
    return (size_t)strlength(s);
}

// Build a user stack in kernel memory and map into newproc address space
// Returns virtual user stack pointer (rsp) on success, 0 on failure
static uint64_t build_user_stack(struct proc *newproc, char *const argv[], char *const envp[]) {
    // Allocate one page for stack
    void *stack_page = pmm_alloc_page();
    if (!stack_page) return 0;
    // We will map this physical page into the user address space at a conventional stack top: 0x7FFFFFFF000 (near top of canonical user space)
    uint64_t user_stack_top = 0x00007FFFFFFFF000ULL; // keep 4K below top for alignment
    uint64_t user_stack_page = user_stack_top & ~(PAGE_SIZE - 1);

    // Temporarily switch to newproc->cr3 to map
    uint64_t orig_cr3 = vmm_get_cr3();
    vmm_set_cr3(newproc->cr3);
    if (vmm_map_page(user_stack_page, (uint64_t)(uintptr_t)stack_page, VMM_PFLAG_USER | VMM_PFLAG_WRITE) != 0) {
        vmm_set_cr3(orig_cr3);
        return 0;
    }
    // Restore original CR3
    vmm_set_cr3(orig_cr3);

    // Now copy data into the physical page (identity mapped assumption)
    void *kstack = (void *)(uintptr_t)stack_page;
    local_memset(kstack, 0, PAGE_SIZE);

    // Build stack contents: argc, argv pointers, envp pointers, auxv
    // We'll place strings at the top region growing down
    uint64_t sp = user_stack_top;
    // helper to push data
    #define PUSH_U64(x) do { sp -= 8; *(uint64_t *)((char*)kstack + (sp - user_stack_page)) = (uint64_t)(x); } while(0)

    // Copy strings for argv and envp into stack area
    // Collect pointers in kernel arrays first
    int argc = 0;
    while (argv && argv[argc]) argc++;
    int envc = 0;
    while (envp && envp[envc]) envc++;

    // Copy strings — place them below top by chunks
    // We'll place all strings contiguously and store user pointers accordingly
    // Start string area a bit below sp
    uint64_t string_area_end = sp - 128; // leave aux/argv area
    uint64_t string_pos = string_area_end;

    // arrays to hold user pointers
    uint64_t *argv_ptrs = (uint64_t *)kmalloc((argc + 1) * sizeof(uint64_t));
    uint64_t *envp_ptrs = (uint64_t *)kmalloc((envc + 1) * sizeof(uint64_t));
    if ((argc && !argv_ptrs) || (envc && !envp_ptrs)) return 0;

    for (int i = argc - 1; i >= 0; --i) {
        size_t l = local_strlen(argv[i]) + 1;
        string_pos -= l;
        // align
        string_pos &= ~0x7;
        // copy into kstack at offset (string_pos - user_stack_page)
        local_memcpy((char*)kstack + (string_pos - user_stack_page), argv[i], l);
        argv_ptrs[i] = string_pos;
    }
    for (int i = envc - 1; i >= 0; --i) {
        size_t l = local_strlen(envp[i]) + 1;
        string_pos -= l;
        string_pos &= ~0x7;
        local_memcpy((char*)kstack + (string_pos - user_stack_page), envp[i], l);
        envp_ptrs[i] = string_pos;
    }

    // Align stack pointer for pointer area
    sp = string_pos & ~0xF;

    // Push NULL terminator for envp
    PUSH_U64(0);
    // push envp pointers
    for (int i = envc - 1; i >= 0; --i) PUSH_U64(envp_ptrs[i]);
    // Push NULL terminator for argv
    PUSH_U64(0);
    for (int i = argc - 1; i >= 0; --i) PUSH_U64(argv_ptrs[i]);
    // Push argc
    PUSH_U64(argc);

    // Push auxiliary vector entries
    // AT_PHDR, AT_PHENT, AT_PHNUM, AT_ENTRY, AT_PAGESZ, AT_NULL
    PUSH_U64(0); // AT_NULL
    PUSH_U64(0);
    // We will set proper AT_* values by later patching from newproc state; for now put zeros
    // In practice many programs only need AT_PAGESZ and AT_NULL; we set minimal set
    PUSH_U64(AT_PAGESZ);
    PUSH_U64(PAGE_SIZE);
    PUSH_U64(AT_ENTRY);
    PUSH_U64(newproc->entry);
    PUSH_U64(AT_PHNUM);
    PUSH_U64(0);
    PUSH_U64(AT_PHENT);
    PUSH_U64(0);
    PUSH_U64(AT_PHDR);
    PUSH_U64(0);

    // Clean up
    #undef PUSH_U64

    // Free temporary kernel arrays
    if (argv_ptrs) kfree(argv_ptrs);
    if (envp_ptrs) kfree(envp_ptrs);

    // Return stack pointer
    return sp;
}

int elf_load_and_create_address_space(const char *path, char *const argv[], char *const envp[], struct proc *newproc) {
    if (!path || !newproc) return -1;

    fat32_file_t file;
    if (fat32_open_file((const char *)path, &file) != 0) {
        tty_putstr("elf: file open failed\n");
        return -2;
    }

    // Read ELF header
    Elf64_Ehdr ehdr;
    if (fat32_read_file(&file, (uint8_t*)&ehdr, sizeof(Elf64_Ehdr)) != (int)sizeof(Elf64_Ehdr)) {
        tty_putstr("elf: read ehdr failed\n");
        return -3;
    }

    // Validate magic
    if (ehdr.e_ident[EI_MAG0] != ELFMAG0 || ehdr.e_ident[EI_MAG1] != ELFMAG1 || ehdr.e_ident[EI_MAG2] != ELFMAG2 || ehdr.e_ident[EI_MAG3] != ELFMAG3) {
        tty_putstr("elf: invalid magic\n");
        return -4;
    }
    if (ehdr.e_ident[EI_CLASS] != ELFCLASS64) {
        tty_putstr("elf: not ELF64\n");
        return -5;
    }
    if (ehdr.e_ident[EI_DATA] != ELFDATA2LSB) {
        tty_putstr("elf: unsupported endianness\n");
        return -6;
    }

    if (!(ehdr.e_type == ET_EXEC || ehdr.e_type == ET_DYN)) {
        tty_putstr("elf: unsupported type\n");
        return -7;
    }

    // Clone current page table for newproc
    uint64_t orig_cr3 = vmm_get_cr3();
    uint64_t new_cr3 = vmm_clone_table(orig_cr3);
    if (!new_cr3) return -8;
    newproc->cr3 = new_cr3;

    // If ET_DYN (PIE), choose random base aligned to 0x100000
    uint64_t base = 0;
    if (ehdr.e_type == ET_DYN) {
        // derive pseudo-random base from a pmm_alloc_page and its address
        void *r = pmm_alloc_page();
        if (!r) return -9;
        uint64_t rnd = ((uint64_t)(uintptr_t)r) ^ (uint64_t)(&rnd);
        // align to 1MiB
        base = (rnd & 0xFFFFFFFFFFFFF000ULL) & ~0xFFFFFULL;
        if (base < 0x1000000) base += 0x1000000;
    }

    // Iterate program headers
    // Read phdr table
    if (ehdr.e_phoff == 0 || ehdr.e_phnum == 0) return -10;

    // Read all program headers into kernel memory
    size_t phdr_tbl_size = ehdr.e_phnum * ehdr.e_phentsize;
    Elf64_Phdr *phdrs = (Elf64_Phdr *)kmalloc(phdr_tbl_size);
    if (!phdrs) return -11;
    // Seek to phoff in file
    // fat32_read_file reads sequentially; use a fresh open to read from offset
    fat32_open_file(path, &file);
    // skip to e_phoff
    if (ehdr.e_phoff) {
        // read and discard up to phoff
        uint8_t tmpbuf[256];
        uint64_t toskip = ehdr.e_phoff;
        while (toskip) {
            uint32_t r = (toskip > sizeof(tmpbuf)) ? sizeof(tmpbuf) : (uint32_t)toskip;
            int got = fat32_read_file(&file, tmpbuf, r);
            if (got <= 0) { kfree(phdrs); return -12; }
            toskip -= got;
        }
    }
    if (fat32_read_file(&file, (uint8_t*)phdrs, phdr_tbl_size) != (int)phdr_tbl_size) {
        kfree(phdrs);
        return -13;
    }

    // For each PT_LOAD, map pages into newproc and copy data
    for (int i = 0; i < ehdr.e_phnum; ++i) {
        Elf64_Phdr *ph = (Elf64_Phdr *)&phdrs[i];
        if (ph->p_type != PT_LOAD) continue;

        uint64_t seg_vaddr = ph->p_vaddr;
        if (ehdr.e_type == ET_DYN) seg_vaddr += base;

        uint64_t seg_start = page_round_down(seg_vaddr);
        uint64_t seg_end = page_round_up(seg_vaddr + ph->p_memsz);
        uint64_t seg_size = seg_end - seg_start;

        // Map each page: allocate physical page and map into newproc
        uint64_t orig = vmm_get_cr3();
        vmm_set_cr3(newproc->cr3);
        for (uint64_t va = seg_start; va < seg_end; va += PAGE_SIZE) {
            void *page = pmm_alloc_page();
            if (!page) {
                vmm_set_cr3(orig);
                kfree(phdrs);
                return -14;
            }
            if (vmm_map_page(va, (uint64_t)(uintptr_t)page, elf_phdr_prot_to_vmm_flags(ph->p_flags)) != 0) {
                vmm_set_cr3(orig);
                kfree(phdrs);
                return -15;
            }
        }
        vmm_set_cr3(orig);

        // Now copy p_filesz from file into mapped pages
        if (ph->p_filesz) {
            // read segment from file: reopen and skip to offset
            fat32_file_t segf;
            if (fat32_open_file(path, &segf) != 0) { kfree(phdrs); return -16; }
            uint64_t toskip = ph->p_offset;
            uint8_t tmpbuf[256];
            while (toskip) {
                uint32_t r = (toskip > sizeof(tmpbuf)) ? sizeof(tmpbuf) : (uint32_t)toskip;
                int got = fat32_read_file(&segf, tmpbuf, r);
                if (got <= 0) { kfree(phdrs); return -17; }
                toskip -= got;
            }
            uint64_t remaining = ph->p_filesz;
            uint64_t file_off = 0;
            while (remaining) {
                uint32_t chunk = (remaining > sizeof(tmpbuf)) ? sizeof(tmpbuf) : (uint32_t)remaining;
                int got = fat32_read_file(&segf, tmpbuf, chunk);
                if (got <= 0) { kfree(phdrs); return -18; }
                // copy into target virtual address (requires mapping newproc and writing into physical page memory)
                uint64_t dest_v = seg_vaddr + file_off;
                uint64_t page_base = page_round_down(dest_v);
                uint64_t offset_in_page = dest_v - page_base;
                // Find physical page mapped at page_base by temporarily switching to newproc
                uint64_t cr3save = vmm_get_cr3();
                vmm_set_cr3(newproc->cr3);
                // get physical address from page table
                uint64_t *pml4 = (uint64_t *)(uintptr_t)vmm_get_cr3();
                // We know vmm_map_page set the page tables; but easier: compute physical using the identity mapping of physical pages in kernel region: allocated page pointer equals physical address and is directly usable via pointer (pmm_alloc_page returned physical address that is identity-mapped).
                // So we can compute the kernel-accessible pointer as (void*)(uintptr_t)( (pte physical) ) but we don't easily get pte here. Simpler: we used pmm_alloc_page earlier and filled those pages with zeros; since pmm_alloc_page returns physical address which is identity-mapped in this kernel, we can write to that physical address directly.
                // Therefore compute kernel pointer = (void*)(uintptr_t)dest_phys where dest_phys = (value we mapped). But we don't have stored mapping of which physical page corresponded to which vaddr. To avoid complexity, instead write into page by calculating physical base from vmm page tables: walk tables to get entry
                uint64_t dest_phys = 0;
                // walk page tables
                uint64_t *pml4e = (uint64_t *)(uintptr_t)vmm_get_cr3();
                size_t i4 = (page_base >> 39) & 0x1FF;
                uint64_t pml4val = pml4e[i4];
                if (!(pml4val & VMM_PFLAG_PRESENT)) { vmm_set_cr3(cr3save); kfree(phdrs); return -19; }
                uint64_t *pdpe = (uint64_t *)(uintptr_t)(pml4val & 0x000ffffffffff000ULL);
                size_t i3 = (page_base >> 30) & 0x1FF;
                uint64_t pdpval = pdpe[i3];
                if (!(pdpval & VMM_PFLAG_PRESENT)) { vmm_set_cr3(cr3save); kfree(phdrs); return -20; }
                uint64_t *pde = (uint64_t *)(uintptr_t)(pdpval & 0x000ffffffffff000ULL);
                size_t i2 = (page_base >> 21) & 0x1FF;
                uint64_t pdeval = pde[i2];
                if (!(pdeval & VMM_PFLAG_PRESENT)) { vmm_set_cr3(cr3save); kfree(phdrs); return -21; }
                uint64_t *pte = (uint64_t *)(uintptr_t)(pdeval & 0x000ffffffffff000ULL);
                size_t i1 = (page_base >> 12) & 0x1FF;
                uint64_t pteval = pte[i1];
                if (!(pteval & VMM_PFLAG_PRESENT)) { vmm_set_cr3(cr3save); kfree(phdrs); return -22; }
                dest_phys = pteval & 0x000ffffffffff000ULL;
                void *kptr = (void *)(uintptr_t)dest_phys;
                // copy chunk to kptr + offset_in_page
                local_memcpy((char*)kptr + offset_in_page, tmpbuf, got);
                vmm_set_cr3(cr3save);

                remaining -= got;
                file_off += got;
            }
            // Zero remaining bss portion (p_memsz - p_filesz)
            if (ph->p_memsz > ph->p_filesz) {
                uint64_t bss_start = seg_vaddr + ph->p_filesz;
                uint64_t bss_end = seg_vaddr + ph->p_memsz;
                uint64_t cur = bss_start;
                while (cur < bss_end) {
                    uint64_t page_base = page_round_down(cur);
                    uint64_t offset_in_page = cur - page_base;
                    uint64_t tozero = (bss_end - cur);
                    if (tozero > PAGE_SIZE - offset_in_page) tozero = PAGE_SIZE - offset_in_page;
                    uint64_t cr3save = vmm_get_cr3();
                    vmm_set_cr3(newproc->cr3);
                    // walk to get phys
                    uint64_t *pml4e = (uint64_t *)(uintptr_t)vmm_get_cr3();
                    size_t i4 = (page_base >> 39) & 0x1FF;
                    uint64_t pml4val = pml4e[i4];
                    uint64_t *pdpe = (uint64_t *)(uintptr_t)(pml4val & 0x000ffffffffff000ULL);
                    size_t i3 = (page_base >> 30) & 0x1FF;
                    uint64_t pdpval = pdpe[i3];
                    uint64_t *pde = (uint64_t *)(uintptr_t)(pdpval & 0x000ffffffffff000ULL);
                    size_t i2 = (page_base >> 21) & 0x1FF;
                    uint64_t pdeval = pde[i2];
                    uint64_t *pte = (uint64_t *)(uintptr_t)(pdeval & 0x000ffffffffff000ULL);
                    size_t i1 = (page_base >> 12) & 0x1FF;
                    uint64_t pteval = pte[i1];
                    uint64_t dest_phys = pteval & 0x000ffffffffff000ULL;
                    void *kptr = (void *)(uintptr_t)dest_phys;
                    local_memset((char*)kptr + offset_in_page, 0, tozero);
                    vmm_set_cr3(cr3save);
                    cur += tozero;
                }
            }
        }
    }

    // Set entry point (add base for ET_DYN)
    newproc->entry = ehdr.e_entry + (ehdr.e_type == ET_DYN ? base : 0);

    // Build user stack and set newproc->user_rsp
    uint64_t user_rsp = build_user_stack(newproc, argv, envp);
    if (!user_rsp) {
        kfree(phdrs);
        return -23;
    }
    newproc->user_rsp = user_rsp;

    kfree(phdrs);
    return 0;
}
