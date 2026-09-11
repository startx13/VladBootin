/*
 * rsa_pkcs1.h
 *
 * Verifica di firme RSA-2048 con padding PKCS#1 v1.5 e digest
 * SHA-256, per uso in secure boot bare-metal.
 *
 * Flusso tipico d'uso nel bootloader:
 *
 *   1. rsa_pubkey_t pubkey = { .n = <chiave pubblica hardcoded>, .e = 65537 };
 *   2. sha256(kernel_image, kernel_len, digest);
 *   3. rsa_verify_result_t res = rsa_pkcs1_v15_verify_sha256(
 *          &pubkey, signature_bytes, digest);
 *   4. if (res != RSA_VERIFY_OK) { blocca il boot }
 */

#ifndef RSA_PKCS1_H
#define RSA_PKCS1_H

#include <stdint.h>
#include "bignum2048.h"

/* Lunghezza di un digest SHA-256 in byte. Definita qui invece che
 * inclusa da un header sha256.h esterno, cosi' questo modulo non
 * dipende dalla tua particolare implementazione SHA-256: gli passi
 * semplicemente 32 byte gia' calcolati con la funzione che usi tu. */
#define RSA_PKCS1_SHA256_DIGEST_SIZE 32

typedef struct {
    bignum2048_t n;   /* modulo, 2048 bit */
    uint32_t     e;   /* esponente pubblico, tipicamente 65537 */
} rsa_pubkey_t;

typedef enum {
    RSA_VERIFY_OK = 0,
    RSA_VERIFY_BAD_KEY,         /* modulo non valido (pari, zero, ecc.) */
    RSA_VERIFY_BAD_PADDING,     /* padding PKCS#1 non conforme */
    RSA_VERIFY_BAD_DIGEST_INFO, /* DigestInfo ASN.1 non corrisponde a SHA-256 */
    RSA_VERIFY_MISMATCH,        /* padding ok ma hash non corrisponde */
} rsa_verify_result_t;

/* Verifica una firma RSA-2048 PKCS#1 v1.5 su un digest SHA-256 gia'
 * calcolato. signature deve essere esattamente BN_BYTES (256) byte,
 * big-endian, tipicamente letta cosi' com'e' da dove il bootloader
 * la trova (es. appesa in coda al kernel, o in un header separato).
 * digest deve essere esattamente 32 byte (SHA256_DIGEST_SIZE).
 *
 * Ritorna RSA_VERIFY_OK se e solo se la firma e' valida per quel
 * digest sotto quella chiave pubblica. Qualunque altro valore vuol
 * dire "non fidarti", senza distinzioni: il chiamante nel bootloader
 * deve trattare tutti gli errori allo stesso modo (blocca il boot),
 * i codici sono solo per diagnostica/log, non per decidere un
 * comportamento diverso caso per caso.
 */
rsa_verify_result_t rsa_pkcs1_v15_verify_sha256(
    const rsa_pubkey_t *pubkey,
    const uint8_t signature[BN_BYTES],
    const uint8_t digest[RSA_PKCS1_SHA256_DIGEST_SIZE]);

#endif /* RSA_PKCS1_H */
