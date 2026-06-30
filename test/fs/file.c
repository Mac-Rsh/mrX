/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS)
#include <mrX/fs/mr_file.h>
#include <mrX/libc/mr_string.h>
#include "testfs.h"

/* Helper: ensure testfs is mounted at "/", tolerant of a prior mount */
static mr_err_t vfs_mount(void) {
    mr_err_t ret = mr_fs_mount(MR_NULL, "/", "testfs", 0, MR_NULL);
    if (ret == -MR_EBUSY) {
        return 0;
    }
    return ret;
}

static void vfs_umount(void) {
    mr_fs_umount("/");
}

/* Tests: mount / umount */

static void test_vfs_mount_root(void) {
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_EXPECT_EQ_INT(ret, 0);

    vfs_umount();
}
MR_TEST_EXPORT(vfs, mount_root, test_vfs_mount_root);

static void test_vfs_mount_unknown_fstype(void) {
    mr_err_t ret;

    ret = mr_fs_mount(MR_NULL, "/", "no_such_fs", 0, MR_NULL);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_ENOENT);
}
MR_TEST_EXPORT(vfs, mount_unknown_fstype, test_vfs_mount_unknown_fstype);

static void test_vfs_mount_double(void) {
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    /* Mounting again at the same point must fail */
    ret = mr_fs_mount(MR_NULL, "/", "testfs", 0, MR_NULL);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EBUSY);

    vfs_umount();
}
MR_TEST_EXPORT(vfs, mount_double, test_vfs_mount_double);

static void test_vfs_umount_not_mounted(void) {
    mr_err_t ret;

    /* Ensure nothing is mounted, then try to umount */
    vfs_umount();
    ret = mr_fs_umount("/");
    MR_TEST_EXPECT_EQ_INT(ret, -MR_ENOENT);
}
MR_TEST_EXPORT(vfs, umount_not_mounted, test_vfs_umount_not_mounted);

static void test_vfs_mount_subdir(void) {
    mr_err_t ret;

    /* Mount at a subdirectory path */
    ret = mr_fs_mount(MR_NULL, "/mnt", "testfs", 0, MR_NULL);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        mr_fs_umount("/mnt");
    }
}
MR_TEST_EXPORT(vfs, mount_subdir, test_vfs_mount_subdir);

/* Tests: open / create / close */

static void test_vfs_open_create(void) {
    mr_file_t f;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/file1", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        MR_TEST_EXPECT_NE_PTR(f.fops, MR_NULL);
        MR_TEST_EXPECT_NE_PTR(f.inode, MR_NULL);
        MR_TEST_EXPECT_EQ_INT(f.inode->type, MR_INODE_REG);
        mr_fs_close(&f);
    }

    vfs_umount();
}
MR_TEST_EXPORT(vfs, open_create, test_vfs_open_create);

static void test_vfs_open_existing(void) {
    mr_file_t f1, f2;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f1, 0, sizeof(f1));
    ret = mr_fs_open(&f1, "/e", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }
    mr_fs_close(&f1);

    /* Re-open without O_CREAT */
    mr_memset(&f2, 0, sizeof(f2));
    ret = mr_fs_open(&f2, "/e", MR_O_RDWR);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        mr_fs_close(&f2);
    }

    vfs_umount();
}
MR_TEST_EXPORT(vfs, open_existing, test_vfs_open_existing);

static void test_vfs_open_noent_no_creat(void) {
    mr_file_t f;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/missing", MR_O_RDONLY);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_ENOENT);

    vfs_umount();
}
MR_TEST_EXPORT(vfs, open_noent_no_creat, test_vfs_open_noent_no_creat);

static void test_vfs_open_excl(void) {
    mr_file_t f;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/x", MR_O_RDWR | MR_O_CREAT | MR_O_EXCL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }
    mr_fs_close(&f);

    /* O_CREAT|O_EXCL on an existing file must fail */
    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/x", MR_O_RDWR | MR_O_CREAT | MR_O_EXCL);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EEXIST);

    vfs_umount();
}
MR_TEST_EXPORT(vfs, open_excl, test_vfs_open_excl);

static void test_vfs_close_not_open(void) {
    mr_file_t f;
    mr_err_t ret;

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_close(&f);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EBADF);
}
MR_TEST_EXPORT(vfs, close_not_open, test_vfs_close_not_open);

static void test_vfs_open_already_open(void) {
    mr_file_t f;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/ao", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }

    /* Opening into the same handle must fail */
    ret = mr_fs_open(&f, "/ao", MR_O_RDWR);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EBUSY);

    mr_fs_close(&f);
    vfs_umount();
}
MR_TEST_EXPORT(vfs, open_already_open, test_vfs_open_already_open);

