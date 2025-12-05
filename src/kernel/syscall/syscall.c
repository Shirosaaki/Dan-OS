/**==============================================
 *                 syscall.c
 *  syscall file
 *  Author: shirosaaki
 *  Date: 2025-12-05
 *=============================================**/

#include "syscall.h"
#include "tty.h"
#include "keyboard.h"
#include "fat32.h"
#include "kmalloc.h"
#include "rtc.h"
#include "scheduler.h"
#include "string.h"

// File descriptor table
static file_descriptor_t fd_table[MAX_OPEN_FILES];

// Standard file descriptors
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

// Helper functions for MSR access
static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

// Initialize syscall subsystem
void syscall_init(void) {
    // Clear file descriptor table
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        fd_table[i].in_use = 0;
    }
    
    // Reserve stdin, stdout, stderr
    fd_table[STDIN_FILENO].in_use = 1;
    fd_table[STDIN_FILENO].flags = O_RDONLY;
    
    fd_table[STDOUT_FILENO].in_use = 1;
    fd_table[STDOUT_FILENO].flags = O_WRONLY;
    
    fd_table[STDERR_FILENO].in_use = 1;
    fd_table[STDERR_FILENO].flags = O_WRONLY;
    
    // Setup syscall/sysret MSRs
    // MSR_STAR: bits 32-47 = kernel CS (0x08), bits 48-63 = user CS (0x1B for ring 3)
    // On sysret: CS = STAR[48:63] + 16, SS = STAR[48:63] + 8
    // On syscall: CS = STAR[32:47], SS = STAR[32:47] + 8
    uint64_t star = ((uint64_t)0x08 << 32) | ((uint64_t)0x1B << 48);
    wrmsr(MSR_STAR, star);
    
    // MSR_LSTAR: syscall entry point (64-bit mode)
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    
    // MSR_SYSCALL_MASK: clear IF (interrupt flag) on syscall entry
    wrmsr(MSR_SYSCALL_MASK, 0x200); // Disable interrupts during syscall
}

// Syscall dispatcher
int64_t syscall_handler(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, 
                        uint64_t arg3, uint64_t arg4, uint64_t arg5) {
    (void)arg4;
    (void)arg5;
    
    switch (syscall_num) {
        case SYS_READ:      // 0
            return sys_read((int)arg1, (void*)arg2, (size_t)arg3);
        case SYS_WRITE:     // 1
            return sys_write((int)arg1, (const void*)arg2, (size_t)arg3);
        case SYS_OPEN:      // 2
            return sys_open((const char*)arg1, (int)arg2);
        case SYS_CLOSE:     // 3
            return sys_close((int)arg1);
        case SYS_STAT:      // 4
            return sys_stat((const char*)arg1, (stat_t*)arg2);
        case SYS_LSEEK:     // 8
            return sys_seek((int)arg1, (int64_t)arg2, (int)arg3);
        case SYS_BRK:       // 12 - not implemented, return 0
            return 0;
        case SYS_NANOSLEEP: // 35
            return sys_sleep((uint32_t)(arg1 / 1000000)); // Convert ns to ms
        case SYS_GETPID:    // 39
            return sys_getpid();
        case SYS_FORK:      // 57
            return sys_fork();
        case SYS_EXEC:      // 59
            return sys_exec((const char*)arg1, (char* const*)arg2);
        case SYS_EXIT:      // 60
            sys_exit((int)arg1);
            return 0;
        case SYS_WAIT:      // 61
            return sys_wait((int*)arg1);
        case SYS_GETCWD:    // 79
            return sys_getcwd((char*)arg1, (size_t)arg2);
        case SYS_CHDIR:     // 80
            return sys_chdir((const char*)arg1);
        case SYS_MKDIR:     // 83
            return sys_mkdir((const char*)arg1);
        case SYS_RMDIR:     // 84
            return sys_rmdir((const char*)arg1);
        case SYS_UNLINK:    // 87
            return sys_unlink((const char*)arg1);
        
        // Custom Dan-OS syscalls
        case SYS_GETLINE:   // 256
            return sys_getline((char*)arg1, (size_t)arg2);
        case SYS_PUTCHAR:   // 257
            return sys_putchar((char)arg1);
        case SYS_GETCHAR:   // 258
            return sys_getchar();
        case SYS_TIME:      // 259
            return sys_time();
            
        default:
            return -1; // Unknown syscall
    }
}

