/*
 * ChaCha20-Poly1305 Implementation for SSH-2
 *
 * Protocol spec:
 *  http://cvsweb.openbsd.org/cgi-bin/cvsweb/src/usr.bin/ssh/PROTOCOL.chacha20poly1305?rev=1.2&content-type=text/x-cvsweb-markup
 *
 * ChaCha20 spec:
 *  http://cr.yp.to/chacha/chacha-20080128.pdf
 *
 * Salsa20 spec:
 *  http://cr.yp.to/snuffle/spec.pdf
 *
 * Poly1305-AES spec:
 *  http://cr.yp.to/mac/poly1305-20050329.pdf
 *
 * The nonce for the Poly1305 is the second part of the key output
 * from the first round of ChaCha20. This removes the AES requirement.
 * This is undocumented!
 *
 * This has an intricate link between the cipher and the MAC. The
 * keying of both is done in by the cipher and setting of the IV is
 * done by the MAC. One cannot operate without the other. The
 * configuration of the ssh_cipheralg structure ensures that the MAC is
 * set (and others ignored) if this cipher is chosen.
 *
 * This cipher also encrypts the length using a different
 * instantiation of the cipher using a different key and IV made from
 * the sequence number which is passed in addition when calling
 * encrypt/decrypt on it.
 */

#include "ssh.h"
#include "mpint_i.h"

#ifndef INLINE
#define INLINE
#endif

/* ChaCha20 implementation, only supporting 256-bit keys */

/* State for each ChaCha20 instance */
struct chacha20 {
    /* Current context, usually with the count incremented
     * 0-3 are the static constant
     * 4-11 are the key
     * 12-13 are the counter
     * 14-15 are the IV */
    uint32_t state[16];
    /* The output of the state above ready to xor */
    unsigned char current[64];
    /* The index of the above currently used to allow a true streaming cipher */
    int currentIndex;
};

static INLINE void chacha20_round(struct chacha20 *ctx)
{
    int i;
    uint32_t copy[16];

    /* Take a copy */
    memcpy(copy, ctx->state, sizeof(copy));

    /* A circular rotation for a 32bit number */
#define rotl(x, shift) x = ((x << shift) | (x >> (32 - shift)))

    /* What to do for each quarter round operation */
#define qrop(a, b, c, d)                        \
    copy[a] += copy[b];                         \
    copy[c] ^= copy[a];                         \
    rotl(copy[c], d)

    /* A quarter round */
#define quarter(a, b, c, d)                     \
    qrop(a, b, d, 16);                          \
    qrop(c, d, b, 12);                          \
    qrop(a, b, d, 8);                           \
    qrop(c, d, b, 7)

    /* Do 20 rounds, in pairs because every other is different */
    for (i = 0; i < 20; i += 2) {
        /* A round */
        quarter(0, 4, 8, 12);
        quarter(1, 5, 9, 13);
        quarter(2, 6, 10, 14);
        quarter(3, 7, 11, 15);
        /* Another slightly different round */
        quarter(0, 5, 10, 15);
        quarter(1, 6, 11, 12);
        quarter(2, 7, 8, 13);
        quarter(3, 4, 9, 14);
    }

    /* Dump the macros, don't need them littering */
#undef rotl
#undef qrop
#undef quarter

    /* Add the initial state */
    for (i = 0; i < 16; ++i) {
        copy[i] += ctx->state[i];
    }

    /* Update the content of the xor buffer */
    for (i = 0; i < 16; ++i) {
        ctx->current[i * 4 + 0] = copy[i] >> 0;
        ctx->current[i * 4 + 1] = copy[i] >> 8;
        ctx->current[i * 4 + 2] = copy[i] >> 16;
        ctx->current[i * 4 + 3] = copy[i] >> 24;
    }
    /* State full, reset pointer to beginning */
    ctx->currentIndex = 0;
    smemclr(copy, sizeof(copy));

    /* Increment round counter */
    ++ctx->state[12];
    /* Check for overflow, not done in one line so the 32 bits are chopped by the type */
    if (!(uint32_t)(ctx->state[12])) {
        ++ctx->state[13];
    }
}

