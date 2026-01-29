//
// Created by dan13615 on 11/20/24.
//

#ifndef STR_H
  #define STR_H

  #include <stddef.h>

  int strlength(const char* str);
  int strcmp(const char* str1, const char* str2);
  int strncmp(const char* str1, const char* str2, size_t n);
  int strcasecmp(const char* str1, const char* str2);
  char* strchr(const char* str, int c);
  char* strdup_k(const char* str);  // Kernel strdup using kmalloc
  void* memcpy_k(void* dest, const void* src, size_t n);
  void* memset_k(void* s, int c, size_t n);
  int isalpha_k(int c);
  int isdigit_k(int c);
  int isalnum_k(int c);
  int isspace_k(int c);
  int toupper_k(int c);
  int tolower_k(int c);

#endif //STR_H
