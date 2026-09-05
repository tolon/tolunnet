/*
 * tn_test.h — minimal TAP (Test Anything Protocol) framework for host tests.
 *
 * Usage:
 *   #include "tn_test.h"
 *
 *   TN_TEST(arithmetic) {
 *       TN_ASSERT_EQ(2 + 2, 4);
 *       TN_ASSERT_STREQ("a", "a");
 *   }
 *
 *   int main(void) {
 *       TN_TEST_RUN(arithmetic);
 *       TN_TEST_PLAN();          // prints "1..N"
 *       return tn_test_failures();   // exit code = number of failed tests
 *   }
 *
 * Directives: TN_SKIP("reason")  -> "ok N - name # SKIP reason" (ends the test)
 *             TN_TODO("TNET-0xx reason") marks the CURRENT test as TODO:
 *             assertions that fail inside a TODO test still print "not ok"
 *             with a "# TODO <reason>" directive, so they document known-open
 *             defects without failing the harness gate (standard TAP rule:
 *             a TODO failure is not a suite failure).
 *
 * Host-only: stdio/stdint/string only, no AmigaOS headers.
 */
#ifndef TN_TEST_H
#define TN_TEST_H

#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int  tn_test_count    = 0;
static int  tn_test_failed   = 0;
static int  tn_test_current_failed = 0;
static int  tn_test_current_todo   = 0;
static const char *tn_test_current_name = "";
static const char *tn_test_current_todo_reason = "";

#define TN_TEST(name) \
    static void tn_test_fn_##name(void); \
    static void name(void) { \
        tn_test_current_failed = 0; \
        tn_test_current_todo = 0; \
        tn_test_current_name = #name; \
        tn_test_current_todo_reason = ""; \
        tn_test_fn_##name(); \
        tn_test_count++; \
        if (tn_test_current_failed) { \
            /* a failing TODO test documents a known-open defect and does not
             * fail the suite (standard TAP rule) */ \
            if (!tn_test_current_todo) tn_test_failed++; \
            if (tn_test_current_todo) { \
                printf("not ok %d - %s # TODO %s\n", tn_test_count, #name, \
                       tn_test_current_todo_reason); \
            } else { \
                printf("not ok %d - %s\n", tn_test_count, #name); \
            } \
        } else { \
            printf("ok %d - %s\n", tn_test_count, #name); \
        } \
    } \
    static void tn_test_fn_##name(void)

/* Mark the current test as TODO (known-open defect; failure tolerated). */
#define TN_TODO(reason) \
    do { tn_test_current_todo = 1; tn_test_current_todo_reason = reason; } while (0)

/* End the test early with an SKIP directive (capability not present). */
#define TN_SKIP(reason) \
    do { \
        tn_test_count++; \
        printf("ok %d - %s # SKIP %s\n", tn_test_count, tn_test_current_name, reason); \
        return; \
    } while (0)

/* Integer equality (64-bit compare, works for pointers with casts too). */
#define TN_ASSERT_EQ(got, want) \
    do { \
        long long tn_g_ = (long long)(got); \
        long long tn_w_ = (long long)(want); \
        if (tn_g_ != tn_w_) { \
            printf("#   FAIL %s:%d: %s == %lld, expected %lld\n", \
                   __FILE__, __LINE__, #got, tn_g_, tn_w_); \
            tn_test_current_failed = 1; \
            return; \
        } \
    } while (0)

/* Unsigned equality (for values that may exceed LLONG_MAX, e.g. 0xFFFFFFFF). */
#define TN_ASSERT_EQ_U(got, want) \
    do { \
        unsigned long long tn_g_ = (unsigned long long)(got); \
        unsigned long long tn_w_ = (unsigned long long)(want); \
        if (tn_g_ != tn_w_) { \
            printf("#   FAIL %s:%d: %s == %llu, expected %llu\n", \
                   __FILE__, __LINE__, #got, tn_g_, tn_w_); \
            tn_test_current_failed = 1; \
            return; \
        } \
    } while (0)

#define TN_ASSERT_STREQ(got, want) \
    do { \
        const char *tn_g_ = (got); \
        const char *tn_w_ = (want); \
        if (tn_g_ == NULL || tn_w_ == NULL || strcmp(tn_g_, tn_w_) != 0) { \
            printf("#   FAIL %s:%d: %s == \"%s\", expected \"%s\"\n", \
                   __FILE__, __LINE__, #got, \
                   tn_g_ ? tn_g_ : "(null)", tn_w_ ? tn_w_ : "(null)"); \
            tn_test_current_failed = 1; \
            return; \
        } \
    } while (0)

#define TN_ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            printf("#   FAIL %s:%d: %s is false\n", __FILE__, __LINE__, #cond); \
            tn_test_current_failed = 1; \
            return; \
        } \
    } while (0)

#define TN_ASSERT_FALSE(cond) \
    do { \
        if (cond) { \
            printf("#   FAIL %s:%d: %s is true (expected false)\n", __FILE__, __LINE__, #cond); \
            tn_test_current_failed = 1; \
            return; \
        } \
    } while (0)

#define TN_TEST_PLAN() \
    do { printf("1..%d\n", tn_test_count); } while (0)

/* Run one TN_TEST function (call from main in declaration order). */
#define TN_TEST_RUN(name) name()

static int tn_test_failures(void)
{
    return tn_test_failed;
}

#endif /* TN_TEST_H */