static void test_vfs_open_bad_accmode(void) {
    mr_file_t f;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    /* access mode 3 (0b11) is out of range: only 0-2 are valid */
    ret = mr_fs_open(&f, "/b", MR_O_ACCMODE);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EINVAL);

    vfs_umount();
}
MR_TEST_EXPORT(vfs, open_bad_accmode, test_vfs_open_bad_accmode);

/* Tests: read / write */

static void test_vfs_write_read(void) {
    mr_file_t f;
    char buf[32];
    mr_ssize_t nw, nr;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/rw", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }

    mr_strncpy(buf, "hello", 6);
    nw = mr_fs_write(&f, buf, 5);
    MR_TEST_EXPECT_EQ_INT(nw, 5);

    /* Seek back to start */
    mr_fs_seek(&f, 0, MR_SEEK_SET);

    mr_memset(buf, 0, sizeof(buf));
    nr = mr_fs_read(&f, buf, 5);
    MR_TEST_EXPECT_EQ_INT(nr, 5);
    MR_TEST_EXPECT_EQ_INT(mr_strncmp(buf, "hello", 5), 0);

    mr_fs_close(&f);
    vfs_umount();
}
MR_TEST_EXPORT(vfs, write_read, test_vfs_write_read);

static void test_vfs_read_write_only(void) {
    mr_file_t f;
    char buf[8];
    mr_ssize_t nr;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/wo", MR_O_WRONLY | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }

    nr = mr_fs_read(&f, buf, sizeof(buf));
    MR_TEST_EXPECT_EQ_INT(nr, -MR_EBADF);

    mr_fs_close(&f);
    vfs_umount();
}
MR_TEST_EXPORT(vfs, read_write_only, test_vfs_read_write_only);

static void test_vfs_write_read_only(void) {
    mr_file_t f1, f2;
    mr_ssize_t nw;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f1, 0, sizeof(f1));
    ret = mr_fs_open(&f1, "/ro", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }
    mr_fs_write(&f1, "data", 4);
    mr_fs_close(&f1);

    mr_memset(&f2, 0, sizeof(f2));
    ret = mr_fs_open(&f2, "/ro", MR_O_RDONLY);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }

    nw = mr_fs_write(&f2, "x", 1);
    MR_TEST_EXPECT_EQ_INT(nw, -MR_EBADF);

    mr_fs_close(&f2);
    vfs_umount();
}
MR_TEST_EXPORT(vfs, write_read_only, test_vfs_write_read_only);

static void test_vfs_read_not_open(void) {
    mr_file_t f;
    char buf[8];
    mr_ssize_t nr;

    mr_memset(&f, 0, sizeof(f));
    nr = mr_fs_read(&f, buf, sizeof(buf));
    MR_TEST_EXPECT_EQ_INT(nr, -MR_EBADF);
}
MR_TEST_EXPORT(vfs, read_not_open, test_vfs_read_not_open);

static void test_vfs_write_not_open(void) {
    mr_file_t f;
    mr_ssize_t nw;

    mr_memset(&f, 0, sizeof(f));
    nw = mr_fs_write(&f, "x", 1);
    MR_TEST_EXPECT_EQ_INT(nw, -MR_EBADF);
}
MR_TEST_EXPORT(vfs, write_not_open, test_vfs_write_not_open);

static void test_vfs_write_grow_size(void) {
    mr_file_t f;
    mr_stat_t st;
    mr_ssize_t nw;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/g", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }

    nw = mr_fs_write(&f, "1234567890", 10);
    MR_TEST_EXPECT_EQ_INT(nw, 10);
    mr_fs_close(&f);

    ret = mr_fs_stat("/g", &st);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        MR_TEST_EXPECT_EQ_INT(st.size, 10);
    }

    vfs_umount();
}
MR_TEST_EXPORT(vfs, write_grow_size, test_vfs_write_grow_size);

/* Tests: seek / tell */

static void test_vfs_seek_set(void) {
    mr_file_t f;
    mr_int64_t pos;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/s", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }

    mr_fs_write(&f, "abcdef", 6);

    pos = mr_fs_seek(&f, 3, MR_SEEK_SET);
    MR_TEST_EXPECT_EQ_INT(pos, 3);
    MR_TEST_EXPECT_EQ_INT(mr_fs_tell(&f), 3);

    mr_fs_close(&f);
    vfs_umount();
}
MR_TEST_EXPORT(vfs, seek_set, test_vfs_seek_set);

