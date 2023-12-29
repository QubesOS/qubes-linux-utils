#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <assert.h>
#include <errno.h>

#include <unistd.h>

#include <err.h>

#include <unicode/uchar.h>
#include <unicode/uscript.h>
#include <unicode/utf.h>

static bool is_permitted_code_point(uint32_t const code_point)
{
    /*
     * Validate that this is a Unicode codepoint that can be assigned a
     * character.  This catches surrogates, code points beyond 0x10FFFF, and
     * various noncharacters.
     */
    if (!(U_IS_UNICODE_CHAR(code_point)))
        return false;

    /* Reject all control characters */
    if (code_point < 0x20)
        return false;

    /* Allow all other ASCII characters except DEL */
    if (code_point < 0x7F)
        return true;

    /*
     * Validate that the codepoint is a valid scalar value and is not a symbol,
     * space, unassigned character, or control character.
     */
    int category = u_charType(code_point);
    switch (category) {
    case U_UNASSIGNED:
        return false;
    case U_UPPERCASE_LETTER:
    case U_LOWERCASE_LETTER:
    case U_TITLECASE_LETTER:
    case U_MODIFIER_LETTER:
    case U_OTHER_LETTER:
        break;
    case U_NON_SPACING_MARK:
    case U_ENCLOSING_MARK:
    case U_COMBINING_SPACING_MARK:
        return false;
    case U_DECIMAL_DIGIT_NUMBER:
    case U_LETTER_NUMBER:
    case U_OTHER_NUMBER:
        break;
    case U_SPACE_SEPARATOR:
        return false;
    case U_LINE_SEPARATOR:
    case U_PARAGRAPH_SEPARATOR:
    case U_CONTROL_CHAR:
    case U_FORMAT_CHAR:
    case U_PRIVATE_USE_CHAR:
        return false;
    case U_DASH_PUNCTUATION:
    case U_START_PUNCTUATION:
    case U_END_PUNCTUATION:
    case U_CONNECTOR_PUNCTUATION:
    case U_OTHER_PUNCTUATION:
    case U_MATH_SYMBOL:
    case U_CURRENCY_SYMBOL:
        break;
    case U_MODIFIER_SYMBOL:
    case U_OTHER_SYMBOL:
        return false;
    case U_INITIAL_PUNCTUATION:
    case U_FINAL_PUNCTUATION:
        break;
    case U_SURROGATE:
    default:
        fprintf(stderr, "BUG: u_charType(0x%" PRIx32 ") returned unexpected value %d", code_point, category);
        abort();
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
        return false;
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
        return false;
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
        return true;
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
#ifdef USCRIPT_OLD_SOGDIAN
    case USCRIPT_OLD_SOGDIAN:
#endif
#ifdef USCRIPT_SOGDIAN
    case USCRIPT_SOGDIAN:
#endif
#ifdef USCRIPT_CHORASMIAN
    case USCRIPT_CHORASMIAN:
#endif
#ifdef USCRIPT_ELYMAIC
    case USCRIPT_ELYMAIC:
#endif
    case USCRIPT_MAHAJANI:
    case USCRIPT_JURCHEN:
    case USCRIPT_TANGUT:
    case USCRIPT_WOLEAI:
    case USCRIPT_ANATOLIAN_HIEROGLYPHS:
    case USCRIPT_KHOJKI:
    case USCRIPT_MULTANI:
    case USCRIPT_MODI:
    case USCRIPT_AHOM:
#ifdef USCRIPT_DOGRA
    case USCRIPT_DOGRA:
#endif
    case USCRIPT_BHAIKSUKI:
    case USCRIPT_MARCHEN:
    case USCRIPT_ZANABAZAR_SQUARE:
#ifdef USCRIPT_DIVES_AKURU
    case USCRIPT_DIVES_AKURU:
#endif
#ifdef USCRIPT_MAKASAR
    case USCRIPT_MAKASAR:
#endif
#ifdef USCRIPT_NANDINAGARI
    case USCRIPT_NANDINAGARI:
#endif
#ifdef USCRIPT_KHITAN_SMALL_SCRIPT
    case USCRIPT_KHITAN_SMALL_SCRIPT:
#endif
        return false; // dead languages or scripts
    case USCRIPT_MENDE:
    case USCRIPT_MANDAIC:
    case USCRIPT_ARABIC:
    case USCRIPT_HEBREW:
    case USCRIPT_NKO:
#ifdef USCRIPT_HANIFI_ROHINGYA
    case USCRIPT_HANIFI_ROHINGYA:
#endif
    case USCRIPT_NUSHU:
    case USCRIPT_ADLAM:
        return false; // right-to-left
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
        return false; // require complex rendering
    default:
        return false; // not sure
    }
}

static void print_code_point_list(FILE *out)
{
    bool last_allowed = false;
    uint32_t range_start = 0;
    for (uint32_t v = 0x20; v < 0x110000; ++v) {
        bool this_allowed = is_permitted_code_point(v);
        if (v < 0x7F)
            assert(this_allowed);
        if (this_allowed ^ last_allowed) {
            last_allowed = this_allowed;
            if (this_allowed) {
                range_start = v;
                // Start a new list
                if (fprintf(out, "    case 0x%06" PRIx32, v) != 17)
                    err(1, "fprintf()");
            } else {
                if (v - range_start > 1) {
                    if (fprintf(out, " ... 0x%06" PRIx32 ":\n", v - 1) != 15)
                        err(1, "fprintf()");
                } else {
                    if (fwrite(":\n", 1, 2, out) != 2)
                        err(1, "fprintf()");
                }
            }
        }
    }
    if (last_allowed)
        errx(1, "BUG: should not allow 0x10FFFF");
    if (fflush(out))
        err(1, "fflush()");
    switch (fsync(fileno(out))) {
    case 0:
        return;
    case -1:
        if ((errno != EROFS) && (errno != EINVAL))
            err(1, "fsync()");
        return;
    default:
        abort();
    }
}

int main(int argc, char **argv)
{
    if (argc != 1)
        errx(1, "No arguments expected");
    (void)argv;
    print_code_point_list(stdout);
}
