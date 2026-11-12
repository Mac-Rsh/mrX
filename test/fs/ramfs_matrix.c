/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-29        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS) && defined(MR_USE_FS_RAMFS)
#include <mrX.h>
#include <mrX/test/fs/fs_test.h>

typedef enum ramfs_matrix_case {
    RAMFS_MATRIX_MKDIR_CREATE,
    RAMFS_MATRIX_MKDIR_NESTED,
    RAMFS_MATRIX_MKDIR_EXISTING,
    RAMFS_MATRIX_MKDIR_PARENT_FILE,
    RAMFS_MATRIX_RMDIR_EMPTY,
    RAMFS_MATRIX_RMDIR_NONEMPTY,
    RAMFS_MATRIX_RMDIR_REGULAR,
    RAMFS_MATRIX_RMDIR_MISSING,
    RAMFS_MATRIX_MKNOD_INVALID,
    RAMFS_MATRIX_OPEN_CREATE,
    RAMFS_MATRIX_OPEN_EXCLUSIVE,
    RAMFS_MATRIX_OPEN_MISSING_PARENT,
    RAMFS_MATRIX_OPEN_MISSING,
    RAMFS_MATRIX_OPEN_DIRECTORY,
    RAMFS_MATRIX_READ_EOF,
    RAMFS_MATRIX_WRITE_ZERO,
    RAMFS_MATRIX_READ_ZERO,
    RAMFS_MATRIX_SEEK_SET,
    RAMFS_MATRIX_SEEK_CUR,
    RAMFS_MATRIX_SEEK_END,
    RAMFS_MATRIX_SEEK_NEGATIVE,
    RAMFS_MATRIX_SEEK_INVALID,
    RAMFS_MATRIX_TRUNCATE_GROW,
    RAMFS_MATRIX_TRUNCATE_SHRINK,
    RAMFS_MATRIX_TRUNCATE_NEGATIVE,
    RAMFS_MATRIX_TRUNCATE_READONLY,
    RAMFS_MATRIX_UNLINK_REGULAR,
    RAMFS_MATRIX_UNLINK_MISSING,
    RAMFS_MATRIX_UNLINK_DIRECTORY,
    RAMFS_MATRIX_RENAME_REGULAR,
    RAMFS_MATRIX_RENAME_MISSING,
    RAMFS_MATRIX_RENAME_DIRECTORY,
    RAMFS_MATRIX_READDIR_EMPTY,
    RAMFS_MATRIX_READDIR_ONE,
    RAMFS_MATRIX_READDIR_END,
    RAMFS_MATRIX_FIFO_CREATE,
    RAMFS_MATRIX_FIFO_EMPTY,
    RAMFS_MATRIX_FIFO_ROUNDTRIP,
    RAMFS_MATRIX_SYNC_UNSUPPORTED,
} ramfs_matrix_case_t;