static void test_vfs_seek_cur(void) {
    mr_file_t f;
    mr_int64_t pos;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/sc", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }

    mr_fs_write(&f, "abcdef", 6);
    mr_fs_seek(&f, 2, MR_SEEK_SET);

    pos = mr_fs_seek(&f, 2, MR_SEEK_CUR);
    MR_TEST_EXPECT_EQ_INT(pos, 4);

    mr_fs_close(&f);
    vfs_umount();
}
MR_TEST_EXPORT(vfs, seek_cur, test_vfs_seek_cur);

static void test_vfs_seek_end(void) {
    mr_file_t f;
    mr_int64_t pos;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/se", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }

    mr_fs_write(&f, "abcdef", 6);

    pos = mr_fs_seek(&f, -2, MR_SEEK_END);
    MR_TEST_EXPECT_EQ_INT(pos, 4);

    mr_fs_close(&f);
    vfs_umount();
}
MR_TEST_EXPORT(vfs, seek_end, test_vfs_seek_end);

static void test_vfs_seek_bad_whence(void) {
    mr_file_t f;
    mr_int64_t pos;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/sbw", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }

    pos = mr_fs_seek(&f, 0, 99);
    MR_TEST_EXPECT_EQ_INT(pos, -MR_EINVAL);

    mr_fs_close(&f);
    vfs_umount();
}
MR_TEST_EXPORT(vfs, seek_bad_whence, test_vfs_seek_bad_whence);

static void test_vfs_tell_not_open(void) {
    mr_file_t f;
    mr_int64_t pos;

    mr_memset(&f, 0, sizeof(f));
    pos = mr_fs_tell(&f);
    MR_TEST_EXPECT_EQ_INT(pos, -MR_EBADF);
}
MR_TEST_EXPORT(vfs, tell_not_open, test_vfs_tell_not_open);

static void test_vfs_seek_negative_clamped(void) {
    mr_file_t f;
    mr_int64_t pos;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/sn", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }

    /* Seek before start should clamp to 0 */
    pos = mr_fs_seek(&f, -100, MR_SEEK_SET);
    MR_TEST_EXPECT_EQ_INT(pos, 0);

    mr_fs_close(&f);
    vfs_umount();
}
MR_TEST_EXPORT(vfs, seek_negative_clamped, test_vfs_seek_negative_clamped);

/* Tests: O_APPEND */

static void test_vfs_o_append(void) {
    mr_file_t f;
    char buf[16];
    mr_ssize_t nr;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/ap", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }
    mr_fs_write(&f, "abc", 3);
    mr_fs_close(&f);

    /* Re-open with O_APPEND; writes should go to the end */
    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/ap", MR_O_RDWR | MR_O_APPEND);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }
    MR_TEST_EXPECT_EQ_INT(mr_fs_tell(&f), 3);
    mr_fs_write(&f, "def", 3);
    mr_fs_close(&f);

    /* Read back: should be "abcdef" */
    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/ap", MR_O_RDONLY);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }
    mr_memset(buf, 0, sizeof(buf));
    nr = mr_fs_read(&f, buf, sizeof(buf));
    MR_TEST_EXPECT_EQ_INT(nr, 6);
    MR_TEST_EXPECT_EQ_INT(mr_strncmp(buf, "abcdef", 6), 0);
    mr_fs_close(&f);

    vfs_umount();
}
MR_TEST_EXPORT(vfs, o_append, test_vfs_o_append);

/* Tests: O_TRUNC */

static void test_vfs_o_trunc(void) {
    mr_file_t f;
    mr_stat_t st;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/tr", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }
    mr_fs_write(&f, "1234567890", 10);
    mr_fs_close(&f);

    /* Re-open with O_TRUNC; size should become 0 */
    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/tr", MR_O_RDWR | MR_O_TRUNC);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }
    MR_TEST_EXPECT_EQ_INT(mr_fs_tell(&f), 0);
    mr_fs_close(&f);

    ret = mr_fs_stat("/tr", &st);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        MR_TEST_EXPECT_EQ_INT(st.size, 0);
    }

    vfs_umount();
}
MR_TEST_EXPORT(vfs, o_trunc, test_vfs_o_trunc);

/* Tests: stat */

static void test_vfs_stat_file(void) {
    mr_file_t f;
    mr_stat_t st;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/st", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }
    mr_fs_write(&f, "hello", 5);
    mr_fs_close(&f);

    ret = mr_fs_stat("/st", &st);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        MR_TEST_EXPECT_EQ_INT(st.type, MR_INODE_REG);
        MR_TEST_EXPECT_EQ_INT(st.size, 5);
    }

    vfs_umount();
}
MR_TEST_EXPORT(vfs, stat_file, test_vfs_stat_file);

