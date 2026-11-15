/*
 * Implementation of the GCM polynomial hash in pure software.
 *
 * I don't know of a faster way to do this in a side-channel safe
 * manner than by precomputing a giant table and iterating over the
 * whole thing.
 *
 * The original GCM reference suggests that you precompute the effects
 * of multiplying a 128-bit value by the fixed key, in the form of a
 * table indexed by some number of bits of the input value, so that
 * you end up computing something of the form
 *
 *   table1[x & 0xFF] ^ table2[(x>>8) & 0xFF] ^ ... ^ table15[(x>>120) & 0xFF]
 *
 * But that was obviously written before cache and timing leaks were
 * known about. What's a time-safe approach?
 *
 * Well, the above technique isn't fixed to 8 bits of input per table.
 * You could trade off the number of tables against the size of each
 * table. At one extreme of this tradeoff, you have 128 tables each
 * indexed by a single input bit - which is to say, you have 128
 * values, each 128 bits wide, and you XOR together the subset of
 * those values corresponding to the input bits, which you can do by
 * making a bitmask out of each input bit using standard constant-
 * time-coding bit twiddling techniques.
 *
 * That's pretty unpleasant when GCM is supposed to be a fast
 * algorithm, but I don't know of a better approach that meets current
 * security standards! Suggestions welcome, if they can get through
 * testsc.
 */

#include "ssh.h"
#include "aesgcm.h"

/*
 * Store a 128-bit value as four uint32_t, from the most significant
 * (w[0]) to the least significant (w[3]).
 */
typedef struct {
    uint32_t w[4];
} value128_t;

typedef struct aesgcm_sw {
    AESGCM_COMMON_FIELDS;

    /* Accumulator for the current evaluation, and mask that will be
     * XORed in at the end. High  */
    value128_t acc, mask;

    /*
     * Table of values to XOR in for each bit, representing the effect
     * of multiplying by the fixed key. The key itself doesn't need to
     * be stored separately, because it's never used. (However, it is
     * also the first entry in the table, so if you _do_ need it,
     * there it is.)
     *
     * Table is indexed from the low bit of the input upwards.
     */
    value128_t table[128];
} aesgcm_sw;

static bool aesgcm_sw_available(void)
{
    return true;  /* pure software implementation, always available */
}

static void aesgcm_sw_setkey_impl(aesgcm_sw *gcm, const unsigned char *var)
{
    value128_t v;
    size_t i;
    for (i = 0; i < 4; i++)
        v.w[i] = GET_32BIT_MSB_FIRST(var + 4*i);

    /*
     * Prepare the table. This has to be done in reverse order, so
     * that the original value of the variable corresponds to
     * table[127], because AES-GCM works in the bit-reversal of its
     * logical specification so that's where the logical constant term
     * lives. (See more detailed comment in aesgcm-ref-poly.c.)
     */
    for (i = 0; i < 128; i++) {
        uint32_t lobit;
        gcm->table[127 - i] = v;

        /* Multiply v by x, which means shifting right (bit reversal
         * again) and then adding 0xE1 at the top if we shifted a 1 out. */
        lobit = v.w[3] & 1;
        v.w[3] = (v.w[3] >> 1) ^ (v.w[2] << 31);
        v.w[2] = (v.w[2] >> 1) ^ (v.w[1] << 31);
        v.w[1] = (v.w[1] >> 1) ^ (v.w[0] << 31);
        v.w[0] = (v.w[0] >> 1) ^ (0xE1000000 & -lobit);
    }
}

static inline void aesgcm_sw_setup(aesgcm_sw *gcm, const unsigned char *mask)
{
    size_t i;
    for (i = 0; i < 4; i++) {
        gcm->mask.w[i] = GET_32BIT_MSB_FIRST(mask + 4*i);
        gcm->acc.w[i] = 0;
    }
}

static inline void aesgcm_sw_coeff(aesgcm_sw *gcm, const unsigned char *coeff)
{
    value128_t out;
    const value128_t *tableptr;
    size_t i, j;
    /* XOR in the new coefficient */
    for (j = 0; j < 4; j++)
        gcm->acc.w[j] ^= GET_32BIT_MSB_FIRST(coeff + 4*j);

    /* And now just loop over the bits of acc, from the least
     * significant upwards, making up a new value by XORing together
     * the entries of 'table' corresponding to set bits. */

    for (j = 0; j < 4; j++)
        out.w[j] = 0;

    tableptr = gcm->table;

    for (j = 4; j-- > 0 ;) {
        for (i = 0; i < 32; i++) {
            uint32_t bit = 1 & gcm->acc.w[j];
            uint32_t mask;
            gcm->acc.w[j] >>= 1;
            mask = -bit;
            out.w[0] ^= mask & tableptr->w[0];
            out.w[1] ^= mask & tableptr->w[1];
            out.w[2] ^= mask & tableptr->w[2];
            out.w[3] ^= mask & tableptr->w[3];
            tableptr++;
        }
    }

    gcm->acc = out;
}

static inline void aesgcm_sw_output(aesgcm_sw *gcm, unsigned char *output)
{
    size_t i;
    for (i = 0; i < 4; i++)
        PUT_32BIT_MSB_FIRST(output + 4*i, gcm->acc.w[i] ^ gcm->mask.w[i]);
    smemclr(&gcm->acc, 16);
    smemclr(&gcm->mask, 16);
}

#define AESGCM_FLAVOUR sw
#define AESGCM_NAME "unaccelerated"
#include "aesgcm-footer.h"
