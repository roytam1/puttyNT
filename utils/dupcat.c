/*
 * dupcat() in misc.h.
 *
 * This function is called with an arbitrary number of 'const char *'
 * parameters, of which the last one is a null pointer. (NT port:
 * upstream has a variadic wrapper macro that adds the null pointer.
 * The compilers of 1993-1996 have no variadic macros, so callers
 * pass it themselves.)
 */

#include <string.h>
#include <stdarg.h>

#include "defs.h"
#include "misc.h"

char *dupcat(const char *s1, ...)
{
    int len;
    char *p, *q, *sn;
    va_list ap;

    len = strlen(s1);
    va_start(ap, s1);
    while (1) {
        sn = va_arg(ap, char *);
        if (!sn)
            break;
        len += strlen(sn);
    }
    va_end(ap);

    p = snewn(len + 1, char);
    strcpy(p, s1);
    q = p + strlen(p);

    va_start(ap, s1);
    while (1) {
        sn = va_arg(ap, char *);
        if (!sn)
            break;
        strcpy(q, sn);
        q += strlen(q);
    }
    va_end(ap);

    return p;
}