/* Initialise context with 256bit key */
static void chacha20_key(struct chacha20 *ctx, const unsigned char *key)
{
    static const char constant[16] = "expand 32-byte k";

    /* Add the fixed string to the start of the state */
    ctx->state[0] = GET_32BIT_LSB_FIRST(constant + 0);
    ctx->state[1] = GET_32BIT_LSB_FIRST(constant + 4);
    ctx->state[2] = GET_32BIT_LSB_FIRST(constant + 8);
    ctx->state[3] = GET_32BIT_LSB_FIRST(constant + 12);

    /* Add the key */
    ctx->state[4]  = GET_32BIT_LSB_FIRST(key + 0);
    ctx->state[5]  = GET_32BIT_LSB_FIRST(key + 4);
    ctx->state[6]  = GET_32BIT_LSB_FIRST(key + 8);
    ctx->state[7]  = GET_32BIT_LSB_FIRST(key + 12);
    ctx->state[8]  = GET_32BIT_LSB_FIRST(key + 16);
    ctx->state[9]  = GET_32BIT_LSB_FIRST(key + 20);
    ctx->state[10] = GET_32BIT_LSB_FIRST(key + 24);
    ctx->state[11] = GET_32BIT_LSB_FIRST(key + 28);

    /* New key, dump context */
    ctx->currentIndex = 64;
}

static void chacha20_iv(struct chacha20 *ctx, const unsigned char *iv)
{
    ctx->state[12] = 0;
    ctx->state[13] = 0;
    ctx->state[14] = GET_32BIT_MSB_FIRST(iv);
    ctx->state[15] = GET_32BIT_MSB_FIRST(iv + 4);

    /* New IV, dump context */
    ctx->currentIndex = 64;
}

static void chacha20_encrypt(struct chacha20 *ctx, unsigned char *blk, int len)
{
    while (len) {
        /* If we don't have any state left, then cycle to the next */
        if (ctx->currentIndex >= 64) {
            chacha20_round(ctx);
        }

        /* Do the xor while there's some state left and some plaintext left */
        while (ctx->currentIndex < 64 && len) {
            *blk++ ^= ctx->current[ctx->currentIndex++];
            --len;
        }
    }
}

/* Decrypt is encrypt... It's xor against a PRNG... */
static INLINE void chacha20_decrypt(struct chacha20 *ctx,
                                    unsigned char *blk, int len)
{
    chacha20_encrypt(ctx, blk, len);
}

/* Poly1305 implementation (no AES, nonce is not encrypted) */

#define NWORDS ((130 + BIGNUM_INT_BITS-1) / BIGNUM_INT_BITS)
typedef struct bigval {
    BignumInt w[NWORDS];
} bigval;

static void bigval_clear(bigval *r)
{
    int i;
    for (i = 0; i < NWORDS; i++)
        r->w[i] = 0;
}

static void bigval_import_le(bigval *r, const void *vdata, int len)
{
    const unsigned char *data = (const unsigned char *)vdata;
    int i;
    bigval_clear(r);
    for (i = 0; i < len; i++)
        r->w[i / BIGNUM_INT_BYTES] |=
            (BignumInt)data[i] << (8 * (i % BIGNUM_INT_BYTES));
}

static void bigval_export_le(const bigval *r, void *vdata, int len)
{
    unsigned char *data = (unsigned char *)vdata;
    int i;
    for (i = 0; i < len; i++)
        data[i] = r->w[i / BIGNUM_INT_BYTES] >> (8 * (i % BIGNUM_INT_BYTES));
}

/*
 * Core functions to do arithmetic mod p = 2^130-5. bigval_add is
 * generated automatically by upstream PuTTY's contrib/make1305.py,
 * which also makes it for 32- and 64-bit BignumInt. bigval_mul_mod_p
 * and bigval_final_reduce are hand-written loop forms of what it
 * generates.
 */

#if BIGNUM_INT_BITS == 16

