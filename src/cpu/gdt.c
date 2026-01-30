/**==============================================
 *                 gdt.c
 *  gdt
 *  Author: Shirosaaki
 *  Date: 2026-01-30
 *=============================================**/
#include <cpu/gdt.h>
#include <kernel/sys/string.h>

uint64_t gdt[7];
struct gdt_ptr gdt_ptr;
struct tss_entry tss;

void gdt_init() {
    // Null descriptor
    gdt[0] = 0;

    // Kernel code segment (ring 0, executable, 64-bit)
    gdt[1] = 0x0020980000000000;

    // Kernel data segment (ring 0, data)
    gdt[2] = 0x0000920000000000;

    // User code segment (ring 3, executable, 64-bit)
    gdt[3] = 0x0020F80000000000;

    // User data segment (ring 3, data)
    gdt[4] = 0x0000F20000000000;

    // TSS descriptor (spans two entries)
    uint64_t tss_base = (uint64_t)&tss;
    uint32_t tss_limit = sizeof(struct tss_entry) - 1;
    gdt[5] = (tss_limit & 0xFFFF) |
             ((tss_base & 0xFFFF) << 16) |
             (((tss_base >> 16) & 0xFF) << 32) |
             (0x89ULL << 40) |
             ((uint64_t)(tss_limit >> 16 & 0xF) << 48) |
             (((tss_base >> 24) & 0xFF) << 56);
    gdt[6] = (tss_base >> 32);

    // Initialize TSS
    memset_k(&tss, 0, sizeof(struct tss_entry));
    tss.iopb_offset = sizeof(struct tss_entry);

    // Load GDT
    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base = (uint64_t)&gdt;
    __asm__ volatile("lgdt %0" : : "m"(gdt_ptr));

    // Load Task Register
    __asm__ volatile("ltr %0" : : "r"((uint16_t)0x28));
}

void tss_set_stack(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}