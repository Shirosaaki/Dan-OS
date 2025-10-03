//
// Program execution module for DanOS
// Created for C program compilation and execution
//

#include "exec.h"
#include "fat32.h"
#include "tty.h"
#include "string.h"
#include "compiler.h"

// Current executing program context
static void* current_code_base = (void*)USER_CODE_BASE;
static void* current_data_base = (void*)USER_DATA_BASE;
static void* current_stack_base = (void*)USER_STACK_BASE;

// VM execution stack (for expressions)
#define VM_STACK_SIZE 256
static s32 vm_stack[VM_STACK_SIZE];
static int vm_sp = 0;

// VM variable storage (separate from execution stack)
#define VM_VAR_SIZE 64
static s32 vm_variables[VM_VAR_SIZE];

// VM helper functions
static void vm_push(s32 value) {
    if (vm_sp >= VM_STACK_SIZE) {
        tty_putstr("Error: VM stack overflow\n");
        return;
    }
    vm_stack[vm_sp++] = value;
}

static s32 vm_pop(void) {
    if (vm_sp <= 0) {
        tty_putstr("Error: VM stack underflow\n");
        return 0;
    }
    return vm_stack[--vm_sp];
}

void exec_init(void) {
    tty_putstr("Execution module initialized\n");
}

// Load and execute a program file
int exec_load_file(const char* filename) {
    fat32_file_t file;
    
    // Open the executable file
    if (fat32_open_file(filename, &file) != 0) {
        tty_putstr("Error: Could not open file ");
        tty_putstr(filename);
        tty_putstr("\n");
        return -1;
    }
    
    // Read the header
    exec_header_t header;
    if (fat32_read_file(&file, (uint8_t*)&header, sizeof(exec_header_t)) != sizeof(exec_header_t)) {
        tty_putstr("Error: Could not read executable header\n");
        return -1;
    }
    
    // Verify magic number
    if (header.magic != EXEC_MAGIC) {
        tty_putstr("Error: Invalid executable format\n");
        tty_putstr("Expected magic: 0x44414E58\n");
        return -1;
    }
    
    // Verify version
    if (header.version != EXEC_VERSION) {
        tty_putstr("Error: Unsupported executable version\n");
        return -1;
    }
    
    tty_putstr("Loading executable...\n");
    tty_putstr("Code size: ");
    tty_putdec(header.code_size);
    tty_putstr(" bytes\n");
    tty_putstr("Data size: ");
    tty_putdec(header.data_size);
    tty_putstr(" bytes\n");
    
    // Read code section
    if (header.code_size > 0) {
        int bytes_read = fat32_read_file(&file, (uint8_t*)current_code_base, header.code_size);
        if (bytes_read != (int)header.code_size) {
            tty_putstr("Error: Could not read code section (read ");
            tty_putdec(bytes_read);
            tty_putstr(" bytes, expected ");
            tty_putdec(header.code_size);
            tty_putstr(")\n");
            return -1;
        }
    }
    
    // Read data section
    if (header.data_size > 0) {
        int bytes_read = fat32_read_file(&file, (uint8_t*)current_data_base, header.data_size);
        if (bytes_read != (int)header.data_size) {
            tty_putstr("Error: Could not read data section (read ");
            tty_putdec(bytes_read);
            tty_putstr(" bytes, expected ");
            tty_putdec(header.data_size);
            tty_putstr(")\n");
            return -1;
        }
    }
    
    tty_putstr("Executing program...\n");
    
    // Execute the program
    return exec_run(current_code_base, header.code_size, 
                   current_data_base, header.data_size, 
                   header.entry_point, header.string_count);
}

