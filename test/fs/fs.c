/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS)
#include <mrX/fs/mr_sb.h>
#include <mrX/fs/mr_fs.h>
#include <mrX/libc/mr_string.h>

/* Dummy callbacks for test file-system objects */
static mr_err_t __fs_dummy_mount(mr_super_block_t *sb, const char *source,
                                  int flags, void *data) {
    MR_IGNORE(sb);
    MR_IGNORE(source);
    MR_IGNORE(flags);
    MR_IGNORE(data);
    return -MR_EIO;
}

static void __fs_dummy_kill_sb(mr_super_block_t *sb) {
    MR_IGNORE(sb);
}

/* Tests */

static void test_fs_init_normal(void) {
    mr_file_system_t fs;
    mr_err_t ret;

    mr_memset(&fs, 0, sizeof(fs));
    ret = mr_file_system_init(&fs, "tfs_init", __fs_dummy_mount,
                              __fs_dummy_kill_sb);

    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        MR_TEST_EXPECT_EQ_INT(MR_OBJECT_IS_INITED(&fs), MR_TRUE);
        MR_TEST_EXPECT_EQ_INT(MR_OBJECT_IS_CLAZZES(&fs, file_system), MR_TRUE);
        mr_file_system_del(&fs);
    }
}
MR_TEST_EXPORT(fs, init_normal, test_fs_init_normal);

static void test_fs_init_name_set(void) {
    mr_file_system_t fs;
    mr_err_t ret;

    mr_memset(&fs, 0, sizeof(fs));
    ret = mr_file_system_init(&fs, "tfs_name", __fs_dummy_mount,
                              __fs_dummy_kill_sb);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        MR_TEST_EXPECT_EQ_INT(mr_strcmp(fs.name, "tfs_name"), 0);
        MR_TEST_EXPECT_EQ_PTR(fs.mount, __fs_dummy_mount);
        MR_TEST_EXPECT_EQ_PTR(fs.kill_sb, __fs_dummy_kill_sb);
        mr_file_system_del(&fs);
    }
}
MR_TEST_EXPORT(fs, init_name_set, test_fs_init_name_set);

static void test_fs_del_unregistered(void) {
    mr_file_system_t fs;
    mr_err_t ret;

    mr_memset(&fs, 0, sizeof(fs));
    ret = mr_file_system_init(&fs, "tfs_del", __fs_dummy_mount,
                              __fs_dummy_kill_sb);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    /* Deleting an unregistered FS should succeed */
    ret = mr_file_system_del(&fs);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
}
MR_TEST_EXPORT(fs, del_unregistered, test_fs_del_unregistered);

static void test_fs_del_registered_busy(void) {
    mr_file_system_t fs;
    mr_err_t ret;

    mr_memset(&fs, 0, sizeof(fs));
    ret = mr_file_system_init(&fs, "tfs_busy", __fs_dummy_mount,
                              __fs_dummy_kill_sb);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_file_system_register(&fs);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        mr_file_system_del(&fs);
        return;
    }

    /* Deleting a registered FS must fail with -MR_EBUSY */
    ret = mr_file_system_del(&fs);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EBUSY);

    /* Clean up */
    mr_file_system_unregister(&fs);
    mr_file_system_del(&fs);
}
MR_TEST_EXPORT(fs, del_registered_busy, test_fs_del_registered_busy);

static void test_fs_register_find(void) {
    mr_file_system_t fs;
    mr_file_system_t *found;
    mr_err_t ret;

    mr_memset(&fs, 0, sizeof(fs));
    ret = mr_file_system_init(&fs, "tfs_rnf", __fs_dummy_mount,
                              __fs_dummy_kill_sb);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_file_system_register(&fs);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        mr_file_system_del(&fs);
        return;
    }

    /* Find it by name */
    found = mr_file_system_find("tfs_rnf");
    MR_TEST_EXPECT_NE_PTR(found, MR_NULL);
    if (found) {
        MR_TEST_EXPECT_EQ_PTR(found, &fs);
        mr_file_system_put(found);
    }

    /* Unregister */
    ret = mr_file_system_unregister(&fs);
    MR_TEST_EXPECT_EQ_INT(ret, 0);

    /* After unregister, find must fail */
    found = mr_file_system_find("tfs_rnf");
    MR_TEST_EXPECT_EQ_PTR(found, MR_NULL);

    mr_file_system_del(&fs);
}
MR_TEST_EXPORT(fs, register_find, test_fs_register_find);

