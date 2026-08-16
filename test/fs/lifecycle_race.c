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
    fs_race_gate_t *gate;
    int index;
    int create_ret;
    int link_ret;
    int failures;
} fs_race_context_t;

/* Same-node namespace race operation */
typedef enum fs_namespace_race_op {
    FS_NAMESPACE_RACE_UNLINK,
    FS_NAMESPACE_RACE_RENAME,
} fs_namespace_race_op_t;

/* Same-node namespace race context */
typedef struct fs_namespace_race_context {
    fs_race_gate_t *gate;
    mr_file_t *file;
    const char *path;
    fs_namespace_race_op_t op;
    mr_err_t ret;
} fs_namespace_race_context_t;

static int fs_race_expect(const char *name, int actual, int expected) {
    MR_IGNORE(name);

    return (actual == expected) ? 0 : 1;
}

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

static void *fs_race_worker(void *arg) {
    fs_race_context_t *context = arg;
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
    if (fs_race_gate_wait(context->gate) != 0) {
        context->failures++;
        return MR_NULL;
    }

    /* Race exclusive creation of the same path */
    context->create_ret
        = mr_fs_open(&file, "/race/once", MR_O_RDWR | MR_O_CREAT | MR_O_EXCL);
    if (context->create_ret == 0) {
        context->failures
            += fs_race_expect("close exclusive file", mr_fs_close(&file), 0);
    }

    /* Race hard-link creation of the same path */
    ret = mr_fs_open(&file, "/race/source", MR_O_RDONLY);
    context->failures += fs_race_expect("open shared source", ret, 0);
    if (ret == 0) {
        context->link_ret = mr_fs_link(&file, "/race/once-link");
        context->failures
            += fs_race_expect("close shared source", mr_fs_close(&file), 0);
    }

    /* Exercise independent object lifecycles concurrently */
    for (index = 0; index < FS_RACE_ROUNDS; index++) {
        mr_snprintf(path, sizeof(path), "/race/w%d-%d", context->index, index);
        mr_snprintf(linked, sizeof(linked), "/race/l%d-%d", context->index,
                    index);
        mr_snprintf(renamed, sizeof(renamed), "/race/r%d-%d", context->index,
                    index);

        ret = mr_fs_open(&file, path, MR_O_RDWR | MR_O_CREAT | MR_O_EXCL);
        context->failures += fs_race_expect("create lifecycle file", ret, 0);
        if (ret != 0) {
            continue;
        }
        context->failures += fs_race_expect("create lifecycle link",
                                            mr_fs_link(&file, linked), 0);
        context->failures += fs_race_expect("rename lifecycle file",
                                            mr_fs_rename(&file, renamed), 0);
        context->failures
            += fs_race_expect("unlink lifecycle file", mr_fs_unlink(&file), 0);
        context->failures += fs_race_expect("stat unlinked file",
                                            mr_fs_stat(&file, &stat), 0);
        if (stat.nlink != 1U) {
            context->failures++;
        }
        context->failures += fs_race_expect(
            "open remaining link", mr_fs_open(&link, linked, MR_O_RDONLY), 0);
        if (link.inode) {
            context->failures += fs_race_expect("unlink remaining link",
                                                mr_fs_unlink(&link), 0);
            context->failures += fs_race_expect("close remaining link",
                                                mr_fs_close(&link), 0);
        }
        context->failures
            += fs_race_expect("close unlinked file", mr_fs_close(&file), 0);
        context->failures
            += fs_race_expect("unlinked path is absent",
                              fs_test_stat(renamed, &stat), -MR_ENOENT);
        context->failures += fs_race_expect(
            "linked path is absent", fs_test_stat(linked, &stat), -MR_ENOENT);
    }

    /* Race namespace context updates with atomic CWD snapshots */
    for (index = 0; index < FS_RACE_ROUNDS; index++) {
        ret = mr_fs_chdir((index & 1) ? "/race/a" : "/race/b");
        context->failures += fs_race_expect("switch race cwd", ret, 0);
        ret = (mr_err_t)mr_fs_getcwd(cwd, sizeof(cwd));
        context->failures += fs_race_expect("snapshot race cwd", ret, 7);
        if ((ret == 7) && (mr_strcmp(cwd, "/race/a") != 0)
            && (mr_strcmp(cwd, "/race/b") != 0)) {
            context->failures++;
        }
    }
    return MR_NULL;
}

