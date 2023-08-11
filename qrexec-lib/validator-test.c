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

static void character_must_be_allowed(uint32_t c)
{
    uint8_t buf[5];
    int32_t off = 0;
    UBool e = false;
    U8_APPEND(buf, off, 4, c, e);
    assert(!e && off <= 4);
    buf[off] = 0;
    if (!qubes_pure_validate_file_name(buf)) {
        fprintf(stderr, "BUG: cannot handle file name %s (codepoint U+%" PRIx32 ")\n", buf, c);
        abort();
    }
}

static void character_must_be_forbidden(uint32_t c)
{
    uint8_t buf[5];
    int32_t off = 0;
    UBool e = false;
    U8_APPEND(buf, off, 4, c, e);
    assert(!e && off <= 4);
    buf[off] = 0;
    if (qubes_pure_validate_file_name(buf)) {
        fprintf(stderr, "BUG: allowed file name with codepoint U+%" PRIx32 "\n", c);
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
        0x3134B, 0x10FFFF,
        0x0,
    };
    for (size_t i = 0; i == 0 || forbidden[i]; i += 2) {
        for (uint32_t v = forbidden[i]; v <= forbidden[i + 1]; ++v) {
            character_must_be_forbidden(v);
        }
    }

    // Flags are too complex to display :(
    assert(!qubes_pure_validate_file_name((uint8_t *)u8"\U0001f3f3"));
    assert(!qubes_pure_validate_file_name((uint8_t *)u8"\ufe0f"));
    assert(!qubes_pure_validate_file_name((uint8_t *)u8"\u200d"));
    assert(!qubes_pure_validate_file_name((uint8_t *)u8"\u26a0"));

    // Emojies are not allowed
    assert(!qubes_pure_validate_file_name((uint8_t *)u8"\U0001f642"));
    // Cuneiform is way too obscure to be worth the risk
    assert(!qubes_pure_validate_file_name((uint8_t *)u8"\U00012000"));
    // Surrogates are forbidden
    for (uint32_t i = 0xD800; i <= 0xDFFF; ++i) {
        uint8_t buf[4] = { 0, 0, 0, 0 };
        int32_t off = 0;
        U8_APPEND_UNSAFE(buf, off, i);
        assert(off == 3);
        assert(!qubes_pure_validate_file_name(buf));
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
