/**==============================================
 *                 tsl_token.c
 *  TSLang token implementation for Dan-OS
 *  Ported from EpiCompiler
 *  Author: shirosaaki
 *  Date: 2025-12-05
 *=============================================**/

#include "tsl_token.h"
#include "kmalloc.h"
#include "string.h"

void token_list_init(TokenList *list)
{
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void token_list_free(TokenList *list)
{
    if (!list) return;
    for (size_t i = 0; i < list->count; ++i) {
        if (list->items[i].lexeme) {
            kfree(list->items[i].lexeme);
        }
    }
    if (list->items) {
        kfree(list->items);
    }
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

int token_list_push(TokenList *list, Token token)
{
    if (list->count >= list->capacity) {
        size_t new_cap = list->capacity ? list->capacity * 2 : 16;
        Token *new_items = kmalloc(new_cap * sizeof(Token));
        if (!new_items) return -1;
        
        // Copy old items
        for (size_t i = 0; i < list->count; i++) {
            new_items[i] = list->items[i];
        }
        
        if (list->items) {
            kfree(list->items);
        }
        list->items = new_items;
        list->capacity = new_cap;
    }
    list->items[list->count++] = token;
    return 0;
}

Token token_create(TokenType type, char *lexeme, size_t line, size_t column)
{
    Token t;
    t.type = type;
    t.lexeme = lexeme;
    t.line = line;
    t.column = column;
    return t;
}