static void test_fs_register_duplicate(void) {
    mr_file_system_t fs1, fs2;
    mr_err_t ret;

    mr_memset(&fs1, 0, sizeof(fs1));
    mr_memset(&fs2, 0, sizeof(fs2));
    ret = mr_file_system_init(&fs1, "tfs_dup", __fs_dummy_mount,
                              __fs_dummy_kill_sb);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }
    ret = mr_file_system_init(&fs2, "tfs_dup", __fs_dummy_mount,
                              __fs_dummy_kill_sb);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        mr_file_system_del(&fs1);
        return;
    }

    ret = mr_file_system_register(&fs1);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        mr_file_system_del(&fs1);
        mr_file_system_del(&fs2);
        return;
    }

    /* Registering the same name again must fail */
    ret = mr_file_system_register(&fs2);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EEXIST);

    mr_file_system_unregister(&fs1);
    mr_file_system_del(&fs1);
    mr_file_system_del(&fs2);
}
MR_TEST_EXPORT(fs, register_duplicate, test_fs_register_duplicate);

static void test_fs_unregister_not_registered(void) {
    mr_file_system_t fs;
    mr_err_t ret;

    mr_memset(&fs, 0, sizeof(fs));
    ret = mr_file_system_init(&fs, "tfs_unr", __fs_dummy_mount,
                              __fs_dummy_kill_sb);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    /* Unregistering a never-registered FS must fail */
    ret = mr_file_system_unregister(&fs);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_ENOENT);

    mr_file_system_del(&fs);
}
MR_TEST_EXPORT(fs, unregister_not_registered, test_fs_unregister_not_registered);

static void test_fs_find_nonexistent(void) {
    mr_file_system_t *found;

    found = mr_file_system_find("tfs_no_such_fs");
    MR_TEST_EXPECT_EQ_PTR(found, MR_NULL);
}
MR_TEST_EXPORT(fs, find_nonexistent, test_fs_find_nonexistent);

static void test_fs_find_testfs(void) {
    mr_file_system_t *found;

    /* The mock testfs should be auto-registered at boot */
    found = mr_file_system_find("testfs");
    MR_TEST_EXPECT_NE_PTR(found, MR_NULL);
    if (found) {
        mr_file_system_put(found);
    }
}
MR_TEST_EXPORT(fs, find_testfs, test_fs_find_testfs);

static void test_fs_register_unregister_cycle(void) {
    mr_file_system_t fs;
    mr_file_system_t *found;
    mr_err_t ret;

    mr_memset(&fs, 0, sizeof(fs));
    ret = mr_file_system_init(&fs, "tfs_cycle", __fs_dummy_mount,
                              __fs_dummy_kill_sb);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    /* Register -> unregister -> register again */
    ret = mr_file_system_register(&fs);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_file_system_unregister(&fs);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    ret = mr_file_system_register(&fs);
    MR_TEST_EXPECT_EQ_INT(ret, 0);

    found = mr_file_system_find("tfs_cycle");
    MR_TEST_EXPECT_NE_PTR(found, MR_NULL);
    if (found) {
        mr_file_system_put(found);
    }

    mr_file_system_unregister(&fs);
    mr_file_system_del(&fs);
}
MR_TEST_EXPORT(fs, register_unregister_cycle,
               test_fs_register_unregister_cycle);

static void test_fs_get_put_refcount(void) {
    mr_file_system_t fs;
    mr_file_system_t *ret_fs;
    mr_err_t ret;

    mr_memset(&fs, 0, sizeof(fs));
    ret = mr_file_system_init(&fs, "tfs_ref", __fs_dummy_mount,
                              __fs_dummy_kill_sb);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    /* Get twice, put twice, then delete */
    ret_fs = mr_file_system_get(&fs);
    MR_TEST_EXPECT_EQ_PTR(ret_fs, &fs);
    ret_fs = mr_file_system_get(&fs);
    MR_TEST_EXPECT_EQ_PTR(ret_fs, &fs);

    mr_file_system_put(&fs);
    mr_file_system_put(&fs);

    mr_file_system_del(&fs);
}
MR_TEST_EXPORT(fs, get_put_refcount, test_fs_get_put_refcount);
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
