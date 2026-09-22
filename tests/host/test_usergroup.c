/*
 * test_usergroup.c — Host unit test suite for usergroup.library.
 *
 * Verifies in-memory database, LVOs, context handling, credentials,
 * and error propagation under ASan/UBSan.
 */

#include "tn_test.h"
#include "../../src/usergroup/usergroup_base.h"

/* Include C source files directly for host unit testing */
#include "../../src/usergroup/ug_db.c"
#include "../../src/usergroup/ug_context.c"
#include "../../src/usergroup/ug_crypt.c"
#include "../../src/usergroup/ug_init.c"

static struct UserGroupBase g_test_base;

static void setup_test_base(void)
{
    memset(&g_test_base, 0, sizeof(g_test_base));
    ug_db_init(&g_test_base);
}

static void teardown_test_base(void)
{
    ug_db_free(&g_test_base);
}

TN_TEST(default_users_exist)
{
    struct passwd *pw;
    setup_test_base();

    pw = ug_lvo_getpwnam("root", &g_test_base);
    TN_ASSERT_TRUE(pw != NULL);
    TN_ASSERT_STREQ(pw->pw_name, "root");
    TN_ASSERT_EQ(pw->pw_uid, 0);
    TN_ASSERT_EQ(pw->pw_gid, 0);

    pw = ug_lvo_getpwnam("amiga", &g_test_base);
    TN_ASSERT_TRUE(pw != NULL);
    TN_ASSERT_STREQ(pw->pw_name, "amiga");
    TN_ASSERT_EQ(pw->pw_uid, 1000);
    TN_ASSERT_EQ(pw->pw_gid, 1000);

    pw = ug_lvo_getpwnam("nobody", &g_test_base);
    TN_ASSERT_TRUE(pw != NULL);
    TN_ASSERT_STREQ(pw->pw_name, "nobody");
    TN_ASSERT_EQ(pw->pw_uid, 65534);

    pw = ug_lvo_getpwnam("nonexistent_user", &g_test_base);
    TN_ASSERT_TRUE(pw == NULL);

    teardown_test_base();
}

TN_TEST(getpwuid_lookup)
{
    struct passwd *pw;
    setup_test_base();

    pw = ug_lvo_getpwuid(0, &g_test_base);
    TN_ASSERT_TRUE(pw != NULL);
    TN_ASSERT_STREQ(pw->pw_name, "root");

    pw = ug_lvo_getpwuid(1000, &g_test_base);
    TN_ASSERT_TRUE(pw != NULL);
    TN_ASSERT_STREQ(pw->pw_name, "amiga");

    pw = ug_lvo_getpwuid(65534, &g_test_base);
    TN_ASSERT_TRUE(pw != NULL);
    TN_ASSERT_STREQ(pw->pw_name, "nobody");

    pw = ug_lvo_getpwuid(9999, &g_test_base);
    TN_ASSERT_TRUE(pw == NULL);

    teardown_test_base();
}

TN_TEST(passwd_iteration)
{
    struct passwd *pw;
    int count = 0;
    setup_test_base();

    ug_lvo_setpwent(&g_test_base);
    while ((pw = ug_lvo_getpwent(&g_test_base)) != NULL) {
        count++;
    }
    TN_ASSERT_EQ(count, 3);

    /* Second iteration */
    count = 0;
    ug_lvo_setpwent(&g_test_base);
    while ((pw = ug_lvo_getpwent(&g_test_base)) != NULL) {
        count++;
    }
    TN_ASSERT_EQ(count, 3);
    ug_lvo_endpwent(&g_test_base);

    teardown_test_base();
}

