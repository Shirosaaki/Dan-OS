#include "fat32.h"
#include "ata.h"
#include "tty.h"
#include "string.h"

static fat32_boot_sector_t boot_sector;
static uint32_t fat_start_lba;
static uint32_t data_start_lba;
static uint32_t root_dir_cluster;
static uint32_t current_directory_cluster;
static int fat32_initialized = 0;

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

// Get current FAT32 date (simplified - just return a default)
uint16_t fat32_get_current_date(void) {
    // Format: (year-1980) << 9 | month << 5 | day
    // Default: 2025-10-02
    return ((2025 - 1980) << 9) | (10 << 5) | 2;
}

// Get current FAT32 time (simplified - just return a default)
uint16_t fat32_get_current_time(void) {
    // Format: hour << 11 | minute << 5 | (second / 2)
    // Default: 12:00:00
    return (12 << 11) | (0 << 5) | (0);
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
