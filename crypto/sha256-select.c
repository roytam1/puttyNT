/*
 * Top-level vtables to select a SHA-256 implementation.
 */

#include <assert.h>
#include <stdlib.h>

#include "putty.h"
#include "ssh.h"
#include "sha256.h"

static ssh_hash *sha256_select(const ssh_hashalg *alg)
{
    static const ssh_hashalg *const real_algs[] = {
        &ssh_sha256_sw,
        NULL,
    };
    size_t i;

    for (i = 0; real_algs[i]; i++) {
        const ssh_hashalg *alg = real_algs[i];
        const struct sha256_extra *alg_extra =
            (const struct sha256_extra *)alg->extra;
        if (check_availability(alg_extra))
            return ssh_hash_new(alg);
    }

    /* We should never reach the NULL at the end of the list, because
     * the last non-NULL entry should be software-only SHA-256, which
     * is always available. */
    unreachable("sha256_select ran off the end of its list");
}

const ssh_hashalg ssh_sha256 = {
    /* .new = */ sha256_select,
    /* .reset = */ NULL,
    /* .copyfrom = */ NULL,
    /* .digest = */ NULL,
    /* .free = */ NULL,
    /* .hlen = */ 32,
    /* .blocklen = */ 64,
    /* .text_basename = */ "SHA-256", /* .annotation = */ "dummy selector vtable", /* .text_name = */ "SHA-256" " (" "dummy selector vtable" ")",
};
