ASM_SRCS 	=	$(wildcard src/bootloader/*.asm)
ASMS64_SRCS = $(wildcard src/kernel/*.S)

KERNEL_SRCS =	$(wildcard src/kernel/*.c)
TTY_SRCS 	=	$(wildcard src/kernel/tty/*.c)
STR_SRCS 	=	$(wildcard src/kernel/string/*.c)
DRV_SRCS 	=	$(wildcard src/kernel/drivers/*.c)
CPU_SRCS 	=	$(wildcard src/cpu/*.c)
COMMAND_SRCS =  $(wildcard src/kernel/commands/*.c)
PMM_SRCS 	=	$(wildcard src/kernel/pmm/*.c)
VMM_SRCS    =   $(wildcard src/kernel/vmm/*.c)
SYSCALL_SRCS=	$(wildcard src/kernel/syscall/*.c)
APPS_SRCS   =   $(wildcard src/kernel/apps/*.c)

ASM_PATH 		= 	src/bootloader/%.asm
KERNEL_PATH 	= 	src/kernel/%.c
TTY_PATH 		= 	src/kernel/tty/%.c
STR_PATH 		= 	src/kernel/string/%.c
DRV_PATH 		= 	src/kernel/drivers/%.c
CPU_PATH 		= 	src/cpu/%.c
COMMAND_PATH 	= 	src/kernel/commands/%.c
PMM_PATH 		= 	src/kernel/pmm/%.c
VMM_PATH 		= 	src/kernel/vmm/%.c
SYSCALL_PATH 	= 	src/kernel/syscall/%.c
APPS_PATH      =   src/kernel/apps/%.c
OBJ_PATH 		= 	obj/%.o

ASM_OBJS 		= 	$(patsubst $(ASM_PATH), $(OBJ_PATH), $(ASM_SRCS))
ASMS64_OBJS 	= 	$(patsubst src/kernel/%.S, $(OBJ_PATH), $(ASMS64_SRCS))
KERNEL_OBJS 	= 	$(patsubst $(KERNEL_PATH), $(OBJ_PATH), $(KERNEL_SRCS))
TTY_OBJS 		= 	$(patsubst $(TTY_PATH), $(OBJ_PATH), $(TTY_SRCS))
STR_OBJS 		= 	$(patsubst $(STR_PATH), $(OBJ_PATH), $(STR_SRCS))
DRV_OBJS 		= 	$(patsubst $(DRV_PATH), $(OBJ_PATH), $(DRV_SRCS))
CPU_OBJS 		= 	$(patsubst $(CPU_PATH), $(OBJ_PATH), $(CPU_SRCS))
COMMAND_OBJS 	= 	$(patsubst $(COMMAND_PATH), $(OBJ_PATH), $(COMMAND_SRCS))
PMM_OBJS 		= 	$(patsubst $(PMM_PATH), $(OBJ_PATH), $(PMM_SRCS))
VMM_OBJS 		= 	$(patsubst $(VMM_PATH), $(OBJ_PATH), $(VMM_SRCS))
SYSCALL_OBJS 	= 	$(patsubst $(SYSCALL_PATH), $(OBJ_PATH), $(SYSCALL_SRCS))
APPS_OBJS      =   $(patsubst $(APPS_PATH), $(OBJ_PATH), $(APPS_SRCS))

OBJS 		= 	$(ASM_OBJS) $(ASMS64_OBJS) $(KERNEL_OBJS) $(TTY_OBJS) $(STR_OBJS) $(DRV_OBJS) $(CPU_OBJS) $(COMMAND_OBJS) $(PMM_OBJS) $(VMM_OBJS) $(SYSCALL_OBJS) $(APPS_OBJS)

NAME 		= 	DanOs
BIN 		= 	target/x86_64/iso/boot/kernel.bin
LINKER 		= 	src/linker/linker.ld
ISO 		= 	build/x86_64/$(NAME).iso
ISO_TARGET 	= 	target/x86_64/iso
BUILD 		= 	build/x86_64
DISK_IMG 	= 	disk.img
TRASH 		= 	obj build $(BIN)
INCLUDE 	= 	src/kernel/includes

MK 			= 	mkdir -p
CC 			= 	x86_64-elf-gcc -ffreestanding -I $(INCLUDE)
NASM 		= 	nasm -f elf64
LD 			= 	x86_64-elf-ld -n -o $(BIN) -T $(LINKER) $(OBJS)
GRUB 		= 	grub-mkrescue /usr/lib/grub/i386-pc -o $(ISO) $(ISO_TARGET)
RM 			= 	rm -rf

.PHONY: all
all: build

$(STR_OBJS): $(OBJ_PATH): $(STR_PATH)
	@ $(MK) $(dir $@) && \
	$(CC) -c $(patsubst $(OBJ_PATH), $(STR_PATH), $@) -o $@

$(KERNEL_OBJS): $(OBJ_PATH): $(KERNEL_PATH)
	@ $(MK) $(dir $@) && \
	$(CC) -c $(patsubst $(OBJ_PATH), $(KERNEL_PATH), $@) -o $@

$(TTY_OBJS): $(OBJ_PATH): $(TTY_PATH)
	@ $(MK) $(dir $@) && \
	$(CC) -c $(patsubst $(OBJ_PATH), $(TTY_PATH), $@) -o $@

$(DRV_OBJS): $(OBJ_PATH): $(DRV_PATH)
	@ $(MK) $(dir $@) && \
	$(CC) -c $(patsubst $(OBJ_PATH), $(DRV_PATH), $@) -o $@

$(CPU_OBJS): $(OBJ_PATH): $(CPU_PATH)
	@ $(MK) $(dir $@) && \
	$(CC) -c $(patsubst $(OBJ_PATH), $(CPU_PATH), $@) -o $@

$(COMMAND_OBJS): $(OBJ_PATH): $(COMMAND_PATH)
	@ $(MK) $(dir $@) && \
	$(CC) -c $(patsubst $(OBJ_PATH), $(COMMAND_PATH), $@) -o $@

$(PMM_OBJS): $(OBJ_PATH): $(PMM_PATH)
	@ $(MK) $(dir $@) && \
	$(CC) -c $(patsubst $(OBJ_PATH), $(PMM_PATH), $@) -o $@

$(VMM_OBJS): $(OBJ_PATH): $(VMM_PATH)
	@ $(MK) $(dir $@) && \
	$(CC) -c $(patsubst $(OBJ_PATH), $(VMM_PATH), $@) -o $@

$(SYSCALL_OBJS): $(OBJ_PATH): $(SYSCALL_PATH)
	@ $(MK) $(dir $@) && \
	$(CC) -c $(patsubst $(OBJ_PATH), $(SYSCALL_PATH), $@) -o $@

$(APPS_OBJS): $(OBJ_PATH): $(APPS_PATH)
	@ $(MK) $(dir $@) && \
	$(CC) -c $(patsubst $(OBJ_PATH), $(APPS_PATH), $@) -o $@

$(ASM_OBJS): $(OBJ_PATH): $(ASM_PATH)
	@ $(MK) $(dir $@) && \
	$(NASM) $(patsubst $(OBJ_PATH), $(ASM_PATH), $@) -o $@

$(ASMS64_OBJS): $(OBJ_PATH): src/kernel/%.S
	@ $(MK) $(dir $@) && \
	$(CC) -c src/kernel/$*.S -o $@

build: $(OBJS)
	@ $(MK) $(BUILD) && \
	$(LD) && \
	$(GRUB)

.PHONY: clean
clean:
	@ $(RM) $(TRASH)

.PHONY: re
re: clean build

.PHONY: run
run:
	@ qemu-system-x86_64 -cdrom $(ISO) -drive file=$(DISK_IMG),format=raw -boot d -serial stdio -display sdl -vga std -m 512 -netdev tap,id=net0,ifname=tap0,script=no,downscript=no -device e1000,netdev=net0

.PHONY: run-user
run-user:
	@ qemu-system-x86_64 -cdrom $(ISO) -drive file=$(DISK_IMG),format=raw -boot d -serial stdio -display sdl -vga std -m 512 -netdev user,id=net0 -device e1000,netdev=net0

.PHONY: run-no-disk
run-no-disk:
	@ qemu-system-x86_64 -cdrom $(ISO)

.PHONY: run-docker
run-docker:
	@ docker exec -it $$(docker ps -q) bash -c "cd /root/env && qemu-system-x86_64 -cdrom $(ISO) -drive file=$(DISK_IMG),format=raw"

.PHONY: disk
disk:
	@ bash create_disk_mtools.sh

.PHONY: disk-docker
disk-docker:
	@ docker exec -it $$(docker ps -q) bash -c "cd /root/env && bash create_disk_mtools.sh"

.PHONY: help
help:
	@ echo "Usage: make [target]"
	@ echo "Targets:"
	@ echo "  build        : Build the kernel"
	@ echo "  clean        : Clean the build"
	@ echo "  re           : Clean and build the kernel"
	@ echo "  run          : Run the kernel with disk (use this!)"
	@ echo "  run-no-disk  : Run without disk"
	@ echo "  run-docker   : Run in Docker (if permission issues)"
	@ echo "  disk         : Create FAT32 disk image"
	@ echo "  disk-docker  : Create disk in Docker"
	@ echo "  help         : Show this message"

