# ==============================================================================
#  SOURCES & VARIABLES
# ==============================================================================

# --- Assembly Sources ---
ASM_SRCS        = $(wildcard src/bootloader/*.asm)
ASMS64_SRCS     = $(wildcard src/kernel/arch/x86_64/*.S)
SYSCALL_S_SRCS  = $(wildcard src/kernel/syscall/*.S)

# --- C Sources ---
KERNEL_SRCS     = $(wildcard src/kernel/*.c)
ARCH_SRCS       = $(wildcard src/kernel/arch/x86_64/*.c)
DRV_SRCS        = $(wildcard src/kernel/drivers/*.c)
FS_SRCS         = $(wildcard src/kernel/fs/*.c)
NET_SRCS        = $(wildcard src/kernel/net/*.c)
SYS_SRCS        = $(wildcard src/kernel/sys/*.c)
APPS_SRCS       = $(wildcard src/kernel/apps/*.c)
CPU_SRCS        = $(wildcard src/cpu/*.c)
PMM_SRCS        = $(wildcard src/kernel/pmm/*.c)
VMM_SRCS        = $(wildcard src/kernel/vmm/*.c)
STRING_SRCS     = $(wildcard src/kernel/string/*.c)
SYSCALL_SRCS    = $(wildcard src/kernel/syscall/*.c)
COMMANDS_SRCS   = $(wildcard src/kernel/commands/*.c)
TTY_SRCS        = $(wildcard src/kernel/tty/*.c)

# --- Object Generation Helper ---
# Maps src/path/file.ext -> obj/file.o
# Note: This assumes filenames are unique across folders. 
OBJ_PATH        = obj/%.o

ASM_OBJS        = $(patsubst src/bootloader/%.asm, $(OBJ_PATH), $(ASM_SRCS))
ASMS64_OBJS     = $(patsubst src/kernel/arch/x86_64/%.S, $(OBJ_PATH), $(ASMS64_SRCS))
SYSCALL_S_OBJS  = $(patsubst src/kernel/syscall/%.S, $(OBJ_PATH), $(SYSCALL_S_SRCS))

KERNEL_OBJS     = $(patsubst src/kernel/%.c, $(OBJ_PATH), $(KERNEL_SRCS))
ARCH_OBJS       = $(patsubst src/kernel/arch/x86_64/%.c, $(OBJ_PATH), $(ARCH_SRCS))
DRV_OBJS        = $(patsubst src/kernel/drivers/%.c, $(OBJ_PATH), $(DRV_SRCS))
FS_OBJS         = $(patsubst src/kernel/fs/%.c, $(OBJ_PATH), $(FS_SRCS))
NET_OBJS        = $(patsubst src/kernel/net/%.c, $(OBJ_PATH), $(NET_SRCS))
SYS_OBJS        = $(patsubst src/kernel/sys/%.c, $(OBJ_PATH), $(SYS_SRCS))
APPS_OBJS       = $(patsubst src/kernel/apps/%.c, $(OBJ_PATH), $(APPS_SRCS))
CPU_OBJS        = $(patsubst src/cpu/%.c, $(OBJ_PATH), $(CPU_SRCS))
PMM_OBJS        = $(patsubst src/kernel/pmm/%.c, $(OBJ_PATH), $(PMM_SRCS))
VMM_OBJS        = $(patsubst src/kernel/vmm/%.c, $(OBJ_PATH), $(VMM_SRCS))
STRING_OBJS     = $(patsubst src/kernel/string/%.c, $(OBJ_PATH), $(STRING_SRCS))
SYSCALL_OBJS    = $(patsubst src/kernel/syscall/%.c, $(OBJ_PATH), $(SYSCALL_SRCS))
COMMANDS_OBJS   = $(patsubst src/kernel/commands/%.c, $(OBJ_PATH), $(COMMANDS_SRCS))
TTY_OBJS        = $(patsubst src/kernel/tty/%.c, $(OBJ_PATH), $(TTY_SRCS))

# Aggregate all objects
OBJS =  $(ASM_OBJS) $(ASMS64_OBJS) $(SYSCALL_S_OBJS) \
        $(KERNEL_OBJS) $(ARCH_OBJS) $(DRV_OBJS) $(FS_OBJS) \
        $(NET_OBJS) $(SYS_OBJS) $(APPS_OBJS) $(CPU_OBJS) \
        $(PMM_OBJS) $(VMM_OBJS) $(STRING_OBJS) $(SYSCALL_OBJS) \
        $(COMMANDS_OBJS) $(TTY_OBJS)

# ==============================================================================
#  BUILD CONFIG
# ==============================================================================

NAME        = DanOs
BIN         = target/x86_64/iso/boot/kernel.bin
LINKER      = src/linker/linker.ld
ISO         = build/x86_64/$(NAME).iso
ISO_TARGET  = target/x86_64/iso
BUILD_DIR   = build/x86_64
DISK_IMG    = disk.img
INCLUDE     = include

MK          = mkdir -p
RM          = rm -rf
CC          = x86_64-elf-gcc
NASM        = nasm
LD          = x86_64-elf-ld
GRUB        = grub-mkrescue

# Flags
CFLAGS      = -ffreestanding -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -I $(INCLUDE)
NASMFLAGS   = -f elf64
LDFLAGS     = -n -o $(BIN) -T $(LINKER)

# ==============================================================================
#  RULES
# ==============================================================================

.PHONY: all build clean re run run-user run-no-disk run-docker disk disk-docker help

all: build

build: $(OBJS)
	@echo "[LINK] Creating kernel binary..."
	@$(MK) $(dir $(BIN))
	@$(MK) $(BUILD_DIR)
	@$(LD) $(LDFLAGS) $(OBJS)
	@echo "[GRUB] Creating ISO..."
	@$(GRUB) /usr/lib/grub/i386-pc -o $(ISO) $(ISO_TARGET)
	@echo "Build complete."

# --- Static Pattern Rules (The Fix) ---
# Syntax: $(TARGETS): target-pattern: prereq-pattern

$(ASM_OBJS): obj/%.o: src/bootloader/%.asm
	@$(MK) $(dir $@)
	$(NASM) $(NASMFLAGS) $< -o $@

$(ASMS64_OBJS): obj/%.o: src/kernel/arch/x86_64/%.S
	@$(MK) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(SYSCALL_S_OBJS): obj/%.o: src/kernel/syscall/%.S
	@$(MK) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_OBJS): obj/%.o: src/kernel/%.c
	@$(MK) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(ARCH_OBJS): obj/%.o: src/kernel/arch/x86_64/%.c
	@$(MK) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(DRV_OBJS): obj/%.o: src/kernel/drivers/%.c
	@$(MK) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(FS_OBJS): obj/%.o: src/kernel/fs/%.c
	@$(MK) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(NET_OBJS): obj/%.o: src/kernel/net/%.c
	@$(MK) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(SYS_OBJS): obj/%.o: src/kernel/sys/%.c
	@$(MK) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(APPS_OBJS): obj/%.o: src/kernel/apps/%.c
	@$(MK) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(CPU_OBJS): obj/%.o: src/cpu/%.c
	@$(MK) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(PMM_OBJS): obj/%.o: src/kernel/pmm/%.c
	@$(MK) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(VMM_OBJS): obj/%.o: src/kernel/vmm/%.c
	@$(MK) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(STRING_OBJS): obj/%.o: src/kernel/string/%.c
	@$(MK) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(SYSCALL_OBJS): obj/%.o: src/kernel/syscall/%.c
	@$(MK) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(COMMANDS_OBJS): obj/%.o: src/kernel/commands/%.c
	@$(MK) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(TTY_OBJS): obj/%.o: src/kernel/tty/%.c
	@$(MK) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# --- Utilities ---

clean:
	@$(RM) obj $(BUILD_DIR) $(BIN)
	@echo "Cleaned."

re: clean build

run:
	@qemu-system-x86_64 -cdrom $(ISO) -drive file=$(DISK_IMG),format=raw -boot d -serial stdio -display sdl -vga std -m 512 -netdev tap,id=net0,ifname=tap0,script=no,downscript=no -device e1000,netdev=net0

run-user:
	@qemu-system-x86_64 -cdrom $(ISO) -drive file=$(DISK_IMG),format=raw -boot d -serial stdio -display sdl -vga std -m 512 -netdev user,id=net0 -device e1000,netdev=net0

run-no-disk:
	@qemu-system-x86_64 -cdrom $(ISO)

run-docker:
	@docker exec -it $$(docker ps -q) bash -c "cd /root/env && qemu-system-x86_64 -cdrom $(ISO) -drive file=$(DISK_IMG),format=raw"

disk:
	@bash create_disk_mtools.sh

disk-docker:
	@docker exec -it $$(docker ps -q) bash -c "cd /root/env && bash create_disk_mtools.sh"

help:
	@echo "Usage: make [target]"
	@echo "Targets:"
	@echo "  build        : Build the kernel"
	@echo "  clean        : Clean the build"
	@echo "  re           : Clean and build the kernel"
	@echo "  run          : Run the kernel with disk (use this!)"
	@echo "  run-no-disk  : Run without disk"
	@echo "  run-docker   : Run in Docker (if permission issues)"
	@echo "  disk         : Create FAT32 disk image"
	@echo "  disk-docker  : Create disk in Docker"
	@echo "  help         : Show this message"
