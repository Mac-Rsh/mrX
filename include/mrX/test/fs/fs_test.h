/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_TEST_FS_H__
#define __MR_TEST_FS_H__

#include <mrX.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Test
 * @{
 */

#if defined(MR_USE_TEST)
#if defined(MR_USE_FS)
MR_INLINE mr_err_t fs_test_mount_root(const char *fstype, int flags,
                                      void *data) {
    mr_err_t ret;

    /* Host init already mounts rootfs at '/'; replace it for the case */
    ret = mr_fs_umount("/");
    if ((ret != 0) && (ret != -MR_ENOENT)) {
        return ret;
    }
    return mr_fs_mount_root(MR_NULL, fstype, flags, data);
}

MR_INLINE mr_err_t fs_test_stat(const char *path, mr_stat_t *stat) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_err_t ret;

    ret = mr_fs_open(&file, path, MR_O_RDONLY);
    if (ret != 0) {
        return ret;
    }
    ret = mr_fs_fstat(&file, stat);
    mr_fs_close(&file);
    return ret;
}
#endif /* defined(MR_USE_FS) */
#endif /* defined(MR_USE_TEST) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_TEST_FS_H__ */