static void *fs_namespace_race_worker(void *arg) {
    fs_namespace_race_context_t *context = arg;

    /* Wait until both namespace operations can contend */
    if (fs_race_gate_wait(context->gate) != 0) {
        context->ret = -MR_EIO;
        return MR_NULL;
    }

    /* Run the selected namespace operation */
    if (context->op == FS_NAMESPACE_RACE_UNLINK) {
        context->ret = mr_fs_unlink(context->file);
    } else {
        context->ret = mr_fs_rename(context->file, context->path);
    }
    return MR_NULL;
}

static int fs_same_node_race_test(void) {
    fs_namespace_race_context_t contexts[2];
    fs_race_gate_t gate;
    mr_file_t files[2] = {MR_FILE_INIT(files[0]), MR_FILE_INIT(files[1])};
    pthread_t threads[2];
    mr_bool_t started[2] = {MR_FALSE};
    mr_stat_t stat;
    int workers;
    int success;
    int failures = 0;
    int index;
    mr_err_t ret;

    /* Open the same dentry through two independent handles */
    ret = mr_fs_open(&files[0], "/race/same-unlink",
                     MR_O_RDWR | MR_O_CREAT | MR_O_EXCL);
    failures += fs_race_expect("create same unlink", ret, 0);
    if (ret != 0) {
        return failures;
    }
    failures += fs_race_expect(
        "open same unlink",
        mr_fs_open(&files[1], "/race/same-unlink", MR_O_RDWR), 0);

    /* Race two unlink requests against one dentry */
    failures
        += fs_race_expect("init same unlink gate", fs_race_gate_init(&gate), 0);
    for (index = 0, workers = 0; index < 2; index++) {
        contexts[index].gate = &gate;
        contexts[index].file = &files[index];
        contexts[index].path = MR_NULL;
        contexts[index].op = FS_NAMESPACE_RACE_UNLINK;
        contexts[index].ret = -MR_EIO;
        ret = pthread_create(&threads[index], MR_NULL, fs_namespace_race_worker,
                             &contexts[index]);
        failures += fs_race_expect("create same unlink worker", ret, 0);
        if (ret == 0) {
            started[index] = MR_TRUE;
            workers++;
        }
    }
    failures += fs_race_expect("open same unlink gate",
                               fs_race_gate_open(&gate, workers), 0);
    for (index = 0, success = 0; index < 2; index++) {
        if (started[index] == MR_TRUE) {
            failures
                += fs_race_expect("join same unlink worker",
                                  pthread_join(threads[index], MR_NULL), 0);
            if (contexts[index].ret == 0) {
                success++;
            } else {
                failures += fs_race_expect("same unlink loser",
                                           contexts[index].ret, -MR_ENOENT);
            }
        }
        failures += fs_race_expect("close same unlink handle",
                                   mr_fs_close(&files[index]), 0);
    }
    if (workers == 2) {
        failures += fs_race_expect("one same unlink winner", success, 1);
    }
    failures += fs_race_expect("same unlink path absent",
                               mr_fs_stat_by_path("/race/same-unlink", &stat),
                               -MR_ENOENT);
    fs_race_gate_destroy(&gate);

    /* Create two source names for a shared destination race */
    for (index = 0; index < 2; index++) {
        char path[32];

        mr_snprintf(path, sizeof(path), "/race/rename-source-%d", index);
        ret = mr_fs_open(&files[index], path,
                         MR_O_RDWR | MR_O_CREAT | MR_O_EXCL);
        failures += fs_race_expect("create rename source", ret, 0);
    }
    failures
        += fs_race_expect("init same rename gate", fs_race_gate_init(&gate), 0);
    for (index = 0, workers = 0; index < 2; index++) {
        started[index] = MR_FALSE;
        contexts[index].gate = &gate;
        contexts[index].file = &files[index];
        contexts[index].path = "/race/rename-destination";
        contexts[index].op = FS_NAMESPACE_RACE_RENAME;
        contexts[index].ret = -MR_EIO;
        ret = pthread_create(&threads[index], MR_NULL, fs_namespace_race_worker,
                             &contexts[index]);
        failures += fs_race_expect("create same rename worker", ret, 0);
        if (ret == 0) {
            started[index] = MR_TRUE;
            workers++;
        }
    }
    failures += fs_race_expect("open same rename gate",
                               fs_race_gate_open(&gate, workers), 0);
    for (index = 0, success = 0; index < 2; index++) {
        if (started[index] == MR_TRUE) {
            failures
                += fs_race_expect("join same rename worker",
                                  pthread_join(threads[index], MR_NULL), 0);
            if (contexts[index].ret == 0) {
                success++;
            } else {
                failures += fs_race_expect("same rename loser",
                                           contexts[index].ret, -MR_EEXIST);
            }
        }
        failures += fs_race_expect("close rename source",
                                   mr_fs_close(&files[index]), 0);
    }
    if (workers == 2) {
        failures += fs_race_expect("one same rename winner", success, 1);
    }
    failures += fs_race_expect(
        "rename destination present",
        mr_fs_stat_by_path("/race/rename-destination", &stat), 0);
    failures
        += fs_race_expect("unlink rename destination",
                          mr_fs_unlink_by_path("/race/rename-destination"), 0);
    for (index = 0; index < 2; index++) {
        char path[32];

        mr_snprintf(path, sizeof(path), "/race/rename-source-%d", index);
        if (mr_fs_stat_by_path(path, &stat) == 0) {
            failures += fs_race_expect("unlink rename loser source",
                                       mr_fs_unlink_by_path(path), 0);
        }
    }
    fs_race_gate_destroy(&gate);
    return failures;
}

