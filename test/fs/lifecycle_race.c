/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-14        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS) && defined(MR_USE_TEST_PTHREAD)
#include <mrX.h>
#include <mrX/fs/mr_sb.h>
#include <pthread.h>
#include "fs_test.h"

/* Concurrent lifecycle test limits */
#define FS_RACE_THREADS (4)
#define FS_RACE_ROUNDS  (12)

/* Concurrent worker start gate */
typedef struct fs_race_gate {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int ready;
    mr_bool_t open;
} fs_race_gate_t;

/* Concurrent worker context */
typedef struct fs_race_context {
    volatile mr_uint32_t *__test_status;
    fs_race_gate_t *gate;
    int index;
    int create_ret;
    int link_ret;
} fs_race_context_t;

/* Same-node namespace race operation */
typedef enum fs_namespace_race_op {
    FS_NAMESPACE_RACE_UNLINK,
    FS_NAMESPACE_RACE_RENAME,
} fs_namespace_race_op_t;

/* Same-node namespace race context */
typedef struct fs_namespace_race_context {
    volatile mr_uint32_t *__test_status;
    fs_race_gate_t *gate;
    mr_file_t *file;
    const char *path;
    fs_namespace_race_op_t op;
    mr_err_t ret;
} fs_namespace_race_context_t;

static int fs_race_gate_init(fs_race_gate_t *gate) {
    int ret;

    /* Init mutex */
    ret = pthread_mutex_init(&gate->mutex, MR_NULL);
    if (ret != 0) {
        return ret;
    }

    /* Init condition */
    ret = pthread_cond_init(&gate->cond, MR_NULL);
    if (ret != 0) {
        pthread_mutex_destroy(&gate->mutex);
        return ret;
    }
    gate->ready = 0;
    gate->open = MR_FALSE;
    return 0;
}

static void fs_race_gate_destroy(fs_race_gate_t *gate) {
    /* Destroy condition */
    pthread_cond_destroy(&gate->cond);

    /* Destroy mutex */
    pthread_mutex_destroy(&gate->mutex);
}

static int fs_race_gate_wait(fs_race_gate_t *gate) {
    int ret;

    /* Lock gate */
    ret = pthread_mutex_lock(&gate->mutex);
    if (ret != 0) {
        return ret;
    }

    /* Announce readiness */
    gate->ready++;
    ret = pthread_cond_broadcast(&gate->cond);
    if (ret != 0) {
        pthread_mutex_unlock(&gate->mutex);
        return ret;
    }

    /* Wait for every worker */
    while (gate->open == MR_FALSE) {
        ret = pthread_cond_wait(&gate->cond, &gate->mutex);
        if (ret != 0) {
            pthread_mutex_unlock(&gate->mutex);
            return ret;
        }
    }

    /* Unlock gate */
    return pthread_mutex_unlock(&gate->mutex);
}

static int fs_race_gate_open(fs_race_gate_t *gate, int workers) {
    int ret;

    /* Lock gate */
    ret = pthread_mutex_lock(&gate->mutex);
    if (ret != 0) {
        return ret;
    }

    /* Wait for every worker to arrive */
    while (gate->ready < workers) {
        ret = pthread_cond_wait(&gate->cond, &gate->mutex);
        if (ret != 0) {
            pthread_mutex_unlock(&gate->mutex);
            return ret;
        }
    }

    /* Release workers */
    gate->open = MR_TRUE;
    ret = pthread_cond_broadcast(&gate->cond);
    if (ret != 0) {
        pthread_mutex_unlock(&gate->mutex);
        return ret;
    }

    /* Unlock gate */
    return pthread_mutex_unlock(&gate->mutex);
}

