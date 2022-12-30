#define U_HIDE_DEPRECATED_API U_HIDE_DEPRECATED_API

#include "pure.h"

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <assert.h>

#include <unicode/uchar.h>
#include <unicode/uscript.h>
#include <unicode/utf.h>

/* validate single UTF-8 character
 * return bytes count of this character, or 0 if the character is invalid */
static int validate_utf8_char(const unsigned char *untrusted_c) {
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

    /*
     * Validate that this is a Unicode codepoint that can be assigned a
     * character.  This catches surrogates, code points beyond 0x10FFFF, and
     * various noncharacters.
     */
    if (!(U_IS_UNICODE_CHAR(code_point)))
        return 0;

    /*
     * Validate that the codepoint is a valid scalar value and is not a symbol,
     * space, unassigned character, or control character.
     */
    switch (code_point) {
#include "unicode-class-table.c"
        return 0; // Invalid UTF-8 or forbidden codepoint
    default:
        break;
    }

    uint32_t s = u_charDirection(code_point);
    switch (s) {
    case U_WHITE_SPACE_NEUTRAL:
    case U_OTHER_NEUTRAL:
    case U_EUROPEAN_NUMBER_TERMINATOR:
    case U_EUROPEAN_NUMBER_SEPARATOR:
    case U_COMMON_NUMBER_SEPARATOR:
    case U_EUROPEAN_NUMBER:
    case U_LEFT_TO_RIGHT:
        break;
    default:
        /* Not safe */
        return 0;
    }
    UErrorCode errcode = 0;
    int script = uscript_getScript(code_point, &errcode);
    if (errcode) {
        fprintf(stderr, "BUG: uscript_getScript failed on codepoint 0x%" PRIX32 " with with code %d\n", code_point, errcode);
        abort();
    }

    int v = uscript_getUsage(script) ;
    switch (v) {
    case USCRIPT_USAGE_UNKNOWN:
    case USCRIPT_USAGE_RECOMMENDED:
        break;
    case USCRIPT_USAGE_NOT_ENCODED:
    case USCRIPT_USAGE_EXCLUDED:
    case USCRIPT_USAGE_ASPIRATIONAL:
    case USCRIPT_USAGE_LIMITED_USE:
        return 0;
    default:
        fprintf(stderr, "BUG: uscript_getUsage returned unexpected value %d codepoint 0x%" PRIX32 "\n", v, code_point);
        abort();
    }

    switch (script) {
    case USCRIPT_INHERITED:
    case USCRIPT_CYRILLIC:
    case USCRIPT_GREEK:
    case USCRIPT_LATIN:
    case USCRIPT_BRAILLE:
    case USCRIPT_SIMPLIFIED_HAN:
    case USCRIPT_TRADITIONAL_HAN:
    case USCRIPT_HAN:
    case USCRIPT_HAN_WITH_BOPOMOFO:
    case USCRIPT_JAMO:
    case USCRIPT_HANGUL:
    case USCRIPT_BOPOMOFO:
    case USCRIPT_KATAKANA_OR_HIRAGANA:
    case USCRIPT_HIRAGANA:
    case USCRIPT_KATAKANA:
    case USCRIPT_JAPANESE:
    case USCRIPT_KOREAN:
    case USCRIPT_COMMON:
        return total_size;
    case USCRIPT_DESERET:
    case USCRIPT_COPTIC:
    case USCRIPT_LINEAR_B:
    case USCRIPT_ETHIOPIC:
    case USCRIPT_GOTHIC:
    case USCRIPT_OGHAM:
    case USCRIPT_OLD_ITALIC:
    case USCRIPT_UGARITIC:
    case USCRIPT_GLAGOLITIC:
    case USCRIPT_KHAROSHTHI:
    case USCRIPT_OLD_PERSIAN:
    case USCRIPT_HIERATIC_EGYPTIAN:
    case USCRIPT_EGYPTIAN_HIEROGLYPHS:
    case USCRIPT_LINEAR_A:
    case USCRIPT_DEMOTIC_EGYPTIAN:
    case USCRIPT_BRAHMI:
    case USCRIPT_KHUTSURI:
    case USCRIPT_OLD_HUNGARIAN:
    case USCRIPT_HARAPPAN_INDUS:
    case USCRIPT_MAYAN_HIEROGLYPHS:
    case USCRIPT_MEROITIC_HIEROGLYPHS:
    case USCRIPT_OLD_PERMIC:
    case USCRIPT_PHOENICIAN:
    case USCRIPT_ORKHON:
    case USCRIPT_RONGORONGO:
    case USCRIPT_CUNEIFORM:
    case USCRIPT_CARIAN:
    case USCRIPT_LYCIAN:
    case USCRIPT_LYDIAN:
    case USCRIPT_REJANG:
    case USCRIPT_IMPERIAL_ARAMAIC:
    case USCRIPT_AVESTAN:
    case USCRIPT_KAITHI:
    case USCRIPT_INSCRIPTIONAL_PAHLAVI:
    case USCRIPT_PSALTER_PAHLAVI:
    case USCRIPT_BOOK_PAHLAVI:
    case USCRIPT_SAMARITAN:
    case USCRIPT_INSCRIPTIONAL_PARTHIAN:
    case USCRIPT_ELBASAN:
    case USCRIPT_CAUCASIAN_ALBANIAN:
    case USCRIPT_PALMYRENE:
    case USCRIPT_NABATAEAN:
    case USCRIPT_HATRAN:
    case USCRIPT_MEROITIC_CURSIVE:
    case USCRIPT_OLD_SOUTH_ARABIAN:
    case USCRIPT_OLD_NORTH_ARABIAN:
    case USCRIPT_OLD_CHURCH_SLAVONIC_CYRILLIC:
    case USCRIPT_OLD_SOGDIAN:
    case USCRIPT_SOGDIAN:
#ifdef USCRIPT_CHORASMIAN
    case USCRIPT_CHORASMIAN:
#endif
    case USCRIPT_ELYMAIC:
    case USCRIPT_MAHAJANI:
    case USCRIPT_JURCHEN:
    case USCRIPT_TANGUT:
    case USCRIPT_WOLEAI:
    case USCRIPT_ANATOLIAN_HIEROGLYPHS:
    case USCRIPT_KHOJKI:
    case USCRIPT_MULTANI:
    case USCRIPT_MODI:
    case USCRIPT_AHOM:
    case USCRIPT_DOGRA:
    case USCRIPT_BHAIKSUKI:
    case USCRIPT_MARCHEN:
    case USCRIPT_ZANABAZAR_SQUARE:
#ifdef USCRIPT_DIVES_AKURU
    case USCRIPT_DIVES_AKURU:
#endif
    case USCRIPT_MAKASAR:
    case USCRIPT_NANDINAGARI:
#ifdef USCRIPT_KHITAN_SMALL_SCRIPT
    case USCRIPT_KHITAN_SMALL_SCRIPT:
#endif
        return 0; // dead languages or scripts
    case USCRIPT_MENDE:
    case USCRIPT_MANDAIC:
    case USCRIPT_ARABIC:
    case USCRIPT_HEBREW:
    case USCRIPT_NKO:
    case USCRIPT_HANIFI_ROHINGYA:
    case USCRIPT_NUSHU:
    case USCRIPT_ADLAM:
        return 0; // right-to-left
    case USCRIPT_DEVANAGARI:
    case USCRIPT_SYRIAC:
    case USCRIPT_BENGALI:
    case USCRIPT_BALINESE:
    case USCRIPT_ESTRANGELO_SYRIAC:
    case USCRIPT_WESTERN_SYRIAC:
    case USCRIPT_EASTERN_SYRIAC:
    case USCRIPT_GUJARATI:
    case USCRIPT_GURMUKHI:
    case USCRIPT_KANNADA:
    case USCRIPT_KHMER:
    case USCRIPT_MALAYALAM:
    case USCRIPT_MONGOLIAN:
    case USCRIPT_MYANMAR:
    case USCRIPT_THAI:
    case USCRIPT_SINHALA:
    case USCRIPT_TAMIL:
    case USCRIPT_TELUGU:
    case USCRIPT_THAANA:
    case USCRIPT_TIBETAN:
    case USCRIPT_ORIYA:
    case USCRIPT_PHAGS_PA:
    case USCRIPT_LIMBU:
    case USCRIPT_LAO:
    case USCRIPT_TAGALOG:
    case USCRIPT_BUHID:
    case USCRIPT_TAI_LE:
    case USCRIPT_BUGINESE:
    case USCRIPT_BATAK:
    case USCRIPT_CHAM:
    case USCRIPT_JAVANESE:
    case USCRIPT_LEPCHA:
    case USCRIPT_MIAO:
    case USCRIPT_LANNA:
    case USCRIPT_SAURASHTRA:
    case USCRIPT_CHAKMA:
    case USCRIPT_TAI_VIET:
    case USCRIPT_KHUDAWADI:
    case USCRIPT_TAKRI:
    case USCRIPT_NEWA:
    case USCRIPT_SOYOMBO:
    case USCRIPT_SIGN_WRITING:
        return 0; // require complex rendering
    default:
        return 0; // not sure
    }
}