static void bigval_add(bigval *r, const bigval *a, const bigval *b)
{
    BignumInt v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14;
    BignumInt v15, v16, v17, v18, v19, v20, v21, v22, v23, v24, v25, v26;
    BignumCarry carry;

    v0 = a->w[0];
    v1 = a->w[1];
    v2 = a->w[2];
    v3 = a->w[3];
    v4 = a->w[4];
    v5 = a->w[5];
    v6 = a->w[6];
    v7 = a->w[7];
    v8 = a->w[8];
    v9 = b->w[0];
    v10 = b->w[1];
    v11 = b->w[2];
    v12 = b->w[3];
    v13 = b->w[4];
    v14 = b->w[5];
    v15 = b->w[6];
    v16 = b->w[7];
    v17 = b->w[8];
    BignumADC(v18, carry, v0, v9, 0);
    BignumADC(v19, carry, v1, v10, carry);
    BignumADC(v20, carry, v2, v11, carry);
    BignumADC(v21, carry, v3, v12, carry);
    BignumADC(v22, carry, v4, v13, carry);
    BignumADC(v23, carry, v5, v14, carry);
    BignumADC(v24, carry, v6, v15, carry);
    BignumADC(v25, carry, v7, v16, carry);
    v26 = v8 + v17 + carry;
    r->w[0] = v18;
    r->w[1] = v19;
    r->w[2] = v20;
    r->w[3] = v21;
    r->w[4] = v22;
    r->w[5] = v23;
    r->w[6] = v24;
    r->w[7] = v25;
    r->w[8] = v26;
}

/*
 * The same arithmetic as the code that contrib/make1305.py makes, one
 * step for each of its steps, but written as loops. For 16-bit
 * BignumInt that code is one function with 230 variables, and the
 * MIPS compilers of 1993-1996 took hours over it.
 */
static void bigval_mul_mod_p(bigval *r, const bigval *a, const bigval *b)
{
    BignumInt p[2*NWORDS-1];           /* a*b, which fits in 17 words */
    BignumInt h[NWORDS-1];
    BignumInt hi, lo, mulcarry, extra, top25;
    BignumCarry carry;
    int i, j;

    /* Schoolbook multiplication. The top word gets no carry out. */
    for (i = 0; i < 2*NWORDS-1; i++)
        p[i] = 0;
    for (i = 0; i < NWORDS; i++) {
        mulcarry = 0;
        for (j = 0; j < NWORDS; j++) {
            if (i + j == 2*NWORDS-2) {
                p[i+j] = (BignumInt)((unsigned long)a->w[i] * b->w[j] +
                                     p[i+j] + mulcarry);
            } else {
                BignumMULADD2(hi, lo, a->w[i], b->w[j], p[i+j], mulcarry);
                p[i+j] = lo;
                mulcarry = hi;
            }
        }
        if (i + NWORDS < 2*NWORDS-1)
            p[i+NWORDS] = mulcarry;
    }

    /*
     * a*b = L + 2^130 H, where L is the low 130 bits, and 2^130 = 5
     * mod p. So add 5 times the low 130 bits of H (8 words in h, and
     * 2 bits in extra) and 25 times the rest of H (top25) to L.
     */
    for (i = 0; i < NWORDS-1; i++)
        h[i] = (BignumInt)((p[NWORDS-1+i] >> 2) | (p[NWORDS+i] << 14));
    mulcarry = 0;
    for (i = 0; i < NWORDS-1; i++) {
        BignumMULADD(hi, lo, 5, h[i], mulcarry);
        h[i] = lo;
        mulcarry = hi;
    }
    extra = (BignumInt)(5 * ((p[2*NWORDS-2] >> 2) & 3) + mulcarry);
    top25 = (BignumInt)(25 * (p[2*NWORDS-2] >> 4));

    carry = 0;
    for (i = 0; i < NWORDS-1; i++)
        BignumADC(p[i], carry, p[i], h[i], carry);
    p[NWORDS-1] = (BignumInt)((p[NWORDS-1] & 3) + extra + carry);

    BignumADC(r->w[0], carry, p[0], top25, 0);
    for (i = 1; i < NWORDS-1; i++)
        BignumADC(r->w[i], carry, p[i], 0, carry);
    r->w[NWORDS-1] = (BignumInt)(p[NWORDS-1] + carry);
}

/*
 * Also the arithmetic of contrib/make1305.py, step by step, as loops.
 * Its straight-line form (44 variables, 24 carry steps) kept the
 * optimiser of the NT 3.1 SDK for MIPS (MSUOPT) busy for over an hour.
 */
