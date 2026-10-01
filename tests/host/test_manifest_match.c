/* 11af item 2: the floppy row's manifest compare is now a pure
 * function shared with the Amiga suite - five cases that pin the
 * shapes that broke the old strstr probes (the first-line bug). */
#include "tn_test.h"
#include "../../tests/amiga/tn_manifest_match.h"

static int run(const char *exp, const char *got,
               int *expn, int *matched, int *gotn, int *mirrored)
{
    return tn_manifest_match(exp, got, expn, matched, gotn, mirrored);
}

/* 1. exact match */
TN_TEST(exact_match)
{
    int en, m, gn, mi;
    TN_ASSERT_EQ(run("a 1\nb 2\n", "a 1\nb 2\n", &en, &m, &gn, &mi), 1);
    TN_ASSERT_EQ(en, 2);
    TN_ASSERT_EQ(m, 2);
    TN_ASSERT_EQ(gn, 2);
    TN_ASSERT_EQ(mi, 2);
}

/* 2. the 11ad bug: the missing line is the FIRST line of got - the
 * old "\n<line>\n" strstr probe could not see it */
TN_TEST(first_line_missing)
{
    int en, m, gn, mi;
    TN_ASSERT_EQ(run("a 1\nb 2\n", "b 2\n", &en, &m, &gn, &mi), 0);
    TN_ASSERT_EQ(en, 2);
    TN_ASSERT_EQ(m, 1);
    TN_ASSERT_EQ(gn, 1);
    TN_ASSERT_EQ(mi, 1);
}

/* 3. the last line missing */
TN_TEST(last_line_missing)
{
    int en, m, gn, mi;
    TN_ASSERT_EQ(run("a 1\nb 2\nc 3\n", "a 1\nb 2\n", &en, &m, &gn, &mi), 0);
    TN_ASSERT_EQ(en, 3);
    TN_ASSERT_EQ(m, 2);
}

/* 4. an extra line in got (e.g. Disk.info) - symmetric failure */
TN_TEST(extra_line_in_got)
{
    int en, m, gn, mi;
    TN_ASSERT_EQ(run("a 1\nb 2\n", "a 1\nb 2\nDisk.info 686\n",
                     &en, &m, &gn, &mi), 0);
    TN_ASSERT_EQ(en, 2);
    TN_ASSERT_EQ(m, 2);
    TN_ASSERT_EQ(gn, 3);
    TN_ASSERT_EQ(mi, 2);
}

/* 5. same name, different size - a mismatch, not a pass */
TN_TEST(size_mismatch)
{
    int en, m, gn, mi;
    TN_ASSERT_EQ(run("a 1\n", "a 2\n", &en, &m, &gn, &mi), 0);
    TN_ASSERT_EQ(en, 1);
    TN_ASSERT_EQ(m, 0);
    TN_ASSERT_EQ(gn, 1);
    TN_ASSERT_EQ(mi, 0);
}

/* 6. order does not matter (FFS hash order vs manifest order) and a
 * trailing newline vs none is the same set */
TN_TEST(order_and_trailing_newline_free)
{
    int en, m, gn, mi;
    TN_ASSERT_EQ(run("a 1\nb 2\n", "b 2\na 1", &en, &m, &gn, &mi), 1);
    TN_ASSERT_EQ(en, 2);
    TN_ASSERT_EQ(gn, 2);
}

int main(void)
{
    TN_TEST_RUN(exact_match);
    TN_TEST_RUN(first_line_missing);
    TN_TEST_RUN(last_line_missing);
    TN_TEST_RUN(extra_line_in_got);
    TN_TEST_RUN(size_mismatch);
    TN_TEST_RUN(order_and_trailing_newline_free);
    TN_TEST_PLAN();
    return tn_test_failures();
}
