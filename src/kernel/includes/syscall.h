/**==============================================
 *                 syscall.h
 *  syscall header
 *  Author: shirosaaki
 *  Date: 2025-12-05
 *=============================================**/

#ifndef SYSCALL_H_
#define SYSCALL_H_

#include <stdint.h>
#include <stddef.h>

// Syscall numbers (Linux x86-64 compatible for basic ones)
#define SYS_READ      0
#define SYS_WRITE     1
#define SYS_OPEN      2
#define SYS_CLOSE     3
#define SYS_STAT      4
#define SYS_FSTAT     5
#define SYS_LSEEK     8
#define SYS_MMAP      9
#define SYS_MUNMAP    11
#define SYS_BRK       12
#define SYS_GETPID    39
#define SYS_FORK      57
#define SYS_EXEC      59
#define SYS_EXIT      60
#define SYS_WAIT      61
#define SYS_MKDIR     83
#define SYS_RMDIR     84
#define SYS_UNLINK    87
#define SYS_CHDIR     80
#define SYS_GETCWD    79
#define SYS_NANOSLEEP 35

// Custom Dan-OS syscalls (256+)
#define SYS_GETLINE   256
#define SYS_PUTCHAR   257
#define SYS_GETCHAR   258
#define SYS_TIME      259

// File open flags
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0100
#define O_TRUNC     0x0200
#define O_APPEND    0x0400

// Seek whence values
#define SEEK_SET    0
#define SEEK_CUR    1
#define SEEK_END    2

// Maximum number of open files
#define MAX_OPEN_FILES  16

// File descriptor structure
typedef struct {
    int      in_use;
    uint32_t first_cluster;
    uint32_t file_size;
    uint32_t current_pos;
    uint32_t current_cluster;
    uint8_t  attributes;
    int      flags;
    char     filename[64];
} file_descriptor_t;

// Stat structure
typedef struct {
    uint32_t st_size;
    uint8_t  st_mode;
    uint16_t st_ctime;
    uint16_t st_mtime;
    uint16_t st_atime;
} stat_t;

// MSR addresses for syscall/sysret
#define MSR_STAR         0xC0000081
#define MSR_LSTAR        0xC0000082
#define MSR_CSTAR        0xC0000083
#define MSR_SYSCALL_MASK 0xC0000084

// Initialize syscall subsystem (setup MSRs and fd table)
void syscall_init(void);

// Syscall handler (called from syscall entry point)
int64_t syscall_handler(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, 
                        uint64_t arg3, uint64_t arg4, uint64_t arg5);

// Assembly entry point for syscall instruction
extern void syscall_entry(void);

// Individual syscall implementations
int64_t sys_read(int fd, void* buf, size_t count);
int64_t sys_write(int fd, const void* buf, size_t count);
int64_t sys_open(const char* pathname, int flags);
int64_t sys_close(int fd);
int64_t sys_getline(char* buf, size_t max_len);
int64_t sys_putchar(char c);
int64_t sys_getchar(void);
void    sys_exit(int status);
int64_t sys_exec(const char* path, char* const argv[]);
int64_t sys_fork(void);
int64_t sys_wait(int* status);
int64_t sys_getpid(void);
int64_t sys_stat(const char* pathname, stat_t* statbuf);
int64_t sys_mkdir(const char* pathname);
int64_t sys_rmdir(const char* pathname);
int64_t sys_unlink(const char* pathname);
int64_t sys_chdir(const char* path);
int64_t sys_getcwd(char* buf, size_t size);
int64_t sys_sleep(uint32_t milliseconds);
int64_t sys_time(void);
int64_t sys_seek(int fd, int64_t offset, int whence);

#endif /* !SYSCALL_H_ */
