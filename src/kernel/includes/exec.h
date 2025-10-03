#ifndef EXEC_H
#define EXEC_H

#include "../../cpu/types.h"

// Simple executable header format for DanOS
typedef struct {
    u32 magic;           // Magic number 0x44414E58 ("DANX")
    u32 code_size;       // Size of code section
    u32 data_size;       // Size of data section
    u32 entry_point;     // Entry point offset
    u32 version;         // Format version
    u32 string_count;    // Number of strings in string table
} exec_header_t;

#define EXEC_MAGIC 0x44414E58  // "DANX"
#define EXEC_VERSION 1

// Memory regions for user programs
#define USER_CODE_BASE 0x400000    // 4MB
#define USER_DATA_BASE 0x500000    // 5MB
#define USER_STACK_BASE 0x600000   // 6MB
#define USER_STACK_SIZE 0x10000    // 64KB

// Function prototypes
int exec_load_file(const char* filename);
int exec_run(void* code_base, u32 code_size, void* data_base, u32 data_size, u32 entry_offset, u32 string_count);
void exec_init(void);

// System call interface
#define SYSCALL_PRINT 0
#define SYSCALL_EXIT 1
#define SYSCALL_GETCHAR 2
#define SYSCALL_OPEN 3
#define SYSCALL_READ 4
#define SYSCALL_WRITE 5
#define SYSCALL_CLOSE 6

void syscall_handler(u32 syscall_num, u32 arg1, u32 arg2, u32 arg3);

#endif // EXEC_H
