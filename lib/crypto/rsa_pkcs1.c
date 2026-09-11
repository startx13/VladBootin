/*
 * rsa_pkcs1.c
 *
 * m = s^e mod n, poi verifica che m sia formattato come:
 *
 *   0x00 0x01 [0xFF * padlen] 0x00 [DigestInfo SHA-256] [digest a 32 byte]
 *
 * dove DigestInfo per SHA-256 e' la sequenza ASN.1 DER fissa:
 *   30 31 30 0d 06 09 60 86 48 01 65 03 04 02 01 05 00 04 20
 * (definita in RFC 8017 / RFC 3447, Appendice B.1 di PKCS#1, e' la
 * codifica standard dell'OID SHA-256).
 *
 * Il messaggio EM (Encoded Message) ha sempre BN_BYTES = 256 byte
 * totali per una chiave RSA-2048.
 */

#include "rsa_pkcs1.h"
#include "bn_montgomery.h"

/* DigestInfo ASN.1 DER per SHA-256, fisso per lo standard PKCS#1.
 * Lunghezza: 19 byte. */
static const uint8_t SHA256_DIGESTINFO_PREFIX[19] = {
    0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
    0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20
};

#define DIGESTINFO_PREFIX_LEN 19

/* Confronto in tempo costante: non deve dipendere dal punto in cui
 * i due buffer differiscono, altrimenti un attaccante che puo'
 * osservare i tempi di verifica (es. tramite un side-channel fisico
 * durante il boot) potrebbe forgiare una firma byte-per-byte.
 * Ritorna 1 se uguali, 0 se diversi. */
static int constant_time_eq(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= a[i] ^ b[i];
    }
    /* diff e' 0 se e solo se tutti i byte erano uguali. La riga
     * seguente e' l'unico punto con un confronto "diff == 0"; e'
     * comunque a tempo costante rispetto a QUALE byte differiva,
     * che e' la proprieta' che ci interessa (un attaccante non deve
     * poter dedurre la posizione del primo mismatch). */
    return diff == 0;
}

rsa_verify_result_t rsa_pkcs1_v15_verify_sha256(
    const rsa_pubkey_t *pubkey,
    const uint8_t signature[BN_BYTES],
    const uint8_t digest[RSA_PKCS1_SHA256_DIGEST_SIZE])
{
    bn_mont_ctx_t ctx;
    if (bn_mont_init(&ctx, &pubkey->n) != 0) {
        return RSA_VERIFY_BAD_KEY;
    }

    bignum2048_t s, m;
    bn_from_be_bytes(&s, signature, BN_BYTES);

    /* Se la firma non e' ridotta modulo n (s >= n), non e' un
     * ciphertext/signature RSA valido: rifiuta subito invece di
     * lasciare che modexp produca un risultato comunque "corretto
     * matematicamente" ma non significativo. */
    if (bn_cmp(&s, &pubkey->n) >= 0) {
        return RSA_VERIFY_BAD_PADDING;
    }

    bn_mont_modexp_u32(&m, &s, pubkey->e, &ctx);

    uint8_t em[BN_BYTES];
    bn_to_be_bytes(em, &m);

    /* --- Verifica padding PKCS#1 v1.5 ---
     * EM = 00 || 01 || PS || 00 || T
     * dove PS e' una stringa di 0xFF di lunghezza tale che l'EM
     * totale sia esattamente BN_BYTES byte, e T e' DigestInfo||digest.
     *
     * Lunghezza minima di PS per essere uno schema valido: 8 byte
     * (requisito RFC 8017 sez 9.2), qui verificata implicitamente
     * dal fatto che T ha lunghezza fissa nota (DIGESTINFO_PREFIX_LEN
     * + 32 = 51 byte) e BN_BYTES=256, quindi PS e' sempre ben oltre
     * 8 byte se il formato e' altrimenti corretto; controlliamo
     * comunque esplicitamente per robustezza / chiarezza.
     */

    if (em[0] != 0x00 || em[1] != 0x01) {
        return RSA_VERIFY_BAD_PADDING;
    }

    /* Cerca il terminatore 0x00 dopo la stringa di 0xFF, scandendo
     * SEMPRE l'intero buffer (niente 'break' anticipato): questo
     * evita che il tempo di esecuzione riveli quanto padding fosse
     * "quasi corretto" prima di un eventuale byte malformato. */
    size_t ps_end = 0;      /* indice del primo 0x00 dopo il padding, se trovato */
    int found_terminator = 0;
    int padding_all_ff = 1;

    for (size_t i = 2; i < BN_BYTES; i++) {
        uint8_t byte = em[i];
        int is_ff = (byte == 0xFF);
        int is_zero = (byte == 0x00);

        if (!found_terminator) {
            if (is_zero) {
                found_terminator = 1;
                ps_end = i;
            } else if (!is_ff) {
                padding_all_ff = 0;
            }
        }
        /* Nota: i byte dopo aver trovato il terminatore non sono
         * ulteriormente validati qui in questo loop (lo fa il
         * blocco DigestInfo sotto); il loop continua comunque fino
         * in fondo per mantenere costante il numero di iterazioni. */
    }

    if (!found_terminator || !padding_all_ff) {
        return RSA_VERIFY_BAD_PADDING;
    }

    size_t ps_len = ps_end - 2; /* byte di 0xFF tra "00 01" e "00" */
    if (ps_len < 8) {
        return RSA_VERIFY_BAD_PADDING; /* PS troppo corto, non conforme RFC 8017 */
    }

    size_t t_start = ps_end + 1;
    size_t t_len = BN_BYTES - t_start;
    if (t_len != DIGESTINFO_PREFIX_LEN + RSA_PKCS1_SHA256_DIGEST_SIZE) {
        return RSA_VERIFY_BAD_DIGEST_INFO;
    }

    if (!constant_time_eq(&em[t_start], SHA256_DIGESTINFO_PREFIX, DIGESTINFO_PREFIX_LEN)) {
        return RSA_VERIFY_BAD_DIGEST_INFO;
    }

    const uint8_t *embedded_digest = &em[t_start + DIGESTINFO_PREFIX_LEN];
    if (!constant_time_eq(embedded_digest, digest, RSA_PKCS1_SHA256_DIGEST_SIZE)) {
        return RSA_VERIFY_MISMATCH;
    }

    return RSA_VERIFY_OK;
}
