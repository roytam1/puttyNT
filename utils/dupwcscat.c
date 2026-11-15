/*
 * dupwcscat() in misc.h.
 *
 * This function is called with an arbitrary number of 'const wchar_t
 * *' parameters, of which the last one is a null pointer. (NT port:
 * upstream has a variadic wrapper macro that adds the null pointer.
 * The compilers of 1993-1996 have no variadic macros, so callers
 * pass it themselves.)
 */

#include <stdarg.h>
#include <wchar.h>

#include "defs.h"
#include "misc.h"

wchar_t *dupwcscat(const wchar_t *s1, ...)
{
    int len;
    wchar_t *p, *q, *sn;
    va_list ap;

    len = wcslen(s1);
    va_start(ap, s1);
    while (1) {
        sn = va_arg(ap, wchar_t *);
        if (!sn)
            break;
        len += wcslen(sn);
    }
    va_end(ap);

    p = snewn(len + 1, wchar_t);
    wcscpy(p, s1);
    q = p + wcslen(p);

    va_start(ap, s1);
    while (1) {
        sn = va_arg(ap, wchar_t *);
        if (!sn)
            break;
        wcscpy(q, sn);
        q += wcslen(q);
    }
    va_end(ap);

    return p;
}