// Execute loaded code (bytecode interpreter)
int exec_run(void* code_base, u32 code_size, void* data_base, u32 data_size, u32 entry_offset, u32 string_count) {
    instruction_t* code = (instruction_t*)code_base;
    
    // Calculate instruction count - code section has instructions + string table
    u32 string_table_size = string_count * sizeof(u32);
    u32 instructions_size = code_size - string_table_size;
    u32 instruction_count = instructions_size / sizeof(instruction_t);
    
    // String table starts right after the instructions
    u32* string_table = (u32*)((u8*)code_base + instructions_size);
    
    tty_putstr("\n--- Program Output ---\n");
    
    vm_sp = 0; // Reset stack pointer
    // Initialize variables to 0
    for (int i = 0; i < VM_VAR_SIZE; i++) {
        vm_variables[i] = 0;
    }
    u32 pc = 0; // Program counter
    u32 instruction_limit = 10000; // Prevent infinite loops
    u32 executed_count = 0;
    
    // Bytecode interpreter loop
    while (pc < instruction_count && executed_count < instruction_limit) {
        executed_count++;
        instruction_t inst = code[pc];
        

        

        
        switch (inst.op) {
            case OP_PUSH:
                vm_push(inst.operand);
                break;
                
            case OP_POP:
                vm_pop();
                break;
                
            case OP_ADD: {
                s32 b = vm_pop();
                s32 a = vm_pop();
                s32 result = a + b;
                vm_push(result);
                break;
            }
            
            case OP_SUB: {
                int32_t b = vm_pop();
                int32_t a = vm_pop();
                vm_push(a - b);
                break;
            }
            
            case OP_MUL: {
                int32_t b = vm_pop();
                int32_t a = vm_pop();
                vm_push(a * b);
                break;
            }
            
            case OP_DIV: {
                s32 b = vm_pop();
                s32 a = vm_pop();
                if (b != 0) {
                    vm_push(a / b);
                } else {
                    tty_putstr("Error: Division by zero\n");
                    return -1;
                }
                break;
            }
            
            case OP_MOD: {
                s32 b = vm_pop();
                s32 a = vm_pop();
                if (b != 0) {
                    vm_push(a % b);
                } else {
                    tty_putstr("Error: Modulo by zero\n");
                    return -1;
                }
                break;
            }
            
            case OP_PRINT: {
                s32 value = vm_pop();
                tty_putdec(value);
                tty_putchar_internal('\n');
                break;
            }
            
            case OP_PRINT_STR: {
                // Check string index bounds
                if (inst.operand >= string_count) {
                    tty_putstr("Error: Invalid string index\n");
                    return -1;
                }
                
                // Get string offset from string table
                u32 str_offset = string_table[inst.operand];
                if (str_offset >= data_size) {
                    tty_putstr("Error: Invalid string offset\n");
                    return -1;
                }
                
                // The string data is stored in the data section
                char* str = (char*)((u8*)data_base + str_offset);
                
                // Print each character with bounds check
                for (int i = 0; i < 256 && str[i] != '\0'; i++) {
                    tty_putchar_internal(str[i]);
                }
                break;
            }
            
            case OP_PRINT_CHAR: {
                s32 value = vm_pop();
                tty_putchar_internal((char)value);
                break;
            }
            
            case OP_LOAD: {
                // Load variable from separate variable storage
                s32 addr = inst.operand;
                if (addr >= 0 && addr < VM_VAR_SIZE) {
                    vm_push(vm_variables[addr]);
                } else {
                    tty_putstr("Error: Invalid variable address\n");
                    return -1;
                }
                break;
            }
            
            case OP_STORE: {
                // Store value to variable storage
                s32 addr = inst.operand;
                s32 value = vm_pop();
                if (addr >= 0 && addr < VM_VAR_SIZE) {
                    vm_variables[addr] = value;
                } else {
                    tty_putstr("Error: Invalid variable address\n");
                    return -1;
                }
                break;
            }
            
            case OP_JMP: {
                pc = inst.operand;
                continue; // Don't increment pc
            }
            
            case OP_JZ: {
                s32 value = vm_pop();
                if (value == 0) {
                    pc = inst.operand;
                    continue;
                }
                break;
            }
            
            case OP_JNZ: {
                s32 value = vm_pop();
                if (value != 0) {
                    pc = inst.operand;
                    continue;
                }
                break;
            }
            
            case OP_CMP_EQ: {
                s32 b = vm_pop();
                s32 a = vm_pop();
                vm_push(a == b ? 1 : 0);
                break;
            }
            
            case OP_CMP_NE: {
                s32 b = vm_pop();
                s32 a = vm_pop();
                vm_push(a != b ? 1 : 0);
                break;
            }
            
            case OP_CMP_LT: {
                s32 b = vm_pop();
                s32 a = vm_pop();
                s32 result = a < b ? 1 : 0;
                vm_push(result);
                break;
            }
            
            case OP_CMP_GT: {
                s32 b = vm_pop();
                s32 a = vm_pop();
                vm_push(a > b ? 1 : 0);
                break;
            }
            
            case OP_CMP_LE: {
                s32 b = vm_pop();
                s32 a = vm_pop();
                vm_push(a <= b ? 1 : 0);
                break;
            }
            
            case OP_CMP_GE: {
                s32 b = vm_pop();
                s32 a = vm_pop();
                vm_push(a >= b ? 1 : 0);
                break;
            }
            
            case OP_AND: {
                s32 b = vm_pop();
                s32 a = vm_pop();
                vm_push(a && b ? 1 : 0);
                break;
            }
            
            case OP_OR: {
                s32 b = vm_pop();
                s32 a = vm_pop();
                vm_push(a || b ? 1 : 0);
                break;
            }
            
            case OP_NOT: {
                s32 a = vm_pop();
                vm_push(!a ? 1 : 0);
                break;
            }
            
            case OP_NEG: {
                s32 a = vm_pop();
                vm_push(-a);
                break;
            }
            
            case OP_HALT:
                goto done;
                
            default:
                tty_putstr("Unknown opcode: ");
                tty_putdec(inst.op);
                tty_putstr("\n");
                return -1;
        }
        
        pc++;
    }
    
done:
    if (executed_count >= instruction_limit) {
        tty_putstr("\n--- Program terminated: instruction limit reached ---\n");
        tty_putstr("Possible infinite loop detected\n");
    } else {
        tty_putstr("\n--- Program Finished ---\n");
    }
    
    return 0;
}

// System call handler for user programs
void syscall_handler(uint32_t syscall_num, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    switch (syscall_num) {
        case SYSCALL_PRINT: {
            // Print string: arg1 = pointer to string
            char* str = (char*)arg1;
            tty_putstr(str);
            break;
        }
        
        case SYSCALL_EXIT: {
            // Exit program: arg1 = exit code
            tty_putstr("\nProgram exited with code: ");
            tty_putdec(arg1);
            tty_putstr("\n");
            // Return to shell (handled by caller)
            break;
        }
        
        case SYSCALL_GETCHAR: {
            // Get character from keyboard
            // TODO: Implement when needed
            break;
        }
        
        case SYSCALL_OPEN: {
            // Open file: arg1 = filename, arg2 = mode
            // TODO: Return file descriptor
            break;
        }
        
        case SYSCALL_READ: {
            // Read file: arg1 = fd, arg2 = buffer, arg3 = size
            // TODO: Implement
            break;
        }
        
        case SYSCALL_WRITE: {
            // Write file: arg1 = fd, arg2 = buffer, arg3 = size
            // TODO: Implement
            break;
        }
        
        case SYSCALL_CLOSE: {
            // Close file: arg1 = fd
            // TODO: Implement
            break;
        }
        
        default:
            tty_putstr("Unknown system call: ");
            tty_putdec(syscall_num);
            tty_putstr("\n");
            break;
    }
}
