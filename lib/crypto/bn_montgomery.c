/*
 * bn_montgomery.c
 *
 * Implementazione CIOS (Coarsely Integrated Operand Scanning)
 * Montgomery multiplication, standard nei toolkit crypto embedded
 * (variante di quella usata in BearSSL/mbedTLS) perche' evita di
 * allocare buffer temporanei piu' grandi di 2*BN_LIMBS e non
 * richiede divisione a 64 bit.
 */

#include "bn_montgomery.h"

/* ---- Passo 1: calcolo di n0_inv = -n^-1 mod 2^32 ----
 *
 * Usa Newton's method per l'inverso modulare mod 2^32, che
 * converge in log2(32)=5 iterazioni per un numero dispari (tecnica
 * standard, la stessa usata in BearSSL br_i31_ninv31 e in mbedTLS
 * mpi_montg_init). Non serve un modinv bignum generico: n0_inv e'
 * un solo word a 32 bit.
 */
static uint32_t mont_compute_n0_inv(uint32_t n0)
{
    /* n0 e' dispari per costruzione (n e' prodotto di due primi
     * dispari), quindi ha inverso mod 2^32. */
    uint32_t x = n0; /* approssimazione iniziale: n0^-1 = n0 mod 8 e' vero per Hensel lifting */
    for (int i = 0; i < 4; i++) {
        /* x_{k+1} = x_k * (2 - n0*x_k) mod 2^32 */
        x = x * (2u - n0 * x);
    }
    /* x ora e' n0^-1 mod 2^32; vogliamo -n0^-1 mod 2^32 */
    return (uint32_t)(0u - x);
}

/* ---- Passo 2: (2^2048)^2 mod n, per portare i numeri in forma
 * Montgomery (m -> m*R mod n dove R = 2^2048) ----
 *
 * Calcolato con shift-and-reduce: partiamo da 1 e raddoppiamo
 * (con riduzione condizionale) 4096 volte (= 2 * BN_BITS), che da'
 * 2^4096 mod n = R^2 mod n. E' O(bit) invece che una vera
 * moltiplicazione, ma va bene: e' eseguito una sola volta per
 * inizializzazione del contesto, non nel loop caldo del modexp.
 */
static void mont_compute_r2_mod_n(bignum2048_t *r2, const bignum2048_t *n)
{
    bignum2048_t acc;
    bn_zero(&acc);
    acc.limb[0] = 1; /* acc = 1 */

    /* Portiamo acc a 1 mod n (nel caso limite n==1, edge case
     * teorico che comunque non e' un modulo RSA valido) */
    bn_sub_if_ge(&acc, n);

    for (int i = 0; i < 2 * BN_BITS; i++) {
        /* acc = acc*2 mod n */
        uint32_t carry_bit = 0;
        {
            uint32_t prev_carry = 0;
            for (int limb = 0; limb < BN_LIMBS; limb++) {
                uint32_t v = acc.limb[limb];
                uint32_t new_carry = v >> 31;
                acc.limb[limb] = (v << 1) | prev_carry;
                prev_carry = new_carry;
            }
            carry_bit = prev_carry;
        }
        /* Se e' uscito un carry dal top limb, acc "virtuale" >= 2^2048 > n
         * di sicuro, quindi va comunque ridotto: la sottrazione
         * condizionale sotto gestisce sia questo sia il caso
         * acc >= n senza overflow. Per gestire correttamente il
         * carry uscito dobbiamo pero' considerarlo: usiamo un
         * confronto esteso. */
        if (carry_bit) {
            /* acc (contando il bit uscito come 2^2048) e' sicuramente >= n
             * (n < 2^2048), quindi sottraiamo n incondizionatamente. */
            bignum2048_t tmp;
            bn_sub(&tmp, &acc, n);
            acc = tmp;
        } else {
            bn_sub_if_ge(&acc, n);
        }
    }

    *r2 = acc;
}

int bn_mont_init(bn_mont_ctx_t *ctx, const bignum2048_t *n)
{
    if (bn_is_zero(n)) {
        return -1;
    }
    if ((n->limb[0] & 1) == 0) {
        return -1; /* n deve essere dispari */
    }

    ctx->n = *n;
    ctx->n0_inv = mont_compute_n0_inv(n->limb[0]);
    mont_compute_r2_mod_n(&ctx->r2_mod_n, n);
    return 0;
}

/* ---- Montgomery multiplication (CIOS) ----
 *
 * Calcola r = a*b*R^-1 mod n, dove R = 2^2048.
 * Questa e' l'operazione fondamentale: se a e b sono gia' in forma
 * Montgomery (cioe' a = a_real*R mod n), il risultato r e' anch'esso
 * in forma Montgomery, cosi' possiamo incatenare moltiplicazioni
 * senza mai fare una riduzione modulare "vera" con divisione.
 *
 * t = a*b (a 4096 bit, rappresentato come BN_LIMBS*2+1 word per il
 * carry finale), poi per ogni limb da 0 a BN_LIMBS-1:
 *   m = t[i] * n0_inv mod 2^32
 *   t = t + m*n*2^(32*i)
 * Alla fine t/R = t >> 2048 e' il risultato prima della riduzione
 * finale condizionale.
 */
