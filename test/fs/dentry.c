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

static void dentry(void) {
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
MR_TEST_EXPORT(fs, dentry, dentry);
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