static size_t validate_path(const char *const untrusted_name, size_t allowed_leading_dotdot)
{
    size_t non_dotdot_components = 0, i = 0;
    do {
        if (i == 0 || untrusted_name[i - 1] == '/') {
            switch (untrusted_name[i]) {
            case '/': // repeated or initial slash
            case '\0': // trailing slash or empty string
                return 0;
            case '.':
                if (untrusted_name[i + 1] == '\0' || untrusted_name[i + 1] == '/')
                    return 0;
                if ((untrusted_name[i + 1] == '.') &&
                    (untrusted_name[i + 2] == '\0' || untrusted_name[i + 2] == '/')) {
                    /* At least 2 leading components required */
                    if (allowed_leading_dotdot <= 2)
                        return 0;
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
                return 0;
            }
        }
    } while (untrusted_name[i]);
    return non_dotdot_components;
}

QUBES_PURE_PUBLIC bool
qubes_pure_validate_file_name(const char *untrusted_filename)
{
    return validate_path(untrusted_filename, 0) > 0;
}

QUBES_PURE_PUBLIC bool
qubes_pure_validate_symbolic_link(const char *untrusted_name,
                                  const char *untrusted_target)
{
    size_t depth = validate_path(untrusted_name, 0);
    return depth > 0 && validate_path(untrusted_target, depth) > 0;
}

QUBES_PURE_PUBLIC bool
qubes_pure_string_safe_for_display(const char *untrusted_str __attribute__((unused)), size_t line_length __attribute__((unused)))
{
    assert(line_length == 0 && "Not yet implemented: nonzero line length");
    size_t i = 0;
    do {
        if (untrusted_str[i] >= 0x20 && untrusted_str[i] <= 0x7E) {
            i++;
        } else {
            int utf8_ret = validate_utf8_char((const unsigned char *)(untrusted_str + i));
            if (utf8_ret > 0) {
                i += utf8_ret;
            } else {
                return false;
            }
        }
    } while (untrusted_str[i]);
    return true;
}
