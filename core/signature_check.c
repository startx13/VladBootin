#include <stdint.h>
#include <stddef.h>
#include "signature_check.h"
#include "../lib/crypto/sha256.h"
#include "../lib/crypto/rsa_pkcs1.h"
#include "../lib/crypto/boot_pubkey.h"
#include "../lib/printf.h"

extern unsigned char __text_start;
extern unsigned char __rodata_end;
extern const uint8_t boot_signature[256];

int selfcheck_verify_signature(void)
{
#ifdef SKIP_SELFCHECK
    printf("\r\n[SELFCHECK] SKIPPED (build QEMU, non verificabile: exec da ELF non firmato)");
    return 1;
#else
    rsa_pubkey_t pubkey;
    bn_from_be_bytes(&pubkey.n, BOOT_PUBKEY_N, sizeof(BOOT_PUBKEY_N));
    pubkey.e = BOOT_PUBKEY_N_EXPONENT;

    uint8_t digest[32];
    size_t region_len = (size_t)(&__rodata_end - &__text_start);
    sha256(&__text_start, region_len, digest);

    rsa_verify_result_t res = rsa_pkcs1_v15_verify_sha256(&pubkey, boot_signature, digest);

    if (res == RSA_VERIFY_OK) {
        printf("\r\n[SELFCHECK] Signature OK");
        return 1;
    } else {
        printf("\r\n[SELFCHECK] Signature FAILED (code %d)", (int)res);
        return 0;
    }
#endif
}