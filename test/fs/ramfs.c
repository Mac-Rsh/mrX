/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS) && defined(MR_USE_FS_RAMFS)
#include <mrX/fs/mr_file.h>
#include <mrX/libc/mr_string.h>

static mr_err_t ramfs_mount(void) {
    mr_fs_umount("/");
    return mr_fs_mount(MR_NULL, "/", "ramfs", 0, MR_NULL);
}

static void ramfs_umount(void) {
    mr_fs_umount("/");
}

/* ---- registration / mount ---- */

static void test_ramfs_registered(void) {
    mr_file_system_t *fs = mr_file_system_find("ramfs");
    MR_TEST_EXPECT_NE_PTR(fs, MR_NULL);
}
MR_TEST_EXPORT(ramfs, registered, test_ramfs_registered);

static void test_ramfs_mount_root(void) {
    mr_err_t ret = ramfs_mount();
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, mount_root, test_ramfs_mount_root);

static void test_ramfs_mount_double(void) {
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }
    ret = mr_fs_mount(MR_NULL, "/", "ramfs", 0, MR_NULL);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EBUSY);
    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, mount_double, test_ramfs_mount_double);

static void test_ramfs_umount_not_mounted(void) {
    mr_err_t ret;

    ramfs_umount();
    ret = mr_fs_umount("/");
    MR_TEST_EXPECT_EQ_INT(ret, -MR_ENOENT);
}
MR_TEST_EXPORT(ramfs, umount_not_mounted, test_ramfs_umount_not_mounted);

/* ---- create / open / close ---- */

static void test_ramfs_open_create(void) {
    mr_file_t f;
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/file", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        MR_TEST_EXPECT_EQ_INT(f.inode->type, MR_INODE_REG);
        MR_TEST_EXPECT_EQ_INT(f.pos, 0);
        mr_fs_close(&f);
    }

    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, open_create, test_ramfs_open_create);

static void test_ramfs_open_noent_no_creat(void) {
    mr_file_t f;
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/missing", MR_O_RDONLY);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_ENOENT);

    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, open_noent_no_creat, test_ramfs_open_noent_no_creat);

static void test_ramfs_open_reopen(void) {
    mr_file_t f1, f2;
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f1, 0, sizeof(f1));
    ret = mr_fs_open(&f1, "/r", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        ramfs_umount();
        return;
    }
    mr_fs_close(&f1);

    mr_memset(&f2, 0, sizeof(f2));
    ret = mr_fs_open(&f2, "/r", MR_O_RDWR);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        mr_fs_close(&f2);
    }

    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, open_reopen, test_ramfs_open_reopen);

static void test_ramfs_open_excl(void) {
    mr_file_t f;
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/ex", MR_O_RDWR | MR_O_CREAT | MR_O_EXCL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        ramfs_umount();
        return;
    }
    mr_fs_close(&f);

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/ex", MR_O_RDWR | MR_O_CREAT | MR_O_EXCL);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EEXIST);

    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, open_excl, test_ramfs_open_excl);

/* ---- read / write ---- */

static void test_ramfs_write_read_basic(void) {
    mr_file_t f;
    char buf[16];
    mr_ssize_t nw, nr;
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/w", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        ramfs_umount();
        return;
    }

    nw = mr_fs_write(&f, "hello world", 11);
    MR_TEST_EXPECT_EQ_INT(nw, 11);
    MR_TEST_EXPECT_EQ_INT(f.inode->size, 11);

    mr_fs_seek(&f, 0, MR_SEEK_SET);
    mr_memset(buf, 0, sizeof(buf));
    nr = mr_fs_read(&f, buf, sizeof(buf));
    MR_TEST_EXPECT_EQ_INT(nr, 11);
    MR_TEST_EXPECT_EQ_INT(mr_strncmp(buf, "hello world", 11), 0);

    mr_fs_close(&f);
    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, write_read_basic, test_ramfs_write_read_basic);

