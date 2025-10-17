#include "kmalloc.h"
#include "pmm.h"
#include "vmm.h"
#include "string.h"
#include <stdint.h>

// Heap base: canonical higher-half address region reserved for kernel heap
#define KERNEL_HEAP_BASE 0xFFFF800000000000ULL
#define PAGE_SIZE 4096
#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

// Minimal block header for free list
typedef struct block_header {
    size_t size; // size of the payload in bytes
    struct block_header *next;
    int free;
} block_header_t;

// Start of heap virtual region and current mapped end
static uintptr_t heap_start = (uintptr_t)KERNEL_HEAP_BASE;
static uintptr_t heap_end = (uintptr_t)KERNEL_HEAP_BASE; // one past last mapped byte
static block_header_t *free_list = NULL;

// Internal: request more pages from PMM and map them into the heap
// Request at least `bytes` more bytes (rounded up to pages). Returns 0 on success, -1 on failure.
static int heap_expand(size_t bytes) {
    if (bytes == 0) return 0;
    size_t need = ALIGN_UP(bytes, PAGE_SIZE);
    size_t pages = need / PAGE_SIZE;

    for (size_t i = 0; i < pages; ++i) {
        void *p = pmm_alloc_page();
        if (!p) return -1;
        uintptr_t pa = (uintptr_t)p;
        uintptr_t va = heap_end + i * PAGE_SIZE;
        // Map with writeable, kernel (not user)
        if (vmm_map_page(va, pa, VMM_PFLAG_WRITE) != 0) {
            // map failed -> free allocated physical page and abort
            pmm_free_page((void *)pa);
            // unmap any pages mapped in this loop (best-effort)
            for (size_t j = 0; j < i; ++j) {
                vmm_unmap_page(heap_end + j * PAGE_SIZE);
            }
            return -1;
        }
    }

    heap_end += pages * PAGE_SIZE;
    // If free_list is empty, initialize with a single big free block for the new region
    if (!free_list) {
        block_header_t *hdr = (block_header_t *)heap_start;
        hdr->size = (heap_end - heap_start) - sizeof(block_header_t);
        hdr->next = NULL;
        hdr->free = 1;
        free_list = hdr;
    } else {
        // Append a free block at previous end
        block_header_t *hdr = (block_header_t *)(heap_end - pages * PAGE_SIZE);
        hdr->size = (pages * PAGE_SIZE) - sizeof(block_header_t);
        hdr->next = NULL;
        hdr->free = 1;

        // Find tail of free_list chain and append
        block_header_t *cur = free_list;
        while (cur->next) cur = cur->next;
        cur->next = hdr;
    }
    return 0;
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;
    // align payload to 8 bytes
    size_t asize = ALIGN_UP(size, 8);
    // include header
    size_t total = asize + sizeof(block_header_t);

    // First-fit search
    block_header_t *prev = NULL;
    block_header_t *cur = free_list;
    while (cur) {
        if (cur->free && cur->size >= asize) {
            // if remainder is big enough, split
            size_t remainder = cur->size - asize;
            if (remainder > sizeof(block_header_t) + 8) {
                // split
                block_header_t *newb = (block_header_t *)((uintptr_t)cur + sizeof(block_header_t) + asize);
                newb->size = remainder - sizeof(block_header_t);
                newb->free = 1;
                newb->next = cur->next;
                cur->size = asize;
                cur->next = newb;
            }
            cur->free = 0;
            // return pointer to payload
            return (void *)((uintptr_t)cur + sizeof(block_header_t));
        }
        prev = cur;
        cur = cur->next;
    }

    // No suitable block; expand heap by at least total bytes
    // Choose expansion amount: multiple of page size, at least one page
    size_t expand_bytes = ALIGN_UP(total, PAGE_SIZE);
    if (expand_bytes == 0) expand_bytes = PAGE_SIZE;
    if (heap_expand(expand_bytes) != 0) return NULL;

    // After expansion, retry allocation (simple: allocate from tail block)
    // Find block at tail (we appended new free block in heap_expand)
    cur = free_list;
    while (cur && cur->next) cur = cur->next;
    if (!cur || !cur->free || cur->size < asize) return NULL;

    size_t remainder = cur->size - asize;
    if (remainder > sizeof(block_header_t) + 8) {
        block_header_t *newb = (block_header_t *)((uintptr_t)cur + sizeof(block_header_t) + asize);
        newb->size = remainder - sizeof(block_header_t);
        newb->free = 1;
        newb->next = cur->next;
        cur->size = asize;
        cur->next = newb;
    }
    cur->free = 0;
    return (void *)((uintptr_t)cur + sizeof(block_header_t));
}

void kfree(void *ptr) {
    if (!ptr) return;
    // find header
    block_header_t *hdr = (block_header_t *)((uintptr_t)ptr - sizeof(block_header_t));
    hdr->free = 1;

    // coalesce adjacent free blocks in the list
    block_header_t *cur = free_list;
    while (cur && cur->next) {
        if (cur->free && cur->next->free) {
            // merge cur and cur->next
            cur->size += sizeof(block_header_t) + cur->next->size;
            cur->next = cur->next->next;
            // continue without advancing to try to merge multiple in a row
            continue;
        }
        cur = cur->next;
    }
}
