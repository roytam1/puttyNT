/*
 * Decode a single UTF-8 character.
 */

#include "putty.h"
#include "misc.h"

unsigned decode_utf8(BinarySource *src, DecodeUTF8Failure *err)
{
    /* Permit user to pass NULL as the err pointer */
    DecodeUTF8Failure dummy;
    unsigned char c;
    unsigned long wc;
    unsigned long min;
    size_t ncont;
    if (!err) err = &dummy;

    /* If the source has no byte available, this will return 0, which
     * we'll return immediately and is a reasonable error return anyway */
    c = get_byte(src);

    /* One-byte cases. */
    if (c < 0x80) {
        *err = DUTF8_SUCCESS;
        return c;
    } else if (c < 0xC0) {
        *err = DUTF8_SPURIOUS_CONTINUATION;
        return 0xFFFD;
    }

    if (c < 0xE0) {
        wc = c & 0x1F; ncont = 1; min = 0x80;
    } else if (c < 0xF0) {
        wc = c & 0x0F; ncont = 2; min = 0x800;
    } else if (c < 0xF8) {
        wc = c & 0x07; ncont = 3; min = 0x10000;
    } else if (c < 0xFC) {
        wc = c & 0x03; ncont = 4; min = 0x200000;
    } else if (c < 0xFE) {
        wc = c & 0x01; ncont = 5; min = 0x4000000;
    } else {
        *err = DUTF8_ILLEGAL_BYTE; /* FE or FF */
        return 0xFFFD;
    }

    while (ncont-- > 0) {
        unsigned char cont;
        if (!get_avail(src)) {
            *err = DUTF8_E_OUT_OF_DATA;
            return 0xFFFD;
        }
        cont = get_byte(src);
        if (!(0x80 <= cont && cont < 0xC0)) {
            BinarySource_REWIND_TO(src, src->pos - 1);
            *err = DUTF8_TRUNCATED_SEQUENCE;
            return 0xFFFD;
        }

        wc = (wc << 6) | (cont & 0x3F);
    }

    if (wc < min) {
        *err = DUTF8_OVERLONG_ENCODING;
        return 0xFFFD;
    }
    if (0xD800 <= wc && wc < 0xE000) {
        *err = DUTF8_ENCODED_SURROGATE;
        return 0xFFFD;
    }
    if (wc > 0x10FFFF) {
        *err = DUTF8_CODE_POINT_TOO_BIG;
        return 0xFFFD;                 /* outside Unicode range */
    }
    *err = DUTF8_SUCCESS;
    return wc;
}

const char *const decode_utf8_error_strings[DUTF8_N_FAILURE_CODES] = {
    #define MSG_ENTRY(sym, string) string,
    DECODE_UTF8_FAILURE_LIST(MSG_ENTRY)
    #undef MSG_ENTRY
};

