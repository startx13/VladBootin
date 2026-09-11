#!/usr/bin/env python3
"""
gen_signature_header.py

Converte un file binario di firma RSA-2048 (256 byte, come prodotto
da 'openssl dgst -sha256 -sign key.pem -out msg.sig msg.bin') in un
header C con l'array della firma. Solo per test rapidi, non fa
parte della libreria.

Uso:
    python3 gen_signature_header.py <firma.sig> [output.h] [nome_array]

Esempio:
    python3 gen_signature_header.py msg.sig test_signature.h signature_bytes
"""
import sys


def c_array(name, data):
    lines = [f"static const uint8_t {name}[{len(data)}] = {{"]
    for i in range(0, len(data), 12):
        chunk = data[i:i + 12]
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    lines.append("};")
    return "\n".join(lines)


def main():
    if len(sys.argv) < 2:
        print(f"Uso: {sys.argv[0]} <firma.sig> [output.h] [nome_array]")
        return 1

    sig_path = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) > 2 else "test_signature.h"
    array_name = sys.argv[3] if len(sys.argv) > 3 else "signature_bytes"

    data = open(sig_path, "rb").read()
    if len(data) != 256:
        print(f"ATTENZIONE: {sig_path} e' lungo {len(data)} byte, atteso 256 "
              f"(firma RSA-2048). Controlla di aver firmato con la chiave giusta.")
        return 1

    guard = output_path.upper().replace(".", "_").replace("/", "_").replace("-", "_")

    with open(output_path, "w") as f:
        f.write(f"/* Generato da {sig_path} con gen_signature_header.py. */\n")
        f.write(f"#ifndef {guard}\n#define {guard}\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write(c_array(array_name, data) + "\n\n")
        f.write(f"#endif /* {guard} */\n")

    print(f"Scritto {output_path}: {array_name}[{len(data)}]")
    return 0


if __name__ == "__main__":
    sys.exit(main())
