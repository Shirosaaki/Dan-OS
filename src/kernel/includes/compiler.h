#ifndef COMPILER_H
#define COMPILER_H

#include "../../../src/cpu/types.h"

// Data types
typedef enum {
    TYPE_VOID,
    TYPE_CHAR,
    TYPE_INT,
    TYPE_STRUCT,
    TYPE_FUNCTION
} data_type_t;

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
    OP_LOAD_MEMBER, // Load struct member
    OP_STORE_MEMBER,// Store struct member
    OP_ALLOC,       // Allocate memory
    OP_DEALLOC,     // Deallocate memory
    OP_SIZEOF,      // Get size of type
    OP_CAST,        // Type cast
    OP_HALT         // Stop execution
} opcode_t;

// Bytecode instruction
typedef struct {
    opcode_t op;
    s32 operand;
} instruction_t;

// Struct member definition
typedef struct {
    char name[32];
    data_type_t type;
    u32 offset;
    u32 pointer_level;  // 0 = value, 1 = *, 2 = **
    s32 struct_type_id; // For struct types
} struct_member_t;

// Struct definition
typedef struct {
    char name[32];
    struct_member_t members[16];
    u32 member_count;
    u32 size;
} struct_def_t;

// Function parameter
typedef struct {
    char name[32];
    data_type_t type;
    u32 pointer_level;
    s32 struct_type_id;
} function_param_t;

// Function definition
typedef struct {
    char name[32];
    data_type_t return_type;
    u32 return_pointer_level;
    s32 return_struct_type_id;
    function_param_t params[8];
    u32 param_count;
    u32 code_start;
    u32 code_end;
    u32 local_var_count;
    int is_declared;  // Forward declaration
} function_def_t;

// Variable entry
typedef struct {
    char name[32];
    data_type_t type;
    s32 address;  // Stack offset or memory address
    u32 pointer_level;  // 0 = value, 1 = *, 2 = **
    s32 struct_type_id; // For struct variables
    u32 size;
    u32 scope_level;
} variable_t;

// Scope for variables
typedef struct {
    u32 start_var_index;
    u32 level;
} scope_t;

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
    struct_def_t* structs;
    u32 struct_count;
    u32 struct_capacity;
    function_def_t* functions;
    u32 function_count;
    u32 function_capacity;
    scope_t* scopes;
    u32 scope_count;
    u32 scope_capacity;
    u32 current_scope_level;
    s32 stack_offset;  // Current stack offset for variables
    u32 current_function;  // Index of current function being compiled
    
    // Statistics
    u32 total_variables_processed;
    char** included_files;
    u32 included_count;
    u32 included_capacity;
} compiler_ctx_t;

// Function prototypes
int compile_c_source(const char* source, u32 source_size, const char* output_file);
void compiler_init(compiler_ctx_t* ctx);
void compiler_free(compiler_ctx_t* ctx);
int compiler_emit(compiler_ctx_t* ctx, opcode_t op, s32 operand);
int compiler_add_string(compiler_ctx_t* ctx, const char* str);
int compiler_add_variable(compiler_ctx_t* ctx, const char* name, data_type_t type, u32 pointer_level, s32 struct_type_id);
int compiler_find_variable(compiler_ctx_t* ctx, const char* name);
int compiler_add_struct(compiler_ctx_t* ctx, const char* name);
int compiler_find_struct(compiler_ctx_t* ctx, const char* name);
int compiler_add_function(compiler_ctx_t* ctx, const char* name);
int compiler_find_function(compiler_ctx_t* ctx, const char* name);
void compiler_enter_scope(compiler_ctx_t* ctx);
void compiler_exit_scope(compiler_ctx_t* ctx);
u32 get_type_size(data_type_t type, u32 pointer_level, compiler_ctx_t* ctx, s32 struct_type_id);

#endif // COMPILER_H
