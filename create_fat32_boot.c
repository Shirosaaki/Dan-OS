#include <stdio.h>
#include <stdint.h>

int main() {
    FILE *disk = fopen("build/disk.img", "wb");
    if (!disk) {
        printf("Cannot create disk file\n");
        return 1;
    }
    
    // Create a minimal FAT32 boot sector
    uint8_t boot_sector[512] = {0};
    
    // Jump instruction (3 bytes)
    boot_sector[0] = 0xEB;
    boot_sector[1] = 0x3C;
    boot_sector[2] = 0x90;
    
    // OEM identifier (8 bytes)
    const char *oem = "DANOS   ";
    for(int i = 0; i < 8; i++) {
        boot_sector[3 + i] = oem[i];
    }
    
    // Bytes per sector (512)
    boot_sector[11] = 0x00;
    boot_sector[12] = 0x02;
    
    // Sectors per cluster (1)
    boot_sector[13] = 0x01;
    
    // Reserved sectors (32 for FAT32)
    boot_sector[14] = 0x20;
    boot_sector[15] = 0x00;
    
    // Number of FATs (2)
    boot_sector[16] = 0x02;
    
    // Root directory entries (0 for FAT32)
    boot_sector[17] = 0x00;
    boot_sector[18] = 0x00;
    
    // Total sectors (small - 0 for FAT32)
    boot_sector[19] = 0x00;
    boot_sector[20] = 0x00;
    
    // Media descriptor (0xF8 = hard disk)
    boot_sector[21] = 0xF8;
    
    // Sectors per FAT (0 for FAT32)
    boot_sector[22] = 0x00;
    boot_sector[23] = 0x00;
    
    // Sectors per track
    boot_sector[24] = 0x3F;
    boot_sector[25] = 0x00;
    
    // Number of heads
    boot_sector[26] = 0xFF;
    boot_sector[27] = 0x00;
    
    // Hidden sectors
    boot_sector[28] = 0x00;
    boot_sector[29] = 0x00;
    boot_sector[30] = 0x00;
    boot_sector[31] = 0x00;
    
    // Total sectors (large) - 20480 sectors = 10MB
    boot_sector[32] = 0x00;
    boot_sector[33] = 0x50;
    boot_sector[34] = 0x00;
    boot_sector[35] = 0x00;
    
    // Sectors per FAT (for FAT32) - 160 sectors
    boot_sector[36] = 0xA0;
    boot_sector[37] = 0x00;
    boot_sector[38] = 0x00;
    boot_sector[39] = 0x00;
    
    // Extended flags
    boot_sector[40] = 0x00;
    boot_sector[41] = 0x00;
    
    // FAT32 version
    boot_sector[42] = 0x00;
    boot_sector[43] = 0x00;
    
    // Root cluster (usually 2)
    boot_sector[44] = 0x02;
    boot_sector[45] = 0x00;
    boot_sector[46] = 0x00;
    boot_sector[47] = 0x00;
    
    // FSInfo sector
    boot_sector[48] = 0x01;
    boot_sector[49] = 0x00;
    
    // Backup boot sector
    boot_sector[50] = 0x06;
    boot_sector[51] = 0x00;
    
    // Drive number
    boot_sector[64] = 0x80;
    
    // Boot signature (THIS IS THE KEY!)
    boot_sector[66] = 0x29;
    
    // Volume ID
    boot_sector[67] = 0x12;
    boot_sector[68] = 0x34;
    boot_sector[69] = 0x56;
    boot_sector[70] = 0x78;
    
    // Volume label (11 bytes)
    const char *label = "DANOS      ";
    for(int i = 0; i < 11; i++) {
        boot_sector[71 + i] = label[i];
    }
    
    // Filesystem type (8 bytes)
    const char *fs = "FAT32   ";
    for(int i = 0; i < 8; i++) {
        boot_sector[82 + i] = fs[i];
    }
    
    // Boot signature
    boot_sector[510] = 0x55;
    boot_sector[511] = 0xAA;
    
    fwrite(boot_sector, 1, 512, disk);
    
    // Fill rest with zeros to make it 10MB
    uint8_t zero_sector[512] = {0};
    for(int i = 1; i < 20480; i++) {
        fwrite(zero_sector, 1, 512, disk);
    }
    
    fclose(disk);
    printf("Created FAT32 disk image with proper boot signature\n");
    return 0;
}