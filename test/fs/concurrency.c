/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-13        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS) && defined(MR_USE_TEST_PTHREAD)
#include <mrX.h>
#include <pthread.h>
#include "fs_test.h"

/* Concurrent namespace test limits */
#define FS_CONCURRENT_THREADS  (4)
#define FS_CONCURRENT_FILES    (16)
#define FS_CONCURRENT_SWITCHES (128)

/* Concurrent worker context */
typedef struct fs_concurrent_context {
    volatile mr_uint32_t *__test_status;
    int index;
} fs_concurrent_context_t;

static void fs_concurrent_worker_run(volatile mr_uint32_t *__test_status,
                                     int index) {
    mr_file_t file = MR_FILE_INIT(file);
    char cwd[32];
    char path[32];
    int i;
    mr_err_t ret;

    /* Create and reopen unique namespace entries */
    for (i = 0; i < FS_CONCURRENT_FILES; i++) {
        mr_snprintf(path, sizeof(path), "/concurrent/t%d-%d", index, i);
        ret = mr_fs_open(&file, path, MR_O_RDWR | MR_O_CREAT | MR_O_EXCL);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
        if (ret != 0) {
            continue;
        }
        MR_TEST_EXPECT_EQ_INT(mr_fs_close(&file), 0);

        ret = mr_fs_open(&file, path, MR_O_RDONLY);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
        if (ret == 0) {
            MR_TEST_EXPECT_EQ_INT(mr_fs_close(&file), 0);
        }
    }

    /* Race namespace context switches with getcwd snapshots */
    for (i = 0; i < FS_CONCURRENT_SWITCHES; i++) {
        ret = mr_fs_chdir((i & 1) ? "/concurrent/a" : "/concurrent/b");
        MR_TEST_EXPECT_EQ_INT(ret, 0);
        ret = (mr_err_t)mr_fs_getcwd(cwd, sizeof(cwd));
        MR_TEST_EXPECT_EQ_INT(ret, 13);
        if (ret == 13) {
            MR_TEST_EXPECT_EQ_INT((mr_strcmp(cwd, "/concurrent/a") == 0)
                                      || (mr_strcmp(cwd, "/concurrent/b") == 0),
                                  MR_TRUE);
        }
    }
}

static void *fs_concurrent_worker(void *arg) {
    fs_concurrent_context_t *context = arg;

    fs_concurrent_worker_run(context->__test_status, context->index);
    return MR_NULL;
}

MR_TEST(fs, concurrency) {
    fs_concurrent_context_t contexts[FS_CONCURRENT_THREADS];
    pthread_t threads[FS_CONCURRENT_THREADS];
    mr_stat_t stat;
    char path[32];
    int index, file;
    mr_err_t ret;

    /* Mount RamFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_fs_mkdir("/concurrent"), 0);
    MR_TEST_ASSERT_EQ_INT(mr_fs_mkdir("/concurrent/a"), 0);
    MR_TEST_ASSERT_EQ_INT(mr_fs_mkdir("/concurrent/b"), 0);

    /* Start workers */
    for (index = 0; index < FS_CONCURRENT_THREADS; index++) {
        contexts[index].__test_status = __test_status;
        contexts[index].index = index;
        ret = pthread_create(&threads[index], MR_NULL, fs_concurrent_worker,
                             &contexts[index]);
        MR_TEST_ASSERT_EQ_INT(ret, 0);
    }

    /* Join workers */
    for (index = 0; index < FS_CONCURRENT_THREADS; index++) {
        MR_TEST_EXPECT_EQ_INT(pthread_join(threads[index], MR_NULL), 0);
    }

    /* Verify every namespace entry */
    for (index = 0; index < FS_CONCURRENT_THREADS; index++) {
        for (file = 0; file < FS_CONCURRENT_FILES; file++) {
            mr_snprintf(path, sizeof(path), "/concurrent/t%d-%d", index, file);
            MR_TEST_EXPECT_EQ_INT(fs_test_stat(path, &stat), 0);
        }
    }

    /* Unmount RamFS */
    MR_TEST_EXPECT_EQ_INT(mr_fs_chdir("/"), 0);
    MR_TEST_EXPECT_EQ_INT(mr_fs_umount("/"), 0);
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS)                           \
        * && defined(MR_USE_TEST_PTHREAD) */