static void test_vfs_stat_noent(void) {
    mr_stat_t st;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_fs_stat("/no_such_file", &st);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_ENOENT);

    vfs_umount();
}
MR_TEST_EXPORT(vfs, stat_noent, test_vfs_stat_noent);

/* Tests: mkdir */

static void test_vfs_mkdir_normal(void) {
    mr_stat_t st;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_fs_mkdir("/dir1");
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        ret = mr_fs_stat("/dir1", &st);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
        if (ret == 0) {
            MR_TEST_EXPECT_EQ_INT(st.type, MR_INODE_DIR);
        }
    }

    vfs_umount();
}
MR_TEST_EXPORT(vfs, mkdir_normal, test_vfs_mkdir_normal);

static void test_vfs_mkdir_existing(void) {
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_fs_mkdir("/d");
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }

    ret = mr_fs_mkdir("/d");
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EEXIST);

    vfs_umount();
}
MR_TEST_EXPORT(vfs, mkdir_existing, test_vfs_mkdir_existing);

static void test_vfs_mkdir_no_parent(void) {
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    /* Parent /nope does not exist */
    ret = mr_fs_mkdir("/nope/child");
    MR_TEST_EXPECT_EQ_INT(ret, -MR_ENOENT);

    vfs_umount();
}
MR_TEST_EXPORT(vfs, mkdir_no_parent, test_vfs_mkdir_no_parent);

static void test_vfs_mkdir_nested(void) {
    mr_stat_t st;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_fs_mkdir("/a");
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }

    ret = mr_fs_mkdir("/a/b");
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        ret = mr_fs_stat("/a/b", &st);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
        if (ret == 0) {
            MR_TEST_EXPECT_EQ_INT(st.type, MR_INODE_DIR);
        }
    }

    vfs_umount();
}
MR_TEST_EXPORT(vfs, mkdir_nested, test_vfs_mkdir_nested);

/* Tests: opendir / readdir / closedir */

static void test_vfs_readdir(void) {
    mr_file_t dir;
    mr_dirent_t de;
    int count;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    /* Create some files */
    mr_fs_mkdir("/d");
    {
        mr_file_t f;
        mr_memset(&f, 0, sizeof(f));
        mr_fs_open(&f, "/d/f1", MR_O_RDWR | MR_O_CREAT);
        mr_fs_close(&f);
        mr_memset(&f, 0, sizeof(f));
        mr_fs_open(&f, "/d/f2", MR_O_RDWR | MR_O_CREAT);
        mr_fs_close(&f);
    }

    mr_memset(&dir, 0, sizeof(dir));
    ret = mr_fs_opendir(&dir, "/d");
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }

    count = 0;
    while (mr_fs_readdir(&dir, &de)) {
        count++;
    }
    MR_TEST_EXPECT_EQ_INT(count, 2);

    mr_fs_closedir(&dir);
    vfs_umount();
}
MR_TEST_EXPORT(vfs, readdir, test_vfs_readdir);

static void test_vfs_opendir_not_dir(void) {
    mr_file_t dir;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    {
        mr_file_t f;
        mr_memset(&f, 0, sizeof(f));
        mr_fs_open(&f, "/file", MR_O_RDWR | MR_O_CREAT);
        mr_fs_close(&f);
    }

    mr_memset(&dir, 0, sizeof(dir));
    ret = mr_fs_opendir(&dir, "/file");
    MR_TEST_EXPECT_EQ_INT(ret, -MR_ENOTDIR);

    vfs_umount();
}
MR_TEST_EXPORT(vfs, opendir_not_dir, test_vfs_opendir_not_dir);

static void test_vfs_opendir_noent(void) {
    mr_file_t dir;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&dir, 0, sizeof(dir));
    ret = mr_fs_opendir(&dir, "/nope");
    MR_TEST_EXPECT_EQ_INT(ret, -MR_ENOENT);

    vfs_umount();
}
MR_TEST_EXPORT(vfs, opendir_noent, test_vfs_opendir_noent);

/* Tests: unlink */

