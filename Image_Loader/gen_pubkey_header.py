#!/usr/bin/env python3
"""
gen_pubkey_header.py

Genera un header C con il modulo RSA-2048 (array uint8_t) a partire
da una chiave pubblica PEM qualsiasi. Non fa parte della libreria:
e' solo tooling da lanciare a mano quando generi/ruoti la chiave di
firma del bootloader.

Uso:
    python3 gen_pubkey_header.py <chiave_pubblica.pem> [output.h] [nome_array]

Esempio:
    python3 gen_pubkey_header.py signing_pub.pem boot_pubkey.h BOOT_PUBKEY_N
"""
import subprocess
import sys


def pem_modulus(pubkey_path):
    out = subprocess.check_output(
        ["openssl", "rsa", "-pubin", "-in", pubkey_path, "-noout", "-modulus"]
    ).decode()
    hexmod = out.strip().split("=")[1]
    modulus = bytes.fromhex(hexmod)
    if len(modulus) == 257 and modulus[0] == 0:
        modulus = modulus[1:]
    if len(modulus) != 256:
        raise ValueError(
            f"modulus di {len(modulus)} byte, atteso 256 (chiave non RSA-2048?)"
        )
    return modulus


def c_array(name, data):
    lines = [f"static const uint8_t {name}[{len(data)}] = {{"]
    for i in range(0, len(data), 12):
        chunk = data[i:i + 12]
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    lines.append("};")
    return "\n".join(lines)


def main():
    if len(sys.argv) < 2:
        print(f"Uso: {sys.argv[0]} <chiave_pubblica.pem> [output.h] [nome_array]")
        return 1

    pubkey_path = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) > 2 else "boot_pubkey.h"
    array_name = sys.argv[3] if len(sys.argv) > 3 else "BOOT_PUBKEY_N"

    modulus = pem_modulus(pubkey_path)

    guard = output_path.upper().replace(".", "_").replace("/", "_").replace("-", "_")

    with open(output_path, "w") as f:
        f.write(f"/* Generato da {pubkey_path} con gen_pubkey_header.py. */\n")
        f.write(f"/* Modulo pubblico RSA-2048, 256 byte big-endian. */\n")
        f.write(f"#ifndef {guard}\n#define {guard}\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write(c_array(array_name, modulus) + "\n\n")
        f.write(f"#define {array_name}_EXPONENT 65537u\n\n")
        f.write(f"#endif /* {guard} */\n")

    print(f"Scritto {output_path}: {array_name}[{len(modulus)}]")
    return 0


if __name__ == "__main__":
    sys.exit(main())
