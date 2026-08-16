/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-16        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS)
#include <mrX/fs/mr_dentry.h>
#include <mrX/fs/mr_nsp.h>

static void test_fs_nsp_init_root_get_failure(void) {
    mr_fs_nsp_t *current;
    mr_fs_nsp_t nsp;
    mr_atomic_t saved;
    mr_err_t ret;

    /* Get the active namespace and preserve the root reference */
    current = mr_fs_nsp_current();
    MR_TEST_ASSERT_NE_PTR(current, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(current->root, MR_NULL);
    saved = mr_atomic_load(&current->root->parent.magic_ref);

    /* Force the first root reference acquisition to fail */
    mr_atomic_store(&current->root->parent.magic_ref, MR_REF_MAGIC);
    ret = mr_fs_nsp_init(&nsp);
    MR_TEST_ASSERT_EQ_INT(ret, -MR_EINVAL);
    MR_TEST_ASSERT_EQ_PTR(nsp.root, MR_NULL);
    MR_TEST_ASSERT_EQ_PTR(nsp.cwd, MR_NULL);

    /* Restore the active namespace root reference */
    mr_atomic_store(&current->root->parent.magic_ref, saved);
}
MR_TEST_EXPORT(fs, nsp_init_root_get_failure,
               test_fs_nsp_init_root_get_failure);
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
