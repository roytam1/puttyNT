/*
 * mpint_i.h: definitions used internally by the bignum code, and
 * also a few other vaguely-bignum-like places.
 */

/* ----------------------------------------------------------------------
 * The definition of BignumInt and the multiply macros used throughout
 * the bignum code to treat numbers as arrays of words. Exported so
 * that other code (e.g. poly1305) can use it too.
 *
 * This code exports:
 *
 *  - two types: 'BignumInt' and 'BignumCarry'. BignumInt is an
 *    unsigned integer type which will be used as the base word size
 *    for all bignum operations. BignumCarry is an unsigned integer
 *    type used to hold the carry flag taken as input and output by
 *    the BignumADC macro (see below).
 *
 *  - five constant macros:
 *     + BIGNUM_INT_BITS, the number of bits in BignumInt,
 *     + BIGNUM_INT_BYTES, the number of bytes that works out to
 *     + BIGNUM_TOP_BIT, the BignumInt value consisting of only the top bit
 *     + BIGNUM_INT_MASK, the BignumInt value with all bits set
 *     + BIGNUM_INT_BITS_BITS, log to the base 2 of BIGNUM_INT_BITS.
 *
 *  - four statement macros: BignumADC, BignumMUL, BignumMULADD,
 *    BignumMULADD2. These do various kinds of multi-word arithmetic,
 *    and all produce two output values.
 *     * BignumADC(ret,retc,a,b,c) takes input BignumInt values a,b
 *       and a BignumCarry c, and outputs a BignumInt ret = a+b+c and
 *       a BignumCarry retc which is the carry off the top of that
 *       addition.
 *     * BignumMUL(rh,rl,a,b) returns the two halves of the
 *       double-width product a*b.
 *     * BignumMULADD(rh,rl,a,b,addend) returns the two halves of the
 *       double-width value a*b + addend.
 *     * BignumMULADD2(rh,rl,a,b,addend1,addend2) returns the two
 *       halves of the double-width value a*b + addend1 + addend2.
 *
 * BignumInt is 16 bits, and the statement macros do their arithmetic
 * in unsigned long, a type twice that length. C89 guarantees unsigned
 * short and unsigned long to be at least those sizes, so this works
 * with every C compiler, including the ones for Windows NT 3.1, which
 * have no 64-bit integer type. (Upstream PuTTY also has 32- and
 * 64-bit BignumInt, for compilers with 64- and 128-bit integers.)
 */

typedef unsigned short BignumInt;
#define BIGNUM_INT_BITS_BITS 4
#define DEFINE_BIGNUMDBLINT typedef unsigned long BignumDblInt

/*
 * Define all the easy constant macros in terms of
 * BIGNUM_INT_BITS_BITS.
 */
#define BIGNUM_INT_BITS (1 << BIGNUM_INT_BITS_BITS)
#define BIGNUM_INT_BYTES (BIGNUM_INT_BITS / 8)
#define BIGNUM_TOP_BIT (((BignumInt)1) << (BIGNUM_INT_BITS-1))
#define BIGNUM_INT_MASK (BIGNUM_TOP_BIT | (BIGNUM_TOP_BIT-1))

/*
 * Just occasionally, we might need a GET_nnBIT_xSB_FIRST macro to
 * operate on whatever BignumInt is.
 */
#define GET_BIGNUMINT_MSB_FIRST GET_16BIT_MSB_FIRST
#define GET_BIGNUMINT_LSB_FIRST GET_16BIT_LSB_FIRST
#define PUT_BIGNUMINT_MSB_FIRST PUT_16BIT_MSB_FIRST
#define PUT_BIGNUMINT_LSB_FIRST PUT_16BIT_LSB_FIRST

/*
 * The statement macros, in terms of the double-width type
 * BignumDblInt. BignumCarry is the same thing as BignumInt, for
 * simplicity.
 */
typedef BignumInt BignumCarry;
/* NT port: a and b go through BignumInt variables, because the MIPS
 * compilers (NT 3.1 SDK, Visual C++ 4.x) lose the cast when b is ~x
 * for a BignumInt x (see normalise_to_1 in mpint.c). */