static void test_vfs_unlink_file(void) {
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    {
        mr_file_t f;
        mr_memset(&f, 0, sizeof(f));
        mr_fs_open(&f, "/u", MR_O_RDWR | MR_O_CREAT);
        mr_fs_close(&f);
    }

    ret = mr_fs_unlink("/u");
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        /* Verify it's gone */
        mr_stat_t st;
        ret = mr_fs_stat("/u", &st);
        MR_TEST_EXPECT_EQ_INT(ret, -MR_ENOENT);
    }

    vfs_umount();
}
MR_TEST_EXPORT(vfs, unlink_file, test_vfs_unlink_file);

static void test_vfs_unlink_empty_dir(void) {
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_fs_mkdir("/edd");
    ret = mr_fs_unlink("/edd");
    MR_TEST_EXPECT_EQ_INT(ret, 0);

    vfs_umount();
}
MR_TEST_EXPORT(vfs, unlink_empty_dir, test_vfs_unlink_empty_dir);

static void test_vfs_unlink_nonempty_dir(void) {
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_fs_mkdir("/nd");
    {
        mr_file_t f;
        mr_memset(&f, 0, sizeof(f));
        mr_fs_open(&f, "/nd/child", MR_O_RDWR | MR_O_CREAT);
        mr_fs_close(&f);
    }

    ret = mr_fs_unlink("/nd");
    MR_TEST_EXPECT_EQ_INT(ret, -MR_ENOTEMPTY);

    vfs_umount();
}
MR_TEST_EXPORT(vfs, unlink_nonempty_dir, test_vfs_unlink_nonempty_dir);

static void test_vfs_unlink_noent(void) {
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_fs_unlink("/no_such");
    MR_TEST_EXPECT_EQ_INT(ret, -MR_ENOENT);

    vfs_umount();
}
MR_TEST_EXPORT(vfs, unlink_noent, test_vfs_unlink_noent);

static void test_vfs_unlink_mountpoint(void) {
    mr_err_t ret;

    /* Mount testfs at a subdir, then try to unlink the mount-point */
    ret = mr_fs_mount(MR_NULL, "/mp", "testfs", 0, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_fs_unlink("/mp");
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EBUSY);

    mr_fs_umount("/mp");
}
MR_TEST_EXPORT(vfs, unlink_mountpoint, test_vfs_unlink_mountpoint);

/* Tests: rename */

static void test_vfs_rename_file(void) {
    mr_stat_t st;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    {
        mr_file_t f;
        mr_memset(&f, 0, sizeof(f));
        mr_fs_open(&f, "/old", MR_O_RDWR | MR_O_CREAT);
        mr_fs_close(&f);
    }

    ret = mr_fs_rename("/old", "/new");
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        ret = mr_fs_stat("/old", &st);
        MR_TEST_EXPECT_EQ_INT(ret, -MR_ENOENT);

        ret = mr_fs_stat("/new", &st);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
    }

    vfs_umount();
}
MR_TEST_EXPORT(vfs, rename_file, test_vfs_rename_file);

static void test_vfs_rename_onto_existing(void) {
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    {
        mr_file_t f;
        mr_memset(&f, 0, sizeof(f));
        mr_fs_open(&f, "/s", MR_O_RDWR | MR_O_CREAT);
        mr_fs_close(&f);
        mr_memset(&f, 0, sizeof(f));
        mr_fs_open(&f, "/d", MR_O_RDWR | MR_O_CREAT);
        mr_fs_close(&f);
    }

    ret = mr_fs_rename("/s", "/d");
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EEXIST);

    vfs_umount();
}
MR_TEST_EXPORT(vfs, rename_onto_existing, test_vfs_rename_onto_existing);

static void test_vfs_rename_noent(void) {
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_fs_rename("/no_src", "/dst");
    MR_TEST_EXPECT_EQ_INT(ret, -MR_ENOENT);

    vfs_umount();
}
MR_TEST_EXPORT(vfs, rename_noent, test_vfs_rename_noent);

static void test_vfs_rename_to_itself(void) {
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    {
        mr_file_t f;
        mr_memset(&f, 0, sizeof(f));
        mr_fs_open(&f, "/self", MR_O_RDWR | MR_O_CREAT);
        mr_fs_close(&f);
    }

    /* Renaming a file to itself is a no-op */
    ret = mr_fs_rename("/self", "/self");
    MR_TEST_EXPECT_EQ_INT(ret, 0);

    vfs_umount();
}
MR_TEST_EXPORT(vfs, rename_to_itself, test_vfs_rename_to_itself);

static void test_vfs_rename_into_subtree(void) {
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_fs_mkdir("/p");
    mr_fs_mkdir("/p/c");

    /* Renaming /p into /p/c must fail */
    ret = mr_fs_rename("/p", "/p/c/x");
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EINVAL);

    vfs_umount();
}
MR_TEST_EXPORT(vfs, rename_into_subtree, test_vfs_rename_into_subtree);

