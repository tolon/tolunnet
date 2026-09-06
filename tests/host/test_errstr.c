/*
 * test_errstr.c — unit tests for portable network & OS error strings.
 */
#include "tn_test.h"
#include "../../src/common/errstr.h"
#include <string.h>

TN_TEST(strerror_known_and_unknown)
{
    TN_ASSERT_STREQ(tn_strerror(0), "Undefined error: 0");
    TN_ASSERT_STREQ(tn_strerror(1), "Operation not permitted");
    TN_ASSERT_STREQ(tn_strerror(9), "Bad file descriptor");
    TN_ASSERT_STREQ(tn_strerror(22), "Invalid argument");
    TN_ASSERT_STREQ(tn_strerror(61), "Connection refused");
    TN_ASSERT_STREQ(tn_strerror(81), "Need authenticator");
    TN_ASSERT_STREQ(tn_strerror(-1), "Unknown error");
    TN_ASSERT_STREQ(tn_strerror(999), "Unknown error");
}

TN_TEST(hstrerror_known_and_unknown)
{
    TN_ASSERT_STREQ(tn_hstrerror(0), "Resolver Error 0 (no error)");
    TN_ASSERT_STREQ(tn_hstrerror(1), "Unknown host");
    TN_ASSERT_STREQ(tn_hstrerror(2), "Host name lookup failure");
    TN_ASSERT_STREQ(tn_hstrerror(3), "Unknown server error");
    TN_ASSERT_STREQ(tn_hstrerror(4), "No address associated with name");
    TN_ASSERT_STREQ(tn_hstrerror(99), "Unknown resolver error");
}

TN_TEST(ioerror_known_and_unknown)
{
    TN_ASSERT_STREQ(tn_ioerror(0), "No error");
    /* Accepts negative */
    TN_ASSERT_STREQ(tn_ioerror(-1), "Device or unit failed to open");
    TN_ASSERT_STREQ(tn_ioerror(-6), "Device unit is busy");
    /* Accepts positive */
    TN_ASSERT_STREQ(tn_ioerror(1), "Device or unit failed to open");
    TN_ASSERT_STREQ(tn_ioerror(7), "Hardware failed self-test");
    TN_ASSERT_STREQ(tn_ioerror(50), "Unknown I/O error");
}

TN_TEST(s2error_known_and_unknown)
{
    TN_ASSERT_STREQ(tn_s2error(0), "No error");
    TN_ASSERT_STREQ(tn_s2error(1), "Resource allocation failure");
    TN_ASSERT_STREQ(tn_s2error(3), "Bad argument");
    TN_ASSERT_STREQ(tn_s2error(10), "Driver is offline");
    TN_ASSERT_STREQ(tn_s2error(11), "Transmission attempt failed");
    TN_ASSERT_STREQ(tn_s2error(42), "Unknown SANA-II error");
}

TN_TEST(s2werror_known_and_unknown)
{
    TN_ASSERT_STREQ(tn_s2werror(0), "Generic error");
    TN_ASSERT_STREQ(tn_s2werror(2), "Unit is currently online");
    TN_ASSERT_STREQ(tn_s2werror(23), "Authentication failed");
    TN_ASSERT_STREQ(tn_s2werror(14), "Unknown SANA-II wire error"); /* code 14 does not exist in spec */
    TN_ASSERT_STREQ(tn_s2werror(100), "Unknown SANA-II wire error");
}

int main(void)
{
    TN_TEST_RUN(strerror_known_and_unknown);
    TN_TEST_RUN(hstrerror_known_and_unknown);
    TN_TEST_RUN(ioerror_known_and_unknown);
    TN_TEST_RUN(s2error_known_and_unknown);
    TN_TEST_RUN(s2werror_known_and_unknown);
    TN_TEST_PLAN();
    return tn_test_failures();
}
