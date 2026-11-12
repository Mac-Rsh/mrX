/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-22        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS) && defined(MR_USE_FS_RAMFS)
#include <mrX.h>
#include <mrX/test/fs/fs_test.h>

#if defined(MR_USE_FS_RAMFS_FIFO) && (MR_CFG_FS_RAMFS_FIFO_SIZE == 32)
#define RAMFS_FIFO_CAP_DEFAULT (32)
#endif /* defined(MR_USE_FS_RAMFS_FIFO) && (MR_CFG_FS_RAMFS_FIFO_SIZE == 32)   \
        */
#define RAMFS_RECLAIM_CYCLES (8)

static void ramfs_expect(volatile mr_uint32_t *__test_status, const char *name,
                         mr_int64_t actual, mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

static void ramfs_heap_expect(volatile mr_uint32_t *__test_status,
                              const mr_heap_info_t *before,
                              const mr_heap_info_t *after) {
    ramfs_expect(__test_status, "ramfs heap restored",
                 after->used_bytes == before->used_bytes, MR_TRUE);
    ramfs_expect(__test_status, "ramfs heap total kept",
                 after->total_bytes == before->total_bytes, MR_TRUE);
}

MR_TEST(fs, ramfs_regular_edges) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_stat_t stat;
    char buffer[32];
    mr_ssize_t io_ret;
    mr_err_t ret;

    /* Mount RamFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    ramfs_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Create a regular file */
    ret = mr_fs_open(&file, "/reg", MR_O_RDWR | MR_O_CREAT);
    ramfs_expect(__test_status, "create reg file", ret, 0);
    if (ret != 0) {
        goto _exit;
    }

    /* Zero-length operations succeed without side effects */
    ramfs_expect(__test_status, "write zero", mr_fs_write(&file, "x", 0), 0);
    ramfs_expect(__test_status, "read zero", mr_fs_read(&file, buffer, 0), 0);
    ramfs_expect(__test_status, "read at eof", mr_fs_read(&file, buffer, 4), 0);

    /* Append at the end updates size and content */
    ramfs_expect(__test_status, "seek end", mr_fs_lseek(&file, 0, MR_SEEK_END),
                 0);
    ramfs_expect(__test_status, "append write", mr_fs_write(&file, "hello", 5),
                 5);
    ramfs_expect(__test_status, "stat append", mr_fs_fstat(&file, &stat), 0);
    ramfs_expect(__test_status, "append size", stat.size, 5);

    /* Partial read stops at end of file */
    ramfs_expect(__test_status, "seek mid", mr_fs_lseek(&file, 3, MR_SEEK_SET),
                 3);
    io_ret = mr_fs_read(&file, buffer, sizeof(buffer));
    ramfs_expect(__test_status, "partial read", io_ret, 2);
    if ((io_ret == 2) && (mr_memcmp(buffer, "lo", 2) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "lo", 2), 0);
    }
    ramfs_expect(__test_status, "position after partial read",
                 mr_fs_lseek(&file, 0, MR_SEEK_CUR), 5);

    /* Writing past the signed position range is rejected */
    ramfs_expect(__test_status, "seek max",
                 mr_fs_lseek(&file, MR_INT64_MAX, MR_SEEK_SET), MR_INT64_MAX);
    ramfs_expect(__test_status, "write overflow pos",
                 mr_fs_write(&file, "AB", 2), -MR_ERANGE);

    /* Unlink while open keeps the handle usable until close */
    ramfs_expect(__test_status, "unlink open reg file", mr_fs_unlink("/reg"),
                 0);
    ramfs_expect(__test_status, "read after unlink",
                 mr_fs_lseek(&file, 0, MR_SEEK_SET), 0);
    io_ret = mr_fs_read(&file, buffer, sizeof(buffer));
    ramfs_expect(__test_status, "read after unlink data", io_ret, 5);

    ramfs_expect(__test_status, "close reg file", mr_fs_close(&file), 0);
    ramfs_expect(__test_status, "stat unlinked reg file",
                 fs_test_stat("/reg", &stat), -MR_ENOENT);

_exit:
    /* Unmount and verify all allocations were released */
    ramfs_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}

