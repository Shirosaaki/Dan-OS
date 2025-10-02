# FAT32 Filesystem Support for DanOS

## Overview
DanOS now includes full FAT32 filesystem support, allowing you to read files from a FAT32-formatted disk image.

## Components Added

### 1. **ATA/IDE Disk Driver** (`src/kernel/drivers/ata.c`)
- Low-level disk access using PIO mode
- Read/write sectors from IDE hard disk
- LBA (Logical Block Addressing) support

### 2. **FAT32 Filesystem Driver** (`src/kernel/drivers/fat32.c`)
- Parse FAT32 boot sector
- Navigate File Allocation Table
- Read directory entries
- Open and read files
- List directory contents

### 3. **New Shell Commands**
- `ls` - List files in current directory
- `cat <filename>` - Display file contents
- `disk` - Show disk information

## Creating the Disk Image

### Using mtools (Recommended - No sudo required)
```bash
./create_disk_mtools.sh
```

### Using mount (Requires sudo)
```bash
sudo ./create_disk.sh
```

### Manual Creation
```bash
# Create 50MB disk
dd if=/dev/zero of=build/disk.img bs=1M count=50

# Format as FAT32
mkfs.vfat -F 32 build/disk.img

# Add files using mtools
echo "Hello!" > test.txt
mcopy -i build/disk.img test.txt ::/
```

## Running with Disk

### Build and Run
```bash
make clean && make build
make disk    # Create disk if not exists
make run     # Run with disk attached
```

### Run without Disk
```bash
make run-no-disk
```

## Testing Commands

Once DanOS boots:

1. **List files**:
   ```
   > ls
   ```
   You should see: `HELLO.TXT`, `TEST.TXT`, `README.TXT`, `INFO.TXT`

2. **Read a file**:
   ```
   > cat HELLO.TXT
   ```
   Output: "Hello from DanOS!"

3. **Check disk**:
   ```
   > disk
   ```

## File Naming Conventions

FAT32 uses 8.3 filenames:
- 8 characters for name
- 3 characters for extension
- All uppercase

Examples:
- `HELLO.TXT` ✓
- `README.TXT` ✓
- `FILE.C` ✓

## Architecture

```
User Command (ls, cat) 
    ↓
Shell (commands.c)
    ↓
FAT32 Driver (fat32.c)
    ↓
ATA Driver (ata.c)
    ↓
Hardware (IDE Disk)
```

## Current Limitations

1. **Read-only**: No write support yet
2. **Root directory only**: No subdirectory navigation
3. **File size**: Limited to 512 bytes display for `cat`
4. **Long filenames**: LFN entries are skipped
5. **Single disk**: Only primary master IDE disk supported

## Future Enhancements

- [ ] Write support
- [ ] Directory navigation (cd command)
- [ ] Create files/directories
- [ ] Delete files
- [ ] Long filename support
- [ ] Multiple disk support
- [ ] Disk caching for better performance

## Troubleshooting

### "Error reading directory"
- Disk may not be properly attached to QEMU
- Check if `build/disk.img` exists
- Verify QEMU command includes `-hda build/disk.img`

### "File not found"
- Use uppercase 8.3 format: `HELLO.TXT` not `hello.txt`
- Run `ls` to see available files
- Ensure disk was created with test files

### Disk not detected
- ATA driver initializes on boot
- Check boot messages for "ATA driver initialized"
- FAT32 should show "Filesystem ready"

## Adding Your Own Files

### Method 1: mtools (Easy)
```bash
# Create file
echo "My content" > myfile.txt

# Copy to disk
mcopy -i build/disk.img myfile.txt ::/MYFILE.TXT

# Verify
mdir -i build/disk.img
```

### Method 2: Mount (Linux)
```bash
# Mount disk
sudo mount -o loop build/disk.img /mnt

# Add files
sudo cp yourfile.txt /mnt/

# Unmount
sudo umount /mnt
```

## Technical Details

### Boot Sector
- Located at LBA 0
- Contains: bytes per sector, sectors per cluster, FAT size, root cluster

### File Allocation Table
- Starts after reserved sectors
- Each entry is 32 bits
- 0x0FFFFFF8 marks end of chain

### Directory Entries
- 32 bytes per entry
- Contains: filename, attributes, first cluster, file size

### Cluster Addressing
```
LBA = data_start + ((cluster - 2) * sectors_per_cluster)
```

Enjoy your new filesystem! 🎉
