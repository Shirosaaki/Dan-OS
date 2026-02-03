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

// Helper to build a 64-bit GDT descriptor
static uint64_t build_desc(uint32_t base, uint32_t limit, uint8_t access, uint8_t flags) {
    uint64_t desc = 0;
    desc  = (uint64_t)(limit & 0xFFFF);
    desc |= (uint64_t)(base & 0xFFFF) << 16;
    desc |= (uint64_t)((base >> 16) & 0xFF) << 32;
    desc |= (uint64_t)access << 40;
    desc |= (uint64_t)((limit >> 16) & 0xF) << 48;
    desc |= (uint64_t)(flags & 0xF) << 52;
    desc |= (uint64_t)((base >> 24) & 0xFF) << 56;
    return desc;
}

void gdt_init() {
    // Null descriptor
    gdt[0] = 0;
    // use file-scope build_desc

    // Kernel code segment (ring 0, executable, 64-bit): access=0x9A, flags upper nibble=(G=1,L=1)->0xA
    gdt[1] = build_desc(0, 0xFFFF, 0x9A, 0xA);

    // Kernel data segment (ring 0, data): access=0x92, flags upper nibble=(G=1,D/B=1)->0xC
    gdt[2] = build_desc(0, 0xFFFF, 0x92, 0xC);

    // User data segment (ring 3, data): access=0xF2 (DPL=3)
    // Placed at index 3 to support SYSRET (expects SS at Base+8 = 16+8 = 24? No. Base=0x10. SS=0x18, CS=0x20)
    // Wait. If Base=0x10. Base+8 = 0x18 => Index 3. Base+16=0x20 => Index 4.
    // So Index 3 MUST be User Data. Index 4 MUST be User Code.
    gdt[3] = build_desc(0, 0xFFFF, 0xF2, 0xC);

    // User code segment (ring 3, executable, 64-bit): access=0xFA (DPL=3)
    gdt[4] = build_desc(0, 0xFFFF, 0xFA, 0xA);

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