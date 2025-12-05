/**==============================================
 *                 tsl_error.h
 *  TSLang error handling for Dan-OS
 *  Author: shirosaaki
 *  Date: 2025-12-05
 *=============================================**/

#ifndef TSL_ERROR_H_
#define TSL_ERROR_H_

#include <stddef.h>

typedef struct TslError {
    char message[256];
    size_t line;
    size_t column;
} TslError;

// Create error (copies message into struct)
void tsl_error_set(TslError *err, const char *msg, size_t line, size_t column);

// Clear error
void tsl_error_clear(TslError *err);

#endif /* !TSL_ERROR_H_ */
