#!/usr/bin/env python3
"""
sign_bootloader.py

Firma il bootloader vladBootin: estrae [__text_start, __rodata_end)
dall'immagine binaria gia' linkata, la firma con la chiave privata
RSA-2048 (SHA-256 + PKCS#1 v1.5, via openssl) e patcha i 256 byte
di firma dentro vladBootin.img alla posizione di __sig_start.

Va lanciato DOPO objcopy, sull'img finale, usando i simboli
dell'ELF per calcolare gli offset nel file binario grezzo.

Uso (lanciato da root/Image_Loader/, vedi target 'sign_bootloader'
nel Makefile di questa cartella):
    python3 sign_bootloader.py <elf> <img> <chiave_privata.pem>

Esempio:
    python3 sign_bootloader.py ../build/vladBootin.elf ../vladBootin.img signing_key.pem
"""
import subprocess
import sys
import os


LOAD_ADDR = 0x00008000  # deve combaciare con ". = 0x00008000;" in linker.ld
SIG_SIZE = 256           # RSA-2048 -> firma di 256 byte


def get_symbol_offset(elf_path, symbol):
    out = subprocess.check_output(["arm-none-eabi-nm", elf_path]).decode()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == symbol:
            return int(parts[0], 16)
    raise RuntimeError(f"simbolo {symbol} non trovato in {elf_path} "
                        f"(controlla che il linker script lo esporti)")


def main():
    if len(sys.argv) != 4:
        print(f"Uso: {sys.argv[0]} <elf> <img> <chiave_privata.pem>")
        return 1

    elf_path, img_path, privkey_path = sys.argv[1], sys.argv[2], sys.argv[3]

    if not os.path.exists(privkey_path):
        print(f"ERRORE: chiave privata non trovata: {privkey_path}")
        return 1

    text_start = get_symbol_offset(elf_path, "__text_start")
    rodata_end = get_symbol_offset(elf_path, "__rodata_end")
    sig_start  = get_symbol_offset(elf_path, "__sig_start")
    sig_end    = get_symbol_offset(elf_path, "__sig_end")

    if sig_end - sig_start != SIG_SIZE:
        raise RuntimeError(
            f"sezione .sig lunga {sig_end - sig_start} byte, attesi "
            f"{SIG_SIZE}. Controlla che 'boot_signature[256]' sia "
            f"definito con __attribute__((section(\".sig\"))) e che "
            f"il linker non l'abbia scartato."
        )

    with open(img_path, "rb") as f:
        data = bytearray(f.read())

    text_off = text_start - LOAD_ADDR
    rodata_end_off = rodata_end - LOAD_ADDR
    sig_off = sig_start - LOAD_ADDR

    if rodata_end_off > len(data) or sig_off + SIG_SIZE > len(data):
        raise RuntimeError(
            f"offset calcolati fuori dai limiti del file ({len(data)} byte). "
            f"text_off=0x{text_off:x} rodata_end_off=0x{rodata_end_off:x} "
            f"sig_off=0x{sig_off:x}"
        )

    region = bytes(data[text_off:rodata_end_off])
    print(f"Regione da firmare: [0x{text_start:x}, 0x{rodata_end:x}) "
          f"= {len(region)} byte (file offset 0x{text_off:x}-0x{rodata_end_off:x})")

    region_file = "/tmp/vladbootin_region.bin"
    sig_file = "/tmp/vladbootin_sig.bin"

    with open(region_file, "wb") as f:
        f.write(region)

    # openssl calcola SHA-256 e firma con padding PKCS#1 v1.5 in un
    # solo passo, garantendo che il DigestInfo ASN.1 prodotto
    # combaci esattamente con quello hardcoded in rsa_pkcs1.c
    subprocess.check_call([
        "openssl", "dgst", "-sha256",
        "-sign", privkey_path,
        "-out", sig_file,
        region_file,
    ])

    with open(sig_file, "rb") as f:
        signature = f.read()

    if len(signature) != SIG_SIZE:
        raise RuntimeError(
            f"firma lunga {len(signature)} byte, attesi {SIG_SIZE} "
            f"(la chiave privata e' davvero RSA-2048?)"
        )

    data[sig_off:sig_off + SIG_SIZE] = signature

    with open(img_path, "wb") as f:
        f.write(data)

    os.remove(region_file)
    os.remove(sig_file)

    print(f"Firma scritta in {img_path} a offset file 0x{sig_off:x} "
          f"({SIG_SIZE} byte)")
    return 0


if __name__ == "__main__":
    sys.exit(main())