static void fs_race_worker_run(fs_race_context_t *context) {
    volatile mr_uint32_t *__test_status = context->__test_status;
    mr_file_t file = MR_FILE_INIT(file);
    mr_file_t link = MR_FILE_INIT(link);
    mr_stat_t stat;
    char path[48];
    char renamed[48];
    char linked[48];
    char cwd[16];
    int index;
    mr_err_t ret;

    /* Wait until all workers can contend */
    MR_TEST_ASSERT_EQ_INT(fs_race_gate_wait(context->gate), 0);

    /* Race exclusive creation of the same path */
    context->create_ret
        = mr_fs_open(&file, "/race/once", MR_O_RDWR | MR_O_CREAT | MR_O_EXCL);
    if (context->create_ret == 0) {
        MR_TEST_EXPECT_EQ_INT(mr_fs_close(&file), 0);
    }

    /* Race hard-link creation of the same path */
    ret = mr_fs_open(&file, "/race/source", MR_O_RDONLY);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        context->link_ret = mr_fs_link(&file, "/race/once-link");
        MR_TEST_EXPECT_EQ_INT(mr_fs_close(&file), 0);
    }

    /* Exercise independent object lifecycles concurrently */
    for (index = 0; index < FS_RACE_ROUNDS; index++) {
        mr_snprintf(path, sizeof(path), "/race/w%d-%d", context->index, index);
        mr_snprintf(linked, sizeof(linked), "/race/l%d-%d", context->index,
                    index);
        mr_snprintf(renamed, sizeof(renamed), "/race/r%d-%d", context->index,
                    index);

        ret = mr_fs_open(&file, path, MR_O_RDWR | MR_O_CREAT | MR_O_EXCL);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
        if (ret != 0) {
            continue;
        }
        MR_TEST_EXPECT_EQ_INT(mr_fs_link(&file, linked), 0);
        MR_TEST_EXPECT_EQ_INT(mr_fs_rename(&file, renamed), 0);
        MR_TEST_EXPECT_EQ_INT(mr_fs_unlink(&file), 0);
        MR_TEST_EXPECT_EQ_INT(mr_fs_stat(&file, &stat), 0);
        MR_TEST_EXPECT_EQ_UINT(stat.nlink, 1U);
        MR_TEST_EXPECT_EQ_INT(mr_fs_open(&link, linked, MR_O_RDONLY), 0);
        if (link.inode) {
            MR_TEST_EXPECT_EQ_INT(mr_fs_unlink(&link), 0);
            MR_TEST_EXPECT_EQ_INT(mr_fs_close(&link), 0);
        }
        MR_TEST_EXPECT_EQ_INT(mr_fs_close(&file), 0);
        MR_TEST_EXPECT_EQ_INT(fs_test_stat(renamed, &stat), -MR_ENOENT);
        MR_TEST_EXPECT_EQ_INT(fs_test_stat(linked, &stat), -MR_ENOENT);
    }

    /* Race namespace context updates with atomic CWD snapshots */
    for (index = 0; index < FS_RACE_ROUNDS; index++) {
        ret = mr_fs_chdir((index & 1) ? "/race/a" : "/race/b");
        MR_TEST_EXPECT_EQ_INT(ret, 0);
        ret = (mr_err_t)mr_fs_getcwd(cwd, sizeof(cwd));
        MR_TEST_EXPECT_EQ_INT(ret, 7);
        if (ret == 7) {
            MR_TEST_EXPECT_EQ_INT((mr_strcmp(cwd, "/race/a") == 0)
                                      || (mr_strcmp(cwd, "/race/b") == 0),
                                  MR_TRUE);
        }
    }
}

static void *fs_race_worker(void *arg) {
    fs_race_worker_run(arg);
    return MR_NULL;
}

static void fs_namespace_race_worker_run(fs_namespace_race_context_t *context) {
    volatile mr_uint32_t *__test_status = context->__test_status;

    /* Wait until both namespace operations can contend */
    MR_TEST_ASSERT_EQ_INT(fs_race_gate_wait(context->gate), 0);

    /* Run the selected namespace operation */
    if (context->op == FS_NAMESPACE_RACE_UNLINK) {
        context->ret = mr_fs_unlink(context->file);
    } else {
        context->ret = mr_fs_rename(context->file, context->path);
    }
}

static void *fs_namespace_race_worker(void *arg) {
    fs_namespace_race_worker_run(arg);
    return MR_NULL;
}