#if defined(MR_USE_FS_RAMFS_FIFO) && (MR_CFG_FS_RAMFS_FIFO_SIZE == 32)
MR_TEST(fs, ramfs_fifo_ring) {
    static const char full[] = "FGHIJKLMNOPQRSTVWXYZ123456789abc";
    mr_heap_info_t heap_before;
    mr_heap_info_t heap_after;
    mr_file_t file = MR_FILE_INIT(file);
    char buffer[40];
    mr_ssize_t io_ret;
    mr_err_t ret;

    /* Mount RamFS */
    heap_before = mr_heap_information(MR_TRUE);
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    ramfs_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Create a FIFO with the default capacity */
    ramfs_expect(__test_status, "mkfifo", mr_fs_mkfifo("/fifo"), 0);
    ret = mr_fs_open(&file, "/fifo", MR_O_RDWR);
    ramfs_expect(__test_status, "open fifo", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    ramfs_expect(__test_status, "fifo seek rejected",
                 mr_fs_lseek(&file, 0, MR_SEEK_SET), -MR_ESPIPE);

    /* Fill, drain partially, refill and wrap around the ring */
    ramfs_expect(__test_status, "fifo fill first",
                 mr_fs_write(&file, "ABCDEFGHIJKLMNOPQRST", 20), 20);
    ramfs_expect(__test_status, "fifo drain head", mr_fs_read(&file, buffer, 5),
                 5);
    ramfs_expect(__test_status, "fifo refill",
                 mr_fs_write(&file, "VWXYZ12345", 10), 10);
    ramfs_expect(__test_status, "fifo write clamped to space",
                 mr_fs_write(&file, "6789abcdef", 10), 7);
    ramfs_expect(__test_status, "fifo write when full",
                 mr_fs_write(&file, "x", 1), 0);

    /* Drain everything and verify the logical order survived the wrap */
    io_ret = 0;
    while (1) {
        mr_ssize_t n = mr_fs_read(&file, buffer + io_ret,
                                  sizeof(buffer) - (mr_size_t)io_ret);
        if (n <= 0) {
            break;
        }
        io_ret += n;
    }
    ramfs_expect(__test_status, "fifo drained length", io_ret,
                 RAMFS_FIFO_CAP_DEFAULT);
    if (io_ret != RAMFS_FIFO_CAP_DEFAULT) {
        goto _close;
    }
    MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, full, RAMFS_FIFO_CAP_DEFAULT), 0);

    /* Empty FIFO reads return zero */
    ramfs_expect(__test_status, "fifo read empty",
                 mr_fs_read(&file, buffer, sizeof(buffer)), 0);

    /* Zero-length operations never touch the queue state */
    ramfs_expect(__test_status, "fifo write zero", mr_fs_write(&file, "x", 0),
                 0);
    ramfs_expect(__test_status, "fifo read zero", mr_fs_read(&file, buffer, 0),
                 0);

_close:
    ramfs_expect(__test_status, "close fifo", mr_fs_close(&file), 0);
_exit:
    /* FIFO nodes unlink like any other node */
    ramfs_expect(__test_status, "unlink fifo", mr_fs_unlink("/fifo"), 0);
    ramfs_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    heap_after = mr_heap_information(MR_TRUE);
    ramfs_heap_expect(__test_status, &heap_before, &heap_after);
    return;
}

#endif /* defined(MR_USE_FS_RAMFS_FIFO) && (MR_CFG_FS_RAMFS_FIFO_SIZE == 32)   \
        */

MR_TEST(fs, ramfs_reclaim_cycles) {
    mr_heap_info_t heap_before;
    mr_heap_info_t heap_after;
    mr_file_t file = MR_FILE_INIT(file);
    mr_stat_t stat;
    char path[24];
    int i;
    mr_err_t ret;
    mr_bool_t mounted = MR_FALSE;

    /* Mount RamFS with a heap snapshot before the test-owned allocations */
    heap_before = mr_heap_information(MR_TRUE);
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    ramfs_expect(__test_status, "mount reclaim ramfs", ret, 0);
    if (ret != 0) {
        return;
    }
    mounted = MR_TRUE;

    /* Grow, shrink, free, and unlink a fresh regular file on every cycle */
    for (i = 0; i < RAMFS_RECLAIM_CYCLES; i++) {
        mr_snprintf(path, sizeof(path), "/reclaim-%d", i);
        ret = mr_fs_open(&file, path, MR_O_RDWR | MR_O_CREAT | MR_O_EXCL);
        ramfs_expect(__test_status, "create reclaim file", ret, 0);
        if (ret != 0) {
            goto _exit;
        }
        ramfs_expect(__test_status, "write reclaim prefix",
                     mr_fs_write(&file, "prefix", 6), 6);
        ramfs_expect(__test_status, "seek reclaim hole",
                     mr_fs_lseek(&file, 64, MR_SEEK_SET), 64);
        ramfs_expect(__test_status, "write reclaim tail",
                     mr_fs_write(&file, "tail", 4), 4);
        ramfs_expect(__test_status, "truncate reclaim middle",
                     mr_fs_ftruncate(&file, 3), 0);
        ramfs_expect(__test_status, "truncate reclaim empty",
                     mr_fs_ftruncate(&file, 0), 0);
        ramfs_expect(__test_status, "close reclaim file", mr_fs_close(&file),
                     0);
        ramfs_expect(__test_status, "unlink reclaim file", mr_fs_unlink(path),
                     0);
        ramfs_expect(__test_status, "reclaim path absent",
                     fs_test_stat(path, &stat), -MR_ENOENT);
    }

_exit:
    if (file.inode) {
        ramfs_expect(__test_status, "close reclaim cleanup", mr_fs_close(&file),
                     0);
    }
    if (mounted) {
        ramfs_expect(__test_status, "umount reclaim ramfs", mr_fs_umount("/"),
                     0);
    }
    heap_after = mr_heap_information(MR_TRUE);
    ramfs_heap_expect(__test_status, &heap_before, &heap_after);
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) &&                        \
          defined(MR_USE_FS_RAMFS) */
