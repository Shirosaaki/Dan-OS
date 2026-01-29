// Author: Shirosaaki (migrated)
// Date: 2026-01-29
// Brief: FAT32 filesystem implementation (migrated to src/kernel/fs)

#include "fat32.h"
#include "ata.h"
#include "tty.h"
#include "string.h"
#include "rtc.h"
#include "kmalloc.h"

static fat32_boot_sector_t boot_sector;
static uint32_t fat_start_lba;
static uint32_t data_start_lba;
static uint32_t root_dir_cluster;
static uint32_t current_directory_cluster;
static int fat32_initialized = 0;
static char current_full_path[256] = "/";

// Buffer for reading sectors
static uint8_t sector_buffer[512];

// Initialize FAT32 filesystem
int fat32_init(void) {
    if (fat32_read_boot_sector() != 0) {
        tty_putstr("Error: Failed to read FAT32 boot sector\n");
        return -1;
    }

    // Calculate important values
    fat_start_lba = boot_sector.reserved_sectors;
    uint32_t fat_size = boot_sector.fat_size_32;
    uint32_t num_fats = boot_sector.num_fats;
    data_start_lba = fat_start_lba + (num_fats * fat_size);
    root_dir_cluster = boot_sector.root_cluster;
    current_directory_cluster = root_dir_cluster;
    fat32_initialized = 1;
    return 0;
}

// Read and parse boot sector
int fat32_read_boot_sector(void) {
    // Use a properly aligned buffer for ATA read
    uint16_t aligned_buffer[256];  // 512 bytes, properly aligned

    // Read boot sector (LBA 0)
    if (ata_read_sectors(0, 1, aligned_buffer) != 0) {
        return -1;
    }

    // Copy to our boot sector structure
    uint8_t* src = (uint8_t*)aligned_buffer;
    uint8_t* dst = (uint8_t*)&boot_sector;
    for (int i = 0; i < sizeof(fat32_boot_sector_t); i++) {
        dst[i] = src[i];
    }

    // More flexible signature check - accept common values
    if (boot_sector.boot_signature != 0x29 && boot_sector.boot_signature != 0x28) {
        tty_putstr("Warning: Unusual boot signature, proceeding anyway\n");
    }

    // Verify bytes per sector
    if (boot_sector.bytes_per_sector != 512) {
        tty_putstr("Error: Unsupported sector size: ");
        uint16_t size = boot_sector.bytes_per_sector;
        if (size == 0) tty_putstr("0");
        else if (size == 512) tty_putstr("512");
        else if (size == 1024) tty_putstr("1024");
        else tty_putstr("unknown");
        tty_putstr("\n");
        return -1;
    }

    if (boot_sector.fat_size_32 == 0) {
        tty_putstr("Error: No FAT32 size specified\n");
        return -1;
    }

    return 0;
}

// Get the LBA of a cluster
static uint32_t fat32_cluster_to_lba(uint32_t cluster) {
    return data_start_lba + ((cluster - 2) * boot_sector.sectors_per_cluster);
}

// Get next cluster in chain from FAT
uint32_t fat32_get_next_cluster(uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_start_lba + (fat_offset / 512);
    uint32_t entry_offset = fat_offset % 512;

    // Read FAT sector
    if (ata_read_sectors(fat_sector, 1, (uint16_t*)sector_buffer) != 0) {
        return FAT32_EOC;
    }

    uint32_t* fat_entry = (uint32_t*)&sector_buffer[entry_offset];
    return (*fat_entry) & 0x0FFFFFFF;
}

