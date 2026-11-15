/*
 * smallprimes.c: implementation of the array of small primes defined
 * in sshkeygen.h.
 */

#include <assert.h>
#include "ssh.h"
#include "sshkeygen.h"

/* The real array that stores the primes. It has to be writable in
 * this module, but outside this module, we only expose the
 * const-qualified pointer 'smallprimes' so that nobody else can
 * accidentally overwrite it. */
static unsigned short smallprimes_array[NSMALLPRIMES];

const unsigned short *const smallprimes = smallprimes_array;

void init_smallprimes(void)
{
    bool A[65536];
    size_t pos;
    size_t i;
    if (smallprimes_array[0])
        return;                        /* already done */


    for (i = 2; i < lenof(A); i++)
        A[i] = true;

    for (i = 2; i < lenof(A); i++) {
        size_t j;
        if (!A[i])
            continue;
        for (j = 2*i; j < lenof(A); j += i)
            A[j] = false;
    }

    pos = 0;
    for (i = 2; i < lenof(A); i++) {
        if (A[i]) {
            assert(pos < NSMALLPRIMES);
            smallprimes_array[pos++] = i;
        }
    }

    assert(pos == NSMALLPRIMES);
}