static void test_ramfs_write_grow(void) {
    mr_file_t f;
    static const mr_size_t total = 4096;
    mr_uint8_t wbuf[256];
    mr_uint8_t rbuf[256];
    mr_ssize_t nw, nr;
    mr_size_t off;
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/big", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        ramfs_umount();
        return;
    }

    for (off = 0; off < total; off += sizeof(wbuf)) {
        mr_size_t i;

        for (i = 0; i < sizeof(wbuf); i++) {
            wbuf[i] = (mr_uint8_t)((off + i) & 0xFF);
        }
        nw = mr_fs_write(&f, wbuf, sizeof(wbuf));
        MR_TEST_EXPECT_EQ_INT(nw, (mr_ssize_t)sizeof(wbuf));
    }
    MR_TEST_EXPECT_EQ_INT(f.inode->size, (mr_int64_t)total);

    mr_fs_seek(&f, 0, MR_SEEK_SET);
    for (off = 0; off < total; off += sizeof(rbuf)) {
        mr_size_t i;

        mr_memset(rbuf, 0, sizeof(rbuf));
        nr = mr_fs_read(&f, rbuf, sizeof(rbuf));
        MR_TEST_EXPECT_EQ_INT(nr, (mr_ssize_t)sizeof(rbuf));
        for (i = 0; i < sizeof(rbuf); i++) {
            MR_TEST_EXPECT_EQ_INT(rbuf[i], (int)((off + i) & 0xFF));
        }
    }

    mr_fs_close(&f);
    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, write_grow, test_ramfs_write_grow);

static void test_ramfs_write_hole(void) {
    mr_file_t f;
    char buf[16];
    mr_ssize_t nr;
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/hole", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        ramfs_umount();
        return;
    }

    mr_fs_seek(&f, 5, MR_SEEK_SET);
    mr_fs_write(&f, "XY", 2);
    MR_TEST_EXPECT_EQ_INT(f.inode->size, 7);

    mr_fs_seek(&f, 0, MR_SEEK_SET);
    mr_memset(buf, 0, sizeof(buf));
    nr = mr_fs_read(&f, buf, 7);
    MR_TEST_EXPECT_EQ_INT(nr, 7);
    MR_TEST_EXPECT_EQ_INT(buf[0], 0);
    MR_TEST_EXPECT_EQ_INT(buf[4], 0);
    MR_TEST_EXPECT_EQ_INT(buf[5], 'X');
    MR_TEST_EXPECT_EQ_INT(buf[6], 'Y');

    mr_fs_close(&f);
    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, write_hole, test_ramfs_write_hole);

static void test_ramfs_write_hole_after_truncate(void) {
    mr_file_t f;
    char buf[8];
    mr_ssize_t nr;
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/ht", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        ramfs_umount();
        return;
    }

    /* Fill with non-zero data, then truncate to free the buffer (dirty) */
    mr_fs_write(&f, "AAAAAA", 6);
    mr_fs_truncate(&f, 0);
    MR_TEST_EXPECT_EQ_INT(f.inode->size, 0);

    /* Seek past start and write - hole [0,4) must be zero-filled */
    mr_fs_seek(&f, 4, MR_SEEK_SET);
    mr_fs_write(&f, "XY", 2);
    MR_TEST_EXPECT_EQ_INT(f.inode->size, 6);

    mr_fs_seek(&f, 0, MR_SEEK_SET);
    mr_memset(buf, 0xFF, sizeof(buf));
    nr = mr_fs_read(&f, buf, 6);
    MR_TEST_EXPECT_EQ_INT(nr, 6);
    MR_TEST_EXPECT_EQ_INT(buf[0], 0);
    MR_TEST_EXPECT_EQ_INT(buf[1], 0);
    MR_TEST_EXPECT_EQ_INT(buf[2], 0);
    MR_TEST_EXPECT_EQ_INT(buf[3], 0);
    MR_TEST_EXPECT_EQ_INT(buf[4], 'X');
    MR_TEST_EXPECT_EQ_INT(buf[5], 'Y');

    mr_fs_close(&f);
    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, write_hole_after_truncate, test_ramfs_write_hole_after_truncate);

static void test_ramfs_read_eof(void) {
    mr_file_t f;
    char buf[8];
    mr_ssize_t nr;
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/eof", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        ramfs_umount();
        return;
    }

    mr_fs_write(&f, "abc", 3);
    mr_fs_seek(&f, 0, MR_SEEK_SET);

    mr_memset(buf, 0xFF, sizeof(buf));
    nr = mr_fs_read(&f, buf, sizeof(buf));
    MR_TEST_EXPECT_EQ_INT(nr, 3);

    mr_fs_close(&f);
    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, read_eof, test_ramfs_read_eof);

static void test_ramfs_write_zero_size(void) {
    mr_file_t f;
    mr_ssize_t nw;
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/z", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        ramfs_umount();
        return;
    }

    nw = mr_fs_write(&f, "data", 0);
    MR_TEST_EXPECT_EQ_INT(nw, 0);
    MR_TEST_EXPECT_EQ_INT(f.inode->size, 0);

    mr_fs_close(&f);
    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, write_zero_size, test_ramfs_write_zero_size);

