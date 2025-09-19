//
// Created by dan13615 on 11/15/24.
//

#include "tty.h"

void kernel_main(void) {
    tty_init();
    tty_middle_screen("Welcome to the DanOS kernel!\n");
    //while (1);
}