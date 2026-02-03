#include <kernel/drivers/elf.h>
#include <kernel/fs/fat32.h>
#include <kernel/arch/x86_64/vmm.h>
#include <kernel/arch/x86_64/pmm.h>
#include <kernel/sys/kmalloc.h>
#include <kernel/sys/string.h>
#include <kernel/sys/tty.h>
#include <stdint.h>
#include <stddef.h>

#define PT_LOAD 1
#define ELFMAG0 0x7F
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'
#define ELFCLASS64 2
#define PAGE_SIZE 4096

// Simple page allocation tracker
typedef struct {
    uint64_t va;
    uint64_t pa;
} va_pa_mapping_t;

#define MAX_SEGMENTS 16
static va_pa_mapping_t segment_map[MAX_SEGMENTS];
static int segment_count = 0;

static void map_add(uint64_t va, uint64_t pa) {
    if (segment_count < MAX_SEGMENTS) {
        segment_map[segment_count].va = va;
        segment_map[segment_count].pa = pa;
        segment_count++;
    }
}

static uint64_t map_lookup(uint64_t va) {
    for (int i = 0; i < segment_count; i++) {
        uint64_t page_va = segment_map[i].va & ~(PAGE_SIZE - 1);
        uint64_t va_page = va & ~(PAGE_SIZE - 1);
        if (page_va == va_page) {
            return segment_map[i].pa + (va & (PAGE_SIZE - 1));
        }
    }
    return 0;
}

