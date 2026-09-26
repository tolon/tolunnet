/*
 * test_log_format.c — z.ai step 9b item 1: the tn_logf formatter must
 * honour '-', '0' and multi-digit field width, and must stop at a
 * trailing '%'. Unit: src/common/log_format.c (pure, host-testable).
 */
#include "tn_test.h"
#include "../../src/common/log_format.h"

#include <stdarg.h>

static char buf[256];

static const char *f(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    tn_logf_vformat(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return buf;
}

TN_TEST(left_align_string)
{
    TN_ASSERT_STREQ("name         |", f("%-13s|", "name"));
    TN_ASSERT_STREQ("tolunnet-host|", f("%-13s|", "tolunnet-host"));
}

TN_TEST(width_padded_string)
{
    /* the netstat table style: %-5s %6lu */
    TN_ASSERT_STREQ("tcp       42", f("%-5s %6lu", "tcp", 42UL));
    TN_ASSERT_STREQ("udp    65535", f("%-5s %6lu", "udp", 65535UL));
}

TN_TEST(zero_pad_numbers)
{
    TN_ASSERT_STREQ("07", f("%02lu", 7UL));
    TN_ASSERT_STREQ("42", f("%02lu", 42UL));
    TN_ASSERT_STREQ("deadbeef", f("%08lx", 0xdeadbeefUL));
    TN_ASSERT_STREQ("0000beef", f("%08lx", 0xbeefUL));
}

TN_TEST(plain_width_number)
{
    TN_ASSERT_STREQ("    42", f("%6lu", 42UL));
    TN_ASSERT_STREQ("123456", f("%6lu", 123456UL));
    TN_ASSERT_STREQ(f("%4ld", -7L), "  -7");
}

TN_TEST(trailing_percent_stops)
{
    TN_ASSERT_STREQ("abc%", f("abc%%"));
    /* a lone trailing '%' must not read past the end */
    buf[0] = 'Z'; buf[1] = 0;
    TN_ASSERT_STREQ(f("abc%"), "abc");  /* trailing %: stop, emit nothing */
    TN_ASSERT_STREQ(f("%"), "");
}

TN_TEST(basename_and_negative)
{
    TN_ASSERT_STREQ(f("x = %ld (%s)", -12L, "(nil)"), "x = -12 ((nil))");
    TN_ASSERT_STREQ("100%", f("%lu%%", 100UL));
}

int main(void)
{
    TN_TEST_RUN(left_align_string);
    TN_TEST_RUN(width_padded_string);
    TN_TEST_RUN(zero_pad_numbers);
    TN_TEST_RUN(plain_width_number);
    TN_TEST_RUN(trailing_percent_stops);
    TN_TEST_RUN(basename_and_negative);
    TN_TEST_PLAN();
    return tn_test_failures();
}