// Find a free file descriptor
static int find_free_fd(void) {
    for (int i = 3; i < MAX_OPEN_FILES; i++) { // Start from 3 (after stdin/stdout/stderr)
        if (!fd_table[i].in_use) {
            return i;
        }
    }
    return -1; // No free file descriptors
}

/**
 * sys_read - Read from a file descriptor
 * @fd: file descriptor
 * @buf: buffer to read into
 * @count: number of bytes to read
 * @return: number of bytes read, or -1 on error
 */
int64_t sys_read(int fd, void* buf, size_t count) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || !fd_table[fd].in_use) {
        return -1;
    }
    
    if (buf == NULL || count == 0) {
        return 0;
    }
    
    // Handle stdin
    if (fd == STDIN_FILENO) {
        char* cbuf = (char*)buf;
        size_t i = 0;
        while (i < count) {
            char c = keyboard_getchar();
            if (c != 0) {
                cbuf[i++] = c;
                if (c == '\n') break;
            }
        }
        return (int64_t)i;
    }
    
    // Handle file read
    file_descriptor_t* file = &fd_table[fd];
    
    // Check if we can read from this fd
    if ((file->flags & O_WRONLY) && !(file->flags & O_RDWR)) {
        return -1;
    }
    
    // Calculate how much we can actually read
    size_t bytes_remaining = file->file_size - file->current_pos;
    size_t to_read = (count < bytes_remaining) ? count : bytes_remaining;
    
    if (to_read == 0) {
        return 0; // EOF
    }
    
    // Create a temporary fat32_file_t for reading
    fat32_file_t temp_file;
    temp_file.first_cluster = file->first_cluster;
    temp_file.file_size = file->file_size;
    temp_file.current_pos = file->current_pos;
    temp_file.current_cluster = file->current_cluster;
    temp_file.attributes = file->attributes;
    
    int bytes_read = fat32_read_file(&temp_file, (uint8_t*)buf, (uint32_t)to_read);
    
    if (bytes_read > 0) {
        file->current_pos = temp_file.current_pos;
        file->current_cluster = temp_file.current_cluster;
    }
    
    return bytes_read;
}

/**
 * sys_write - Write to a file descriptor
 * @fd: file descriptor
 * @buf: buffer to write from
 * @count: number of bytes to write
 * @return: number of bytes written, or -1 on error
 */
int64_t sys_write(int fd, const void* buf, size_t count) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || !fd_table[fd].in_use) {
        return -1;
    }
    
    if (buf == NULL || count == 0) {
        return 0;
    }
    
    // Handle stdout/stderr
    if (fd == STDOUT_FILENO || fd == STDERR_FILENO) {
        const char* cbuf = (const char*)buf;
        for (size_t i = 0; i < count; i++) {
            tty_putchar(cbuf[i]);
        }
        return (int64_t)count;
    }
    
    // Handle file write
    file_descriptor_t* file = &fd_table[fd];
    
    // Check if we can write to this fd
    if ((file->flags & O_RDONLY) && !(file->flags & O_RDWR)) {
        return -1;
    }
    
    // Use fat32_update_file to write the data
    // Note: This is a simple implementation that overwrites the entire file
    int result = fat32_update_file(file->filename, (const uint8_t*)buf, (uint32_t)count);
    
    if (result == 0) {
        file->file_size = (uint32_t)count;
        file->current_pos = (uint32_t)count;
        return (int64_t)count;
    }
    
    return -1;
}

/**
 * sys_open - Open a file
 * @pathname: path to the file
 * @flags: open flags (O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, etc.)
 * @return: file descriptor, or -1 on error
 */
int64_t sys_open(const char* pathname, int flags) {
    if (pathname == NULL) {
        return -1;
    }
    
    int fd = find_free_fd();
    if (fd < 0) {
        return -1; // No free file descriptors
    }
    
    fat32_file_t file;
    int result = fat32_open_file(pathname, &file);
    
    if (result != 0) {
        // File doesn't exist
        if (flags & O_CREAT) {
            // Create the file
            if (fat32_create_file(pathname, NULL, 0) != 0) {
                return -1;
            }
            // Try opening again
            result = fat32_open_file(pathname, &file);
            if (result != 0) {
                return -1;
            }
        } else {
            return -1;
        }
    }
    
    // Handle O_TRUNC - truncate file to zero length
    if (flags & O_TRUNC) {
        fat32_update_file(pathname, NULL, 0);
        file.file_size = 0;
        file.current_pos = 0;
    }
    
    // Fill in file descriptor
    fd_table[fd].in_use = 1;
    fd_table[fd].first_cluster = file.first_cluster;
    fd_table[fd].file_size = file.file_size;
    fd_table[fd].current_pos = 0;
    fd_table[fd].current_cluster = file.first_cluster;
    fd_table[fd].attributes = file.attributes;
    fd_table[fd].flags = flags;
    
    // Copy filename
    size_t i;
    for (i = 0; pathname[i] && i < 63; i++) {
        fd_table[fd].filename[i] = pathname[i];
    }
    fd_table[fd].filename[i] = '\0';
    
    // Handle O_APPEND - seek to end
    if (flags & O_APPEND) {
        fd_table[fd].current_pos = file.file_size;
    }
    
    return fd;
}

