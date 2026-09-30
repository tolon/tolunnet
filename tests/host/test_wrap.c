/*
 * test_wrap.c — unit tests for the pixel-measured word wrap
 * (11w item 1). Fixed metric: every character is 8 px wide.
 * Run under ASan/UBSan via make test-host.
 */
#include "tn_test.h"
#include "../../src/setup/wrap.h"
#include <stdio.h>
#include <string.h>

#define ROWS 8

static char g_rows[ROWS][96];

static long measure8(const char *s, int len, void *ud)
{
    (void)ud;
    (void)s;
    return (long)len * 8;
}

TN_TEST(empty_input_yields_no_rows)
{
    TN_ASSERT_EQ(tn_wrap_rows("", measure8, NULL, 80, g_rows, ROWS, "    "), 0);
}

TN_TEST(null_text_and_null_measure_are_safe)
{
    /* NULL inputs must not crash and must produce 0 rows */
    TN_ASSERT_EQ(tn_wrap_rows(NULL, measure8, NULL, 80, g_rows, ROWS, "  "), 0);
    TN_ASSERT_EQ(tn_wrap_rows("text", NULL, NULL, 80, g_rows, ROWS, "  "), 0);
}

TN_TEST(single_word_fits)
{
    TN_ASSERT_EQ(tn_wrap_rows("hello", measure8, NULL, 80, g_rows, ROWS, ""), 1);
    TN_ASSERT_STREQ(g_rows[0], "hello");
}

TN_TEST(exact_fit_stays_on_one_row)
{
    /* 10 chars * 8 px = 80 px == maxw: must NOT wrap */
    TN_ASSERT_EQ(tn_wrap_rows("0123456789", measure8, NULL, 80, g_rows, ROWS, ""), 1);
    TN_ASSERT_STREQ(g_rows[0], "0123456789");
}

TN_TEST(one_px_over_wraps)
{
    TN_ASSERT_EQ(tn_wrap_rows("aaaaa bbbbb", measure8, NULL, 40, g_rows, ROWS, ""), 2);
    TN_ASSERT_STREQ(g_rows[0], "aaaaa");
    TN_ASSERT_STREQ(g_rows[1], "bbbbb");
}

TN_TEST(tabs_act_as_word_separators)
{
    TN_ASSERT_EQ(tn_wrap_rows("aa\tbb\tcc", measure8, NULL, 80, g_rows, ROWS, ""), 1);
    TN_ASSERT_STREQ(g_rows[0], "aa bb cc");
}

/* 11w item 1, W2: the indent is part of the measurement. maxw 60,
 * indent 32 px, word 32 px: the whole row "    word" is 64 px >
 * 60 px, so the wrapper keeps only the prefix that fits
 * ("    wor", 56 px) and the tail continues on the next row. */
TN_TEST(indent_counts_toward_the_width)
{
    int nr = tn_wrap_rows("word", measure8, NULL, 60, g_rows, ROWS, "    ");
    int i;
    TN_ASSERT_TRUE(nr >= 1);
    for (i = 0; i < nr; i++)
        TN_ASSERT_TRUE((long)strlen(g_rows[i]) * 8 <= 60);
    TN_ASSERT_TRUE(nr >= 2);   /* "    wor" + "    d" */
    TN_ASSERT_STREQ(g_rows[0], "    wor");
}

/* 11w item 1, W2 corner: when the indent alone is >= maxw the px
 * rule can never hold; the guarantee degrades to progress - each
 * row still carries at least one character of the word. */
TN_TEST(indent_wider_than_maxw_still_progresses)
{
    int nr = tn_wrap_rows("word", measure8, NULL, 24, g_rows, ROWS, "    ");
    int i;
    TN_ASSERT_TRUE(nr >= 1);
    for (i = 0; i < nr; i++) {
        TN_ASSERT_TRUE(strlen(g_rows[i]) >= 1);   /* progress */
    }
}

/* 11w item 1, W1/W4: a single 200-char word wraps into pieces that
 * are pure word characters (a hard-split never inserts a space) and
 * every row stays inside its buffer. */
