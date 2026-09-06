/*
 * test_sbtc.c — SocketBaseTagList dispatch semantics (Round 3 §B.1).
 * Unit: src/common/sbtc_dispatch.c (drives the -294 LVO).
 *
 * The dispatcher is pure (no dereferences): pointer-carrying REF cases are
 * asserted by their op/is_ref/value classification; the actual memory access
 * happens in lib_vectors.c on Amiga and is covered by the emulator bench.
 */
#include "tn_test.h"
#include "../../src/common/sbtc_dispatch.h"

/* Tag encoding per SDK: code<<1 | SET | REF (TAG_USER omitted — orthogonal) */
#define TAG(code, set, ref) ((((uint32_t)(code)) << 1) | ((set) ? 1u : 0u) | ((ref) ? 0x8000u : 0u))

static TnSbtcState base_state(void)
{
    TnSbtcState st;
    st.sig_int = 0x1000;
    st.sig_io  = 0x2000;
    st.sig_urg = 0x4000;
    st.sig_event = 0x8000;
    st.errno_val = 35;
    st.herrno_val = 1;
    st.dtablesize = 32;
    st.have_bits = TN_SBTC_HAVE_DNS_API_BIT | TN_SBTC_HAVE_ADDR_CONV_API_BIT;
    st.release_str = 0xDEADBe00; /* opaque pointer-sized value */
    return st;
}

TN_TEST(getval_getref_matrix)
{
    TnSbtcState st = base_state();
    TnSbtcResult r;

    /* GETVAL: caller writes value into ti_Data */
    TN_ASSERT_EQ(tn_sbtc_dispatch_tag(TAG(TN_SBTC_SIGIOMASK, 0, 0), 0, &st, &r), 1);
    TN_ASSERT_TRUE(r.op == TN_SBTC_OP_GET);
    TN_ASSERT_EQ(r.is_ref, 0);
    TN_ASSERT_EQ_U(r.value, 0x2000u);

    /* GETREF: caller writes value through ti_Data (is_ref signalled) */
    TN_ASSERT_EQ(tn_sbtc_dispatch_tag(TAG(TN_SBTC_SIGIOMASK, 0, 1), 0xCAFE, &st, &r), 1);
    TN_ASSERT_TRUE(r.op == TN_SBTC_OP_GET);
    TN_ASSERT_EQ(r.is_ref, 1);
    TN_ASSERT_EQ_U(r.value, 0x2000u);
}

TN_TEST(setval_setref_matrix)
{
    TnSbtcState st = base_state();
    TnSbtcResult r;

    /* SETVAL: value carried directly */
    TN_ASSERT_EQ(tn_sbtc_dispatch_tag(TAG(TN_SBTC_BREAKMASK, 1, 0), 0xABC0, &st, &r), 1);
    TN_ASSERT_TRUE(r.op == TN_SBTC_OP_SET_SIGINT);
    TN_ASSERT_EQ(r.is_ref, 0);
    TN_ASSERT_EQ_U(r.value, 0xABC0u);

    /* SETREF: caller must read through ti_Data (value = the pointer) */
    TN_ASSERT_EQ(tn_sbtc_dispatch_tag(TAG(TN_SBTC_SIGURGMASK, 1, 1), 0x7000, &st, &r), 1);
    TN_ASSERT_TRUE(r.op == TN_SBTC_OP_SET_SIGURG);
    TN_ASSERT_EQ(r.is_ref, 1);
    TN_ASSERT_EQ_U(r.value, 0x7000u);
}

TN_TEST(return_counts_unknown_only)
{
    TnSbtcState st = base_state();
    TnSbtcResult r;

    /* TNET-036: known tags are handled (contribute 0 to the count)... */
    TN_ASSERT_EQ(tn_sbtc_dispatch_tag(TAG(TN_SBTC_ERRNO, 0, 0), 0, &st, &r), 1);
    TN_ASSERT_EQ(tn_sbtc_dispatch_tag(TAG(TN_SBTC_HAVE_DNS_API, 0, 0), 0, &st, &r), 1);
    /* ...unknown codes report unhandled (the LVO counts them) */
    TN_ASSERT_EQ(tn_sbtc_dispatch_tag(TAG(0x3FF0, 0, 0), 0, &st, &r), 0);
    TN_ASSERT_EQ(r.handled, 0);
}

