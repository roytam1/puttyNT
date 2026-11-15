/*
 * Division and decimal conversion for the uint64 type (a 64-bit
 * unsigned integer held as two 32-bit halves, see defs.h), which
 * misc.h's inline functions don't cover. Not for secret data.
 */

#include <assert.h>

#include "defs.h"
#include "misc.h"

/*
 * Divide x by d, which must be from 1 to 65535. Returns the quotient,
 * and stores the remainder in *rem if rem is not NULL.
 */
uint64 uint64_divmod(uint64 x, unsigned d, unsigned *rem)
{
    uint32_t digits[4], r;
    unsigned i;

    assert(d >= 1 && d <= 0xFFFF);

    /* Long division, in base 2^16. r < d, so (r << 16) fits in 32
     * bits, and each quotient digit is less than 2^16. */
    digits[0] = x.hi >> 16;
    digits[1] = x.hi & 0xFFFF;
    digits[2] = x.lo >> 16;
    digits[3] = x.lo & 0xFFFF;
    r = 0;
    for (i = 0; i < 4; i++) {
        uint32_t cur = (r << 16) | digits[i];
        digits[i] = cur / d;
        r = cur % d;
    }

    if (rem)
        *rem = r;
    return uint64_make((digits[0] << 16) | digits[1],
                       (digits[2] << 16) | digits[3]);
}

/*
 * Write x in decimal into buf, which must have room for
 * UINT64_DECIMAL_SIZE characters. Returns buf.
 */
char *uint64_decimal(uint64 x, char *buf)
{
    char tmp[UINT64_DECIMAL_SIZE];
    size_t n = 0, i;
    unsigned digit;

    do {
        x = uint64_divmod(x, 10, &digit);
        tmp[n++] = '0' + digit;
    } while (x.hi != 0 || x.lo != 0);

    for (i = 0; i < n; i++)
        buf[i] = tmp[n - 1 - i];
    buf[n] = '\0';
    return buf;
}

/*
 * Read the decimal number at the start of s (digits only: no sign or
 * spaces) into *out. Returns how many characters it read, or 0 if s
 * doesn't start with a digit or the number doesn't fit in 64 bits.
 */
size_t uint64_from_decimal(const char *s, uint64 *out)
{
    uint64 x = uint64_make(0, 0), ten_x;
    size_t n;

    for (n = 0; s[n] >= '0' && s[n] <= '9'; n++) {
        /* x = x*10 + digit, failing if that needs more than 64 bits */
        if (x.hi > 0xFFFFFFFF / 10)
            return 0;
        ten_x = uint64_mul32(x.lo, 10);
        if (ten_x.hi > 0xFFFFFFFF - x.hi * 10)
            return 0;
        ten_x.hi += x.hi * 10;
        x = uint64_add32(ten_x, s[n] - '0');
        if (uint64_cmp(x, ten_x) < 0)
            return 0;
    }

    if (n > 0)
        *out = x;
    return n;
}
