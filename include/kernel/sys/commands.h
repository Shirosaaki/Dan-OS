//
// Created by Shirosaaki on 02/10/2025.
//

#ifndef COMMANDS_H_
    #define COMMANDS_H_
    #include <kernel/sys/tty.h>
    #include <kernel/drivers/vga.h>
    #include <kernel/sys/string.h>
    #include <cpu/ports.h>
    #include <kernel/sys/power.h>


void tty_process_command(void);

#endif // COMMANDS_H_
