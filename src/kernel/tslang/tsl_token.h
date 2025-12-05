/**==============================================
 *                 tsl_token.h
 *  TSLang token definitions for Dan-OS
 *  Ported from EpiCompiler
 *  Author: shirosaaki
 *  Date: 2025-12-05
 *=============================================**/

#ifndef TSL_TOKEN_H_
#define TSL_TOKEN_H_

#include <stddef.h>
#include <stdint.h>

typedef enum TokenType {
    TOK_IDENTIFIER,
    TOK_NUMBER,
    TOK_STRING,
    TOK_OPERATOR,
    TOK_SYMBOL,
    TOK_KEYWORD,
    TOK_NEWLINE,
    TOK_INDENT,
    TOK_DEDENT,
    TOK_EOF,
    TOK_UNKNOWN
} TokenType;

typedef struct Token {
    TokenType type;
    char *lexeme;
    size_t line;
    size_t column;
} Token;

typedef struct TokenList {
    Token *items;
    size_t count;
    size_t capacity;
} TokenList;

// TokenList helpers
void token_list_init(TokenList *list);
void token_list_free(TokenList *list);
int token_list_push(TokenList *list, Token token);

// Utility to create token
Token token_create(TokenType type, char *lexeme, size_t line, size_t column);

#endif /* !TSL_TOKEN_H_ */
