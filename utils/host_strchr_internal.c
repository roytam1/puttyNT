/*
 * Find a character in a string, unless it's a colon contained within
 * square brackets. Used for untangling strings of the form
 * 'host:port', where host can be an IPv6 literal.
 *
 * This internal function provides an API that's a bit like strchr (in
 * that it returns a pointer to the character it found, or NULL), and
 * a bit like strcspn (in that you can give it a set of characters to
 * look for, not just one). Also it has an option to return the first
 * or last character it finds. Other functions in the utils directory
 * provide wrappers on it with APIs more like familiar <string.h>
 * functions.
 */

#include <string.h>

#include "defs.h"
#include "misc.h"
#include "utils/utils.h"

const char *host_strchr_internal(const char *s, const char *set, bool first)
{
    int brackets = 0;
    const char *ret = NULL;

    while (1) {
        if (!*s)
            return ret;

        if (*s == '[')
            brackets++;
        else if (*s == ']' && brackets > 0)
            brackets--;
        else if (brackets && *s == ':')
            /* never match */ ;
        else if (strchr(set, *s)) {
            ret = s;
            if (first)
                return ret;
        }

        s++;
    }
}

