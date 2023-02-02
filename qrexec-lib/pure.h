#ifndef QUBES_UTIL_PURE_H
#define QUBES_UTIL_PURE_H QUBES_UTIL_PURE_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

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
    const uint8_t *untrusted_pointer;
    /// Length of the data
    size_t length;
} __attribute__((may_alias));

/** A mutable slice.  Equivalent to Rust's &mut [T]. */
struct QubesMutableSlice {
    /// Pointer to the data
    uint8_t *untrusted_pointer;
    /// Length of the data
    size_t length;
} __attribute__((may_alias));

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
 * Validate that `untrusted_str` is safe to display.  To be considered safe to
 * display, a string must be valid UTF-8 and contain no control characters
 * except perhaps newline.  The string must also contain no characters that
 * are complex to render and thus significantly increase the attack surface
 * of text rendering libraries such as Fribidi, Harfbuzz, or Pango.  The set
 * of characters that are considered complex to render is implementation
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
 * \param untrusted_str The string to be validated.  Must be NUL-terminated
 * but is otherwise untrusted.
 *
 * \param line_length The maximum length of a line.  If zero, no line breaks
 * are allowed.  Otherwise, at most line_length characters can occur without
 * an intervening newline, and a trailing newline is required.
 *
 * \note Currently, nonzero `line_length` is not implemented and will cause
 * an assertion failure.
 */
QUBES_PURE_PUBLIC bool
qubes_pure_check_string_safe_for_display(const char *untrusted_str,
                                         size_t line_length);
#endif // !defined QUBES_UTIL_PURE_H
