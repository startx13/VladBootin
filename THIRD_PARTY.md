# Third-party components

VladBootin is released under the GNU General Public License v3.0. The repository also contains or incorporates third-party components that retain their original licensing terms.

## `bztsrc/raspi3-tutorial`

Several Raspberry Pi hardware-support components are derived from the `raspi3-tutorial` project by bzt, including code used for:

- FAT filesystem support
- SD card access
- GPIO
- Raspberry Pi mailbox
- delays
- framebuffer support
- `core/graphics/homer.h`

The original `raspi3-tutorial` project is released under the **MIT License**. The original copyright/license notices are retained in the relevant source files.

Upstream: https://github.com/bztsrc/raspi3-tutorial

## `mpaland/printf`

`lib/printf.c` and `lib/printf.h` are derived from the portable `printf` implementation by Marco Paland (`mpaland/printf`).

The component is released under the **MIT License**, and its original attribution/license notice is retained in the source files.

Upstream: https://github.com/mpaland/printf

## Raspberry Pi / Linux kernel test image

`Image_Loader/kernel.img` is a test kernel image obtained from the official Raspberry Pi firmware repository. It is not part of VladBootin's original codebase.

The kernel image is a build of Linux and is therefore distributed under the licensing terms applicable to Linux, primarily **GPL-2.0-only** (with the Linux syscall exception where applicable). The Linux/Raspberry Pi licensing information applies to the kernel image itself and is separate from VladBootin's GPLv3 license.

Upstream firmware repository: https://github.com/raspberrypi/firmware

## Original project attribution

VladBootin was originally inspired by and based on ideas from `raspbootin` and `raspi3-tutorial`. No third-party license is implied for `raspbootin` by this document; the attribution is retained as project history.

## Project license

All original VladBootin code is licensed under the **GNU General Public License v3.0**. See [`LICENSE`](LICENSE) for the complete license text.
