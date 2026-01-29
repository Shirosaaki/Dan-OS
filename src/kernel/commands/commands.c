
//
// Created by Shirosaaki on 02/10/2025.
//
#include <kernel/sys/commands.h>
#include <kernel/fs/fat32.h>
#include <kernel/drivers/ata.h>
#include <kernel/drivers/rtc.h>
#include <kernel/sys/tty.h>
#include <kernel/net/net.h>
#include <kernel/drivers/e1000.h>
#include <kernel/net/dns.h>
#include <kernel/net/tcp.h>
#include <kernel/net/http.h>

extern void tty_putchar_internal(char c);
extern size_t tty_row;
extern size_t tty_column;
extern uint8_t tty_color;
extern char cmd_buffer[];
extern int cmd_buffer_pos;
extern int cmd_cursor_pos;
extern void cmd_vm(const char* filename);

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
            tty_putstr("  ls       - List files (hides files starting with .)\n");
            tty_putstr("  ls -a    - List all files (including hidden)\n");
            tty_putstr("  ls dir   - List files in specified directory\n");
            tty_putstr("  rd       - Display file contents\n");
            tty_putstr("  ct       - Create file (ct filename content)\n");
            tty_putstr("  rm       - Delete file (rm filename or rm *)\n");
            tty_putstr("  wr       - Text editor (wr filename, Ctrl+S save, Ctrl+E exit)\n");
            tty_putstr("  cp       - Copy file (cp source dest, use . for current dir)\n");
            tty_putstr("  mv       - Move files/dirs (mv source dest, use . for current)\n");
            tty_putstr("  rn       - Rename file (rn oldname newname)\n");
            tty_putstr("  md       - Create directory (md dirname)\n");
            tty_putstr("  cd       - Change directory (cd path, supports ../.. and folder/subfolder)\n");
            tty_putstr("  rmdir    - Remove directory and all contents (rmdir dirname)\n");
            tty_putstr("  time     - Display current time and date\n");
            tty_putstr("  timezone - Set timezone (timezone +/-H:M NAME or timezone list)\n");
            tty_putstr("  disk     - Show disk information\n");
            tty_putstr("  history  - Show command history\n");
            tty_putstr("  ping     - Ping an IP address (ping x.x.x.x)\n");
            tty_putstr("  ifconfig - Show network interface information\n");
            tty_putstr("  netpoll  - Poll network for packets (debug)\n");
            tty_putstr("  dns      - Resolve hostname to IP (dns hostname)\n");
            tty_putstr("  fetch    - Fetch a webpage (fetch http://url)\n");
            tty_putstr("  reboot   - Reboot the system\n");
            tty_putstr("  shutdown  - Shut down the system\n");
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
        } else if (strncmp(cmd_buffer, "ls -a", 5) == 0) {
            // ls -a: list all files including hidden
            if (strlength(cmd_buffer) > 6 && cmd_buffer[5] == ' ') {
                // ls -a dirname
                char dirname[32];
                int j = 0;
                for (int i = 6; i < strlength(cmd_buffer) && j < 31; i++, j++) {
                    dirname[j] = cmd_buffer[i];
                }
                dirname[j] = '\0';
                // TODO: implement list_directory_by_name_ex with show_all flag
                if (fat32_list_directory_by_name(dirname) != 0) {
                    tty_putstr("Error reading directory\n");
                }
            } else {
                // ls -a: list current directory with hidden files
                uint32_t current_cluster = fat32_get_current_directory();
                if (fat32_list_directory_ex(current_cluster, 1) != 0) {
                    tty_putstr("Error reading directory\n");
                }
            }
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
                // ls - list current directory (hide hidden files)
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
                tty_putstr("Press Ctrl+S to save, Ctrl+E to exit editor\n");
            } else {
                // Add command to history before entering editor mode
                tty_history_commit();
                
                // Reset command buffer
                cmd_buffer_pos = 0;
                cmd_cursor_pos = 0;
                
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
            // Change directory: cd path (supports complex paths like folder/subfolder, ../.., etc.)
            char path[256];
            int j = 0;
            for (int i = 3; i < strlength(cmd_buffer) && j < 255; i++, j++) {
                path[j] = cmd_buffer[i];
            }
            path[j] = '\0';
            
            if (path[0] == '\0') {
                tty_putstr("Usage: cd path\n");
                tty_putstr("Examples:\n");
                tty_putstr("  cd Documents        - Go to Documents directory\n");
                tty_putstr("  cd ..              - Go to parent directory\n");
                tty_putstr("  cd ../../..        - Go up three levels\n");
                tty_putstr("  cd folder/subfolder - Navigate through path\n");
                tty_putstr("  cd /               - Go to root directory\n");
            } else {
                // Check if it's a simple single directory (no slashes)
                int has_slash = 0;
                for (int k = 0; path[k] != '\0'; k++) {
                    if (path[k] == '/') {
                        has_slash = 1;
                        break;
                    }
                }
                
                if (has_slash || (path[0] == '.' && path[1] == '.' && (path[2] == '/' || path[2] == '\0'))) {
                    // Complex path - use path navigation
                    if (fat32_change_directory_path(path) != 0) {
                        tty_putstr("Error changing directory\n");
                    }
                } else {
                    // Simple single directory - use basic function
                    if (fat32_change_directory(path) != 0)
                        tty_putstr("Error changing directory\n");
                    
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
        } else if (strncmp(cmd_buffer, "time", 4) == 0 && (strlength(cmd_buffer) == 4 || cmd_buffer[4] == ' ')) {
            // Display current local time and date
            rtc_time_t local_time;
            rtc_read_local_time(&local_time);
            
            timezone_t current_tz;
            timezone_get(&current_tz);
            
            char time_str[16];
            char date_str[16];
            rtc_format_time_string(&local_time, time_str);
            rtc_format_date_string(&local_time, date_str);
            
            tty_putstr("Current time: ");
            tty_putstr(time_str);
            tty_putstr(" ");
            tty_putstr(current_tz.name);
            tty_putstr("\n");
            tty_putstr("Current date: ");
            tty_putstr(date_str);
            tty_putstr("\n");
        } else if (strncmp(cmd_buffer, "date ", 5) == 0) {
            // Set time and date: date HH:MM:SS DD/MM/YYYY
            rtc_time_t new_time;
            
            // Parse the input: "date HH:MM:SS DD/MM/YYYY"
            if (strlength(cmd_buffer) >= 24) {
                // Extract HH:MM:SS
                new_time.hours = (cmd_buffer[5] - '0') * 10 + (cmd_buffer[6] - '0');
                new_time.minutes = (cmd_buffer[8] - '0') * 10 + (cmd_buffer[9] - '0');
                new_time.seconds = (cmd_buffer[11] - '0') * 10 + (cmd_buffer[12] - '0');
                
                // Extract DD/MM/YYYY
                new_time.day = (cmd_buffer[14] - '0') * 10 + (cmd_buffer[15] - '0');
                new_time.month = (cmd_buffer[17] - '0') * 10 + (cmd_buffer[18] - '0');
                new_time.year = (cmd_buffer[20] - '0') * 1000 + (cmd_buffer[21] - '0') * 100 +
                               (cmd_buffer[22] - '0') * 10 + (cmd_buffer[23] - '0');
                
                // Validate ranges
                if (new_time.hours < 24 && new_time.minutes < 60 && new_time.seconds < 60 &&
                    new_time.day >= 1 && new_time.day <= 31 && 
                    new_time.month >= 1 && new_time.month <= 12 &&
                    new_time.year >= 1970 && new_time.year <= 2099) {
                    
                    rtc_set_time(&new_time);
                    tty_putstr("Time and date updated successfully!\n");
                    
                    // Show the new time
                    char time_str[16];
                    char date_str[16];
                    rtc_format_time_string(&new_time, time_str);
                    rtc_format_date_string(&new_time, date_str);
                    
                    tty_putstr("New time: ");
                    tty_putstr(time_str);
                    tty_putstr(" ");
                    tty_putstr(date_str);
                    tty_putstr("\n");
                } else {
                    tty_putstr("Error: Invalid time or date values\n");
                    tty_putstr("Valid ranges: HH(00-23) MM(00-59) SS(00-59) DD(01-31) MM(01-12) YYYY(1970-2099)\n");
                }
            } else {
                tty_putstr("Usage: date HH:MM:SS DD/MM/YYYY\n");
                tty_putstr("Example: date 14:30:00 25/12/2025\n");
            }
        } else if (strncmp(cmd_buffer, "timezone ", 9) == 0) {
            // Set timezone: timezone +/-H:M NAME or timezone list
            tty_putstr("DEBUG: Processing timezone command\n");
            if (strncmp(cmd_buffer + 9, "list", 4) == 0) {
                // Show common timezones
                tty_putstr("Common timezones:\n");
                tty_putstr("  UTC    +0:00   Coordinated Universal Time\n");
                tty_putstr("  GMT    +0:00   Greenwich Mean Time\n");
                tty_putstr("  EST    -5:00   Eastern Standard Time\n");
                tty_putstr("  CST    -6:00   Central Standard Time\n");
                tty_putstr("  MST    -7:00   Mountain Standard Time\n");
                tty_putstr("  PST    -8:00   Pacific Standard Time\n");
                tty_putstr("  CET    +1:00   Central European Time\n");
                tty_putstr("  JST    +9:00   Japan Standard Time\n");
                tty_putstr("  AEST  +10:00   Australian Eastern Standard Time\n");
                tty_putstr("\nUsage: timezone +/-H:M NAME\n");
                tty_putstr("Example: timezone -5:00 EST\n");
            } else if (strlength(cmd_buffer) >= 15) {
                // Parse timezone: +/-H:M NAME
                char sign = cmd_buffer[9];
                if (sign == '+' || sign == '-') {
                    int8_t hours = (cmd_buffer[10] - '0') * 10 + (cmd_buffer[11] - '0');
                    int8_t minutes = (cmd_buffer[13] - '0') * 10 + (cmd_buffer[14] - '0');
                    
                    if (sign == '-') {
                        hours = -hours;
                        minutes = -minutes;
                    }
                    
                    // Validate ranges
                    if (hours >= -12 && hours <= 14 && minutes >= -59 && minutes <= 59) {
                        // Extract name (skip space after minutes)
                        char tz_name[8] = {0};
                        int name_start = 16; // Position after "H:M "
                        int name_len = 0;
                        
                        while (name_start < strlength(cmd_buffer) && name_len < 7) {
                            tz_name[name_len++] = cmd_buffer[name_start++];
                        }
                        tz_name[name_len] = '\0';
                        
                        if (name_len > 0) {
                            timezone_set(hours, minutes, tz_name);
                            
                            tty_putstr("Timezone set to: ");
                            tty_putstr(tz_name);
                            tty_putstr(" (");
                            if (hours >= 0) tty_putstr("+");
                            
                            // Simple number display for hours
                            if (hours < 0) {
                                tty_putstr("-");
                                hours = -hours;
                            }
                            if (hours >= 10) tty_putchar('0' + hours / 10);
                            tty_putchar('0' + hours % 10);
                            tty_putstr(":");
                            
                            // Minutes
                            if (minutes < 0) minutes = -minutes;
                            tty_putchar('0' + minutes / 10);
                            tty_putchar('0' + minutes % 10);
                            tty_putstr(")\n");
                        } else {
                            tty_putstr("Error: Timezone name required\n");
                        }
                    } else {
                        tty_putstr("Error: Invalid timezone offset\n");
                        tty_putstr("Valid range: -12:00 to +14:00\n");
                    }
                } else {
                    tty_putstr("Error: Invalid format. Use +/- prefix\n");
                }
            } else {
                tty_putstr("Usage: timezone +/-H:M NAME or timezone list\n");
                tty_putstr("Example: timezone -5:00 EST\n");
                tty_putstr("Example: timezone +1:00 CET\n");
                tty_putstr("Type 'timezone list' to see common timezones\n");
            }
        } else if (strncmp(cmd_buffer, "timezone", 8) == 0 && strlength(cmd_buffer) == 8) {
            // Show current timezone
            tty_putstr("DEBUG: Processing timezone status command\n");
            timezone_t current_tz;
            timezone_get(&current_tz);
            
            tty_putstr("Current timezone: ");
            tty_putstr(current_tz.name);
            tty_putstr(" (");
            
            if (current_tz.offset_hours >= 0) tty_putstr("+");
            else {
                tty_putstr("-");
                current_tz.offset_hours = -current_tz.offset_hours;
            }
            
            if (current_tz.offset_hours >= 10) tty_putchar('0' + current_tz.offset_hours / 10);
            tty_putchar('0' + current_tz.offset_hours % 10);
            tty_putstr(":");
            
            int8_t minutes = current_tz.offset_minutes;
            if (minutes < 0) minutes = -minutes;
            tty_putchar('0' + minutes / 10);
            tty_putchar('0' + minutes % 10);
            tty_putstr(")\n");
        } else if (strncmp(cmd_buffer, "history", 7) == 0 && (strlength(cmd_buffer) == 7 || cmd_buffer[7] == ' ')) {
            tty_print_history();
        } else if (strncmp(cmd_buffer, "reboot", 6) == 0 && strlength(cmd_buffer) == 6) {
            tty_putstr("Rebooting system...\n");
            kernel_reboot();
            // Should not return
            for(;;) __asm__ volatile("hlt");
        } else if (strncmp(cmd_buffer, "shutdown", 8) == 0 && strlength(cmd_buffer) == 8) {
            tty_putstr("Shutting down system...\n");
            kernel_shutdown();
            for(;;) __asm__ volatile("hlt");
        } else if (strncmp(cmd_buffer, "ping ", 5) == 0) {
            // ping command - send ICMP echo request
            // Parse IP address: ping x.x.x.x
            char* ip_str = cmd_buffer + 5;
            uint32_t ip = 0;
            int octet = 0;
            int num = 0;
            for (int i = 0; ip_str[i] != '\0' && ip_str[i] != ' '; i++) {
                if (ip_str[i] == '.') {
                    if (octet >= 3) break;
                    ip = (ip << 8) | (num & 0xFF);
                    num = 0;
                    octet++;
                } else if (ip_str[i] >= '0' && ip_str[i] <= '9') {
                    num = num * 10 + (ip_str[i] - '0');
                }
            }
            ip = (ip << 8) | (num & 0xFF);  // Add last octet
            
            if (octet != 3) {
                tty_putstr("Usage: ping x.x.x.x\n");
            } else {
                net_interface_t* iface = net_get_interface();
                if (!iface) {
                    tty_putstr("No network interface available\n");
                } else {
                    char ip_buf[16];
                    ip_to_string(ip, ip_buf);
                    tty_putstr("Pinging ");
                    tty_putstr(ip_buf);
                    tty_putstr("...\n");
                    
                    // Reset ping state
                    ping_reply_received = 0;
                    
                    // Try to send ping (may fail if ARP not resolved)
                    int sent = 0;
                    if (icmp_send_echo(iface, ip, 1, 1, "DanOS", 5) == 0) {
                        sent = 1;
                    } else {
                        tty_putstr("Resolving MAC address...\n");
                        // Wait for ARP reply (up to ~2 seconds)
                        for (volatile int i = 0; i < 20000000 && !sent; i++) {
                            e1000_poll();
                            if (i % 5000000 == 0 && i > 0) {
                                // Retry sending ping
                                if (icmp_send_echo(iface, ip, 1, 1, "DanOS", 5) == 0) {
                                    sent = 1;
                                }
                            }
                        }
                    }
                    
                    if (sent) {
                        tty_putstr("Waiting for reply (30s timeout)...\n");
                        
                        // Wait up to 30 seconds for reply
                        // ~100 million iterations ≈ 30 seconds on typical QEMU speed
                        int timeout = 0;
                        for (volatile uint32_t i = 0; i < 100000000; i++) {
                            e1000_poll();
                            
                            if (ping_reply_received) {
                                // Got a reply!
                                char reply_ip[16];
                                ip_to_string(ping_reply_from, reply_ip);
                                tty_putstr("Reply from ");
                                tty_putstr(reply_ip);
                                tty_putstr(": seq=");
                                tty_putdec(ping_reply_seq);
                                tty_putstr("\n");
                                timeout = 0;
                                break;
                            }
                            
                            // Check for Ctrl+C (stop signal)
                            if (tty_check_stop()) {
                                tty_putstr("\nPing interrupted.\n");
                                timeout = 0;
                                break;
                            }
                        }
                        
                        if (!ping_reply_received && timeout == 0) {
                            // Loop finished without reply and wasn't interrupted
                            if (!tty_check_stop()) {
                                tty_putstr("Request timed out.\n");
                            }
                        }
                    } else {
                        tty_putstr("Failed to send ping (could not resolve MAC)\n");
                    }
                }
            }
        } else if (strncmp(cmd_buffer, "ifconfig", 8) == 0 && (strlength(cmd_buffer) == 8 || cmd_buffer[8] == ' ')) {
            // ifconfig - show/configure network interface
            net_interface_t* iface = net_get_interface();
            if (!iface) {
                tty_putstr("No network interface available\n");
            } else {
                char buf[32];
                tty_putstr("Interface: ");
                tty_putstr(iface->name);
                tty_putstr("\n");
                
                tty_putstr("  MAC: ");
                mac_to_string(&iface->mac, buf);
                tty_putstr(buf);
                tty_putstr("\n");
                
                tty_putstr("  IP:  ");
                ip_to_string(iface->ip, buf);
                tty_putstr(buf);
                tty_putstr("\n");
                
                tty_putstr("  Mask: ");
                ip_to_string(iface->netmask, buf);
                tty_putstr(buf);
                tty_putstr("\n");
                
                tty_putstr("  GW:  ");
                ip_to_string(iface->gateway, buf);
                tty_putstr(buf);
                tty_putstr("\n");
                
                tty_putstr("  TX: ");
                tty_putdec((uint32_t)iface->tx_packets);
                tty_putstr(" packets, ");
                tty_putdec((uint32_t)iface->tx_bytes);
                tty_putstr(" bytes\n");
                
                tty_putstr("  RX: ");
                tty_putdec((uint32_t)iface->rx_packets);
                tty_putstr(" packets, ");
                tty_putdec((uint32_t)iface->rx_bytes);
                tty_putstr(" bytes\n");
            }
        } else if (strncmp(cmd_buffer, "dns ", 4) == 0) {
            // DNS lookup command
            char* hostname = cmd_buffer + 4;
            
            // Skip leading spaces
            while (*hostname == ' ') hostname++;
            
            if (*hostname == '\0') {
                tty_putstr("Usage: dns hostname\n");
                tty_putstr("Example: dns www.google.com\n");
            } else {
                tty_putstr("Resolving ");
                tty_putstr(hostname);
                tty_putstr("...\n");
                
                uint32_t ip;
                if (dns_resolve(hostname, &ip) == 0) {
                    char ip_str[16];
                    ip_to_string(ip, ip_str);
                    tty_putstr("Resolved to: ");
                    tty_putstr(ip_str);
                    tty_putstr("\n");
                } else {
                    tty_putstr("DNS resolution failed\n");
                }
            }
        } else if (strncmp(cmd_buffer, "fetch ", 6) == 0) {
            // Fetch a webpage via HTTP
            char* url = cmd_buffer + 6;
            
            // Skip leading spaces
            while (*url == ' ') url++;
            
            if (*url == '\0') {
                tty_putstr("Usage: fetch http://hostname/path\n");
                tty_putstr("Example: fetch http://example.com/\n");
                tty_putstr("Note: Only HTTP is supported (not HTTPS)\n");
            } else {
                tty_putstr("Fetching: ");
                tty_putstr(url);
                tty_putstr("\n");
                
                // Allocate response body buffer
                static char body_buffer[8192];
                http_response_t response;
                http_response_init(&response, body_buffer, sizeof(body_buffer));
                
                // http_fetch handles URL parsing and HTTPS warning
                if (http_fetch(url, &response) == 0) {
                    tty_putstr("\n--- HTTP Response ---\n");
                    tty_putstr("Status: ");
                    tty_putdec(response.status_code);
                    tty_putstr(" ");
                    tty_putstr(response.status_text);
                    tty_putstr("\n");
                    
                    if (response.content_type[0]) {
                        tty_putstr("Content-Type: ");
                        tty_putstr(response.content_type);
                        tty_putstr("\n");
                    }
                    
                    tty_putstr("Content-Length: ");
                    tty_putdec((uint32_t)response.body_len);
                    tty_putstr(" bytes\n");
                    
                    tty_putstr("\n--- Body (first 2KB) ---\n");
                    
                    // Print body (truncate if too long)
                    size_t display_len = response.body_len;
                    if (display_len > 2048) display_len = 2048;
                    
                    for (size_t i = 0; i < display_len; i++) {
                        char c = response.body[i];
                        if (c >= 32 && c < 127) {
                            tty_putchar_internal(c);
                        } else if (c == '\n' || c == '\r') {
                            tty_putchar_internal('\n');
                        } else if (c == '\t') {
                            tty_putstr("    ");
                        }
                    }
                    
                    if (response.body_len > 2048) {
                        tty_putstr("\n\n[...truncated, ");
                        tty_putdec((uint32_t)(response.body_len - 2048));
                        tty_putstr(" more bytes...]\n");
                    }
                    tty_putstr("\n");
                } else {
                    tty_putstr("Failed to fetch URL\n");
                }
            }
        } else if (strncmp(cmd_buffer, "netpoll", 7) == 0) {
            // Manual network poll - useful for debugging
            tty_putstr("Polling network...\n");
            e1000_poll();
            tty_putstr("Done.\n");
        } else if (strncmp(cmd_buffer, "vm ", 3) == 0) {
            char filename[32];
            int j = 0;
            int i = 3;
            
            // Skip spaces
            while (i < strlength(cmd_buffer) && cmd_buffer[i] == ' ') i++;
            
            // Get filename
            while (i < strlength(cmd_buffer) && cmd_buffer[i] != ' ' && j < 31) {
                filename[j++] = cmd_buffer[i++];
            }
            filename[j] = '\0';
            
            if (filename[0] == '\0') {
                tty_putstr("Usage: vm <payload.bin>\n");
            } else {
                cmd_vm(filename);
            }
        } else if (strncmp(cmd_buffer, "vmnow ", 6) == 0) {
            // Immediate run for debugging (no scheduler)
            char filename[32];
            int j = 0;
            int i = 6;
            while (i < strlength(cmd_buffer) && cmd_buffer[i] == ' ') i++;
            while (i < strlength(cmd_buffer) && cmd_buffer[i] != ' ' && j < 31) {
                filename[j++] = cmd_buffer[i++];
            }
            filename[j] = '\0';
            if (filename[0] == '\0') {
                tty_putstr("Usage: vmnow <payload.bin>\n");
            } else {
                // Call synchronous run path
                // Reuse vm_task logic but run directly
                extern void vm_task_direct_run(const char *filename);
                vm_task_direct_run(filename);
            }
        } else if (strncmp(cmd_buffer, "vmprobe ", 8) == 0) {
            char filename[32];
            int j = 0;
            int i = 8;
            while (i < strlength(cmd_buffer) && cmd_buffer[i] == ' ') i++;
            while (i < strlength(cmd_buffer) && cmd_buffer[i] != ' ' && j < 31) {
                filename[j++] = cmd_buffer[i++];
            }
            filename[j] = '\0';
            if (filename[0] == '\0') {
                tty_putstr("Usage: vmprobe <payload.bin>\n");
            } else {
                extern int vm_probe(const char *filename);
                int rc = vm_probe(filename);
                if (rc == 0) tty_putstr("Probe OK\n"); else tty_putstr("Probe FAILED\n");
            }
        } else {
            tty_putstr("Unknown command: ");
            tty_putstr(cmd_buffer);
            tty_putstr("\n");
            tty_putstr("Type 'help' for available commands.\n");
        }
    }
    
    // Add command to history before clearing buffer
    tty_history_commit();
    
    // Reset command buffer
    cmd_buffer_pos = 0;
    cmd_cursor_pos = 0;
    
    // Show current directory in prompt
    char path[64];
    fat32_get_current_path(path, 64);
    tty_putstr("DanOS:");
    tty_putstr(path);
    tty_putstr("$ ");
    
    // Update prompt position for history navigation
    tty_set_prompt_position();
}
