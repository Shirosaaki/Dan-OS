//
// Created by dan13615 on 11/15/24.
//

#include "tty.h"

void kernel_main(void) {
    tty_init();
    tty_putstr("Welcome to the DanOS kernel!\n");
    tty_putstr("> ");
}