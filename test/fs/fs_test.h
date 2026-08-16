#ifndef __MR_TEST_FS_H__
#define __MR_TEST_FS_H__

#include <mrX.h>

MR_INLINE mr_err_t fs_test_stat(const char *path, mr_stat_t *stat) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_err_t ret;

    ret = mr_fs_open(&file, path, MR_O_RDONLY);
    if (ret != 0) {
        return ret;
    }
    ret = mr_fs_stat(&file, stat);
    mr_fs_close(&file);
    return ret;
}

MR_INLINE mr_err_t fs_test_unlink(const char *path) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_err_t ret;

    ret = mr_fs_open(&file, path, MR_O_RDONLY);
    if (ret != 0) {
        return ret;
    }
    ret = mr_fs_unlink(&file);
    mr_fs_close(&file);
    return ret;
}

MR_INLINE mr_err_t fs_test_rename(const char *old_path, const char *new_path) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_err_t ret;

    ret = mr_fs_open(&file, old_path, MR_O_RDONLY);
    if (ret != 0) {
        return ret;
    }
    ret = mr_fs_rename(&file, new_path);
    mr_fs_close(&file);
    return ret;
}

#endif /* __MR_TEST_FS_H__ */
