#define U_HIDE_DEPRECATED_API U_HIDE_DEPRECATED_API

#include "pure.h"

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>
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

// Statically assert that a statement is not reachable.
//
// At runtime, this is just abort(), but it comes with a neat trick:
// if optimizations are on, CHECK_UNREACHABLE is defined, and the compiler
// claims to be GNU-compatible, the compiler must prove that this is
// unreachable.  Otherwise, it is a compile-time error.
//
// To enable static checking of this macro, pass CHECK_UNREACHABLE=1 to the
// makefile or include it in the environment.
#if defined __GNUC__ && defined __OPTIMIZE__ && defined CHECK_UNREACHABLE
#define COMPILETIME_UNREACHABLE do {    \
    extern void not_reachable(void)     \
        __attribute__((                 \
            error("Compiler could not prove that this statement is not reachable"), \
            noreturn));                 \
    not_reachable();                    \
} while (0)
#else
#define COMPILETIME_UNREACHABLE do {    \
    assert(0);                          \
    abort();                            \
} while (0)
#endif

// This is one of the trickiest, most security-critical functions in the
// whole repository (opendir_safe() in unpack.c is the other).  It is critical
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
// '/', "./", or ".\0" indicate a non-canonical path.  These are currently
// rejected, but they could safely be accepted in the future without allowing
// directory traversal attacks.  "../" and "..\0" are ".." components: the code
// checks that the limit on non-".." components has not been exceeded, fails if
// it has, and otherwise decrements the limit.  This ensures that a directory
// tree cannot contain symlinks that point outside of the tree itself.
// Anything else is a normal path component: the limit on ".." components
// is set to zero, and the number of non-".." components is incremented.
//
// The return value is the number of non-".." components on
// success, or a negative errno value on failure.  The return value might be
// zero.
static ssize_t validate_path(const uint8_t *const untrusted_name,
                             size_t allowed_leading_dotdot,
                             const uint32_t flags)
{
    // We assume that there are not SSIZE_MAX path components.
    // This cannot happen on hardware using a flat address space,
    // as this would require SIZE_MAX bytes in the path and leave
    // no space for the executable code.
    ssize_t non_dotdot_components = 0;
    bool const allow_non_canonical = (flags & QUBES_PURE_ALLOW_NON_CANONICAL_PATHS);
    if (untrusted_name[0] == '\0')
        return allow_non_canonical ? 0 : -ENOLINK; // empty path
    if (untrusted_name[0] == '/')
        return -ENOLINK; // absolute path
    size_t i;
    for (i = 0; untrusted_name[i]; i++) {
        if (i == 0 || untrusted_name[i - 1] == '/') {
            // Start of a path component
            switch (untrusted_name[i]) {
            case '\0': // impossible, loop exit condition & if statement before
                       // loop check this
                COMPILETIME_UNREACHABLE;
            case '/': // repeated slash
                if (allow_non_canonical)
                    continue;
                return -EILSEQ;
            case '.':
                if (untrusted_name[i + 1] == '\0' || untrusted_name[i + 1] == '/') {
                    // Path component is "."
                    if (allow_non_canonical)
                        continue;
                    return -EILSEQ;
                }
                if ((untrusted_name[i + 1] == '.') &&
                    (untrusted_name[i + 2] == '\0' || untrusted_name[i + 2] == '/')) {
                    /* Check if the limit on leading ".." components has been exceeded */
                    if (allowed_leading_dotdot < 1)
                        return -ENOLINK;
                    allowed_leading_dotdot--;
                    i++; // loop will advance past second "."
                    continue;
                }
                __attribute__((fallthrough));
            default:
                allowed_leading_dotdot = 0; // do not allow further ".." components
                non_dotdot_components++;
                break;
            }
        }
        if (untrusted_name[i] == 0) {
            // If this is violated, the subsequent i++ will be out of bounds
            COMPILETIME_UNREACHABLE;
        } else if ((0x20 <= untrusted_name[i] && untrusted_name[i] <= 0x7E) ||
                   (flags & QUBES_PURE_ALLOW_UNSAFE_CHARACTERS) != 0) {
            /* loop will advance past this */
        } else {
            int utf8_ret = validate_utf8_char((const unsigned char *)(untrusted_name + i));
            if (utf8_ret > 0) {
                i += (size_t)(utf8_ret - 1); /* loop will do one more increment */
            } else {
                return -EILSEQ;
            }
        }
    }
    if (i < 1 || untrusted_name[i]) {
        // ideally this would be COMPILETIME_UNREACHABLE but GCC can't prove this
        assert(0);
        return -EILSEQ;
    }
    if ((flags & QUBES_PURE_ALLOW_TRAILING_SLASH) == 0 &&
            untrusted_name[i - 1] == '/')
        return -EILSEQ;
    return non_dotdot_components;
}

