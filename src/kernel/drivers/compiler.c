//
// Comprehensive C compiler for DanOS
// Supports variables, pointers, loops, conditionals, functions
//

#include "compiler.h"
#include "tty.h"
#include "string.h"
#include "fat32.h"
#include "exec.h"

#define MAX_CODE 4096
#define MAX_STRINGS 128
#define MAX_VARS 64

static instruction_t static_code[MAX_CODE];
static char* static_strings[MAX_STRINGS];
static variable_t static_variables[MAX_VARS];
static char string_storage[8192];
static u32 string_storage_offset = 0;

// Forward declarations for recursive parsing
static int parse_expression(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size);
static int parse_statement(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size);

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
    ctx->stack_offset = 0;
    string_storage_offset = 0;
}

void compiler_free(compiler_ctx_t* ctx) {
    ctx->code_size = 0;
    ctx->string_count = 0;
    ctx->variable_count = 0;
    ctx->stack_offset = 0;
    string_storage_offset = 0;
}

int compiler_emit(compiler_ctx_t* ctx, opcode_t op, s32 operand) {
    if (ctx->code_size >= ctx->code_capacity) {
        tty_putstr("Error: Code buffer full\n");
        return -1;
    }
    
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

int compiler_add_variable(compiler_ctx_t* ctx, const char* name, int is_pointer) {
    if (ctx->variable_count >= ctx->variable_capacity) {
        tty_putstr("Error: Variable table full\n");
        return -1;
    }
    
    // Copy variable name
    int i;
    for (i = 0; i < 31 && name[i] != '\0'; i++) {
        ctx->variables[ctx->variable_count].name[i] = name[i];
    }
    ctx->variables[ctx->variable_count].name[i] = '\0';
    
    ctx->variables[ctx->variable_count].address = ctx->stack_offset++;
    ctx->variables[ctx->variable_count].is_pointer = is_pointer;
    
    return ctx->variable_count++;
}

int compiler_find_variable(compiler_ctx_t* ctx, const char* name) {
    for (u32 i = 0; i < ctx->variable_count; i++) {
        if (strcmp(ctx->variables[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
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

// Parse printf statement
static int try_parse_printf(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    u32 start = *pos;
    
    skip_whitespace(src, pos, size);
    
    if (*pos + 6 > size || strncmp(&src[*pos], "printf", 6) != 0) {
        *pos = start;
        return 0;
    }
    *pos += 6;
    
    skip_whitespace(src, pos, size);
    
    if (*pos >= size || src[*pos] != '(') {
        *pos = start;
        return 0;
    }
    (*pos)++;
    
    skip_whitespace(src, pos, size);
    
    if (*pos >= size || src[*pos] != '"') {
        *pos = start;
        return 0;
    }
    (*pos)++;
    
    // Extract string with escape sequences
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
                case '0': str_buf[str_pos++] = '\0'; break;
                default: str_buf[str_pos++] = src[*pos]; break;
            }
            (*pos)++;
        } else {
            str_buf[str_pos++] = src[*pos];
            (*pos)++;
        }
    }
    str_buf[str_pos] = '\0';
    
    if (*pos >= size || src[*pos] != '"') {
        tty_putstr("Error: Unterminated string\n");
        return -1;
    }
    (*pos)++;
    
    skip_whitespace(src, pos, size);
    
    if (*pos >= size || src[*pos] != ')') {
        tty_putstr("Error: Expected ')' after printf string\n");
        return -1;
    }
    (*pos)++;
    
    skip_whitespace(src, pos, size);
    
    if (*pos >= size || src[*pos] != ';') {
        tty_putstr("Error: Expected ';' after printf, found: '");
        if (*pos < size) {
            tty_putchar_internal(src[*pos]);
        } else {
            tty_putstr("EOF");
        }
        tty_putstr("'\n");
        return -1;
    }
    (*pos)++;
    
    // Emit string print
    int str_id = compiler_add_string(ctx, str_buf);
    if (str_id < 0) return -1;
    
    compiler_emit(ctx, OP_PRINT_STR, str_id);
    
    return 1;
}

// Parse variable declaration (int x = 5; or int x;)
static int try_parse_declaration(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    u32 start = *pos;
    
    skip_whitespace(src, pos, size);
    
    // Check for type (int, char, etc.)
    char type[32];
    if (!is_alpha(src[*pos])) {
        *pos = start;
        return 0;
    }
    
    parse_identifier(src, pos, size, type, sizeof(type));
    
    // Only support int for now
    if (strcmp(type, "int") != 0 && strcmp(type, "char") != 0) {
        *pos = start;
        return 0;
    }
    
    skip_whitespace(src, pos, size);
    
    // Check for pointer
    int is_pointer = 0;
    if (*pos < size && src[*pos] == '*') {
        is_pointer = 1;
        (*pos)++;
        skip_whitespace(src, pos, size);
    }
    
    // Get variable name
    char var_name[32];
    if (!is_alpha(src[*pos])) {
        tty_putstr("Error: Expected variable name\n");
        return -1;
    }
    
    parse_identifier(src, pos, size, var_name, sizeof(var_name));
    
    skip_whitespace(src, pos, size);
    
    // Add variable
    int var_idx = compiler_add_variable(ctx, var_name, is_pointer);
    if (var_idx < 0) return -1;
    
    // Check for initialization
    if (*pos < size && src[*pos] == '=') {
        (*pos)++;
        skip_whitespace(src, pos, size);
        
        // Parse expression
        if (parse_expression(ctx, src, pos, size) < 0) {
            return -1;
        }
        
        // Store value
        compiler_emit(ctx, OP_STORE, ctx->variables[var_idx].address);
    } else {
        // Initialize to 0
        compiler_emit(ctx, OP_PUSH, 0);
        compiler_emit(ctx, OP_STORE, ctx->variables[var_idx].address);
    }
    
    skip_whitespace(src, pos, size);
    
    if (*pos >= size || src[*pos] != ';') {
        tty_putstr("Error: Expected ';'\n");
        return -1;
    }
    (*pos)++;
    
    return 1;
}

// Parse primary expression (number, variable, or parenthesized expression)
static int parse_primary(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    skip_whitespace(src, pos, size);
    
    if (*pos >= size) return -1;
    
    // Number
    if (is_digit(src[*pos]) || src[*pos] == '-') {
        int num = parse_number(src, pos, size);
        compiler_emit(ctx, OP_PUSH, num);
        return 0;
    }
    
    // Parenthesized expression
    if (src[*pos] == '(') {
        (*pos)++;
        if (parse_expression(ctx, src, pos, size) < 0) return -1;
        skip_whitespace(src, pos, size);
        if (*pos >= size || src[*pos] != ')') {
            tty_putstr("Error: Expected ')'\n");
            return -1;
        }
        (*pos)++;
        return 0;
    }
    
    // Variable or address-of
    if (is_alpha(src[*pos])) {
        char var_name[32];
        parse_identifier(src, pos, size, var_name, sizeof(var_name));
        
        int var_idx = compiler_find_variable(ctx, var_name);
        if (var_idx < 0) {
            tty_putstr("Error: Undefined variable: ");
            tty_putstr(var_name);
            tty_putstr("\n");
            return -1;
        }
        
        compiler_emit(ctx, OP_LOAD, ctx->variables[var_idx].address);
        return 0;
    }
    
    tty_putstr("Error: Unexpected token in expression\n");
    return -1;
}

// Parse multiplicative expression (* / %)
static int parse_term(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    if (parse_primary(ctx, src, pos, size) < 0) return -1;
    
    while (*pos < size) {
        skip_whitespace(src, pos, size);
        if (*pos >= size) break;
        
        char op = src[*pos];
        if (op != '*' && op != '/' && op != '%') break;
        
        (*pos)++;
        if (parse_primary(ctx, src, pos, size) < 0) return -1;
        
        if (op == '*') compiler_emit(ctx, OP_MUL, 0);
        else if (op == '/') compiler_emit(ctx, OP_DIV, 0);
        else compiler_emit(ctx, OP_MOD, 0);
    }
    
    return 0;
}

// Parse additive expression (+ -)
static int parse_arith_expr(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    if (parse_term(ctx, src, pos, size) < 0) return -1;
    
    while (*pos < size) {
        skip_whitespace(src, pos, size);
        if (*pos >= size) break;
        
        char op = src[*pos];
        if (op != '+' && op != '-') break;
        
        (*pos)++;
        if (parse_term(ctx, src, pos, size) < 0) return -1;
        
        if (op == '+') compiler_emit(ctx, OP_ADD, 0);
        else compiler_emit(ctx, OP_SUB, 0);
    }
    
    return 0;
}

// Parse comparison expression (< > <= >= == !=)
static int parse_comparison(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    if (parse_arith_expr(ctx, src, pos, size) < 0) return -1;
    
    skip_whitespace(src, pos, size);
    if (*pos >= size) return 0;
    
    // Check for comparison operators
    if (*pos + 1 < size) {
        if (src[*pos] == '<' && src[*pos + 1] == '=') {
            *pos += 2;
            if (parse_arith_expr(ctx, src, pos, size) < 0) return -1;
            compiler_emit(ctx, OP_CMP_LE, 0);
            return 0;
        } else if (src[*pos] == '>' && src[*pos + 1] == '=') {
            *pos += 2;
            if (parse_arith_expr(ctx, src, pos, size) < 0) return -1;
            compiler_emit(ctx, OP_CMP_GE, 0);
            return 0;
        } else if (src[*pos] == '=' && src[*pos + 1] == '=') {
            *pos += 2;
            if (parse_arith_expr(ctx, src, pos, size) < 0) return -1;
            compiler_emit(ctx, OP_CMP_EQ, 0);
            return 0;
        } else if (src[*pos] == '!' && src[*pos + 1] == '=') {
            *pos += 2;
            if (parse_arith_expr(ctx, src, pos, size) < 0) return -1;
            compiler_emit(ctx, OP_CMP_NE, 0);
            return 0;
        }
    }
    
    if (src[*pos] == '<') {
        (*pos)++;
        if (parse_arith_expr(ctx, src, pos, size) < 0) return -1;
        compiler_emit(ctx, OP_CMP_LT, 0);
    } else if (src[*pos] == '>') {
        (*pos)++;
        if (parse_arith_expr(ctx, src, pos, size) < 0) return -1;
        compiler_emit(ctx, OP_CMP_GT, 0);
    }
    
    return 0;
}

// Parse full expression
static int parse_expression(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    return parse_comparison(ctx, src, pos, size);
}

// Parse assignment statement
static int try_parse_assignment(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    u32 start = *pos;
    
    skip_whitespace(src, pos, size);
    
    if (!is_alpha(src[*pos])) {
        *pos = start;
        return 0;
    }
    
    // Get variable name
    char var_name[32];
    parse_identifier(src, pos, size, var_name, sizeof(var_name));
    
    skip_whitespace(src, pos, size);
    
    if (*pos >= size || src[*pos] != '=') {
        *pos = start;
        return 0;
    }
    (*pos)++;
    
    skip_whitespace(src, pos, size);
    
    // Find variable
    int var_idx = compiler_find_variable(ctx, var_name);
    if (var_idx < 0) {
        tty_putstr("Error: Undefined variable: ");
        tty_putstr(var_name);
        tty_putstr("\n");
        return -1;
    }
    
    // Parse expression
    if (parse_expression(ctx, src, pos, size) < 0) return -1;
    
    // Store value
    compiler_emit(ctx, OP_STORE, ctx->variables[var_idx].address);
    
    skip_whitespace(src, pos, size);
    
    if (*pos >= size || src[*pos] != ';') {
        tty_putstr("Error: Expected ';'\n");
        return -1;
    }
    (*pos)++;
    
    return 1;
}

// Parse assignment without semicolon (for use in for loop increment)
static int parse_assignment_no_semi(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    u32 start = *pos;
    
    skip_whitespace(src, pos, size);
    
    if (*pos >= size || !is_alpha(src[*pos])) {
        *pos = start;
        return 0;
    }
    
    // Get variable name
    char var_name[32];
    parse_identifier(src, pos, size, var_name, sizeof(var_name));
    
    skip_whitespace(src, pos, size);
    
    if (*pos >= size || src[*pos] != '=') {
        *pos = start;
        return 0;
    }
    (*pos)++;
    
    skip_whitespace(src, pos, size);
    
    // Find variable
    int var_idx = compiler_find_variable(ctx, var_name);
    if (var_idx < 0) {
        tty_putstr("Error: Undefined variable in for loop: ");
        tty_putstr(var_name);
        tty_putstr("\n");
        return -1;
    }
    
    // Parse expression
    if (parse_expression(ctx, src, pos, size) < 0) return -1;
    
    // Store value
    compiler_emit(ctx, OP_STORE, ctx->variables[var_idx].address);
    
    return 1;
}

// Parse if statement
static int try_parse_if(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    u32 start = *pos;
    
    skip_whitespace(src, pos, size);
    
    if (*pos + 2 > size || strncmp(&src[*pos], "if", 2) != 0 || is_alnum(src[*pos + 2])) {
        *pos = start;
        return 0;
    }
    *pos += 2;
    
    skip_whitespace(src, pos, size);
    
    if (*pos >= size || src[*pos] != '(') {
        tty_putstr("Error: Expected '(' after if\n");
        return -1;
    }
    (*pos)++;
    
    // Parse condition
    if (parse_expression(ctx, src, pos, size) < 0) return -1;
    
    skip_whitespace(src, pos, size);
    
    if (*pos >= size || src[*pos] != ')') {
        tty_putstr("Error: Expected ')'\n");
        return -1;
    }
    (*pos)++;
    
    // Emit conditional jump (will be patched)
    int jz_addr = compiler_emit(ctx, OP_JZ, 0);
    
    skip_whitespace(src, pos, size);
    
    // Parse then block
    if (*pos < size && src[*pos] == '{') {
        (*pos)++;
        while (*pos < size && src[*pos] != '}') {
            if (parse_statement(ctx, src, pos, size) < 0) return -1;
            skip_whitespace(src, pos, size);
        }
        if (*pos >= size || src[*pos] != '}') {
            tty_putstr("Error: Expected '}'\n");
            return -1;
        }
        (*pos)++;
    } else {
        if (parse_statement(ctx, src, pos, size) < 0) return -1;
    }
    
    // Check for else
    skip_whitespace(src, pos, size);
    int else_jmp_addr = -1;
    if (*pos + 4 <= size && strncmp(&src[*pos], "else", 4) == 0 && !is_alnum(src[*pos + 4])) {
        *pos += 4;
        
        // Emit unconditional jump over else block
        else_jmp_addr = compiler_emit(ctx, OP_JMP, 0);
        
        // Patch conditional jump to here
        ctx->code[jz_addr].operand = ctx->code_size;
        
        skip_whitespace(src, pos, size);
        
        // Parse else block
        if (*pos < size && src[*pos] == '{') {
            (*pos)++;
            while (*pos < size && src[*pos] != '}') {
                if (parse_statement(ctx, src, pos, size) < 0) return -1;
                skip_whitespace(src, pos, size);
            }
            if (*pos >= size || src[*pos] != '}') {
                tty_putstr("Error: Expected '}'\n");
                return -1;
            }
            (*pos)++;
        } else {
            if (parse_statement(ctx, src, pos, size) < 0) return -1;
        }
        
        // Patch jump over else
        ctx->code[else_jmp_addr].operand = ctx->code_size;
    } else {
        // Patch conditional jump
        ctx->code[jz_addr].operand = ctx->code_size;
    }
    
    return 1;
}

// Parse while loop
static int try_parse_while(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    u32 start = *pos;
    
    skip_whitespace(src, pos, size);
    
    if (*pos + 5 > size || strncmp(&src[*pos], "while", 5) != 0 || is_alnum(src[*pos + 5])) {
        *pos = start;
        return 0;
    }
    *pos += 5;
    
    skip_whitespace(src, pos, size);
    
    if (*pos >= size || src[*pos] != '(') {
        tty_putstr("Error: Expected '(' after while\n");
        return -1;
    }
    (*pos)++;
    
    // Remember loop start
    int loop_start = ctx->code_size;
    
    // Parse condition
    if (parse_expression(ctx, src, pos, size) < 0) return -1;
    
    skip_whitespace(src, pos, size);
    
    if (*pos >= size || src[*pos] != ')') {
        tty_putstr("Error: Expected ')'\n");
        return -1;
    }
    (*pos)++;
    
    // Emit conditional jump (will be patched)
    int jz_addr = compiler_emit(ctx, OP_JZ, 0);
    
    skip_whitespace(src, pos, size);
    
    // Parse loop body
    if (*pos < size && src[*pos] == '{') {
        (*pos)++;
        while (*pos < size && src[*pos] != '}') {
            if (parse_statement(ctx, src, pos, size) < 0) return -1;
            skip_whitespace(src, pos, size);
        }
        if (*pos >= size || src[*pos] != '}') {
            tty_putstr("Error: Expected '}'\n");
            return -1;
        }
        (*pos)++;
    } else {
        if (parse_statement(ctx, src, pos, size) < 0) return -1;
    }
    
    // Jump back to condition
    compiler_emit(ctx, OP_JMP, loop_start);
    
    // Patch conditional jump
    ctx->code[jz_addr].operand = ctx->code_size;
    
    return 1;
}

// Parse for loop
static int try_parse_for(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    u32 start = *pos;
    
    skip_whitespace(src, pos, size);
    
    if (*pos + 3 > size || strncmp(&src[*pos], "for", 3) != 0 || is_alnum(src[*pos + 3])) {
        *pos = start;
        return 0;
    }
    *pos += 3;
    
    skip_whitespace(src, pos, size);
    
    if (*pos >= size || src[*pos] != '(') {
        tty_putstr("Error: Expected '(' after for\n");
        return -1;
    }
    (*pos)++;
    
    skip_whitespace(src, pos, size);
    
    // Parse initialization
    if (*pos < size && src[*pos] != ';') {
        int result = try_parse_declaration(ctx, src, pos, size);
        if (result == 0) {
            result = try_parse_assignment(ctx, src, pos, size);
        }
        if (result < 0) return -1;
        if (result == 0) {
            tty_putstr("Error: Invalid for loop initialization\n");
            return -1;
        }
    } else {
        (*pos)++; // Skip semicolon
    }
    
    skip_whitespace(src, pos, size);
    
    // Remember condition start
    int cond_start = ctx->code_size;
    
    // Parse condition
    if (*pos < size && src[*pos] != ';') {
        if (parse_expression(ctx, src, pos, size) < 0) return -1;
    } else {
        compiler_emit(ctx, OP_PUSH, 1); // Always true
    }
    
    skip_whitespace(src, pos, size);
    
    if (*pos >= size || src[*pos] != ';') {
        tty_putstr("Error: Expected ';' after for condition\n");
        return -1;
    }
    (*pos)++;
    
    // Emit conditional jump to exit (will be patched)
    int jz_addr = compiler_emit(ctx, OP_JZ, 0);
    
    skip_whitespace(src, pos, size);
    
    // Skip the increment for now - we'll handle it later
    u32 increment_start = *pos;
    if (*pos < size && src[*pos] != ')') {
        // Find the closing parenthesis
        int paren_depth = 1;
        while (*pos < size && paren_depth > 0) {
            if (src[*pos] == '(') paren_depth++;
            else if (src[*pos] == ')') paren_depth--;
            if (paren_depth > 0) (*pos)++;
        }
    }
    
    if (*pos >= size || src[*pos] != ')') {
        tty_putstr("Error: Expected ')' after for loop\n");
        return -1;
    }
    (*pos)++;
    
    skip_whitespace(src, pos, size);
    
    // Parse loop body
    if (*pos < size && src[*pos] == '{') {
        (*pos)++;
        while (*pos < size && src[*pos] != '}') {
            if (parse_statement(ctx, src, pos, size) < 0) return -1;
            skip_whitespace(src, pos, size);
        }
        if (*pos >= size || src[*pos] != '}') {
            tty_putstr("Error: Expected '}'\n");
            return -1;
        }
        (*pos)++;
    } else {
        if (parse_statement(ctx, src, pos, size) < 0) return -1;
    }
    
    // Now handle increment
    u32 temp_pos = increment_start;
    if (temp_pos < *pos && src[temp_pos] != ')') {
        int result = parse_assignment_no_semi(ctx, src, &temp_pos, *pos);
        if (result < 0) return -1;
        if (result == 0) {
            tty_putstr("Error: Invalid for loop increment\n");
            return -1;
        }
    }
    
    // Jump back to condition
    compiler_emit(ctx, OP_JMP, cond_start);
    
    // Patch conditional jump to exit
    ctx->code[jz_addr].operand = ctx->code_size;
    
    return 1;
}

// Parse a statement
static int parse_statement(compiler_ctx_t* ctx, const char* src, u32* pos, u32 size) {
    skip_whitespace(src, pos, size);
    
    if (*pos >= size) return 0;
    
    // Try each statement type
    int result;
    
    result = try_parse_printf(ctx, src, pos, size);
    if (result != 0) {
        if (result > 0) tty_putstr("[PARSED: printf]\n");
        return result;
    }
    
    result = try_parse_if(ctx, src, pos, size);
    if (result != 0) {
        if (result > 0) tty_putstr("[PARSED: if]\n");
        return result;
    }
    
    result = try_parse_while(ctx, src, pos, size);
    if (result != 0) {
        if (result > 0) tty_putstr("[PARSED: while]\n");
        return result;
    }
    
    result = try_parse_for(ctx, src, pos, size);
    if (result != 0) {
        if (result > 0) tty_putstr("[PARSED: for]\n");
        return result;
    }
    
    result = try_parse_declaration(ctx, src, pos, size);
    if (result != 0) {
        if (result > 0) tty_putstr("[PARSED: declaration]\n");
        return result;
    }
    
    result = try_parse_assignment(ctx, src, pos, size);
    if (result != 0) {
        if (result > 0) tty_putstr("[PARSED: assignment]\n");
        return result;
    }
    
    // Skip single-line comments
    if (*pos + 1 < size && src[*pos] == '/' && src[*pos + 1] == '/') {
        while (*pos < size && src[*pos] != '\n') (*pos)++;
        return 0;
    }
    
    // Skip multi-line comments
    if (*pos + 1 < size && src[*pos] == '/' && src[*pos + 1] == '*') {
        *pos += 2;
        while (*pos + 1 < size) {
            if (src[*pos] == '*' && src[*pos + 1] == '/') {
                *pos += 2;
                break;
            }
            (*pos)++;
        }
        return 0;
    }
    
    // Skip empty statements
    if (src[*pos] == ';') {
        (*pos)++;
        return 0;
    }
    
    // Check for unrecognized statement - likely a typo or unsupported feature
    if (is_alpha(src[*pos])) {
        char identifier[32];
        u32 temp_pos = *pos;
        parse_identifier(src, &temp_pos, size, identifier, sizeof(identifier));
        
        tty_putstr("Error: Unknown statement or function: '");
        tty_putstr(identifier);
        tty_putstr("'\n");
        tty_putstr("Hint: Did you mean 'printf'?\n");
        return -1;
    }
    
    // Unknown token
    if (*pos < size) {
        tty_putstr("Error: Unexpected character: '");
        tty_putchar_internal(src[*pos]);
        tty_putstr("'\n");
        return -1;
    }
    
    return 0;
}

// Main compiler entry point
int compile_c_source(const char* source, u32 source_size, const char* output_file) {
    compiler_ctx_t ctx;
    compiler_init(&ctx);
    
    tty_putstr("Compiling source code...\n");
    
    u32 pos = 0;
    
    // Check for main() function wrapper
    skip_whitespace(source, &pos, source_size);
    
    // Look for "int main()" pattern
    int has_main = 0;
    u32 saved_pos = pos;
    
    if (pos + 3 < source_size && strncmp(&source[pos], "int", 3) == 0 && is_whitespace(source[pos + 3])) {
        u32 temp_pos = pos + 3;
        skip_whitespace(source, &temp_pos, source_size);
        
        if (temp_pos + 4 < source_size && strncmp(&source[temp_pos], "main", 4) == 0) {
            temp_pos += 4;
            skip_whitespace(source, &temp_pos, source_size);
            
            if (temp_pos < source_size && source[temp_pos] == '(') {
                temp_pos++;
                skip_whitespace(source, &temp_pos, source_size);
                
                if (temp_pos < source_size && source[temp_pos] == ')') {
                    temp_pos++;
                    skip_whitespace(source, &temp_pos, source_size);
                    
                    if (temp_pos < source_size && source[temp_pos] == '{') {
                        // Found main() function!
                        has_main = 1;
                        pos = temp_pos + 1; // Skip the opening brace
                        
                        // Find the matching closing brace
                        u32 brace_count = 1;
                        u32 end_pos = pos;
                        while (end_pos < source_size && brace_count > 0) {
                            if (source[end_pos] == '{') brace_count++;
                            else if (source[end_pos] == '}') brace_count--;
                            if (brace_count > 0) end_pos++;
                        }
                        source_size = end_pos; // Only parse inside main()
                    }
                }
            }
        }
    }
    
    if (!has_main) {
        pos = saved_pos; // Reset if no main found
    }
    
    // Parse all statements
    while (pos < source_size) {
        int result = parse_statement(&ctx, source, &pos, source_size);
        if (result < 0) {
            // Show context around error
            tty_putstr("Error at position ");
            tty_putdec(pos);
            tty_putstr(":\n");
            
            // Show some context
            u32 start = pos > 20 ? pos - 20 : 0;
            u32 end = pos + 20 < source_size ? pos + 20 : source_size;
            
            tty_putstr("...");
            for (u32 i = start; i < end; i++) {
                if (source[i] == '\n') {
                    tty_putstr("\\n");
                } else if (source[i] >= 32 && source[i] < 127) {
                    tty_putchar_internal(source[i]);
                }
            }
            tty_putstr("...\n");
            
            tty_putstr("Compilation failed\n");
            return -1;
        }
        skip_whitespace(source, &pos, source_size);
    }
    
    // Emit halt instruction
    compiler_emit(&ctx, OP_HALT, 0);
    
    tty_putstr("Generated ");
    tty_putdec(ctx.code_size);
    tty_putstr(" instructions\n");
    
    // Debug: Show generated bytecode
    tty_putstr("Bytecode:\n");
    for (u32 i = 0; i < ctx.code_size && i < 20; i++) {
        tty_putstr("  ");
        tty_putdec(i);
        tty_putstr(": OP=");
        tty_putdec(ctx.code[i].op);
        tty_putstr(" ARG=");
        tty_putdec(ctx.code[i].operand);
        tty_putstr("\n");
    }
    if (ctx.code_size > 20) {
        tty_putstr("  ... (");
        tty_putdec(ctx.code_size - 20);
        tty_putstr(" more)\n");
    }
    
    tty_putstr("String count: ");
    tty_putdec(ctx.string_count);
    tty_putstr("\n");
    tty_putstr("Variables: ");
    tty_putdec(ctx.variable_count);
    tty_putstr("\n");
    tty_putstr("String data size: ");
    tty_putdec(string_storage_offset);
    tty_putstr(" bytes\n");
    
    // Create executable file
    exec_header_t header;
    header.magic = EXEC_MAGIC;
    header.version = EXEC_VERSION;
    header.code_size = ctx.code_size * sizeof(instruction_t) + ctx.string_count * sizeof(u32);
    header.data_size = string_storage_offset;
    header.entry_point = 0;
    header.string_count = ctx.string_count;
    
    tty_putstr("Total code section: ");
    tty_putdec(header.code_size);
    tty_putstr(" bytes\n");
    tty_putstr("Total data section: ");
    tty_putdec(header.data_size);
    tty_putstr(" bytes\n");
    
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
