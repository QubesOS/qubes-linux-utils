#define U_HIDE_DEPRECATED_API U_HIDE_DEPRECATED_API

#include "pure.h"

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <assert.h>

QUBES_PURE_PUBLIC bool
qubes_pure_code_point_safe_for_display(uint32_t code_point) {
    switch (code_point) {
#include "unicode-allowlist-table.c"
        return true;
    default:
        return false;
    }
}

/* validate single UTF-8 character
 * return bytes count of this character, or 0 if the character is invalid */
static int validate_utf8_char(const uint8_t *untrusted_c) {
    int tails_count = 0;
    int total_size = 0;
    uint32_t code_point;
    /* it is safe to access byte pointed by the parameter,
     * but every next byte can access only if previous byte was not NUL.
     */

    /* According to http://www.ietf.org/rfc/rfc3629.txt:
     *   UTF8-char   = UTF8-1 / UTF8-2 / UTF8-3 / UTF8-4
     *   UTF8-1      = %x00-7F
     *   UTF8-2      = %xC2-DF UTF8-tail
     *   UTF8-3      = %xE0 %xA0-BF UTF8-tail / %xE1-EC 2( UTF8-tail ) /
     *                 %xED %x80-9F UTF8-tail / %xEE-EF 2( UTF8-tail )
     *   UTF8-4      = %xF0 %x90-BF 2( UTF8-tail ) / %xF1-F3 3( UTF8-tail ) /
     *                 %xF4 %x80-8F 2( UTF8-tail )
     *   UTF8-tail   = %x80-BF
     *
     * This code uses a slightly different grammar:
     *
     *   UTF8-char   = UTF8-1 / UTF8-2 / UTF8-3 / UTF8-4
     *   UTF8-1      = %x20-7F
     *   UTF8-2      = %xC2-DF UTF8-tail
     *   UTF8-3      = %xE0 %xA0-BF UTF8-tail / %xE1-EF 2( UTF8-tail )
     *   UTF8-4      = %xF0 %x90-BF 2( UTF8-tail ) / %xF1-F4 3( UTF8-tail )
     *   UTF8-tail   = %x80-BF
     *
     * The differences are:
     *
     * - ASCII control characters are rejected, allowing a fast path for other
     *   ASCII characters.
     * - Surrogates and some values above 0x10FFFF are accepted here, but are
     *   rejected as forbidden code points later.
     */
    switch (*untrusted_c) {
        case 0xC2 ... 0xDF:
            total_size = 2;
            tails_count = 1;
            code_point = *untrusted_c & 0x1F;
            break;
        case 0xE0:
            untrusted_c++;
            total_size = 3;
            if (*untrusted_c >= 0xA0 && *untrusted_c <= 0xBF)
                tails_count = 1;
            else
                return 0;
            code_point = *untrusted_c & 0x3F;
            break;
        case 0xE1 ... 0xEF:
            total_size = 3;
            tails_count = 2;
            code_point = *untrusted_c & 0xF;
            break;
        case 0xF0:
            untrusted_c++;
            total_size = 4;
            if (*untrusted_c >= 0x90 && *untrusted_c <= 0xBF)
                tails_count = 2;
            else
                return 0;
            code_point = *untrusted_c & 0x3F;
            break;
        case 0xF1 ... 0xF4:
            total_size = 4;
            tails_count = 3;
            code_point = *untrusted_c & 0x7;
            break;
        default:
            return 0; // control ASCII or invalid UTF-8
    }

    while (tails_count-- > 0) {
        untrusted_c++;
        if (!(*untrusted_c >= 0x80 && *untrusted_c <= 0xBF))
            return 0;
        code_point = code_point << 6 | (*untrusted_c & 0x3F);
    }

    return qubes_pure_code_point_safe_for_display(code_point) ? total_size : 0;
}

