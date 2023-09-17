/*
 * The Qubes OS Project, https://www.qubes-os.org
 *
 * Copyright (C) 2023  Demi Marie Obenour  <demi@invisiblethingslab.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef QUBES_UTIL_PURE_H
#define QUBES_UTIL_PURE_H QUBES_UTIL_PURE_H
#ifdef __cplusplus
extern "C" {
#else
#include <stdbool.h> // for bool
#endif
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if defined QUBES_EXPORT
# error QUBES_EXPORT already defined
#elif defined _WIN32 || defined __CYGWIN__
# ifdef _MSC_VER
#  define QUBES_EXPORT __declspec(dllexport)
#  define QUBES_IMPORT __declspec(dllimport)
# elif defined __GNUC__
#  define QUBES_EXPORT __attribute__(dllexport)
#  define QUBES_EXPORT __attribute__(dllimport)
# else
#  error unknown C compiler for Windows target
# endif
#elif defined __has_attribute
# if __has_attribute(visibility)
#  define QUBES_EXPORT __attribute__((visibility("default")))
#  define QUBES_IMPORT __attribute__((visibility("default")))
# elif defined __clang__
#  error clang always supports visibility
# endif
#elif defined __GNUC__ && __GNUC__ >= 4
# define QUBES_EXPORT __attribute__((visibility("default")))
# define QUBES_IMPORT __attribute__((visibility("default")))
#endif

#ifndef QUBES_EXPORT
# if (defined __clang__) || (defined __GNUC__ && __GNUC__ >= 4) || defined _MSC_VER
#  error This compiler should support visibility but does not
# endif
# define QUBES_EXPORT /* nothing */
# define QUBES_IMPORT /* nothing */
#endif

#ifdef QUBES_PURE_IMPLEMENTATION
# define QUBES_PURE_PUBLIC QUBES_EXPORT
#else
# define QUBES_PURE_PUBLIC QUBES_IMPORT
#endif

/**
 * A counted buffer used by some functions in this library.
 * Equivalent to Rust's &[T].
 */
struct QubesSlice {
    /// Pointer to the data
    const uint8_t *pointer;
    /// Length of the data
    size_t length;
} __attribute__((__may_alias__));

/** A mutable slice.  Equivalent to Rust's &mut [T]. */
struct QubesMutableSlice {
    /// Pointer to the data
    uint8_t *pointer;
    /// Length of the data
    size_t length;
} __attribute__((__may_alias__));

/**
 * Validate that a string is a valid path and will not result in
 * directory traversal if used as such.  Characters that are unsafe for
 * filenames are also rejected, including invalid UTF-8 sequences and
 * all control characters.  The input must be NUL-terminated.
 *
 * Returns true on success and false on failure.
 */
QUBES_PURE_PUBLIC bool
qubes_pure_validate_file_name(const uint8_t *untrusted_filename);

/**
 * Validate that `untrusted_name` is a valid symbolic link name
 * and that creating a symbolic link with that name and target
 * `untrusted_target` is also safe.  Characters that are unsafe for
 * filenames are also rejected, including invalid UTF-8
 * sequences and all control characters.  The input must be
 * NUL-terminated.
 *
 * Returns true on success and false on failure.
 */
QUBES_PURE_PUBLIC bool
qubes_pure_validate_symbolic_link(const uint8_t *untrusted_name,
                                  const uint8_t *untrusted_target);