static void test_ramfs_o_append(void) {
    mr_file_t f;
    char buf[16];
    mr_ssize_t nr;
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/ap", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        ramfs_umount();
        return;
    }
    mr_fs_write(&f, "AAA", 3);
    mr_fs_close(&f);

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/ap", MR_O_RDWR | MR_O_APPEND);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        ramfs_umount();
        return;
    }
    mr_fs_write(&f, "BB", 2);
    mr_fs_close(&f);

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/ap", MR_O_RDONLY);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        ramfs_umount();
        return;
    }
    mr_memset(buf, 0, sizeof(buf));
    nr = mr_fs_read(&f, buf, sizeof(buf));
    MR_TEST_EXPECT_EQ_INT(nr, 5);
    MR_TEST_EXPECT_EQ_INT(mr_strncmp(buf, "AAABB", 5), 0);
    mr_fs_close(&f);

    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, o_append, test_ramfs_o_append);

/* ---- seek / tell ---- */

static void test_ramfs_seek_set(void) {
    mr_file_t f;
    mr_int64_t pos;
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/sk", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        ramfs_umount();
        return;
    }
    mr_fs_write(&f, "0123456789", 10);

    pos = mr_fs_seek(&f, 3, MR_SEEK_SET);
    MR_TEST_EXPECT_EQ_INT(pos, 3);
    pos = mr_fs_tell(&f);
    MR_TEST_EXPECT_EQ_INT(pos, 3);

    mr_fs_close(&f);
    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, seek_set, test_ramfs_seek_set);

static void test_ramfs_seek_end(void) {
    mr_file_t f;
    mr_int64_t pos;
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/se", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        ramfs_umount();
        return;
    }
    mr_fs_write(&f, "abcdef", 6);

    pos = mr_fs_seek(&f, -2, MR_SEEK_END);
    MR_TEST_EXPECT_EQ_INT(pos, 4);

    mr_fs_close(&f);
    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, seek_end, test_ramfs_seek_end);

/* ---- stat ---- */

static void test_ramfs_stat_file(void) {
    mr_file_t f;
    mr_stat_t st;
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/st", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        ramfs_umount();
        return;
    }
    mr_fs_write(&f, "12345678", 8);
    mr_fs_close(&f);

    mr_memset(&st, 0, sizeof(st));
    ret = mr_fs_stat("/st", &st);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(st.type, MR_INODE_REG);
    MR_TEST_EXPECT_EQ_INT(st.size, 8);

    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, stat_file, test_ramfs_stat_file);

static void test_ramfs_stat_noent(void) {
    mr_stat_t st;
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_fs_stat("/nope", &st);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_ENOENT);

    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, stat_noent, test_ramfs_stat_noent);

/* ---- directories ---- */

static void test_ramfs_mkdir(void) {
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_fs_mkdir("/d");
    MR_TEST_EXPECT_EQ_INT(ret, 0);

    ret = mr_fs_mkdir("/d");
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EEXIST);

    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, mkdir, test_ramfs_mkdir);

static void test_ramfs_mkdir_nested(void) {
    mr_file_t f;
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_fs_mkdir("/a");
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        ramfs_umount();
        return;
    }
    ret = mr_fs_mkdir("/a/b");
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        ramfs_umount();
        return;
    }
    ret = mr_fs_mkdir("/a/b/c");
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/a/b/c/file", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        mr_fs_write(&f, "deep", 4);
        mr_fs_close(&f);
    }

    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, mkdir_nested, test_ramfs_mkdir_nested);

static void test_ramfs_readdir(void) {
    mr_file_t f;
    mr_dirent_t de;
    int found_file = 0, found_sub = 0;
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_fs_mkdir("/sub");
    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/f1", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret == 0) {
        mr_fs_close(&f);
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/", MR_O_DIRECTORY);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        ramfs_umount();
        return;
    }

    while (mr_fs_readdir(&f, &de)) {
        if (mr_strcmp(de.name, "sub") == 0) {
            found_sub = 1;
            MR_TEST_EXPECT_EQ_INT(de.type, MR_INODE_DIR);
        }
        if (mr_strcmp(de.name, "f1") == 0) {
            found_file = 1;
            MR_TEST_EXPECT_EQ_INT(de.type, MR_INODE_REG);
        }
    }
    MR_TEST_EXPECT_EQ_INT(found_sub, 1);
    MR_TEST_EXPECT_EQ_INT(found_file, 1);

    mr_fs_close(&f);
    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, readdir, test_ramfs_readdir);

