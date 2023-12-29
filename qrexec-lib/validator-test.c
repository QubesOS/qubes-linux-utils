#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include "pure.h"
#include <unicode/utf8.h>
#ifdef NDEBUG
// without assertions this test program would not test anything
# error "String validation test program does not work without assertions."
#endif
#include <assert.h>

static void character_must_be_allowed(UChar32 c)
{
    char buf[5];
    int32_t off = 0;
    UBool e = false;
    U8_APPEND((uint8_t *)buf, off, 4, c, e);
    assert(!e && off <= 4);
    buf[off] = 0;
    if (!qubes_pure_code_point_safe_for_display(c) ||
        !qubes_pure_string_safe_for_display(buf, 0))
    {
        fprintf(stderr, "BUG: cannot handle file name %s (codepoint U+%" PRIx32 ")\n", buf, (int32_t)c);
        abort();
    }
}

static void character_must_be_forbidden(UChar32 c)
{
    uint8_t buf[128];
    int32_t off = 0;
    if (qubes_pure_code_point_safe_for_display(c)) {
        fprintf(stderr, "BUG: allowed codepoint U+%" PRIx32 "\n", (int32_t)c);
        abort();
    } else if (c < 0) {
        return; // cannot be encoded sensibly
    } else if (c < (1 << 7)) {
        buf[off++] = c;
    } else if (c < (1 << 11)) {
        buf[off++] = (0xC0 | (c >> 6));
        buf[off++] = (0x80 | (c & 0x3F));
    } else if (c < (1L << 16)) {
        buf[off++] = (0xE0 | (c >> 12));
        buf[off++] = (0x80 | ((c >> 6) & 0x3F));
        buf[off++] = (0x80 | (c & 0x3F));
    } else if (c < 0x140000) {
        buf[off++] = (0xF0 | (c >> 18));
        buf[off++] = (0x80 | ((c >> 12) & 0x3F));
        buf[off++] = (0x80 | ((c >> 6) & 0x3F));
        buf[off++] = (0x80 | (c & 0x3F));
    } else {
        return; // trivially rejected
    }
    if (c < 0x110000 && !U_IS_SURROGATE(c)) {
        UChar32 compare_c;
        U8_GET(buf, 0, 0, off, compare_c);
        assert(compare_c >= 0);
        assert(compare_c == c);
    }

    buf[off++] = 0;
    if (qubes_pure_string_safe_for_display((const char *)buf, 0))
    {
        fprintf(stderr, "BUG: allowed string with codepoint U+%" PRIx32 "\n", (int32_t)c);
        abort();
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    assert(qubes_pure_validate_file_name((uint8_t *)u8"simple_safe_filename.txt"));
    // Greek letters are safe
    assert(qubes_pure_validate_file_name((uint8_t *)u8"\u03b2.txt"));
    assert(qubes_pure_validate_file_name((uint8_t *)u8"\u03b1.txt"));
    // As are Cyrillic ones
    assert(qubes_pure_validate_file_name((uint8_t *)u8"\u0400.txt"));
    // As are unicode quotation marks
    assert(qubes_pure_validate_file_name((uint8_t *)u8"\u201c"));
    // As are ASCII characters, except DEL and controls
    for (uint32_t i = 0x20; i < 0x7F; ++i)
        character_must_be_allowed(i);
    // And CJK ideographs
    uint32_t cjk_ranges[] = {
        0x03400, 0x04DBF,
        0x04E00, 0x09FFC,
        0x20000, 0x2A6DD,
        0x2A700, 0x2B734,
        0x2B740, 0x2B81D,
        0x2B820, 0x2CEA1,
        0x2CEB0, 0x2EBE0,
        0x30000, 0x3134A,
        0x0,
    };
    for (size_t i = 0; cjk_ranges[i]; i += 2) {
        for (uint32_t v = cjk_ranges[i]; v <= cjk_ranges[i + 1]; ++v) {
            character_must_be_allowed(v);
        }
    }
    // Forbidden ranges
    uint32_t const forbidden[] = {
        // C0 controls and empty string
        0x0, 0x1F,
        // C1 controls
        0x0007F, 0x0009F,
        // Private-use area
        0x0E000, 0x0F8FF,
        // Spaces
        0xA0, 0xA0,
        0x02000, 0x0200A,
        0x0205F, 0x0205F,
        0x0180E, 0x0180E,
        0x01680, 0x01680,
        // Line breaks
        0x202A, 0x202B,
        // Non-characters
        0x0FDD0, 0x0FDEF,
        0x0FFFE, 0x0FFFF,
        0x1FFFE, 0x1FFFF,
        0x2FFFE, 0x2FFFF,
        // Forbidden codepoints
        0x0323B0, 0x10FFFF,
        // Too long
        0x110000, UINT32_MAX - 1,
        0x0,
    };
    for (size_t i = 0; i == 0 || forbidden[i]; i += 2) {
        for (uint32_t v = forbidden[i]; v <= forbidden[i + 1]; ++v) {
            character_must_be_forbidden(v);
        }
    }

    // Flags are too complex to display :(
    assert(!qubes_pure_string_safe_for_display(u8"\U0001f3f3", 0));
    assert(!qubes_pure_string_safe_for_display(u8"\ufe0f", 0));
    assert(!qubes_pure_string_safe_for_display(u8"\u200d", 0));
    assert(!qubes_pure_string_safe_for_display(u8"\u26a0", 0));

    // Emojies are not allowed
    assert(!qubes_pure_string_safe_for_display(u8"\U0001f642", 0));
    // Cuneiform is way too obscure to be worth the risk
    assert(!qubes_pure_string_safe_for_display(u8"\U00012000", 0));
    // Surrogates are forbidden
    for (uint32_t i = 0xD800; i <= 0xDFFF; ++i) {
        uint8_t buf[4] = {
            i >> 12 | 0xE0,
            0x80 | (i >> 6 & 0x3F),
            0x80 | (i & 0x3F),
            0,
        };
        assert(buf[0] == 0xED);
        assert(buf[1] >= 0xA0 && buf[1] <= 0xBF);
        assert(!qubes_pure_string_safe_for_display((char *)buf, 0));
        assert(!qubes_pure_code_point_safe_for_display(i));
    }

    // Invalid codepoints beyond 0x10FFFFF are forbidden
    for (uint32_t i = 0x90; i < 0xC0; ++i) {
        for (uint32_t j = 0x80; j < 0xC0; ++j) {
            for (uint32_t k = 0x80; k < 0xC0; ++k) {
                char buf[5] = { 0xF4, i, j, k, 0 };
                assert(!qubes_pure_string_safe_for_display(buf, 0));
            }
        }
    }

    /* Check for code points that cannot be assigned characters */
    for (uint64_t i = 0; i <= UINT32_MAX >> 0; ++i) {
        uint32_t j = (uint32_t)i;
        if (j < 32 || j == 0x7F || !U_IS_UNICODE_CHAR(j)) {
            assert(!qubes_pure_code_point_safe_for_display(j));
        } else {
            assert(j < 0x10FFFFE);
        }
    }

    // Directory traversal checks
    assert(!qubes_pure_validate_file_name((uint8_t *)".."));
    assert(!qubes_pure_validate_file_name((uint8_t *)"../.."));
    assert(!qubes_pure_validate_file_name((uint8_t *)"a/.."));
    assert(!qubes_pure_validate_file_name((uint8_t *)"a/../b"));
    assert(!qubes_pure_validate_file_name((uint8_t *)"/"));
    assert(!qubes_pure_validate_file_name((uint8_t *)"//"));
    assert(!qubes_pure_validate_file_name((uint8_t *)"///"));
    assert(!qubes_pure_validate_file_name((uint8_t *)"/a"));
    assert(!qubes_pure_validate_file_name((uint8_t *)"//a"));
    assert(!qubes_pure_validate_file_name((uint8_t *)"///a"));

    // No repeated slashes
    assert(!qubes_pure_validate_file_name((uint8_t *)"a//b"));

    // No "." as a path component
    assert(!qubes_pure_validate_file_name((uint8_t *)"."));
    assert(!qubes_pure_validate_file_name((uint8_t *)"a/."));
    assert(!qubes_pure_validate_file_name((uint8_t *)"./a"));
    assert(!qubes_pure_validate_file_name((uint8_t *)"a/./a"));

    // No ".." as a path component
    assert(!qubes_pure_validate_file_name((uint8_t *)".."));
    assert(!qubes_pure_validate_file_name((uint8_t *)"a/.."));
    assert(!qubes_pure_validate_file_name((uint8_t *)"../a"));
    assert(!qubes_pure_validate_file_name((uint8_t *)"a/../a"));

    // Looks like "." or ".." but is not
    assert(qubes_pure_validate_file_name((const uint8_t *)".a"));
    assert(qubes_pure_validate_file_name((const uint8_t *)"..a"));

    // Symbolic links
    // Top level cannot be symlink
    assert(!qubes_pure_validate_symbolic_link((const uint8_t *)"a", (const uint8_t *)"b"));
    // Symbolic links cannot escape
    assert(!qubes_pure_validate_symbolic_link((const uint8_t *)"a/b", (const uint8_t *)"../a"));
    assert(!qubes_pure_validate_symbolic_link((const uint8_t *)"a/b", (const uint8_t *)"../a/b/c"));
    assert(!qubes_pure_validate_symbolic_link((const uint8_t *)"a/b/c", (const uint8_t *)"../../a"));
    assert(qubes_pure_validate_symbolic_link((const uint8_t *)"a/b", (const uint8_t *)"a"));
    assert(qubes_pure_validate_symbolic_link((const uint8_t *)"a/b/c", (const uint8_t *)"../a"));
    // Absolute symlinks are rejected
    assert(!qubes_pure_validate_symbolic_link((const uint8_t *)"a/b/c", (const uint8_t *)"/a"));
}