static void ramfs_matrix_expect(volatile mr_uint32_t *__test_status,
                                mr_int64_t actual, mr_int64_t expected) {
    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

static mr_err_t ramfs_matrix_seed(mr_file_t *file, const char *path,
                                  const char *data, mr_size_t size) {
    mr_err_t ret;

    ret = mr_fs_open(file, path, MR_O_RDWR | MR_O_CREAT);
    if (ret != 0) {
        return ret;
    }
    ret = (mr_err_t)mr_fs_write(file, data, size);
    if (ret == (mr_err_t)size) {
        ret = mr_fs_close(file);
    }
    return ret;
}

static void ramfs_matrix_run(volatile mr_uint32_t *__test_status,
                             ramfs_matrix_case_t test_case) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_stat_t stat;
    mr_dirent_t dirent;
    char buffer[16];
    mr_err_t ret;
    mr_bool_t mounted;

    mounted = MR_FALSE;
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    ramfs_matrix_expect(__test_status, ret, 0);
    if (ret != 0) {
        return;
    }
    mounted = MR_TRUE;

    switch (test_case) {
        case RAMFS_MATRIX_MKDIR_CREATE: {
            ramfs_matrix_expect(__test_status, mr_fs_mkdir("/dir"), 0);
            ramfs_matrix_expect(__test_status, fs_test_stat("/dir", &stat), 0);
            ramfs_matrix_expect(__test_status, stat.type, MR_INODE_DIR);
            break;
        }
        case RAMFS_MATRIX_MKDIR_NESTED: {
            ramfs_matrix_expect(__test_status, mr_fs_mkdir("/top"), 0);
            ramfs_matrix_expect(__test_status, mr_fs_mkdir("/top/child"), 0);
            ramfs_matrix_expect(__test_status,
                                fs_test_stat("/top/child", &stat), 0);
            break;
        }
        case RAMFS_MATRIX_MKDIR_EXISTING: {
            ramfs_matrix_expect(__test_status, mr_fs_mkdir("/dir"), 0);
            ramfs_matrix_expect(__test_status, mr_fs_mkdir("/dir"), -MR_EEXIST);
            break;
        }
        case RAMFS_MATRIX_MKDIR_PARENT_FILE: {
            ramfs_matrix_expect(__test_status,
                                ramfs_matrix_seed(&file, "/file", "x", 1), 0);
            ramfs_matrix_expect(__test_status, mr_fs_mkdir("/file/child"),
                                -MR_ENOTDIR);
            break;
        }
        case RAMFS_MATRIX_RMDIR_EMPTY: {
            ramfs_matrix_expect(__test_status, mr_fs_mkdir("/dir"), 0);
            ramfs_matrix_expect(__test_status, mr_fs_rmdir("/dir"), 0);
            ramfs_matrix_expect(__test_status, fs_test_stat("/dir", &stat),
                                -MR_ENOENT);
            break;
        }
        case RAMFS_MATRIX_RMDIR_NONEMPTY: {
            ramfs_matrix_expect(__test_status, mr_fs_mkdir("/dir"), 0);
            ramfs_matrix_expect(__test_status,
                                ramfs_matrix_seed(&file, "/dir/file", "x", 1),
                                0);
            ramfs_matrix_expect(__test_status, mr_fs_rmdir("/dir"),
                                -MR_ENOTEMPTY);
            break;
        }
        case RAMFS_MATRIX_RMDIR_REGULAR: {
            ramfs_matrix_expect(__test_status,
                                ramfs_matrix_seed(&file, "/file", "x", 1), 0);
            ramfs_matrix_expect(__test_status, mr_fs_rmdir("/file"),
                                -MR_ENOTDIR);
            break;
        }
        case RAMFS_MATRIX_RMDIR_MISSING: {
            ramfs_matrix_expect(__test_status, mr_fs_rmdir("/missing"),
                                -MR_ENOENT);
            break;
        }
        case RAMFS_MATRIX_MKNOD_INVALID: {
            ramfs_matrix_expect(__test_status,
                                mr_fs_mknod("/bad", MR_INODE_NONE, MR_NULL),
                                -MR_EINVAL);
            break;
        }
        case RAMFS_MATRIX_OPEN_CREATE: {
            ramfs_matrix_expect(
                __test_status,
                mr_fs_open(&file, "/new", MR_O_RDWR | MR_O_CREAT), 0);
            ramfs_matrix_expect(__test_status, mr_fs_close(&file), 0);
            ramfs_matrix_expect(__test_status, fs_test_stat("/new", &stat), 0);
            break;
        }
        case RAMFS_MATRIX_OPEN_EXCLUSIVE: {
            ramfs_matrix_expect(
                __test_status,
                mr_fs_open(&file, "/new", MR_O_RDWR | MR_O_CREAT | MR_O_EXCL),
                0);
            ramfs_matrix_expect(__test_status, mr_fs_close(&file), 0);
            ramfs_matrix_expect(
                __test_status,
                mr_fs_open(&file, "/new", MR_O_RDWR | MR_O_CREAT | MR_O_EXCL),
                -MR_EEXIST);
            break;
        }
        case RAMFS_MATRIX_OPEN_MISSING_PARENT: {
            ramfs_matrix_expect(
                __test_status,
                mr_fs_open(&file, "/missing/new", MR_O_RDWR | MR_O_CREAT),
                -MR_ENOENT);
            break;
        }
        case RAMFS_MATRIX_OPEN_MISSING: {
            ramfs_matrix_expect(__test_status,
                                mr_fs_open(&file, "/missing", MR_O_RDONLY),
                                -MR_ENOENT);
            break;
        }
        case RAMFS_MATRIX_OPEN_DIRECTORY: {
            ramfs_matrix_expect(__test_status, mr_fs_mkdir("/dir"), 0);
            ramfs_matrix_expect(
                __test_status,
                mr_fs_open(&file, "/dir", MR_O_RDONLY | MR_O_DIRECTORY), 0);
            ramfs_matrix_expect(__test_status, mr_fs_close(&file), 0);
            break;
        }
        case RAMFS_MATRIX_READ_EOF: {
            ramfs_matrix_expect(__test_status,
                                ramfs_matrix_seed(&file, "/file", "x", 1), 0);
            ramfs_matrix_expect(__test_status,
                                mr_fs_open(&file, "/file", MR_O_RDONLY), 0);
            ramfs_matrix_expect(__test_status,
                                mr_fs_lseek(&file, 1, MR_SEEK_SET), 1);
            ramfs_matrix_expect(__test_status, mr_fs_read(&file, buffer, 1), 0);
            ramfs_matrix_expect(__test_status, mr_fs_close(&file), 0);
            break;
        }
        case RAMFS_MATRIX_WRITE_ZERO: {
            ramfs_matrix_expect(
                __test_status,
                mr_fs_open(&file, "/file", MR_O_RDWR | MR_O_CREAT), 0);
            ramfs_matrix_expect(__test_status, mr_fs_write(&file, "x", 0), 0);
            ramfs_matrix_expect(__test_status, mr_fs_fstat(&file, &stat), 0);
            ramfs_matrix_expect(__test_status, stat.size, 0);
            ramfs_matrix_expect(__test_status, mr_fs_close(&file), 0);
            break;
        }
        case RAMFS_MATRIX_READ_ZERO: {
            ramfs_matrix_expect(__test_status,
                                ramfs_matrix_seed(&file, "/file", "x", 1), 0);
            ramfs_matrix_expect(__test_status,
                                mr_fs_open(&file, "/file", MR_O_RDONLY), 0);
            ramfs_matrix_expect(__test_status, mr_fs_read(&file, buffer, 0), 0);
            ramfs_matrix_expect(__test_status,
                                mr_fs_lseek(&file, 0, MR_SEEK_CUR), 0);
            ramfs_matrix_expect(__test_status, mr_fs_close(&file), 0);
            break;
        }
        case RAMFS_MATRIX_SEEK_SET: {
            ramfs_matrix_expect(
                __test_status,
                mr_fs_open(&file, "/file", MR_O_RDWR | MR_O_CREAT), 0);
            ramfs_matrix_expect(__test_status,
                                mr_fs_lseek(&file, 7, MR_SEEK_SET), 7);
            ramfs_matrix_expect(__test_status, mr_fs_close(&file), 0);
            break;
        }
        case RAMFS_MATRIX_SEEK_CUR: {
            ramfs_matrix_expect(
                __test_status,
                mr_fs_open(&file, "/file", MR_O_RDWR | MR_O_CREAT), 0);
            ramfs_matrix_expect(__test_status,
                                mr_fs_lseek(&file, 3, MR_SEEK_SET), 3);
            ramfs_matrix_expect(__test_status,
                                mr_fs_lseek(&file, 2, MR_SEEK_CUR), 5);
            ramfs_matrix_expect(__test_status, mr_fs_close(&file), 0);
            break;
        }
        case RAMFS_MATRIX_SEEK_END: {
            ramfs_matrix_expect(__test_status,
                                ramfs_matrix_seed(&file, "/file", "abc", 3), 0);
            ramfs_matrix_expect(__test_status,
                                mr_fs_open(&file, "/file", MR_O_RDWR), 0);
            ramfs_matrix_expect(__test_status,
                                mr_fs_lseek(&file, -1, MR_SEEK_END), 2);
            ramfs_matrix_expect(__test_status, mr_fs_close(&file), 0);
            break;
        }
        case RAMFS_MATRIX_SEEK_NEGATIVE: {
            ramfs_matrix_expect(
                __test_status,
                mr_fs_open(&file, "/file", MR_O_RDWR | MR_O_CREAT), 0);
            ramfs_matrix_expect(
                __test_status, mr_fs_lseek(&file, -1, MR_SEEK_SET), -MR_EINVAL);
            ramfs_matrix_expect(__test_status, mr_fs_close(&file), 0);
            break;
        }
        case RAMFS_MATRIX_SEEK_INVALID: {
            ramfs_matrix_expect(
                __test_status,
                mr_fs_open(&file, "/file", MR_O_RDWR | MR_O_CREAT), 0);
            ramfs_matrix_expect(__test_status, mr_fs_lseek(&file, 0, 99),
                                -MR_EINVAL);
            ramfs_matrix_expect(__test_status, mr_fs_close(&file), 0);
            break;
        }
        case RAMFS_MATRIX_TRUNCATE_GROW: {
            ramfs_matrix_expect(
                __test_status,
                mr_fs_open(&file, "/file", MR_O_RDWR | MR_O_CREAT), 0);
            ramfs_matrix_expect(__test_status, mr_fs_ftruncate(&file, 8), 0);
            ramfs_matrix_expect(__test_status, mr_fs_fstat(&file, &stat), 0);
            ramfs_matrix_expect(__test_status, stat.size, 8);
            ramfs_matrix_expect(__test_status, mr_fs_close(&file), 0);
            break;
        }
        case RAMFS_MATRIX_TRUNCATE_SHRINK: {
            ramfs_matrix_expect(
                __test_status, ramfs_matrix_seed(&file, "/file", "abcd", 4), 0);
            ramfs_matrix_expect(__test_status,
                                mr_fs_open(&file, "/file", MR_O_RDWR), 0);
            ramfs_matrix_expect(__test_status, mr_fs_ftruncate(&file, 2), 0);
            ramfs_matrix_expect(__test_status, mr_fs_fstat(&file, &stat), 0);
            ramfs_matrix_expect(__test_status, stat.size, 2);
            ramfs_matrix_expect(__test_status, mr_fs_close(&file), 0);
            break;
        }
        case RAMFS_MATRIX_TRUNCATE_NEGATIVE: {
            ramfs_matrix_expect(
                __test_status,
                mr_fs_open(&file, "/file", MR_O_RDWR | MR_O_CREAT), 0);
            ramfs_matrix_expect(__test_status, mr_fs_ftruncate(&file, -1),
                                -MR_EINVAL);
            ramfs_matrix_expect(__test_status, mr_fs_close(&file), 0);
            break;
        }
        case RAMFS_MATRIX_TRUNCATE_READONLY: {
            ramfs_matrix_expect(__test_status,
                                ramfs_matrix_seed(&file, "/file", "x", 1), 0);
            ramfs_matrix_expect(__test_status,
                                mr_fs_open(&file, "/file", MR_O_RDONLY), 0);
            ramfs_matrix_expect(__test_status, mr_fs_ftruncate(&file, 0),
                                -MR_EBADF);
            ramfs_matrix_expect(__test_status, mr_fs_close(&file), 0);
            break;
        }
        case RAMFS_MATRIX_UNLINK_REGULAR: {
            ramfs_matrix_expect(__test_status,
                                ramfs_matrix_seed(&file, "/file", "x", 1), 0);
            ramfs_matrix_expect(__test_status, mr_fs_unlink("/file"), 0);
            ramfs_matrix_expect(__test_status, fs_test_stat("/file", &stat),
                                -MR_ENOENT);
            break;
        }
        case RAMFS_MATRIX_UNLINK_MISSING: {
            ramfs_matrix_expect(__test_status, mr_fs_unlink("/missing"),
                                -MR_ENOENT);
            break;
        }
        case RAMFS_MATRIX_UNLINK_DIRECTORY: {
            ramfs_matrix_expect(__test_status, mr_fs_mkdir("/dir"), 0);
            ramfs_matrix_expect(__test_status, mr_fs_unlink("/dir"),
                                -MR_EISDIR);
            break;
        }
        case RAMFS_MATRIX_RENAME_REGULAR: {
            ramfs_matrix_expect(__test_status,
                                ramfs_matrix_seed(&file, "/old", "x", 1), 0);
            ramfs_matrix_expect(__test_status, mr_fs_rename("/old", "/new"), 0);
            ramfs_matrix_expect(__test_status, fs_test_stat("/old", &stat),
                                -MR_ENOENT);
            ramfs_matrix_expect(__test_status, fs_test_stat("/new", &stat), 0);
            break;
        }
        case RAMFS_MATRIX_RENAME_MISSING: {
            ramfs_matrix_expect(__test_status, mr_fs_rename("/missing", "/new"),
                                -MR_ENOENT);
            break;
        }
        case RAMFS_MATRIX_RENAME_DIRECTORY: {
            ramfs_matrix_expect(__test_status, mr_fs_mkdir("/old"), 0);
            ramfs_matrix_expect(__test_status, mr_fs_rename("/old", "/new"), 0);
            ramfs_matrix_expect(__test_status, fs_test_stat("/new", &stat), 0);
            ramfs_matrix_expect(__test_status, stat.type, MR_INODE_DIR);
            break;
        }
        case RAMFS_MATRIX_READDIR_EMPTY: {
            ramfs_matrix_expect(__test_status, mr_fs_mkdir("/dir"), 0);
            ramfs_matrix_expect(__test_status,
                                mr_fs_open(&file, "/dir", MR_O_RDONLY), 0);
            ramfs_matrix_expect(__test_status, mr_fs_readdir(&file, &dirent),
                                0);
            ramfs_matrix_expect(__test_status, mr_fs_close(&file), 0);
            break;
        }
        case RAMFS_MATRIX_READDIR_ONE: {
            ramfs_matrix_expect(__test_status, mr_fs_mkdir("/dir"), 0);
            ramfs_matrix_expect(__test_status,
                                ramfs_matrix_seed(&file, "/dir/item", "x", 1),
                                0);
            ramfs_matrix_expect(__test_status,
                                mr_fs_open(&file, "/dir", MR_O_RDONLY), 0);
            ramfs_matrix_expect(__test_status, mr_fs_readdir(&file, &dirent),
                                1);
            MR_TEST_EXPECT_EQ_INT(mr_strcmp(dirent.name, "item"), 0);
            ramfs_matrix_expect(__test_status, dirent.type, MR_INODE_REG);
            ramfs_matrix_expect(__test_status, mr_fs_close(&file), 0);
            break;
        }
        case RAMFS_MATRIX_READDIR_END: {
            ramfs_matrix_expect(__test_status, mr_fs_mkdir("/dir"), 0);
            ramfs_matrix_expect(__test_status,
                                ramfs_matrix_seed(&file, "/dir/item", "x", 1),
                                0);
            ramfs_matrix_expect(__test_status,
                                mr_fs_open(&file, "/dir", MR_O_RDONLY), 0);
            ramfs_matrix_expect(__test_status, mr_fs_readdir(&file, &dirent),
                                1);
            ramfs_matrix_expect(__test_status, mr_fs_readdir(&file, &dirent),
                                0);
            ramfs_matrix_expect(__test_status, mr_fs_close(&file), 0);
            break;
        }
        case RAMFS_MATRIX_FIFO_CREATE: {
#if defined(MR_USE_FS_RAMFS_FIFO)
            ramfs_matrix_expect(__test_status, mr_fs_mkfifo("/fifo"), 0);
            ret = fs_test_stat("/fifo", &stat);
            ramfs_matrix_expect(__test_status, ret, 0);
            if (ret == 0) {
                ramfs_matrix_expect(__test_status, stat.type, MR_INODE_FIFO);
            }
#else
            ramfs_matrix_expect(__test_status, mr_fs_mkfifo("/fifo"),
                                -MR_ENOSYS);
            ramfs_matrix_expect(__test_status, fs_test_stat("/fifo", &stat),
                                -MR_ENOENT);
#endif /* defined(MR_USE_FS_RAMFS_FIFO) */
            break;
        }
#if defined(MR_USE_FS_RAMFS_FIFO)
        case RAMFS_MATRIX_FIFO_EMPTY: {
            ramfs_matrix_expect(__test_status, mr_fs_mkfifo("/fifo"), 0);
            ramfs_matrix_expect(__test_status,
                                mr_fs_open(&file, "/fifo", MR_O_RDWR), 0);
            ramfs_matrix_expect(__test_status, mr_fs_read(&file, buffer, 1), 0);
            ramfs_matrix_expect(__test_status, mr_fs_close(&file), 0);
            break;
        }
        case RAMFS_MATRIX_FIFO_ROUNDTRIP: {
            ramfs_matrix_expect(__test_status, mr_fs_mkfifo("/fifo"), 0);
            ret = mr_fs_open(&file, "/fifo", MR_O_RDWR);
            ramfs_matrix_expect(__test_status, ret, 0);
            if (ret != 0) {
                break;
            }
            ramfs_matrix_expect(__test_status, mr_fs_write(&file, "x", 1), 1);
            ret = (mr_err_t)mr_fs_read(&file, buffer, 1);
            ramfs_matrix_expect(__test_status, ret, 1);
            if (ret == 1) {
                MR_TEST_EXPECT_EQ_INT(buffer[0], 'x');
            }
            ramfs_matrix_expect(__test_status, mr_fs_close(&file), 0);
            break;
        }
#endif /* defined(MR_USE_FS_RAMFS_FIFO) */
        case RAMFS_MATRIX_SYNC_UNSUPPORTED: {
            ramfs_matrix_expect(
                __test_status,
                mr_fs_open(&file, "/file", MR_O_RDWR | MR_O_CREAT), 0);
            ramfs_matrix_expect(__test_status, mr_fs_fsync(&file), -MR_ENOSYS);
            ramfs_matrix_expect(__test_status, mr_fs_ioctl(&file, 1, MR_NULL),
                                -MR_ENOSYS);
            ramfs_matrix_expect(__test_status, mr_fs_close(&file), 0);
            break;
        }
        default: {
            ramfs_matrix_expect(__test_status, MR_FALSE, MR_TRUE);
            break;
        }
    }

    if (file.inode) {
        ramfs_matrix_expect(__test_status, mr_fs_close(&file), 0);
    }
    if (mounted) {
        ramfs_matrix_expect(__test_status, mr_fs_umount("/"), 0);
    }
}

