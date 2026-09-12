MFLAGS = -mcpu=cortex-a7 -fpic -ffreestanding -std=gnu99

OBJ := build

SOURCES := $(shell find core driver fs lib -type f -name '*.c')

OBJECTS := $(patsubst %.c,$(OBJ)/%.o,$(SOURCES))

# Quiet build by default
Q := @

# Verbose build:
# make V=1
ifeq ($(V),1)
	Q :=
endif

# Salta il self-check crittografico a runtime (utile solo per
# make run: QEMU esegue build/vladBootin.elf direttamente, non
# vladBootin.img firmato, quindi il check fallirebbe sempre li').
# make SKIP_SELFCHECK=1
ifeq ($(SKIP_SELFCHECK),1)
	MFLAGS += -DSKIP_SELFCHECK
endif


.PHONY: all startfile link clean run


all: startfile $(OBJECTS) link


startfile:
	@mkdir -p build

	@printf "  [AS]      %s\n" "start.S"
	$(Q)arm-none-eabi-gcc $(MFLAGS) -c start.S -o build/start.o

	@printf "  [CC]      %s\n" "main.c"
	$(Q)arm-none-eabi-gcc $(MFLAGS) -c main.c -o build/main.o -O2


# SHA256 compilata con ottimizzazione, senza modificare
# l'ottimizzazione del resto del progetto.
$(OBJ)/lib/crypto/sha256.o: lib/crypto/sha256.c
	@mkdir -p $(dir $@)

	@printf "  [CC]      %s\n" "$<"
	$(Q)arm-none-eabi-gcc $(MFLAGS) -O2 -c $< -o $@


$(OBJ)/%.o: %.c
	@mkdir -p $(dir $@)

	@printf "  [CC]      %s\n" "$<"
	$(Q)arm-none-eabi-gcc $(MFLAGS) -c $< -o $@


link:
	@printf "  [LD]      %s\n" "build/vladBootin.elf"
	$(Q)arm-none-eabi-gcc $(MFLAGS) \
		-T linker.ld \
		-o build/vladBootin.elf \
		-nostdlib \
		-ffreestanding \
		-O2 \
		build/start.o \
		build/main.o \
		$(OBJECTS) \
		-lgcc

	@printf "  [OBJCOPY] %s\n" "vladBootin.img"
	$(Q)arm-none-eabi-objcopy build/vladBootin.elf -O binary vladBootin.img

	@printf "  [SIGN]    %s\n" "vladBootin.img"
	$(Q)$(MAKE) -C Image_Loader sign_bootloader


clean:
	@printf "  [CLEAN]\n"
	$(Q)rm -rf build
	$(Q)rm -f vladBootin.img


run: all
	@printf "  [QEMU]    %s\n" "Raspberry Pi 2"
	$(Q)qemu-system-arm \
		-M raspi2b \
		-cpu cortex-a7 \
		-m 1024M \
		-kernel build/vladBootin.elf \
		-sd Image_Loader/2026-06-18-raspios-trixie-armhf-lite.img \
		-dtb Image_Loader/bcm2709-rpi-2-b.dtb \
		-serial pty \
		-display gtk \
		-gdb tcp::9000