/**
 * Validate that `code_point` is safe to display.  To be considered safe to
 * display, a code point must be valid and not be a control character.
 * Additionally, the code point must not be a character that is complex
 * to render and thus significantly increases the attack surface of text
 * rendering libraries such as Fribidi, Harfbuzz, or Pango.  The set of
 * characters that are considered complex to render is implementation
 * dependent and may change in future versions of this library.  Currenty,
 * it includes the following:
 *
 * - Characters that have a character direction property other than
 *   WHITE_SPACE_NEUTRAL, OTHRE_NEUTRAL, EUROPEAN_NUMBER_TERMINATOR,
 *   EUROPEAN_NUMBER_SEPARATOR, COMMON_NUMBER_SEPARATOR, or LEFT_TO_RIGHT.
 *
 * - Characters that are part of scripts that are not recommended for use
 *   in identifiers.  This includes limited-use scripts.
 *
 * - Characters with scripts that are not INHERITED, CYRILLIC, GREEK, LATIN,
 *   BRAILLE, SIMPLIFIED_HAN, TRADITIONAL_HAN, HAN, HAN_WITH_BOPOMOFO, JAMO,
 *   HANGUL, BOPOMOFO, KATAKANA_OR_HIRAGANA, HIRIGANA, KATAKANA, JAPANESE,
 *   KOREAN, or COMMON.
 *
 * This is implemented as an allowlist, not as a blocklist, so unknown code
 * points are considered _unsafe_ for display.
 *
 * @param code_point The code point to check for being safe to display.
 *
 * This API does _not_ require that @p code_point is a valid Unicode code point.
 * Invalid code points are simply considered to be unsafe for display.
 * Therefore, this function has defined behavior for _all_ inputs.
 */
QUBES_PURE_PUBLIC bool
qubes_pure_code_point_safe_for_display(uint32_t code_point);

/**
 * Validate that `untrusted_str` is safe to display.  To be considered safe to
 * display, a string must be valid UTF-8 and contain no control characters
 * except perhaps newline.  The string must also contain no characters that
 * are considered unsafe for display by qubes_pure_code_point_safe_for_display().
 */
QUBES_PURE_PUBLIC bool
qubes_pure_string_safe_for_display(const char *untrusted_str,
                                   size_t line_length);

/** Initialize a QubesSlice from a nul-terminated string. */
static inline struct QubesSlice
qubes_pure_buffer_init_from_nul_terminated_string(const char *str)
{
    if (str == NULL)
        abort();
    return (struct QubesSlice) {
        .pointer = (const uint8_t *)str,
        .length = strlen(str),
    };
}

/** Minimum length of a Qube name */
#define QUBES_PURE_MIN_QUBE_NAME_LEN 1

/** Maximum length of a Qube name */
#define QUBES_PURE_MAX_QUBE_NAME_LEN 31

/**
 * Errors that can occur when validating a qube name.
 */
enum QubeNameValidationError {
    /// Qube name is OK (not an error).
    QUBE_NAME_OK = 0,
    /// Name is empty.
    QUBE_NAME_EMPTY = -1,
    /// Name is more than QUBES_PURE_MAX_QUBE_NAME_LEN bytes.
    QUBE_NAME_TOO_LONG = -2,
    /// Name does not start with an ASCII letter.
    QUBE_NAME_INVALID_FIRST_CHARACTER = -3,
    /// Invalid character in name.
    QUBE_NAME_INVALID_SUBSEQUENT_CHARACTER = -4,
    /// Name is `none`, `default`, `Domain-0`, or ends in `-dm`.
    /// These names are reserved.
    QUBE_NAME_RESERVED = -6,
};

/**
 * Validate that `untrusted_str` is a valid qube name.  A valid qube name
 * must:
 *
 * - Have a length between 1 and 31 bytes (inclusive).
 * - Consist only of characters matching the glob pattern [A-Za-z0-9_.-].
 * - Begin with an uppercase or lowercase ASCII letter.
 * - Not end with the 3 bytes `-dm`, to avoid confusing with device-model
 *   stubdomains.
 * - Not be `none` or `default`, which are reserved for the admin API.
 * - Not be `Domain-0`, which is used by libvirt and libxl to refer to dom0.
 *
 * Returns QUBE_NAME_OK (0) on success and something else on failure.
 */
QUBES_PURE_PUBLIC enum QubeNameValidationError
qubes_pure_is_valid_qube_name(const struct QubesSlice untrusted_str);

#ifdef __cplusplus
}
#endif
#endif // !defined QUBES_UTIL_PURE_H
