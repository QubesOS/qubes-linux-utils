#include <errno.h>
#include <limits.h>
#include "pure.h"

QUBES_PURE_PUBLIC int
qubes_pure_parse_nonneg_ll(const char *untrusted_str, long long *out)
{
    long long value = 0;
    bool overflow = false;

    /* Reject empty string. */
    if (untrusted_str[0] == '\0')
        return -EINVAL;

    for (const char *p = untrusted_str; *p != '\0'; ++p) {
        /* Sign, whitespace, or any other non-digit character. */
        if (*p < '0' || *p > '9')
            return -EINVAL;
        if (overflow)
            continue;
        int digit = *p - '0';
        /*
         * Keep checking even on overflow, since the
         * overflowing part could be invalid
         */
        if (value > (LLONG_MAX - digit) / 10)
            overflow = true;
        else
            value = value * 10 + digit;
    }

    if (overflow)
        return -ERANGE;
    *out = value;
    return 0;
}