TN_TEST(default_groups_exist)
{
    struct group *gr;
    setup_test_base();

    gr = ug_lvo_getgrnam("wheel", &g_test_base);
    TN_ASSERT_TRUE(gr != NULL);
    TN_ASSERT_STREQ(gr->gr_name, "wheel");
    TN_ASSERT_EQ(gr->gr_gid, 0);
    TN_ASSERT_TRUE(gr->gr_mem != NULL);
    TN_ASSERT_STREQ(gr->gr_mem[0], "root");
    TN_ASSERT_STREQ(gr->gr_mem[1], "amiga");

    gr = ug_lvo_getgrnam("staff", &g_test_base);
    TN_ASSERT_TRUE(gr != NULL);
    TN_ASSERT_STREQ(gr->gr_name, "staff");
    TN_ASSERT_EQ(gr->gr_gid, 1000);

    gr = ug_lvo_getgrgid(65534, &g_test_base);
    TN_ASSERT_TRUE(gr != NULL);
    TN_ASSERT_STREQ(gr->gr_name, "nobody");

    gr = ug_lvo_getgrnam("ghost_group", &g_test_base);
    TN_ASSERT_TRUE(gr == NULL);

    teardown_test_base();
}

TN_TEST(group_iteration)
{
    struct group *gr;
    int count = 0;
    setup_test_base();

    ug_lvo_setgrent(&g_test_base);
    while ((gr = ug_lvo_getgrent(&g_test_base)) != NULL) {
        count++;
    }
    TN_ASSERT_EQ(count, 3);
    ug_lvo_endgrent(&g_test_base);

    teardown_test_base();
}

TN_TEST(context_uid_and_gid)
{
    setup_test_base();

    TN_ASSERT_EQ(ug_lvo_getuid(&g_test_base), 0);
    TN_ASSERT_EQ(ug_lvo_geteuid(&g_test_base), 0);
    TN_ASSERT_EQ(ug_lvo_getgid(&g_test_base), 0);
    TN_ASSERT_EQ(ug_lvo_getegid(&g_test_base), 0);

    TN_ASSERT_EQ(ug_lvo_setuid(1000, &g_test_base), 0);
    TN_ASSERT_EQ(ug_lvo_getuid(&g_test_base), 1000);
    TN_ASSERT_EQ(ug_lvo_geteuid(&g_test_base), 1000);

    TN_ASSERT_EQ(ug_lvo_setreuid(0, 1000, &g_test_base), 0);
    TN_ASSERT_EQ(ug_lvo_getuid(&g_test_base), 0);
    TN_ASSERT_EQ(ug_lvo_geteuid(&g_test_base), 1000);

    TN_ASSERT_EQ(ug_lvo_setgid(500, &g_test_base), 0);
    TN_ASSERT_EQ(ug_lvo_getgid(&g_test_base), 500);

    TN_ASSERT_EQ(ug_lvo_setregid(0, 500, &g_test_base), 0);
    TN_ASSERT_EQ(ug_lvo_getgid(&g_test_base), 0);
    TN_ASSERT_EQ(ug_lvo_getegid(&g_test_base), 500);

    teardown_test_base();
}

TN_TEST(groups_and_initgroups)
{
    LONG gids[4] = { 10, 20, 30, 40 };
    LONG out_gids[4] = { 0 };
    LONG count;
    setup_test_base();

    TN_ASSERT_EQ(ug_lvo_setgroups(3, gids, &g_test_base), 0);
    count = ug_lvo_getgroups(4, out_gids, &g_test_base);
    TN_ASSERT_EQ(count, 3);
    TN_ASSERT_EQ(out_gids[0], 10);
    TN_ASSERT_EQ(out_gids[1], 20);
    TN_ASSERT_EQ(out_gids[2], 30);

    TN_ASSERT_EQ(ug_lvo_initgroups("amiga", 50, &g_test_base), 0);
    count = ug_lvo_getgroups(4, out_gids, &g_test_base);
    TN_ASSERT_EQ(count, 1);
    TN_ASSERT_EQ(out_gids[0], 50);

    teardown_test_base();
}

TN_TEST(umask_operations)
{
    setup_test_base();

    TN_ASSERT_EQ(ug_lvo_getumask(&g_test_base), 022);
    TN_ASSERT_EQ(ug_lvo_umask(027, &g_test_base), 022);
    TN_ASSERT_EQ(ug_lvo_getumask(&g_test_base), 027);

    teardown_test_base();
}

