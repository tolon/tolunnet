/*
 * test_route.c — route table lookup (longest-prefix, default, delete).
 *
 * Round 3 §B.1: stub. The route table unit (src/task/route.c) arrives with
 * the §D/T3 interface & routing work; this file exists so the harness
 * reserves its slot and the skip is visible in every TAP run.
 */
#include "tn_test.h"

TN_TEST(route_longest_prefix)
{
    TN_SKIP("route table ships with the §D/T3 interface work (SCOPE v4 T3)");
}

TN_TEST(route_default_and_delete)
{
    TN_SKIP("route table ships with the §D/T3 interface work (SCOPE v4 T3)");
}

int main(void)
{
    TN_TEST_RUN(route_longest_prefix);
    TN_TEST_RUN(route_default_and_delete);
    TN_TEST_PLAN();
    return tn_test_failures();
}
