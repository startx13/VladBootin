# VladBootin

**"Simple" bootloader for Raspberry Pi 2.**

VladBootin is a bare-metal ARM bootloader written in C and ARM assembly, originally developed as a hobby project and later revived and expanded.

It is designed primarily for the **Raspberry Pi 2 Model B** and can also be tested under **QEMU**.

The project started from ideas and code from [raspbootin](https://github.com/mrvn/raspbootin) and the [raspi3-tutorial](https://github.com/bztsrc/raspi3-tutorial) projects.

> Yes, it is called "simple".
>
> It has FAT, FDT, RSA, SHA-256, framebuffer support, memory management, SMP startup code and a boot protocol.
>
> The quotes are important.

---

## Features

VladBootin currently provides:

* ARM32 bare-metal execution
* Raspberry Pi 2 / Cortex-A7 support
* QEMU `raspi2b` support
* Linux ARM kernel booting
* Device Tree Blob (DTB) loading and modification
* Serial kernel loading
* SD card / FAT filesystem support
* Interactive serial console
* Framebuffer support
* GPIO support
* Raspberry Pi mailbox interface
* Basic memory allocator
* SHA-256 implementation
* RSA PKCS#1 v1.5 signature verification
* Bootloader self-integrity verification
* ARM exception and interrupt handlers
* Kernel image validation [WIP]
* Kernel and DTB hashing
* Boot argument modification

---

## Architecture

The project is divided into several logical components:

```text
VladBootin
├── core/
│   ├── boot_mode/
│   │   ├── serial_boot.c
│   │   └── ...
│   ├── mm.c
│   ├── signature_check.c
│   └── ...
│
├── driver/
│   ├── uart/
│   ├── gpio/
│   ├── mailbox/
│   ├── framebuffer/
│   └── ...
│
├── fs/
│   └── fat/
│
├── lib/
│   ├── crypto/
│   │   ├── sha256.c
│   │   └── rsa_pkcs1.c
│   ├── fdt/
│   └── ...
│
├── Image_Loader/
│
├── start.S
├── main.c
├── linker.ld
└── Makefile
```

The boot process starts in `start.S`, which provides the ARM exception vector table, CPU initialization and stack setup before transferring control to the C runtime.

The main application is implemented in `main.c`, while hardware access, filesystem support, cryptography and boot modes are separated into dedicated modules.

---

## Boot flow

The general boot sequence is:

```text
CPU reset
   │
   ▼
_start
   │
   ├── Save Raspberry Pi firmware boot arguments
   ├── Identify CPU/core
   ├── Install exception vectors
   ├── Initialize stacks
   ├── Clear BSS
   │
   ▼
vladBootin_main()
   │
   ├── UART initialization
   ├── Bootloader integrity check
   ├── Hardware initialization
   └── Interactive boot menu
           │
           ├── Serial boot
           │
           └── SD/FAT boot
                    │
                    ▼
              Load Linux kernel
                    │
                    ▼
                 Load DTB
                    │
                    ▼
             Update bootargs
                    │
                    ▼
             Update memory map
                    │
                    ▼
               Prepare CPU
                    │
                    ▼
                Linux ARM
```

---

## Serial boot

The serial boot mode allows a host computer to transfer a Linux kernel and DTB to the Raspberry Pi.

The protocol uses a small handshake and acknowledgement mechanism:

```text
Bootloader                         Host
    │                                │
    │<──────── SYN ──────────────────│
    │──── ACK ──────────────────────>│
    │<──── Kernel size ──────────────│
    │<──── Kernel data ──────────────│
    │──── ACK ──────────────────────>│
    │<──── DTB size ────────────────│
    │<──── DTB data ────────────────│
    │──── ACK ──────────────────────>│
    │                                │
    ▼                                │
  Validate                           │
    │                                │
    ▼                                │
   Boot                              │
```

Kernel data is transferred in chunks and checked against the available memory range.

The DTB is also checked for the expected Device Tree magic value before it is used.

---

## Linux boot

Before jumping into the Linux ARM entry point, VladBootin:

1. Updates the kernel command line.
2. Updates the memory description in the DTB.
3. Performs the required CPU/cache/MMU preparation.
4. Disables interrupts.
5. Transfers control to the kernel entry point.

The Linux ARM boot convention is used when entering the kernel.

Conceptually:

```text
r0 = 0
r1 = machine type / architecture value
r2 = DTB address
```

The exact low-level transition is implemented in `start.S`.

---

## Device Tree

VladBootin modifies the loaded DTB before handing it to Linux.

Currently this includes modifying:

* kernel boot arguments
* physical memory description

For example, serial boot mode configures boot arguments similar to:

```text
root=/dev/mmcblk0p2 rw rootwait console=ttyS1,115200
```

The DTB is kept in memory independently from the kernel image and its address is passed to Linux during the final handoff.

---

## FAT filesystem

The bootloader contains FAT filesystem support, allowing files to be accessed directly from an SD card.

The interactive console exposes commands for inspecting the filesystem and reading files.

Examples include:

```text
ls
cat <file>
```

This allows VladBootin to operate without requiring every boot image to be transferred over the serial link.

---

## Interactive console

VladBootin provides an interactive UART console for development and debugging.

Available commands include functionality for:

* displaying help
* inspecting memory
* dumping memory
* initializing the SD card
* browsing FAT filesystems
* reading files
* testing memory allocation
* testing cryptographic functions
* framebuffer operations
* booting kernels
* debugging hardware initialization

The exact command set may change while development continues.

---

## Memory management

A small bare-metal allocator is provided for dynamically allocated bootloader data.

Allocations are:

* aligned
* zero-initialized
* checked against the configured memory/MMIO boundary

The memory manager also contains ARM data-abort diagnostics, including fault status and fault address reporting.

This is primarily intended for debugging bare-metal failures rather than replacing a full operating-system memory subsystem.

---

## Cryptography and integrity checking

VladBootin includes:

* SHA-256
* RSA public-key verification
* PKCS#1 v1.5 signature verification

The bootloader can verify a signature over its own executable image during startup.

The integrity check hashes the region between the linker symbols:

```text
__text_start
        │
        ▼
   code + rodata
        │
        ▼
__rodata_end
```

The resulting SHA-256 digest is verified against the embedded RSA signature and public key.

A successful check results in:

```text
[SELFCHECK] Signature OK
```

A failed check stops execution.

### QEMU

QEMU normally executes the ELF image directly, while the physical boot image can be signed during the build process.

For this reason, a dedicated development build option exists:

```text
SKIP_SELFCHECK=1
```

This bypasses the runtime integrity check for QEMU development builds.

**This is not a security feature and must not be considered a secure-boot root of trust.**

---

## ARM startup code

`start.S` contains the low-level CPU startup code.

It provides:

* ARM exception vectors
* reset entry point
* CPU/core identification
* stack initialization
* BSS initialization
* CPU mode configuration
* cache/MMU preparation
* Linux kernel handoff

The bootloader is linked to run from the Raspberry Pi bootloader load address used by the project.

---

## Building

### Requirements

A suitable ARM cross-compilation toolchain is required.

For example:

```text
arm-none-eabi-gcc
arm-none-eabi-ld
arm-none-eabi-objcopy
```

GNU Make is also required.

Build with:

```bash
make
```

The build produces the bootloader image and performs the additional image/signing steps defined by the Makefile.

---

## Running with QEMU

VladBootin can be tested using QEMU's Raspberry Pi 2 machine model.

The project Makefile provides a run target:

```bash
make run
```

The QEMU configuration uses:

```text
Machine:   raspi2b
CPU:       Cortex-A7
Memory:    1024 MB
Serial:    PTY
Debugger:  TCP port 9000
Display:   GTK
```

QEMU development builds may be configured with:

```bash
make SKIP_SELFCHECK=1
```

depending on the current build configuration.

---

## Hardware

The primary target is:

**Raspberry Pi 2 Model B**

The project currently targets the ARM Cortex-A7 architecture used by the Raspberry Pi 2.

The bootloader is intended to remain close to the hardware rather than depending on an operating system or firmware abstraction layer beyond the Raspberry Pi boot environment.

---

## Project status

VladBootin is an **experimental hobby bootloader**.

It is functional enough to:

* initialize the Raspberry Pi environment
* provide an interactive console
* access hardware peripherals
* load kernel images
* load and modify DTBs
* perform integrity checks
* attempt Linux ARM handoff

However, the project is still under active development and some hardware/QEMU paths may require additional work.

Expect rough edges. This is a bootloader, after all.

---

## Known limitations

Current development areas include:

* improving Raspberry Pi hardware compatibility
* improving Linux kernel handoff reliability
* expanding filesystem support
* improving error handling
* expanding documentation
* improving portability
* testing additional Raspberry Pi models
* refining the cryptographic/integrity model

The Raspberry Pi 2 is the primary supported platform.

---

## Development philosophy

VladBootin is intentionally implemented close to the hardware.

The project avoids depending on:

* Linux
* libc
* a full runtime environment
* an operating-system kernel
* high-level bootloader frameworks

Instead, it implements the pieces required by the project directly:

```text
CPU
 │
 ├── exceptions
 ├── caches/MMU
 ├── memory
 └── SMP
      │
      ▼
 Raspberry Pi hardware
      │
      ├── UART
      ├── GPIO
      ├── mailbox
      ├── framebuffer
      └── SD
             │
             ▼
          FAT / files
             │
             ▼
          Linux kernel
```

The goal is not to compete with mature bootloaders.

The goal is to understand what happens between:

```text
RESET
  ↓
bare metal
  ↓
bootloader
  ↓
Linux
```

---

## Credits

VladBootin was originally inspired by and based on ideas from:

* `raspbootin`
* `raspi3-tutorial`

See the original projects for their respective licenses and source code.

---

## License

VladBootin is released under the **GNU General Public License v3.0**.

See [`LICENSE`](LICENSE) for the complete license text.