int elf_load_and_create_address_space(const char *path, char *const argv[], char *const envp[], struct proc *newproc) {
    tty_putstr("[ELF] Loading: ");
    tty_putstr(path);
    tty_putstr("\n");

    if (!path || !newproc) return -1;

    segment_count = 0;  // Reset mapping

    // Open and read ELF header
    fat32_file_t file;
    if (fat32_open_file(path, &file) != 0) {
        tty_putstr("[ELF] File open failed\n");
        return -2;
    }

    Elf64_Ehdr ehdr;
    if (fat32_read_file(&file, (uint8_t*)&ehdr, sizeof(Elf64_Ehdr)) != sizeof(Elf64_Ehdr)) {
        tty_putstr("[ELF] Header read failed\n");
        return -3;
    }

    // Validate ELF header
    if (ehdr.e_ident[0] != ELFMAG0 || ehdr.e_ident[1] != ELFMAG1 || 
        ehdr.e_ident[2] != ELFMAG2 || ehdr.e_ident[3] != ELFMAG3) {
        tty_putstr("[ELF] Invalid magic\n");
        return -4;
    }
    if (ehdr.e_ident[4] != ELFCLASS64) {
        tty_putstr("[ELF] Not 64-bit\n");
        return -5;
    }

    tty_putstr("[ELF] Entry: ");
    tty_puthex64(ehdr.e_entry);
    tty_putstr("\n");

    // Get kernel CR3 and clone it for user process
    uint64_t kernel_cr3 = vmm_get_cr3();
    uint64_t user_cr3 = vmm_clone_table(kernel_cr3);
    if (!user_cr3) {
        tty_putstr("[ELF] Clone table failed\n");
        return -6;
    }
    newproc->cr3 = user_cr3;

    // Read program headers
    size_t phdr_size = ehdr.e_phnum * ehdr.e_phentsize;
    Elf64_Phdr *phdrs = (Elf64_Phdr *)kmalloc(phdr_size);
    if (!phdrs) {
        tty_putstr("[ELF] Phdr malloc failed\n");
        return -7;
    }

    // Seek to program headers
    fat32_file_t file2;
    fat32_open_file(path, &file2);
    if (ehdr.e_phoff > 0) {
        uint8_t skipbuf[512];
        uint64_t skip = ehdr.e_phoff;
        while (skip > 0) {
            int to_skip = (skip > 512) ? 512 : skip;
            fat32_read_file(&file2, skipbuf, to_skip);
            skip -= to_skip;
        }
    }
    fat32_read_file(&file2, (uint8_t*)phdrs, phdr_size);

    // Process each PT_LOAD segment
    for (unsigned seg_idx = 0; seg_idx < ehdr.e_phnum; seg_idx++) {
        Elf64_Phdr *ph = &phdrs[seg_idx];
        if (ph->p_type != PT_LOAD) continue;

        uint64_t seg_va = ph->p_vaddr;
        uint64_t seg_end = seg_va + ph->p_memsz;

        tty_putstr("[ELF] Load segment: va=");
        tty_puthex64(seg_va);
        tty_putstr(" size=");
        tty_putdec(ph->p_memsz);
        tty_putstr("\n");

        // Allocate physical pages for the segment
        for (uint64_t va = seg_va; va < seg_end; va += PAGE_SIZE) {
            void *phys = pmm_alloc_page();
            if (!phys) {
                tty_putstr("[ELF] PMM alloc failed\n");
                kfree(phdrs);
                return -8;
            }

            uint64_t pa = (uint64_t)(uintptr_t)phys;
            
            // Zero the page
            memset_k(phys, 0, PAGE_SIZE);

            // Map it in user page table with proper permissions
            // For executable segments: READ + EXECUTE (not WRITE) + USER
            // For writable segments: READ + WRITE + USER
            uint64_t flags = 0x1 | 0x4;  // PRESENT | USER
            if (ph->p_flags & 0x2) flags |= 0x2;  // WRITE if needed

            if (vmm_map_page_in_table(user_cr3, va, pa, flags) != 0) {
                tty_putstr("[ELF] Map page failed at va=");
                tty_puthex64(va);
                tty_putstr("\n");
                kfree(phdrs);
                return -9;
            }

            map_add(va, pa);
        }

        // Copy file data into pages
        if (ph->p_filesz > 0) {
            fat32_file_t data_file;
            fat32_open_file(path, &data_file);

            // Seek to file offset
            if (ph->p_offset > 0) {
                uint8_t skipbuf[512];
                uint64_t skip = ph->p_offset;
                while (skip > 0) {
                    int to_skip = (skip > 512) ? 512 : skip;
                    fat32_read_file(&data_file, skipbuf, to_skip);
                    skip -= to_skip;
                }
            }

            // Copy data
            uint64_t remaining = ph->p_filesz;
            uint8_t data_buf[512];

            while (remaining > 0) {
                int to_read = (remaining > 512) ? 512 : remaining;
                int got = fat32_read_file(&data_file, data_buf, to_read);
                if (got <= 0) break;

                // Find where in virtual address space we are
                uint64_t file_va = seg_va + (ph->p_filesz - remaining);
                
                // Look up physical address
                uint64_t file_pa = map_lookup(file_va);
                if (!file_pa) {
                    tty_putstr("[ELF] VA not mapped: ");
                    tty_puthex64(file_va);
                    tty_putstr("\n");
                    remaining -= got;
                    continue;
                }

                // Copy to physical address (kernel identity-mapped)
                uint8_t *dest = (uint8_t *)(uintptr_t)file_pa;
                memset_k(dest, 0, got);  // Clear first
                for (int i = 0; i < got; i++) {
                    dest[i] = data_buf[i];
                }

                remaining -= got;
            }
        }
    }

    kfree(phdrs);
    newproc->entry = ehdr.e_entry;

    // Allocate stack at high user address
    void *stack_phys = pmm_alloc_page();
    if (!stack_phys) {
        tty_putstr("[ELF] Stack alloc failed\n");
        return -10;
    }

    memset_k(stack_phys, 0, PAGE_SIZE);

    uint64_t stack_va = 0x7FFFFFFFF000ULL;
    uint64_t stack_pa = (uint64_t)(uintptr_t)stack_phys;

    // Map stack as PRESENT | WRITE | USER
    if (vmm_map_page_in_table(user_cr3, stack_va, stack_pa, 0x1 | 0x2 | 0x4) != 0) {
        tty_putstr("[ELF] Stack map failed\n");
        return -11;
    }

    // Set stack pointer (16-byte aligned at top)
    uint64_t stack_top = stack_va + PAGE_SIZE;
    newproc->user_rsp = (stack_top - 16) & ~0xFULL;

    tty_putstr("[ELF] Done: entry=");
    tty_puthex64(newproc->entry);
    tty_putstr(" rsp=");
    tty_puthex64(newproc->user_rsp);
    tty_putstr("\n");

    return 0;
}
