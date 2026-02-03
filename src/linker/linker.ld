ENTRY(start);
SECTIONS
{
    . = 1M;

    /* Provide symbols to allow the kernel to know its physical bounds
       so the physical memory manager can reserve kernel pages. */
     __kernel_start = .;
     /* Provide single-underscore aliases for toolchains/objects that expect
         symbol names with a single leading underscore. */
     _kernel_start = __kernel_start;
    .boot :
    {
        KEEP(*(.header))
    }
    .text :
    {
        *(.text)
    }
    .rodata :
    {
        *(.rodata)
    }
    .data :
    {
        *(.data)
    }
    .bss :
    {
        *(.bss)
    }
    __kernel_end = .;
    _kernel_end = __kernel_end;
}