/**
 * sys_close - Close a file descriptor
 * @fd: file descriptor
 * @return: 0 on success, -1 on error
 */
int64_t sys_close(int fd) {
    if (fd < 3 || fd >= MAX_OPEN_FILES) { // Can't close stdin/stdout/stderr
        return -1;
    }
    
    if (!fd_table[fd].in_use) {
        return -1;
    }
    
    fd_table[fd].in_use = 0;
    fd_table[fd].first_cluster = 0;
    fd_table[fd].file_size = 0;
    fd_table[fd].current_pos = 0;
    fd_table[fd].current_cluster = 0;
    fd_table[fd].flags = 0;
    fd_table[fd].filename[0] = '\0';
    
    return 0;
}

/**
 * sys_getline - Read a line from stdin
 * @buf: buffer to store the line
 * @max_len: maximum length of the buffer
 * @return: number of characters read, or -1 on error
 */
int64_t sys_getline(char* buf, size_t max_len) {
    if (buf == NULL || max_len == 0) {
        return -1;
    }
    
    size_t pos = 0;
    
    while (pos < max_len - 1) {
        char c = keyboard_getchar();
        
        if (c == 0) {
            continue; // No key pressed, keep waiting
        }
        
        if (c == '\n' || c == '\r') {
            buf[pos++] = '\n';
            tty_putchar('\n');
            break;
        }
        
        if (c == '\b' || c == 127) { // Backspace
            if (pos > 0) {
                pos--;
                tty_backspace();
            }
            continue;
        }
        
        // Regular character
        buf[pos++] = c;
        tty_putchar(c); // Echo
    }
    
    buf[pos] = '\0';
    return (int64_t)pos;
}

/**
 * sys_putchar - Print a character to stdout
 * @c: character to print
 * @return: the character printed
 */
int64_t sys_putchar(char c) {
    tty_putchar(c);
    return (int64_t)c;
}

/**
 * sys_getchar - Get a character from stdin
 * @return: the character read, or 0 if no character available
 */
int64_t sys_getchar(void) {
    char c;
    do {
        c = keyboard_getchar();
    } while (c == 0);
    return (int64_t)c;
}

/**
 * sys_exit - Exit the current process
 * @status: exit status code
 */
void sys_exit(int status) {
    (void)status;
    // In a simple kernel, we might just halt or return to shell
    // For now, just print a message
    tty_putstr("Process exited with status: ");
    tty_putdec((uint32_t)status);
    tty_putstr("\n");
    
    // TODO: Actually terminate the process when scheduler is implemented
}

/**
 * sys_exec - Execute a program
 * @path: path to the executable
 * @argv: argument vector
 * @return: -1 on error (never returns on success)
 */
int64_t sys_exec(const char* path, char* const argv[]) {
    (void)path;
    (void)argv;
    // TODO: Implement exec when ELF loading is available
    tty_putstr("exec: not yet implemented\n");
    return -1;
}

/**
 * sys_fork - Create a child process
 * @return: 0 in child, child PID in parent, -1 on error
 */
int64_t sys_fork(void) {
    // TODO: Implement fork when scheduler supports it
    tty_putstr("fork: not yet implemented\n");
    return -1;
}

/**
 * sys_wait - Wait for a child process
 * @status: pointer to store child's exit status
 * @return: PID of terminated child, or -1 on error
 */
int64_t sys_wait(int* status) {
    (void)status;
    // TODO: Implement wait when scheduler supports it
    tty_putstr("wait: not yet implemented\n");
    return -1;
}

/**
 * sys_getpid - Get current process ID
 * @return: current process ID
 */
int64_t sys_getpid(void) {
    // TODO: Return actual PID when scheduler is implemented
    return 1; // Return 1 as the kernel process
}

