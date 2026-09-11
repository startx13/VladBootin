/*
 * bignum2048.c
 *
 * Implementazione aritmetica bignum a 2048 bit, bare-metal.
 */

#include "bignum2048.h"

void bn_zero(bignum2048_t *r)
{
    for (int i = 0; i < BN_LIMBS; i++) {
        r->limb[i] = 0;
    }
}

void bn_from_be_bytes(bignum2048_t *r, const uint8_t *buf, size_t len)
{
    bn_zero(r);
    if (len > BN_BYTES) {
        /* Input troppo lungo: tronchiamo dai byte piu' significativi
         * (cioe' prendiamo solo gli ultimi BN_BYTES byte). Questo
         * non dovrebbe mai capitare con chiavi/firme RSA-2048 ben
         * formate: il chiamante deve validare len prima. */
        buf += (len - BN_BYTES);
        len = BN_BYTES;
    }

    /* buf e' big-endian: l'ultimo byte di buf e' il meno
     * significativo, va in limb[0] byte 0. */
    for (size_t i = 0; i < len; i++) {
        size_t byte_index_from_end = len - 1 - i; /* 0 = LSB byte */
        size_t limb_index = byte_index_from_end / 4;
        size_t shift = (byte_index_from_end % 4) * 8;
        r->limb[limb_index] |= ((uint32_t)buf[i]) << shift;
    }
}

void bn_to_be_bytes(uint8_t *buf, const bignum2048_t *a)
{
    for (size_t i = 0; i < BN_BYTES; i++) {
        size_t byte_index_from_end = BN_BYTES - 1 - i;
        size_t limb_index = byte_index_from_end / 4;
        size_t shift = (byte_index_from_end % 4) * 8;
        buf[i] = (uint8_t)((a->limb[limb_index] >> shift) & 0xFF);
    }
}

int bn_cmp(const bignum2048_t *a, const bignum2048_t *b)
{
    for (int i = BN_LIMBS - 1; i >= 0; i--) {
        if (a->limb[i] != b->limb[i]) {
            return (a->limb[i] > b->limb[i]) ? 1 : -1;
        }
    }
    return 0;
}

int bn_is_zero(const bignum2048_t *a)
{
    uint32_t acc = 0;
    for (int i = 0; i < BN_LIMBS; i++) {
        acc |= a->limb[i];
    }
    return acc == 0;
}

uint32_t bn_add(bignum2048_t *r, const bignum2048_t *a, const bignum2048_t *b)
{
    uint64_t carry = 0;
    for (int i = 0; i < BN_LIMBS; i++) {
        uint64_t sum = (uint64_t)a->limb[i] + (uint64_t)b->limb[i] + carry;
        r->limb[i] = (uint32_t)sum;
        carry = sum >> 32;
    }
    return (uint32_t)carry;
}

uint32_t bn_sub(bignum2048_t *r, const bignum2048_t *a, const bignum2048_t *b)
{
    uint64_t borrow = 0;
    for (int i = 0; i < BN_LIMBS; i++) {
        uint64_t ai = a->limb[i];
        uint64_t bi = (uint64_t)b->limb[i] + borrow;
        if (ai >= bi) {
            r->limb[i] = (uint32_t)(ai - bi);
            borrow = 0;
        } else {
            r->limb[i] = (uint32_t)((ai + ((uint64_t)1 << 32)) - bi);
            borrow = 1;
        }
    }
    return (uint32_t)borrow;
}

int bn_sub_if_ge(bignum2048_t *r, const bignum2048_t *b)
{
    if (bn_cmp(r, b) >= 0) {
        bignum2048_t tmp;
        bn_sub(&tmp, r, b);
        *r = tmp;
        return 1;
    }
    return 0;
}

uint32_t bn_shr1(bignum2048_t *r, const bignum2048_t *a)
{
    uint32_t carry_in = 0;
    for (int i = BN_LIMBS - 1; i >= 0; i--) {
        uint32_t new_carry = a->limb[i] & 1;
        r->limb[i] = (a->limb[i] >> 1) | (carry_in << 31);
        carry_in = new_carry;
    }
    return carry_in;
}

int bn_test_bit(const bignum2048_t *a, unsigned int i)
{
    if (i >= BN_BITS) {
        return 0;
    }
    return (a->limb[i / 32] >> (i % 32)) & 1;
}
