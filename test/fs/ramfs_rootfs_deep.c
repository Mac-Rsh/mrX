/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-30        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS)                                 \
    && (defined(MR_USE_FS_RAMFS) || defined(MR_USE_FS_ROOTFS))
#include <mrX.h>
#include <mrX/test/fs/fs_test.h>

#if defined(MR_USE_FS_RAMFS_FIFO) && (MR_CFG_FS_RAMFS_FIFO_SIZE == 32)
/* RamFS FIFO fixture capacity in bytes */
#define DEEP_FIFO_CAP (32)
/* Create/destroy cycles for the FIFO reclaim test */
#define DEEP_FIFO_CYCLES (8)

/* FIFO test patterns */
static const char deep_fifo_data24[] = "0123456789abcdefghijklmn";
static const char deep_fifo_data16[] = "QRSTUVWXYZabcdef";
static const char deep_fifo_data32[] = "abcdefghijklmnopqrstuvwxyz012345";

#endif /* defined(MR_USE_FS_RAMFS_FIFO) && (MR_CFG_FS_RAMFS_FIFO_SIZE == 32)   \
        */

static void deep_expect(volatile mr_uint32_t *__test_status, const char *name,
                        mr_int64_t actual, mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

#if defined(MR_USE_FS_RAMFS)
static void deep_mem_expect(volatile mr_uint32_t *__test_status,
                            const char *name, const void *actual,
                            const void *expected, mr_size_t size) {
    int cmp = mr_memcmp(actual, expected, size);

    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(cmp, 0);
}

#endif /* defined(MR_USE_FS_RAMFS) */

#if defined(MR_USE_FS_ROOTFS)
static void deep_stat_expect(volatile mr_uint32_t *__test_status,
                             const char *path, mr_err_t expected,
                             mr_inode_type_t type) {
    mr_stat_t stat;
    mr_err_t ret;

    ret = fs_test_stat(path, &stat);
    deep_expect(__test_status, path, ret, expected);
    if ((ret == 0) && (stat.type != type)) {
        MR_TEST_EXPECT_EQ_INT(stat.type, type);
    }
    return;
}

#endif /* defined(MR_USE_FS_ROOTFS) */

#if defined(MR_USE_FS_RAMFS_FIFO) && (MR_CFG_FS_RAMFS_FIFO_SIZE == 32)
MR_TEST(fs, ramfs_deep_fifo_exact_capacity) {
    static const char fill_a[] = "0123456789abcdefghijklmnopqrstuv";
    static const char fill_b[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345";
    static const char fill_c[] = "zyxwvutsrqponmlkjihgfedcba543210";
    mr_file_t file = MR_FILE_INIT(file);
    char buffer[40];
    mr_ssize_t io_ret;
    mr_err_t ret;

    /* Mount RamFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    deep_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Create a FIFO and fill it to the exact capacity in one write */
    deep_expect(__test_status, "mkfifo", mr_fs_mkfifo("/df-cap"), 0);
    ret = mr_fs_open(&file, "/df-cap", MR_O_RDWR);
    deep_expect(__test_status, "open fifo", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    io_ret = mr_fs_write(&file, fill_a, DEEP_FIFO_CAP);
    deep_expect(__test_status, "exact capacity write", io_ret, DEEP_FIFO_CAP);
    io_ret = mr_fs_read(&file, buffer, sizeof(buffer));
    deep_expect(__test_status, "exact capacity read", io_ret, DEEP_FIFO_CAP);
    if (io_ret == DEEP_FIFO_CAP) {
        deep_mem_expect(__test_status, "exact capacity content", buffer, fill_a,
                        DEEP_FIFO_CAP);
    }
    deep_expect(__test_status, "read empty after drain",
                mr_fs_read(&file, buffer, sizeof(buffer)), 0);

    /* Refill from the wrapped cursor position in one write */
    io_ret = mr_fs_write(&file, fill_b, DEEP_FIFO_CAP);
    deep_expect(__test_status, "wrapped exact write", io_ret, DEEP_FIFO_CAP);
    io_ret = mr_fs_read(&file, buffer, DEEP_FIFO_CAP / 2);
    deep_expect(__test_status, "wrapped first half", io_ret, DEEP_FIFO_CAP / 2);
    if (io_ret == DEEP_FIFO_CAP / 2) {
        deep_mem_expect(__test_status, "wrapped first half content", buffer,
                        fill_b, DEEP_FIFO_CAP / 2);
    }
    io_ret = mr_fs_read(&file, buffer, sizeof(buffer));
    deep_expect(__test_status, "wrapped second half", io_ret,
                DEEP_FIFO_CAP / 2);
    if (io_ret == DEEP_FIFO_CAP / 2) {
        deep_mem_expect(__test_status, "wrapped second half content", buffer,
                        fill_b + DEEP_FIFO_CAP / 2, DEEP_FIFO_CAP / 2);
    }

    /* A full FIFO rejects further data until it is drained again */
    io_ret = mr_fs_write(&file, fill_c, DEEP_FIFO_CAP);
    deep_expect(__test_status, "third exact write", io_ret, DEEP_FIFO_CAP);
    deep_expect(__test_status, "write when full", mr_fs_write(&file, "x", 1),
                0);
    io_ret = mr_fs_read(&file, buffer, sizeof(buffer));
    deep_expect(__test_status, "third drain", io_ret, DEEP_FIFO_CAP);
    if (io_ret == DEEP_FIFO_CAP) {
        deep_mem_expect(__test_status, "third drain content", buffer, fill_c,
                        DEEP_FIFO_CAP);
    }

    deep_expect(__test_status, "close fifo", mr_fs_close(&file), 0);
_exit:
    deep_expect(__test_status, "unlink fifo", mr_fs_unlink("/df-cap"), 0);
    deep_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}

MR_TEST(fs, ramfs_deep_fifo_wrap_edge) {
    mr_file_t file = MR_FILE_INIT(file);
    char expected[DEEP_FIFO_CAP];
    char buffer[40];
    mr_ssize_t io_ret;
    mr_err_t ret;

    /* Mount RamFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    deep_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Park both cursors at a non-zero offset with an empty FIFO */
    deep_expect(__test_status, "mkfifo", mr_fs_mkfifo("/df-wrap"), 0);
    ret = mr_fs_open(&file, "/df-wrap", MR_O_RDWR);
    deep_expect(__test_status, "open fifo", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    deep_expect(
        __test_status, "fill to boundary",
        mr_fs_write(&file, deep_fifo_data24, sizeof(deep_fifo_data24) - 1),
        sizeof(deep_fifo_data24) - 1);
    io_ret = mr_fs_read(&file, buffer, sizeof(buffer));
    deep_expect(__test_status, "drain to boundary", io_ret,
                sizeof(deep_fifo_data24) - 1);
    if (io_ret == (mr_ssize_t)(sizeof(deep_fifo_data24) - 1)) {
        deep_mem_expect(__test_status, "boundary content", buffer,
                        deep_fifo_data24, sizeof(deep_fifo_data24) - 1);
    }

    /* An empty FIFO with the cursor at 24 still accepts a full write */
    deep_expect(
        __test_status, "split write",
        mr_fs_write(&file, deep_fifo_data16, sizeof(deep_fifo_data16) - 1),
        sizeof(deep_fifo_data16) - 1);
    io_ret = mr_fs_read(&file, buffer, sizeof(buffer));
    deep_expect(__test_status, "split read", io_ret,
                sizeof(deep_fifo_data16) - 1);
    if (io_ret == (mr_ssize_t)(sizeof(deep_fifo_data16) - 1)) {
        deep_mem_expect(__test_status, "split read content", buffer,
                        deep_fifo_data16, sizeof(deep_fifo_data16) - 1);
    }

    /* Fill completely, then rotate one byte through the full ring */
    deep_expect(__test_status, "fill complete",
                mr_fs_write(&file, deep_fifo_data32, DEEP_FIFO_CAP),
                DEEP_FIFO_CAP);
    deep_expect(__test_status, "write when full", mr_fs_write(&file, "x", 1),
                0);
    io_ret = mr_fs_read(&file, buffer, 1);
    deep_expect(__test_status, "rotate read", io_ret, 1);
    if ((io_ret == 1) && (buffer[0] != deep_fifo_data32[0])) {
        MR_TEST_EXPECT_EQ_INT(buffer[0], deep_fifo_data32[0]);
    }
    deep_expect(__test_status, "rotate write", mr_fs_write(&file, "Z", 1), 1);

    /* The rotated ring drains in order through a single wrapped read */
    mr_memcpy(expected, deep_fifo_data32 + 1, DEEP_FIFO_CAP - 1);
    expected[DEEP_FIFO_CAP - 1] = 'Z';
    io_ret = mr_fs_read(&file, buffer, sizeof(buffer));
    deep_expect(__test_status, "rotated drain", io_ret, DEEP_FIFO_CAP);
    if (io_ret == DEEP_FIFO_CAP) {
        deep_mem_expect(__test_status, "rotated drain content", buffer,
                        expected, DEEP_FIFO_CAP);
    }
    deep_expect(__test_status, "rotated empty",
                mr_fs_read(&file, buffer, sizeof(buffer)), 0);

    deep_expect(__test_status, "close fifo", mr_fs_close(&file), 0);
_exit:
    deep_expect(__test_status, "unlink fifo", mr_fs_unlink("/df-wrap"), 0);
    deep_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}

MR_TEST(fs, ramfs_deep_fifo_two_readers) {
    mr_file_t writer = MR_FILE_INIT(writer);
    mr_file_t reader1 = MR_FILE_INIT(reader1);
    mr_file_t reader2 = MR_FILE_INIT(reader2);
    char buffer[8];
    mr_ssize_t io_ret;
    mr_err_t ret;

    /* Mount RamFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    deep_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* One writer and two readers share a single queue */
    deep_expect(__test_status, "mkfifo", mr_fs_mkfifo("/df-rd"), 0);
    ret = mr_fs_open(&writer, "/df-rd", MR_O_WRONLY);
    deep_expect(__test_status, "open writer", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    ret = mr_fs_open(&reader1, "/df-rd", MR_O_RDONLY);
    deep_expect(__test_status, "open reader1", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    ret = mr_fs_open(&reader2, "/df-rd", MR_O_RDONLY);
    deep_expect(__test_status, "open reader2", ret, 0);
    if (ret != 0) {
        goto _exit;
    }

    /* Interleaved reads consume disjoint byte ranges */
    deep_expect(__test_status, "write five", mr_fs_write(&writer, "ABCDE", 5),
                5);
    io_ret = mr_fs_read(&reader1, buffer, 2);
    deep_expect(__test_status, "reader1 partial", io_ret, 2);
    if ((io_ret == 2) && (mr_memcmp(buffer, "AB", 2) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "AB", 2), 0);
    }
    io_ret = mr_fs_read(&reader2, buffer, sizeof(buffer));
    deep_expect(__test_status, "reader2 rest", io_ret, 3);
    if ((io_ret == 3) && (mr_memcmp(buffer, "CDE", 3) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "CDE", 3), 0);
    }
    deep_expect(__test_status, "reader1 empty",
                mr_fs_read(&reader1, buffer, sizeof(buffer)), 0);

    /* Access mode violations are reported per handle */
    deep_expect(__test_status, "write on reader", mr_fs_write(&reader1, "x", 1),
                -MR_EBADF);
    deep_expect(__test_status, "read on writer",
                mr_fs_read(&writer, buffer, sizeof(buffer)), -MR_EBADF);

    /* Closing one reader keeps the remaining pair working */
    deep_expect(__test_status, "close reader1", mr_fs_close(&reader1), 0);
    deep_expect(__test_status, "write two", mr_fs_write(&writer, "FG", 2), 2);
    io_ret = mr_fs_read(&reader2, buffer, sizeof(buffer));
    deep_expect(__test_status, "reader2 after close", io_ret, 2);
    if ((io_ret == 2) && (mr_memcmp(buffer, "FG", 2) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "FG", 2), 0);
    }
    deep_expect(__test_status, "write three", mr_fs_write(&writer, "HIJ", 3),
                3);
    io_ret = mr_fs_read(&reader2, buffer, sizeof(buffer));
    deep_expect(__test_status, "reader2 final", io_ret, 3);
    if ((io_ret == 3) && (mr_memcmp(buffer, "HIJ", 3) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "HIJ", 3), 0);
    }

    deep_expect(__test_status, "close reader2", mr_fs_close(&reader2), 0);
    deep_expect(__test_status, "close writer", mr_fs_close(&writer), 0);
_exit:
    if (writer.inode) {
        deep_expect(__test_status, "close writer cleanup", mr_fs_close(&writer),
                    0);
    }
    if (reader1.inode) {
        deep_expect(__test_status, "close reader1 cleanup",
                    mr_fs_close(&reader1), 0);
    }
    if (reader2.inode) {
        deep_expect(__test_status, "close reader2 cleanup",
                    mr_fs_close(&reader2), 0);
    }
    deep_expect(__test_status, "unlink fifo", mr_fs_unlink("/df-rd"), 0);
    deep_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}

MR_TEST(fs, ramfs_deep_fifo_two_writers) {
    mr_file_t writer1 = MR_FILE_INIT(writer1);
    mr_file_t writer2 = MR_FILE_INIT(writer2);
    mr_file_t reader = MR_FILE_INIT(reader);
    char buffer[8];
    mr_ssize_t io_ret;
    mr_err_t ret;

    /* Mount RamFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    deep_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Two writers alternate on one queue with a single consumer */
    deep_expect(__test_status, "mkfifo", mr_fs_mkfifo("/df-wr"), 0);
    ret = mr_fs_open(&writer1, "/df-wr", MR_O_WRONLY);
    deep_expect(__test_status, "open writer1", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    ret = mr_fs_open(&writer2, "/df-wr", MR_O_WRONLY);
    deep_expect(__test_status, "open writer2", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    ret = mr_fs_open(&reader, "/df-wr", MR_O_RDONLY);
    deep_expect(__test_status, "open reader", ret, 0);
    if (ret != 0) {
        goto _exit;
    }

    /* Interleaved writes preserve the call order in the byte stream */
    deep_expect(__test_status, "writer1 three", mr_fs_write(&writer1, "123", 3),
                3);
    deep_expect(__test_status, "writer2 two", mr_fs_write(&writer2, "45", 2),
                2);
    deep_expect(__test_status, "writer1 one", mr_fs_write(&writer1, "6", 1), 1);
    io_ret = mr_fs_read(&reader, buffer, sizeof(buffer));
    deep_expect(__test_status, "drain interleave", io_ret, 6);
    if ((io_ret == 6) && (mr_memcmp(buffer, "123456", 6) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "123456", 6), 0);
    }

    /* Queued data outlives every handle being closed */
    deep_expect(__test_status, "writer2 tail", mr_fs_write(&writer2, "789", 3),
                3);
    deep_expect(__test_status, "close writer1", mr_fs_close(&writer1), 0);
    deep_expect(__test_status, "close writer2", mr_fs_close(&writer2), 0);
    deep_expect(__test_status, "close reader", mr_fs_close(&reader), 0);
    ret = mr_fs_open(&reader, "/df-wr", MR_O_RDONLY);
    deep_expect(__test_status, "reopen reader", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    io_ret = mr_fs_read(&reader, buffer, sizeof(buffer));
    deep_expect(__test_status, "read after reopen", io_ret, 3);
    if ((io_ret == 3) && (mr_memcmp(buffer, "789", 3) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "789", 3), 0);
    }
    deep_expect(__test_status, "close reopened reader", mr_fs_close(&reader),
                0);

_exit:
    if (writer1.inode) {
        deep_expect(__test_status, "close writer1 cleanup",
                    mr_fs_close(&writer1), 0);
    }
    if (writer2.inode) {
        deep_expect(__test_status, "close writer2 cleanup",
                    mr_fs_close(&writer2), 0);
    }
    if (reader.inode) {
        deep_expect(__test_status, "close reader cleanup", mr_fs_close(&reader),
                    0);
    }
    deep_expect(__test_status, "unlink fifo", mr_fs_unlink("/df-wr"), 0);
    deep_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}

MR_TEST(fs, ramfs_deep_fifo_flags_stat) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_stat_t stat;
    char buffer[8];
    mr_ssize_t io_ret;
    mr_err_t ret;

    /* Mount RamFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    deep_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Queue some data and reopen with O_TRUNC */
    deep_expect(__test_status, "mkfifo", mr_fs_mkfifo("/df-fl"), 0);
    ret = mr_fs_open(&file, "/df-fl", MR_O_WRONLY);
    deep_expect(__test_status, "open writer", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    deep_expect(__test_status, "seed fifo", mr_fs_write(&file, "XY", 2), 2);
    deep_expect(__test_status, "close writer", mr_fs_close(&file), 0);

    /* O_TRUNC only applies to regular files and keeps FIFO data */
    ret = mr_fs_open(&file, "/df-fl", MR_O_RDWR | MR_O_TRUNC);
    deep_expect(__test_status, "open trunc fifo", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    io_ret = mr_fs_read(&file, buffer, sizeof(buffer));
    deep_expect(__test_status, "trunc keeps data", io_ret, 2);
    if ((io_ret == 2) && (mr_memcmp(buffer, "XY", 2) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "XY", 2), 0);
    }

    /* Every seek base is rejected on a FIFO */
    deep_expect(__test_status, "seek set fifo",
                mr_fs_lseek(&file, 0, MR_SEEK_SET), -MR_ESPIPE);
    deep_expect(__test_status, "seek cur fifo",
                mr_fs_lseek(&file, 0, MR_SEEK_CUR), -MR_ESPIPE);
    deep_expect(__test_status, "seek end fifo",
                mr_fs_lseek(&file, 0, MR_SEEK_END), -MR_ESPIPE);

    /* The status reports a zero-length FIFO inode */
    ret = mr_fs_fstat(&file, &stat);
    deep_expect(__test_status, "fstat fifo", ret, 0);
    if (ret == 0) {
        deep_expect(__test_status, "fifo stat type", stat.type, MR_INODE_FIFO);
        deep_expect(__test_status, "fifo stat size", stat.size, 0);
    }

    /* Writes through the trunc-mode handle still work */
    deep_expect(__test_status, "write trunc fifo", mr_fs_write(&file, "Z", 1),
                1);
    deep_expect(__test_status, "close trunc fifo", mr_fs_close(&file), 0);

    /* O_APPEND writes land on the queue like any other write */
    ret = mr_fs_open(&file, "/df-fl", MR_O_RDONLY);
    deep_expect(__test_status, "open reader", ret, 0);
    if (ret == 0) {
        io_ret = mr_fs_read(&file, buffer, sizeof(buffer));
        deep_expect(__test_status, "read queued", io_ret, 1);
        if ((io_ret == 1) && (buffer[0] != 'Z')) {
            MR_TEST_EXPECT_EQ_INT(buffer[0], 'Z');
        }
        deep_expect(__test_status, "close reader", mr_fs_close(&file), 0);
    }
    ret = mr_fs_open(&file, "/df-fl", MR_O_WRONLY | MR_O_APPEND);
    deep_expect(__test_status, "open append fifo", ret, 0);
    if (ret == 0) {
        deep_expect(__test_status, "append fifo write",
                    mr_fs_write(&file, "W", 1), 1);
        deep_expect(__test_status, "close append fifo", mr_fs_close(&file), 0);
    }
    ret = mr_fs_open(&file, "/df-fl", MR_O_RDONLY);
    deep_expect(__test_status, "open final reader", ret, 0);
    if (ret == 0) {
        io_ret = mr_fs_read(&file, buffer, sizeof(buffer));
        deep_expect(__test_status, "read appended", io_ret, 1);
        if ((io_ret == 1) && (buffer[0] != 'W')) {
            MR_TEST_EXPECT_EQ_INT(buffer[0], 'W');
        }
        deep_expect(__test_status, "close final reader", mr_fs_close(&file), 0);
    }

_exit:
    if (file.inode) {
        deep_expect(__test_status, "close fifo cleanup", mr_fs_close(&file), 0);
    }
    deep_expect(__test_status, "unlink fifo", mr_fs_unlink("/df-fl"), 0);
    deep_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}

MR_TEST(fs, ramfs_deep_fifo_reclaim_cycles) {
    mr_heap_info_t heap_before;
    mr_heap_info_t heap_after;
    mr_file_t writer = MR_FILE_INIT(writer);
    mr_file_t reader = MR_FILE_INIT(reader);
    char buffer[DEEP_FIFO_CAP];
    int i;
    mr_err_t ret;
    mr_ssize_t io_ret;

    /* Snapshot the heap before the test owns any allocation */
    heap_before = mr_heap_information(MR_TRUE);
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    deep_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Cycle a FIFO through create, dual open, full transfer and removal */
    for (i = 0; i < DEEP_FIFO_CYCLES; i++) {
        deep_expect(__test_status, "mkfifo cycle", mr_fs_mkfifo("/df-cycle"),
                    0);
        ret = mr_fs_open(&writer, "/df-cycle", MR_O_WRONLY);
        deep_expect(__test_status, "open cycle writer", ret, 0);
        if (ret != 0) {
            goto _exit;
        }
        ret = mr_fs_open(&reader, "/df-cycle", MR_O_RDONLY);
        deep_expect(__test_status, "open cycle reader", ret, 0);
        if (ret != 0) {
            goto _exit;
        }
        deep_expect(__test_status, "write cycle",
                    mr_fs_write(&writer, deep_fifo_data32, DEEP_FIFO_CAP),
                    DEEP_FIFO_CAP);
        io_ret = mr_fs_read(&reader, buffer, sizeof(buffer));
        deep_expect(__test_status, "read cycle", io_ret, DEEP_FIFO_CAP);
        if (io_ret == DEEP_FIFO_CAP) {
            deep_mem_expect(__test_status, "read cycle content", buffer,
                            deep_fifo_data32, DEEP_FIFO_CAP);
        }
        deep_expect(__test_status, "close cycle writer", mr_fs_close(&writer),
                    0);
        deep_expect(__test_status, "close cycle reader", mr_fs_close(&reader),
                    0);
        deep_expect(__test_status, "unlink cycle", mr_fs_unlink("/df-cycle"),
                    0);
    }

_exit:
    if (writer.inode) {
        deep_expect(__test_status, "close writer cleanup", mr_fs_close(&writer),
                    0);
    }
    if (reader.inode) {
        deep_expect(__test_status, "close reader cleanup", mr_fs_close(&reader),
                    0);
    }
    deep_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    heap_after = mr_heap_information(MR_TRUE);
    deep_expect(__test_status, "fifo heap restored",
                heap_after.used_bytes == heap_before.used_bytes, MR_TRUE);
    deep_expect(__test_status, "fifo heap total kept",
                heap_after.total_bytes == heap_before.total_bytes, MR_TRUE);
    return;
}

#endif /* defined(MR_USE_FS_RAMFS_FIFO) && (MR_CFG_FS_RAMFS_FIFO_SIZE == 32)   \
        */

#if defined(MR_USE_FS_RAMFS)
/* Sparse write offset exercising a large zero-filled hole */
#define DEEP_HOLE_OFFSET (100000)

MR_TEST(fs, ramfs_deep_regular_large_hole) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_uint8_t zeros[64];
    mr_uint8_t chunk[64];
    mr_uint8_t last[64];
    mr_stat_t stat;
    mr_size_t last_len = 0;
    char buffer[16];
    mr_int64_t total = 0;
    mr_ssize_t io_ret;
    mr_err_t ret;

    /* Mount RamFS */
    mr_memset(zeros, 0, sizeof(zeros));
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    deep_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Write four bytes at a large offset on a fresh file */
    ret = mr_fs_open(&file, "/df-hole", MR_O_RDWR | MR_O_CREAT);
    deep_expect(__test_status, "create hole file", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    deep_expect(__test_status, "seek hole",
                mr_fs_lseek(&file, DEEP_HOLE_OFFSET, MR_SEEK_SET),
                DEEP_HOLE_OFFSET);
    deep_expect(__test_status, "write tail", mr_fs_write(&file, "TAIL", 4), 4);
    ret = mr_fs_fstat(&file, &stat);
    deep_expect(__test_status, "stat hole file", ret, 0);
    if (ret == 0) {
        deep_expect(__test_status, "hole size", stat.size,
                    DEEP_HOLE_OFFSET + 4);
    }

    /* Streaming the whole file back yields zeros and the tail only */
    deep_expect(__test_status, "seek head", mr_fs_lseek(&file, 0, MR_SEEK_SET),
                0);
    while ((io_ret = mr_fs_read(&file, chunk, sizeof(chunk))) > 0) {
        if (total == 0) {
            deep_mem_expect(__test_status, "hole prefix zeros", chunk, zeros,
                            sizeof(chunk));
        }
        mr_memcpy(last, chunk, (mr_size_t)io_ret);
        last_len = (mr_size_t)io_ret;
        total += io_ret;
    }
    deep_expect(__test_status, "hole stream total", total,
                DEEP_HOLE_OFFSET + 4);
    if (total == DEEP_HOLE_OFFSET + 4) {
        deep_expect(__test_status, "hole tail chunk", (mr_int64_t)last_len,
                    (DEEP_HOLE_OFFSET + 4) % 64);
        if (last_len == 36U) {
            deep_mem_expect(__test_status, "hole tail content", last + 32,
                            "TAIL", 4);
        }
    }

    /* Reads at and beyond end of file return zero without moving data */
    deep_expect(__test_status, "read at eof",
                mr_fs_read(&file, buffer, sizeof(buffer)), 0);
    deep_expect(__test_status, "seek past eof",
                mr_fs_lseek(&file, DEEP_HOLE_OFFSET + 14, MR_SEEK_SET),
                DEEP_HOLE_OFFSET + 14);
    deep_expect(__test_status, "read past eof",
                mr_fs_read(&file, buffer, sizeof(buffer)), 0);

    /* Writing past the hole extends the file across the gap */
    deep_expect(__test_status, "write past eof", mr_fs_write(&file, "X", 1), 1);
    ret = mr_fs_fstat(&file, &stat);
    deep_expect(__test_status, "stat extended", ret, 0);
    if (ret == 0) {
        deep_expect(__test_status, "extended size", stat.size,
                    DEEP_HOLE_OFFSET + 15);
    }
    deep_expect(__test_status, "seek gap",
                mr_fs_lseek(&file, DEEP_HOLE_OFFSET + 4, MR_SEEK_SET),
                DEEP_HOLE_OFFSET + 4);
    io_ret = mr_fs_read(&file, buffer, 10);
    deep_expect(__test_status, "read gap", io_ret, 10);
    if (io_ret == 10) {
        deep_mem_expect(__test_status, "gap zeros", buffer, zeros, 10);
    }
    io_ret = mr_fs_read(&file, buffer, 1);
    deep_expect(__test_status, "read gap tail", io_ret, 1);
    if ((io_ret == 1) && (buffer[0] != 'X')) {
        MR_TEST_EXPECT_EQ_INT(buffer[0], 'X');
    }

    deep_expect(__test_status, "close hole file", mr_fs_close(&file), 0);
_exit:
    if (file.inode) {
        deep_expect(__test_status, "close hole cleanup", mr_fs_close(&file), 0);
    }
    deep_expect(__test_status, "unlink hole file", mr_fs_unlink("/df-hole"), 0);
    deep_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}

MR_TEST(fs, ramfs_deep_regular_write_bounds) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_stat_t stat;
    char buffer[16];
    mr_ssize_t io_ret;
    mr_err_t ret;

    /* Mount RamFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    deep_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Seed a small file */
    ret = mr_fs_open(&file, "/df-bnd", MR_O_RDWR | MR_O_CREAT);
    deep_expect(__test_status, "create bounds file", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    deep_expect(__test_status, "write seed",
                mr_fs_write(&file, "0123456789", 10), 10);

    /* A write crossing the signed position limit is rejected */
    deep_expect(__test_status, "seek max",
                mr_fs_lseek(&file, MR_INT64_MAX, MR_SEEK_SET), MR_INT64_MAX);
    deep_expect(__test_status, "write at max", mr_fs_write(&file, "A", 1),
                -MR_ERANGE);

    /* A write ending exactly at the limit fails the allocation instead */
    deep_expect(__test_status, "seek below max",
                mr_fs_lseek(&file, MR_INT64_MAX - 1, MR_SEEK_SET),
                MR_INT64_MAX - 1);
    deep_expect(__test_status, "write to max", mr_fs_write(&file, "A", 1),
                -MR_ENOMEM);

    /* Failed writes leave size, content and position untouched */
    ret = mr_fs_fstat(&file, &stat);
    deep_expect(__test_status, "stat after failed writes", ret, 0);
    if (ret == 0) {
        deep_expect(__test_status, "size kept", stat.size, 10);
    }
    deep_expect(__test_status, "position kept",
                mr_fs_lseek(&file, 0, MR_SEEK_CUR), MR_INT64_MAX - 1);

    /* Truncating to the allocation limit fails without touching the file */
    deep_expect(__test_status, "truncate max",
                mr_fs_ftruncate(&file, MR_INT64_MAX), -MR_ENOMEM);
    ret = mr_fs_fstat(&file, &stat);
    deep_expect(__test_status, "stat after failed truncate", ret, 0);
    if (ret == 0) {
        deep_expect(__test_status, "truncate size kept", stat.size, 10);
    }
    deep_expect(__test_status, "seek head", mr_fs_lseek(&file, 0, MR_SEEK_SET),
                0);
    io_ret = mr_fs_read(&file, buffer, sizeof(buffer));
    deep_expect(__test_status, "read kept", io_ret, 10);
    if (io_ret == 10) {
        deep_mem_expect(__test_status, "content kept", buffer, "0123456789",
                        10);
    }

    /* After freeing the buffer a fresh hole write rebuilds the file */
    deep_expect(__test_status, "truncate zero", mr_fs_ftruncate(&file, 0), 0);
    deep_expect(__test_status, "seek rebuild",
                mr_fs_lseek(&file, 50000, MR_SEEK_SET), 50000);
    deep_expect(__test_status, "write rebuild", mr_fs_write(&file, "R", 1), 1);
    ret = mr_fs_fstat(&file, &stat);
    deep_expect(__test_status, "stat rebuilt", ret, 0);
    if (ret == 0) {
        deep_expect(__test_status, "rebuilt size", stat.size, 50001);
    }
    deep_expect(__test_status, "seek rebuilt head",
                mr_fs_lseek(&file, 49999, MR_SEEK_SET), 49999);
    io_ret = mr_fs_read(&file, buffer, 2);
    deep_expect(__test_status, "read rebuilt", io_ret, 2);
    if (io_ret == 2) {
        deep_expect(__test_status, "rebuilt hole byte", buffer[0], 0);
        deep_expect(__test_status, "rebuilt data byte", buffer[1], 'R');
    }

    deep_expect(__test_status, "close bounds file", mr_fs_close(&file), 0);
_exit:
    if (file.inode) {
        deep_expect(__test_status, "close bounds cleanup", mr_fs_close(&file),
                    0);
    }
    deep_expect(__test_status, "unlink bounds file", mr_fs_unlink("/df-bnd"),
                0);
    deep_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}

MR_TEST(fs, ramfs_deep_regular_two_handles) {
    mr_file_t first = MR_FILE_INIT(first);
    mr_file_t second = MR_FILE_INIT(second);
    mr_uint8_t zeros[8];
    mr_stat_t stat;
    char buffer[16];
    mr_ssize_t io_ret;
    mr_err_t ret;

    /* Mount RamFS */
    mr_memset(zeros, 0, sizeof(zeros));
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    deep_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* One writer handle and one later reader handle */
    ret = mr_fs_open(&first, "/df-two", MR_O_RDWR | MR_O_CREAT);
    deep_expect(__test_status, "create two file", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    deep_expect(__test_status, "first write", mr_fs_write(&first, "abcdef", 6),
                6);
    ret = mr_fs_open(&second, "/df-two", MR_O_RDWR);
    deep_expect(__test_status, "open second handle", ret, 0);
    if (ret != 0) {
        goto _exit;
    }

    /* Each handle tracks its own position */
    io_ret = mr_fs_read(&second, buffer, 3);
    deep_expect(__test_status, "second read", io_ret, 3);
    if ((io_ret == 3) && (mr_memcmp(buffer, "abc", 3) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "abc", 3), 0);
    }
    deep_expect(__test_status, "first position kept",
                mr_fs_lseek(&first, 0, MR_SEEK_CUR), 6);

    /* New bytes written by the first are visible to the second */
    deep_expect(__test_status, "first extend", mr_fs_write(&first, "XY", 2), 2);
    io_ret = mr_fs_read(&second, buffer, sizeof(buffer));
    deep_expect(__test_status, "second sees growth", io_ret, 5);
    if ((io_ret == 5) && (mr_memcmp(buffer, "defXY", 5) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "defXY", 5), 0);
    }

    /* An external shrink strands the first handle at end of file */
    deep_expect(__test_status, "second shrink", mr_fs_ftruncate(&second, 2), 0);
    deep_expect(__test_status, "read stranded",
                mr_fs_read(&first, buffer, sizeof(buffer)), 0);
    deep_expect(__test_status, "seek shrunk head",
                mr_fs_lseek(&first, 0, MR_SEEK_SET), 0);
    io_ret = mr_fs_read(&first, buffer, sizeof(buffer));
    deep_expect(__test_status, "read shrunk", io_ret, 2);
    if ((io_ret == 2) && (mr_memcmp(buffer, "ab", 2) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "ab", 2), 0);
    }

    /* A hole write after the shrink zero-fills only the new gap */
    deep_expect(__test_status, "seek hole two",
                mr_fs_lseek(&first, 10, MR_SEEK_SET), 10);
    deep_expect(__test_status, "write hole two", mr_fs_write(&first, "Q", 1),
                1);
    ret = mr_fs_fstat(&second, &stat);
    deep_expect(__test_status, "stat hole two", ret, 0);
    if (ret == 0) {
        deep_expect(__test_status, "hole two size", stat.size, 11);
    }
    deep_expect(__test_status, "seek second head",
                mr_fs_lseek(&second, 0, MR_SEEK_SET), 0);
    io_ret = mr_fs_read(&second, buffer, sizeof(buffer));
    deep_expect(__test_status, "read hole two", io_ret, 11);
    if (io_ret == 11) {
        deep_expect(__test_status, "hole two head", buffer[0], 'a');
        deep_expect(__test_status, "hole two second", buffer[1], 'b');
        deep_mem_expect(__test_status, "hole two zeros", buffer + 2, zeros, 8);
        deep_expect(__test_status, "hole two tail", buffer[10], 'Q');
    }

    deep_expect(__test_status, "close first handle", mr_fs_close(&first), 0);
    deep_expect(__test_status, "close second handle", mr_fs_close(&second), 0);
_exit:
    if (first.inode) {
        deep_expect(__test_status, "close first cleanup", mr_fs_close(&first),
                    0);
    }
    if (second.inode) {
        deep_expect(__test_status, "close second cleanup", mr_fs_close(&second),
                    0);
    }
    deep_expect(__test_status, "unlink two file", mr_fs_unlink("/df-two"), 0);
    deep_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}

MR_TEST(fs, ramfs_deep_regular_append_edges) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_file_t reader = MR_FILE_INIT(reader);
    char buffer[8];
    mr_ssize_t io_ret;
    mr_err_t ret;

    /* Mount RamFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    deep_expect(__test_status, "mount ramfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* An append handle may also read from its own position */
    ret = mr_fs_open(&file, "/df-app", MR_O_RDWR | MR_O_CREAT | MR_O_APPEND);
    deep_expect(__test_status, "open append rdwr", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    deep_expect(__test_status, "append write one", mr_fs_write(&file, "12", 2),
                2);
    deep_expect(__test_status, "append position after write",
                mr_fs_lseek(&file, 0, MR_SEEK_CUR), 2);
    deep_expect(__test_status, "seek before append read",
                mr_fs_lseek(&file, 0, MR_SEEK_SET), 0);
    io_ret = mr_fs_read(&file, buffer, sizeof(buffer));
    deep_expect(__test_status, "append handle read", io_ret, 2);
    if ((io_ret == 2) && (mr_memcmp(buffer, "12", 2) != 0)) {
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(buffer, "12", 2), 0);
    }

    /* A zero-length append write keeps the current position untouched */
    deep_expect(__test_status, "seek append head",
                mr_fs_lseek(&file, 0, MR_SEEK_SET), 0);
    deep_expect(__test_status, "append write zero", mr_fs_write(&file, "x", 0),
                0);
    deep_expect(__test_status, "append position restored",
                mr_fs_lseek(&file, 0, MR_SEEK_CUR), 0);

    /* A non-empty append write still selects end of file */
    deep_expect(__test_status, "append write two", mr_fs_write(&file, "3", 1),
                1);
    deep_expect(__test_status, "append position end",
                mr_fs_lseek(&file, 0, MR_SEEK_CUR), 3);

    /* O_TRUNC combined with O_APPEND empties the file before appending */
    deep_expect(__test_status, "close append handle", mr_fs_close(&file), 0);
    ret = mr_fs_open(&file, "/df-app", MR_O_WRONLY | MR_O_APPEND | MR_O_TRUNC);
    deep_expect(__test_status, "open append trunc", ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    deep_expect(__test_status, "append after trunc", mr_fs_write(&file, "T", 1),
                1);
    deep_expect(__test_status, "append trunc position",
                mr_fs_lseek(&file, 0, MR_SEEK_CUR), 1);
    deep_expect(__test_status, "close append trunc", mr_fs_close(&file), 0);

    /* The truncated file holds only the appended byte */
    ret = mr_fs_open(&reader, "/df-app", MR_O_RDONLY);
    deep_expect(__test_status, "open verify reader", ret, 0);
    if (ret == 0) {
        io_ret = mr_fs_read(&reader, buffer, sizeof(buffer));
        deep_expect(__test_status, "verify content", io_ret, 1);
        if ((io_ret == 1) && (buffer[0] != 'T')) {
            MR_TEST_EXPECT_EQ_INT(buffer[0], 'T');
        }
        deep_expect(__test_status, "close verify reader", mr_fs_close(&reader),
                    0);
    }

_exit:
    if (file.inode) {
        deep_expect(__test_status, "close file cleanup", mr_fs_close(&file), 0);
    }
    if (reader.inode) {
        deep_expect(__test_status, "close reader cleanup", mr_fs_close(&reader),
                    0);
    }
    deep_expect(__test_status, "unlink app file", mr_fs_unlink("/df-app"), 0);
    deep_expect(__test_status, "umount ramfs", mr_fs_umount("/"), 0);
    return;
}

#endif /* defined(MR_USE_FS_RAMFS) */

#if defined(MR_USE_FS_ROOTFS)
MR_TEST(fs, rootfs_deep_mount_twice) {
    mr_err_t ret;

    /* Mount RootFS at the namespace root */
    ret = fs_test_mount_root("rootfs", 0, MR_NULL);
    deep_expect(__test_status, "mount rootfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Mounting another super block over the root is refused */
    deep_expect(__test_status, "repeat root mount",
                mr_fs_mount_root(MR_NULL, "rootfs", 0, MR_NULL), -MR_EBUSY);

    /* The same rule applies to a nested RootFS mount point */
    deep_expect(__test_status, "mkdir mount dir", mr_fs_mkdir("/rf-m"), 0);
    deep_expect(__test_status, "mount nested rootfs",
                mr_fs_mount(MR_NULL, "/rf-m", "rootfs", 0, MR_NULL), 0);
    deep_expect(__test_status, "repeat nested mount",
                mr_fs_mount(MR_NULL, "/rf-m", "rootfs", 0, MR_NULL), -MR_EBUSY);
    deep_expect(__test_status, "mkdir nested child", mr_fs_mkdir("/rf-m/child"),
                0);
    deep_stat_expect(__test_status, "/rf-m/child", 0, MR_INODE_DIR);
    deep_expect(__test_status, "umount nested rootfs", mr_fs_umount("/rf-m"),
                0);
    deep_expect(__test_status, "rmdir mount dir", mr_fs_rmdir("/rf-m"), 0);

    /* Best-effort teardown so a failure never blocks the root unmount */
    mr_fs_umount("/rf-m");
    mr_fs_rmdir("/rf-m");
    deep_expect(__test_status, "umount rootfs", mr_fs_umount("/"), 0);
    return;
}

MR_TEST(fs, rootfs_deep_fifo_mknod) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_stat_t stat;
    char buffer[4];
    mr_err_t ret;

    /* Mount RootFS */
    ret = fs_test_mount_root("rootfs", 0, MR_NULL);
    deep_expect(__test_status, "mount rootfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* mknod accepts a FIFO type even though RootFS has no file hooks */
    deep_expect(__test_status, "mkdir device dir", mr_fs_mkdir("/rf-dev"), 0);
    deep_expect(__test_status, "mknod fifo", mr_fs_mkfifo("/rf-dev/pipe"), 0);
    ret = fs_test_stat("/rf-dev/pipe", &stat);
    deep_expect(__test_status, "stat fifo node", ret, 0);
    if (ret == 0) {
        deep_expect(__test_status, "fifo node type", stat.type, MR_INODE_FIFO);
        deep_expect(__test_status, "fifo node size", stat.size, 0);
    }

    /* The node opens but every data operation is unsupported */
    ret = mr_fs_open(&file, "/rf-dev/pipe", MR_O_RDWR);
    deep_expect(__test_status, "open fifo node", ret, 0);
    if (ret == 0) {
        deep_expect(__test_status, "read fifo node",
                    mr_fs_read(&file, buffer, sizeof(buffer)), -MR_ENOSYS);
        deep_expect(__test_status, "write fifo node",
                    mr_fs_write(&file, buffer, sizeof(buffer)), -MR_ENOSYS);
        deep_expect(__test_status, "seek fifo node",
                    mr_fs_lseek(&file, 4, MR_SEEK_SET), 4);
        deep_expect(__test_status, "truncate fifo node",
                    mr_fs_ftruncate(&file, 0), -MR_EINVAL);
        deep_expect(__test_status, "sync fifo node", mr_fs_fsync(&file),
                    -MR_ENOSYS);
        deep_expect(__test_status, "ioctl fifo node",
                    mr_fs_ioctl(&file, 1, MR_NULL), -MR_ENOSYS);
        deep_expect(__test_status, "close fifo node", mr_fs_close(&file), 0);
    }

    /* The node removes like any other namespace entry */
    deep_expect(__test_status, "unlink fifo node", mr_fs_unlink("/rf-dev/pipe"),
                0);
    deep_expect(__test_status, "rmdir device dir", mr_fs_rmdir("/rf-dev"), 0);
    deep_expect(__test_status, "umount rootfs", mr_fs_umount("/"), 0);
    return;
}

MR_TEST(fs, rootfs_deep_node_ops) {
    mr_heap_info_t heap_before;
    mr_heap_info_t heap_after;
    mr_file_t file = MR_FILE_INIT(file);
    mr_file_t dir = MR_FILE_INIT(dir);
    mr_dirent_t dirent;
    mr_stat_t stat;
    int metadata;
    mr_bool_t found = MR_FALSE;
    mr_bool_t extra = MR_FALSE;
    mr_err_t ret;

    /* Snapshot the heap and mount RootFS */
    heap_before = mr_heap_information(MR_TRUE);
    ret = fs_test_mount_root("rootfs", 0, MR_NULL);
    deep_expect(__test_status, "mount rootfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* A fresh RootFS root enumerates as empty */
    ret = mr_fs_open(&dir, "/", MR_O_RDONLY | MR_O_DIRECTORY);
    deep_expect(__test_status, "open rootfs root", ret, 0);
    if (ret == 0) {
        deep_expect(__test_status, "root empty", mr_fs_readdir(&dir, &dirent),
                    0);
        deep_expect(__test_status, "close rootfs root", mr_fs_close(&dir), 0);
    }

    /* Exactly one entry appears after a creation */
    deep_expect(__test_status, "mkdir node dir", mr_fs_mkdir("/rf-node"), 0);
    ret = mr_fs_open(&dir, "/", MR_O_RDONLY | MR_O_DIRECTORY);
    deep_expect(__test_status, "reopen rootfs root", ret, 0);
    if (ret == 0) {
        while ((ret = mr_fs_readdir(&dir, &dirent)) > 0) {
            if ((mr_strcmp(dirent.name, "rf-node") == 0)
                && (dirent.type == MR_INODE_DIR)) {
                found = MR_TRUE;
            } else {
                extra = MR_TRUE;
            }
        }
        deep_expect(__test_status, "finish readdir", ret, 0);
        deep_expect(__test_status, "found node dir", found, MR_TRUE);
        deep_expect(__test_status, "no unexpected entries", extra, MR_FALSE);
        deep_expect(__test_status, "close rootfs root", mr_fs_close(&dir), 0);
    }

    /* A mknod node keeps its private pointer and reports generic status */
    deep_expect(__test_status, "mknod reg node",
                mr_fs_mknod("/rf-node/data", MR_INODE_REG, &metadata), 0);
    ret = mr_fs_open(&file, "/rf-node/data", MR_O_RDWR);
    deep_expect(__test_status, "open reg node", ret, 0);
    if (ret == 0) {
        deep_expect(__test_status, "reg node private",
                    file.inode->priv == &metadata, MR_TRUE);
        deep_expect(__test_status, "seek reg node end",
                    mr_fs_lseek(&file, 0, MR_SEEK_END), 0);
        deep_expect(__test_status, "seek reg node set",
                    mr_fs_lseek(&file, 7, MR_SEEK_SET), 7);
        deep_expect(__test_status, "seek reg node cur",
                    mr_fs_lseek(&file, 0, MR_SEEK_CUR), 7);
        ret = mr_fs_fstat(&file, &stat);
        deep_expect(__test_status, "stat reg node", ret, 0);
        if (ret == 0) {
            deep_expect(__test_status, "reg node size", stat.size, 0);
            deep_expect(__test_status, "reg node type", stat.type,
                        MR_INODE_REG);
        }
        deep_expect(__test_status, "close reg node", mr_fs_close(&file), 0);
    }

    /* Clean up */
    deep_expect(__test_status, "unlink reg node", mr_fs_unlink("/rf-node/data"),
                0);
    deep_expect(__test_status, "rmdir node dir", mr_fs_rmdir("/rf-node"), 0);
    deep_expect(__test_status, "umount rootfs", mr_fs_umount("/"), 0);
    heap_after = mr_heap_information(MR_TRUE);
    deep_expect(__test_status, "rootfs heap restored",
                heap_after.used_bytes == heap_before.used_bytes, MR_TRUE);
    return;
}

MR_TEST(fs, rootfs_deep_umount_lifecycle) {
    mr_err_t ret;

    /* Mount RootFS and confirm the root is a directory */
    ret = fs_test_mount_root("rootfs", 0, MR_NULL);
    deep_expect(__test_status, "mount rootfs", ret, 0);
    if (ret != 0) {
        return;
    }
    deep_stat_expect(__test_status, "/", 0, MR_INODE_DIR);

    /* Unmounting twice reports the missing mount once */
    deep_expect(__test_status, "umount rootfs", mr_fs_umount("/"), 0);
    deep_expect(__test_status, "umount unmounted", mr_fs_umount("/"),
                -MR_ENOENT);

    /* A fresh RootFS mounts cleanly over the bare root again */
    ret = fs_test_mount_root("rootfs", 0, MR_NULL);
    deep_expect(__test_status, "remount rootfs", ret, 0);
    if (ret != 0) {
        return;
    }
    deep_expect(__test_status, "mkdir after remount", mr_fs_mkdir("/rf-again"),
                0);
    deep_stat_expect(__test_status, "/rf-again", 0, MR_INODE_DIR);
    deep_expect(__test_status, "rmdir after remount", mr_fs_rmdir("/rf-again"),
                0);
    deep_expect(__test_status, "umount remounted rootfs", mr_fs_umount("/"), 0);
    return;
}

#endif /* defined(MR_USE_FS_ROOTFS) */
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS)                           \
          && (defined(MR_USE_FS_RAMFS) || defined(MR_USE_FS_ROOTFS)) */