static void test_vfs_rename_cross_dir(void) {
    mr_stat_t st;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_fs_mkdir("/d1");
    mr_fs_mkdir("/d2");
    {
        mr_file_t f;
        mr_memset(&f, 0, sizeof(f));
        mr_fs_open(&f, "/d1/f", MR_O_RDWR | MR_O_CREAT);
        mr_fs_close(&f);
    }

    ret = mr_fs_rename("/d1/f", "/d2/f");
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        ret = mr_fs_stat("/d1/f", &st);
        MR_TEST_EXPECT_EQ_INT(ret, -MR_ENOENT);

        ret = mr_fs_stat("/d2/f", &st);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
    }

    vfs_umount();
}
MR_TEST_EXPORT(vfs, rename_cross_dir, test_vfs_rename_cross_dir);

/* Tests: truncate */

static void test_vfs_truncate_shrink(void) {
    mr_file_t f;
    mr_stat_t st;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/ts", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }
    mr_fs_write(&f, "1234567890", 10);

    ret = mr_fs_truncate(&f, 3);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    mr_fs_close(&f);

    if (ret == 0) {
        ret = mr_fs_stat("/ts", &st);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
        if (ret == 0) {
            MR_TEST_EXPECT_EQ_INT(st.size, 3);
        }
    }

    vfs_umount();
}
MR_TEST_EXPORT(vfs, truncate_shrink, test_vfs_truncate_shrink);

static void test_vfs_truncate_grow(void) {
    mr_file_t f;
    mr_stat_t st;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/tg", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }
    mr_fs_write(&f, "ab", 2);

    ret = mr_fs_truncate(&f, 10);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    mr_fs_close(&f);

    if (ret == 0) {
        ret = mr_fs_stat("/tg", &st);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
        if (ret == 0) {
            MR_TEST_EXPECT_EQ_INT(st.size, 10);
        }
    }

    vfs_umount();
}
MR_TEST_EXPORT(vfs, truncate_grow, test_vfs_truncate_grow);

static void test_vfs_truncate_read_only(void) {
    mr_file_t f;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/tr2", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }
    mr_fs_write(&f, "data", 4);
    mr_fs_close(&f);

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/tr2", MR_O_RDONLY);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }

    ret = mr_fs_truncate(&f, 2);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EBADF);

    mr_fs_close(&f);
    vfs_umount();
}
MR_TEST_EXPORT(vfs, truncate_read_only, test_vfs_truncate_read_only);

static void test_vfs_truncate_negative(void) {
    mr_file_t f;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/tn", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }

    ret = mr_fs_truncate(&f, -1);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EINVAL);

    mr_fs_close(&f);
    vfs_umount();
}
MR_TEST_EXPORT(vfs, truncate_negative, test_vfs_truncate_negative);

/* Tests: ioctl / sync (unsupported by testfs) */

static void test_vfs_ioctl_enosys(void) {
    mr_file_t f;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/io", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }

    ret = mr_fs_ioctl(&f, 0, MR_NULL);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_ENOSYS);

    mr_fs_close(&f);
    vfs_umount();
}
MR_TEST_EXPORT(vfs, ioctl_enosys, test_vfs_ioctl_enosys);

static void test_vfs_sync_enosys(void) {
    mr_file_t f;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/sy", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }

    ret = mr_fs_sync(&f);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_ENOSYS);

    mr_fs_close(&f);
    vfs_umount();
}
MR_TEST_EXPORT(vfs, sync_enosys, test_vfs_sync_enosys);

/* Tests: directory open read-only enforcement */

static void test_vfs_open_dir_writable(void) {
    mr_file_t f;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_fs_mkdir("/dw");

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/dw", MR_O_RDWR);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EISDIR);

    vfs_umount();
}
MR_TEST_EXPORT(vfs, open_dir_writable, test_vfs_open_dir_writable);

static void test_vfs_read_dir_eisdir(void) {
    mr_file_t f;
    char buf[8];
    mr_ssize_t nr;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_fs_mkdir("/rd");

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_opendir(&f, "/rd");
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }

    nr = mr_fs_read(&f, buf, sizeof(buf));
    MR_TEST_EXPECT_EQ_INT(nr, -MR_EISDIR);

    mr_fs_closedir(&f);
    vfs_umount();
}
MR_TEST_EXPORT(vfs, read_dir_eisdir, test_vfs_read_dir_eisdir);

/* Tests: open with O_DIRECTORY on a regular file */