TN_TEST(errno_ptr_widths)
{
    TnSbtcState st = base_state();
    TnSbtcResult r;

    TN_ASSERT_EQ(tn_sbtc_dispatch_tag(TAG(TN_SBTC_ERRNOBYTEPTR, 1, 0), 0x1000, &st, &r), 1);
    TN_ASSERT_TRUE(r.op == TN_SBTC_OP_SET_ERRNO_PTR);
    TN_ASSERT_EQ(r.errno_ptr_width, 1);

    TN_ASSERT_EQ(tn_sbtc_dispatch_tag(TAG(TN_SBTC_ERRNOWORDPTR, 1, 0), 0x1000, &st, &r), 1);
    TN_ASSERT_EQ(r.errno_ptr_width, 2);

    TN_ASSERT_EQ(tn_sbtc_dispatch_tag(TAG(TN_SBTC_ERRNOLONGPTR, 1, 0), 0x1000, &st, &r), 1);
    TN_ASSERT_EQ(r.errno_ptr_width, 4);
}

TN_TEST(capability_truthfulness)
{
    TnSbtcState st = base_state();
    TnSbtcResult r;

    /* enabled in have_bits -> 1 */
    TN_ASSERT_EQ(tn_sbtc_dispatch_tag(TAG(TN_SBTC_HAVE_DNS_API, 0, 0), 99, &st, &r), 1);
    TN_ASSERT_TRUE(r.op == TN_SBTC_OP_GET);
    TN_ASSERT_EQ_U(r.value, 1u);
    /* not enabled -> 0, honestly */
    TN_ASSERT_EQ(tn_sbtc_dispatch_tag(TAG(TN_SBTC_HAVE_LOCAL_DATABASE_API, 0, 0), 99, &st, &r), 1);
    TN_ASSERT_EQ_U(r.value, 0u);
    /* Tier-2 stacks: always 0 */
    TN_ASSERT_EQ(tn_sbtc_dispatch_tag(TAG(TN_SBTC_HAVE_ROUTING_API, 0, 0), 99, &st, &r), 1);
    TN_ASSERT_EQ_U(r.value, 0u);
    TN_ASSERT_EQ(tn_sbtc_dispatch_tag(TAG(TN_SBTC_CAN_SHARE_LIBRARY_BASES, 0, 0), 99, &st, &r), 1);
    TN_ASSERT_EQ_U(r.value, 0u);
}

TN_TEST(errno_set_surfaces_to_caller)
{
    /* SET errno must go through the library's width-aware helper, so the
     * dispatcher reports an op instead of mutating plain state. */
    TnSbtcState st = base_state();
    TnSbtcResult r;

    TN_ASSERT_EQ(tn_sbtc_dispatch_tag(TAG(TN_SBTC_ERRNO, 1, 0), 60, &st, &r), 1);
    TN_ASSERT_TRUE(r.op == TN_SBTC_OP_SET_ERRNO);
    TN_ASSERT_EQ_U(r.value, 60u);
    /* and the state copy is NOT mutated by the pure dispatcher */
    TN_ASSERT_EQ(st.errno_val, 35);
}

TN_TEST(release_string_get)
{
    TnSbtcState st = base_state();
    TnSbtcResult r;

    TN_ASSERT_EQ(tn_sbtc_dispatch_tag(TAG(TN_SBTC_RELEASESTRPTR, 0, 0), 0, &st, &r), 1);
    TN_ASSERT_TRUE(r.op == TN_SBTC_OP_GET);
    TN_ASSERT_EQ_U(r.value, st.release_str);
}

int main(void)
{
    TN_TEST_RUN(getval_getref_matrix);
    TN_TEST_RUN(setval_setref_matrix);
    TN_TEST_RUN(return_counts_unknown_only);
    TN_TEST_RUN(errno_ptr_widths);
    TN_TEST_RUN(capability_truthfulness);
    TN_TEST_RUN(errno_set_surfaces_to_caller);
    TN_TEST_RUN(release_string_get);
    TN_TEST_PLAN();
    return tn_test_failures();
}
