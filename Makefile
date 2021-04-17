all:
	arm-none-eabi-gcc -mcpu=cortex-a7 -fpic -ffreestanding -c start.S -o build/start.o
	arm-none-eabi-gcc -mcpu=cortex-a7 -fpic -ffreestanding -std=gnu99 -c main.c -o build/main.o -O2 
	arm-none-eabi-gcc -mcpu=cortex-a7 -fpic -ffreestanding -std=gnu99 -c lib/uart.c -o build/uart.o -O2 
	arm-none-eabi-gcc -mcpu=cortex-a7 -fpic -ffreestanding -std=gnu99 -c lib/printf.c -o build/printf.o -O2 
	arm-none-eabi-gcc -mcpu=cortex-a7 -fpic -ffreestanding -std=gnu99 -c lib/sd.c -o build/sd.o -O2 
	arm-none-eabi-gcc -mcpu=cortex-a7 -fpic -ffreestanding -std=gnu99 -c lib/delays.c -o build/delays.o -O2 
	arm-none-eabi-gcc -mcpu=cortex-a7 -fpic -ffreestanding -std=gnu99 -c lib/fat.c -o build/fat.o -O2 
	arm-none-eabi-gcc -mcpu=cortex-a7 -fpic -ffreestanding -std=gnu99 -c lib/stdlib.c -o build/stdlib.o -O2 
	arm-none-eabi-gcc -mcpu=cortex-a7 -fpic -ffreestanding -std=gnu99 -c lib/mm.c -o build/mm.o -O2 
	arm-none-eabi-gcc -mcpu=cortex-a7 -T linker.ld -o build/vladBootin.elf -ffreestanding -O2 -nostdlib build/*.o -lgcc
	arm-none-eabi-objcopy build/vladBootin.elf -O binary vladBootin.img
clean:
	rm build/*
	rm vladBootin.img