ASM_SRCS 	=	$(wildcard src/bootloader/*.asm)

KERNEL_SRCS =	$(wildcard src/kernel/*.c)
TTY_SRCS 	=	$(wildcard src/kernel/tty/*.c)
STR_SRCS 	=	$(wildcard src/kernel/string/*.c)
CPU_SRCS 	=	$(wildcard src/cpu/*.c)

ASM_PATH 	= 	src/bootloader/%.asm
KERNEL_PATH = 	src/kernel/%.c
TTY_PATH 	= 	src/kernel/tty/%.c
STR_PATH 	= 	src/kernel/string/%.c
CPU_PATH 	= 	src/cpu/%.c
OBJ_PATH 	= 	obj/%.o

ASM_OBJS 	= 	$(patsubst $(ASM_PATH), $(OBJ_PATH), $(ASM_SRCS))
KERNEL_OBJS = 	$(patsubst $(KERNEL_PATH), $(OBJ_PATH), $(KERNEL_SRCS))
TTY_OBJS 	= 	$(patsubst $(TTY_PATH), $(OBJ_PATH), $(TTY_SRCS))
STR_OBJS 	= 	$(patsubst $(STR_PATH), $(OBJ_PATH), $(STR_SRCS))
CPU_OBJS 	= 	$(patsubst $(CPU_PATH), $(OBJ_PATH), $(CPU_SRCS))

OBJS 		= 	$(ASM_OBJS) $(KERNEL_OBJS) $(TTY_OBJS) $(STR_OBJS) $(CPU_OBJS)

NAME 		= 	DanOs
BIN 		= 	target/x86_64/iso/boot/kernel.bin
LINKER 		= 	src/linker/linker.ld
ISO 		= 	build/x86_64/$(NAME).iso
ISO_TARGET 	= 	target/x86_64/iso
BUILD 		= 	build/x86_64
TRASH 		= 	obj build $(BIN)
INCLUDE 	= 	src/kernel/includes

MK 			= 	mkdir -p
CC 			= 	x86_64-elf-gcc -ffreestanding -I $(INCLUDE)
NASM 		= 	nasm -f elf64
LD 			= 	x86_64-elf-ld -n -o $(BIN) -T $(LINKER) $(OBJS)
GRUB 		= 	grub-mkrescue /usr/lib/grub/i386-pc -o $(ISO) $(ISO_TARGET)
RM 			= 	rm -rf

$(STR_OBJS): $(OBJ_PATH): $(STR_PATH)
	@ $(MK) $(dir $@) && \
	$(CC) -c $(patsubst $(OBJ_PATH), $(STR_PATH), $@) -o $@

$(KERNEL_OBJS): $(OBJ_PATH): $(KERNEL_PATH)
	@ $(MK) $(dir $@) && \
	$(CC) -c $(patsubst $(OBJ_PATH), $(KERNEL_PATH), $@) -o $@

$(TTY_OBJS): $(OBJ_PATH): $(TTY_PATH)
	@ $(MK) $(dir $@) && \
	$(CC) -c $(patsubst $(OBJ_PATH), $(TTY_PATH), $@) -o $@

$(CPU_OBJS): $(OBJ_PATH): $(CPU_PATH)
	@ $(MK) $(dir $@) && \
	$(CC) -c $(patsubst $(OBJ_PATH), $(CPU_PATH), $@) -o $@

$(ASM_OBJS): $(OBJ_PATH): $(ASM_PATH)
	@ $(MK) $(dir $@) && \
	$(NASM) $(patsubst $(OBJ_PATH), $(ASM_PATH), $@) -o $@

.PHONY: build
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
	@ qemu-system-x86_64 -cdrom $(ISO)

.PHONY: help
help:
	@ echo "Usage: make [target]"
	@ echo "Targets:"
	@ echo "  build: Build the kernel"
	@ echo "  clean: Clean the build"
	@ echo "  re: Clean and build the kernel"
	@ echo "  run: Run the kernel"
	@ echo "  help: Show this message"

