/*
 * test_fdset.c — WaitSelect fd_set / timeout marshaling (Round 3 §B.1).
 * Unit: src/common/fdset_util.c. Target semantics; the daemon adopts them
 * in §C11 (TNET-067) — until then this pins the contract the adoption must
 * match.
 */
#include "tn_test.h"
#include "../../src/common/fdset_util.h"

TN_TEST(fdset_is_64bit)
{
    /* NDK bsdsocket fd_set is 64-bit; bits 32..63 must be addressable even
     * though only 0..31 are valid descriptors for tolunnet. */
    uint64_t set = 0;
    tn_fd_set(&set, 31);
    tn_fd_set(&set, 63);
    TN_ASSERT_TRUE(tn_fd_isset(set, 31));
    TN_ASSERT_TRUE(tn_fd_isset(set, 63));
    tn_fd_clr(&set, 63);
    TN_ASSERT_TRUE(!tn_fd_isset(set, 63));
    TN_ASSERT_EQ_U(set, (uint64_t)1 << 31);
}

TN_TEST(nfds_out_of_table_is_ebadf)
{
    TN_ASSERT_EQ(tn_fdset_check_nfds(0), 0);
    TN_ASSERT_EQ(tn_fdset_check_nfds(1), 0);
    TN_ASSERT_EQ(tn_fdset_check_nfds(32), 0);    /* fds 0..31: valid */
    TN_ASSERT_EQ(tn_fdset_check_nfds(33), TN_EBADF); /* fd 32 out of table */
    TN_ASSERT_EQ(tn_fdset_check_nfds(64), TN_EBADF);
}

TN_TEST(nfds_invalid_is_einval)
{
    TN_ASSERT_EQ(tn_fdset_check_nfds(-1), TN_EINVAL);
    TN_ASSERT_EQ(tn_fdset_check_nfds(65), TN_EINVAL); /* > FD_SETSIZE(64) */
    TN_ASSERT_EQ(tn_fdset_check_nfds(1000), TN_EINVAL);
}

TN_TEST(timeout_conversion)
{
    TN_ASSERT_EQ_U(tn_waitselect_timeout_ms(0, 0), 0u);
    TN_ASSERT_EQ_U(tn_waitselect_timeout_ms(0, 500000), 500u);
    TN_ASSERT_EQ_U(tn_waitselect_timeout_ms(1, 500000), 1500u);
    TN_ASSERT_EQ_U(tn_waitselect_timeout_ms(2, 0), 2000u);
    /* saturation: huge values behave as "forever", never wrap to small */
    TN_ASSERT_EQ_U(tn_waitselect_timeout_ms(0xFFFFFFFFu, 0), 0xFFFFFFFFu);
    TN_ASSERT_EQ_U(tn_waitselect_timeout_ms(0xFFFFFFFFu, 999999u), 0xFFFFFFFFu);
}

TN_TEST(empty_sets_with_timeout)
{
    /* Empty fd sets + finite timeout is a legal "sleep" call: validation
     * passes and the timeout is finite. */
    uint64_t r = 0, w = 0;
    TN_ASSERT_EQ(r == 0 && w == 0, 1);
    TN_ASSERT_EQ(tn_fdset_check_nfds(0), 0);
    TN_ASSERT_EQ_U(tn_waitselect_timeout_ms(0, 200000), 200u);
}

int main(void)
{
    TN_TEST_RUN(fdset_is_64bit);
    TN_TEST_RUN(nfds_out_of_table_is_ebadf);
    TN_TEST_RUN(nfds_invalid_is_einval);
    TN_TEST_RUN(timeout_conversion);
    TN_TEST_RUN(empty_sets_with_timeout);
    TN_TEST_PLAN();
    return tn_test_failures();
}