#define RAMFS_MATRIX_CASE(_name, _case)                                        \
    MR_TEST(fs_matrix, _name) {                                                \
        ramfs_matrix_run(__test_status, _case);                                \
    }

RAMFS_MATRIX_CASE(mkdir_create, RAMFS_MATRIX_MKDIR_CREATE)
RAMFS_MATRIX_CASE(mkdir_nested, RAMFS_MATRIX_MKDIR_NESTED)
RAMFS_MATRIX_CASE(mkdir_existing, RAMFS_MATRIX_MKDIR_EXISTING)
RAMFS_MATRIX_CASE(mkdir_parent_file, RAMFS_MATRIX_MKDIR_PARENT_FILE)
RAMFS_MATRIX_CASE(rmdir_empty, RAMFS_MATRIX_RMDIR_EMPTY)
RAMFS_MATRIX_CASE(rmdir_nonempty, RAMFS_MATRIX_RMDIR_NONEMPTY)
RAMFS_MATRIX_CASE(rmdir_regular, RAMFS_MATRIX_RMDIR_REGULAR)
RAMFS_MATRIX_CASE(rmdir_missing, RAMFS_MATRIX_RMDIR_MISSING)
RAMFS_MATRIX_CASE(mknod_invalid, RAMFS_MATRIX_MKNOD_INVALID)
RAMFS_MATRIX_CASE(open_create, RAMFS_MATRIX_OPEN_CREATE)
RAMFS_MATRIX_CASE(open_exclusive, RAMFS_MATRIX_OPEN_EXCLUSIVE)
RAMFS_MATRIX_CASE(open_missing_parent, RAMFS_MATRIX_OPEN_MISSING_PARENT)
RAMFS_MATRIX_CASE(open_missing, RAMFS_MATRIX_OPEN_MISSING)
RAMFS_MATRIX_CASE(open_directory, RAMFS_MATRIX_OPEN_DIRECTORY)
RAMFS_MATRIX_CASE(read_eof, RAMFS_MATRIX_READ_EOF)
RAMFS_MATRIX_CASE(write_zero, RAMFS_MATRIX_WRITE_ZERO)
RAMFS_MATRIX_CASE(read_zero, RAMFS_MATRIX_READ_ZERO)
RAMFS_MATRIX_CASE(seek_set, RAMFS_MATRIX_SEEK_SET)
RAMFS_MATRIX_CASE(seek_cur, RAMFS_MATRIX_SEEK_CUR)
RAMFS_MATRIX_CASE(seek_end, RAMFS_MATRIX_SEEK_END)
RAMFS_MATRIX_CASE(seek_negative, RAMFS_MATRIX_SEEK_NEGATIVE)
RAMFS_MATRIX_CASE(seek_invalid, RAMFS_MATRIX_SEEK_INVALID)
RAMFS_MATRIX_CASE(truncate_grow, RAMFS_MATRIX_TRUNCATE_GROW)
RAMFS_MATRIX_CASE(truncate_shrink, RAMFS_MATRIX_TRUNCATE_SHRINK)
RAMFS_MATRIX_CASE(truncate_negative, RAMFS_MATRIX_TRUNCATE_NEGATIVE)
RAMFS_MATRIX_CASE(truncate_readonly, RAMFS_MATRIX_TRUNCATE_READONLY)
RAMFS_MATRIX_CASE(unlink_regular, RAMFS_MATRIX_UNLINK_REGULAR)
RAMFS_MATRIX_CASE(unlink_missing, RAMFS_MATRIX_UNLINK_MISSING)
RAMFS_MATRIX_CASE(unlink_directory, RAMFS_MATRIX_UNLINK_DIRECTORY)
RAMFS_MATRIX_CASE(rename_regular, RAMFS_MATRIX_RENAME_REGULAR)
RAMFS_MATRIX_CASE(rename_missing, RAMFS_MATRIX_RENAME_MISSING)
RAMFS_MATRIX_CASE(rename_directory, RAMFS_MATRIX_RENAME_DIRECTORY)
RAMFS_MATRIX_CASE(readdir_empty, RAMFS_MATRIX_READDIR_EMPTY)
RAMFS_MATRIX_CASE(readdir_one, RAMFS_MATRIX_READDIR_ONE)
RAMFS_MATRIX_CASE(readdir_end, RAMFS_MATRIX_READDIR_END)
RAMFS_MATRIX_CASE(fifo_create, RAMFS_MATRIX_FIFO_CREATE)
#if defined(MR_USE_FS_RAMFS_FIFO)
RAMFS_MATRIX_CASE(fifo_empty, RAMFS_MATRIX_FIFO_EMPTY)
RAMFS_MATRIX_CASE(fifo_roundtrip, RAMFS_MATRIX_FIFO_ROUNDTRIP)
#endif /* defined(MR_USE_FS_RAMFS_FIFO) */
RAMFS_MATRIX_CASE(sync_unsupported, RAMFS_MATRIX_SYNC_UNSUPPORTED)

#undef RAMFS_MATRIX_CASE

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) &&                        \
          defined(MR_USE_FS_RAMFS) */