#define BignumADC(ret, retc, a, b, c) do                        \
    {                                                           \
        DEFINE_BIGNUMDBLINT;                                    \
        BignumInt ADC_a = (BignumInt)(a);                       \
        BignumInt ADC_b = (BignumInt)(b);                       \
        BignumDblInt ADC_temp = ADC_a;                          \
        ADC_temp += ADC_b;                                      \
        ADC_temp += (c);                                        \
        (ret) = (BignumInt)ADC_temp;                            \
        (retc) = (BignumCarry)(ADC_temp >> BIGNUM_INT_BITS);    \
    } while (0)

#define BignumMUL(rh, rl, a, b) do                              \
    {                                                           \
        DEFINE_BIGNUMDBLINT;                                    \
        BignumDblInt MUL_temp = (BignumInt)(a);                 \
        MUL_temp *= (BignumInt)(b);                             \
        (rh) = (BignumInt)(MUL_temp >> BIGNUM_INT_BITS);        \
        (rl) = (BignumInt)(MUL_temp);                           \
    } while (0)

#define BignumMULADD(rh, rl, a, b, addend) do                   \
    {                                                           \
        DEFINE_BIGNUMDBLINT;                                    \
        BignumDblInt MUL_temp = (BignumInt)(a);                 \
        MUL_temp *= (BignumInt)(b);                             \
        MUL_temp += (BignumInt)(addend);                        \
        (rh) = (BignumInt)(MUL_temp >> BIGNUM_INT_BITS);        \
        (rl) = (BignumInt)(MUL_temp);                           \
    } while (0)

#define BignumMULADD2(rh, rl, a, b, addend1, addend2) do        \
    {                                                           \
        DEFINE_BIGNUMDBLINT;                                    \
        BignumDblInt MUL_temp = (BignumInt)(a);                 \
        MUL_temp *= (BignumInt)(b);                             \
        MUL_temp += (BignumInt)(addend1);                       \
        MUL_temp += (BignumInt)(addend2);                       \
        (rh) = (BignumInt)(MUL_temp >> BIGNUM_INT_BITS);        \
        (rl) = (BignumInt)(MUL_temp);                           \
    } while (0)

/* ----------------------------------------------------------------------
 * Data structures used inside mpint.c.
 */

struct mp_int {
    size_t nw;
    BignumInt *w;
};

struct MontyContext {
    /*
     * The actual modulus.
     */
    mp_int *m;

    /*
     * Montgomery multiplication works by selecting a value r > m,
     * coprime to m, which is really easy to divide by. In binary
     * arithmetic, that means making it a power of 2; in fact we make
     * it a whole number of BignumInt.
     *
     * We don't store r directly as an mp_int (there's no need). But
     * its value is 2^rbits; we also store rw = rbits/BIGNUM_INT_BITS
     * (the corresponding word offset within an mp_int).
     *
     * pw is the number of words needed to store an mp_int you're
     * doing reduction on: it has to be big enough to hold the sum of
     * an input value up to m^2 plus an extra addend up to m*r.
     */
    size_t rbits, rw, pw;

    /*
     * The key step in Montgomery reduction requires the inverse of -m
     * mod r.
     */
    mp_int *minus_minv_mod_r;

    /*
     * r^1, r^2 and r^3 mod m, which are used for various purposes.
     *
     * (Annoyingly, this is one of the rare cases where it would have
     * been nicer to have a Pascal-style 1-indexed array. I couldn't
     * _quite_ bring myself to put a gratuitous zero element in here.
     * So you just have to live with getting r^k by taking the [k-1]th
     * element of this array.)
     */
    mp_int *powers_of_r_mod_m[3];

    /*
     * Persistent scratch space from which monty_* functions can
     * allocate storage for intermediate values.
     */
    mp_int *scratch;
};

/* Functions shared between mpint.c and mpunsafe.c */
mp_int *mp_make_sized(size_t nw);
