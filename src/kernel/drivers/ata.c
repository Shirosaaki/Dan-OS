//
// Created by Shirosaaki on 02/10/2025.
//

#include "ata.h"
#include "tty.h"
#include "../../cpu/ports.h"

static uint16_t ata_base = ATA_PRIMARY_IO;
static uint16_t ata_ctrl = ATA_PRIMARY_CONTROL;

// Wait for ATA device to be ready
static void ata_wait_ready(void) {
    while (inb(ata_base + ATA_REG_STATUS) & ATA_STATUS_BSY);
}

// Wait for data to be ready
static void ata_wait_drq(void) {
    while (!(inb(ata_base + ATA_REG_STATUS) & ATA_STATUS_DRQ));
}

// Initialize ATA driver
void ata_init(void) {    
    // Select master drive
    outb(ata_base + ATA_REG_DEVICE, ATA_MASTER);
    
    // Wait for drive to be ready
    ata_wait_ready();
}

// Identify ATA device
void ata_identify(void) {
    tty_putstr("Identifying ATA device...\n");
    
    // Select master drive
    outb(ata_base + ATA_REG_DEVICE, ATA_MASTER);
    ata_wait_ready();
    
    // Send IDENTIFY command
    outb(ata_base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_wait_ready();
    
    // Check if drive exists
    uint8_t status = inb(ata_base + ATA_REG_STATUS);
    if (status == 0) {
        tty_putstr("No ATA device found.\n");
        return;
    }
    
    ata_wait_drq();
    
    // Read identification data
    uint16_t identify[256];
    for (int i = 0; i < 256; i++) {
        identify[i] = inw(ata_base + ATA_REG_DATA);
    }
    
    tty_putstr("ATA device identified successfully.\n");
}

// Read sectors from disk
int ata_read_sectors(uint32_t lba, uint8_t sector_count, uint16_t* buffer) {
    if (sector_count == 0) {
        return -1;
    }
    
    // Wait for drive to be ready
    ata_wait_ready();
    
    // Select master drive and set LBA mode
    outb(ata_base + ATA_REG_DEVICE, (ATA_MASTER | 0x40) | ((lba >> 24) & 0x0F));
    
    // Send sector count and LBA
    outb(ata_base + ATA_REG_SECCOUNT, sector_count);
    outb(ata_base + ATA_REG_LBA_LOW, (uint8_t)(lba & 0xFF));
    outb(ata_base + ATA_REG_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ata_base + ATA_REG_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
    
    // Send READ command
    outb(ata_base + ATA_REG_COMMAND, ATA_CMD_READ_PIO);
    
    // Read sectors
    for (int s = 0; s < sector_count; s++) {
        ata_wait_drq();
        
        // Check for errors
        uint8_t status = inb(ata_base + ATA_REG_STATUS);
        if (status & ATA_STATUS_ERR) {
            return -1;
        }
        
        // Read 256 words (512 bytes)
        for (int i = 0; i < 256; i++) {
            buffer[s * 256 + i] = inw(ata_base + ATA_REG_DATA);
        }
    }
    
    return 0;
}

// Write sectors to disk
int ata_write_sectors(uint32_t lba, uint8_t sector_count, uint16_t* buffer) {
    if (sector_count == 0) {
        return -1;
    }
    
    // Wait for drive to be ready
    ata_wait_ready();
    
    // Select master drive and set LBA mode
    outb(ata_base + ATA_REG_DEVICE, (ATA_MASTER | 0x40) | ((lba >> 24) & 0x0F));
    
    // Send sector count and LBA
    outb(ata_base + ATA_REG_SECCOUNT, sector_count);
    outb(ata_base + ATA_REG_LBA_LOW, (uint8_t)(lba & 0xFF));
    outb(ata_base + ATA_REG_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ata_base + ATA_REG_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
    
    // Send WRITE command
    outb(ata_base + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);
    
    // Write sectors
    for (int s = 0; s < sector_count; s++) {
        ata_wait_drq();
        
        // Check for errors
        uint8_t status = inb(ata_base + ATA_REG_STATUS);
        if (status & ATA_STATUS_ERR) {
            return -1;
        }
        
        // Write 256 words (512 bytes)
        for (int i = 0; i < 256; i++) {
            outw(ata_base + ATA_REG_DATA, buffer[s * 256 + i]);
        }
    }
    
    // Flush cache
    outb(ata_base + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
    ata_wait_ready();
    
    return 0;
}
