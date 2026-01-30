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

// ELF structures
typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} Elf64_Shdr;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t r_addend;
} Elf64_Rela;

// ELF header and program header types used by the loader
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t  Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t  Elf64_Sxword;
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;

typedef struct {
    unsigned char e_ident[16];
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

#define ELF64_R_TYPE(info) ((info) & 0xFFFFFFFF)
#define SHT_RELA 4
#define R_X86_64_RELATIVE 8

#endif // DANOS_ELF_H
