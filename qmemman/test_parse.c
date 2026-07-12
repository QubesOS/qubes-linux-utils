/*
 * Unit tests for the parse() function in meminfo-writer.c.
 *
 * These tests verify correctness of the memory-usage calculation and the
 * threshold / hysteresis logic.
 *
 * NOTE: None of these tests will fail if snprintf() is reverted to sprintf()
 * in meminfo-writer.c.  The output buffer (outbuf) is 4096 bytes and
 * used_mem is a long long (max 20 decimal characters), so no overflow is
 * possible with valid input regardless of which variant is used.  The
 * snprintf() change is kept as defensive best-practice (CWE-676), consistent
 * with the change already made in #140.  test_output_length_within_bounds()
 * below documents the size contract that snprintf() makes explicit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Pull in globals and parse() from meminfo-writer.c */
extern long prev_used_mem;
extern int used_mem_change_threshold;
extern int delay;
extern int usr1_received;
const char *parse(const char *meminfo_buf, const char *dom_current_buf);

/* ---- helpers ------------------------------------------------------------ */

static int tests_run = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); \
        tests_failed++; \
    } else { \
        printf("PASS: %s\n", msg); \
    } \
} while (0)

/* Build a minimal /proc/meminfo string from named fields (values in kB). */
static char meminfo_buf[4096];
static void make_meminfo(long long MemTotal, long long MemFree,
                         long long Buffers, long long Cached,
                         long long SwapTotal, long long SwapFree)
{
    snprintf(meminfo_buf, sizeof(meminfo_buf),
        "MemTotal:  %lld kB\n"
        "MemFree:   %lld kB\n"
        "Buffers:   %lld kB\n"
        "Cached:    %lld kB\n"
        "SwapTotal: %lld kB\n"
        "SwapFree:  %lld kB\n",
        MemTotal, MemFree, Buffers, Cached, SwapTotal, SwapFree);
}

/* Reset global state before each logical test. */
static void reset_globals(int threshold)
{
    prev_used_mem = 0;
    used_mem_change_threshold = threshold;
    delay = 1;
    usr1_received = 0;
}

/* ---- tests -------------------------------------------------------------- */

/* parse() should return a non-NULL string the first time (prev_used_mem==0). */
static void test_first_call_returns_value(void)
{
    reset_globals(1000);
    make_meminfo(8000000, 2000000, 100000, 500000, 0, 0);
    /* used_mem = 8000000 - 100000 - 500000 - 2000000 + 0 - 0 = 5400000 */
    const char *result = parse(meminfo_buf, NULL);
    CHECK(result != NULL, "first call returns non-NULL (prev_used_mem==0)");
    if (result) {
        long long val = strtoll(result, NULL, 10);
        CHECK(val == 5400000LL, "first call returns correct used_mem value");
    }
}

/* parse() should return NULL when the change is below threshold. */
static void test_below_threshold_returns_null(void)
{
    reset_globals(1000000); /* high threshold */
    make_meminfo(8000000, 2000000, 100000, 500000, 0, 0);
    /* First call always returns (prev==0). */
    parse(meminfo_buf, NULL);

    /* Same meminfo again → diff == 0, well below threshold. */
    const char *result = parse(meminfo_buf, NULL);
    CHECK(result == NULL, "below-threshold call returns NULL");
}

/* parse() should return non-NULL when change exceeds threshold. */
static void test_above_threshold_returns_value(void)
{
    reset_globals(100); /* low threshold */
    make_meminfo(8000000, 2000000, 100000, 500000, 0, 0);
    parse(meminfo_buf, NULL); /* seed prev_used_mem */

    /* Change free mem by 1000 kB → diff = 1000, above threshold of 100. */
    make_meminfo(8000000, 1999000, 100000, 500000, 0, 0);
    const char *result = parse(meminfo_buf, NULL);
    CHECK(result != NULL, "above-threshold call returns non-NULL");
}

/* parse() output is always a plain decimal integer string. */
static void test_output_is_decimal_string(void)
{
    reset_globals(1);
    make_meminfo(16000000, 4000000, 200000, 1000000, 2000000, 500000);
    /* used_mem = 16000000 - 200000 - 1000000 - 4000000 + 2000000 - 500000 = 12300000 */
    const char *result = parse(meminfo_buf, NULL);
    CHECK(result != NULL, "output is non-NULL for large but valid memory values");
    if (result) {
        /* Must be parseable as a decimal number with no trailing garbage. */
        char *end = NULL;
        long long val = strtoll(result, &end, 10);
        CHECK(*end == '\0', "output string is a pure decimal integer");
        CHECK(val == 12300000LL, "output value matches expected used_mem");
    }
}

/* dom_current_buf overrides MemTotal when non-zero. */
static void test_dom_current_overrides_memtotal(void)
{
    reset_globals(1);
    make_meminfo(8000000, 2000000, 100000, 500000, 0, 0);
    /* Override MemTotal with 4000000; used_mem = 4000000-100000-500000-2000000 = 1400000 */
    const char *result = parse(meminfo_buf, "4000000");
    CHECK(result != NULL, "dom_current override returns non-NULL");
    if (result) {
        long long val = strtoll(result, NULL, 10);
        CHECK(val == 1400000LL, "dom_current override produces correct used_mem");
    }
}

/*
 * Verify the formatted output fits within the size constraint documented by
 * the snprintf() call: a long long decimal is at most 20 characters
 * (LLONG_MAX = 9223372036854775807, 19 digits, plus sign = 20), which is
 * well within the 4096-byte outbuf.  This test would catch regressions if
 * the format string were changed to something that could produce longer output.
 *
 * Note: this test does NOT fail with sprintf() — the buffer is always large
 * enough for the current format.  The snprintf() change is defensive and this
 * test documents the size contract it makes explicit.
 */
static void test_output_length_within_bounds(void)
{
    reset_globals(1);
    make_meminfo(8000000, 2000000, 100000, 500000, 0, 0);
    const char *result = parse(meminfo_buf, NULL);
    CHECK(result != NULL, "output is non-NULL for bounds check");
    if (result) {
        size_t len = strlen(result);
        /* A long long decimal is at most 20 characters. */
        CHECK(len <= 20, "output length fits within long long decimal maximum (20 chars)");
        /* The static outbuf in parse() is 4096 bytes; snprintf ensures no overrun. */
        CHECK(len < 4096, "output length is within outbuf size (4096 bytes)");
    }
}

/* ---- main --------------------------------------------------------------- */

int main(void)
{
    test_first_call_returns_value();
    test_below_threshold_returns_null();
    test_above_threshold_returns_value();
    test_output_is_decimal_string();
    test_dom_current_overrides_memtotal();
    test_output_length_within_bounds();

    printf("\n%d/%d tests passed.\n", tests_run - tests_failed, tests_run);
    return tests_failed ? 1 : 0;
}