// Parse filename from standard format to FAT format
void fat32_parse_filename(const char* input, char* output) {
    int i, j;

    // Initialize output with spaces
    for (i = 0; i < 11; i++) {
        output[i] = ' ';
    }

    // Handle special directory entries "." and ".."
    if (input[0] == '.' && input[1] == '\0') {
        // Current directory "."
        output[0] = '.';
        return;
    }

    if (input[0] == '.' && input[1] == '.' && input[2] == '\0') {
        // Parent directory ".."
        output[0] = '.';
        output[1] = '.';
        return;
    }

    // Handle hidden files (starting with '.' but not "." or "..")
    j = 0;
    if (input[0] == '.') {
        output[0] = '.';
        j = 1;
        i = 1;
        // Parse rest of name part
        for (; i < 8 && input[j] != '\0' && input[j] != '.'; i++, j++) {
            if (input[j] >= 'a' && input[j] <= 'z') {
                output[i] = input[j] - 32; // Convert to uppercase
            } else {
                output[i] = input[j];
            }
        }
    } else {
        // Parse regular filename - name part
        for (i = 0, j = 0; i < 8 && input[j] != '\0' && input[j] != '.'; i++, j++) {
            if (input[j] >= 'a' && input[j] <= 'z') {
                output[i] = input[j] - 32; // Convert to uppercase
            } else {
                output[i] = input[j];
            }
        }
    }

    // Skip to extension
    while (input[j] != '\0' && input[j] != '.') {
        j++;
    }

    if (input[j] == '.') {
        j++;
        // Parse extension
        for (i = 8; i < 11 && input[j] != '\0'; i++, j++) {
            if (input[j] >= 'a' && input[j] <= 'z') {
                output[i] = input[j] - 32; // Convert to uppercase
            } else {
                output[i] = input[j];
            }
        }
    }
}

// Parse filename and return case flags for the reserved byte
uint8_t fat32_parse_filename_with_case(const char* input, char* output) {
    int i, j;
    uint8_t case_flags = 0;
    int name_has_lower = 0, name_has_upper = 0;
    int ext_has_lower = 0, ext_has_upper = 0;

    // Initialize output with spaces
    for (i = 0; i < 11; i++) {
        output[i] = ' ';
    }

    // Handle special directory entries "." and ".."
    if (input[0] == '.' && input[1] == '\0') {
        output[0] = '.';
        return 0;
    }

    if (input[0] == '.' && input[1] == '.' && input[2] == '\0') {
        output[0] = '.';
        output[1] = '.';
        return 0;
    }

    // Handle hidden files (starting with '.' but not "." or "..")
    j = 0;
    if (input[0] == '.') {
        output[0] = '.';
        j = 1;
        i = 1;
        // Parse rest of name part and track case
        for (; i < 8 && input[j] != '\0' && input[j] != '.'; i++, j++) {
            if (input[j] >= 'a' && input[j] <= 'z') {
                output[i] = input[j] - 32; // Convert to uppercase
                name_has_lower = 1;
            } else if (input[j] >= 'A' && input[j] <= 'Z') {
                output[i] = input[j];
                name_has_upper = 1;
            } else {
                output[i] = input[j];
            }
        }
    } else {
        // Parse name part and track case (normal case)
        for (i = 0, j = 0; i < 8 && input[j] != '\0' && input[j] != '.'; i++, j++) {
            if (input[j] >= 'a' && input[j] <= 'z') {
                output[i] = input[j] - 32; // Convert to uppercase
                name_has_lower = 1;
            } else if (input[j] >= 'A' && input[j] <= 'Z') {
                output[i] = input[j];
                name_has_upper = 1;
            } else {
                output[i] = input[j];
            }
        }
    }

    // Skip to extension
    while (input[j] != '\0' && input[j] != '.') {
        j++;
    }

    if (input[j] == '.') {
        j++;
        // Parse extension and track case
        for (i = 8; i < 11 && input[j] != '\0'; i++, j++) {
            if (input[j] >= 'a' && input[j] <= 'z') {
                output[i] = input[j] - 32; // Convert to uppercase
                ext_has_lower = 1;
            } else if (input[j] >= 'A' && input[j] <= 'Z') {
                output[i] = input[j];
                ext_has_upper = 1;
            } else {
                output[i] = input[j];
            }
        }
    }

    // Set case flags (only if all letters are same case)
    if (name_has_lower && !name_has_upper) {
        case_flags |= 0x08;
    }
    if (ext_has_lower && !ext_has_upper) {
        case_flags |= 0x10;
    }

    return case_flags;
}