static int fs_lifecycle_race_test(void) {
    fs_race_gate_t gate;
    fs_race_context_t contexts[FS_RACE_THREADS];
    pthread_t threads[FS_RACE_THREADS];
    mr_stat_t stat;
    mr_bool_t started[FS_RACE_THREADS] = {MR_FALSE};
    int create_success = 0;
    int link_success = 0;
    int failures = 0;
    int workers = 0;
    int index;
    mr_err_t ret;

    /* Mount RamFS */
    ret = mr_fs_mount(MR_NULL, "/", "ramfs", 0, MR_NULL);
    failures += fs_race_expect("mount lifecycle ramfs", ret, 0);
    if (ret != 0) {
        return failures;
    }
    failures += fs_race_expect("mkdir race root", mr_fs_mkdir("/race"), 0);
    failures += fs_race_expect("mkdir race a", mr_fs_mkdir("/race/a"), 0);
    failures += fs_race_expect("mkdir race b", mr_fs_mkdir("/race/b"), 0);

    /* Create the shared hard-link source */
    {
        mr_file_t source = MR_FILE_INIT(source);

        ret = mr_fs_open(&source, "/race/source", MR_O_RDWR | MR_O_CREAT);
        failures += fs_race_expect("create shared source", ret, 0);
        if (ret == 0) {
            failures += fs_race_expect("close shared source",
                                       mr_fs_close(&source), 0);
        }
    }

    /* Restrict unmount protection to the target super-block */
    {
        mr_file_t mount_a = MR_FILE_INIT(mount_a);
        mr_super_block_t *sb;

        failures += fs_race_expect("mkdir mount a",
                                   mr_fs_mkdir("/race/mount-a"), 0);
        failures += fs_race_expect("mkdir mount b",
                                   mr_fs_mkdir("/race/mount-b"), 0);
        failures += fs_race_expect(
            "mount race a",
            mr_fs_mount(MR_NULL, "/race/mount-a", "ramfs", 0, MR_NULL), 0);
        failures += fs_race_expect(
            "mount race b",
            mr_fs_mount(MR_NULL, "/race/mount-b", "ramfs", 0, MR_NULL), 0);
        ret = mr_fs_opendir(&mount_a, "/race/mount-a");
        failures += fs_race_expect("open mount a", ret, 0);
        if (ret == 0) {
            sb = mount_a.inode->sb;
            ret = mr_super_block_op_enter(sb);
            failures += fs_race_expect("enter mount a operation", ret, 0);
            if (ret == 0) {
                failures += fs_race_expect(
                    "reject target unmount", mr_fs_umount("/race/mount-a"),
                    -MR_EBUSY);
                failures += fs_race_expect(
                    "allow unrelated unmount", mr_fs_umount("/race/mount-b"),
                    0);
                mr_super_block_op_exit(sb);
            }
            failures += fs_race_expect("umount idle mount a",
                                       mr_fs_umount("/race/mount-a"), 0);
            failures += fs_race_expect("close mount a",
                                       mr_fs_closedir(&mount_a), 0);
        }
    }

    /* Race unlink and rename commits on shared namespace nodes */
    failures += fs_same_node_race_test();

    /* Init worker gate */
    ret = fs_race_gate_init(&gate);
    failures += fs_race_expect("init lifecycle gate", ret, 0);
    if (ret != 0) {
        goto _exit;
    }

    /* Start workers */
    for (index = 0; index < FS_RACE_THREADS; index++) {
        contexts[index].gate = &gate;
        contexts[index].index = index;
        contexts[index].create_ret = -MR_EIO;
        contexts[index].link_ret = -MR_EIO;
        contexts[index].failures = 0;
        ret = pthread_create(&threads[index], MR_NULL, fs_race_worker,
                             &contexts[index]);
        failures += fs_race_expect("create lifecycle worker", ret, 0);
        if (ret == 0) {
            started[index] = MR_TRUE;
            workers++;
        }
    }

    /* Release workers together */
    if (workers > 0) {
        failures += fs_race_expect("open lifecycle gate",
                                   fs_race_gate_open(&gate, workers), 0);
    }

    /* Join workers and inspect competing results */
    for (index = 0; index < FS_RACE_THREADS; index++) {
        if (started[index] == MR_FALSE) {
            continue;
        }
        failures += fs_race_expect("join lifecycle worker",
                                   pthread_join(threads[index], MR_NULL), 0);
        failures += contexts[index].failures;
        if (contexts[index].create_ret == 0) {
            create_success++;
        } else {
            failures += fs_race_expect("exclusive create loser",
                                       contexts[index].create_ret, -MR_EEXIST);
        }
        if (contexts[index].link_ret == 0) {
            link_success++;
        } else {
            failures += fs_race_expect("exclusive link loser",
                                       contexts[index].link_ret, -MR_EEXIST);
        }
    }
    failures
        += fs_race_expect("one exclusive create winner", create_success, 1);
    failures += fs_race_expect("one exclusive link winner", link_success, 1);
    failures += fs_race_expect("verify exclusive file",
                               fs_test_stat("/race/once", &stat), 0);
    failures += fs_race_expect("verify exclusive link",
                               fs_test_stat("/race/once-link", &stat), 0);
    fs_race_gate_destroy(&gate);

_exit:
    /* Restore namespace and unmount */
    failures += fs_race_expect("restore lifecycle cwd", mr_fs_chdir("/"), 0);
    failures += fs_race_expect("umount lifecycle ramfs", mr_fs_umount("/"), 0);
    return failures;
}

static void lifecycle_race(void) {
    MR_TEST_EXPECT_EQ_INT(fs_lifecycle_race_test(), 0);
}
MR_TEST_EXPORT(fs, lifecycle_race, lifecycle_race);
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS)                           \
        * && defined(MR_USE_TEST_PTHREAD) */
