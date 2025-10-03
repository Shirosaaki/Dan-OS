//
// Created by Shirosaaki on 02/10/2025.
//

#ifndef ATA_H
#define ATA_H

#include <stdint.h>

// ATA ports
#define ATA_PRIMARY_IO      0x1F0
#define ATA_PRIMARY_CONTROL 0x3F6
#define ATA_SECONDARY_IO    0x170
#define ATA_SECONDARY_CONTROL 0x376

// ATA registers offsets
#define ATA_REG_DATA       0
#define ATA_REG_ERROR      1
#define ATA_REG_FEATURES   1
#define ATA_REG_SECCOUNT   2
#define ATA_REG_LBA_LOW    3
#define ATA_REG_LBA_MID    4
#define ATA_REG_LBA_HIGH   5
#define ATA_REG_DEVICE     6
#define ATA_REG_STATUS     7
#define ATA_REG_COMMAND    7

// ATA status bits
#define ATA_STATUS_ERR  (1 << 0)  // Error
#define ATA_STATUS_DRQ  (1 << 3)  // Data request ready
#define ATA_STATUS_SRV  (1 << 4)  // Overlapped mode service request
#define ATA_STATUS_DF   (1 << 5)  // Drive fault error
#define ATA_STATUS_RDY  (1 << 6)  // Drive ready
#define ATA_STATUS_BSY  (1 << 7)  // Busy

// ATA commands
#define ATA_CMD_READ_PIO        0x20
#define ATA_CMD_READ_PIO_EXT    0x24
#define ATA_CMD_WRITE_PIO       0x30
#define ATA_CMD_WRITE_PIO_EXT   0x34
#define ATA_CMD_CACHE_FLUSH     0xE7
#define ATA_CMD_CACHE_FLUSH_EXT 0xEA
#define ATA_CMD_IDENTIFY        0xEC

// ATA device types
#define ATA_MASTER 0xA0
#define ATA_SLAVE  0xB0

// Sector size
#define ATA_SECTOR_SIZE 512

// Function prototypes
void ata_init(void);
int ata_read_sectors(uint32_t lba, uint8_t sector_count, uint16_t* buffer);
int ata_write_sectors(uint32_t lba, uint8_t sector_count, uint16_t* buffer);
void ata_identify(void);

#endif // ATA_H
