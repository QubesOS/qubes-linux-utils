#include <stdint.h>
#include "pure.h"

QUBES_PURE_PUBLIC enum QubeNameValidationError
qubes_pure_is_valid_qube_name(const struct QubesSlice untrusted_name)
{
    /* Check the length first. */
    if (untrusted_name.length < QUBES_PURE_MIN_QUBE_NAME_LEN)
        return QUBE_NAME_EMPTY;
    if (untrusted_name.length > QUBES_PURE_MAX_QUBE_NAME_LEN)
        return QUBE_NAME_TOO_LONG;
    size_t const length = untrusted_name.length;
    switch (untrusted_name.pointer[0]) {
    case 'a' ... 'z':
    case 'A' ... 'Z':
        break;
    default:
        return QUBE_NAME_INVALID_FIRST_CHARACTER; /* bad first character */
    }

    /* Check all other characters */
    for (size_t i = 1; i < length; ++i) {
        switch (untrusted_name.pointer[i]) {
        case 'a' ... 'z':
        case 'A' ... 'Z':
        case '0' ... '9':
        case '_':
        case '.':
        case '-':
            break;
        default:
            /* Bad character */
            return QUBE_NAME_INVALID_SUBSEQUENT_CHARACTER;
        }
    }

    /*
     * All special cases are for names with at least 4 bytes,
     * except for the literal string "-dm" which is already rejected
     * due to beginning with "-".  Therefore, any string of length
     * 3 or less that has not already been rejected is valid.
     */
    if (length < 4)
        return QUBE_NAME_OK;

    /*
     * A name ending in "-dm" could be the name of a stubdomain, so it is
     * disallowed.
     */
    if (memcmp(untrusted_name.pointer + (length - 3), "-dm", 3) == 0)
        return QUBE_NAME_RESERVED;

    switch (length) {
    case 4:
        /* "none" is reserved for use by the Admin API. */
        return memcmp(untrusted_name.pointer, "none", 4) == 0 ?
            QUBE_NAME_RESERVED : QUBE_NAME_OK;
    case 7:
        /* "default" is reserved for use by the Admin API. */
        return memcmp(untrusted_name.pointer, "default", 7) == 0 ?
            QUBE_NAME_RESERVED : QUBE_NAME_OK;
    case 8:
        /* "Domain-0" is used by libxl to refer to dom0. */
        return memcmp(untrusted_name.pointer, "Domain-0", 8) == 0 ?
            QUBE_NAME_RESERVED : QUBE_NAME_OK;
    default:
        /* Anything else is valid! */
        return QUBE_NAME_OK;
    }
}