static void mont_mul(bignum2048_t *r, const bignum2048_t *a,
                      const bignum2048_t *b, const bn_mont_ctx_t *ctx)
{
    /* t ha bisogno di BN_LIMBS*2 + 1 word per contenere il prodotto
     * a*b (2*BN_LIMBS word) piu' il carry che si accumula durante
     * la riduzione (1 word extra). */
    uint32_t t[BN_LIMBS * 2 + 1];
    for (int i = 0; i < BN_LIMBS * 2 + 1; i++) {
        t[i] = 0;
    }

    for (int i = 0; i < BN_LIMBS; i++) {
        /* --- fase di moltiplicazione: t += a[i] * b (shiftato di i limb) --- */
        uint64_t carry = 0;
        uint64_t ai = a->limb[i];
        for (int j = 0; j < BN_LIMBS; j++) {
            uint64_t prod = ai * (uint64_t)b->limb[j] + (uint64_t)t[i + j] + carry;
            t[i + j] = (uint32_t)prod;
            carry = prod >> 32;
        }
        /* propaga il carry residuo oltre BN_LIMBS+i */
        int k = i + BN_LIMBS;
        while (carry != 0) {
            uint64_t sum = (uint64_t)t[k] + carry;
            t[k] = (uint32_t)sum;
            carry = sum >> 32;
            k++;
        }

        /* --- fase di riduzione: m = t[i]*n0_inv mod 2^32; t += m*n*2^(32*i) --- */
        uint32_t m = t[i] * ctx->n0_inv;
        uint64_t carry2 = 0;
        for (int j = 0; j < BN_LIMBS; j++) {
            uint64_t prod = (uint64_t)m * ctx->n.limb[j] + (uint64_t)t[i + j] + carry2;
            t[i + j] = (uint32_t)prod;
            carry2 = prod >> 32;
        }
        k = i + BN_LIMBS;
        while (carry2 != 0) {
            uint64_t sum = (uint64_t)t[k] + carry2;
            t[k] = (uint32_t)sum;
            carry2 = sum >> 32;
            k++;
        }
        /* Per costruzione t[i] deve essere diventato 0 qui (m e'
         * scelto apposta per annullare t[i] mod 2^32); non lo
         * verifichiamo a runtime per non appesantire il loop caldo. */
    }

    /* Il risultato e' t >> 2048, cioe' le word da BN_LIMBS a
     * 2*BN_LIMBS (+ eventuale carry in t[2*BN_LIMBS]) */
    bignum2048_t result;
    for (int i = 0; i < BN_LIMBS; i++) {
        result.limb[i] = t[BN_LIMBS + i];
    }

    /* Riduzione finale condizionale: se result >= n (compreso il
     * caso in cui t[2*BN_LIMBS] e' 1, cioe' c'e' stato un overflow
     * oltre i BN_LIMBS word), sottrai n. */
    if (t[BN_LIMBS * 2] != 0) {
        bignum2048_t tmp;
        bn_sub(&tmp, &result, &ctx->n);
        result = tmp;
    } else {
        bn_sub_if_ge(&result, &ctx->n);
    }

    *r = result;
}

void bn_mont_modexp_u32(bignum2048_t *r, const bignum2048_t *base,
                         uint32_t exp, const bn_mont_ctx_t *ctx)
{
    /* Converti base in forma Montgomery: base_mont = base*R mod n
     * = mont_mul(base, R2modN) perche' mont_mul(a,b) = a*b*R^-1,
     * quindi mont_mul(base, R2) = base*R^2*R^-1 = base*R. */
    bignum2048_t base_mont;
    mont_mul(&base_mont, base, &ctx->r2_mod_n, ctx);

    /* acc_mont = 1 in forma Montgomery = R mod n = mont_mul(1, R2) */
    bignum2048_t one;
    bn_zero(&one);
    one.limb[0] = 1;
    bignum2048_t acc_mont;
    mont_mul(&acc_mont, &one, &ctx->r2_mod_n, ctx);

    /* Square-and-multiply da MSB a LSB dell'esponente.
     * L'esponente pubblico RSA (tipicamente 65537 = 0x10001) e'
     * pubblico per definizione, quindi non c'e' problema di
     * side-channel a farlo dipendente dai bit dell'esponente qui:
     * chiunque conosce gia' e. */
    int started = 0;
    for (int i = 31; i >= 0; i--) {
        if (!started) {
            if (((exp >> i) & 1) == 0) {
                continue; /* salta gli zeri iniziali */
            }
            started = 1;
        }
        /* acc = acc^2 */
        bignum2048_t sq;
        mont_mul(&sq, &acc_mont, &acc_mont, ctx);
        acc_mont = sq;

        if ((exp >> i) & 1) {
            bignum2048_t mul;
            mont_mul(&mul, &acc_mont, &base_mont, ctx);
            acc_mont = mul;
        }
    }
    if (!started) {
        /* exp == 0: base^0 = 1 */
        *r = one;
        return;
    }

    /* Riconverti da forma Montgomery: acc_real = acc_mont * R^-1 mod n
     * = mont_mul(acc_mont, 1) */
    mont_mul(r, &acc_mont, &one, ctx);
}