static void bigval_final_reduce(bigval *n)
{
    BignumInt w[NWORDS-1];
    BignumInt top, add, sum;
    BignumCarry carry;
    int i;

    /* The low 130 bits, plus 5 times the rest (2^130 = 5 mod p) */
    add = (BignumInt)(5 * (n->w[NWORDS-1] >> 2));
    BignumADC(w[0], carry, n->w[0], add, 0);
    for (i = 1; i < NWORDS-1; i++)
        BignumADC(w[i], carry, n->w[i], 0, carry);
    top = (BignumInt)((n->w[NWORDS-1] & 3) + carry);

    /* If adding 5 to that reaches 2^130, it is at least p: then
     * subtract p by adding 5 and dropping bit 130. */
    BignumADC(sum, carry, w[0], 5, 0);
    for (i = 1; i < NWORDS-1; i++)
        BignumADC(sum, carry, w[i], 0, carry);
    add = (BignumInt)(5 * ((BignumInt)(top + carry) >> 2));

    BignumADC(n->w[0], carry, w[0], add, 0);
    for (i = 1; i < NWORDS-1; i++)
        BignumADC(n->w[i], carry, w[i], 0, carry);
    n->w[NWORDS-1] = (BignumInt)((top + carry) & 3);
}

#else
#error Add another bit count to contrib/make1305.py and rerun it
#endif

struct poly1305 {
    unsigned char nonce[16];
    bigval r;
    bigval h;

    /* Buffer in case we get less that a multiple of 16 bytes */
    unsigned char buffer[16];
    int bufferIndex;
};

static void poly1305_init(struct poly1305 *ctx)
{
    memset(ctx->nonce, 0, 16);
    ctx->bufferIndex = 0;
    bigval_clear(&ctx->h);
}

static void poly1305_key(struct poly1305 *ctx, ptrlen key)
{
    unsigned char key_copy[16];
    assert(key.len == 32);             /* Takes a 256 bit key */

    memcpy(key_copy, key.ptr, 16);

    /* Key the MAC itself
     * bytes 4, 8, 12 and 16 are required to have their top four bits clear */
    key_copy[3] &= 0x0f;
    key_copy[7] &= 0x0f;
    key_copy[11] &= 0x0f;
    key_copy[15] &= 0x0f;
    /* bytes 5, 9 and 13 are required to have their bottom two bits clear */
    key_copy[4] &= 0xfc;
    key_copy[8] &= 0xfc;
    key_copy[12] &= 0xfc;
    bigval_import_le(&ctx->r, key_copy, 16);
    smemclr(key_copy, sizeof(key_copy));

    /* Use second 128 bits as the nonce */
    memcpy(ctx->nonce, (const char *)key.ptr + 16, 16);
}

/* Feed up to 16 bytes (should only be less for the last chunk) */
static void poly1305_feed_chunk(struct poly1305 *ctx,
                                const unsigned char *chunk, int len)
{
    bigval c;
    bigval_import_le(&c, chunk, len);
    c.w[len / BIGNUM_INT_BYTES] |=
        (BignumInt)1 << (8 * (len % BIGNUM_INT_BYTES));
    bigval_add(&c, &c, &ctx->h);
    bigval_mul_mod_p(&ctx->h, &c, &ctx->r);
}

static void poly1305_feed(struct poly1305 *ctx,
                          const unsigned char *buf, int len)
{
    /* Check for stuff left in the buffer from last time */
    if (ctx->bufferIndex) {
        /* Try to fill up to 16 */
        while (ctx->bufferIndex < 16 && len) {
            ctx->buffer[ctx->bufferIndex++] = *buf++;
            --len;
        }
        if (ctx->bufferIndex == 16) {
            poly1305_feed_chunk(ctx, ctx->buffer, 16);
            ctx->bufferIndex = 0;
        }
    }

    /* Process 16 byte whole chunks */
    while (len >= 16) {
        poly1305_feed_chunk(ctx, buf, 16);
        len -= 16;
        buf += 16;
    }

    /* Cache stuff that's left over */
    if (len) {
        memcpy(ctx->buffer, buf, len);
        ctx->bufferIndex = len;
    }
}

/* Finalise and populate buffer with 16 byte with MAC */
static void poly1305_finalise(struct poly1305 *ctx, unsigned char *mac)
{
    bigval tmp;

    if (ctx->bufferIndex) {
        poly1305_feed_chunk(ctx, ctx->buffer, ctx->bufferIndex);
    }

    bigval_import_le(&tmp, ctx->nonce, 16);
    bigval_final_reduce(&ctx->h);
    bigval_add(&tmp, &tmp, &ctx->h);
    bigval_export_le(&tmp, mac, 16);
}

