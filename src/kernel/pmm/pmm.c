#include <kernel/arch/x86_64/pmm.h>
#include <stdint.h>
#include <stddef.h>
#include <kernel/sys/string.h> // for mem* helpers if available; fallback to local
#include <kernel/sys/tty.h>

// Simple bitmap physical memory manager.
// Layout:
//  - We'll scan Multiboot2 memory map tags to find usable regions.
//  - Create a bitmap where each bit represents one 4KiB page.
//  - The bitmap itself will be placed in a reserved memory region (we'll use first usable region after kernel), but for simplicity
//    we'll allocate it from a static area in the kernel binary (compile-time array) large enough for moderately sized RAM (e.g., 128 MiB).

#define MAX_RAM_BYTES (256 * 1024 * 1024ULL) // support up to 256 MiB physical for bitmap sizing convenience
#define MAX_PAGES (MAX_RAM_BYTES / PMM_PAGE_SIZE)
#define BITMAP_SIZE_BYTES ((MAX_PAGES + 7) / 8)

static uint8_t pmm_bitmap[BITMAP_SIZE_BYTES];
static size_t total_pages = 0;
static size_t free_pages = 0;
static uintptr_t physical_memory_base = 0; // lowest physical address tracked by bitmap (aligned to page)

// Kernel-provided symbols from linker script
extern uint8_t __kernel_start;
extern uint8_t __kernel_end;

// Helpers
static inline void bitmap_set(size_t idx) { pmm_bitmap[idx >> 3] |= (1 << (idx & 7)); }
static inline void bitmap_clear(size_t idx) { pmm_bitmap[idx >> 3] &= ~(1 << (idx & 7)); }
static inline int bitmap_test(size_t idx) { return (pmm_bitmap[idx >> 3] >> (idx & 7)) & 1; }

// Multiboot2 tag parsing helpers
struct mb2_tag { uint32_t type; uint32_t size; };
static inline uint32_t read_u32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static inline uint64_t read_u64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= ((uint64_t)p[8 + i]) << (i*8);
    return v;
}