static void test_ramfs_readdir_many(void) {
    mr_file_t f;
    mr_dirent_t de;
    char name[8];
    int count = 0;
    int i;
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    for (i = 0; i < 10; i++) {
        name[0] = (char)('0' + i);
        name[1] = '\0';
        mr_memset(&f, 0, sizeof(f));
        ret = mr_fs_open(&f, name, MR_O_RDWR | MR_O_CREAT);
        MR_TEST_ASSERT_EQ_INT(ret, 0);
        if (ret == 0) {
            mr_fs_close(&f);
        }
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/", MR_O_DIRECTORY);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        ramfs_umount();
        return;
    }

    while (mr_fs_readdir(&f, &de)) {
        count++;
    }
    MR_TEST_EXPECT_EQ_INT(count, 10);

    mr_fs_close(&f);
    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, readdir_many, test_ramfs_readdir_many);

/* ---- unlink ---- */

static void test_ramfs_unlink_file(void) {
    mr_file_t f;
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/u", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        ramfs_umount();
        return;
    }
    mr_fs_close(&f);

    ret = mr_fs_unlink("/u");
    MR_TEST_EXPECT_EQ_INT(ret, 0);

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/u", MR_O_RDONLY);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_ENOENT);

    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, unlink_file, test_ramfs_unlink_file);

static void test_ramfs_unlink_dir_nonempty(void) {
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_fs_mkdir("/dd");
    mr_fs_mkdir("/dd/inner");

    ret = mr_fs_unlink("/dd");
    MR_TEST_EXPECT_EQ_INT(ret, -MR_ENOTEMPTY);

    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, unlink_dir_nonempty, test_ramfs_unlink_dir_nonempty);

static void test_ramfs_unlink_noent(void) {
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_fs_unlink("/ghost");
    MR_TEST_EXPECT_EQ_INT(ret, -MR_ENOENT);

    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, unlink_noent, test_ramfs_unlink_noent);

/* ---- rename ---- */

static void test_ramfs_rename_file(void) {
    mr_file_t f;
    char buf[8];
    mr_ssize_t nr;
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/old", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        ramfs_umount();
        return;
    }
    mr_fs_write(&f, "data", 4);
    mr_fs_close(&f);

    ret = mr_fs_rename("/old", "/new");
    MR_TEST_EXPECT_EQ_INT(ret, 0);

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/old", MR_O_RDONLY);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_ENOENT);

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/new", MR_O_RDONLY);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        ramfs_umount();
        return;
    }
    mr_memset(buf, 0, sizeof(buf));
    nr = mr_fs_read(&f, buf, sizeof(buf));
    MR_TEST_EXPECT_EQ_INT(nr, 4);
    MR_TEST_EXPECT_EQ_INT(mr_strncmp(buf, "data", 4), 0);
    mr_fs_close(&f);

    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, rename_file, test_ramfs_rename_file);

static void test_ramfs_rename_cross_dir(void) {
    mr_file_t f;
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_fs_mkdir("/src");
    mr_fs_mkdir("/dst");

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/src/f", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret == 0) {
        mr_fs_close(&f);
    }

    ret = mr_fs_rename("/src/f", "/dst/f");
    MR_TEST_EXPECT_EQ_INT(ret, 0);

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/src/f", MR_O_RDONLY);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_ENOENT);

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/dst/f", MR_O_RDONLY);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        mr_fs_close(&f);
    }

    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, rename_cross_dir, test_ramfs_rename_cross_dir);

/* ---- truncate ---- */

static void test_ramfs_truncate_grow(void) {
    mr_file_t f;
    char buf[16];
    mr_ssize_t nr;
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/tg", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        ramfs_umount();
        return;
    }
    mr_fs_write(&f, "AB", 2);

    ret = mr_fs_truncate(&f, 10);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(f.inode->size, 10);

    mr_fs_seek(&f, 0, MR_SEEK_SET);
    mr_memset(buf, 0xFF, sizeof(buf));
    nr = mr_fs_read(&f, buf, sizeof(buf));
    MR_TEST_EXPECT_EQ_INT(nr, 10);
    MR_TEST_EXPECT_EQ_INT(buf[0], 'A');
    MR_TEST_EXPECT_EQ_INT(buf[1], 'B');
    MR_TEST_EXPECT_EQ_INT(buf[2], 0);
    MR_TEST_EXPECT_EQ_INT(buf[9], 0);

    mr_fs_close(&f);
    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, truncate_grow, test_ramfs_truncate_grow);

static void test_ramfs_truncate_shrink(void) {
    mr_file_t f;
    char buf[16];
    mr_ssize_t nr;
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/ts", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        ramfs_umount();
        return;
    }
    mr_fs_write(&f, "0123456789", 10);

    ret = mr_fs_truncate(&f, 3);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(f.inode->size, 3);

    mr_fs_seek(&f, 0, MR_SEEK_SET);
    mr_memset(buf, 0, sizeof(buf));
    nr = mr_fs_read(&f, buf, sizeof(buf));
    MR_TEST_EXPECT_EQ_INT(nr, 3);
    MR_TEST_EXPECT_EQ_INT(mr_strncmp(buf, "012", 3), 0);

    mr_fs_close(&f);
    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, truncate_shrink, test_ramfs_truncate_shrink);