static void test_vfs_open_directory_on_file(void) {
    mr_file_t f;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    {
        mr_file_t tmp;
        mr_memset(&tmp, 0, sizeof(tmp));
        mr_fs_open(&tmp, "/regf", MR_O_RDWR | MR_O_CREAT);
        mr_fs_close(&tmp);
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/regf", MR_O_RDONLY | MR_O_DIRECTORY);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_ENOTDIR);

    vfs_umount();
}
MR_TEST_EXPORT(vfs, open_directory_on_file,
               test_vfs_open_directory_on_file);

/* Tests: open with O_CREAT|O_DIRECTORY (should fail) */

static void test_vfs_open_creat_directory(void) {
    mr_file_t f;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/cd", MR_O_CREAT | MR_O_DIRECTORY);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_ENOTDIR);

    vfs_umount();
}
MR_TEST_EXPORT(vfs, open_creat_directory, test_vfs_open_creat_directory);

/* Tests: large write beyond initial capacity */

static void test_vfs_write_large(void) {
    mr_file_t f;
    char buf[128];
    mr_ssize_t nw, nr;
    mr_stat_t st;
    mr_err_t ret;
    int i;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/lg", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }

    for (i = 0; i < (int)sizeof(buf); i++) {
        buf[i] = (char)i;
    }
    nw = mr_fs_write(&f, buf, sizeof(buf));
    MR_TEST_EXPECT_EQ_INT(nw, sizeof(buf));
    mr_fs_close(&f);

    ret = mr_fs_stat("/lg", &st);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        MR_TEST_EXPECT_EQ_INT(st.size, sizeof(buf));
    }

    /* Read back and verify */
    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/lg", MR_O_RDONLY);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret == 0) {
        char rbuf[128];
        mr_memset(rbuf, 0, sizeof(rbuf));
        nr = mr_fs_read(&f, rbuf, sizeof(rbuf));
        MR_TEST_EXPECT_EQ_INT(nr, sizeof(buf));
        MR_TEST_EXPECT_EQ_INT(mr_memcmp(rbuf, buf, sizeof(buf)), 0);
        mr_fs_close(&f);
    }

    vfs_umount();
}
MR_TEST_EXPORT(vfs, write_large, test_vfs_write_large);

/* Tests: open in nested directory after mkdir */

static void test_vfs_open_in_subdir(void) {
    mr_file_t f;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_fs_mkdir("/sd");
    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/sd/f", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        mr_fs_close(&f);
    }

    vfs_umount();
}
MR_TEST_EXPORT(vfs, open_in_subdir, test_vfs_open_in_subdir);

/* Tests: partial read at EOF */

static void test_vfs_read_partial_eof(void) {
    mr_file_t f;
    char buf[16];
    mr_ssize_t nr;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/pe", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }
    mr_fs_write(&f, "abc", 3);
    mr_fs_seek(&f, 0, MR_SEEK_SET);

    mr_memset(buf, 0, sizeof(buf));
    nr = mr_fs_read(&f, buf, 16);
    MR_TEST_EXPECT_EQ_INT(nr, 3);

    /* Second read at EOF returns 0 */
    nr = mr_fs_read(&f, buf, 16);
    MR_TEST_EXPECT_EQ_INT(nr, 0);

    mr_fs_close(&f);
    vfs_umount();
}
MR_TEST_EXPORT(vfs, read_partial_eof, test_vfs_read_partial_eof);

/* Tests: stat on root */

static void test_vfs_stat_root(void) {
    mr_stat_t st;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_fs_stat("/", &st);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        MR_TEST_EXPECT_EQ_INT(st.type, MR_INODE_DIR);
    }

    vfs_umount();
}
MR_TEST_EXPORT(vfs, stat_root, test_vfs_stat_root);

/* Tests: open in missing parent dir */

static void test_vfs_open_missing_parent(void) {
    mr_file_t f;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/no/such/file", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_ENOENT);

    vfs_umount();
}
MR_TEST_EXPORT(vfs, open_missing_parent, test_vfs_open_missing_parent);

static void test_vfs_unlink_root(void) {
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    /* Root can not be unlinked */
    ret = mr_fs_unlink("/");
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EBUSY);

    vfs_umount();
}
MR_TEST_EXPORT(vfs, unlink_root, test_vfs_unlink_root);