/* SSH-2 wrapper */

struct ccp_context {
    struct chacha20 a_cipher; /* Used for length */
    struct chacha20 b_cipher; /* Used for content */

    /* Cache of the first 4 bytes because they are the sequence number */
    /* Kept in 8 bytes with the top as zero to allow easy passing to setiv */
    int mac_initialised; /* Where we have got to in filling mac_iv */
    unsigned char mac_iv[8];

    struct poly1305 mac;

    BinarySink_IMPLEMENTATION;
    ssh_cipher ciph;
    ssh2_mac mac_if;
    bool ciph_allocated, mac_allocated;
};

static ssh2_mac *poly_ssh2_new(
    const ssh2_macalg *alg, ssh_cipher *cipher)
{
    struct ccp_context *ctx = container_of(cipher, struct ccp_context, ciph);
    ctx->mac_if.vt = alg;
    ctx->mac_allocated = true;
    BinarySink_DELEGATE_INIT(&ctx->mac_if, ctx);
    return &ctx->mac_if;
}

static void ccp_common_free(struct ccp_context *ctx)
{
    if (ctx->ciph_allocated || ctx->mac_allocated)
        return;

    smemclr(&ctx->a_cipher, sizeof(ctx->a_cipher));
    smemclr(&ctx->b_cipher, sizeof(ctx->b_cipher));
    smemclr(&ctx->mac, sizeof(ctx->mac));
    sfree(ctx);
}

static void poly_ssh2_free(ssh2_mac *mac)
{
    struct ccp_context *ctx = container_of(mac, struct ccp_context, mac_if);
    ctx->mac_allocated = false;
    ccp_common_free(ctx);
}

static void poly_setkey(ssh2_mac *mac, ptrlen key)
{
    /* Uses the same context as ChaCha20, so ignore */
}

static void poly_start(ssh2_mac *mac)
{
    struct ccp_context *ctx = container_of(mac, struct ccp_context, mac_if);

    ctx->mac_initialised = 0;
    memset(ctx->mac_iv, 0, 8);
    poly1305_init(&ctx->mac);
}

static void poly_BinarySink_write(BinarySink *bs, const void *blkv, size_t len)
{
    struct ccp_context *ctx = BinarySink_DOWNCAST(bs, struct ccp_context);
    const unsigned char *blk = (const unsigned char *)blkv;

    /* First 4 bytes are the IV */
    while (ctx->mac_initialised < 4 && len) {
        ctx->mac_iv[7 - ctx->mac_initialised] = *blk++;
        ++ctx->mac_initialised;
        --len;
    }

    /* Initialise the IV if needed */
    if (ctx->mac_initialised == 4) {
        chacha20_iv(&ctx->b_cipher, ctx->mac_iv);
        ++ctx->mac_initialised;  /* Don't do it again */

        /* Do first rotation */
        chacha20_round(&ctx->b_cipher);

        /* Set the poly key */
        poly1305_key(&ctx->mac, make_ptrlen(ctx->b_cipher.current, 32));

        /* Set the first round as used */
        ctx->b_cipher.currentIndex = 64;
    }

    /* Update the MAC with anything left */
    if (len) {
        poly1305_feed(&ctx->mac, blk, len);
    }
}

static void poly_genresult(ssh2_mac *mac, unsigned char *blk)
{
    struct ccp_context *ctx = container_of(mac, struct ccp_context, mac_if);
    poly1305_finalise(&ctx->mac, blk);
}

static const char *poly_text_name(ssh2_mac *mac)
{
    return "Poly1305";
}

const ssh2_macalg ssh2_poly1305 = {
    /* .new = */ poly_ssh2_new,
    /* .free = */ poly_ssh2_free,
    /* .setkey = */ poly_setkey,
    /* .start = */ poly_start,
    /* .genresult = */ poly_genresult,
    /* .next_message = */ nullmac_next_message,
    /* .text_name = */ poly_text_name,
    /* .name = */ "",
    /* .etm_name = */ "", /* Not selectable individually, just part of
                     * ChaCha20-Poly1305 */
    /* .len = */ 16,
    /* .keylen = */ 0,
};