static void test_ramfs_truncate_to_zero(void) {
    mr_file_t f;
    mr_ssize_t nr;
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/tz", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        ramfs_umount();
        return;
    }
    mr_fs_write(&f, "data", 4);

    ret = mr_fs_truncate(&f, 0);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(f.inode->size, 0);

    mr_fs_seek(&f, 0, MR_SEEK_SET);
    nr = mr_fs_read(&f, (char[1]){0}, 1);
    MR_TEST_EXPECT_EQ_INT(nr, 0);

    mr_fs_close(&f);
    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, truncate_to_zero, test_ramfs_truncate_to_zero);

static void test_ramfs_truncate_extend_write(void) {
    mr_file_t f;
    char buf[16];
    mr_ssize_t nr;
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/te", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        ramfs_umount();
        return;
    }
    mr_fs_write(&f, "hello", 5);

    mr_fs_truncate(&f, 8);
    MR_TEST_EXPECT_EQ_INT(f.inode->size, 8);

    mr_fs_seek(&f, 5, MR_SEEK_SET);
    mr_fs_write(&f, "WORLD", 5);
    MR_TEST_EXPECT_EQ_INT(f.inode->size, 10);

    mr_fs_seek(&f, 0, MR_SEEK_SET);
    mr_memset(buf, 0, sizeof(buf));
    nr = mr_fs_read(&f, buf, sizeof(buf));
    MR_TEST_EXPECT_EQ_INT(nr, 10);
    MR_TEST_EXPECT_EQ_INT(buf[4], 'o');
    MR_TEST_EXPECT_EQ_INT(buf[5], 'W');

    mr_fs_close(&f);
    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, truncate_extend_write, test_ramfs_truncate_extend_write);

/* ---- O_TRUNC ---- */

static void test_ramfs_o_trunc(void) {
    mr_file_t f;
    char buf[8];
    mr_ssize_t nr;
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/tr", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        ramfs_umount();
        return;
    }
    mr_fs_write(&f, "1234567890", 10);
    mr_fs_close(&f);

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/tr", MR_O_RDWR | MR_O_TRUNC);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        ramfs_umount();
        return;
    }
    MR_TEST_EXPECT_EQ_INT(f.inode->size, 0);
    mr_fs_write(&f, "hi", 2);
    mr_fs_close(&f);

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/tr", MR_O_RDONLY);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        ramfs_umount();
        return;
    }
    mr_memset(buf, 0, sizeof(buf));
    nr = mr_fs_read(&f, buf, sizeof(buf));
    MR_TEST_EXPECT_EQ_INT(nr, 2);
    MR_TEST_EXPECT_EQ_INT(mr_strncmp(buf, "hi", 2), 0);
    mr_fs_close(&f);

    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, o_trunc, test_ramfs_o_trunc);

/* ---- overwrite ---- */

static void test_ramfs_overwrite_middle(void) {
    mr_file_t f;
    char buf[16];
    mr_ssize_t nr;
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/ow", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        ramfs_umount();
        return;
    }
    mr_fs_write(&f, "AAAAA", 5);
    mr_fs_seek(&f, 1, MR_SEEK_SET);
    mr_fs_write(&f, "bb", 2);
    mr_fs_close(&f);

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/ow", MR_O_RDONLY);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        ramfs_umount();
        return;
    }
    mr_memset(buf, 0, sizeof(buf));
    nr = mr_fs_read(&f, buf, sizeof(buf));
    MR_TEST_EXPECT_EQ_INT(nr, 5);
    MR_TEST_EXPECT_EQ_INT(mr_strncmp(buf, "AbbAA", 5), 0);
    mr_fs_close(&f);

    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, overwrite_middle, test_ramfs_overwrite_middle);

/* ---- mount isolation ---- */

static void test_ramfs_mount_isolation(void) {
    mr_file_t f;
    mr_err_t ret;

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/persist", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret == 0) {
        mr_fs_write(&f, "data", 4);
        mr_fs_close(&f);
    }
    ramfs_umount();

    ret = ramfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/persist", MR_O_RDONLY);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_ENOENT);

    ramfs_umount();
}
MR_TEST_EXPORT(ramfs, mount_isolation, test_ramfs_mount_isolation);
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) && defined(MR_USE_FS_RAMFS) */
