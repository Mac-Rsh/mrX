/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-13        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS)                              \
    && defined(MR_USE_TEST_PTHREAD)
#include <mrX.h>
#include <pthread.h>
#include "fs_test.h"

/* Concurrent namespace test limits */
#define FS_CONCURRENT_THREADS          (4)
#define FS_CONCURRENT_FILES            (16)
#define FS_CONCURRENT_SWITCHES         (128)

/* Concurrent worker context */
typedef struct fs_concurrent_context {
    int index;
    int failures;
} fs_concurrent_context_t;

static int fs_concurrent_expect(const char *name, mr_err_t actual,
                                mr_err_t expected) {
    MR_IGNORE(name);

    return (actual == expected) ? 0 : 1;
}

static void *fs_concurrent_worker(void *arg) {
    fs_concurrent_context_t *context = arg;
    mr_file_t file = MR_FILE_INIT(file);
    char cwd[32];
    char path[32];
    int index;
    mr_err_t ret;

    /* Create and reopen unique namespace entries */
    for (index = 0; index < FS_CONCURRENT_FILES; index++) {
        mr_snprintf(path, sizeof(path), "/concurrent/t%d-%d", context->index,
                    index);
        ret = mr_fs_open(&file, path, MR_O_RDWR | MR_O_CREAT | MR_O_EXCL);
        context->failures += fs_concurrent_expect("concurrent create", ret, 0);
        if (ret != 0) {
            continue;
        }
        context->failures
            += fs_concurrent_expect("concurrent close", mr_fs_close(&file), 0);

        ret = mr_fs_open(&file, path, MR_O_RDONLY);
        context->failures += fs_concurrent_expect("concurrent reopen", ret, 0);
        if (ret == 0) {
            context->failures += fs_concurrent_expect("concurrent reclose",
                                                      mr_fs_close(&file), 0);
        }
    }

    /* Race namespace context switches with getcwd snapshots */
    for (index = 0; index < FS_CONCURRENT_SWITCHES; index++) {
        ret = mr_fs_chdir((index & 1) ? "/concurrent/a" : "/concurrent/b");
        context->failures += fs_concurrent_expect("concurrent chdir", ret, 0);
        ret = (mr_err_t)mr_fs_getcwd(cwd, sizeof(cwd));
        context->failures
            += fs_concurrent_expect("concurrent getcwd", ret, 13);
        if ((ret == 13) && (mr_strcmp(cwd, "/concurrent/a") != 0)
            && (mr_strcmp(cwd, "/concurrent/b") != 0)) {
            context->failures++;
        }
    }
    return MR_NULL;
}

static int fs_concurrency_test(void) {
    fs_concurrent_context_t contexts[FS_CONCURRENT_THREADS];
    pthread_t threads[FS_CONCURRENT_THREADS];
    mr_stat_t stat;
    char path[32];
    int failures = 0;
    int index, file;
    mr_err_t ret;

    /* Mount RamFS */
    ret = mr_fs_mount(MR_NULL, "/", "ramfs", 0, MR_NULL);
    failures += fs_concurrent_expect("concurrent mount ramfs", ret, 0);
    if (ret != 0) {
        return failures;
    }
    failures += fs_concurrent_expect("mkdir concurrent",
                                     mr_fs_mkdir("/concurrent"), 0);
    failures += fs_concurrent_expect("mkdir concurrent a",
                                     mr_fs_mkdir("/concurrent/a"), 0);
    failures += fs_concurrent_expect("mkdir concurrent b",
                                     mr_fs_mkdir("/concurrent/b"), 0);

    /* Start workers */
    for (index = 0; index < FS_CONCURRENT_THREADS; index++) {
        contexts[index].index = index;
        contexts[index].failures = 0;
        ret = pthread_create(&threads[index], MR_NULL, fs_concurrent_worker,
                             &contexts[index]);
        failures += fs_concurrent_expect("create concurrent thread", ret, 0);
        if (ret != 0) {
            threads[index] = (pthread_t)0;
        }
    }

    /* Join workers */
    for (index = 0; index < FS_CONCURRENT_THREADS; index++) {
        if (threads[index]) {
            ret = pthread_join(threads[index], MR_NULL);
            failures += fs_concurrent_expect("join concurrent thread", ret, 0);
        }
        failures += contexts[index].failures;
    }

    /* Verify every namespace entry */
    for (index = 0; index < FS_CONCURRENT_THREADS; index++) {
        for (file = 0; file < FS_CONCURRENT_FILES; file++) {
            mr_snprintf(path, sizeof(path), "/concurrent/t%d-%d", index, file);
            failures += fs_concurrent_expect("verify concurrent file",
                                             fs_test_stat(path, &stat), 0);
        }
    }

    /* Unmount RamFS */
    failures += fs_concurrent_expect("restore concurrent cwd",
                                     mr_fs_chdir("/"), 0);
    failures += fs_concurrent_expect("concurrent umount ramfs",
                                     mr_fs_umount("/"), 0);
    return failures;
}

static void concurrency(void) {
    MR_TEST_EXPECT_EQ_INT(fs_concurrency_test(), 0);
}
MR_TEST_EXPORT(fs, concurrency, concurrency);
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS)
        * && defined(MR_USE_TEST_PTHREAD) */
