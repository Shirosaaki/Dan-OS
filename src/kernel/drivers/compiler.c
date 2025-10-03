//
// Full-featured C compiler for DanOS
// Supports structs, functions, pointers, header files, and more
//

#include "compiler.h"
#include "tty.h"
#include "string.h"
#include "fat32.h"
#include "exec.h"

#define MAX_CODE 8192
#define MAX_STRINGS 256
#define MAX_VARS 256
#define MAX_STRUCTS 64
#define MAX_FUNCTIONS 128
#define MAX_SCOPES 64
#define MAX_INCLUDES 32

static instruction_t static_code[MAX_CODE];
static char* static_strings[MAX_STRINGS];
static variable_t static_variables[MAX_VARS];
static struct_def_t static_structs[MAX_STRUCTS];
static function_def_t static_functions[MAX_FUNCTIONS];
static scope_t static_scopes[MAX_SCOPES];
static char* static_includes[MAX_INCLUDES];
static char string_storage[16384];
static u32 string_storage_offset = 0;

// Forward declarations for recursive parsing
static int parse_expression(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size);
static int parse_statement(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size);
static int parse_type_declaration(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size, 
                                  data_type_t* type, u32* pointer_level, s32* struct_type_id, char* name);
static int try_parse_printf(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size);

void compiler_init(compiler_ctx_t* ctx) {
    ctx->code = static_code;
    ctx->code_size = 0;
    ctx->code_capacity = MAX_CODE;
    ctx->strings = static_strings;
    ctx->string_count = 0;
    ctx->string_capacity = MAX_STRINGS;
    ctx->variables = static_variables;
    ctx->variable_count = 0;
    ctx->variable_capacity = MAX_VARS;
    ctx->structs = static_structs;
    ctx->struct_count = 0;
    ctx->struct_capacity = MAX_STRUCTS;
    ctx->functions = static_functions;
    ctx->function_count = 0;
    ctx->function_capacity = MAX_FUNCTIONS;
    ctx->scopes = static_scopes;
    ctx->scope_count = 0;
    ctx->scope_capacity = MAX_SCOPES;
    ctx->current_scope_level = 0;
    ctx->stack_offset = 0;
    ctx->current_function = (u32)-1;
    ctx->included_files = static_includes;
    ctx->included_count = 0;
    ctx->included_capacity = MAX_INCLUDES;
    ctx->total_variables_processed = 0;
    string_storage_offset = 0;
}

void compiler_free(compiler_ctx_t* ctx) {
    ctx->code_size = 0;
    ctx->string_count = 0;
    ctx->variable_count = 0;
    ctx->struct_count = 0;
    ctx->function_count = 0;
    ctx->scope_count = 0;
    ctx->current_scope_level = 0;
    ctx->stack_offset = 0;
    ctx->current_function = (u32)-1;
    ctx->included_count = 0;
    string_storage_offset = 0;
}

int compiler_emit(compiler_ctx_t* ctx, opcode_t op, s32 operand) {
    if (ctx->code_size >= ctx->code_capacity) {
        tty_putstr("Error: Code buffer full\n");
        return -1;
    }
    
    // Debug: Show ALL instructions being emitted
    tty_putstr("[EMIT PC=");
    tty_putdec(ctx->code_size);
    tty_putstr(": op=");
    tty_putdec(op);
    tty_putstr(" (");
    if (op == OP_PUSH) tty_putstr("PUSH");
    else if (op == OP_PRINT_STR) tty_putstr("PRINT_STR");
    else if (op == OP_CALL) tty_putstr("CALL");
    else if (op == OP_RET) tty_putstr("RET");
    else if (op == OP_LOAD) tty_putstr("LOAD");
    else if (op == OP_STORE) tty_putstr("STORE");
    else tty_putstr("?");
    tty_putstr(") operand=");
    tty_putdec(operand);
    tty_putstr("]\n");
    
    ctx->code[ctx->code_size].op = op;
    ctx->code[ctx->code_size].operand = operand;
    ctx->code_size++;
    return ctx->code_size - 1;
}

int compiler_add_string(compiler_ctx_t* ctx, const char* str) {
    if (ctx->string_count >= ctx->string_capacity) {
        tty_putstr("Error: String table full\n");
        return -1;
    }
    
    u32 len = strlength(str);
    if (string_storage_offset + len + 1 > sizeof(string_storage)) {
        tty_putstr("Error: String storage full\n");
        return -1;
    }
    
    char* storage_ptr = &string_storage[string_storage_offset];
    for (u32 i = 0; i <= len; i++) {
        storage_ptr[i] = str[i];
    }
    
    ctx->strings[ctx->string_count] = storage_ptr;
    string_storage_offset += len + 1;
    
    return ctx->string_count++;
}

u32 get_type_size(data_type_t type, u32 pointer_level, compiler_ctx_t* ctx, s32 struct_type_id) {
    if (pointer_level > 0) {
        return 4; // Pointer size is always 4 bytes
    }
    
    switch (type) {
        case TYPE_VOID: return 0;
        case TYPE_CHAR: return 1;
        case TYPE_INT: return 4;
        case TYPE_STRUCT:
            if (struct_type_id >= 0 && struct_type_id < (s32)ctx->struct_count) {
                return ctx->structs[struct_type_id].size;
            }
            return 0;
        case TYPE_FUNCTION: return 4; // Function pointers
        default: return 0;
    }
}

int compiler_add_variable(compiler_ctx_t* ctx, const char* name, data_type_t type, u32 pointer_level, s32 struct_type_id) {
    if (ctx->variable_count >= ctx->variable_capacity) {
        tty_putstr("Error: Variable table full\n");
        return -1;
    }
    
    // Check for duplicate in current scope
    for (u32 i = 0; i < ctx->variable_count; i++) {
        if (ctx->variables[i].scope_level == ctx->current_scope_level && 
            strcmp(ctx->variables[i].name, name) == 0) {
            tty_putstr("Error: Variable '");
            tty_putstr(name);
            tty_putstr("' already defined in current scope\n");
            return -1;
        }
    }
    
    // Copy variable name
    int i;
    for (i = 0; i < 31 && name[i] != '\0'; i++) {
        ctx->variables[ctx->variable_count].name[i] = name[i];
    }
    ctx->variables[ctx->variable_count].name[i] = '\0';
    
    ctx->variables[ctx->variable_count].type = type;
    ctx->variables[ctx->variable_count].pointer_level = pointer_level;
    ctx->variables[ctx->variable_count].struct_type_id = struct_type_id;
    ctx->variables[ctx->variable_count].scope_level = ctx->current_scope_level;
    ctx->variables[ctx->variable_count].size = get_type_size(type, pointer_level, ctx, struct_type_id);
    ctx->variables[ctx->variable_count].address = ctx->stack_offset;
    
    ctx->stack_offset += ctx->variables[ctx->variable_count].size;
    
    ctx->total_variables_processed++;
    return ctx->variable_count++;
}