/**
 * sys_malloc - Allocate memory
 * @size: number of bytes to allocate
 * @return: pointer to allocated memory, or NULL on failure
 */
void* sys_malloc(size_t size) {
    return kmalloc(size);
}

/**
 * sys_free - Free allocated memory
 * @ptr: pointer to memory to free
 */
void sys_free(void* ptr) {
    kfree(ptr);
}

/**
 * sys_stat - Get file status
 * @pathname: path to the file
 * @statbuf: buffer to store file status
 * @return: 0 on success, -1 on error
 */
int64_t sys_stat(const char* pathname, stat_t* statbuf) {
    if (pathname == NULL || statbuf == NULL) {
        return -1;
    }
    
    fat32_file_t file;
    if (fat32_open_file(pathname, &file) != 0) {
        return -1;
    }
    
    statbuf->st_size = file.file_size;
    statbuf->st_mode = file.attributes;
    statbuf->st_ctime = 0; // TODO: Get actual timestamps from FAT32
    statbuf->st_mtime = 0;
    statbuf->st_atime = 0;
    
    return 0;
}

/**
 * sys_mkdir - Create a directory
 * @pathname: path for the new directory
 * @return: 0 on success, -1 on error
 */
int64_t sys_mkdir(const char* pathname) {
    if (pathname == NULL) {
        return -1;
    }
    return fat32_create_directory(pathname);
}

/**
 * sys_rmdir - Remove a directory
 * @pathname: path to the directory
 * @return: 0 on success, -1 on error
 */
int64_t sys_rmdir(const char* pathname) {
    if (pathname == NULL) {
        return -1;
    }
    return fat32_remove_directory(pathname);
}

/**
 * sys_unlink - Delete a file
 * @pathname: path to the file
 * @return: 0 on success, -1 on error
 */
int64_t sys_unlink(const char* pathname) {
    if (pathname == NULL) {
        return -1;
    }
    return fat32_delete_file(pathname);
}

/**
 * sys_chdir - Change current working directory
 * @path: path to the new directory
 * @return: 0 on success, -1 on error
 */
int64_t sys_chdir(const char* path) {
    if (path == NULL) {
        return -1;
    }
    return fat32_change_directory_path(path);
}

/**
 * sys_getcwd - Get current working directory
 * @buf: buffer to store the path
 * @size: size of the buffer
 * @return: 0 on success, -1 on error
 */
int64_t sys_getcwd(char* buf, size_t size) {
    if (buf == NULL || size == 0) {
        return -1;
    }
    fat32_get_current_path(buf, (int)size);
    return 0;
}

/**
 * sys_sleep - Sleep for a specified time
 * @milliseconds: time to sleep in milliseconds
 * @return: 0 on success
 */
int64_t sys_sleep(uint32_t milliseconds) {
    // Simple busy-wait implementation
    // TODO: Use proper timer interrupt for better sleeping
    volatile uint32_t i;
    for (i = 0; i < milliseconds * 10000; i++) {
        __asm__ volatile("nop");
    }
    return 0;
}

/**
 * sys_time - Get current time
 * @return: current time (seconds since epoch, or RTC value)
 */
int64_t sys_time(void) {
    rtc_time_t time;
    rtc_read_time(&time);
    
    // Return a simple timestamp (not actual Unix timestamp)
    return (int64_t)(time.year * 365 * 24 * 3600 + 
                     time.month * 30 * 24 * 3600 + 
                     time.day * 24 * 3600 + 
                     time.hours * 3600 + 
                     time.minutes * 60 + 
                     time.seconds);
}

/**
 * sys_seek - Reposition file offset
 * @fd: file descriptor
 * @offset: offset value
 * @whence: SEEK_SET, SEEK_CUR, or SEEK_END
 * @return: new offset, or -1 on error
 */
int64_t sys_seek(int fd, int64_t offset, int whence) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || !fd_table[fd].in_use) {
        return -1;
    }
    
    // Can't seek on stdin/stdout/stderr
    if (fd < 3) {
        return -1;
    }
    
    file_descriptor_t* file = &fd_table[fd];
    int64_t new_pos;
    
    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = (int64_t)file->current_pos + offset;
            break;
        case SEEK_END:
            new_pos = (int64_t)file->file_size + offset;
            break;
        default:
            return -1;
    }
    
    if (new_pos < 0 || new_pos > (int64_t)file->file_size) {
        return -1;
    }
    
    file->current_pos = (uint32_t)new_pos;
    // Note: current_cluster would need to be recalculated for actual FAT32 seeking
    
    return new_pos;
}

