/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __TESTFS_H__
#define __TESTFS_H__

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS)
#include <mrX/fs/mr_fs.h>
#include <mrX/fs/mr_sb.h>

/* The testfs file-system name */
#define TESTFS_NAME                     "testfs"

/* The failing testfs file-system name */
#define TESTFS_FAIL_NAME                "testfs_fail"

/**
 * @brief Get the testfs file-system object (auto-registered at boot).
 *
 * @return The file-system on success, MR_NULL on failure. The returned object
 * holds one reference; call mr_file_system_put() when done.
 */
mr_file_system_t *testfs_get(void);

/**
 * @brief Get the testfs mount callback (for unit tests that build super-blocks
 * directly).
 */
mr_err_t testfs_mount(mr_super_block_t *sb, const char *source, int flags,
                      void *data);

/**
 * @brief Get the testfs kill-super-block callback.
 */
void testfs_kill_sb(mr_super_block_t *sb);

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */

#endif /* __TESTFS_H__ */
