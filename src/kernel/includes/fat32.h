//
// Created by Shirosaaki on 02/10/2025.
//

#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>

// FAT32 Boot Sector structure
typedef struct {
    uint8_t  jump_boot[3];
    uint8_t  oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    
    // FAT32 specific
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    uint8_t  volume_label[11];
    uint8_t  fs_type[8];
} __attribute__((packed)) fat32_boot_sector_t;

// FAT32 Directory Entry structure
typedef struct {
    uint8_t  name[11];           // 8.3 filename
    uint8_t  attributes;         // File attributes
    uint8_t  reserved;
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t first_cluster_high; // High 16 bits of cluster
    uint16_t modify_time;
    uint16_t modify_date;
    uint16_t first_cluster_low;  // Low 16 bits of cluster
    uint32_t file_size;
} __attribute__((packed)) fat32_dir_entry_t;

// File attributes
#define FAT_ATTR_READ_ONLY 0x01
#define FAT_ATTR_HIDDEN    0x02
#define FAT_ATTR_SYSTEM    0x04
#define FAT_ATTR_VOLUME_ID 0x08
#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_ARCHIVE   0x20
#define FAT_ATTR_LONG_NAME (FAT_ATTR_READ_ONLY | FAT_ATTR_HIDDEN | FAT_ATTR_SYSTEM | FAT_ATTR_VOLUME_ID)

// Special cluster values
#define FAT32_FREE_CLUSTER     0x00000000
#define FAT32_RESERVED_CLUSTER 0x0FFFFFF6
#define FAT32_BAD_CLUSTER      0x0FFFFFF7
#define FAT32_EOC              0x0FFFFFF8  // End of chain

// File handle structure
typedef struct {
    uint32_t first_cluster;
    uint32_t file_size;
    uint32_t current_pos;
    uint32_t current_cluster;
    uint8_t  attributes;
    char     filename[12];
} fat32_file_t;

// Function prototypes
int fat32_init(void);
int fat32_read_boot_sector(void);
int fat32_list_directory(uint32_t cluster);
int fat32_list_directory_ex(uint32_t cluster, int show_all);
int fat32_open_file(const char* filename, fat32_file_t* file);
int fat32_read_file(fat32_file_t* file, uint8_t* buffer, uint32_t size);
int fat32_find_file(const char* filename, uint32_t dir_cluster, fat32_dir_entry_t* entry);
uint32_t fat32_get_next_cluster(uint32_t cluster);
void fat32_print_file_info(fat32_dir_entry_t* entry, int show_hidden);

// Write operations
int fat32_create_file(const char* filename, const uint8_t* data, uint32_t size);
int fat32_write_file(fat32_file_t* file, const uint8_t* buffer, uint32_t size);
int fat32_delete_file(const char* filename);
int fat32_delete_all_files(void);
int fat32_update_file(const char* filename, const uint8_t* data, uint32_t new_size);
uint32_t fat32_allocate_cluster(void);
int fat32_set_next_cluster(uint32_t cluster, uint32_t value);
int fat32_add_dir_entry(uint32_t dir_cluster, fat32_dir_entry_t* entry);
int fat32_extend_cluster_chain(uint32_t last_cluster, uint32_t additional_clusters);
int fat32_free_cluster_chain(uint32_t start_cluster);
int fat32_update_dir_entry_size(const char* filename, uint32_t new_size);

// Directory operations  
int fat32_create_directory(const char* dirname);
int fat32_change_directory(const char* dirname);
int fat32_change_directory_path(const char* path);
int fat32_remove_directory(const char* dirname);
int fat32_remove_directory_recursive(const char* dirname);
int fat32_list_directory_by_name(const char* dirname);
uint32_t fat32_get_current_directory(void);
void fat32_get_current_path(char* path, int max_len);

// File operations
int fat32_copy_file(const char* source, const char* dest);
int fat32_move_file(const char* source, const char* dest);
int fat32_move_directory(const char* source, const char* dest);
int fat32_rename_file(const char* oldname, const char* newname);
int fat32_resolve_path(const char* path, uint32_t* result_cluster, char* result_filename);

// Helper functions
void fat32_parse_filename(const char* input, char* output);
int fat32_compare_names(const char* name1, const char* name2);
int fat32_parse_path(const char* path, uint32_t* dir_cluster, char* filename);
uint16_t fat32_get_current_date(void);
uint16_t fat32_get_current_time(void);

#endif // FAT32_H