static void fs_same_node_race_test(volatile mr_uint32_t *__test_status) {
    fs_namespace_race_context_t contexts[2];
    fs_race_gate_t gate;
    mr_file_t files[2] = {MR_FILE_INIT(files[0]), MR_FILE_INIT(files[1])};
    pthread_t threads[2];
    mr_bool_t started[2] = {MR_FALSE};
    mr_stat_t stat;
    int workers;
    int success;
    int index;
    mr_err_t ret;

    /* Open the same dentry through two independent handles */
    ret = mr_fs_open(&files[0], "/race/same-unlink",
                     MR_O_RDWR | MR_O_CREAT | MR_O_EXCL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_fs_open(&files[1], "/race/same-unlink", MR_O_RDWR),
                          0);

    /* Race two unlink requests against one dentry */
    MR_TEST_ASSERT_EQ_INT(fs_race_gate_init(&gate), 0);
    for (index = 0, workers = 0; index < 2; index++) {
        contexts[index].__test_status = __test_status;
        contexts[index].gate = &gate;
        contexts[index].file = &files[index];
        contexts[index].path = MR_NULL;
        contexts[index].op = FS_NAMESPACE_RACE_UNLINK;
        contexts[index].ret = -MR_EIO;
        ret = pthread_create(&threads[index], MR_NULL, fs_namespace_race_worker,
                             &contexts[index]);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
        if (ret == 0) {
            started[index] = MR_TRUE;
            workers++;
        }
    }
    MR_TEST_EXPECT_EQ_INT(fs_race_gate_open(&gate, workers), 0);
    for (index = 0, success = 0; index < 2; index++) {
        if (started[index] == MR_TRUE) {
            MR_TEST_EXPECT_EQ_INT(pthread_join(threads[index], MR_NULL), 0);
            if (contexts[index].ret == 0) {
                success++;
            } else {
                MR_TEST_EXPECT_EQ_INT(contexts[index].ret, -MR_ENOENT);
            }
        }
        MR_TEST_EXPECT_EQ_INT(mr_fs_close(&files[index]), 0);
    }
    if (workers == 2) {
        MR_TEST_EXPECT_EQ_INT(success, 1);
    }
    MR_TEST_EXPECT_EQ_INT(mr_fs_stat_by_path("/race/same-unlink", &stat),
                          -MR_ENOENT);
    fs_race_gate_destroy(&gate);

    /* Create two source names for a shared destination race */
    for (index = 0; index < 2; index++) {
        char path[32];

        mr_snprintf(path, sizeof(path), "/race/rename-source-%d", index);
        ret = mr_fs_open(&files[index], path,
                         MR_O_RDWR | MR_O_CREAT | MR_O_EXCL);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
    }
    MR_TEST_ASSERT_EQ_INT(fs_race_gate_init(&gate), 0);
    for (index = 0, workers = 0; index < 2; index++) {
        started[index] = MR_FALSE;
        contexts[index].__test_status = __test_status;
        contexts[index].gate = &gate;
        contexts[index].file = &files[index];
        contexts[index].path = "/race/rename-destination";
        contexts[index].op = FS_NAMESPACE_RACE_RENAME;
        contexts[index].ret = -MR_EIO;
        ret = pthread_create(&threads[index], MR_NULL, fs_namespace_race_worker,
                             &contexts[index]);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
        if (ret == 0) {
            started[index] = MR_TRUE;
            workers++;
        }
    }
    MR_TEST_EXPECT_EQ_INT(fs_race_gate_open(&gate, workers), 0);
    for (index = 0, success = 0; index < 2; index++) {
        if (started[index] == MR_TRUE) {
            MR_TEST_EXPECT_EQ_INT(pthread_join(threads[index], MR_NULL), 0);
            if (contexts[index].ret == 0) {
                success++;
            } else {
                MR_TEST_EXPECT_EQ_INT(contexts[index].ret, -MR_EEXIST);
            }
        }
        MR_TEST_EXPECT_EQ_INT(mr_fs_close(&files[index]), 0);
    }
    if (workers == 2) {
        MR_TEST_EXPECT_EQ_INT(success, 1);
    }
    MR_TEST_EXPECT_EQ_INT(mr_fs_stat_by_path("/race/rename-destination", &stat),
                          0);
    MR_TEST_EXPECT_EQ_INT(mr_fs_unlink_by_path("/race/rename-destination"), 0);
    for (index = 0; index < 2; index++) {
        char path[32];

        mr_snprintf(path, sizeof(path), "/race/rename-source-%d", index);
        if (mr_fs_stat_by_path(path, &stat) == 0) {
            MR_TEST_EXPECT_EQ_INT(mr_fs_unlink_by_path(path), 0);
        }
    }
    fs_race_gate_destroy(&gate);
}