void pmm_init(void *multiboot_info, size_t memory_size) {
    // Zero bitmap
    for (size_t i = 0; i < BITMAP_SIZE_BYTES; ++i) pmm_bitmap[i] = 0xFF; // mark all used first

    if (!multiboot_info) {
        // fallback: assume small memory from 1M..MAX_RAM_BYTES
        physical_memory_base = 0x100000; // 1MiB
        total_pages = (MAX_RAM_BYTES - physical_memory_base) / PMM_PAGE_SIZE;
        free_pages = 0;
        return;
    }

    uint8_t *mb = (uint8_t *)multiboot_info;
    uint32_t total_size = read_u32(mb);
    if (total_size == 0) return;

    // We'll scan for memory map (tag type 6) which is multiboot2 memory map
    uint32_t offset = 8;
    uintptr_t highest_addr = 0;
    uintptr_t lowest_addr = (uintptr_t)-1;

    // First pass to find max physical address so we know how many pages to track
    while (offset + 8 <= total_size) {
        struct mb2_tag *tag = (struct mb2_tag *)(mb + offset);
        if (tag->type == 0) break;
        if (tag->type == 6) {
            uint8_t *t = mb + offset;
            uint32_t entry_size = read_u32(t + 8);
            uint32_t entry_version = read_u32(t + 12);
            uint32_t pos = 16;
            while (pos + entry_size <= tag->size) {
                uint64_t base = 0;
                for (int i = 0; i < 8; ++i) base |= ((uint64_t)t[16 + pos - 16 + i]) << (i*8);
                uint64_t len = 0;
                for (int i = 0; i < 8; ++i) len |= ((uint64_t)t[16 + pos - 16 + 8 + i]) << (i*8);
                uint32_t type = (uint32_t)t[16 + pos - 16 + 16];
                if (type == 1) { // available RAM
                    if (base < lowest_addr) lowest_addr = base;
                    if (base + len > highest_addr) highest_addr = base + len;
                }
                pos += entry_size;
            }
        }
        uint32_t sz = tag->size;
        if (sz == 0) break;
        offset += (sz + 7) & ~7u;
    }

    if (highest_addr == 0 || lowest_addr == (uintptr_t)-1) {
        // no usable regions found; default
        physical_memory_base = 0x100000;
        total_pages = (MAX_RAM_BYTES - physical_memory_base) / PMM_PAGE_SIZE;
        for (size_t i = 0; i < total_pages; ++i) bitmap_set(i); // mark used
        free_pages = 0;
        return;
    }

    // Cap tracking to MAX_RAM_BYTES
    if (highest_addr > MAX_RAM_BYTES) highest_addr = MAX_RAM_BYTES;
    if (lowest_addr > highest_addr) lowest_addr = 0x100000;

    physical_memory_base = lowest_addr & ~(PMM_PAGE_SIZE - 1);
    total_pages = (highest_addr - physical_memory_base) / PMM_PAGE_SIZE;
    if (total_pages > MAX_PAGES) total_pages = MAX_PAGES;

    // Initially mark all pages used; we'll mark available pages free below
    for (size_t i = 0; i < total_pages; ++i) bitmap_set(i);
    free_pages = 0;

    // Second pass: mark available regions as free, but skip reserved regions and kernel area
    offset = 8;
    while (offset + 8 <= total_size) {
        struct mb2_tag *tag = (struct mb2_tag *)(mb + offset);
        if (tag->type == 0) break;
        if (tag->type == 6) {
            uint8_t *t = mb + offset;
            uint32_t entry_size = read_u32(t + 8);
            uint32_t entry_version = read_u32(t + 12);
            uint32_t pos = 16;
            while (pos + entry_size <= tag->size) {
                uint64_t base = 0;
                for (int i = 0; i < 8; ++i) base |= ((uint64_t)t[16 + pos - 16 + i]) << (i*8);
                uint64_t len = 0;
                for (int i = 0; i < 8; ++i) len |= ((uint64_t)t[16 + pos - 16 + 8 + i]) << (i*8);
                uint32_t type = (uint32_t)t[16 + pos - 16 + 16];
                if (type == 1) { // available RAM
                    uintptr_t start = (uintptr_t)base;
                    uintptr_t end = (uintptr_t)(base + len);
                    // clamp to our tracked area
                    if (end <= physical_memory_base) { pos += entry_size; continue; }
                    if (start < physical_memory_base) start = physical_memory_base;
                    if (start >= physical_memory_base + total_pages * PMM_PAGE_SIZE) { pos += entry_size; continue; }
                    if (end > physical_memory_base + total_pages * PMM_PAGE_SIZE) end = physical_memory_base + total_pages * PMM_PAGE_SIZE;
                    size_t pstart = (start - physical_memory_base) / PMM_PAGE_SIZE;
                    size_t pend = (end - physical_memory_base + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE; // one past
                    for (size_t p = pstart; p < pend && p < total_pages; ++p) {
                        // Reserve kernel pages
                        uintptr_t page_phys = physical_memory_base + p * PMM_PAGE_SIZE;
                        if (page_phys >= (uintptr_t)&__kernel_start && page_phys < (uintptr_t)&__kernel_end) {
                            bitmap_set(p);
                        } else {
                            bitmap_clear(p);
                            free_pages++;
                        }
                    }
                }

                pos += entry_size;
            }
        }
        uint32_t sz = tag->size;
        if (sz == 0) break;
        offset += (sz + 7) & ~7u;
    }

    // Mark first 1MB reserved
    if (physical_memory_base == 0x100000) {
        size_t reserve_pages = (0x100000 - physical_memory_base) / PMM_PAGE_SIZE;
        for (size_t p = 0; p < reserve_pages && p < total_pages; ++p) {
            if (!bitmap_test(p)) { bitmap_set(p); free_pages--; }
        }
    }

}

void *pmm_alloc_page(void) {
    if (free_pages == 0) return 0;
    for (size_t i = 0; i < total_pages; ++i) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            free_pages--;
            return (void *)(uintptr_t)(physical_memory_base + i * PMM_PAGE_SIZE);
        }
    }
    return 0;
}

void pmm_free_page(void *addr) {
    if (!addr) return;
    uintptr_t a = (uintptr_t)addr;
    if (a < physical_memory_base) return;
    size_t idx = (a - physical_memory_base) / PMM_PAGE_SIZE;
    if (idx >= total_pages) return;
    if (!bitmap_test(idx)) {
        // double free
        return;
    }
    bitmap_clear(idx);
    free_pages++;
}

size_t pmm_total_pages(void) { return total_pages; }
size_t pmm_free_pages(void) { return free_pages; }

// External helper used by some code paths: find first zero bit in bitmap starting at idx
// Return index of bit found or -1 (size_t max) if none.
size_t ffs64_find_zero(size_t start_idx) {
    for (size_t i = start_idx; i < total_pages; ++i) {
        if (!bitmap_test(i)) return i;
    }
    return (size_t)-1;
}