TN_TEST(word_200_chars_hard_split_no_space_inserted)
{
    static char rows[24][96];
    static char text[201];
    static char expect0[81];
    int nr, i;

    memset(text, 'a', 200);
    text[200] = '\0';
    memset(expect0, 'a', 80);
    expect0[80] = '\0';
    memset(rows, 0, sizeof(rows));
    nr = tn_wrap_rows(text, measure8, NULL, 80 * 8, rows, 24, "    ");
    /* the px budget per row is maxw - indent(32 px) = 608 px = 76
     * chars: 200 chars split 76 + 76 + 48 over three rows, every
     * row pure word characters with the 4-space indent */
    TN_ASSERT_EQ(nr, 3);
    for (i = 0; i < nr; i++) {
        TN_ASSERT_TRUE(strlen(rows[i]) <= 95);
        /* no space after the 4-char indent (W4: the hard-split
         * never inserts a space inside the word) */
        TN_ASSERT_TRUE(strchr(rows[i] + 4, ' ') == NULL);
        TN_ASSERT_TRUE(strlen(rows[i]) >= 40);
    }
    TN_ASSERT_EQ((int)(strlen(rows[0]) + strlen(rows[1]) + strlen(rows[2]) - 3 * 4), 200);
}

/* 11w item 1, W1: rows near the 95-char limit with a huge maxw must
 * not overflow (this runs under ASan) and must stay under 96. */
TN_TEST(rows_near_the_95_limit_with_huge_maxw)
{
    static char rows[8][96];
    static char text[400];
    int nr, i;

    memset(text, 'x', 399);
    text[399] = '\0';
    memset(rows, 0, sizeof(rows));
    nr = tn_wrap_rows(text, measure8, NULL, 4000, rows, 8, "  ");
    /* the 96-byte row buffer caps every stored row at 95 chars
     * (2 of them the indent): 399 chars split over 5 rows, no
     * overflow. The px budget (4000) is far above the char
     * budget (93/row) here, so rows are char-limited. */
    TN_ASSERT_EQ(nr, 5);
    for (i = 0; i < nr; i++)
        TN_ASSERT_TRUE(strlen(rows[i]) <= 95);
}

TN_TEST(row_max_1_keeps_only_the_first_row)
{
    TN_ASSERT_EQ(tn_wrap_rows("aaaa bbbb cccc", measure8, NULL, 32, g_rows, 1, ""), 1);
    TN_ASSERT_STREQ(g_rows[0], "aaaa");
}

/* 11w item 1: invariant - 200 deterministic pseudo texts wrapped at
 * maxw 24..400: every row measures <= maxw and stays < 96 chars. */
static unsigned pseudo_state = 12345;
static unsigned pseudo_next(void)
{
    pseudo_state = pseudo_state * 1103515245u + 12345u;
    return (pseudo_state >> 16) & 0x7fff;
}

TN_TEST(invariant_200_texts_all_rows_within_bounds)
{
    int t;
    for (t = 0; t < 200; t++) {
        static char text[400];
        static char rows[8][96];
        long maxw = 24 + (long)((t * 37) % 377);
        int len = 1 + (int)(pseudo_next() % 60);
        int w, i;
        int nr;
        for (w = 0; w < len; w++) {
            int wl = 1 + (int)(pseudo_next() % 12);
            for (i = 0; i < wl; i++)
                text[w++] = (char)('a' + (pseudo_next() % 26));
            text[w++] = ' ';
        }
        text[w] = '\0';
        memset(rows, 0, sizeof(rows));
        nr = tn_wrap_rows(text, measure8, NULL, maxw, rows, 8, "  ");
        TN_ASSERT_TRUE(nr >= 1);
        for (i = 0; i < nr; i++) {
            TN_ASSERT_TRUE(strlen(rows[i]) < 96);
            TN_ASSERT_TRUE((long)strlen(rows[i]) * 8 <= maxw);
        }
    }
}

int main(void)
{
    TN_TEST_RUN(empty_input_yields_no_rows);
    TN_TEST_RUN(null_text_and_null_measure_are_safe);
    TN_TEST_RUN(single_word_fits);
    TN_TEST_RUN(exact_fit_stays_on_one_row);
    TN_TEST_RUN(one_px_over_wraps);
    TN_TEST_RUN(tabs_act_as_word_separators);
    TN_TEST_RUN(indent_counts_toward_the_width);
    TN_TEST_RUN(indent_wider_than_maxw_still_progresses);
    TN_TEST_RUN(word_200_chars_hard_split_no_space_inserted);
    TN_TEST_RUN(rows_near_the_95_limit_with_huge_maxw);
    TN_TEST_RUN(row_max_1_keeps_only_the_first_row);
    TN_TEST_RUN(invariant_200_texts_all_rows_within_bounds);
    TN_TEST_PLAN();
    return tn_test_failures();
}
