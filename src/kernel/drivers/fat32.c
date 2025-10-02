#include "fat32.h"
#include "ata.h"
#include "tty.h"
#include "string.h"
#include "rtc.h"

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
    
    // Debug: Show boot signature
    char hex[3];
    hex[0] = "0123456789ABCDEF"[(boot_sector.boot_signature >> 4) & 0xF];
    hex[1] = "0123456789ABCDEF"[boot_sector.boot_signature & 0xF];
    hex[2] = '\0';
    
    // More flexible signature check - accept common values
    if (boot_sector.boot_signature != 0x29 && boot_sector.boot_signature != 0x28) {
        tty_putstr("Warning: Unusual boot signature, proceeding anyway\n");
        // Don't return -1, just warn
    }
    
    // Verify bytes per sector
    if (boot_sector.bytes_per_sector != 512) {
        tty_putstr("Error: Unsupported sector size: ");
        // Simple number display
        uint16_t size = boot_sector.bytes_per_sector;
        if (size == 0) tty_putstr("0");
        else if (size == 512) tty_putstr("512");
        else if (size == 1024) tty_putstr("1024");
        else tty_putstr("unknown");
        tty_putstr("\n");
        return -1;
    }
    
    // Check if it's likely FAT32
    if (boot_sector.fat_size_16 != 0) {
        tty_putstr("Warning: FAT16 detected, trying to use as FAT32\n");
        // Don't fail, some FAT32 disks might have this set
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
    
    // Parse regular filename
    // Parse name part
    for (i = 0, j = 0; i < 8 && input[j] != '\0' && input[j] != '.'; i++, j++) {
        if (input[j] >= 'a' && input[j] <= 'z') {
            output[i] = input[j] - 32; // Convert to uppercase
        } else {
            output[i] = input[j];
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

// Compare FAT filenames
int fat32_compare_names(const char* name1, const char* name2) {
    for (int i = 0; i < 11; i++) {
        if (name1[i] != name2[i]) {
            return 0;
        }
    }
    return 1;
}

// Print file information
void fat32_print_file_info(fat32_dir_entry_t* entry) {
    // Print filename
    for (int i = 0; i < 8 && entry->name[i] != ' '; i++) {
        tty_putchar_internal(entry->name[i]);
    }
    
    if (entry->name[8] != ' ') {
        tty_putchar_internal('.');
        for (int i = 8; i < 11 && entry->name[i] != ' '; i++) {
            tty_putchar_internal(entry->name[i]);
        }
    }
    
    // Print size or directory indicator
    if (entry->attributes & FAT_ATTR_DIRECTORY) {
        tty_putstr("  <DIR>");
    } else {
        tty_putstr("  ");
        // Simple size display
        uint32_t size = entry->file_size;
        if (size < 1024) {
            tty_putstr("bytes");
        } else if (size < 1024 * 1024) {
            tty_putstr("KB");
        } else {
            tty_putstr("MB");
        }
    }
    
    tty_putchar_internal('\n');
}

// List directory contents
int fat32_list_directory(uint32_t cluster) {
    if (cluster == 0) {
        cluster = root_dir_cluster;
    }
    
    uint32_t current_cluster = cluster;
    
    while (current_cluster < FAT32_EOC) {
        uint32_t lba = fat32_cluster_to_lba(current_cluster);
        
        // Read all sectors in this cluster
        for (uint32_t i = 0; i < boot_sector.sectors_per_cluster; i++) {
            if (ata_read_sectors(lba + i, 1, (uint16_t*)sector_buffer) != 0) {
                return -1;
            }
            
            // Parse directory entries
            fat32_dir_entry_t* entries = (fat32_dir_entry_t*)sector_buffer;
            
            for (int j = 0; j < 16; j++) {  // 16 entries per sector
                if (entries[j].name[0] == 0x00) {
                    // End of directory
                    return 0;
                }
                
                if (entries[j].name[0] == 0xE5) {
                    // Deleted entry, skip
                    continue;
                }
                
                if (entries[j].attributes == FAT_ATTR_LONG_NAME) {
                    // Long filename entry, skip for now
                    continue;
                }
                
                if (entries[j].attributes & FAT_ATTR_VOLUME_ID) {
                    // Volume label, skip
                    continue;
                }
                
                // Print file info
                fat32_print_file_info(&entries[j]);
            }
        }
        
        // Get next cluster
        current_cluster = fat32_get_next_cluster(current_cluster);
    }
    
    return 0;
}

// Find a file in directory
int fat32_find_file(const char* filename, uint32_t dir_cluster, fat32_dir_entry_t* entry) {
    char fat_name[11];
    fat32_parse_filename(filename, fat_name);
    
    if (dir_cluster == 0) {
        dir_cluster = root_dir_cluster;
    }
    
    uint32_t current_cluster = dir_cluster;
    
    while (current_cluster < FAT32_EOC) {
        uint32_t lba = fat32_cluster_to_lba(current_cluster);
        
        for (uint32_t i = 0; i < boot_sector.sectors_per_cluster; i++) {
            if (ata_read_sectors(lba + i, 1, (uint16_t*)sector_buffer) != 0) {
                return -1;
            }
            
            fat32_dir_entry_t* entries = (fat32_dir_entry_t*)sector_buffer;
            
            for (int j = 0; j < 16; j++) {
                if (entries[j].name[0] == 0x00) {
                    return -1;  // File not found
                }
                
                if (entries[j].name[0] == 0xE5) {
                    continue;
                }
                
                if (entries[j].attributes == FAT_ATTR_LONG_NAME) {
                    continue;
                }
                
                if (fat32_compare_names((char*)entries[j].name, fat_name)) {
                    *entry = entries[j];
                    return 0;  // Found!
                }
            }
        }
        
        current_cluster = fat32_get_next_cluster(current_cluster);
    }
    
    return -1;  // Not found
}

// Open a file
int fat32_open_file(const char* filename, fat32_file_t* file) {
    fat32_dir_entry_t entry;
    
    if (fat32_find_file(filename, current_directory_cluster, &entry) != 0) {
        return -1;
    }
    
    file->first_cluster = ((uint32_t)entry.first_cluster_high << 16) | entry.first_cluster_low;
    file->file_size = entry.file_size;
    file->current_pos = 0;
    file->current_cluster = file->first_cluster;
    file->attributes = entry.attributes;
    
    return 0;
}

// Read from a file
int fat32_read_file(fat32_file_t* file, uint8_t* buffer, uint32_t size) {
    uint32_t bytes_read = 0;
    uint32_t cluster_size = boot_sector.sectors_per_cluster * 512;
    
    while (bytes_read < size && file->current_pos < file->file_size) {
        uint32_t lba = fat32_cluster_to_lba(file->current_cluster);
        uint32_t offset_in_cluster = file->current_pos % cluster_size;
        uint32_t bytes_to_read = cluster_size - offset_in_cluster;
        
        if (bytes_to_read > size - bytes_read) {
            bytes_to_read = size - bytes_read;
        }
        
        if (bytes_to_read > file->file_size - file->current_pos) {
            bytes_to_read = file->file_size - file->current_pos;
        }
        
        // Read cluster
        uint8_t cluster_buffer[cluster_size];
        for (uint32_t i = 0; i < boot_sector.sectors_per_cluster; i++) {
            if (ata_read_sectors(lba + i, 1, (uint16_t*)(cluster_buffer + i * 512)) != 0) {
                return bytes_read;
            }
        }
        
        // Copy data to buffer
        for (uint32_t i = 0; i < bytes_to_read; i++) {
            buffer[bytes_read + i] = cluster_buffer[offset_in_cluster + i];
        }
        
        bytes_read += bytes_to_read;
        file->current_pos += bytes_to_read;
        
        // Check if we need to move to next cluster
        if (file->current_pos % cluster_size == 0 && file->current_pos < file->file_size) {
            file->current_cluster = fat32_get_next_cluster(file->current_cluster);
            if (file->current_cluster >= FAT32_EOC) {
                break;
            }
        }
    }
    
    return bytes_read;
}

// Get current FAT32 date from RTC
uint16_t fat32_get_current_date(void) {
    rtc_time_t current_time;
    rtc_read_time(&current_time);
    
    // Format: (year-1980) << 9 | month << 5 | day
    uint16_t year = current_time.year;
    if (year < 1980) year = 1980; // FAT32 epoch
    
    return ((year - 1980) << 9) | (current_time.month << 5) | current_time.day;
}

// Get current FAT32 time from RTC  
uint16_t fat32_get_current_time(void) {
    rtc_time_t current_time;
    rtc_read_time(&current_time);
    
    // Format: hour << 11 | minute << 5 | (second / 2)
    return (current_time.hours << 11) | (current_time.minutes << 5) | (current_time.seconds / 2);
}

// Set FAT entry (write to FAT)
int fat32_set_next_cluster(uint32_t cluster, uint32_t value) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_start_lba + (fat_offset / 512);
    uint32_t entry_offset = fat_offset % 512;
    
    // Read FAT sector
    if (ata_read_sectors(fat_sector, 1, (uint16_t*)sector_buffer) != 0) {
        return -1;
    }
    
    // Update entry
    uint32_t* fat_entry = (uint32_t*)&sector_buffer[entry_offset];
    *fat_entry = value & 0x0FFFFFFF;
    
    // Write back FAT sector
    if (ata_write_sectors(fat_sector, 1, (uint16_t*)sector_buffer) != 0) {
        return -1;
    }
    
    // Write to second FAT if exists
    if (boot_sector.num_fats > 1) {
        uint32_t fat2_sector = fat_sector + boot_sector.fat_size_32;
        ata_write_sectors(fat2_sector, 1, (uint16_t*)sector_buffer);
    }
    
    return 0;
}

// Allocate a free cluster
uint32_t fat32_allocate_cluster(void) {
    uint32_t total_clusters = (boot_sector.total_sectors_32 - data_start_lba) / boot_sector.sectors_per_cluster;
    
    // Search for free cluster (starting from cluster 2)
    for (uint32_t cluster = 2; cluster < total_clusters + 2; cluster++) {
        uint32_t next = fat32_get_next_cluster(cluster);
        if (next == FAT32_FREE_CLUSTER) {
            // Mark as end of chain
            fat32_set_next_cluster(cluster, FAT32_EOC);
            return cluster;
        }
    }
    
    return 0;  // No free cluster found
}

// Add directory entry to a directory cluster
int fat32_add_dir_entry(uint32_t dir_cluster, fat32_dir_entry_t* entry) {
    if (dir_cluster == 0) {
        dir_cluster = root_dir_cluster;
    }
    
    uint32_t current_cluster = dir_cluster;
    
    while (current_cluster < FAT32_EOC) {
        uint32_t lba = fat32_cluster_to_lba(current_cluster);
        
        for (uint32_t i = 0; i < boot_sector.sectors_per_cluster; i++) {
            if (ata_read_sectors(lba + i, 1, (uint16_t*)sector_buffer) != 0) {
                return -1;
            }
            
            fat32_dir_entry_t* entries = (fat32_dir_entry_t*)sector_buffer;
            
            // Find free entry
            for (int j = 0; j < 16; j++) {
                if (entries[j].name[0] == 0x00 || entries[j].name[0] == 0xE5) {
                    // Found free slot
                    entries[j] = *entry;
                    
                    // Write back sector
                    if (ata_write_sectors(lba + i, 1, (uint16_t*)sector_buffer) != 0) {
                        return -1;
                    }
                    
                    return 0;  // Success
                }
            }
        }
        
        // Move to next cluster
        uint32_t next_cluster = fat32_get_next_cluster(current_cluster);
        if (next_cluster >= FAT32_EOC) {
            // Need to allocate new cluster for directory
            uint32_t new_cluster = fat32_allocate_cluster();
            if (new_cluster == 0) {
                return -1;  // Disk full
            }
            fat32_set_next_cluster(current_cluster, new_cluster);
            current_cluster = new_cluster;
            
            // Clear new cluster
            uint8_t zero_buffer[512] = {0};
            uint32_t new_lba = fat32_cluster_to_lba(new_cluster);
            for (uint32_t i = 0; i < boot_sector.sectors_per_cluster; i++) {
                ata_write_sectors(new_lba + i, 1, (uint16_t*)zero_buffer);
            }
        } else {
            current_cluster = next_cluster;
        }
    }
    
    return -1;  // Failed
}

// Create a new file
int fat32_create_file(const char* filename, const uint8_t* data, uint32_t size) {
    // Check if file already exists
    fat32_dir_entry_t existing_entry;
    if (fat32_find_file(filename, current_directory_cluster, &existing_entry) == 0) {
        tty_putstr("Error: File already exists\n");
        return -1;
    }
    
    uint32_t first_cluster = 0;
    
    // Only allocate cluster if file has content
    if (size > 0) {
        first_cluster = fat32_allocate_cluster();
        if (first_cluster == 0) {
            tty_putstr("Error: Disk full\n");
            return -1;
        }
    }
    
    // Write data to cluster (only if we have content and allocated a cluster)
    if (size > 0 && first_cluster != 0) {
        uint32_t cluster_size = boot_sector.sectors_per_cluster * 512;
        uint32_t lba = fat32_cluster_to_lba(first_cluster);
        
        // For simplicity, limit to one cluster
        uint32_t write_size = size > cluster_size ? cluster_size : size;
        
        // Prepare buffer
        uint8_t write_buffer[cluster_size];
        for (uint32_t i = 0; i < cluster_size; i++) {
            write_buffer[i] = i < write_size ? data[i] : 0;
        }
        
        // Write sectors
        for (uint32_t i = 0; i < boot_sector.sectors_per_cluster; i++) {
            if (ata_write_sectors(lba + i, 1, (uint16_t*)(write_buffer + i * 512)) != 0) {
                return -1;
            }
        }
    }
    
    // Create directory entry
    fat32_dir_entry_t new_entry;
    fat32_parse_filename(filename, (char*)new_entry.name);
    new_entry.attributes = FAT_ATTR_ARCHIVE;
    new_entry.reserved = 0;
    new_entry.create_time_tenth = 0;
    new_entry.create_time = fat32_get_current_time();
    new_entry.create_date = fat32_get_current_date();
    new_entry.access_date = fat32_get_current_date();
    new_entry.first_cluster_high = (first_cluster >> 16) & 0xFFFF;
    new_entry.modify_time = fat32_get_current_time();
    new_entry.modify_date = fat32_get_current_date();
    new_entry.first_cluster_low = first_cluster & 0xFFFF;
    new_entry.file_size = size;
    
    // Add entry to directory
    if (fat32_add_dir_entry(current_directory_cluster, &new_entry) != 0) {
        tty_putstr("Error: Could not add directory entry\n");
        return -1;
    }
    
    return 0;
}

// Delete a file from the FAT32 filesystem
int fat32_delete_file(const char* filename) {
    if (!fat32_initialized) {
        tty_putstr("Error: FAT32 not initialized\n");
        return -1;
    }
    
    char fat32_name[11];
    fat32_parse_filename(filename, fat32_name);
    
    // Read root directory
    uint32_t lba = fat32_cluster_to_lba(current_directory_cluster);
    uint8_t sector_buffer[512];
    
    for (uint32_t sector = 0; sector < boot_sector.sectors_per_cluster; sector++) {
        if (ata_read_sectors(lba + sector, 1, (uint16_t*)sector_buffer) != 0) {
            return -1;
        }
        
        fat32_dir_entry_t* entries = (fat32_dir_entry_t*)sector_buffer;
        
        for (int i = 0; i < 512 / sizeof(fat32_dir_entry_t); i++) {
            // Check if entry is deleted or end of directory
            if (entries[i].name[0] == 0x00) break;
            if (entries[i].name[0] == 0xE5) continue; // Already deleted
            
            // Skip volume labels and directories
            if (entries[i].attributes & FAT_ATTR_VOLUME_ID) continue;
            if (entries[i].attributes & FAT_ATTR_DIRECTORY) continue;
            
            // Compare filenames
            if (fat32_compare_names((char*)entries[i].name, fat32_name)) {
                // Found the file - mark as deleted
                entries[i].name[0] = 0xE5;
                
                // Get cluster chain and free it
                uint32_t cluster = ((uint32_t)entries[i].first_cluster_high << 16) | entries[i].first_cluster_low;
                
                // Free cluster chain in FAT
                while (cluster >= 2 && cluster < 0x0FFFFFF8) {
                    uint32_t next_cluster = fat32_get_next_cluster(cluster);
                    fat32_set_next_cluster(cluster, 0); // Mark as free
                    cluster = next_cluster;
                }
                
                // Write back the modified directory entry
                if (ata_write_sectors(lba + sector, 1, (uint16_t*)sector_buffer) != 0) {
                    tty_putstr("Error: Could not update directory\n");
                    return -1;
                }
                
                tty_putstr("File deleted: ");
                tty_putstr(filename);
                tty_putstr("\n");
                return 0;
            }
        }
    }
    
    tty_putstr("Error: File not found: ");
    tty_putstr(filename);
    tty_putstr("\n");
    return -1;
}

// Delete all files from the FAT32 filesystem
int fat32_delete_all_files(void) {
    if (!fat32_initialized) {
        tty_putstr("Error: FAT32 not initialized\n");
        return -1;
    }
    
    int deleted_count = 0;
    
    // Read root directory
    uint32_t lba = fat32_cluster_to_lba(current_directory_cluster);
    uint8_t sector_buffer[512];
    
    for (uint32_t sector = 0; sector < boot_sector.sectors_per_cluster; sector++) {
        if (ata_read_sectors(lba + sector, 1, (uint16_t*)sector_buffer) != 0) {
            return -1;
        }
        
        fat32_dir_entry_t* entries = (fat32_dir_entry_t*)sector_buffer;
        int sector_modified = 0;
        
        for (int i = 0; i < 512 / sizeof(fat32_dir_entry_t); i++) {
            // Check if entry is end of directory
            if (entries[i].name[0] == 0x00) break;
            if (entries[i].name[0] == 0xE5) continue; // Already deleted
            
            // Skip volume labels and directories
            if (entries[i].attributes & FAT_ATTR_VOLUME_ID) continue;
            if (entries[i].attributes & FAT_ATTR_DIRECTORY) continue;
            
            // Found a file - mark as deleted
            entries[i].name[0] = 0xE5;
            sector_modified = 1;
            deleted_count++;
            
            // Get cluster chain and free it
            uint32_t cluster = ((uint32_t)entries[i].first_cluster_high << 16) | entries[i].first_cluster_low;
            
            // Free cluster chain in FAT
            while (cluster >= 2 && cluster < 0x0FFFFFF8) {
                uint32_t next_cluster = fat32_get_next_cluster(cluster);
                fat32_set_next_cluster(cluster, 0); // Mark as free
                cluster = next_cluster;
            }
            
            // Print filename being deleted (convert from FAT32 format)
            tty_putstr("Deleted: ");
            for (int j = 0; j < 8; j++) {
                if (entries[i].name[j] != ' ') {
                    tty_putchar_internal(entries[i].name[j]);
                }
            }
            if (entries[i].name[8] != ' ') {
                tty_putchar_internal('.');
                for (int j = 8; j < 11; j++) {
                    if (entries[i].name[j] != ' ') {
                        tty_putchar_internal(entries[i].name[j]);
                    }
                }
            }
            tty_putchar_internal('\n');
        }
        
        // Write back the modified directory if needed
        if (sector_modified) {
            if (ata_write_sectors(lba + sector, 1, (uint16_t*)sector_buffer) != 0) {
                tty_putstr("Error: Could not update directory\n");
                return -1;
            }
        }
    }
    
    if (deleted_count > 0) {
        tty_putstr("Total files deleted: ");
        // Simple number display
        if (deleted_count < 10) {
            char num = '0' + deleted_count;
            tty_putchar_internal(num);
        } else {
            tty_putstr("many");
        }
        tty_putchar_internal('\n');
    } else {
        tty_putstr("No files to delete\n");
    }
    
    return 0;
}

// Extend a cluster chain by adding additional clusters
int fat32_extend_cluster_chain(uint32_t last_cluster, uint32_t additional_clusters) {
    if (!fat32_initialized) {
        return -1;
    }
    
    uint32_t current_cluster = last_cluster;
    
    for (uint32_t i = 0; i < additional_clusters; i++) {
        uint32_t new_cluster = fat32_allocate_cluster();
        if (new_cluster == 0) {
            tty_putstr("Error: No more free clusters available\n");
            return -1;
        }
        
        // Link the current cluster to the new one
        if (fat32_set_next_cluster(current_cluster, new_cluster) != 0) {
            return -1;
        }
        
        // Mark the new cluster as end of chain
        if (fat32_set_next_cluster(new_cluster, FAT32_EOC) != 0) {
            return -1;
        }
        
        current_cluster = new_cluster;
    }
    
    return 0;
}

// Free a cluster chain starting from a given cluster
int fat32_free_cluster_chain(uint32_t start_cluster) {
    if (!fat32_initialized) {
        return -1;
    }
    
    uint32_t current_cluster = start_cluster;
    
    while (current_cluster >= 2 && current_cluster < 0x0FFFFFF8) {
        uint32_t next_cluster = fat32_get_next_cluster(current_cluster);
        
        // Mark current cluster as free
        if (fat32_set_next_cluster(current_cluster, 0) != 0) {
            return -1;
        }
        
        current_cluster = next_cluster;
    }
    
    return 0;
}

// Update directory entry file size
int fat32_update_dir_entry_size(const char* filename, uint32_t new_size) {
    if (!fat32_initialized) {
        return -1;
    }
    
    char fat32_name[11];
    fat32_parse_filename(filename, fat32_name);
    
    // Read root directory
    uint32_t lba = fat32_cluster_to_lba(current_directory_cluster);
    uint8_t sector_buffer[512];
    
    for (uint32_t sector = 0; sector < boot_sector.sectors_per_cluster; sector++) {
        if (ata_read_sectors(lba + sector, 1, (uint16_t*)sector_buffer) != 0) {
            return -1;
        }
        
        fat32_dir_entry_t* entries = (fat32_dir_entry_t*)sector_buffer;
        
        for (int i = 0; i < 512 / sizeof(fat32_dir_entry_t); i++) {
            // Check if entry is deleted or end of directory
            if (entries[i].name[0] == 0x00) break;
            if (entries[i].name[0] == 0xE5) continue; // Already deleted
            
            // Skip volume labels and directories
            if (entries[i].attributes & FAT_ATTR_VOLUME_ID) continue;
            if (entries[i].attributes & FAT_ATTR_DIRECTORY) continue;
            
            // Compare filenames
            if (fat32_compare_names((char*)entries[i].name, fat32_name)) {
                // Update file size
                entries[i].file_size = new_size;
                
                // Update modification time/date
                entries[i].modify_time = fat32_get_current_time();
                entries[i].modify_date = fat32_get_current_date();
                
                // Write back the modified directory entry
                if (ata_write_sectors(lba + sector, 1, (uint16_t*)sector_buffer) != 0) {
                    return -1;
                }
                
                return 0;
            }
        }
    }
    
    return -1; // File not found
}

// Update an existing file or create a new one with dynamic sizing
int fat32_update_file(const char* filename, const uint8_t* data, uint32_t new_size) {
    if (!fat32_initialized) {
        tty_putstr("Error: FAT32 not initialized\n");
        return -1;
    }
    
    // Try to find existing file
    fat32_file_t existing_file;
    int file_exists = (fat32_open_file(filename, &existing_file) == 0);
    
    if (file_exists) {
        // File exists - update it
        uint32_t old_size = existing_file.file_size;
        uint32_t old_clusters = (old_size + boot_sector.sectors_per_cluster * 512 - 1) / (boot_sector.sectors_per_cluster * 512);
        uint32_t new_clusters = (new_size + boot_sector.sectors_per_cluster * 512 - 1) / (boot_sector.sectors_per_cluster * 512);
        
        uint32_t first_cluster = existing_file.first_cluster;
        
        if (new_clusters > old_clusters) {
            // Need to allocate more clusters
            uint32_t additional_clusters = new_clusters - old_clusters;
            
            // Find the last cluster in the existing chain
            uint32_t last_cluster = first_cluster;
            if (old_clusters > 1) {
                uint32_t current = first_cluster;
                while (current >= 2 && current < 0x0FFFFFF8) {
                    uint32_t next = fat32_get_next_cluster(current);
                    if (next >= 0x0FFFFFF8) break; // End of chain
                    last_cluster = next;
                    current = next;
                }
            }
            
            // Extend the cluster chain
            if (fat32_extend_cluster_chain(last_cluster, additional_clusters) != 0) {
                tty_putstr("Error: Could not extend file\n");
                return -1;
            }
        } else if (new_clusters < old_clusters) {
            // Need to free some clusters
            uint32_t clusters_to_keep = new_clusters;
            uint32_t current = first_cluster;
            
            // Navigate to the cluster that should be the new end
            for (uint32_t i = 1; i < clusters_to_keep && current >= 2 && current < 0x0FFFFFF8; i++) {
                current = fat32_get_next_cluster(current);
            }
            
            if (current >= 2 && current < 0x0FFFFFF8) {
                uint32_t next_cluster = fat32_get_next_cluster(current);
                
                // Mark this cluster as end of chain
                fat32_set_next_cluster(current, FAT32_EOC);
                
                // Free the remaining clusters
                if (next_cluster >= 2 && next_cluster < 0x0FFFFFF8) {
                    fat32_free_cluster_chain(next_cluster);
                }
            }
        }
        
        // Write the new data
        uint32_t bytes_written = 0;
        uint32_t current_cluster = first_cluster;
        uint32_t cluster_size = boot_sector.sectors_per_cluster * 512;
        
        while (bytes_written < new_size && current_cluster >= 2 && current_cluster < 0x0FFFFFF8) {
            uint32_t lba = fat32_cluster_to_lba(current_cluster);
            uint8_t write_buffer[cluster_size];
            
            // Clear buffer
            for (uint32_t i = 0; i < cluster_size; i++) {
                write_buffer[i] = 0;
            }
            
            // Copy data to buffer
            uint32_t bytes_to_copy = new_size - bytes_written;
            if (bytes_to_copy > cluster_size) {
                bytes_to_copy = cluster_size;
            }
            
            if (bytes_to_copy > 0) {
                for (uint32_t i = 0; i < bytes_to_copy; i++) {
                    write_buffer[i] = data[bytes_written + i];
                }
            }
            
            // Write cluster
            for (uint32_t i = 0; i < boot_sector.sectors_per_cluster; i++) {
                if (ata_write_sectors(lba + i, 1, (uint16_t*)(write_buffer + i * 512)) != 0) {
                    return -1;
                }
            }
            
            bytes_written += bytes_to_copy;
            current_cluster = fat32_get_next_cluster(current_cluster);
        }
        
        // Update directory entry size
        if (fat32_update_dir_entry_size(filename, new_size) != 0) {
            tty_putstr("Error: Could not update directory entry\n");
            return -1;
        }
        
        return 0;
    } else {
        // File doesn't exist - create new file
        return fat32_create_file(filename, data, new_size);
    }
}

// Create a new directory
int fat32_create_directory(const char* dirname) {
    if (!fat32_initialized) {
        tty_putstr("Error: FAT32 not initialized\n");
        return -1;
    }
    
    tty_putstr("Creating directory: ");
    tty_putstr(dirname);
    tty_putstr("\n");
    
    char fat32_name[11];
    fat32_parse_filename(dirname, fat32_name);
    
    // Check if directory already exists
    fat32_dir_entry_t existing_entry;
    if (fat32_find_file(dirname, current_directory_cluster, &existing_entry) == 0) {
        tty_putstr("Error: Directory already exists: ");
        tty_putstr(dirname);
        tty_putstr("\n");
        return -1;
    }
    
    // Allocate cluster for new directory
    uint32_t dir_cluster = fat32_allocate_cluster();
    if (dir_cluster == 0) {
        tty_putstr("Error: No free clusters available\n");
        return -1;
    }
    
    // Mark end of cluster chain
    fat32_set_next_cluster(dir_cluster, FAT32_EOC);
    
    // Clear directory cluster
    uint8_t empty_sector[512];
    for (int i = 0; i < 512; i++) empty_sector[i] = 0;
    
    uint32_t lba = fat32_cluster_to_lba(dir_cluster);
    for (uint32_t i = 0; i < boot_sector.sectors_per_cluster; i++) {
        if (ata_write_sectors(lba + i, 1, (uint16_t*)empty_sector) != 0) {
            tty_putstr("Error: Could not clear directory cluster\n");
            return -1;
        }
    }
    
    // Create "." entry (current directory)
    fat32_dir_entry_t dot_entry;
    for (int i = 0; i < 11; i++) dot_entry.name[i] = ' ';
    dot_entry.name[0] = '.';
    dot_entry.attributes = FAT_ATTR_DIRECTORY;
    dot_entry.reserved = 0;
    dot_entry.create_time_tenth = 0;
    dot_entry.create_time = fat32_get_current_time();
    dot_entry.create_date = fat32_get_current_date();
    dot_entry.access_date = fat32_get_current_date();
    dot_entry.first_cluster_high = (dir_cluster >> 16) & 0xFFFF;
    dot_entry.modify_time = fat32_get_current_time();
    dot_entry.modify_date = fat32_get_current_date();
    dot_entry.first_cluster_low = dir_cluster & 0xFFFF;
    dot_entry.file_size = 0;
    
    // Create ".." entry (parent directory)
    fat32_dir_entry_t dotdot_entry;
    for (int i = 0; i < 11; i++) dotdot_entry.name[i] = ' ';
    dotdot_entry.name[0] = '.';
    dotdot_entry.name[1] = '.';
    dotdot_entry.attributes = FAT_ATTR_DIRECTORY;
    dotdot_entry.reserved = 0;
    dotdot_entry.create_time_tenth = 0;
    dotdot_entry.create_time = fat32_get_current_time();
    dotdot_entry.create_date = fat32_get_current_date();
    dotdot_entry.access_date = fat32_get_current_date();
    
    // Special case: if current directory is root, ".." should point to root (cluster 0 in some systems)
    uint32_t parent_cluster = current_directory_cluster;
    if (current_directory_cluster == boot_sector.root_cluster) {
        parent_cluster = 0; // Root directory special case for ".."
    }
    
    dotdot_entry.first_cluster_high = (parent_cluster >> 16) & 0xFFFF;
    dotdot_entry.modify_time = fat32_get_current_time();
    dotdot_entry.modify_date = fat32_get_current_date();
    dotdot_entry.first_cluster_low = parent_cluster & 0xFFFF;
    dotdot_entry.file_size = 0;
    
    // Write . and .. entries to the new directory
    uint8_t sector_buffer[512];
    for (int i = 0; i < 512; i++) sector_buffer[i] = 0;
    
    fat32_dir_entry_t* entries = (fat32_dir_entry_t*)sector_buffer;
    entries[0] = dot_entry;
    entries[1] = dotdot_entry;
    
    if (ata_write_sectors(lba, 1, (uint16_t*)sector_buffer) != 0) {
        tty_putstr("Error: Could not write directory entries\n");
        return -1;
    }
    
    // Create directory entry in parent directory
    fat32_dir_entry_t new_dir_entry;
    fat32_parse_filename(dirname, (char*)new_dir_entry.name);
    new_dir_entry.attributes = FAT_ATTR_DIRECTORY;
    new_dir_entry.reserved = 0;
    new_dir_entry.create_time_tenth = 0;
    new_dir_entry.create_time = fat32_get_current_time();
    new_dir_entry.create_date = fat32_get_current_date();
    new_dir_entry.access_date = fat32_get_current_date();
    new_dir_entry.first_cluster_high = (dir_cluster >> 16) & 0xFFFF;
    new_dir_entry.modify_time = fat32_get_current_time();
    new_dir_entry.modify_date = fat32_get_current_date();
    new_dir_entry.first_cluster_low = dir_cluster & 0xFFFF;
    new_dir_entry.file_size = 0;
    
    // Add entry to parent directory
    if (fat32_add_dir_entry(current_directory_cluster, &new_dir_entry) != 0) {
        tty_putstr("Error: Could not add directory entry\n");
        return -1;
    }
    
    tty_putstr("Directory created successfully: ");
    tty_putstr(dirname);
    tty_putstr("\n");
    return 0;
    
    /*
    char fat32_name[11];
    fat32_parse_filename(dirname, fat32_name);
    
    // Check if directory already exists
    fat32_dir_entry_t existing_entry;
    if (fat32_find_file(dirname, current_directory_cluster, &existing_entry) == 0) {
        tty_putstr("Error: Directory already exists: ");
        tty_putstr(dirname);
        tty_putstr("\n");
        return -1;
    }
    
    // Allocate cluster for new directory
    uint32_t dir_cluster = fat32_allocate_cluster();
    if (dir_cluster == 0) {
        tty_putstr("Error: No free clusters available\n");
        return -1;
    }*/
}

// Change current directory
int fat32_change_directory(const char* dirname) {
    if (!fat32_initialized) {
        tty_putstr("Error: FAT32 not initialized\n");
        return -1;
    }
    
    // Handle special cases
    if (dirname[0] == '.' && dirname[1] == '\0') {
        // Current directory - no change needed
        return 0;
    }
    
    if (dirname[0] == '.' && dirname[1] == '.' && dirname[2] == '\0') {
        // Parent directory
        fat32_dir_entry_t dotdot_entry;
        if (fat32_find_file("..", current_directory_cluster, &dotdot_entry) == 0) {
            uint32_t parent_cluster = ((uint32_t)dotdot_entry.first_cluster_high << 16) | dotdot_entry.first_cluster_low;
            
            // Handle special case where ".." points to root (cluster 0)
            if (parent_cluster == 0) {
                parent_cluster = boot_sector.root_cluster;
            }
            
            current_directory_cluster = parent_cluster;
            
            // Update full path - go up one level
            if (parent_cluster == boot_sector.root_cluster) {
                current_full_path[0] = '/';
                current_full_path[1] = '\0';
            } else {
                // Remove the last directory from the path
                int len = 0;
                while (current_full_path[len] != '\0') len++; // Get string length
                
                // Go backwards to find the last '/' (but not the first one)
                if (len > 1) {
                    len--; // Skip the trailing character
                    while (len > 0 && current_full_path[len] != '/') {
                        len--;
                    }
                    current_full_path[len + 1] = '\0'; // Keep the '/' and terminate after it
                    
                    // If we're back to root, make sure it's just "/"
                    if (len == 0) {
                        current_full_path[1] = '\0';
                    }
                }
            }
            return 0;
        } else {
            tty_putstr("Error: Cannot find .. entry in current directory\n");
            tty_putstr("Debugging: Listing current directory contents:\n");
            fat32_list_directory(current_directory_cluster);
            return -1;
        }
    }
    
    // Find the directory
    fat32_dir_entry_t entry;
    if (fat32_find_file(dirname, current_directory_cluster, &entry) != 0) {
        tty_putstr("Error: Directory not found: ");
        tty_putstr(dirname);
        tty_putstr("\n");
        return -1;
    }
    
    // Check if it's actually a directory
    if (!(entry.attributes & FAT_ATTR_DIRECTORY)) {
        tty_putstr("Error: Not a directory: ");
        tty_putstr(dirname);
        tty_putstr("\n");
        return -1;
    }
    
    // Change to the directory
    uint32_t new_cluster = ((uint32_t)entry.first_cluster_high << 16) | entry.first_cluster_low;
    current_directory_cluster = new_cluster;
    
    // Update full path - add new directory
    int path_len = 0;
    while (current_full_path[path_len] != '\0') path_len++; // Get current path length
    
    // If not at root, add a slash separator
    if (path_len > 1) {
        current_full_path[path_len++] = '/';
    }
    
    // Add the new directory name
    int j = 0;
    while (j < 200 && dirname[j] != '\0' && (path_len + j) < 255) {
        current_full_path[path_len + j] = dirname[j];
        j++;
    }
    current_full_path[path_len + j] = '\0';
    
    return 0;
}

// Change directory using complex paths (e.g., "folder/subfolder", "../../..", "../folder")
int fat32_change_directory_path(const char* path) {
    if (!fat32_initialized) {
        tty_putstr("Error: FAT32 not initialized\n");
        return -1;
    }
    
    // Handle empty path
    if (path[0] == '\0') {
        return 0;
    }
    
    // Handle absolute paths (starting with /)
    if (path[0] == '/') {
        // Go to root first
        current_directory_cluster = boot_sector.root_cluster;
        current_full_path[0] = '/';
        current_full_path[1] = '\0';
        
        // If path is just "/", we're done
        if (path[1] == '\0') {
            return 0;
        }
        
        // Continue with rest of path (skip the leading /)
        path++;
    }
    
    // Parse path components separated by '/'
    char component[64];
    int path_pos = 0;
    int comp_pos = 0;
    
    while (1) {
        // Extract next path component
        comp_pos = 0;
        
        // Skip any leading slashes
        while (path[path_pos] == '/') path_pos++;
        
        // If we've reached the end, we're done
        if (path[path_pos] == '\0') break;
        
        // Extract component until next '/' or end of string
        while (path[path_pos] != '\0' && path[path_pos] != '/' && comp_pos < 63) {
            component[comp_pos++] = path[path_pos++];
        }
        component[comp_pos] = '\0';
        
        // Skip empty components
        if (component[0] == '\0') continue;
        
        // Change to this directory component
        if (fat32_change_directory(component) != 0) {
            tty_putstr("Error: Cannot navigate to: ");
            tty_putstr(component);
            tty_putstr("\n");
            return -1;
        }
    }
    
    return 0;
}

// Remove an empty directory
int fat32_remove_directory(const char* dirname) {
    if (!fat32_initialized) {
        tty_putstr("Error: FAT32 not initialized\n");
        return -1;
    }
    
    // Don't allow removing . or ..
    if ((dirname[0] == '.' && dirname[1] == '\0') || 
        (dirname[0] == '.' && dirname[1] == '.' && dirname[2] == '\0')) {
        tty_putstr("Error: Cannot remove . or ..\n");
        return -1;
    }
    
    // Find the directory
    fat32_dir_entry_t entry;
    if (fat32_find_file(dirname, current_directory_cluster, &entry) != 0) {
        tty_putstr("Error: Directory not found: ");
        tty_putstr(dirname);
        tty_putstr("\n");
        return -1;
    }
    
    // Check if it's actually a directory
    if (!(entry.attributes & FAT_ATTR_DIRECTORY)) {
        tty_putstr("Error: Not a directory: ");
        tty_putstr(dirname);
        tty_putstr("\n");
        return -1;
    }
    
    // Check if directory is empty (should only contain . and ..)
    uint32_t dir_cluster = ((uint32_t)entry.first_cluster_high << 16) | entry.first_cluster_low;
    uint32_t lba = fat32_cluster_to_lba(dir_cluster);
    uint8_t sector_buffer[512];
    
    if (ata_read_sectors(lba, 1, (uint16_t*)sector_buffer) != 0) {
        tty_putstr("Error: Could not read directory\n");
        return -1;
    }
    
    fat32_dir_entry_t* entries = (fat32_dir_entry_t*)sector_buffer;
    int entry_count = 0;
    
    for (int i = 0; i < 512 / sizeof(fat32_dir_entry_t); i++) {
        if (entries[i].name[0] == 0x00) break; // End of directory
        if (entries[i].name[0] == 0xE5) continue; // Deleted entry
        
        entry_count++;
        
        // Allow only . and .. entries
        if (!(entries[i].name[0] == '.' && 
              (entries[i].name[1] == ' ' || entries[i].name[1] == '.'))) {
            tty_putstr("Error: Directory not empty: ");
            tty_putstr(dirname);
            tty_putstr("\n");
            return -1;
        }
    }
    
    // Directory is empty, proceed with deletion
    // Free the directory's cluster
    fat32_set_next_cluster(dir_cluster, 0);
    
    // Remove directory entry from parent
    char fat32_name[11];
    fat32_parse_filename(dirname, fat32_name);
    
    lba = fat32_cluster_to_lba(current_directory_cluster);
    
    for (uint32_t sector = 0; sector < boot_sector.sectors_per_cluster; sector++) {
        if (ata_read_sectors(lba + sector, 1, (uint16_t*)sector_buffer) != 0) {
            return -1;
        }
        
        entries = (fat32_dir_entry_t*)sector_buffer;
        
        for (int i = 0; i < 512 / sizeof(fat32_dir_entry_t); i++) {
            if (entries[i].name[0] == 0x00) break;
            if (entries[i].name[0] == 0xE5) continue;
            
            if (fat32_compare_names((char*)entries[i].name, fat32_name)) {
                // Mark as deleted
                entries[i].name[0] = 0xE5;
                
                // Write back
                if (ata_write_sectors(lba + sector, 1, (uint16_t*)sector_buffer) != 0) {
                    tty_putstr("Error: Could not update directory\n");
                    return -1;
                }
                
                tty_putstr("Directory removed: ");
                tty_putstr(dirname);
                tty_putstr("\n");
                return 0;
            }
        }
    }
    
    tty_putstr("Error: Could not find directory entry\n");
    return -1;
}

// Remove a directory recursively (with all contents)
int fat32_remove_directory_recursive(const char* dirname) {
    if (!fat32_initialized) {
        tty_putstr("Error: FAT32 not initialized\n");
        return -1;
    }
    
    // Don't allow removing . or ..
    if ((dirname[0] == '.' && dirname[1] == '\0') || 
        (dirname[0] == '.' && dirname[1] == '.' && dirname[2] == '\0')) {
        tty_putstr("Error: Cannot remove . or ..\n");
        return -1;
    }
    
    // Find the directory
    fat32_dir_entry_t entry;
    if (fat32_find_file(dirname, current_directory_cluster, &entry) != 0) {
        tty_putstr("Error: Directory not found: ");
        tty_putstr(dirname);
        tty_putstr("\n");
        return -1;
    }
    
    // Check if it's actually a directory
    if (!(entry.attributes & FAT_ATTR_DIRECTORY)) {
        tty_putstr("Error: Not a directory: ");
        tty_putstr(dirname);
        tty_putstr("\n");
        return -1;
    }
    
    uint32_t dir_cluster = ((uint32_t)entry.first_cluster_high << 16) | entry.first_cluster_low;
    
    // First, recursively delete all contents of the directory
    uint32_t lba = fat32_cluster_to_lba(dir_cluster);
    uint8_t sector_buffer[512];
    
    for (uint32_t sector = 0; sector < boot_sector.sectors_per_cluster; sector++) {
        if (ata_read_sectors(lba + sector, 1, (uint16_t*)sector_buffer) != 0) {
            tty_putstr("Error: Could not read directory contents\n");
            return -1;
        }
        
        fat32_dir_entry_t* entries = (fat32_dir_entry_t*)sector_buffer;
        
        for (int i = 0; i < 512 / sizeof(fat32_dir_entry_t); i++) {
            if (entries[i].name[0] == 0x00) break; // End of directory
            if (entries[i].name[0] == 0xE5) continue; // Already deleted
            
            // Skip . and .. entries
            if (entries[i].name[0] == '.' && 
                (entries[i].name[1] == ' ' || entries[i].name[1] == '.')) {
                continue;
            }
            
            // Convert FAT32 name back to normal filename for deletion
            char filename[12];
            int pos = 0;
            
            // Copy name part (8 chars)
            for (int j = 0; j < 8 && entries[i].name[j] != ' '; j++) {
                filename[pos++] = entries[i].name[j];
            }
            
            // Add extension if present
            if (entries[i].name[8] != ' ') {
                filename[pos++] = '.';
                for (int j = 8; j < 11 && entries[i].name[j] != ' '; j++) {
                    filename[pos++] = entries[i].name[j];
                }
            }
            filename[pos] = '\0';
            
            if (entries[i].attributes & FAT_ATTR_DIRECTORY) {
                // It's a subdirectory - recursively delete it
                tty_putstr("Removing subdirectory: ");
                tty_putstr(filename);
                tty_putstr("\n");
                
                // Save current directory
                uint32_t saved_current_dir = current_directory_cluster;
                
                // Change to the directory we're cleaning
                current_directory_cluster = dir_cluster;
                
                // Recursively delete subdirectory
                fat32_remove_directory_recursive(filename);
                
                // Restore current directory
                current_directory_cluster = saved_current_dir;
            } else {
                // It's a file - delete it
                tty_putstr("Removing file: ");
                tty_putstr(filename);
                tty_putstr("\n");
                
                // Save current directory
                uint32_t saved_current_dir = current_directory_cluster;
                
                // Change to the directory containing the file
                current_directory_cluster = dir_cluster;
                
                // Delete the file
                fat32_delete_file(filename);
                
                // Restore current directory
                current_directory_cluster = saved_current_dir;
            }
        }
    }
    
    // Now the directory should be empty, delete it using the regular function
    // Free the directory's cluster chain
    fat32_free_cluster_chain(dir_cluster);
    
    // Remove directory entry from parent
    char fat32_name[11];
    fat32_parse_filename(dirname, fat32_name);
    
    lba = fat32_cluster_to_lba(current_directory_cluster);
    
    for (uint32_t sector = 0; sector < boot_sector.sectors_per_cluster; sector++) {
        if (ata_read_sectors(lba + sector, 1, (uint16_t*)sector_buffer) != 0) {
            return -1;
        }
        
        fat32_dir_entry_t* entries = (fat32_dir_entry_t*)sector_buffer;
        
        for (int i = 0; i < 512 / sizeof(fat32_dir_entry_t); i++) {
            if (entries[i].name[0] == 0x00) break;
            if (entries[i].name[0] == 0xE5) continue;
            
            if (fat32_compare_names((char*)entries[i].name, fat32_name)) {
                // Mark as deleted
                entries[i].name[0] = 0xE5;
                
                // Write back
                if (ata_write_sectors(lba + sector, 1, (uint16_t*)sector_buffer) != 0) {
                    tty_putstr("Error: Could not update directory\n");
                    return -1;
                }
                
                tty_putstr("Directory and all contents removed: ");
                tty_putstr(dirname);
                tty_putstr("\n");
                return 0;
            }
        }
    }
    
    tty_putstr("Error: Could not find directory entry\n");
    return -1;
}

// List a specific directory by name  
int fat32_list_directory_by_name(const char* dirname) {
    if (!fat32_initialized) {
        tty_putstr("Error: FAT32 not initialized\n");
        return -1;
    }
    
    uint32_t target_cluster = current_directory_cluster;
    
    // If directory name provided, find it
    if (dirname != 0 && dirname[0] != '\0') {
        fat32_dir_entry_t entry;
        if (fat32_find_file(dirname, current_directory_cluster, &entry) != 0) {
            tty_putstr("Error: Directory not found: ");
            tty_putstr(dirname);
            tty_putstr("\n");
            return -1;
        }
        
        // Check if it's actually a directory
        if (!(entry.attributes & FAT_ATTR_DIRECTORY)) {
            tty_putstr("Error: Not a directory: ");
            tty_putstr(dirname);
            tty_putstr("\n");
            return -1;
        }
        
        target_cluster = ((uint32_t)entry.first_cluster_high << 16) | entry.first_cluster_low;
    }
    
    // List the target directory
    return fat32_list_directory(target_cluster);
}

// Get current directory cluster
uint32_t fat32_get_current_directory(void) {
    return current_directory_cluster;
}

// Get current directory path
void fat32_get_current_path(char* path, int max_len) {
    // Copy the full path
    int i = 0;
    while (i < max_len - 1 && current_full_path[i] != '\0') {
        path[i] = current_full_path[i];
        i++;
    }
    path[i] = '\0';
}

// Copy a file from source to destination
int fat32_copy_file(const char* source, const char* dest) {
    if (!fat32_initialized) {
        tty_putstr("Error: FAT32 not initialized\n");
        return -1;
    }
    
    // Parse source path
    uint32_t source_dir_cluster;
    char source_filename[32];
    if (fat32_parse_path(source, &source_dir_cluster, source_filename) != 0) {
        tty_putstr("Error: Invalid source path: ");
        tty_putstr(source);
        tty_putstr("\n");
        return -1;
    }
    
    // Check if source file exists
    fat32_dir_entry_t source_entry;
    if (fat32_find_file(source_filename, source_dir_cluster, &source_entry) != 0) {
        tty_putstr("Error: Source file not found: ");
        tty_putstr(source);
        tty_putstr("\n");
        return -1;
    }
    
    // Check if source is a directory
    if (source_entry.attributes & FAT_ATTR_DIRECTORY) {
        tty_putstr("Error: Cannot copy directories: ");
        tty_putstr(source);
        tty_putstr("\n");
        return -1;
    }
    
    // Parse destination
    uint32_t target_dir_cluster;
    char target_filename[32];
    
    // Handle special case: "." means current directory with original filename
    if (dest[0] == '.' && dest[1] == '\0') {
        target_dir_cluster = current_directory_cluster;
        int i = 0;
        while (source_filename[i] && i < 31) {
            target_filename[i] = source_filename[i];
            i++;
        }
        target_filename[i] = '\0';
    } else {
        // Check if destination is a directory or filename
        fat32_dir_entry_t dest_entry;
        if (fat32_find_file(dest, current_directory_cluster, &dest_entry) == 0 && 
            (dest_entry.attributes & FAT_ATTR_DIRECTORY)) {
            // Destination is an existing directory - copy into it with original filename
            target_dir_cluster = ((uint32_t)dest_entry.first_cluster_high << 16) | dest_entry.first_cluster_low;
            int i = 0;
            while (source_filename[i] && i < 31) {
                target_filename[i] = source_filename[i];
                i++;
            }
            target_filename[i] = '\0';
        } else {
            // Parse as path (could be new filename or path to directory)
            if (fat32_parse_path(dest, &target_dir_cluster, target_filename) != 0) {
                tty_putstr("Error: Invalid destination path: ");
                tty_putstr(dest);
                tty_putstr("\n");
                return -1;
            }
        }
    }
    
    // Read the source file data
    uint32_t file_size = source_entry.file_size;
    if (file_size > 32768) { // Limit to 32KB for safety
        tty_putstr("Error: File too large to copy (>32KB)\n");
        return -1;
    }
    
    // Allocate buffer for file data
    static uint8_t copy_buffer[32768];
    
    // Open and read source file
    fat32_file_t source_file;
    source_file.first_cluster = ((uint32_t)source_entry.first_cluster_high << 16) | source_entry.first_cluster_low;
    source_file.file_size = file_size;
    source_file.current_cluster = source_file.first_cluster;
    source_file.current_pos = 0;
    
    int bytes_read = fat32_read_file(&source_file, copy_buffer, file_size);
    if (bytes_read < 0) {
        tty_putstr("Error: Could not read source file\n");
        return -1;
    }
    
    // Save current directory and switch to target directory
    uint32_t saved_dir = current_directory_cluster;
    current_directory_cluster = target_dir_cluster;
    
    // Create file in target directory
    int result = fat32_create_file(target_filename, copy_buffer, bytes_read);
    
    // Restore current directory
    current_directory_cluster = saved_dir;
    
    if (result != 0) {
        tty_putstr("Error: Could not create destination file\n");
        return -1;
    }
    
    tty_putstr("File copied: ");
    tty_putstr(source);
    tty_putstr(" -> ");
    tty_putstr(dest);
    tty_putstr("\n");
    return 0;
}

// Move a file to a different directory
int fat32_move_file(const char* source, const char* dest_dir) {
    if (!fat32_initialized) {
        tty_putstr("Error: FAT32 not initialized\n");
        return -1;
    }
    
    // Parse source path
    uint32_t source_dir_cluster;
    char source_filename[32];
    if (fat32_parse_path(source, &source_dir_cluster, source_filename) != 0) {
        tty_putstr("Error: Invalid source path: ");
        tty_putstr(source);
        tty_putstr("\n");
        return -1;
    }
    
    // Find source file
    fat32_dir_entry_t source_entry;
    if (fat32_find_file(source_filename, source_dir_cluster, &source_entry) != 0) {
        tty_putstr("Error: Source file not found: ");
        tty_putstr(source);
        tty_putstr("\n");
        return -1;
    }
    
    // Check if source is a file (not directory)
    if (source_entry.attributes & FAT_ATTR_DIRECTORY) {
        tty_putstr("Error: Cannot move directories\n");
        return -1;
    }
    
    // Handle destination
    uint32_t dest_cluster;
    
    if (dest_dir[0] == '.' && dest_dir[1] == '\0') {
        // Move to current directory
        dest_cluster = current_directory_cluster;
    } else {
        // Check if destination directory exists
        fat32_dir_entry_t dest_entry;
        if (fat32_find_file(dest_dir, current_directory_cluster, &dest_entry) != 0) {
            tty_putstr("Error: Destination directory not found: ");
            tty_putstr(dest_dir);
            tty_putstr("\n");
            return -1;
        }
        
        // Check if destination is actually a directory
        if (!(dest_entry.attributes & FAT_ATTR_DIRECTORY)) {
            tty_putstr("Error: Destination is not a directory: ");
            tty_putstr(dest_dir);
            tty_putstr("\n");
            return -1;
        }
        
        dest_cluster = ((uint32_t)dest_entry.first_cluster_high << 16) | dest_entry.first_cluster_low;
    }
    
    // Read source file data
    uint32_t file_size = source_entry.file_size;
    if (file_size > 32768) {
        tty_putstr("Error: File too large to move (>32KB)\n");
        return -1;
    }
    
    static uint8_t move_buffer[32768];
    
    // Read source file
    fat32_file_t source_file;
    source_file.first_cluster = ((uint32_t)source_entry.first_cluster_high << 16) | source_entry.first_cluster_low;
    source_file.file_size = file_size;
    source_file.current_cluster = source_file.first_cluster;
    source_file.current_pos = 0;
    
    int bytes_read = fat32_read_file(&source_file, move_buffer, file_size);
    if (bytes_read < 0) {
        tty_putstr("Error: Could not read source file\n");
        return -1;
    }
    
    // Save current directory and switch to destination directory
    uint32_t saved_dir = current_directory_cluster;
    current_directory_cluster = dest_cluster;
    
    // Create file in destination directory with same filename
    int result = fat32_create_file(source_filename, move_buffer, bytes_read);
    
    // Restore current directory
    current_directory_cluster = saved_dir;
    
    if (result != 0) {
        tty_putstr("Error: Could not create file in destination directory\n");
        return -1;
    }
    
    // Switch back to source directory to delete file
    current_directory_cluster = source_dir_cluster;
    
    // Delete source file
    if (fat32_delete_file(source_filename) != 0) {
        tty_putstr("Warning: File copied but could not delete source\n");
        // Restore original directory
        current_directory_cluster = saved_dir;
        return -1;
    }
    
    // Restore original directory
    current_directory_cluster = saved_dir;
    
    tty_putstr("File moved: ");
    tty_putstr(source);
    tty_putstr(" -> ");
    tty_putstr(dest_dir);
    tty_putstr("/");
    tty_putstr(source_filename);
    tty_putstr("\n");
    return 0;
}

// Rename a file in the same directory
int fat32_rename_file(const char* old_name, const char* new_name) {
    if (!fat32_initialized) {
        tty_putstr("Error: FAT32 not initialized\n");
        return -1;
    }
    
    // Check if source file exists
    fat32_dir_entry_t entry;
    if (fat32_find_file(old_name, current_directory_cluster, &entry) != 0) {
        tty_putstr("Error: File not found: ");
        tty_putstr(old_name);
        tty_putstr("\n");
        return -1;
    }
    
    // Check if destination name already exists
    fat32_dir_entry_t dest_entry;
    if (fat32_find_file(new_name, current_directory_cluster, &dest_entry) == 0) {
        tty_putstr("Error: File already exists: ");
        tty_putstr(new_name);
        tty_putstr("\n");
        return -1;
    }
    
    // Find and update the directory entry
    uint32_t lba = fat32_cluster_to_lba(current_directory_cluster);
    uint8_t sector_buffer[512];
    char fat32_old_name[11];
    fat32_parse_filename(old_name, fat32_old_name);
    
    for (uint32_t sector = 0; sector < boot_sector.sectors_per_cluster; sector++) {
        if (ata_read_sectors(lba + sector, 1, (uint16_t*)sector_buffer) != 0) {
            return -1;
        }
        
        fat32_dir_entry_t* entries = (fat32_dir_entry_t*)sector_buffer;
        
        for (int i = 0; i < 512 / sizeof(fat32_dir_entry_t); i++) {
            if (entries[i].name[0] == 0x00) break;
            if (entries[i].name[0] == 0xE5) continue;
            
            if (fat32_compare_names((char*)entries[i].name, fat32_old_name)) {
                // Found the entry - update the name
                fat32_parse_filename(new_name, (char*)entries[i].name);
                
                // Write back
                if (ata_write_sectors(lba + sector, 1, (uint16_t*)sector_buffer) != 0) {
                    tty_putstr("Error: Could not update directory entry\n");
                    return -1;
                }
                
                tty_putstr("File renamed: ");
                tty_putstr(old_name);
                tty_putstr(" -> ");
                tty_putstr(new_name);
                tty_putstr("\n");
                return 0;
            }
        }
    }
    
    tty_putstr("Error: Could not find directory entry to rename\n");
    return -1;
}

// Parse a path like "folder/file" and return the directory cluster and filename
int fat32_parse_path(const char* path, uint32_t* dir_cluster, char* filename) {
    // Start from current directory
    *dir_cluster = current_directory_cluster;
    
    // Handle absolute paths (starting with /)
    if (path[0] == '/') {
        *dir_cluster = boot_sector.root_cluster;
        path++; // Skip the leading /
    }
    
    // Handle simple filename (no path separators)
    int has_slash = 0;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/') {
            has_slash = 1;
            break;
        }
    }
    
    if (!has_slash) {
        // Simple filename - copy and return current directory
        int i = 0;
        while (path[i] && i < 31) {
            filename[i] = path[i];
            i++;
        }
        filename[i] = '\0';
        return 0;
    }
    
    // Parse path with directories
    char current_dir[32];
    int path_pos = 0;
    int dir_pos = 0;
    
    while (path[path_pos]) {
        if (path[path_pos] == '/') {
            // End of directory name
            current_dir[dir_pos] = '\0';
            
            if (dir_pos > 0) {
                // Navigate to this directory
                fat32_dir_entry_t entry;
                if (fat32_find_file(current_dir, *dir_cluster, &entry) != 0) {
                    return -1; // Directory not found
                }
                
                if (!(entry.attributes & FAT_ATTR_DIRECTORY)) {
                    return -1; // Not a directory
                }
                
                *dir_cluster = ((uint32_t)entry.first_cluster_high << 16) | entry.first_cluster_low;
            }
            
            dir_pos = 0;
            path_pos++;
        } else {
            // Add character to current directory name
            if (dir_pos < 31) {
                current_dir[dir_pos++] = path[path_pos];
            }
            path_pos++;
        }
    }
    
    // The remaining part is the filename
    current_dir[dir_pos] = '\0';
    int i = 0;
    while (current_dir[i] && i < 31) {
        filename[i] = current_dir[i];
        i++;
    }
    filename[i] = '\0';
    
    return 0;
}

