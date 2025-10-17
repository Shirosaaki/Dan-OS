// Minimal ELF loader interface and proc definition
#ifndef DANOS_ELF_H
#define DANOS_ELF_H

#include <stdint.h>

// Minimal process structure used by loader
struct proc {
    uint64_t cr3;      // page table base (physical)
    uint64_t entry;    // user RIP
    uint64_t user_rsp; // user RSP
};

// Load an ELF64 binary from filesystem and create an address space for `newproc`.
// On success returns 0, sets newproc->entry and newproc->user_rsp and newproc->cr3.
// See implementation comments for error codes (<0).
int elf_load_and_create_address_space(const char *path, char *const argv[], char *const envp[], struct proc *newproc);

#endif // DANOS_ELF_H
