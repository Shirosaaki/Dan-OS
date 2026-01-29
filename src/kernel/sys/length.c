//
// Created by dan13615 on 11/20/24.
//

#include <kernel/sys/string.h>

int strlength(const char* str) {
    int len = 0;
    while (str[len])
      len++;
    return len;
}