// Move a directory to another location
int fat32_move_directory(const char* source, const char* dest) {
    if (!fat32_initialized) {
        tty_putstr("Error: FAT32 not initialized\n");
        return -1;
    }
    
    // Parse source path
    uint32_t source_parent_cluster;
    char source_dirname[32];
    if (fat32_parse_path(source, &source_parent_cluster, source_dirname) != 0) {
        tty_putstr("Error: Invalid source path\n");
        return -1;
    }
    
    // Find source directory
    fat32_dir_entry_t source_entry;
    if (fat32_find_file(source_dirname, source_parent_cluster, &source_entry) != 0) {
        tty_putstr("Error: Source directory not found: ");
        tty_putstr(source);
        tty_putstr("\n");
        return -1;
    }
    
    // Verify it's actually a directory
    if (!(source_entry.attributes & FAT_ATTR_DIRECTORY)) {
        tty_putstr("Error: Source is not a directory: ");
        tty_putstr(source);
        tty_putstr("\n");
        return -1;
    }
    
    // Parse destination path
    uint32_t dest_parent_cluster;
    char dest_name[32];
    
    // Check if destination ends with / or is a directory
    int dest_len = 0;
    while (dest[dest_len]) dest_len++;
    
    if (dest_len > 0 && dest[dest_len - 1] == '/') {
        // Destination is a directory - use original directory name
        if (fat32_parse_path(dest, &dest_parent_cluster, dest_name) != 0) {
            tty_putstr("Error: Invalid destination path\n");
            return -1;
        }
        // Use source directory name
        int i = 0;
        while (source_dirname[i] && i < 31) {
            dest_name[i] = source_dirname[i];
            i++;
        }
        dest_name[i] = '\0';
    } else {
        // Check if destination is existing directory
        uint32_t temp_cluster;
        char temp_name[32];
        if (fat32_parse_path(dest, &temp_cluster, temp_name) == 0) {
            fat32_dir_entry_t temp_entry;
            if (fat32_find_file(temp_name, temp_cluster, &temp_entry) == 0 && 
                (temp_entry.attributes & FAT_ATTR_DIRECTORY)) {
                // Destination is existing directory
                dest_parent_cluster = ((uint32_t)temp_entry.first_cluster_high << 16) | temp_entry.first_cluster_low;
                int i = 0;
                while (source_dirname[i] && i < 31) {
                    dest_name[i] = source_dirname[i];
                    i++;
                }
                dest_name[i] = '\0';
            } else {
                // Destination is new name
                dest_parent_cluster = temp_cluster;
                int i = 0;
                while (temp_name[i] && i < 31) {
                    dest_name[i] = temp_name[i];
                    i++;
                }
                dest_name[i] = '\0';
            }
        } else {
            tty_putstr("Error: Invalid destination path\n");
            return -1;
        }
    }
    
    // Check if destination already exists
    fat32_dir_entry_t dest_check;
    if (fat32_find_file(dest_name, dest_parent_cluster, &dest_check) == 0) {
        tty_putstr("Error: Destination already exists: ");
        tty_putstr(dest_name);
        tty_putstr("\n");
        return -1;
    }
    
    // Remove from source parent directory
    uint32_t source_lba = fat32_cluster_to_lba(source_parent_cluster);
    uint8_t sector_buffer[512];
    char fat32_source_name[11];
    fat32_parse_filename(source_dirname, fat32_source_name);
    
    // Find and remove source entry
    for (uint32_t sector = 0; sector < boot_sector.sectors_per_cluster; sector++) {
        if (ata_read_sectors(source_lba + sector, 1, (uint16_t*)sector_buffer) != 0) {
            return -1;
        }
        
        fat32_dir_entry_t* entries = (fat32_dir_entry_t*)sector_buffer;
        
        for (int i = 0; i < 512 / sizeof(fat32_dir_entry_t); i++) {
            if (entries[i].name[0] == 0x00) break;
            if (entries[i].name[0] == 0xE5) continue;
            
            if (fat32_compare_names((char*)entries[i].name, fat32_source_name)) {
                // Mark as deleted
                entries[i].name[0] = 0xE5;
                
                // Write back
                if (ata_write_sectors(source_lba + sector, 1, (uint16_t*)sector_buffer) != 0) {
                    return -1;
                }
                goto source_removed;
            }
        }
    }
    
    tty_putstr("Error: Could not remove source directory entry\n");
    return -1;
    
source_removed:
    // Add to destination parent directory
    fat32_dir_entry_t new_entry = source_entry;
    fat32_parse_filename(dest_name, (char*)new_entry.name);
    
    if (fat32_add_dir_entry(dest_parent_cluster, &new_entry) != 0) {
        tty_putstr("Error: Could not add to destination directory\n");
        return -1;
    }
    
    // Update the ".." entry in the moved directory to point to new parent
    uint32_t dir_cluster = ((uint32_t)source_entry.first_cluster_high << 16) | source_entry.first_cluster_low;
    uint32_t dir_lba = fat32_cluster_to_lba(dir_cluster);
    
    if (ata_read_sectors(dir_lba, 1, (uint16_t*)sector_buffer) != 0) {
        tty_putstr("Warning: Could not update parent reference\n");
    } else {
        fat32_dir_entry_t* dir_entries = (fat32_dir_entry_t*)sector_buffer;
        
        // Find ".." entry (should be second entry)
        if (dir_entries[1].name[0] == '.' && dir_entries[1].name[1] == '.') {
            dir_entries[1].first_cluster_high = (dest_parent_cluster >> 16) & 0xFFFF;
            dir_entries[1].first_cluster_low = dest_parent_cluster & 0xFFFF;
            
            if (ata_write_sectors(dir_lba, 1, (uint16_t*)sector_buffer) != 0) {
                tty_putstr("Warning: Could not update parent reference\n");
            }
        }
    }
    
    tty_putstr("Directory moved: ");
    tty_putstr(source);
    tty_putstr(" -> ");
    tty_putstr(dest);
    tty_putstr("\n");
    return 0;
}
