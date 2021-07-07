MFLAGS = -mcpu=cortex-a7 -fpic -ffreestanding -std=gnu99
LIB := lib
OBJ := build
SOURCES := $(wildcard $(LIB)/*.c)
OBJECTS := $(patsubst $(LIB)/%.c, $(OBJ)/%.o, $(SOURCES))

all: startfile $(OBJECTS) link



startfile:
	arm-none-eabi-gcc $(MFLAGS) -c start.S -o build/start.o
	arm-none-eabi-gcc $(MFLAGS) -c main.c -o build/main.o -O2 

$(OBJ)/%.o: $(LIB)/%.c
	arm-none-eabi-gcc $(MFLAGS) -c $< -o $@

link:
	arm-none-eabi-gcc $(MFLAGS) -T linker.ld -o build/vladBootin.elf -nostdlib -ffreestanding -O2 build/*.o -lgcc
	arm-none-eabi-objcopy build/vladBootin.elf -O binary vladBootin.img

clean:
	rm build/*
	rm vladBootin.img

run: all
	sudo qemu-system-arm -machine raspi2b -kernel build/vladBootin.elf -serial stdio -gdb tcp::9000

	
