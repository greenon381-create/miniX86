# ===== TOOLCHAIN =====
CC = gcc
ASM = nasm
LD = ld

CFLAGS = -m32 -ffreestanding -O0 -fno-pie -fno-stack-protector -fno-builtin
LDFLAGS = -m elf_i386

# ===== OUTPUT =====
KERNEL = kernel.elf
ISO = os.iso

# ===== SOURCES =====
C_SRC = kernel.c
ASM_SRC = kernel_entry.asm

C_OBJ = kernel.o
ASM_OBJ = kernel_entry.o

# ===== BUILD KERNEL =====
all: $(ISO)

$(ASM_OBJ):
	$(ASM) -f elf32 $(ASM_SRC) -o $(ASM_OBJ)

$(C_OBJ):
	$(CC) $(CFLAGS) -c $(C_SRC) -o $(C_OBJ)

$(KERNEL): $(ASM_OBJ) $(C_OBJ)
	$(LD) $(LDFLAGS) -T linker.ld -o $(KERNEL) $(ASM_OBJ) $(C_OBJ)

# ===== CREATE GRUB ISO =====
$(ISO): $(KERNEL)
	mkdir -p iso/boot/grub
	cp $(KERNEL) iso/boot/kernel.elf

	# GRUB config
	echo 'set timeout=0' > iso/boot/grub/grub.cfg
	echo 'set default=0' >> iso/boot/grub/grub.cfg
	echo '' >> iso/boot/grub/grub.cfg
	echo 'menuentry "miniX86" {' >> iso/boot/grub/grub.cfg
	echo '    multiboot /boot/kernel.elf' >> iso/boot/grub/grub.cfg
	echo '    boot' >> iso/boot/grub/grub.cfg
	echo '}' >> iso/boot/grub/grub.cfg

	grub-mkrescue -o $(ISO) iso

# ===== RUN =====
run: $(ISO)
	qemu-system-i386 -cdrom $(ISO)

# ===== CLEAN =====
clean:
	rm -rf *.o *.elf iso $(ISO)

flash: 
	sudo dd if=os.iso of=/dev/sdb bs=4M status=progress

finish:
	sync

flash2:
	sudo dd if=os.iso of=/dev/sdc bs=4M status=progress