// Compare FAT filenames (case-insensitive - just compares the 11-byte names)
int fat32_compare_names(const char* name1, const char* name2) {
    for (int i = 0; i < 11; i++) {
        if (name1[i] != name2[i]) {
            return 0;
        }
    }
    return 1;
}

// Compare FAT filenames with case sensitivity using case flags
int fat32_compare_names_case_sensitive(const char* fat_name, uint8_t expected_case_flags,
                                        const char* entry_name, uint8_t entry_case_flags) {
    for (int i = 0; i < 11; i++) {
        if (fat_name[i] != entry_name[i]) {
            return 0;
        }
    }
    return (expected_case_flags & 0x18) == (entry_case_flags & 0x18);
}

// Print file information (kept as original implementation)
void fat32_print_file_info(fat32_dir_entry_t* entry, int show_hidden) {
    int is_hidden = (entry->name[0] == '.');
    if (!show_hidden && is_hidden) return;
    int lowercase_name = (entry->reserved & 0x08);
    int lowercase_ext = (entry->reserved & 0x10);
    for (int i = 0; i < 8 && entry->name[i] != ' '; i++) {
        char c = entry->name[i];
        if (lowercase_name && c >= 'A' && c <= 'Z') c = c + 32;
        tty_putchar_internal(c);
    }
    if (entry->name[8] != ' ') {
        tty_putchar_internal('.');
        for (int i = 8; i < 11 && entry->name[i] != ' '; i++) {
            char c = entry->name[i];
            if (lowercase_ext && c >= 'A' && c <= 'Z') c = c + 32;
            tty_putchar_internal(c);
        }
    }
    if (entry->attributes & FAT_ATTR_DIRECTORY) {
        tty_putstr("  <DIR>");
    } else {
        tty_putstr("  ");
        uint32_t size = entry->file_size;
        if (size < 1024) { tty_putdec(size); tty_putstr(" B"); }
        else if (size < 1024 * 1024) { tty_putdec(size / 1024); tty_putstr(" KB"); }
        else { tty_putdec(size / (1024 * 1024)); tty_putstr(" MB"); }
    }
    tty_putchar_internal('\n');
}

// Read from a file (heap allocation to avoid stack overflow)
int fat32_read_file(fat32_file_t* file, uint8_t* buffer, uint32_t size) {
    uint32_t bytes_read = 0;
    uint32_t cluster_size = boot_sector.sectors_per_cluster * 512;

    uint8_t* cluster_buffer = (uint8_t*)kmalloc(cluster_size);
    if (!cluster_buffer) {
        tty_putstr("FAT32: Out of memory\n");
        return -1;
    }

    while (bytes_read < size && file->current_pos < file->file_size) {
        uint32_t lba = fat32_cluster_to_lba(file->current_cluster);
        uint32_t offset_in_cluster = file->current_pos % cluster_size;
        uint32_t bytes_to_read = cluster_size - offset_in_cluster;

        if (bytes_to_read > size - bytes_read) bytes_to_read = size - bytes_read;
        if (bytes_to_read > file->file_size - file->current_pos) bytes_to_read = file->file_size - file->current_pos;

        // Read cluster
        for (uint32_t i = 0; i < boot_sector.sectors_per_cluster; i++) {
            if (ata_read_sectors(lba + i, 1, (uint16_t*)(cluster_buffer + i * 512)) != 0) {
                kfree(cluster_buffer);
                return bytes_read;
            }
        }

        // Copy data
        for (uint32_t i = 0; i < bytes_to_read; i++) {
            buffer[bytes_read + i] = cluster_buffer[offset_in_cluster + i];
        }

        bytes_read += bytes_to_read;
        file->current_pos += bytes_to_read;

        if (file->current_pos % cluster_size == 0 && file->current_pos < file->file_size) {
            file->current_cluster = fat32_get_next_cluster(file->current_cluster);
            if (file->current_cluster >= FAT32_EOC) break;
        }
    }

    kfree(cluster_buffer);
    return bytes_read;
}

// Note: remaining functions from original source (create, delete, list, etc.) are
// preserved in the repository. This migrated file contains the heap-based read
// fix and core helpers; full implementation exists in-tree.
