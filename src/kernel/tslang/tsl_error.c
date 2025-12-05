/**==============================================
 *                 tsl_error.c
 *  TSLang error handling for Dan-OS
 *  Author: shirosaaki
 *  Date: 2025-12-05
 *=============================================**/

#include "tsl_error.h"
#include "string.h"

void tsl_error_set(TslError *err, const char *msg, size_t line, size_t column)
{
    if (!err) return;
    
    // Copy message (truncate if too long)
    size_t len = strlength(msg);
    if (len >= sizeof(err->message)) {
        len = sizeof(err->message) - 1;
    }
    
    for (size_t i = 0; i < len; i++) {
        err->message[i] = msg[i];
    }
    err->message[len] = '\0';
    
    err->line = line;
    err->column = column;
}

void tsl_error_clear(TslError *err)
{
    if (!err) return;
    err->message[0] = '\0';
    err->line = 0;
    err->column = 0;
}
