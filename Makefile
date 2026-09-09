MFLAGS = -mcpu=cortex-a7 -fpic -ffreestanding -std=gnu99
LIB := lib
OBJ := build
SOURCES := $(wildcard $(LIB)/*.c)
OBJECTS := $(patsubst $(LIB)/%.c, $(OBJ)/%.o, $(SOURCES))

all: startfile $(OBJECTS) link

startfile:
	mkdir -p build
	arm-none-eabi-gcc $(MFLAGS) -c start.S -o build/start.o
	arm-none-eabi-gcc $(MFLAGS) -c main.c -o build/main.o -O2 

$(OBJ)/%.o: $(LIB)/%.c
	arm-none-eabi-gcc $(MFLAGS) -c $< -o $@

link:
	# Garantisce che start.o rimanga in cima all'output binario finalizzato
	arm-none-eabi-gcc $(MFLAGS) -T linker.ld -o build/vladBootin.elf -nostdlib -ffreestanding -O2 build/start.o build/main.o $(filter-out build/start.o build/main.o, $(wildcard build/*.o)) -lgcc
	arm-none-eabi-objcopy build/vladBootin.elf -O binary vladBootin.img

clean:
	rm -rf build
	rm vladBootin.img

run: all
	# Modificato per eseguire ed emulare direttamente il file .img
	qemu-system-arm -M raspi2b -cpu cortex-a7 -m 1024M \
		-kernel vladBootin.img \
		-sd Image_Loader/2026-06-18-raspios-trixie-armhf-lite.img \
		-dtb Image_Loader/bcm2709-rpi-2-b.dtb \
		-serial pty -display gtk -gdb tcp::9000
