
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
            tty_putstr("  ls dir   - List files in specified directory\n");
            tty_putstr("  rd       - Display file contents\n");
            tty_putstr("  ct       - Create file (ct filename content)\n");
            tty_putstr("  rm       - Delete file (rm filename or rm *)\n");
            tty_putstr("  wr       - Text editor (wr filename, Ctrl to save)\n");
            tty_putstr("  cp       - Copy file (cp source dest, use . for current dir)\n");
            tty_putstr("  mv       - Move files/dirs (mv source dest, use . for current)\n");
            tty_putstr("  rn       - Rename file (rn oldname newname)\n");
            tty_putstr("  md       - Create directory (md dirname)\n");
            tty_putstr("  cd       - Change directory (cd dirname or cd ..)\n");
            tty_putstr("  rmdir    - Remove directory and all contents (rmdir dirname)\n");
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
            // List directory - with optional directory name
            if (strlength(cmd_buffer) > 3 && cmd_buffer[2] == ' ') {
                // ls dirname - list specific directory
                char dirname[32];
                int j = 0;
                for (int i = 3; i < strlength(cmd_buffer) && j < 31; i++, j++) {
                    dirname[j] = cmd_buffer[i];
                }
                dirname[j] = '\0';
                if (fat32_list_directory_by_name(dirname) != 0) {
                    tty_putstr("Error reading directory\n");
                }
            } else {
                // ls - list current directory
                tty_putstr("Directory listing:\n");
                uint32_t current_cluster = fat32_get_current_directory();
                if (fat32_list_directory(current_cluster) != 0) {
                    tty_putstr("Error reading directory\n");
                }
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
                    tty_putstr("File is empty !\n");
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
        } else if (strncmp(cmd_buffer, "rm ", 3) == 0) {
            // Remove file: rm filename or rm *
            char filename[32];
            int j = 0;
            
            // Skip initial spaces after "rm"
            int i = 3;
            while (i < strlength(cmd_buffer) && cmd_buffer[i] == ' ') i++;
            
            // Get the argument
            while (i < strlength(cmd_buffer) && cmd_buffer[i] != ' ' && j < 31) {
                filename[j++] = cmd_buffer[i++];
            }
            filename[j] = '\0';
            
            if (filename[0] == '\0') {
                tty_putstr("Usage: rm filename  or  rm *\n");
                tty_putstr("Example: rm test.txt\n");
                tty_putstr("Example: rm * (deletes all files)\n");
            } else if (strncmp(filename, "*", 1) == 0) {
                // Delete all files
                tty_putstr("Deleting all files...\n");
                tty_putstr("Are you sure? This will delete ALL files!\n");
                tty_putstr("Proceeding in 1 second...\n");
                
                // Simple delay (not perfect but works for demo)
                for (volatile int delay = 0; delay < 1000000; delay++);
                
                if (fat32_delete_all_files() == 0) {
                    tty_putstr("All files deleted successfully!\n");
                } else {
                    tty_putstr("Error deleting files\n");
                }
            } else {
                // Delete single file
                tty_putstr("Deleting file: ");
                tty_putstr(filename);
                tty_putstr("\n");
                
                if (fat32_delete_file(filename) == 0) {
                    // Success message is printed by fat32_delete_file
                } else {
                    // Error message is printed by fat32_delete_file
                }
            }
        } else if (strncmp(cmd_buffer, "wr ", 3) == 0) {
            // Text editor: wr filename
            char filename[32];
            int j = 0;
            for (int i = 3; i < strlength(cmd_buffer) && j < 31; i++, j++) {
                filename[j] = cmd_buffer[i];
            }
            filename[j] = '\0';
            
            if (filename[0] == '\0') {
                tty_putstr("Usage: wr filename\n");
                tty_putstr("Example: wr document.txt\n");
                tty_putstr("Press Ctrl to save and exit editor\n");
            } else {
                // Start editor mode
                tty_start_editor_mode(filename);
                return; // Don't print prompt, we're in editor mode
            }
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
                
                if (fat32_update_file(filename, content, j) == 0) {
                    tty_putstr("File saved successfully!\n");
                    tty_putstr("Use 'ls' to see it, 'rd ");
                    tty_putstr(filename);
                    tty_putstr("' to read it\n");
                } else {
                    tty_putstr("Error saving file\n");
                }
            }
        } else if (strncmp(cmd_buffer, "md ", 3) == 0) {
            // Create directory: md dirname
            char dirname[32];
            int j = 0;
            for (int i = 3; i < strlength(cmd_buffer) && j < 31; i++, j++) {
                dirname[j] = cmd_buffer[i];
            }
            dirname[j] = '\0';
            
            if (dirname[0] == '\0') {
                tty_putstr("Usage: md dirname\n");
                tty_putstr("Example: md Documents\n");
            } else {
                fat32_create_directory(dirname);
            }
        } else if (strncmp(cmd_buffer, "cd ", 3) == 0) {
            // Change directory: cd dirname
            char dirname[32];
            int j = 0;
            for (int i = 3; i < strlength(cmd_buffer) && j < 31; i++, j++) {
                dirname[j] = cmd_buffer[i];
            }
            dirname[j] = '\0';
            
            if (dirname[0] == '\0') {
                tty_putstr("Usage: cd dirname\n");
                tty_putstr("Example: cd Documents\n");
                tty_putstr("Example: cd .. (parent directory)\n");
            } else {
                if (fat32_change_directory(dirname) == 0) {
                    tty_putstr("Changed to directory: ");
                    tty_putstr(dirname);
                    tty_putstr("\n");
                }
            }
        } else if (strncmp(cmd_buffer, "cd", 2) == 0 && strlength(cmd_buffer) == 2) {
            // cd without arguments - show current directory
            char path[64];
            fat32_get_current_path(path, 64);
            tty_putstr("Current directory: ");
            tty_putstr(path);
            tty_putstr("\n");
        } else if (strncmp(cmd_buffer, "rmdir ", 6) == 0) {
            // Remove directory: rmdir dirname
            char dirname[32];
            int j = 0;
            for (int i = 6; i < strlength(cmd_buffer) && j < 31; i++, j++) {
                dirname[j] = cmd_buffer[i];
            }
            dirname[j] = '\0';
            
            if (dirname[0] == '\0') {
                tty_putstr("Usage: rmdir dirname\n");
                tty_putstr("Example: rmdir Documents\n");
                tty_putstr("Warning: This will remove the directory and ALL its contents!\n");
            } else {
                tty_putstr("Warning: Removing directory and all contents: ");
                tty_putstr(dirname);
                tty_putstr("\n");
                fat32_remove_directory_recursive(dirname);
            }
        } else if (strncmp(cmd_buffer, "cp ", 3) == 0) {
            // Copy file: cp source dest
            char source[32], dest[32];
            int i = 3, j = 0;
            
            // Skip spaces
            while (i < strlength(cmd_buffer) && cmd_buffer[i] == ' ') i++;
            
            // Get source filename
            while (i < strlength(cmd_buffer) && cmd_buffer[i] != ' ' && j < 31) {
                source[j++] = cmd_buffer[i++];
            }
            source[j] = '\0';
            
            // Skip spaces
            while (i < strlength(cmd_buffer) && cmd_buffer[i] == ' ') i++;
            
            // Get destination filename
            j = 0;
            while (i < strlength(cmd_buffer) && cmd_buffer[i] != ' ' && j < 31) {
                dest[j++] = cmd_buffer[i++];
            }
            dest[j] = '\0';
            
            if (source[0] == '\0' || dest[0] == '\0') {
                tty_putstr("Usage: cp source destination\n");
                tty_putstr("Example: cp file1.txt file2.txt\n");
            } else {
                fat32_copy_file(source, dest);
            }
        } else if (strncmp(cmd_buffer, "mv ", 3) == 0) {
            // Move file to directory: mv file destdir
            char source[32], dest_dir[32];
            int i = 3, j = 0;
            
            // Skip spaces
            while (i < strlength(cmd_buffer) && cmd_buffer[i] == ' ') i++;
            
            // Get source filename
            while (i < strlength(cmd_buffer) && cmd_buffer[i] != ' ' && j < 31) {
                source[j++] = cmd_buffer[i++];
            }
            source[j] = '\0';
            
            // Skip spaces
            while (i < strlength(cmd_buffer) && cmd_buffer[i] == ' ') i++;
            
            // Get destination directory
            j = 0;
            while (i < strlength(cmd_buffer) && cmd_buffer[i] != ' ' && j < 31) {
                dest_dir[j++] = cmd_buffer[i++];
            }
            dest_dir[j] = '\0';
            
            if (source[0] == '\0' || dest_dir[0] == '\0') {
                tty_putstr("Usage: mv source dest\n");
                tty_putstr("Example: mv file.txt Documents\n");
                tty_putstr("Example: mv folder/ backup/\n");
                tty_putstr("Example: mv file.txt ..\n");
            } else {
                // Auto-detect if source is file or directory
                uint32_t source_cluster;
                char source_name[32];
                if (fat32_parse_path(source, &source_cluster, source_name) == 0) {
                    fat32_dir_entry_t entry;
                    if (fat32_find_file(source_name, source_cluster, &entry) == 0) {
                        if (entry.attributes & FAT_ATTR_DIRECTORY) {
                            // It's a directory - use directory move
                            fat32_move_directory(source, dest_dir);
                        } else {
                            // It's a file - use file move
                            fat32_move_file(source, dest_dir);
                        }
                    } else {
                        tty_putstr("Error: Source not found: ");
                        tty_putstr(source);
                        tty_putstr("\n");
                    }
                } else {
                    tty_putstr("Error: Invalid source path: ");
                    tty_putstr(source);
                    tty_putstr("\n");
                }
            }
        } else if (strncmp(cmd_buffer, "rn ", 3) == 0) {
            // Rename file: rn oldname newname
            char old_name[32], new_name[32];
            int i = 3, j = 0;
            
            // Skip spaces
            while (i < strlength(cmd_buffer) && cmd_buffer[i] == ' ') i++;
            
            // Get old filename
            while (i < strlength(cmd_buffer) && cmd_buffer[i] != ' ' && j < 31) {
                old_name[j++] = cmd_buffer[i++];
            }
            old_name[j] = '\0';
            
            // Skip spaces
            while (i < strlength(cmd_buffer) && cmd_buffer[i] == ' ') i++;
            
            // Get new filename
            j = 0;
            while (i < strlength(cmd_buffer) && cmd_buffer[i] != ' ' && j < 31) {
                new_name[j++] = cmd_buffer[i++];
            }
            new_name[j] = '\0';
            
            if (old_name[0] == '\0' || new_name[0] == '\0') {
                tty_putstr("Usage: rn oldname newname\n");
                tty_putstr("Example: rn oldfile.txt newfile.txt\n");
            } else {
                fat32_rename_file(old_name, new_name);
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
    
    // Show current directory in prompt
    char path[64];
    fat32_get_current_path(path, 64);
    tty_putstr("DanOS:");
    tty_putstr(path);
    tty_putstr("$ ");
}