// This is one of the trickiest, security-critical functions in the whole
// repository (opendir_safe() in unpack.c is the other).  It is critical
// for preventing directory traversal attacks.  The code does use a chroot()
// and a bind mount, but the bind mount is not always effective if mount
// namespaces are in use, and the chroot can be bypassed (QSB-015).
//
// Preconditions:
//
// - untrusted_name is NUL-terminated.
// - allowed_leading_dotdot is the maximum number of leading "../" sequences
//   allowed.  Might be 0.
//
// Algorithm:
//
// At the start of the loop and after '/', the code checks for '/' and '.'.
// '/', "./", or ".\0" indicate a non-canonical path: the code skips past them
// if allow_non_canonical is set, and fails otherwise.  "../" and "..\0" are
// ".." components: the code checks that the limit on non-".." components has
// not been exceeded, fails if it has, and otherwise decrements the limit.
// Anything else is a normal path component: the limit on ".." components
// is set to zero, and the number of non-".." components is incremented.
//
// The return value is the number of non-".." components on
// success, or -1 on failure.
static ssize_t validate_path(const uint8_t *const untrusted_name, size_t allowed_leading_dotdot)
{
    // We assume that there are not SSIZE_MAX path components.
    // This cannot happen on hardware using a flat address space,
    // as this would require SIZE_MAX bytes in the path and leave
    // no space for the executable code.
    ssize_t non_dotdot_components = 0;
    size_t i = 0;
    do {
        if (i == 0 || untrusted_name[i - 1] == '/') {
            switch (untrusted_name[i]) {
            case '/': // repeated or initial slash
            case '\0': // trailing slash or empty string
                return -1;
            case '.':
                if (untrusted_name[i + 1] == '\0' || untrusted_name[i + 1] == '/')
                    return -1;
                if ((untrusted_name[i + 1] == '.') &&
                    (untrusted_name[i + 2] == '\0' || untrusted_name[i + 2] == '/')) {
                    /* Check if the limit on leading ".." components has been exceeded */
                    if (allowed_leading_dotdot < 1)
                        return -1;
                    allowed_leading_dotdot--;
                    i += 2; // advance past ".."
                    continue;
                }
                __attribute__((fallthrough));
            default:
                allowed_leading_dotdot = 0; // do not allow further ".." components
                non_dotdot_components++;
                break;
            }
        }
        if (untrusted_name[i] >= 0x20 && untrusted_name[i] <= 0x7E) {
            i++;
        } else {
            int utf8_ret = validate_utf8_char((const unsigned char *)(untrusted_name + i));
            if (utf8_ret > 0) {
                i += utf8_ret;
            } else {
                return -1;
            }
        }
    } while (untrusted_name[i]);
    return non_dotdot_components;
}

QUBES_PURE_PUBLIC bool
qubes_pure_validate_file_name(const uint8_t *untrusted_filename)
{
    return validate_path(untrusted_filename, 0) > 0;
}

QUBES_PURE_PUBLIC bool
qubes_pure_validate_symbolic_link(const uint8_t *untrusted_name,
                                  const uint8_t *untrusted_target)
{
    ssize_t depth = validate_path(untrusted_name, 0);
    // Symlink paths must have at least 2 components: "a/b" is okay
    // but "a" is not.  This ensures that the toplevel "a" entry
    // is not a symbolic link.
    if (depth < 2)
        return false;
    // Symlinks must have at least 2 more path components in the name
    // than the number of leading ".." path elements in the target.
    // "a/b" can point to "c" (which resolves to "a/c") but not "../c"
    // (which resolves to "c").  Similarly and "a/b/c" can point to "../d"
    // (which resolves to "a/d") but not "../../d" (which resolves to "d").
    // This ensures that ~/QubesIncoming/QUBENAME/a/b cannot point outside
    // of ~/QubesIncoming/QUBENAME/a.
    return validate_path(untrusted_target, (size_t)(depth - 2)) >= 0;
}

QUBES_PURE_PUBLIC bool
qubes_pure_string_safe_for_display(const char *untrusted_str, size_t line_length)
{
    assert(line_length == 0 && "Not yet implemented: nonzero line length");
    size_t i = 0;
    do {
        if (untrusted_str[i] >= 0x20 && untrusted_str[i] <= 0x7E) {
            i++;
        } else {
            int utf8_ret = validate_utf8_char((const uint8_t *)(untrusted_str + i));
            if (utf8_ret > 0) {
                i += utf8_ret;
            } else {
                return false;
            }
        }
    } while (untrusted_str[i]);
    return true;
}
