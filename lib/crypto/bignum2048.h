/*
 * bignum2048.h
 *
 * Aritmetica bignum a precisione fissa per RSA-2048, bare-metal.
 * Nessuna dipendenza da libc (no malloc, no stdio). Pensato per
 * essere compilato con arm-none-eabi-gcc -ffreestanding -nostdlib.
 *
 * Rappresentazione: array di 64 uint32_t, little-endian per "limb"
 * (limb[0] = word meno significativa), 2048 bit totali.
 *
 * NOTA DI SICUREZZA:
 * Questa libreria fa solo VERIFICA con chiave pubblica (nessun
 * segreto coinvolto nel modexp), quindi non e' stata scritta per
 * essere constant-time nella moltiplicazione modulare: sarebbe
 * codice molto piu' complesso per un guadagno di sicurezza che, in
 * questo scenario (nessuna chiave privata manipolata a runtime),
 * e' marginale. Il confronto finale dell'hash invece E' in tempo
 * costante (vedi rsa_pkcs1.c), perche' li' un timing side-channel
 * permetterebbe forgery byte-per-byte del padding.
 */

#ifndef BIGNUM2048_H
#define BIGNUM2048_H

#include <stdint.h>
#include <stddef.h>

#define BN_LIMBS      64          /* 64 * 32 = 2048 bit */
#define BN_BITS       2048
#define BN_BYTES      256

typedef struct {
    uint32_t limb[BN_LIMBS];      /* limb[0] = meno significativo */
} bignum2048_t;

/* ---- Inizializzazione / conversione ---- */

/* Azzera un bignum */
void bn_zero(bignum2048_t *r);

/* Carica un bignum da un buffer big-endian (come arrivano RSA
 * modulus/signature standard, es. da un header o da flash). len
 * deve essere <= BN_BYTES; i byte mancanti in testa sono zero. */
void bn_from_be_bytes(bignum2048_t *r, const uint8_t *buf, size_t len);

/* Scrive un bignum in un buffer big-endian di esattamente BN_BYTES
 * byte (zero-padded a sinistra). buf deve avere spazio per
 * BN_BYTES byte. */
void bn_to_be_bytes(uint8_t *buf, const bignum2048_t *a);

/* ---- Confronti ---- */

/* Ritorna <0 se a<b, 0 se a==b, >0 se a>b. Non e' constant-time
 * (usato solo internamente per normalizzazione, non su segreti). */
int bn_cmp(const bignum2048_t *a, const bignum2048_t *b);

/* Ritorna 1 se a==0, altrimenti 0 */
int bn_is_zero(const bignum2048_t *a);

/* ---- Operazioni aritmetiche ---- */

/* r = a + b (mod 2^2048). Ritorna il carry out (0 o 1). */
uint32_t bn_add(bignum2048_t *r, const bignum2048_t *a, const bignum2048_t *b);

/* r = a - b (mod 2^2048), assumendo a>=b. Ritorna il borrow (0 o 1);
 * se b>a il risultato e' comunque "corretto mod 2^2048" ma il
 * chiamante deve gestire il borrow se gli serve un sottrazione
 * "vera". */
uint32_t bn_sub(bignum2048_t *r, const bignum2048_t *a, const bignum2048_t *b);

/* r = (a - b) se a>=b, altrimenti r invariato; ritorna 1 se ha
 * sottratto, 0 altrimenti. Utile per la riduzione condizionale
 * tipica di Montgomery. */
int bn_sub_if_ge(bignum2048_t *r, const bignum2048_t *b);

/* Shift a destra di 1 bit, ritorna il bit uscito (0 o 1) */
uint32_t bn_shr1(bignum2048_t *r, const bignum2048_t *a);

/* Ritorna 1 se il bit i-esimo (0 = LSB) di a e' settato */
int bn_test_bit(const bignum2048_t *a, unsigned int i);

#endif /* BIGNUM2048_H */