static bool flag_check(const uint32_t flags)
{
    int const allowed = (QUBES_PURE_ALLOW_UNSAFE_CHARACTERS |
                         QUBES_PURE_ALLOW_NON_CANONICAL_SYMLINKS |
                         QUBES_PURE_ALLOW_NON_CANONICAL_PATHS |
                         QUBES_PURE_ALLOW_TRAILING_SLASH |
                         QUBES_PURE_ALLOW_UNSAFE_SYMLINKS);
    return (flags & ~(__typeof__(flags))allowed) == 0;
}

QUBES_PURE_PUBLIC int
qubes_pure_validate_file_name_v2(const uint8_t *const untrusted_filename,
                                 const uint32_t flags)
{
    if (!flag_check(flags))
        return -EINVAL;
    // We require at least one non-".." component in the path.
    ssize_t res = validate_path(untrusted_filename, 0, flags);
    // Always return -EILSEQ, since -ENOLINK only makes sense for symlinks
    return res > 0 ? 0 : -EILSEQ;
}

QUBES_PURE_PUBLIC bool
qubes_pure_validate_file_name(const uint8_t *const untrusted_filename)
{
    return qubes_pure_validate_file_name_v2(untrusted_filename,
                                            QUBES_PURE_ALLOW_TRAILING_SLASH) == 0;
}

QUBES_PURE_PUBLIC int
qubes_pure_validate_symbolic_link_v2(const uint8_t *untrusted_name,
                                     const uint8_t *untrusted_target,
                                     uint32_t flags)
{
    if (!flag_check(flags))
        return -EINVAL;
    ssize_t depth = validate_path(untrusted_name, 0, flags);
    if (depth < 0)
        return -EILSEQ; // -ENOLINK is only for symlinks
    if ((flags & QUBES_PURE_ALLOW_UNSAFE_SYMLINKS) != 0)
        return depth > 0 ? 0 : -ENOLINK;
    if ((flags & QUBES_PURE_ALLOW_NON_CANONICAL_SYMLINKS) != 0)
        flags |= QUBES_PURE_ALLOW_NON_CANONICAL_PATHS;
    // Symlink paths must have at least 2 components: "a/b" is okay
    // but "a" is not.  This ensures that the toplevel "a" entry
    // is not a symbolic link.
    if (depth < 2)
        return -ENOLINK;
    // Symlinks must have at least 2 more path components in the name
    // than the number of leading ".." path elements in the target.
    // "a/b" can point to "c" (which resolves to "a/c") but not "../c"
    // (which resolves to "c").  Similarly and "a/b/c" can point to "../d"
    // (which resolves to "a/d") but not "../../d" (which resolves to "d").
    // This ensures that ~/QubesIncoming/QUBENAME/a/b cannot point outside
    // of ~/QubesIncoming/QUBENAME/a.  Always allow trailing slash in the
    // symbolic link target, whether or not they are allowed in the path.
    ssize_t res = validate_path(untrusted_target, (size_t)(depth - 2),
                                flags | QUBES_PURE_ALLOW_TRAILING_SLASH);
    return res < 0 ? res : 0;
}

QUBES_PURE_PUBLIC bool
qubes_pure_validate_symbolic_link(const uint8_t *untrusted_name,
                                  const uint8_t *untrusted_target)
{
    return qubes_pure_validate_symbolic_link_v2(untrusted_name, untrusted_target,
                                                QUBES_PURE_ALLOW_TRAILING_SLASH) == 0;
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
