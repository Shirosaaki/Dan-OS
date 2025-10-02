
//
// Created by Shirosaaki on 02/10/2025.
//
#include "commands.h"
#include "fat32.h"
#include "ata.h"

extern void tty_putchar_internal(char c);
extern size_t tty_row;
extern size_t tty_column;
extern uint8_t tty_color;
extern char cmd_buffer[];
extern int cmd_buffer_pos;

void tty_process_command(void) {
    cmd_buffer[cmd_buffer_pos] = '\0'; // Null terminate
    
    // Process the command
    if (cmd_buffer_pos > 0) {
        if (strncmp(cmd_buffer, "help", 4) == 0) {
            tty_putstr("Available commands:\n");
            tty_putstr("  help     - Show this help message\n");
            tty_putstr("  clear    - Clear the screen\n");
            tty_putstr("  echo     - Echo back the input\n");
            tty_putstr("  about    - Show OS information\n");
            tty_putstr("  ls       - List files in current directory\n");
            tty_putstr("  rd       - Display file contents\n");
            tty_putstr("  ct       - Create file (ct filename content)\n");
            tty_putstr("  disk     - Show disk information\n");
        } else if (strncmp(cmd_buffer, "cls", 3) == 0) {
            tty_clear();
            tty_row = 0;
            tty_column = 0;
        } else if (strncmp(cmd_buffer, "about", 5) == 0) {
            tty_putstr("DanOS - A simple 64-bit operating system\n");
            tty_putstr("Version: 0.2\n");
            tty_putstr("Author: dan13615\n");
            tty_putstr("Features: FAT32 filesystem support\n");
        } else if (strncmp(cmd_buffer, "echo ", 5) == 0) {
            for (int i = 5; i < strlength(cmd_buffer); i++) {
                tty_putchar_internal(cmd_buffer[i]);
            }
            tty_putchar_internal('\n');
        } else if (strncmp(cmd_buffer, "ls", 2) == 0) {
            // List directory
            tty_putstr("Directory listing:\n");
            if (fat32_list_directory(0) != 0) {
                tty_putstr("Error reading directory\n");
            }
        } else if (strncmp(cmd_buffer, "rd ", 3) == 0) {
            // Display file contents
            char filename[32];
            int j = 0;
            for (int i = 3; i < strlength(cmd_buffer) && j < 31; i++, j++) {
                filename[j] = cmd_buffer[i];
            }
            filename[j] = '\0';
            
            fat32_file_t file;
            if (fat32_open_file(filename, &file) == 0) {
                uint8_t buffer[512];
                uint32_t bytes_to_read = file.file_size > 512 ? 512 : file.file_size;
                int bytes_read = fat32_read_file(&file, buffer, bytes_to_read);
                
                if (bytes_read > 0) {
                    for (int i = 0; i < bytes_read; i++) {
                        if (buffer[i] >= 32 && buffer[i] < 127) {
                            tty_putchar_internal(buffer[i]);
                        } else if (buffer[i] == '\n' || buffer[i] == '\r') {
                            tty_putchar_internal('\n');
                        }
                    }
                    tty_putchar_internal('\n');
                } else {
                    tty_putstr("Error reading file\n");
                }
            } else {
                tty_putstr("File not found: ");
                tty_putstr(filename);
                tty_putchar_internal('\n');
            }
        } else if (strncmp(cmd_buffer, "disk", 4) == 0) {
            // Show disk info
            tty_putstr("Identifying disk...\n");
            ata_identify();
        } else if (strncmp(cmd_buffer, "ct ", 3) == 0) {
            // Create file: ct filename content
            // Parse filename and content
            char filename[32];
            uint8_t content[256];
            int i = 3;
            int j = 0;
            
            // Skip spaces
            while (i < strlength(cmd_buffer) && cmd_buffer[i] == ' ') i++;
            
            // Get filename
            while (i < strlength(cmd_buffer) && cmd_buffer[i] != ' ' && j < 31) {
                filename[j++] = cmd_buffer[i++];
            }
            filename[j] = '\0';
            
            // Skip spaces
            while (i < strlength(cmd_buffer) && cmd_buffer[i] == ' ') i++;
            
            // Get content
            j = 0;
            while (i < strlength(cmd_buffer) && j < 255) {
                content[j++] = cmd_buffer[i++];
            }
            content[j] = '\0';
            
            if (filename[0] == '\0') {
                tty_putstr("Usage: ct filename [content]\n");
                tty_putstr("Example: ct test.txt Hello World!\n");
                tty_putstr("Example: ct empty.txt (creates empty file)\n");
            } else {
                // Allow empty files (j can be 0)
                tty_putstr("Creating file: ");
                tty_putstr(filename);
                if (j == 0) {
                    tty_putstr(" (empty file)");
                }
                tty_putstr("\n");
                
                if (fat32_create_file(filename, content, j) == 0) {
                    tty_putstr("File created successfully!\n");
                    tty_putstr("Use 'ls' to see it, 'rd ");
                    tty_putstr(filename);
                    tty_putstr("' to read it\n");
                } else {
                    tty_putstr("Error creating file\n");
                }
            }
        } else {
            tty_putstr("Unknown command: ");
            tty_putstr(cmd_buffer);
            tty_putstr("\n");
            tty_putstr("Type 'help' for available commands.\n");
        }
    }
    
    // Reset command buffer
    cmd_buffer_pos = 0;
    tty_putstr("> ");
}