int compiler_find_variable(compiler_ctx_t* ctx, const char* name) {
    // Search from most recent to oldest (reverse scope order)
    for (s32 i = ctx->variable_count - 1; i >= 0; i--) {
        if (strcmp(ctx->variables[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int compiler_add_struct(compiler_ctx_t* ctx, const char* name) {
    if (ctx->struct_count >= ctx->struct_capacity) {
        tty_putstr("Error: Struct table full\n");
        return -1;
    }
    
    // Copy struct name
    int i;
    for (i = 0; i < 31 && name[i] != '\0'; i++) {
        ctx->structs[ctx->struct_count].name[i] = name[i];
    }
    ctx->structs[ctx->struct_count].name[i] = '\0';
    
    ctx->structs[ctx->struct_count].member_count = 0;
    ctx->structs[ctx->struct_count].size = 0;
    
    return ctx->struct_count++;
}

int compiler_find_struct(compiler_ctx_t* ctx, const char* name) {
    for (u32 i = 0; i < ctx->struct_count; i++) {
        if (strcmp(ctx->structs[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int compiler_add_function(compiler_ctx_t* ctx, const char* name) {
    if (ctx->function_count >= ctx->function_capacity) {
        tty_putstr("Error: Function table full\n");
        return -1;
    }
    
    // Copy function name
    int i;
    for (i = 0; i < 31 && name[i] != '\0'; i++) {
        ctx->functions[ctx->function_count].name[i] = name[i];
    }
    ctx->functions[ctx->function_count].name[i] = '\0';
    
    ctx->functions[ctx->function_count].param_count = 0;
    ctx->functions[ctx->function_count].code_start = 0;
    ctx->functions[ctx->function_count].code_end = 0;
    ctx->functions[ctx->function_count].local_var_count = 0;
    ctx->functions[ctx->function_count].is_declared = 0;
    
    return ctx->function_count++;
}

int compiler_find_function(compiler_ctx_t* ctx, const char* name) {
    for (u32 i = 0; i < ctx->function_count; i++) {
        if (strcmp(ctx->functions[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

void compiler_enter_scope(compiler_ctx_t* ctx) {
    if (ctx->scope_count >= ctx->scope_capacity) {
        tty_putstr("Error: Scope stack overflow\n");
        return;
    }
    
    ctx->scopes[ctx->scope_count].start_var_index = ctx->variable_count;
    ctx->scopes[ctx->scope_count].level = ctx->current_scope_level++;
    ctx->scope_count++;
}

void compiler_exit_scope(compiler_ctx_t* ctx) {
    if (ctx->scope_count == 0) {
        tty_putstr("Error: Scope stack underflow\n");
        return;
    }
    
    ctx->scope_count--;
    u32 start_var = ctx->scopes[ctx->scope_count].start_var_index;
    
    // Remove variables from this scope
    ctx->variable_count = start_var;
    ctx->current_scope_level--;
}

// Tokenizer helpers
static int is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_alnum(char c) {
    return is_alpha(c) || is_digit(c);
}

static void skip_whitespace(const char* src, u32* pos, u32 size) {
    while (*pos < size && is_whitespace(src[*pos])) {
        (*pos)++;
    }
}

static void skip_line_comment(const char* src, u32* pos, u32 size) {
    while (*pos < size && src[*pos] != '\n') {
        (*pos)++;
    }
}

static void skip_block_comment(const char* src, u32* pos, u32 size) {
    (*pos) += 2; // Skip /*
    while (*pos + 1 < size) {
        if (src[*pos] == '*' && src[*pos + 1] == '/') {
            *pos += 2;
            return;
        }
        (*pos)++;
    }
}

static void skip_whitespace_and_comments(const char* src, u32* pos, u32 size) {
    while (*pos < size) {
        if (is_whitespace(src[*pos])) {
            (*pos)++;
        } else if (*pos + 1 < size && src[*pos] == '/' && src[*pos + 1] == '/') {
            skip_line_comment(src, pos, size);
        } else if (*pos + 1 < size && src[*pos] == '/' && src[*pos + 1] == '*') {
            skip_block_comment(src, pos, size);
        } else {
            break;
        }
    }
}

static int parse_number(const char* src, u32* pos, u32 size) {
    int num = 0;
    int is_negative = 0;
    
    if (*pos < size && src[*pos] == '-') {
        is_negative = 1;
        (*pos)++;
    }
    
    while (*pos < size && is_digit(src[*pos])) {
        num = num * 10 + (src[*pos] - '0');
        (*pos)++;
    }
    
    return is_negative ? -num : num;
}

static int parse_identifier(const char* src, u32* pos, u32 size, char* buf, int buf_size) {
    int i = 0;
    while (*pos < size && is_alnum(src[*pos]) && i < buf_size - 1) {
        buf[i++] = src[*pos];
        (*pos)++;
    }
    buf[i] = '\0';
    return i;
}

static int match_keyword(const char* src, u32* pos, u32 size, const char* keyword) {
    u32 start_pos = *pos;
    u32 len = strlength(keyword);
    
    if (*pos + len > size) {
        return 0;
    }
    
    for (u32 i = 0; i < len; i++) {
        if (src[*pos + i] != keyword[i]) {
            return 0;
        }
    }
    
    // Make sure it's not part of a longer identifier
    if (*pos + len < size && is_alnum(src[*pos + len])) {
        return 0;
    }
    
    *pos += len;
    return 1;
}

static data_type_t parse_type_name(const char* name) {
    if (strcmp(name, "void") == 0) return TYPE_VOID;
    if (strcmp(name, "char") == 0) return TYPE_CHAR;
    if (strcmp(name, "int") == 0) return TYPE_INT;
    return TYPE_STRUCT; // Assume it's a struct name
}

// Parse struct definition
static int parse_struct_definition(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    u32 start_pos = *pos;
    
    if (!match_keyword(src, pos, size, "struct")) {
        *pos = start_pos;
        return 0;
    }
    
    skip_whitespace_and_comments(src, pos, size);
    
    char struct_name[32];
    if (parse_identifier(src, pos, size, struct_name, sizeof(struct_name)) == 0) {
        tty_putstr("Error: Expected struct name\n");
        return -1;
    }
    
    skip_whitespace_and_comments(src, pos, size);
    
    if (*pos >= size || src[*pos] != '{') {
        tty_putstr("Error: Expected '{' after struct name\n");
        return -1;
    }
    (*pos)++;
    
    int struct_id = compiler_add_struct(ctx, struct_name);
    if (struct_id < 0) return -1;
    
    struct_def_t* struct_def = &ctx->structs[struct_id];
    u32 current_offset = 0;
    
    skip_whitespace_and_comments(src, pos, size);
    
    // Parse struct members
    while (*pos < size && src[*pos] != '}') {
        // Parse member type
        data_type_t member_type;
        u32 pointer_level = 0;
        s32 member_struct_id = -1;
        char member_name[32];
        
        if (parse_type_declaration(ctx, src, pos, size, &member_type, &pointer_level, &member_struct_id, member_name) != 0) {
            tty_putstr("Error: Failed to parse struct member\n");
            return -1;
        }
        
        skip_whitespace_and_comments(src, pos, size);
        
        if (*pos >= size || src[*pos] != ';') {
            tty_putstr("Error: Expected ';' after struct member\n");
            return -1;
        }
        (*pos)++;
        
        // Add member to struct
        if (struct_def->member_count >= 16) {
            tty_putstr("Error: Too many struct members\n");
            return -1;
        }
        
        struct_member_t* member = &struct_def->members[struct_def->member_count];
        int i;
        for (i = 0; i < 31 && member_name[i] != '\0'; i++) {
            member->name[i] = member_name[i];
        }
        member->name[i] = '\0';
        
        member->type = member_type;
        member->pointer_level = pointer_level;
        member->struct_type_id = member_struct_id;
        member->offset = current_offset;
        
        u32 member_size = get_type_size(member_type, pointer_level, ctx, member_struct_id);
        current_offset += member_size;
        struct_def->member_count++;
        
        skip_whitespace_and_comments(src, pos, size);
    }
    
    if (*pos >= size || src[*pos] != '}') {
        tty_putstr("Error: Expected '}' to close struct\n");
        return -1;
    }
    (*pos)++;
    
    struct_def->size = current_offset;
    
    skip_whitespace_and_comments(src, pos, size);
    
    if (*pos < size && src[*pos] == ';') {
        (*pos)++;
    }
    
    return 1;
}

// Parse type declaration (type + optional pointers + name)
static int parse_type_declaration(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size, 
                                  data_type_t* type, u32* pointer_level, s32* struct_type_id, char* name) {
    u32 start_pos = *pos;
    
    skip_whitespace_and_comments(src, pos, size);
    
    // Check for struct keyword
    if (match_keyword(src, pos, size, "struct")) {
        *type = TYPE_STRUCT;
        skip_whitespace_and_comments(src, pos, size);
        
        char struct_name[32];
        if (parse_identifier(src, pos, size, struct_name, sizeof(struct_name)) == 0) {
            tty_putstr("Error: Expected struct name\n");
            return -1;
        }
        
        *struct_type_id = compiler_find_struct(ctx, struct_name);
        if (*struct_type_id < 0) {
            tty_putstr("Error: Unknown struct '");
            tty_putstr(struct_name);
            tty_putstr("'\n");
            return -1;
        }
    } else {
        // Parse basic type
        char type_name[32];
        if (parse_identifier(src, pos, size, type_name, sizeof(type_name)) == 0) {
            tty_putstr("Error: Expected type name\n");
            return -1;
        }
        
        *type = parse_type_name(type_name);
        *struct_type_id = -1;
    }
    
    skip_whitespace_and_comments(src, pos, size);
    
    // Parse pointer levels
    *pointer_level = 0;
    while (*pos < size && src[*pos] == '*' && *pointer_level < 2) {
        (*pointer_level)++;
        (*pos)++;
        skip_whitespace_and_comments(src, pos, size);
    }
    
    // Parse variable name
    if (parse_identifier(src, pos, size, name, 32) == 0) {
        tty_putstr("Error: Expected variable name\n");
        return -1;
    }
    
    skip_whitespace_and_comments(src, pos, size);
    
    // Check for array declaration
    if (*pos < size && src[*pos] == '[') {
        (*pos)++;
        skip_whitespace_and_comments(src, pos, size);
        
        // Parse array size (for now, just skip it)
        if (*pos < size && src[*pos] != ']') {
            u32 temp_pos = *pos;
            int array_size = parse_number(src, pos, size);
            if (*pos == temp_pos) {
                // Not a number, skip expression
                int bracket_count = 1;
                while (*pos < size && bracket_count > 0) {
                    if (src[*pos] == '[') bracket_count++;
                    else if (src[*pos] == ']') bracket_count--;
                    (*pos)++;
                }
                (*pos)--; // Back up to the closing bracket
            }
        }
        
        skip_whitespace_and_comments(src, pos, size);
        if (*pos >= size || src[*pos] != ']') {
            tty_putstr("Error: Expected ']' in array declaration\n");
            return -1;
        }
        (*pos)++;
        
        // Treat arrays as pointers for now
        *pointer_level = 1;
    }
    
    return 0;
}

// Parse function declaration/definition
static int parse_function(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    u32 start_pos = *pos;
    
    // Parse return type
    data_type_t return_type;
    u32 return_pointer_level = 0;
    s32 return_struct_id = -1;
    char function_name[32];
    
    if (parse_type_declaration(ctx, src, pos, size, &return_type, &return_pointer_level, &return_struct_id, function_name) != 0) {
        *pos = start_pos;
        return 0;
    }
    
    skip_whitespace_and_comments(src, pos, size);
    
    if (*pos >= size || src[*pos] != '(') {
        *pos = start_pos;
        return 0;
    }
    (*pos)++;
    
    int func_id = compiler_find_function(ctx, function_name);
    if (func_id < 0) {
        func_id = compiler_add_function(ctx, function_name);
        if (func_id < 0) return -1;
    }
    
    function_def_t* func = &ctx->functions[func_id];
    func->return_type = return_type;
    func->return_pointer_level = return_pointer_level;
    func->return_struct_type_id = return_struct_id;
    func->param_count = 0;
    
    skip_whitespace_and_comments(src, pos, size);
    
    // Parse parameters
    while (*pos < size && src[*pos] != ')') {
        if (func->param_count >= 8) {
            tty_putstr("Error: Too many function parameters\n");
            return -1;
        }
        
        if (match_keyword(src, pos, size, "void")) {
            skip_whitespace_and_comments(src, pos, size);
            break;
        }
        
        // Parse parameter
        data_type_t param_type;
        u32 param_pointer_level = 0;
        s32 param_struct_id = -1;
        char param_name[32];
        
        if (parse_type_declaration(ctx, src, pos, size, &param_type, &param_pointer_level, &param_struct_id, param_name) != 0) {
            tty_putstr("Error: Failed to parse function parameter\n");
            return -1;
        }
        
        function_param_t* param = &func->params[func->param_count++];
        int i;
        for (i = 0; i < 31 && param_name[i] != '\0'; i++) {
            param->name[i] = param_name[i];
        }
        param->name[i] = '\0';
        param->type = param_type;
        param->pointer_level = param_pointer_level;
        param->struct_type_id = param_struct_id;
        
        skip_whitespace_and_comments(src, pos, size);
        
        if (*pos < size && src[*pos] == ',') {
            (*pos)++;
            skip_whitespace_and_comments(src, pos, size);
        }
    }
    
    if (*pos >= size || src[*pos] != ')') {
        tty_putstr("Error: Expected ')' after function parameters\n");
        return -1;
    }
    (*pos)++;
    
    skip_whitespace_and_comments(src, pos, size);
    
    // Check if it's just a declaration or a definition
    if (*pos < size && src[*pos] == ';') {
        (*pos)++;
        func->is_declared = 1;
        return 1;
    }
    
    // Function definition - parse body
    if (*pos >= size || src[*pos] != '{') {
        tty_putstr("Error: Expected '{' or ';' after function declaration\n");
        return -1;
    }
    
    func->code_start = ctx->code_size;
    ctx->current_function = func_id;
    
    // Enter function scope and add parameters as local variables
    compiler_enter_scope(ctx);
    
    for (u32 i = 0; i < func->param_count; i++) {
        function_param_t* param = &func->params[i];
        compiler_add_variable(ctx, param->name, param->type, param->pointer_level, param->struct_type_id);
    }
    
    // Parse function body
    if (parse_statement(ctx, src, pos, size) != 0) {
        tty_putstr("Error: Failed to parse function body\n");
        return -1;
    }
    
    // Automatically add return statement if function doesn't end with one
    // This prevents functions from falling through to the next code
    if (ctx->code_size == 0 || ctx->code[ctx->code_size - 1].op != OP_RET) {
        compiler_emit(ctx, OP_PUSH, 0);  // Push default return value
        compiler_emit(ctx, OP_RET, 0);   // Return
    }
    
    compiler_exit_scope(ctx);
    
    func->code_end = ctx->code_size;
    ctx->current_function = (u32)-1;
    
    return 1;
}

// Parse primary expression (numbers, variables, function calls, etc.)
static int parse_primary_expression(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    skip_whitespace_and_comments(src, pos, size);
    
    // Check for parenthesized expression
    if (*pos < size && src[*pos] == '(') {
        (*pos)++;
        if (parse_expression(ctx, src, pos, size) != 0) {
            return -1;
        }
        skip_whitespace_and_comments(src, pos, size);
        if (*pos >= size || src[*pos] != ')') {
            tty_putstr("Error: Expected ')'\n");
            return -1;
        }
        (*pos)++;
        return 0;
    }
    
    // Check for string literal
    if (*pos < size && src[*pos] == '"') {
        (*pos)++;
        char str_buf[256];
        u32 str_pos = 0;
        
        while (*pos < size && src[*pos] != '"' && str_pos < 255) {
            if (src[*pos] == '\\' && *pos + 1 < size) {
                (*pos)++;
                switch (src[*pos]) {
                    case 'n': str_buf[str_pos++] = '\n'; break;
                    case 't': str_buf[str_pos++] = '\t'; break;
                    case 'r': str_buf[str_pos++] = '\r'; break;
                    case '\\': str_buf[str_pos++] = '\\'; break;
                    case '"': str_buf[str_pos++] = '"'; break;
                    default: str_buf[str_pos++] = src[*pos]; break;
                }
            } else {
                str_buf[str_pos++] = src[*pos];
            }
            (*pos)++;
        }
        
        if (*pos >= size || src[*pos] != '"') {
            tty_putstr("Error: Unterminated string literal\n");
            return -1;
        }
        (*pos)++;
        str_buf[str_pos] = '\0';
        
        int str_id = compiler_add_string(ctx, str_buf);
        if (str_id < 0) return -1;
        
        // Debug: Show string being compiled
        tty_putstr("String literal '");
        tty_putstr(str_buf);
        tty_putstr("' -> index ");
        tty_putdec(str_id);
        tty_putstr("\n");
        
        compiler_emit(ctx, OP_PUSH, str_id);
        return 0;
    }
    
    // Check for character literal
    if (*pos < size && src[*pos] == '\'') {
        (*pos)++;
        if (*pos >= size) {
            tty_putstr("Error: Unterminated character literal\n");
            return -1;
        }
        
        char c = src[*pos];
        if (c == '\\' && *pos + 1 < size) {
            (*pos)++;
            switch (src[*pos]) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case '\\': c = '\\'; break;
                case '\'': c = '\''; break;
                default: c = src[*pos]; break;
            }
        }
        (*pos)++;
        
        if (*pos >= size || src[*pos] != '\'') {
            tty_putstr("Error: Expected closing quote for character literal\n");
            return -1;
        }
        (*pos)++;
        
        compiler_emit(ctx, OP_PUSH, (s32)c);
        return 0;
    }
    
    // Check for number
    if (*pos < size && (is_digit(src[*pos]) || src[*pos] == '-')) {
        u32 temp_pos = *pos;
        int num = parse_number(src, pos, size);
        if (*pos > temp_pos) {
            compiler_emit(ctx, OP_PUSH, num);
            return 0;
        }
    }
    
    // Check for identifier (variable, function call, etc.)
    if (*pos < size && is_alpha(src[*pos])) {
        char identifier[32];
        u32 temp_pos = *pos;
        if (parse_identifier(src, pos, size, identifier, sizeof(identifier)) > 0) {
            skip_whitespace_and_comments(src, pos, size);
            
            // Check for function call
            if (*pos < size && src[*pos] == '(') {
                // Special handling for printf
                if (strcmp(identifier, "printf") == 0) {
                    // Reset position and let try_parse_printf handle it
                    *pos -= strlength(identifier);
                    if (try_parse_printf(ctx, src, pos, size) == 1) {
                        return 0;
                    } else {
                        tty_putstr("Error: Failed to parse printf\n");
                        return -1;
                    }
                }
                
                (*pos)++;
                
                int func_id = compiler_find_function(ctx, identifier);
                if (func_id < 0) {
                    tty_putstr("Error: Unknown function '");
                    tty_putstr(identifier);
                    tty_putstr("'\n");
                    return -1;
                }
                
                // Parse function arguments
                u32 arg_count = 0;
                skip_whitespace_and_comments(src, pos, size);
                
                while (*pos < size && src[*pos] != ')') {
                    if (parse_expression(ctx, src, pos, size) != 0) {
                        return -1;
                    }
                    arg_count++;
                    
                    skip_whitespace_and_comments(src, pos, size);
                    if (*pos < size && src[*pos] == ',') {
                        (*pos)++;
                        skip_whitespace_and_comments(src, pos, size);
                    }
                }
                
                if (*pos >= size || src[*pos] != ')') {
                    tty_putstr("Error: Expected ')' after function arguments\n");
                    return -1;
                }
                (*pos)++;
                
                // Emit function call
                compiler_emit(ctx, OP_CALL, func_id);
                return 0;
            }
            
            // Variable access
            int var_id = compiler_find_variable(ctx, identifier);
            if (var_id < 0) {
                tty_putstr("Error: Unknown variable '");
                tty_putstr(identifier);
                tty_putstr("'\n");
                return -1;
            }
            
            compiler_emit(ctx, OP_LOAD, var_id);
            return 0;
        }
        *pos = temp_pos;
    }
    
    tty_putstr("Error: Expected primary expression\n");
    return -1;
}

// Parse postfix expression (array subscript, member access, etc.)
static int parse_postfix_expression(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    if (parse_primary_expression(ctx, src, pos, size) != 0) {
        return -1;
    }
    
    while (1) {
        skip_whitespace_and_comments(src, pos, size);
        
        // Array subscript [expr]
        if (*pos < size && src[*pos] == '[') {
            (*pos)++;
            if (parse_expression(ctx, src, pos, size) != 0) {
                return -1;
            }
            skip_whitespace_and_comments(src, pos, size);
            if (*pos >= size || src[*pos] != ']') {
                tty_putstr("Error: Expected ']'\n");
                return -1;
            }
            (*pos)++;
            
            // Convert array[index] to *(array + index)
            compiler_emit(ctx, OP_ADD, 0);
            compiler_emit(ctx, OP_LOAD_PTR, 0);
            continue;
        }
        
        // Member access .member
        if (*pos < size && src[*pos] == '.') {
            (*pos)++;
            skip_whitespace_and_comments(src, pos, size);
            
            char member_name[32];
            if (parse_identifier(src, pos, size, member_name, sizeof(member_name)) == 0) {
                tty_putstr("Error: Expected member name after '.'\n");
                return -1;
            }
            
            // For now, emit a placeholder - would need type information to resolve properly
            compiler_emit(ctx, OP_LOAD_MEMBER, 0);
            continue;
        }
        
        // Pointer member access ->member
        if (*pos + 1 < size && src[*pos] == '-' && src[*pos + 1] == '>') {
            *pos += 2;
            skip_whitespace_and_comments(src, pos, size);
            
            char member_name[32];
            if (parse_identifier(src, pos, size, member_name, sizeof(member_name)) == 0) {
                tty_putstr("Error: Expected member name after '->'\n");
                return -1;
            }
            
            // Dereference pointer first, then access member
            compiler_emit(ctx, OP_LOAD_PTR, 0);
            compiler_emit(ctx, OP_LOAD_MEMBER, 0);
            continue;
        }
        
        break;
    }
    
    return 0;
}

// Parse unary expression (*, &, !, -, +, ++, --, etc.)
static int parse_unary_expression(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    skip_whitespace_and_comments(src, pos, size);
    
    // Dereference operator *
    if (*pos < size && src[*pos] == '*') {
        (*pos)++;
        if (parse_unary_expression(ctx, src, pos, size) != 0) {
            return -1;
        }
        compiler_emit(ctx, OP_LOAD_PTR, 0);
        return 0;
    }
    
    // Address-of operator &
    if (*pos < size && src[*pos] == '&') {
        (*pos)++;
        skip_whitespace_and_comments(src, pos, size);
        
        // Need to handle variable address
        if (*pos < size && is_alpha(src[*pos])) {
            char identifier[32];
            if (parse_identifier(src, pos, size, identifier, sizeof(identifier)) > 0) {
                int var_id = compiler_find_variable(ctx, identifier);
                if (var_id < 0) {
                    tty_putstr("Error: Unknown variable in address-of\n");
                    return -1;
                }
                compiler_emit(ctx, OP_ADDR_OF, var_id);
                return 0;
            }
        }
        
        tty_putstr("Error: Invalid address-of operand\n");
        return -1;
    }
    
    // Unary minus -
    if (*pos < size && src[*pos] == '-') {
        // Check if it's part of a number
        u32 temp_pos = *pos;
        (*pos)++;
        skip_whitespace_and_comments(src, pos, size);
        if (*pos < size && is_digit(src[*pos])) {
            *pos = temp_pos;
            return parse_postfix_expression(ctx, src, pos, size);
        }
        
        if (parse_unary_expression(ctx, src, pos, size) != 0) {
            return -1;
        }
        compiler_emit(ctx, OP_NEG, 0);
        return 0;
    }
    
    // Unary plus +
    if (*pos < size && src[*pos] == '+') {
        (*pos)++;
        return parse_unary_expression(ctx, src, pos, size);
    }
    
    // Logical not !
    if (*pos < size && src[*pos] == '!') {
        (*pos)++;
        if (parse_unary_expression(ctx, src, pos, size) != 0) {
            return -1;
        }
        compiler_emit(ctx, OP_NOT, 0);
        return 0;
    }
    
    // Sizeof operator
    if (match_keyword(src, pos, size, "sizeof")) {
        skip_whitespace_and_comments(src, pos, size);
        if (*pos >= size || src[*pos] != '(') {
            tty_putstr("Error: Expected '(' after sizeof\n");
            return -1;
        }
        (*pos)++;
        
        // For now, just skip the type and return size 4
        int paren_count = 1;
        while (*pos < size && paren_count > 0) {
            if (src[*pos] == '(') paren_count++;
            else if (src[*pos] == ')') paren_count--;
            (*pos)++;
        }
        
        compiler_emit(ctx, OP_SIZEOF, 4); // Default size
        return 0;
    }
    
    return parse_postfix_expression(ctx, src, pos, size);
}

// Parse multiplicative expression (*, /, %)
static int parse_multiplicative_expression(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    if (parse_unary_expression(ctx, src, pos, size) != 0) {
        return -1;
    }
    
    while (1) {
        skip_whitespace_and_comments(src, pos, size);
        
        if (*pos < size && src[*pos] == '*') {
            // Make sure it's not a dereference (check for space or identifier before)
            if (*pos > 0 && (is_alnum(src[*pos - 1]) || src[*pos - 1] == ')')) {
                (*pos)++;
                if (parse_unary_expression(ctx, src, pos, size) != 0) {
                    return -1;
                }
                compiler_emit(ctx, OP_MUL, 0);
            } else {
                break;
            }
        } else if (*pos < size && src[*pos] == '/') {
            (*pos)++;
            if (parse_unary_expression(ctx, src, pos, size) != 0) {
                return -1;
            }
            compiler_emit(ctx, OP_DIV, 0);
        } else if (*pos < size && src[*pos] == '%') {
            (*pos)++;
            if (parse_unary_expression(ctx, src, pos, size) != 0) {
                return -1;
            }
            compiler_emit(ctx, OP_MOD, 0);
        } else {
            break;
        }
    }
    
    return 0;
}

// Parse additive expression (+, -)
static int parse_additive_expression(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    if (parse_multiplicative_expression(ctx, src, pos, size) != 0) {
        return -1;
    }
    
    while (1) {
        skip_whitespace_and_comments(src, pos, size);
        
        if (*pos < size && src[*pos] == '+') {
            (*pos)++;
            if (parse_multiplicative_expression(ctx, src, pos, size) != 0) {
                return -1;
            }
            compiler_emit(ctx, OP_ADD, 0);
        } else if (*pos < size && src[*pos] == '-') {
            // Make sure it's not part of ->
            if (*pos + 1 < size && src[*pos + 1] == '>') {
                break;
            }
            (*pos)++;
            if (parse_multiplicative_expression(ctx, src, pos, size) != 0) {
                return -1;
            }
            compiler_emit(ctx, OP_SUB, 0);
        } else {
            break;
        }
    }
    
    return 0;
}

// Parse relational expression (<, >, <=, >=)
static int parse_relational_expression(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    if (parse_additive_expression(ctx, src, pos, size) != 0) {
        return -1;
    }
    
    while (1) {
        skip_whitespace_and_comments(src, pos, size);
        
        if (*pos + 1 < size && src[*pos] == '<' && src[*pos + 1] == '=') {
            *pos += 2;
            if (parse_additive_expression(ctx, src, pos, size) != 0) {
                return -1;
            }
            compiler_emit(ctx, OP_CMP_LE, 0);
        } else if (*pos + 1 < size && src[*pos] == '>' && src[*pos + 1] == '=') {
            *pos += 2;
            if (parse_additive_expression(ctx, src, pos, size) != 0) {
                return -1;
            }
            compiler_emit(ctx, OP_CMP_GE, 0);
        } else if (*pos < size && src[*pos] == '<') {
            (*pos)++;
            if (parse_additive_expression(ctx, src, pos, size) != 0) {
                return -1;
            }
            compiler_emit(ctx, OP_CMP_LT, 0);
        } else if (*pos < size && src[*pos] == '>') {
            (*pos)++;
            if (parse_additive_expression(ctx, src, pos, size) != 0) {
                return -1;
            }
            compiler_emit(ctx, OP_CMP_GT, 0);
        } else {
            break;
        }
    }
    
    return 0;
}

// Parse equality expression (==, !=)
static int parse_equality_expression(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    if (parse_relational_expression(ctx, src, pos, size) != 0) {
        return -1;
    }
    
    while (1) {
        skip_whitespace_and_comments(src, pos, size);
        
        if (*pos + 1 < size && src[*pos] == '=' && src[*pos + 1] == '=') {
            *pos += 2;
            if (parse_relational_expression(ctx, src, pos, size) != 0) {
                return -1;
            }
            compiler_emit(ctx, OP_CMP_EQ, 0);
        } else if (*pos + 1 < size && src[*pos] == '!' && src[*pos + 1] == '=') {
            *pos += 2;
            if (parse_relational_expression(ctx, src, pos, size) != 0) {
                return -1;
            }
            compiler_emit(ctx, OP_CMP_NE, 0);
        } else {
            break;
        }
    }
    
    return 0;
}

// Parse logical AND expression (&&)
static int parse_logical_and_expression(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    if (parse_equality_expression(ctx, src, pos, size) != 0) {
        return -1;
    }
    
    while (1) {
        skip_whitespace_and_comments(src, pos, size);
        
        if (*pos + 1 < size && src[*pos] == '&' && src[*pos + 1] == '&') {
            *pos += 2;
            if (parse_equality_expression(ctx, src, pos, size) != 0) {
                return -1;
            }
            compiler_emit(ctx, OP_AND, 0);
        } else {
            break;
        }
    }
    
    return 0;
}

// Parse logical OR expression (||)
static int parse_logical_or_expression(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    if (parse_logical_and_expression(ctx, src, pos, size) != 0) {
        return -1;
    }
    
    while (1) {
        skip_whitespace_and_comments(src, pos, size);
        
        if (*pos + 1 < size && src[*pos] == '|' && src[*pos + 1] == '|') {
            *pos += 2;
            if (parse_logical_and_expression(ctx, src, pos, size) != 0) {
                return -1;
            }
            compiler_emit(ctx, OP_OR, 0);
        } else {
            break;
        }
    }
    
    return 0;
}

// Parse assignment expression (=, +=, -=, etc.)
static int parse_assignment_expression(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    u32 start_pos = *pos;
    
    // Try to parse as regular expression first
    if (parse_logical_or_expression(ctx, src, pos, size) != 0) {
        return -1;
    }
    
    skip_whitespace_and_comments(src, pos, size);
    
    // Check for assignment operators
    if (*pos < size && src[*pos] == '=') {
        // Make sure it's not ==
        if (*pos + 1 < size && src[*pos + 1] == '=') {
            return 0; // Already handled in equality expression
        }
        
        (*pos)++;
        
        // Need to re-parse left side as lvalue
        *pos = start_pos;
        
        // Handle different types of lvalues
        skip_whitespace_and_comments(src, pos, size);
        
        // Simple variable assignment
        if (is_alpha(src[*pos])) {
            char identifier[32];
            if (parse_identifier(src, pos, size, identifier, sizeof(identifier)) > 0) {
                int var_id = compiler_find_variable(ctx, identifier);
                if (var_id < 0) {
                    tty_putstr("Error: Unknown variable in assignment\n");
                    return -1;
                }
                
                skip_whitespace_and_comments(src, pos, size);
                
                // Handle array assignment var[index] = value
                if (*pos < size && src[*pos] == '[') {
                    (*pos)++;
                    if (parse_expression(ctx, src, pos, size) != 0) {
                        return -1;
                    }
                    skip_whitespace_and_comments(src, pos, size);
                    if (*pos >= size || src[*pos] != ']') {
                        tty_putstr("Error: Expected ']'\n");
                        return -1;
                    }
                    (*pos)++;
                    
                    skip_whitespace_and_comments(src, pos, size);
                    if (*pos >= size || src[*pos] != '=') {
                        tty_putstr("Error: Expected '=' in array assignment\n");
                        return -1;
                    }
                    (*pos)++;
                    
                    // Load array base address
                    compiler_emit(ctx, OP_ADDR_OF, var_id);
                    
                    if (parse_assignment_expression(ctx, src, pos, size) != 0) {
                        return -1;
                    }
                    
                    // Store to array[index]
                    compiler_emit(ctx, OP_STORE_PTR, 0);
                    return 0;
                }
                
                if (*pos >= size || src[*pos] != '=') {
                    tty_putstr("Error: Expected '=' in assignment\n");
                    return -1;
                }
                (*pos)++;
                
                if (parse_assignment_expression(ctx, src, pos, size) != 0) {
                    return -1;
                }
                
                compiler_emit(ctx, OP_STORE, var_id);
                return 0;
            }
        }
        
        // Pointer dereference assignment *ptr = value
        if (src[*pos] == '*') {
            (*pos)++;
            if (parse_logical_or_expression(ctx, src, pos, size) != 0) {
                return -1;
            }
            
            skip_whitespace_and_comments(src, pos, size);
            if (*pos >= size || src[*pos] != '=') {
                tty_putstr("Error: Expected '=' in pointer assignment\n");
                return -1;
            }
            (*pos)++;
            
            if (parse_assignment_expression(ctx, src, pos, size) != 0) {
                return -1;
            }
            
            compiler_emit(ctx, OP_STORE_PTR, 0);
            return 0;
        }
        
        tty_putstr("Error: Invalid left-hand side of assignment\n");
        return -1;
    }
    
    return 0;
}

// Main expression parser
static int parse_expression(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    return parse_assignment_expression(ctx, src, pos, size);
}

// Parse variable declaration
static int parse_variable_declaration(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    data_type_t base_type;
    u32 base_pointer_level = 0;
    s32 base_struct_type_id = -1;
    char var_name[32];
    
    if (parse_type_declaration(ctx, src, pos, size, &base_type, &base_pointer_level, &base_struct_type_id, var_name) != 0) {
        return -1;
    }
    
    skip_whitespace_and_comments(src, pos, size);
    
    // Handle first variable (already parsed)
    if (*pos < size && src[*pos] == '=') {
        (*pos)++;
        if (parse_expression(ctx, src, pos, size) != 0) {
            return -1;
        }
        
        // Add variable and store initial value
        int var_id = compiler_add_variable(ctx, var_name, base_type, base_pointer_level, base_struct_type_id);
        if (var_id < 0) return -1;
        
        compiler_emit(ctx, OP_STORE, var_id);
    } else {
        // Just declare the variable
        int var_id = compiler_add_variable(ctx, var_name, base_type, base_pointer_level, base_struct_type_id);
        if (var_id < 0) return -1;
    }
    
    // Handle additional variables (comma-separated)
    skip_whitespace_and_comments(src, pos, size);
    while (*pos < size && src[*pos] == ',') {
        (*pos)++;
        skip_whitespace_and_comments(src, pos, size);
        
        // Parse next variable name (same type as first)
        if (parse_identifier(src, pos, size, var_name, sizeof(var_name)) <= 0) {
            tty_putstr("Error: Expected variable name after ','\n");
            return -1;
        }
        
        skip_whitespace_and_comments(src, pos, size);
        
        // Check for initialization
        if (*pos < size && src[*pos] == '=') {
            (*pos)++;
            if (parse_expression(ctx, src, pos, size) != 0) {
                return -1;
            }
            
            // Add variable and store initial value
            int var_id = compiler_add_variable(ctx, var_name, base_type, base_pointer_level, base_struct_type_id);
            if (var_id < 0) return -1;
            
            compiler_emit(ctx, OP_STORE, var_id);
        } else {
            // Just declare the variable
            int var_id = compiler_add_variable(ctx, var_name, base_type, base_pointer_level, base_struct_type_id);
            if (var_id < 0) return -1;
        }
        
        skip_whitespace_and_comments(src, pos, size);
    }
    
    if (*pos < size && src[*pos] == ';') {
        (*pos)++;
        return 0;
    }
    
    tty_putstr("Error: Expected ';' after variable declaration\n");
    return -1;
}

// Parse printf statement
static int try_parse_printf(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    u32 start = *pos;
    
    skip_whitespace_and_comments(src, pos, size);
    
    if (!match_keyword(src, pos, size, "printf")) {
        *pos = start;
        return 0;
    }
    
    skip_whitespace_and_comments(src, pos, size);
    
    if (*pos >= size || src[*pos] != '(') {
        *pos = start;
        return 0;
    }
    (*pos)++;
    
    skip_whitespace_and_comments(src, pos, size);
    
    // Parse first argument (format string)
    if (parse_expression(ctx, src, pos, size) != 0) {
        tty_putstr("Error: Failed to parse printf format string\n");
        return -1;
    }
    
    // Parse remaining arguments
    u32 arg_count = 1; // Count format string
    skip_whitespace_and_comments(src, pos, size);
    
    while (*pos < size && src[*pos] == ',') {
        (*pos)++;
        skip_whitespace_and_comments(src, pos, size);
        
        if (parse_expression(ctx, src, pos, size) != 0) {
            tty_putstr("Error: Failed to parse printf argument\n");
            return -1;
        }
        arg_count++;
        
        skip_whitespace_and_comments(src, pos, size);
    }
    
    if (*pos >= size || src[*pos] != ')') {
        tty_putstr("Error: Expected ')' after printf arguments\n");
        return -1;
    }
    (*pos)++;
    
    compiler_emit(ctx, OP_PRINT_STR, arg_count);
    
    return 1;
}

// Parse return statement
static int parse_return_statement(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    if (!match_keyword(src, pos, size, "return")) {
        return 0;
    }
    
    skip_whitespace_and_comments(src, pos, size);
    
    // Check if there's a return value
    if (*pos < size && src[*pos] != ';') {
        if (parse_expression(ctx, src, pos, size) != 0) {
            return -1;
        }
    } else {
        // Return void - push 0
        compiler_emit(ctx, OP_PUSH, 0);
    }
    
    compiler_emit(ctx, OP_RET, 0);
    
    skip_whitespace_and_comments(src, pos, size);
    
    if (*pos < size && src[*pos] == ';') {
        (*pos)++;
    }
    
    return 1;
}

// Parse if statement
static int parse_if_statement(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    if (!match_keyword(src, pos, size, "if")) {
        return 0;
    }
    
    skip_whitespace_and_comments(src, pos, size);
    
    if (*pos >= size || src[*pos] != '(') {
        tty_putstr("Error: Expected '(' after if\n");
        return -1;
    }
    (*pos)++;
    
    if (parse_expression(ctx, src, pos, size) != 0) {
        return -1;
    }
    
    skip_whitespace_and_comments(src, pos, size);
    
    if (*pos >= size || src[*pos] != ')') {
        tty_putstr("Error: Expected ')' after if condition\n");
        return -1;
    }
    (*pos)++;
    
    // Emit conditional jump
    int jz_addr = compiler_emit(ctx, OP_JZ, 0);
    
    // Parse if body
    if (parse_statement(ctx, src, pos, size) != 0) {
        return -1;
    }
    
    skip_whitespace_and_comments(src, pos, size);
    
    // Check for else
    if (match_keyword(src, pos, size, "else")) {
        int jmp_addr = compiler_emit(ctx, OP_JMP, 0);
        
        // Patch the conditional jump to skip to else
        ctx->code[jz_addr].operand = ctx->code_size;
        
        if (parse_statement(ctx, src, pos, size) != 0) {
            return -1;
        }
        
        // Patch the unconditional jump to skip else
        ctx->code[jmp_addr].operand = ctx->code_size;
    } else {
        // Patch the conditional jump to skip to end
        ctx->code[jz_addr].operand = ctx->code_size;
    }
    
    return 1;
}

// Parse while statement
static int parse_while_statement(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    if (!match_keyword(src, pos, size, "while")) {
        return 0;
    }
    
    u32 loop_start = ctx->code_size;
    
    skip_whitespace_and_comments(src, pos, size);
    
    if (*pos >= size || src[*pos] != '(') {
        tty_putstr("Error: Expected '(' after while\n");
        return -1;
    }
    (*pos)++;
    
    if (parse_expression(ctx, src, pos, size) != 0) {
        return -1;
    }
    
    skip_whitespace_and_comments(src, pos, size);
    
    if (*pos >= size || src[*pos] != ')') {
        tty_putstr("Error: Expected ')' after while condition\n");
        return -1;
    }
    (*pos)++;
    
    // Emit conditional jump
    int jz_addr = compiler_emit(ctx, OP_JZ, 0);
    
    // Parse loop body
    if (parse_statement(ctx, src, pos, size) != 0) {
        return -1;
    }
    
    // Jump back to loop condition
    compiler_emit(ctx, OP_JMP, loop_start);
    
    // Patch the conditional jump to exit loop
    ctx->code[jz_addr].operand = ctx->code_size;
    
    return 1;
}

// Parse for statement
static int parse_for_statement(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    if (!match_keyword(src, pos, size, "for")) {
        return 0;
    }
    
    skip_whitespace_and_comments(src, pos, size);
    
    if (*pos >= size || src[*pos] != '(') {
        tty_putstr("Error: Expected '(' after for\n");
        return -1;
    }
    (*pos)++;
    
    // Enter new scope for for loop variables
    compiler_enter_scope(ctx);
    
    // Parse initialization (can be declaration or expression)
    skip_whitespace_and_comments(src, pos, size);
    if (*pos < size && src[*pos] != ';') {
        // Try variable declaration first
        u32 temp_pos = *pos;
        char temp_name[32];
        if (parse_identifier(src, pos, size, temp_name, sizeof(temp_name)) > 0) {
            data_type_t temp_type = parse_type_name(temp_name);
            if (temp_type != TYPE_STRUCT || compiler_find_struct(ctx, temp_name) >= 0) {
                *pos = temp_pos;
                if (parse_variable_declaration(ctx, src, pos, size) != 0) {
                    compiler_exit_scope(ctx);
                    return -1;
                }
            } else {
                *pos = temp_pos;
                if (parse_expression(ctx, src, pos, size) != 0) {
                    compiler_exit_scope(ctx);
                    return -1;
                }
                compiler_emit(ctx, OP_POP, 0); // Discard result
                skip_whitespace_and_comments(src, pos, size);
                if (*pos >= size || src[*pos] != ';') {
                    tty_putstr("Error: Expected ';' after for initialization\n");
                    compiler_exit_scope(ctx);
                    return -1;
                }
                (*pos)++;
            }
        } else {
            *pos = temp_pos;
            if (parse_expression(ctx, src, pos, size) != 0) {
                compiler_exit_scope(ctx);
                return -1;
            }
            compiler_emit(ctx, OP_POP, 0); // Discard result
            skip_whitespace_and_comments(src, pos, size);
            if (*pos >= size || src[*pos] != ';') {
                tty_putstr("Error: Expected ';' after for initialization\n");
                compiler_exit_scope(ctx);
                return -1;
            }
            (*pos)++;
        }
    } else {
        (*pos)++; // Skip empty initialization
    }
    
    u32 condition_start = ctx->code_size;
    
    // Parse condition
    skip_whitespace_and_comments(src, pos, size);
    if (*pos < size && src[*pos] != ';') {
        if (parse_expression(ctx, src, pos, size) != 0) {
            compiler_exit_scope(ctx);
            return -1;
        }
    } else {
        compiler_emit(ctx, OP_PUSH, 1); // Default to true
    }
    
    skip_whitespace_and_comments(src, pos, size);
    if (*pos >= size || src[*pos] != ';') {
        tty_putstr("Error: Expected ';' after for condition\n");
        compiler_exit_scope(ctx);
        return -1;
    }
    (*pos)++;
    
    // Emit conditional jump to exit
    int exit_jump = compiler_emit(ctx, OP_JZ, 0);
    
    // Jump over increment to body
    int body_jump = compiler_emit(ctx, OP_JMP, 0);
    
    u32 increment_start = ctx->code_size;
    
    // Parse increment
    skip_whitespace_and_comments(src, pos, size);
    if (*pos < size && src[*pos] != ')') {
        if (parse_expression(ctx, src, pos, size) != 0) {
            compiler_exit_scope(ctx);
            return -1;
        }
        compiler_emit(ctx, OP_POP, 0); // Discard result
    }
    
    skip_whitespace_and_comments(src, pos, size);
    if (*pos >= size || src[*pos] != ')') {
        tty_putstr("Error: Expected ')' after for increment\n");
        compiler_exit_scope(ctx);
        return -1;
    }
    (*pos)++;
    
    // Jump back to condition
    compiler_emit(ctx, OP_JMP, condition_start);
    
    // Patch body jump
    ctx->code[body_jump].operand = ctx->code_size;
    
    // Parse loop body
    if (parse_statement(ctx, src, pos, size) != 0) {
        compiler_exit_scope(ctx);
        return -1;
    }
    
    // Jump to increment
    compiler_emit(ctx, OP_JMP, increment_start);
    
    // Patch exit jump
    ctx->code[exit_jump].operand = ctx->code_size;
    
    compiler_exit_scope(ctx);
    return 1;
}

// Parse do-while statement
static int parse_do_while_statement(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    if (!match_keyword(src, pos, size, "do")) {
        return 0;
    }
    
    u32 loop_start = ctx->code_size;
    
    // Parse loop body
    if (parse_statement(ctx, src, pos, size) != 0) {
        return -1;
    }
    
    skip_whitespace_and_comments(src, pos, size);
    
    if (!match_keyword(src, pos, size, "while")) {
        tty_putstr("Error: Expected 'while' after do body\n");
        return -1;
    }
    
    skip_whitespace_and_comments(src, pos, size);
    
    if (*pos >= size || src[*pos] != '(') {
        tty_putstr("Error: Expected '(' after while\n");
        return -1;
    }
    (*pos)++;
    
    if (parse_expression(ctx, src, pos, size) != 0) {
        return -1;
    }
    
    skip_whitespace_and_comments(src, pos, size);
    
    if (*pos >= size || src[*pos] != ')') {
        tty_putstr("Error: Expected ')' after while condition\n");
        return -1;
    }
    (*pos)++;
    
    // Jump back to loop start if condition is true
    compiler_emit(ctx, OP_JNZ, loop_start);
    
    skip_whitespace_and_comments(src, pos, size);
    if (*pos < size && src[*pos] == ';') {
        (*pos)++;
    }
    
    return 1;
}

// Parse break statement
static int parse_break_statement(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    if (!match_keyword(src, pos, size, "break")) {
        return 0;
    }
    
    skip_whitespace_and_comments(src, pos, size);
    
    if (*pos < size && src[*pos] == ';') {
        (*pos)++;
    }
    
    // For now, just emit a comment - would need loop context tracking for real breaks
    tty_putstr("Warning: break statement not fully implemented\n");
    
    return 1;
}

// Parse continue statement
static int parse_continue_statement(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    if (!match_keyword(src, pos, size, "continue")) {
        return 0;
    }
    
    skip_whitespace_and_comments(src, pos, size);
    
    if (*pos < size && src[*pos] == ';') {
        (*pos)++;
    }
    
    // For now, just emit a comment - would need loop context tracking for real continues
    tty_putstr("Warning: continue statement not fully implemented\n");
    
    return 1;
}

// Parse compound statement (block)
static int parse_compound_statement(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    if (*pos >= size || src[*pos] != '{') {
        return 0;
    }
    (*pos)++;
    
    compiler_enter_scope(ctx);
    
    skip_whitespace_and_comments(src, pos, size);
    
    while (*pos < size && src[*pos] != '}') {
        if (parse_statement(ctx, src, pos, size) != 0) {
            compiler_exit_scope(ctx);
            return -1;
        }
        skip_whitespace_and_comments(src, pos, size);
    }
    
    if (*pos >= size || src[*pos] != '}') {
        tty_putstr("Error: Expected '}' to close block\n");
        compiler_exit_scope(ctx);
        return -1;
    }
    (*pos)++;
    
    compiler_exit_scope(ctx);
    return 1;
}

// Parse statement
static int parse_statement(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    skip_whitespace_and_comments(src, pos, size);
    
    if (*pos >= size) return 0;
    
    // Try compound statement
    if (parse_compound_statement(ctx, src, pos, size)) {
        return 0;
    }
    
    // Try return statement
    if (parse_return_statement(ctx, src, pos, size)) {
        return 0;
    }
    
    // Try if statement
    if (parse_if_statement(ctx, src, pos, size)) {
        return 0;
    }
    
    // Try while statement
    if (parse_while_statement(ctx, src, pos, size)) {
        return 0;
    }
    
    // Try for statement
    if (parse_for_statement(ctx, src, pos, size)) {
        return 0;
    }
    
    // Try do-while statement
    if (parse_do_while_statement(ctx, src, pos, size)) {
        return 0;
    }
    
    // Try break statement
    if (parse_break_statement(ctx, src, pos, size)) {
        return 0;
    }
    
    // Try continue statement
    if (parse_continue_statement(ctx, src, pos, size)) {
        return 0;
    }
    
    // Try printf
    int printf_result = try_parse_printf(ctx, src, pos, size);
    if (printf_result == 1) {
        skip_whitespace_and_comments(src, pos, size);
        if (*pos < size && src[*pos] == ';') {
            (*pos)++;
        }
        return 0;
    } else if (printf_result == -1) {
        return -1;
    }
    
    // Try variable declaration (check for type keywords)
    u32 temp_pos = *pos;
    char temp_name[32];
    if (parse_identifier(src, pos, size, temp_name, sizeof(temp_name)) > 0) {
        data_type_t temp_type = parse_type_name(temp_name);
        if (temp_type != TYPE_STRUCT || compiler_find_struct(ctx, temp_name) >= 0) {
            *pos = temp_pos;
            return parse_variable_declaration(ctx, src, pos, size);
        }
    }
    *pos = temp_pos;
    
    // Try expression statement
    if (parse_expression(ctx, src, pos, size) == 0) {
        skip_whitespace_and_comments(src, pos, size);
        if (*pos < size && src[*pos] == ';') {
            (*pos)++;
        }
        // Pop the expression result
        compiler_emit(ctx, OP_POP, 0);
        return 0;
    }
    
    tty_putstr("Error: Expected statement\n");
    return -1;
}

// Parse #include directive
static int parse_include_directive(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    if (*pos >= size || src[*pos] != '#') {
        return 0;
    }
    (*pos)++;
    
    skip_whitespace(src, pos, size);
    
    if (!match_keyword(src, pos, size, "include")) {
        return 0;
    }
    
    skip_whitespace(src, pos, size);
    
    if (*pos >= size || (src[*pos] != '"' && src[*pos] != '<')) {
        tty_putstr("Error: Expected filename after #include\n");
        return -1;
    }
    
    char quote_char = src[*pos];
    if (quote_char == '<') quote_char = '>';
    (*pos)++;
    
    char filename[64];
    u32 filename_pos = 0;
    
    while (*pos < size && src[*pos] != quote_char && filename_pos < 63) {
        filename[filename_pos++] = src[*pos];
        (*pos)++;
    }
    
    if (*pos >= size || src[*pos] != quote_char) {
        tty_putstr("Error: Unterminated include filename\n");
        return -1;
    }
    (*pos)++;
    filename[filename_pos] = '\0';
    
    // Check if already included
    for (u32 i = 0; i < ctx->included_count; i++) {
        if (strcmp(ctx->included_files[i], filename) == 0) {
            return 1; // Already included, skip
        }
    }
    
    // Add to included list
    if (ctx->included_count >= ctx->included_capacity) {
        tty_putstr("Error: Too many included files\n");
        return -1;
    }
    
    // Store filename in included list
    u32 filename_len = strlength(filename);
    char* stored_filename = &string_storage[string_storage_offset];
    if (string_storage_offset + filename_len + 1 > sizeof(string_storage)) {
        tty_putstr("Error: String storage full for include filename\n");
        return -1;
    }
    
    for (u32 i = 0; i <= filename_len; i++) {
        stored_filename[i] = filename[i];
    }
    ctx->included_files[ctx->included_count++] = stored_filename;
    string_storage_offset += filename_len + 1;
    
    tty_putstr("Including: ");
    tty_putstr(filename);
    tty_putstr("\n");
    
    // Try to load and compile the header file
    u8 header_buffer[8192];
    fat32_file_t header_file;
    
    if (fat32_open_file(filename, &header_file) == 0) {
        u32 header_size = fat32_read_file(&header_file, header_buffer, sizeof(header_buffer));
        tty_putstr("Successfully loaded header file, size: ");
        tty_putdec(header_size);
        tty_putstr(" bytes\n");
        
        // Recursively compile the header file
        u32 header_pos = 0;
        while (header_pos < header_size) {
            skip_whitespace_and_comments((char*)header_buffer, &header_pos, header_size);
            
            if (header_pos >= header_size) break;
            
            // Parse header file contents (only declarations, not definitions)
            // Try include directive
            if (parse_include_directive(ctx, (char*)header_buffer, &header_pos, header_size)) {
                continue;
            }
            
            // Try struct definition
            if (parse_struct_definition(ctx, (char*)header_buffer, &header_pos, header_size)) {
                continue;
            }
            
            // Try function declaration (not definition)
            u32 temp_pos = header_pos;
            if (parse_function(ctx, (char*)header_buffer, &header_pos, header_size)) {
                continue;
            }
            header_pos = temp_pos;
            
            // Try variable declaration
            temp_pos = header_pos;
            if (parse_variable_declaration(ctx, (char*)header_buffer, &header_pos, header_size) == 0) {
                continue;
            }
            header_pos = temp_pos;
            
            // Skip unknown lines in header files
            while (header_pos < header_size && header_buffer[header_pos] != '\n') {
                header_pos++;
            }
            if (header_pos < header_size) header_pos++;
        }
    } else {
        tty_putstr("Warning: Could not load header file '");
        tty_putstr(filename);
        tty_putstr("'\n");
    }
    
    return 1;
}

// Parse #define directive
static int parse_define_directive(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    if (*pos >= size || src[*pos] != '#') {
        return 0;
    }
    (*pos)++;
    
    skip_whitespace(src, pos, size);
    
    if (!match_keyword(src, pos, size, "define")) {
        return 0;
    }
    
    skip_whitespace(src, pos, size);
    
    char macro_name[32];
    if (parse_identifier(src, pos, size, macro_name, sizeof(macro_name)) == 0) {
        tty_putstr("Error: Expected macro name after #define\n");
        return -1;
    }
    
    skip_whitespace(src, pos, size);
    
    // Skip macro value for now (just consume until end of line)
    while (*pos < size && src[*pos] != '\n') {
        (*pos)++;
    }
    if (*pos < size) (*pos)++; // Skip newline
    
    tty_putstr("Defined macro: ");
    tty_putstr(macro_name);
    tty_putstr("\n");
    
    return 1;
}

// Parse #ifdef directive
static int parse_ifdef_directive(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    if (*pos >= size || src[*pos] != '#') {
        return 0;
    }
    
    u32 temp_pos = *pos;
    (*pos)++;
    
    skip_whitespace(src, pos, size);
    
    if (!match_keyword(src, pos, size, "ifdef") && !match_keyword(src, pos, size, "ifndef") && 
        !match_keyword(src, pos, size, "if") && !match_keyword(src, pos, size, "endif") &&
        !match_keyword(src, pos, size, "else")) {
        *pos = temp_pos;
        return 0;
    }
    
    // For now, just skip the entire directive
    while (*pos < size && src[*pos] != '\n') {
        (*pos)++;
    }
    if (*pos < size) (*pos)++; // Skip newline
    
    return 1;
}

// Parse any preprocessor directive
static int parse_preprocessor_directive(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    if (*pos >= size || src[*pos] != '#') {
        return 0;
    }
    
    // Try include directive
    if (parse_include_directive(ctx, src, pos, size)) {
        return 1;
    }
    
    // Try define directive
    if (parse_define_directive(ctx, src, pos, size)) {
        return 1;
    }
    
    // Try ifdef/ifndef/if/endif/else directives
    if (parse_ifdef_directive(ctx, src, pos, size)) {
        return 1;
    }
    
    // Unknown preprocessor directive - skip it
    while (*pos < size && src[*pos] != '\n') {
        (*pos)++;
    }
    if (*pos < size) (*pos)++; // Skip newline
    
    return 1;
}

// Main compilation function
int compile_c_source(const char* source, u32 source_size, const char* output_file) {
    compiler_ctx_t ctx;
    compiler_init(&ctx);
    
    u32 pos = 0;
    
    tty_putstr("Starting C compilation...\n");
    
    // Parse top-level constructs
    while (pos < source_size) {
        skip_whitespace_and_comments(source, &pos, source_size);
        
        if (pos >= source_size) break;
        
        // Try any preprocessor directive
        if (parse_preprocessor_directive(&ctx, source, &pos, source_size)) {
            continue;
        }
        
        // Try struct definition
        if (parse_struct_definition(&ctx, source, &pos, source_size)) {
            continue;
        }
        
        // Try function definition
        if (parse_function(&ctx, source, &pos, source_size)) {
            continue;
        }
        
        // Try variable declaration
        u32 temp_pos = pos;
        if (parse_variable_declaration(&ctx, source, &pos, source_size) == 0) {
            continue;
        }
        pos = temp_pos;
        
        // Try statement (for global statements)
        if (parse_statement(&ctx, source, &pos, source_size) == 0) {
            continue;
        }
        
        // If nothing matched, error
        tty_putstr("Error: Unexpected token at position ");
        tty_putdec(pos);
        tty_putstr(": '");
        if (pos < source_size) {
            tty_putchar_internal(source[pos]);
        }
        tty_putstr("'\n");
        
        // Show some context
        tty_putstr("Context: ");
        for (u32 i = pos; i < source_size && i < pos + 20; i++) {
            tty_putchar_internal(source[i]);
        }
        tty_putstr("...\n");
        
        tty_putstr("Compilation failed\n");
        return -1;
    }
    
    // Set entry point to start of main call code
    u32 entry_point = ctx.code_size;
    
    // Generate call to main() function if it exists
    int main_func_id = compiler_find_function(&ctx, "main");
    if (main_func_id >= 0) {
        tty_putstr("Generating call to main() at PC=");
        tty_putdec(entry_point);
        tty_putstr("...\n");
        compiler_emit(&ctx, OP_CALL, main_func_id);
    } else {
        tty_putstr("Warning: No main() function found\n");
    }
    
    // Emit halt instruction
    compiler_emit(&ctx, OP_HALT, 0);
    
    tty_putstr("Generated ");
    tty_putdec(ctx.code_size);
    tty_putstr(" instructions\n");
    
    tty_putstr("Variables: ");
    tty_putdec(ctx.total_variables_processed);
    tty_putstr("\n");
    
    tty_putstr("Functions: ");
    tty_putdec(ctx.function_count);
    tty_putstr("\n");
    
    tty_putstr("Structs: ");
    tty_putdec(ctx.struct_count);
    tty_putstr("\n");
    
    // Create executable file (simplified version)
    exec_header_t header;
    header.magic = EXEC_MAGIC;
    header.version = EXEC_VERSION;
    header.code_size = ctx.code_size * sizeof(instruction_t) + ctx.string_count * sizeof(u32);
    header.data_size = string_storage_offset;
    header.entry_point = entry_point;
    header.string_count = ctx.string_count;
    
    // Write executable file
    u8* file_buffer = (u8*)0x700000;
    u32 file_size = 0;
    
    // Write header
    for (u32 i = 0; i < sizeof(exec_header_t); i++) {
        file_buffer[file_size++] = ((u8*)&header)[i];
    }
    
    // Write code (instructions)
    for (u32 i = 0; i < ctx.code_size; i++) {
        for (u32 j = 0; j < sizeof(instruction_t); j++) {
            file_buffer[file_size++] = ((u8*)&ctx.code[i])[j];
        }
    }
    
    // Write string pointers
    for (u32 i = 0; i < ctx.string_count; i++) {
        u32 offset = (u32)(ctx.strings[i] - string_storage);
        for (u32 j = 0; j < sizeof(u32); j++) {
            file_buffer[file_size++] = ((u8*)&offset)[j];
        }
    }
    
    // Write string data
    for (u32 i = 0; i < string_storage_offset; i++) {
        file_buffer[file_size++] = string_storage[i];
    }
    
    // Create file on filesystem
    if (fat32_create_file(output_file, file_buffer, file_size) != 0) {
        tty_putstr("Error: Could not create output file\n");
        return -1;
    }
    
    tty_putstr("Compilation successful: ");
    tty_putstr(output_file);
    tty_putstr("\n");
    
    compiler_free(&ctx);
    return 0;
}