//
// Created by dan13615 on 11/20/24.
//

#ifndef VGA_H
  #define VGA_H

    #include <stdint.h>
    #include <stdbool.h>
    #include <stddef.h>

    enum VGA_COLOR {
      PRINT_COLOR_BLACK = 0,
      PRINT_COLOR_BLUE = 1,
      PRINT_COLOR_GREEN = 2,
      PRINT_COLOR_CYAN = 3,
      PRINT_COLOR_RED = 4,
      PRINT_COLOR_MAGENTA = 5,
      PRINT_COLOR_BROWN = 6,
      PRINT_COLOR_LIGHT_GREY = 7,
      PRINT_COLOR_DARK_GREY = 8,
      PRINT_COLOR_LIGHT_BLUE = 9,
      PRINT_COLOR_LIGHT_GREEN = 10,
      PRINT_COLOR_LIGHT_CYAN = 11,
      PRINT_COLOR_LIGHT_RED = 12,
      PRINT_COLOR_PINK = 13,
      PRINT_COLOR_YELLOW = 14,
      PRINT_COLOR_WHITE = 15,
    };

    #define VGA_WIDTH 80
    #define VGA_HEIGHT 25
    #define VGA_BUFFER (uint16_t*) 0xB8000

#endif //VGA_H
