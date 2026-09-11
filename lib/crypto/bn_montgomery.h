/*
 * bn_montgomery.h
 *
 * Moltiplicazione modulare secondo Montgomery e modular
 * exponentiation per bignum a 2048 bit. Usata per calcolare
 * s^e mod n durante la verifica RSA.
 *
 * Perche' Montgomery: fare a*b mod n con n a 2048 bit tramite
 * divisione lunga classica e' costoso e complesso da implementare
 * correttamente senza bug. Montgomery riduce la riduzione modulare
 * a shift + add, molto piu' semplice ed efficiente su target senza
 * divisore hardware a 64 bit (come ARM11 su RPi2/BCM2836... in
 * realta' RPi2 ha Cortex-A7 con divisore, ma il principio vale lo
 * stesso e il codice resta piu' pulito).
 */

#ifndef BN_MONTGOMERY_H
#define BN_MONTGOMERY_H

#include "bignum2048.h"

/* Contesto Montgomery precalcolato per un dato modulo n (dispari,
 * come deve essere qualunque modulo RSA valido essendo prodotto di
 * due primi dispari). */
typedef struct {
    bignum2048_t n;        /* modulo */
    bignum2048_t r2_mod_n; /* (2^2048)^2 mod n, per convertire in forma Montgomery */
    uint32_t     n0_inv;   /* -n^-1 mod 2^32, usato nella riduzione */
} bn_mont_ctx_t;

/* Inizializza il contesto Montgomery per il modulo n.
 * Ritorna 0 se n e' valido (dispari, non zero), -1 altrimenti. */
int bn_mont_init(bn_mont_ctx_t *ctx, const bignum2048_t *n);

/* Modular exponentiation: r = base^exp mod ctx->n.
 * exp e' passato come intero a 32 bit perche' nel nostro caso d'uso
 * (verifica RSA) l'esponente pubblico e' quasi sempre 65537 (o
 * comunque piccolo); non serve supportare esponenti a 2048 bit qui.
 * Se in futuro serve un esponente bignum generico, si puo'
 * aggiungere una variante bn_mont_modexp_bn(). */
void bn_mont_modexp_u32(bignum2048_t *r, const bignum2048_t *base,
                         uint32_t exp, const bn_mont_ctx_t *ctx);

#endif /* BN_MONTGOMERY_H */