MR_TEST(fs, lifecycle_race) {
    fs_race_gate_t gate;
    fs_race_context_t contexts[FS_RACE_THREADS];
    pthread_t threads[FS_RACE_THREADS];
    mr_stat_t stat;
    mr_bool_t started[FS_RACE_THREADS] = {MR_FALSE};
    int create_success = 0;
    int link_success = 0;
    int workers = 0;
    int index;
    mr_err_t ret;

    /* Mount RamFS */
    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_fs_mkdir("/race"), 0);
    MR_TEST_ASSERT_EQ_INT(mr_fs_mkdir("/race/a"), 0);
    MR_TEST_ASSERT_EQ_INT(mr_fs_mkdir("/race/b"), 0);

    /* Create the shared hard-link source */
    {
        mr_file_t source = MR_FILE_INIT(source);

        ret = mr_fs_open(&source, "/race/source", MR_O_RDWR | MR_O_CREAT);
        MR_TEST_ASSERT_EQ_INT(ret, 0);
        MR_TEST_ASSERT_EQ_INT(mr_fs_close(&source), 0);
    }

    /* Restrict unmount protection to the target super-block */
    {
        mr_file_t mount_a = MR_FILE_INIT(mount_a);
        mr_super_block_t *sb;

        MR_TEST_EXPECT_EQ_INT(mr_fs_mkdir("/race/mount-a"), 0);
        MR_TEST_EXPECT_EQ_INT(mr_fs_mkdir("/race/mount-b"), 0);
        MR_TEST_EXPECT_EQ_INT(
            mr_fs_mount(MR_NULL, "/race/mount-a", "ramfs", 0, MR_NULL), 0);
        MR_TEST_EXPECT_EQ_INT(
            mr_fs_mount(MR_NULL, "/race/mount-b", "ramfs", 0, MR_NULL), 0);
        ret = mr_fs_opendir(&mount_a, "/race/mount-a");
        MR_TEST_EXPECT_EQ_INT(ret, 0);
        if (ret == 0) {
            sb = mount_a.inode->sb;
            ret = mr_super_block_op_enter(sb);
            MR_TEST_EXPECT_EQ_INT(ret, 0);
            if (ret == 0) {
                MR_TEST_EXPECT_EQ_INT(mr_fs_umount("/race/mount-a"), -MR_EBUSY);
                MR_TEST_EXPECT_EQ_INT(mr_fs_umount("/race/mount-b"), 0);
                mr_super_block_op_exit(sb);
            }
            MR_TEST_EXPECT_EQ_INT(mr_fs_umount("/race/mount-a"), 0);
            MR_TEST_EXPECT_EQ_INT(mr_fs_closedir(&mount_a), 0);
        }
    }

    /* Race unlink and rename commits on shared namespace nodes */
    fs_same_node_race_test(__test_status);

    /* Init worker gate */
    ret = fs_race_gate_init(&gate);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    /* Start workers */
    for (index = 0; index < FS_RACE_THREADS; index++) {
        contexts[index].__test_status = __test_status;
        contexts[index].gate = &gate;
        contexts[index].index = index;
        contexts[index].create_ret = -MR_EIO;
        contexts[index].link_ret = -MR_EIO;
        ret = pthread_create(&threads[index], MR_NULL, fs_race_worker,
                             &contexts[index]);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
        if (ret == 0) {
            started[index] = MR_TRUE;
            workers++;
        }
    }

    /* Release workers together */
    if (workers > 0) {
        MR_TEST_EXPECT_EQ_INT(fs_race_gate_open(&gate, workers), 0);
    }

    /* Join workers and inspect competing results */
    for (index = 0; index < FS_RACE_THREADS; index++) {
        if (started[index] == MR_FALSE) {
            continue;
        }
        MR_TEST_EXPECT_EQ_INT(pthread_join(threads[index], MR_NULL), 0);
        if (contexts[index].create_ret == 0) {
            create_success++;
        } else {
            MR_TEST_EXPECT_EQ_INT(contexts[index].create_ret, -MR_EEXIST);
        }
        if (contexts[index].link_ret == 0) {
            link_success++;
        } else {
            MR_TEST_EXPECT_EQ_INT(contexts[index].link_ret, -MR_EEXIST);
        }
    }
    MR_TEST_EXPECT_EQ_INT(create_success, 1);
    MR_TEST_EXPECT_EQ_INT(link_success, 1);
    MR_TEST_EXPECT_EQ_INT(fs_test_stat("/race/once", &stat), 0);
    MR_TEST_EXPECT_EQ_INT(fs_test_stat("/race/once-link", &stat), 0);
    fs_race_gate_destroy(&gate);

    /* Restore namespace and unmount */
    MR_TEST_EXPECT_EQ_INT(mr_fs_chdir("/"), 0);
    MR_TEST_EXPECT_EQ_INT(mr_fs_umount("/"), 0);
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS)                           \
        * && defined(MR_USE_TEST_PTHREAD) */
