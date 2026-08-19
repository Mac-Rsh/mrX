/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-14        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS)
#include <mrX.h>

MR_TEST(fs, dentry) {
    mr_dentry_t *dentry;

    /* Reject names that path lookup treats specially */
    dentry = mr_dentry_create(MR_NULL, "%s", "");
    MR_TEST_EXPECT_EQ_PTR(dentry, MR_NULL);
    dentry = mr_dentry_create(MR_NULL, "%s", ".");
    MR_TEST_EXPECT_EQ_PTR(dentry, MR_NULL);
    dentry = mr_dentry_create(MR_NULL, "%s", "..");
    MR_TEST_EXPECT_EQ_PTR(dentry, MR_NULL);

    /* Reject a multi-component name */
    dentry = mr_dentry_create(MR_NULL, "%s", "a/b");
    MR_TEST_EXPECT_EQ_PTR(dentry, MR_NULL);

    /* Keep the global root name available to namespace implementations */
    dentry = mr_dentry_create(MR_NULL, "%s", "/");
    MR_TEST_EXPECT_NE_PTR(dentry, MR_NULL);
    if (dentry) {
        mr_dentry_del(dentry);
    }
}

MR_TEST(fs, dentry_name_and_empty) {
    mr_dentry_t *dentry;

    dentry = mr_dentry_create(MR_NULL, "file%d", 3);
    MR_TEST_ASSERT_NE_PTR(dentry, MR_NULL);
    if (dentry == MR_NULL) {
        return;
    }

    MR_TEST_EXPECT_EQ_INT(mr_strcmp(MR_DENTRY_NAME(dentry), "file3"), 0);
    MR_TEST_EXPECT_EQ_UINT(MR_DENTRY_NAME_LEN(dentry), 5);
    MR_TEST_EXPECT_EQ_INT(mr_dentry_is_empty(dentry), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(MR_DENTRY_IS_ADDED(dentry), MR_FALSE);
    MR_TEST_EXPECT_NE_PTR(mr_dentry_get(dentry), MR_NULL);
    MR_TEST_EXPECT_EQ_INT(mr_dentry_put(dentry), MR_FALSE);
    mr_dentry_del(dentry);
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
