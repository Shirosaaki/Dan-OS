// Migrated kmalloc into src/kernel/sys
#include <kernel/sys/kmalloc.h>
#include <kernel/arch/x86_64/pmm.h>
#include <kernel/arch/x86_64/vmm.h>
#include <kernel/sys/string.h>
#include <stdint.h>

#define KERNEL_HEAP_BASE 0xFFFF800000000000ULL
#define PAGE_SIZE 4096
#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

typedef struct block_header {
    size_t size;
    struct block_header *next;
    int free;
} block_header_t;

static uintptr_t heap_start = (uintptr_t)KERNEL_HEAP_BASE;
static uintptr_t heap_end = (uintptr_t)KERNEL_HEAP_BASE;
static block_header_t *free_list = NULL;

static int heap_expand(size_t bytes) {
    if (bytes == 0) return 0;
    size_t need = ALIGN_UP(bytes, PAGE_SIZE);
    size_t pages = need / PAGE_SIZE;
    for (size_t i = 0; i < pages; ++i) {
        void *p = pmm_alloc_page();
        if (!p) return -1;
        uintptr_t pa = (uintptr_t)p;
        uintptr_t va = heap_end + i * PAGE_SIZE;
        if (vmm_map_page(va, pa, VMM_PFLAG_WRITE) != 0) {
            pmm_free_page((void *)pa);
            for (size_t j = 0; j < i; ++j) vmm_unmap_page(heap_end + j * PAGE_SIZE);
            return -1;
        }
    }
    heap_end += pages * PAGE_SIZE;
    if (!free_list) {
        block_header_t *hdr = (block_header_t *)heap_start;
        hdr->size = (heap_end - heap_start) - sizeof(block_header_t);
        hdr->next = NULL;
        hdr->free = 1;
        free_list = hdr;
    } else {
        block_header_t *hdr = (block_header_t *)(heap_end - pages * PAGE_SIZE);
        hdr->size = (pages * PAGE_SIZE) - sizeof(block_header_t);
        hdr->free = 1;
        hdr->next = NULL;
        block_header_t *cur = free_list;
        while (cur->next) cur = cur->next;
        cur->next = hdr;
    }
    return 0;
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;
    size_t asize = ALIGN_UP(size, 8);
    size_t total = asize + sizeof(block_header_t);
    block_header_t *prev = NULL;
    block_header_t *cur = free_list;
    while (cur) {
        if (cur->free && cur->size >= asize) {
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
        prev = cur;
        cur = cur->next;
    }
    size_t expand_bytes = ALIGN_UP(total, PAGE_SIZE);
    if (expand_bytes == 0) expand_bytes = PAGE_SIZE;
    if (heap_expand(expand_bytes) != 0) return NULL;
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
    block_header_t *hdr = (block_header_t *)((uintptr_t)ptr - sizeof(block_header_t));
    hdr->free = 1;
    block_header_t *cur = free_list;
    while (cur && cur->next) {
        if (cur->free && cur->next->free) {
            cur->size += sizeof(block_header_t) + cur->next->size;
            cur->next = cur->next->next;
            continue;
        }
        cur = cur->next;
    }
}

void *kmalloc_aligned(size_t size, size_t alignment) {
    if (size == 0 || alignment == 0) return NULL;
    if (alignment < 8) alignment = 8;
    size_t total = size + alignment + sizeof(void*);
    void *raw = kmalloc(total);
    if (!raw) return NULL;
    uintptr_t raw_addr = (uintptr_t)raw;
    uintptr_t aligned_addr = (raw_addr + sizeof(void*) + alignment - 1) & ~(alignment - 1);
    *((void**)(aligned_addr - sizeof(void*))) = raw;
    return (void*)aligned_addr;
}

void kfree_aligned(void *ptr) {
    if (!ptr) return;
    void *raw = *((void**)((uintptr_t)ptr - sizeof(void*)));
    kfree(raw);
}