TN_TEST(login_and_session)
{
    LONG sid;
    setup_test_base();

    TN_ASSERT_STREQ(ug_lvo_getlogin(&g_test_base), "root");
    TN_ASSERT_EQ(ug_lvo_setlogin("amigadev", &g_test_base), 0);
    TN_ASSERT_STREQ(ug_lvo_getlogin(&g_test_base), "amigadev");

    sid = ug_lvo_setsid(&g_test_base);
    TN_ASSERT_TRUE(sid != 0);
    TN_ASSERT_EQ(ug_lvo_getpgrp(&g_test_base), sid);

    teardown_test_base();
}

TN_TEST(setup_context_tags_and_errno)
{
    LONG err_var = 0;
    struct TagItem tags[3];
    setup_test_base();

    tags[0].ti_Tag = UGT_ERRNOLPTR;
    tags[0].ti_Data = (uintptr_t)&err_var;
    tags[1].ti_Tag = UGT_INTRMASK;
    tags[1].ti_Data = 0x1000;
    tags[2].ti_Tag = TAG_DONE;
    tags[2].ti_Data = 0;

    TN_ASSERT_EQ(ug_lvo_ug_setupcontexttaglist("test", tags, &g_test_base), 0);

    ug_set_task_error(&g_test_base, 2); /* ENOENT */
    TN_ASSERT_EQ(err_var, 2);
    TN_ASSERT_EQ(ug_lvo_ug_geterr(&g_test_base), 2);
    TN_ASSERT_STREQ(ug_lvo_ug_strerror(2, &g_test_base), "No such file or directory");

    teardown_test_base();
}

TN_TEST(crypt_and_salt_generation)
{
    UBYTE *res;
    UBYTE salt_buf[4];
    struct passwd pw;
    setup_test_base();

    res = ug_lvo_crypt((UBYTE *)"testpass", (UBYTE *)"ab", &g_test_base);
    TN_ASSERT_TRUE(res != NULL);
    TN_ASSERT_EQ(res[0], 'a');
    TN_ASSERT_EQ(res[1], 'b');
    TN_ASSERT_EQ(strlen((char *)res), 13);

    pw.pw_passwd = "xy12345";
    TN_ASSERT_TRUE(ug_lvo_ug_getsalt(&pw, salt_buf, sizeof(salt_buf), &g_test_base) != NULL);
    TN_ASSERT_EQ(salt_buf[0], 'x');
    TN_ASSERT_EQ(salt_buf[1], 'y');

    teardown_test_base();
}

TN_TEST(dynamic_user_addition)
{
    struct passwd *pw;
    setup_test_base();

    TN_ASSERT_EQ(ug_db_add_user(&g_test_base, "tolon", "secret", 2000, 2000,
                                "Tolon Admin", "DH0:Users/tolon", "C:Shell"), TRUE);

    pw = ug_lvo_getpwnam("tolon", &g_test_base);
    TN_ASSERT_TRUE(pw != NULL);
    TN_ASSERT_STREQ(pw->pw_name, "tolon");
    TN_ASSERT_EQ(pw->pw_uid, 2000);
    TN_ASSERT_EQ(pw->pw_gid, 2000);
    TN_ASSERT_STREQ(pw->pw_gecos, "Tolon Admin");
    TN_ASSERT_STREQ(pw->pw_dir, "DH0:Users/tolon");

    pw = ug_lvo_getpwuid(2000, &g_test_base);
    TN_ASSERT_TRUE(pw != NULL);
    TN_ASSERT_STREQ(pw->pw_name, "tolon");

    teardown_test_base();
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    TN_TEST_RUN(default_users_exist);
    TN_TEST_RUN(getpwuid_lookup);
    TN_TEST_RUN(passwd_iteration);
    TN_TEST_RUN(default_groups_exist);
    TN_TEST_RUN(group_iteration);
    TN_TEST_RUN(context_uid_and_gid);
    TN_TEST_RUN(groups_and_initgroups);
    TN_TEST_RUN(umask_operations);
    TN_TEST_RUN(login_and_session);
    TN_TEST_RUN(setup_context_tags_and_errno);
    TN_TEST_RUN(crypt_and_salt_generation);
    TN_TEST_RUN(dynamic_user_addition);

    TN_TEST_PLAN();
    return tn_test_failures();
}