static ssh_cipher *ccp_new(const ssh_cipheralg *alg)
{
    struct ccp_context *ctx = snew(struct ccp_context);
    BinarySink_INIT(ctx, poly_BinarySink_write);
    poly1305_init(&ctx->mac);
    ctx->ciph.vt = alg;
    ctx->ciph_allocated = true;
    ctx->mac_allocated = false;
    return &ctx->ciph;
}

static void ccp_free(ssh_cipher *cipher)
{
    struct ccp_context *ctx = container_of(cipher, struct ccp_context, ciph);
    ctx->ciph_allocated = false;
    ccp_common_free(ctx);
}

static void ccp_iv(ssh_cipher *cipher, const void *iv)
{
    /* struct ccp_context *ctx =
           container_of(cipher, struct ccp_context, ciph); */
    /* IV is set based on the sequence number */
}

static void ccp_key(ssh_cipher *cipher, const void *vkey)
{
    const unsigned char *key = (const unsigned char *)vkey;
    struct ccp_context *ctx = container_of(cipher, struct ccp_context, ciph);
    /* Initialise the a_cipher (for decrypting lengths) with the first 256 bits */
    chacha20_key(&ctx->a_cipher, key + 32);
    /* Initialise the b_cipher (for content and MAC) with the second 256 bits */
    chacha20_key(&ctx->b_cipher, key);
}

static void ccp_encrypt(ssh_cipher *cipher, void *blk, int len)
{
    struct ccp_context *ctx = container_of(cipher, struct ccp_context, ciph);
    chacha20_encrypt(&ctx->b_cipher, blk, len);
}

static void ccp_decrypt(ssh_cipher *cipher, void *blk, int len)
{
    struct ccp_context *ctx = container_of(cipher, struct ccp_context, ciph);
    chacha20_decrypt(&ctx->b_cipher, blk, len);
}

static void ccp_length_op(struct ccp_context *ctx, void *blk, int len,
                          unsigned long seq)
{
    unsigned char iv[8];
    /*
     * According to RFC 4253 (section 6.4), the packet sequence number wraps
     * at 2^32, so its 32 high-order bits will always be zero.
     */
    PUT_32BIT_LSB_FIRST(iv, 0);
    PUT_32BIT_LSB_FIRST(iv + 4, seq);
    chacha20_iv(&ctx->a_cipher, iv);
    chacha20_iv(&ctx->b_cipher, iv);
    /* Reset content block count to 1, as the first is the key for Poly1305 */
    ++ctx->b_cipher.state[12];
    smemclr(iv, sizeof(iv));
}

static void ccp_encrypt_length(ssh_cipher *cipher, void *blk, int len,
                               unsigned long seq)
{
    struct ccp_context *ctx = container_of(cipher, struct ccp_context, ciph);
    ccp_length_op(ctx, blk, len, seq);
    chacha20_encrypt(&ctx->a_cipher, blk, len);
}

static void ccp_decrypt_length(ssh_cipher *cipher, void *blk, int len,
                               unsigned long seq)
{
    struct ccp_context *ctx = container_of(cipher, struct ccp_context, ciph);
    ccp_length_op(ctx, blk, len, seq);
    chacha20_decrypt(&ctx->a_cipher, blk, len);
}

const ssh_cipheralg ssh2_chacha20_poly1305 = {
    /* .new = */ ccp_new,
    /* .free = */ ccp_free,
    /* .setiv = */ ccp_iv,
    /* .setkey = */ ccp_key,
    /* .encrypt = */ ccp_encrypt,
    /* .decrypt = */ ccp_decrypt,
    /* .encrypt_length = */ ccp_encrypt_length,
    /* .decrypt_length = */ ccp_decrypt_length,
    /* .next_message = */ nullcipher_next_message,
    /* .ssh2_id = */ "chacha20-poly1305@openssh.com",
    /* .blksize = */ 1,
    /* .real_keybits = */ 512,
    /* .padded_keybytes = */ 64,
    /* .flags = */ SSH_CIPHER_SEPARATE_LENGTH,
    /* .text_name = */ "ChaCha20",
    /* .required_mac = */ &ssh2_poly1305,
};

static const ssh_cipheralg *const ccp_list[] = {
    &ssh2_chacha20_poly1305
};

const ssh2_ciphers ssh2_ccp = { lenof(ccp_list), ccp_list };
