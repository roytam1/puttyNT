/*
 * Top-level vtables to select a SHA-512 implementation.
 */

#include <assert.h>
#include <stdlib.h>

#include "putty.h"
#include "ssh.h"
#include "sha512.h"

static const ssh_hashalg *const real_sha512_algs[] = {
    &ssh_sha512_sw,
    NULL,
};

static const ssh_hashalg *const real_sha384_algs[] = {
    &ssh_sha384_sw,
    NULL,
};

static ssh_hash *sha512_select(const ssh_hashalg *alg)
{
    const ssh_hashalg *const *real_algs =
        (const ssh_hashalg *const *)alg->extra;
    size_t i;

    for (i = 0; real_algs[i]; i++) {
        const ssh_hashalg *alg = real_algs[i];
        const struct sha512_extra *alg_extra =
            (const struct sha512_extra *)alg->extra;
        if (check_availability(alg_extra))
            return ssh_hash_new(alg);
    }

    /* We should never reach the NULL at the end of the list, because
     * the last non-NULL entry should be software-only SHA-512, which
     * is always available. */
    unreachable("sha512_select ran off the end of its list");
}

const ssh_hashalg ssh_sha512 = {
    /* .new = */ sha512_select,
    /* .reset = */ NULL,
    /* .copyfrom = */ NULL,
    /* .digest = */ NULL,
    /* .free = */ NULL,
    /* .hlen = */ 64,
    /* .blocklen = */ 128,
    /* .text_basename = */ "SHA-512", /* .annotation = */ "dummy selector vtable", /* .text_name = */ "SHA-512" " (" "dummy selector vtable" ")",
    /* .extra = */ real_sha512_algs,
};

const ssh_hashalg ssh_sha384 = {
    /* .new = */ sha512_select,
    /* .reset = */ NULL,
    /* .copyfrom = */ NULL,
    /* .digest = */ NULL,
    /* .free = */ NULL,
    /* .hlen = */ 48,
    /* .blocklen = */ 128,
    /* .text_basename = */ "SHA-384", /* .annotation = */ "dummy selector vtable", /* .text_name = */ "SHA-384" " (" "dummy selector vtable" ")",
    /* .extra = */ real_sha384_algs,
};
