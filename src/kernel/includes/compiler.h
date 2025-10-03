#ifndef COMPILER_H
#define COMPILER_H

#include "../../../src/cpu/types.h"

// Simple bytecode instructions
typedef enum {
    OP_PUSH,        // Push constant
    OP_POP,         // Pop value
    OP_ADD,         // Add top two stack values
    OP_SUB,         // Subtract
    OP_MUL,         // Multiply
    OP_DIV,         // Divide
    OP_MOD,         // Modulo
    OP_PRINT,       // Print top of stack
    OP_PRINT_STR,   // Print string
    OP_PRINT_CHAR,  // Print character
    OP_LOAD,        // Load variable
    OP_STORE,       // Store variable
    OP_LOAD_PTR,    // Load from pointer
    OP_STORE_PTR,   // Store to pointer
    OP_ADDR_OF,     // Get address of variable
    OP_JMP,         // Unconditional jump
    OP_JZ,          // Jump if zero
    OP_JNZ,         // Jump if not zero
    OP_CMP_EQ,      // Compare equal
    OP_CMP_NE,      // Compare not equal
    OP_CMP_LT,      // Compare less than
    OP_CMP_GT,      // Compare greater than
    OP_CMP_LE,      // Compare less or equal
    OP_CMP_GE,      // Compare greater or equal
    OP_AND,         // Logical AND
    OP_OR,          // Logical OR
    OP_NOT,         // Logical NOT
    OP_NEG,         // Negate
    OP_CALL,        // Call function
    OP_RET,         // Return from function
    OP_HALT         // Stop execution
} opcode_t;

// Bytecode instruction
typedef struct {
    opcode_t op;
    s32 operand;
} instruction_t;

// Variable entry
typedef struct {
    char name[32];
    s32 address;  // Stack offset or memory address
    int is_pointer;
} variable_t;

// Compiler context
typedef struct {
    instruction_t* code;
    u32 code_size;
    u32 code_capacity;
    char** strings;
    u32 string_count;
    u32 string_capacity;
    variable_t* variables;
    u32 variable_count;
    u32 variable_capacity;
    s32 stack_offset;  // Current stack offset for variables
} compiler_ctx_t;

// Function prototypes
int compile_c_source(const char* source, u32 source_size, const char* output_file);
void compiler_init(compiler_ctx_t* ctx);
void compiler_free(compiler_ctx_t* ctx);
int compiler_emit(compiler_ctx_t* ctx, opcode_t op, s32 operand);
int compiler_add_string(compiler_ctx_t* ctx, const char* str);
int compiler_add_variable(compiler_ctx_t* ctx, const char* name, int is_pointer);
int compiler_find_variable(compiler_ctx_t* ctx, const char* name);

#endif // COMPILER_H