static void test_vfs_unlink_open_file(void) {
    mr_file_t f;
    char buf[8];
    mr_ssize_t nr;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/of", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }
    mr_fs_write(&f, "data", 4);
    mr_fs_seek(&f, 0, MR_SEEK_SET);

    /* Unlink while the file is open */
    ret = mr_fs_unlink("/of");
    MR_TEST_EXPECT_EQ_INT(ret, 0);

    /* The open file should still be readable */
    mr_memset(buf, 0, sizeof(buf));
    nr = mr_fs_read(&f, buf, 4);
    MR_TEST_EXPECT_EQ_INT(nr, 4);
    MR_TEST_EXPECT_EQ_INT(mr_strncmp(buf, "data", 4), 0);

    mr_fs_close(&f);
    vfs_umount();
}
MR_TEST_EXPORT(vfs, unlink_open_file, test_vfs_unlink_open_file);

static void test_vfs_rename_dir_with_children(void) {
    mr_stat_t st;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_fs_mkdir("/old");
    {
        mr_file_t f;
        mr_memset(&f, 0, sizeof(f));
        mr_fs_open(&f, "/old/child", MR_O_RDWR | MR_O_CREAT);
        mr_fs_close(&f);
    }

    ret = mr_fs_rename("/old", "/new");
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        /* The child should move with the directory */
        ret = mr_fs_stat("/new/child", &st);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
    }

    vfs_umount();
}
MR_TEST_EXPORT(vfs, rename_dir_with_children,
               test_vfs_rename_dir_with_children);

static void test_vfs_close_returns_fops_error(void) {
    mr_file_t f;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/ce", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }

    /* Close should succeed (testfs close returns 0) */
    ret = mr_fs_close(&f);
    MR_TEST_EXPECT_EQ_INT(ret, 0);

    /* Double close fails */
    ret = mr_fs_close(&f);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EBADF);

    vfs_umount();
}
MR_TEST_EXPORT(vfs, close_returns_fops_error, test_vfs_close_returns_fops_error);

static void test_vfs_truncate_not_open(void) {
    mr_file_t f;
    mr_err_t ret;

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_truncate(&f, 0);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EBADF);
}
MR_TEST_EXPORT(vfs, truncate_not_open, test_vfs_truncate_not_open);

static void test_vfs_ioctl_not_open(void) {
    mr_file_t f;
    mr_err_t ret;

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_ioctl(&f, 0, MR_NULL);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EBADF);
}
MR_TEST_EXPORT(vfs, ioctl_not_open, test_vfs_ioctl_not_open);

static void test_vfs_sync_not_open(void) {
    mr_file_t f;
    mr_err_t ret;

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_sync(&f);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EBADF);
}
MR_TEST_EXPORT(vfs, sync_not_open, test_vfs_sync_not_open);

static void test_vfs_overwrite_middle(void) {
    mr_file_t f;
    char buf[16];
    mr_ssize_t nw, nr;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/ow", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }
    mr_fs_write(&f, "AAAAA", 5);
    mr_fs_seek(&f, 1, MR_SEEK_SET);
    nw = mr_fs_write(&f, "bb", 2);
    MR_TEST_EXPECT_EQ_INT(nw, 2);
    mr_fs_close(&f);

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/ow", MR_O_RDONLY);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }
    mr_memset(buf, 0, sizeof(buf));
    nr = mr_fs_read(&f, buf, sizeof(buf));
    MR_TEST_EXPECT_EQ_INT(nr, 5);
    MR_TEST_EXPECT_EQ_INT(mr_strncmp(buf, "AbbAA", 5), 0);
    mr_fs_close(&f);

    vfs_umount();
}
MR_TEST_EXPORT(vfs, overwrite_middle, test_vfs_overwrite_middle);

static void test_vfs_seek_past_eof(void) {
    mr_file_t f;
    char buf[8];
    mr_ssize_t nr;
    mr_int64_t pos;
    mr_err_t ret;

    ret = vfs_mount();
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_memset(&f, 0, sizeof(f));
    ret = mr_fs_open(&f, "/pe2", MR_O_RDWR | MR_O_CREAT);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        vfs_umount();
        return;
    }
    mr_fs_write(&f, "abc", 3);

    /* Seek past EOF */
    pos = mr_fs_seek(&f, 100, MR_SEEK_SET);
    MR_TEST_EXPECT_EQ_INT(pos, 100);

    /* Read at that position returns 0 bytes */
    mr_memset(buf, 0, sizeof(buf));
    nr = mr_fs_read(&f, buf, sizeof(buf));
    MR_TEST_EXPECT_EQ_INT(nr, 0);

    mr_fs_close(&f);
    vfs_umount();
}
MR_TEST_EXPORT(vfs, seek_past_eof, test_vfs_seek_past_eof);
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
