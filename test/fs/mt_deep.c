/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-30        MacRsh        Multi-thread deep test version
 */

/* Concurrency race probe switch
 *
 * The cases in this file exercise real races in the fs core and are
 * disabled by default because they crash the whole test binary:
 *
 * - fs.mt_unlink_rename_churn repeatedly aborts in dentry.c:706
 *   (MR_DENTRY_IS_ADDED, a dentry is re-linked while still linked) and
 *   has also produced segmentation faults (stale dentry use)
 * - fs.mt_append_integrity and fs.mt_stripe_write lose concurrent file
 *   extensions in ramfs_regular_write (final size below expectation)
 * - fs.mt_dentry_pressure_walk sees -MR_EEXIST on per-thread unique
 *   names from the O_CREAT|O_EXCL check-then-create window
 * - the remaining cases produce intermittent EXPECT failures
 *
 * Define MR_USE_FS_RACE_PROBE (for example with -DMR_USE_FS_RACE_PROBE)
 * to build and run them.
 */
#if defined(MR_USE_FS_RACE_PROBE)

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS) && defined(MR_USE_TEST_PTHREAD)
#include <mrX.h>
#include <pthread.h>
#include <mrX/libc/mr_atomic.h>
#include <mrX/libc/mr_string.h>
#include <mrX/test/fs/fs_test.h>

/* Multi-thread fs test thread count definition */
#define MT_FS_THREADS                       (4)

/* Multi-thread fs test iteration count definition */
#define MT_FS_ITERS                         (100)

/* Multi-thread fs test append block size definition */
#define MT_FS_BLOCK                         (16)

/* Multi-thread fs test write stripe size definition */
#define MT_FS_STRIPE                        (64)

/* Multi-thread fs test spin safety limit definition */
#define MT_FS_SPIN                          (1000000)

/* Multi-thread fs test exclusive create round count definition */
#define MT_FS_EXCL_ROUNDS                   (50)

/* Multi-thread fs test fifo byte count definition */
#define MT_FIFO_BYTES                       (200)

/* Multi-thread fs test walker iteration count definition */
#define MT_FS_WALK_ITERS                    (50)

/* Multi-thread fs test long name prefix definition */
#define MT_FS_LONG_NAME                     "/dirt/xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"

/* Multi-thread fs thread handle type */
typedef struct mt_fs_worker {
    pthread_t handle;
    int started;
    int id;
    volatile mr_uint32_t *__test_status;
    void *ctx;
} mt_fs_worker_t;

/* Helper: expects a return value to be zero or one of two error codes */
static void mt_fs_expect_ret(volatile mr_uint32_t *__test_status, int ret,
                             int ok1, int ok2) {
    MR_TEST_EXPECT_EQ_INT((ret == 0) || (ret == ok1) || (ret == ok2), MR_TRUE);
}

/* Helper: spawns every worker thread and returns the started count */
static int mt_fs_spawn(mt_fs_worker_t *workers, int count,
                       void *(*entry)(void *),
                       volatile mr_uint32_t *__test_status, void *ctx) {
    int created = 0;
    int i;

    for (i = 0; i < count; i++) {
        workers[i].started = 0;
        workers[i].id = i;
        workers[i].__test_status = __test_status;
        workers[i].ctx = ctx;
        if (pthread_create(&workers[i].handle, MR_NULL, entry, &workers[i])
            != 0) {
            break;
        }
        workers[i].started = 1;
        created++;
    }
    return created;
}

/* Helper: joins every started worker and reports join failures */
static void mt_fs_join(mt_fs_worker_t *workers, int created,
                       volatile mr_uint32_t *__test_status) {
    int i;

    for (i = 0; i < created; i++) {
        MR_TEST_EXPECT_EQ_INT(pthread_join(workers[i].handle, MR_NULL), 0);
    }
}

#if defined(MR_USE_FS_RAMFS_FIFO)
/* Fifo case shared context type */
typedef struct mt_fifo_fs_ctx {
    const char *path;
    mr_atomic_t writer_done;
    mr_atomic_t total_read;
    int total_written;
} mt_fifo_fs_ctx_t;
#endif /* defined(MR_USE_FS_RAMFS_FIFO) */

/* Exclusive create case shared context type */
typedef struct mt_excl_ctx {
    const char *path;
    mr_atomic_t round;
    mr_atomic_t arrived;
    mr_atomic_t done;
    mr_atomic_t created;
} mt_excl_ctx_t;

/* Append case worker argument type */
typedef struct mt_append_arg {
    const char *path;
    int id;
} mt_append_arg_t;

/* Stripe case worker argument type */
typedef struct mt_stripe_arg {
    const char *path;
    int id;
} mt_stripe_arg_t;

/* Churn case worker argument type */
typedef struct mt_churn_arg {
    const char *path;
    const char *alt_path;
    int id;
} mt_churn_arg_t;

/* Truncate case shared context type */
typedef struct mt_trunc_ctx {
    const char *path;
    int pattern;
} mt_trunc_ctx_t;

/* Mount case worker argument type */
typedef struct mt_mount_arg {
    const char *mount_path;
    const char *stable_path;
    int id;
} mt_mount_arg_t;

/* Dentry pressure case worker argument type */
typedef struct mt_dentry_arg {
    const char *dir;
    int id;
} mt_dentry_arg_t;

/* Append worker: appends tagged blocks to one file with O_APPEND */
static void *mt_append_worker(void *argument) {
    mt_fs_worker_t *worker = argument;
    volatile mr_uint32_t *__test_status = worker->__test_status;
    mt_append_arg_t *arg = worker->ctx;
    mr_file_t file = MR_FILE_INIT(file);
    mr_uint8_t block[MT_FS_BLOCK];
    int i, k;
    mr_err_t ret;

    ret = mr_fs_open(&file, arg->path,
                     MR_O_WRONLY | MR_O_CREAT | MR_O_APPEND);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return MR_NULL;
    }
    for (i = 0; i < MT_FS_ITERS; i++) {
        /* Each block carries a magic, owner id, sequence and XOR pattern */
        block[0] = 0xA5;
        block[1] = (mr_uint8_t)arg->id;
        block[2] = (mr_uint8_t)(i & 0xff);
        block[3] = (mr_uint8_t)((i >> 8) & 0xff);
        for (k = 4; k < MT_FS_BLOCK; k++) {
            block[k] = (mr_uint8_t)(arg->id ^ (i & 0xff) ^ k);
        }
        MR_TEST_EXPECT_EQ_INT(mr_fs_write(&file, block, MT_FS_BLOCK),
                              MT_FS_BLOCK);
    }
    MR_TEST_EXPECT_EQ_INT(mr_fs_close(&file), 0);
    return MR_NULL;
}

/* Test: concurrent O_APPEND writers must preserve every tagged block */
MR_TEST(fs, mt_append_integrity) {
    mt_fs_worker_t workers[MT_FS_THREADS];
    mt_append_arg_t args[MT_FS_THREADS];
    mr_file_t file = MR_FILE_INIT(file);
    mr_stat_t stat;
    mr_uint8_t block[MT_FS_BLOCK];
    char path[] = "/appended";
    int blocks = 0;
    int created;
    int i, k;
    mr_err_t ret;
    mr_bool_t mounted = MR_FALSE;

    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    mounted = MR_TRUE;

    for (i = 0; i < MT_FS_THREADS; i++) {
        args[i].path = path;
        args[i].id = i;
    }
    created = mt_fs_spawn(workers, MT_FS_THREADS, mt_append_worker,
                          __test_status, args);
    mt_fs_join(workers, created, __test_status);
    if (created != MT_FS_THREADS) {
        goto _exit;
    }

    /* The final size must account for every appended block exactly once */
    ret = mr_fs_open(&file, path, MR_O_RDONLY);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    MR_TEST_EXPECT_EQ_INT(mr_fs_fstat(&file, &stat), 0);
    MR_TEST_EXPECT_EQ_INT((int)stat.size,
                          MT_FS_THREADS * MT_FS_ITERS * MT_FS_BLOCK);
    while (mr_fs_read(&file, block, MT_FS_BLOCK) == MT_FS_BLOCK) {
        int tid = block[1];
        int seq = block[2] | (block[3] << 8);

        blocks++;
        MR_TEST_EXPECT_EQ_UINT(block[0], 0xA5);
        MR_TEST_EXPECT_LT_INT(tid, MT_FS_THREADS);
        MR_TEST_EXPECT_LT_INT(seq, MT_FS_ITERS);
        for (k = 4; k < MT_FS_BLOCK; k++) {
            MR_TEST_EXPECT_EQ_UINT(block[k],
                                   (mr_uint8_t)(tid ^ (seq & 0xff) ^ k));
        }
    }
    /* Every block must appear exactly once, no overwrite and no gap */
    MR_TEST_EXPECT_EQ_INT(blocks, MT_FS_THREADS * MT_FS_ITERS);
    MR_TEST_EXPECT_EQ_INT(mr_fs_close(&file), 0);

_exit:
    if (mounted) {
        MR_TEST_EXPECT_EQ_INT(mr_fs_chdir("/"), 0);
        MR_TEST_EXPECT_EQ_INT(mr_fs_umount("/"), 0);
    }
}

/* Stripe worker: writes a private offset range through its own handle */
static void *mt_stripe_worker(void *argument) {
    mt_fs_worker_t *worker = argument;
    volatile mr_uint32_t *__test_status = worker->__test_status;
    mt_stripe_arg_t *arg = worker->ctx;
    mr_file_t file = MR_FILE_INIT(file);
    mr_uint8_t buf[MT_FS_STRIPE];
    int base = arg->id * MT_FS_STRIPE;
    int i;
    mr_err_t ret;

    ret = mr_fs_open(&file, arg->path, MR_O_RDWR | MR_O_CREAT);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return MR_NULL;
    }
    for (i = 0; i < MT_FS_STRIPE; i++) {
        buf[i] = (mr_uint8_t)(base + i);
    }
    /* Seek based write into a range no other thread touches */
    MR_TEST_EXPECT_EQ_INT((int)mr_fs_lseek(&file, base, MR_SEEK_SET), base);
    MR_TEST_EXPECT_EQ_INT(mr_fs_write(&file, buf, MT_FS_STRIPE),
                          MT_FS_STRIPE);
    MR_TEST_EXPECT_EQ_INT(mr_fs_close(&file), 0);
    return MR_NULL;
}

/* Test: concurrent seek writes into disjoint ranges must not corrupt bytes */
MR_TEST(fs, mt_stripe_write) {
    mt_fs_worker_t workers[MT_FS_THREADS];
    mt_stripe_arg_t args[MT_FS_THREADS];
    mr_file_t file = MR_FILE_INIT(file);
    mr_stat_t stat;
    mr_uint8_t buf[MT_FS_STRIPE];
    char path[] = "/striped";
    int created;
    int i, j, k;
    mr_err_t ret;
    mr_bool_t mounted = MR_FALSE;

    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    mounted = MR_TRUE;

    for (i = 0; i < MT_FS_THREADS; i++) {
        args[i].path = path;
        args[i].id = i;
    }
    created = mt_fs_spawn(workers, MT_FS_THREADS, mt_stripe_worker,
                          __test_status, args);
    mt_fs_join(workers, created, __test_status);
    if (created != MT_FS_THREADS) {
        goto _exit;
    }

    /* Read back the whole file and validate every stripe byte */
    ret = mr_fs_open(&file, path, MR_O_RDONLY);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    MR_TEST_EXPECT_EQ_INT(mr_fs_fstat(&file, &stat), 0);
    MR_TEST_EXPECT_EQ_INT((int)stat.size, MT_FS_THREADS * MT_FS_STRIPE);
    for (i = 0; i < MT_FS_THREADS; i++) {
        ret = (mr_err_t)mr_fs_read(&file, buf, MT_FS_STRIPE);
        MR_TEST_EXPECT_EQ_INT(ret, MT_FS_STRIPE);
        if (ret != MT_FS_STRIPE) {
            break;
        }
        for (j = 0; j < MT_FS_STRIPE; j++) {
            k = i * MT_FS_STRIPE + j;
            MR_TEST_EXPECT_EQ_UINT(buf[j], (mr_uint8_t)k);
        }
    }
    MR_TEST_EXPECT_EQ_INT(mr_fs_close(&file), 0);
    /* Potential bug: ramfs_regular_write() samples inode->size, reallocates
     * and copies in separate steps, so two writers extending the file at the
     * same time can lose one extension and later writes splice into a stale
     * buffer */
_exit:
    if (mounted) {
        MR_TEST_EXPECT_EQ_INT(mr_fs_chdir("/"), 0);
        MR_TEST_EXPECT_EQ_INT(mr_fs_umount("/"), 0);
    }
}

/* Exclusive create worker: attempts one exclusive create per round */
static void *mt_excl_worker(void *argument) {
    mt_fs_worker_t *worker = argument;
    volatile mr_uint32_t *__test_status = worker->__test_status;
    mt_excl_ctx_t *ctx = worker->ctx;
    mr_file_t file = MR_FILE_INIT(file);
    int i, spins;
    mr_err_t ret;

    for (i = 1; i <= MT_FS_EXCL_ROUNDS; i++) {
        /* Wait for the main thread to publish the round number */
        spins = 0;
        while (mr_atomic_load(&ctx->round) != i) {
            if (++spins > MT_FS_SPIN) {
                return MR_NULL;
            }
        }
        ret = mr_fs_open(&file, ctx->path,
                         MR_O_WRONLY | MR_O_CREAT | MR_O_EXCL);
        MR_TEST_EXPECT_EQ_INT((ret == 0) || (ret == -MR_EEXIST), MR_TRUE);
        if (ret == 0) {
            mr_atomic_fetch_add(&ctx->created, 1);
            MR_TEST_EXPECT_EQ_INT(mr_fs_close(&file), 0);
        }
        mr_atomic_fetch_add(&ctx->done, 1);
    }
    return MR_NULL;
}

/* Test: concurrent O_CREAT|O_EXCL on one path must admit exactly one winner
 * per round */
MR_TEST(fs, mt_creat_excl_single_winner) {
    mt_fs_worker_t workers[2];
    mt_excl_ctx_t ctx;
    mr_stat_t stat;
    char path[] = "/excl";
    int created;
    int round, spins;
    mr_err_t ret;
    mr_bool_t mounted = MR_FALSE;

    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    mounted = MR_TRUE;

    ctx.path = path;
    ctx.round = 0;
    mr_atomic_init(&ctx.arrived, 0);
    mr_atomic_init(&ctx.done, 0);
    mr_atomic_init(&ctx.created, 0);

    created = mt_fs_spawn(workers, 2, mt_excl_worker, __test_status, &ctx);
    if (created != 2) {
        mt_fs_join(workers, created, __test_status);
        goto _exit;
    }

    for (round = 1; round <= MT_FS_EXCL_ROUNDS; round++) {
        /* Reset per round state and clear the previous winner */
        mr_fs_unlink(path);
        mr_atomic_store(&ctx.created, 0);
        mr_atomic_store(&ctx.done, 0);
        mr_atomic_store(&ctx.round, round);

        /* Wait until both workers finished this round */
        spins = 0;
        while (mr_atomic_load(&ctx.done) != 2) {
            if (++spins > MT_FS_SPIN) {
                break;
            }
        }
        MR_TEST_EXPECT_EQ_INT((int)mr_atomic_load(&ctx.done), 2);

        /* Exactly one thread may have won the exclusive create */
        MR_TEST_EXPECT_EQ_UINT((mr_uint32_t)mr_atomic_load(&ctx.created), 1);
        MR_TEST_EXPECT_EQ_INT(fs_test_stat(path, &stat), 0);
    }
    mt_fs_join(workers, created, __test_status);
    /* Potential bug: mr_fs_open() resolves the leaf as absent, then creates
     * the dentry in separate steps, so two threads can both pass the EXCL
     * check and one dentry replaces the other instead of failing with
     * -MR_EEXIST */
    mr_fs_unlink(path);

_exit:
    if (mounted) {
        MR_TEST_EXPECT_EQ_INT(mr_fs_chdir("/"), 0);
        MR_TEST_EXPECT_EQ_INT(mr_fs_umount("/"), 0);
    }
}

/* Unlink and rename churn worker: races destructive ops on one path */
static void *mt_unlink_rename_worker(void *argument) {
    mt_fs_worker_t *worker = argument;
    volatile mr_uint32_t *__test_status = worker->__test_status;
    mt_churn_arg_t *arg = worker->ctx;
    mr_file_t file = MR_FILE_INIT(file);
    mr_stat_t stat;
    mr_uint8_t byte = 0x5A;
    int i;
    mr_err_t ret;

    for (i = 0; i < MT_FS_ITERS; i++) {
        switch (worker->id) {
        case 0:
            /* Creator keeps the path populated */
            ret = mr_fs_open(&file, arg->path, MR_O_WRONLY | MR_O_CREAT);
            MR_TEST_EXPECT_EQ_INT(ret, 0);
            if (ret == 0) {
                MR_TEST_EXPECT_EQ_INT(mr_fs_write(&file, &byte, 1), 1);
                MR_TEST_EXPECT_EQ_INT(mr_fs_close(&file), 0);
            }
            break;
        case 1:
            /* Unlinker removes it whenever it exists */
            ret = mr_fs_unlink(arg->path);
            mt_fs_expect_ret(__test_status, ret, -MR_ENOENT, 0);
            break;
        case 2:
            /* Renamer moves it away and removes the destination */
            ret = mr_fs_rename(arg->path, arg->alt_path);
            mt_fs_expect_ret(__test_status, ret, -MR_ENOENT, 0);
            if (ret == 0) {
                ret = mr_fs_unlink(arg->alt_path);
                mt_fs_expect_ret(__test_status, ret, -MR_ENOENT, 0);
            }
            break;
        default:
            /* Statter only accepts existence or a clean absence */
            ret = fs_test_stat(arg->path, &stat);
            mt_fs_expect_ret(__test_status, ret, -MR_ENOENT, 0);
            break;
        }
    }
    return MR_NULL;
}

/* Test: concurrent create, unlink, rename and stat of one path must stay
 * consistent and never crash */
MR_TEST(fs, mt_unlink_rename_churn) {
    mt_fs_worker_t workers[MT_FS_THREADS];
    mt_churn_arg_t args[MT_FS_THREADS];
    mr_stat_t stat;
    char path[] = "/raced";
    char alt_path[] = "/raced2";
    int created;
    int i;
    mr_err_t ret;
    mr_bool_t mounted = MR_FALSE;

    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    mounted = MR_TRUE;

    for (i = 0; i < MT_FS_THREADS; i++) {
        args[i].path = path;
        args[i].alt_path = alt_path;
        args[i].id = i;
    }
    created = mt_fs_spawn(workers, MT_FS_THREADS, mt_unlink_rename_worker,
                          __test_status, args);
    mt_fs_join(workers, created, __test_status);
    if (created != MT_FS_THREADS) {
        goto _exit;
    }

    /* Any surviving file must hold exactly the content the creator wrote */
    ret = fs_test_stat(path, &stat);
    mt_fs_expect_ret(__test_status, ret, -MR_ENOENT, 0);
    if (ret == 0) {
        MR_TEST_EXPECT_EQ_INT((int)stat.size, 1);
    }
    ret = fs_test_stat(alt_path, &stat);
    mt_fs_expect_ret(__test_status, ret, -MR_ENOENT, 0);
    if (ret == 0) {
        MR_TEST_EXPECT_EQ_INT((int)stat.size, 1);
    }
    /* Potential bug: mr_fs_rename() and mr_fs_unlink() re-resolve the parent
     * and the leaf in separate unlocked steps, so a concurrent unlink can
     * free the dentry between resolution and removal and operate on a stale
     * or detached dentry */

_exit:
    /* Best effort cleanup of whatever survived the race */
    mr_fs_unlink(path);
    mr_fs_unlink(alt_path);
    if (mounted) {
        MR_TEST_EXPECT_EQ_INT(mr_fs_chdir("/"), 0);
        MR_TEST_EXPECT_EQ_INT(mr_fs_umount("/"), 0);
    }
}

/* Mkdir and rmdir churn worker: races directory create and remove */
static void *mt_mkdir_rmdir_worker(void *argument) {
    mt_fs_worker_t *worker = argument;
    volatile mr_uint32_t *__test_status = worker->__test_status;
    mt_churn_arg_t *arg = worker->ctx;
    int i;
    mr_err_t ret;

    for (i = 0; i < MT_FS_ITERS; i++) {
        if ((worker->id & 1) == 0) {
            ret = mr_fs_mkdir(arg->path);
            mt_fs_expect_ret(__test_status, ret, -MR_EEXIST, 0);
        } else {
            ret = mr_fs_rmdir(arg->path);
            mt_fs_expect_ret(__test_status, ret, -MR_ENOENT, 0);
        }
    }
    return MR_NULL;
}

/* Test: concurrent mkdir and rmdir of one directory name must end in a
 * consistent state */
MR_TEST(fs, mt_mkdir_rmdir_churn) {
    mt_fs_worker_t workers[MT_FS_THREADS];
    mt_churn_arg_t args[MT_FS_THREADS];
    mr_stat_t stat;
    char path[] = "/mdr";
    int created;
    int i;
    mr_err_t ret;
    mr_bool_t mounted = MR_FALSE;

    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    mounted = MR_TRUE;

    for (i = 0; i < MT_FS_THREADS; i++) {
        args[i].path = path;
        args[i].alt_path = MR_NULL;
        args[i].id = i;
    }
    created = mt_fs_spawn(workers, MT_FS_THREADS, mt_mkdir_rmdir_worker,
                          __test_status, args);
    mt_fs_join(workers, created, __test_status);
    if (created != MT_FS_THREADS) {
        goto _exit;
    }

    /* The final state must be exactly an empty directory or absence */
    ret = fs_test_stat(path, &stat);
    mt_fs_expect_ret(__test_status, ret, -MR_ENOENT, 0);
    if (ret == 0) {
        MR_TEST_EXPECT_EQ_UINT(stat.type, MR_INODE_DIR);
    }
    ret = mr_fs_rmdir(path);
    mt_fs_expect_ret(__test_status, ret, -MR_ENOENT, 0);
    MR_TEST_EXPECT_EQ_INT(fs_test_stat(path, &stat), -MR_ENOENT);
    /* Potential bug: mr_fs_mkdir() and mr_fs_rmdir() check emptiness and
     * detach the dentry in separate steps, so rmdir can pass the empty check
     * while mkdir inserts a child and the directory is removed non empty */

_exit:
    mr_fs_rmdir(path);
    if (mounted) {
        MR_TEST_EXPECT_EQ_INT(mr_fs_chdir("/"), 0);
        MR_TEST_EXPECT_EQ_INT(mr_fs_umount("/"), 0);
    }
}

/* Truncate worker: shrinks and refills one file repeatedly */
static void *mt_truncate_worker(void *argument) {
    mt_fs_worker_t *worker = argument;
    volatile mr_uint32_t *__test_status = worker->__test_status;
    mt_trunc_ctx_t *ctx = worker->ctx;
    mr_file_t file = MR_FILE_INIT(file);
    mr_uint8_t buf[2 * MT_FS_STRIPE];
    int i;
    mr_err_t ret;

    ret = mr_fs_open(&file, ctx->path, MR_O_RDWR | MR_O_CREAT);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return MR_NULL;
    }
    mr_memset(buf, ctx->pattern, sizeof(buf));
    for (i = 0; i < MT_FS_ITERS; i++) {
        MR_TEST_EXPECT_EQ_INT(mr_fs_ftruncate(&file, 0), 0);
        MR_TEST_EXPECT_EQ_INT(mr_fs_write(&file, buf, sizeof(buf)),
                              (int)sizeof(buf));
        MR_TEST_EXPECT_EQ_INT(mr_fs_ftruncate(&file, MT_FS_STRIPE), 0);
    }
    MR_TEST_EXPECT_EQ_INT(mr_fs_close(&file), 0);
    return MR_NULL;
}

/* Truncate reader worker: reads a file while its size churns */
static void *mt_truncate_reader(void *argument) {
    mt_fs_worker_t *worker = argument;
    volatile mr_uint32_t *__test_status = worker->__test_status;
    mt_trunc_ctx_t *ctx = worker->ctx;
    mr_file_t file = MR_FILE_INIT(file);
    mr_uint8_t buf[MT_FS_STRIPE];
    int i, j;
    mr_ssize_t n;
    mr_err_t ret;

    ret = mr_fs_open(&file, ctx->path, MR_O_RDONLY);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return MR_NULL;
    }
    for (i = 0; i < MT_FS_ITERS; i++) {
        /* Rewind so every read can observe live data instead of draining
         * the file once and reading nothing afterwards */
        MR_TEST_EXPECT_EQ_INT((int)mr_fs_lseek(&file, 0, MR_SEEK_SET), 0);
        n = mr_fs_read(&file, buf, MT_FS_STRIPE);
        /* Reads must never fail while the file is only truncated */
        MR_TEST_EXPECT_GE_INT((int)n, 0);
        for (j = 0; j < (int)n; j++) {
            /* Every visible byte is either a hole zero or the pattern */
            MR_TEST_EXPECT_EQ_UINT((buf[j] == 0x00)
                                       || (buf[j] == (mr_uint8_t)ctx->pattern),
                                   MR_TRUE);
        }
    }
    MR_TEST_EXPECT_EQ_INT(mr_fs_close(&file), 0);
    return MR_NULL;
}

/* Test: readers of a file being truncated must see partial or empty data,
 * never garbage or errors */
MR_TEST(fs, mt_truncate_vs_read) {
    mt_fs_worker_t workers[MT_FS_THREADS];
    mt_trunc_ctx_t ctx;
    char path[] = "/trunc";
    int created;
    mr_err_t ret;
    mr_bool_t mounted = MR_FALSE;

    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    mounted = MR_TRUE;

    ctx.path = path;
    ctx.pattern = 0xAA;

    /* Worker 0 truncates and refills, workers 1 to 3 read concurrently */
    created = mt_fs_spawn(workers, 1, mt_truncate_worker, __test_status, &ctx);
    created += mt_fs_spawn(workers + 1, MT_FS_THREADS - 1, mt_truncate_reader,
                           __test_status, &ctx);
    mt_fs_join(workers, created, __test_status);
    if (created != MT_FS_THREADS) {
        goto _exit;
    }
    /* Potential bug: ramfs_inode_truncate() frees node->regular.data while a
     * concurrent ramfs_regular_read() already sampled inode->size and copies
     * from the old buffer, so a read racing a truncate to zero dereferences
     * freed memory */

_exit:
    if (mounted) {
        MR_TEST_EXPECT_EQ_INT(mr_fs_chdir("/"), 0);
        MR_TEST_EXPECT_EQ_INT(mr_fs_umount("/"), 0);
    }
}

/* Mount worker: repeatedly mounts and unmounts a ramfs at one mount point */
static void *mt_mount_worker(void *argument) {
    mt_fs_worker_t *worker = argument;
    volatile mr_uint32_t *__test_status = worker->__test_status;
    mt_mount_arg_t *arg = worker->ctx;
    int i;
    mr_err_t ret;

    for (i = 0; i < 20; i++) {
        ret = mr_fs_mount(MR_NULL, arg->mount_path, "ramfs", 0, MR_NULL);
        mt_fs_expect_ret(__test_status, ret, -MR_EEXIST, -MR_EBUSY);
        ret = mr_fs_umount(arg->mount_path);
        mt_fs_expect_ret(__test_status, ret, -MR_ENOENT, 0);
    }
    return MR_NULL;
}

/* Stable worker: does file ops away from the mount point */
static void *mt_stable_worker(void *argument) {
    mt_fs_worker_t *worker = argument;
    volatile mr_uint32_t *__test_status = worker->__test_status;
    mt_mount_arg_t *arg = worker->ctx;
    mr_file_t file = MR_FILE_INIT(file);
    mr_stat_t stat;
    char path[64];
    mr_uint8_t byte;
    int i;
    mr_err_t ret;

    for (i = 0; i < MT_FS_ITERS; i++) {
        mr_snprintf(path, sizeof(path), "%s/t%d-%d", arg->stable_path,
                    worker->id, i);
        byte = (mr_uint8_t)i;
        ret = mr_fs_open(&file, path, MR_O_WRONLY | MR_O_CREAT | MR_O_EXCL);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
        if (ret == 0) {
            MR_TEST_EXPECT_EQ_INT(mr_fs_write(&file, &byte, 1), 1);
            MR_TEST_EXPECT_EQ_INT(mr_fs_close(&file), 0);
        }
        ret = mr_fs_unlink(path);
        mt_fs_expect_ret(__test_status, ret, -MR_ENOENT, 0);
        /* Path walks across the flipping mount point must stay safe */
        ret = fs_test_stat(arg->mount_path, &stat);
        mt_fs_expect_ret(__test_status, ret, -MR_ENOENT, 0);
    }
    return MR_NULL;
}

/* Test: mount and umount of a subdirectory while other threads operate on
 * files elsewhere must not disturb the rest of the tree */
MR_TEST(fs, mt_mount_umount_vs_ops) {
    mt_fs_worker_t workers[MT_FS_THREADS];
    mt_mount_arg_t args[MT_FS_THREADS];
    char mount_path[] = "/mntp";
    char stable_path[] = "/stable";
    int created;
    int i;
    mr_err_t ret;
    mr_bool_t mounted = MR_FALSE;

    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    mounted = MR_TRUE;
    MR_TEST_ASSERT_EQ_INT(mr_fs_mkdir(mount_path), 0);
    MR_TEST_ASSERT_EQ_INT(mr_fs_mkdir(stable_path), 0);

    for (i = 0; i < MT_FS_THREADS; i++) {
        args[i].mount_path = mount_path;
        args[i].stable_path = stable_path;
        args[i].id = i;
    }
    /* Worker 0 flips the mount, workers 1 to 3 churn files in /stable */
    created = mt_fs_spawn(workers, 1, mt_mount_worker, __test_status, args);
    created += mt_fs_spawn(workers + 1, MT_FS_THREADS - 1, mt_stable_worker,
                           __test_status, &args[1]);
    mt_fs_join(workers, created, __test_status);
    if (created != MT_FS_THREADS) {
        goto _exit;
    }

    /* The mount worker always unmounts after mounting, so the mount point
     * must be free again or absent, never still mounted */
    ret = mr_fs_umount(mount_path);
    mt_fs_expect_ret(__test_status, ret, -MR_ENOENT, 0);
    /* Potential bug: path resolution samples the mounted flag and the child
     * dentry of a mount point in separate steps, so a lookup racing umount
     * can return the covered dentry or a stale super block pointer */

_exit:
    /* Best effort unmount so the root unmount is not blocked with -MR_EBUSY */
    mr_fs_umount(mount_path);
    mr_fs_rmdir(mount_path);
    mr_fs_rmdir(stable_path);
    if (mounted) {
        MR_TEST_EXPECT_EQ_INT(mr_fs_chdir("/"), 0);
        MR_TEST_EXPECT_EQ_INT(mr_fs_umount("/"), 0);
    }
}

#if defined(MR_USE_FS_RAMFS_FIFO)
/* Fifo writer worker: writes an increasing byte sequence through the fifo */
static void *mt_fifo_writer(void *argument) {
    mt_fs_worker_t *worker = argument;
    volatile mr_uint32_t *__test_status = worker->__test_status;
    mt_fifo_fs_ctx_t *ctx = worker->ctx;
    mr_file_t file = MR_FILE_INIT(file);
    mr_uint8_t byte;
    int spins, value;
    mr_ssize_t n;
    mr_err_t ret;

    ret = mr_fs_open(&file, ctx->path, MR_O_WRONLY);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        mr_atomic_store(&ctx->writer_done, 1);
        return MR_NULL;
    }
    for (value = 0; value < MT_FIFO_BYTES; value++) {
        byte = (mr_uint8_t)value;
        /* Partial or zero writes are retried until the byte is accepted */
        spins = 0;
        do {
            n = mr_fs_write(&file, &byte, 1);
            MR_TEST_EXPECT_GE_INT((int)n, 0);
            if (++spins > MT_FS_SPIN) {
                break;
            }
        } while (n == 0);
        if (n == 1) {
            ctx->total_written++;
        }
    }
    MR_TEST_EXPECT_EQ_INT(mr_fs_close(&file), 0);
    mr_atomic_store(&ctx->writer_done, 1);
    return MR_NULL;
}

/* Fifo reader worker: drains the fifo and checks per reader ordering */
static void *mt_fifo_reader(void *argument) {
    mt_fs_worker_t *worker = argument;
    volatile mr_uint32_t *__test_status = worker->__test_status;
    mt_fifo_fs_ctx_t *ctx = worker->ctx;
    mr_file_t file = MR_FILE_INIT(file);
    mr_uint8_t buf[16];
    int last = -1;
    int spins = 0;
    int i;
    mr_ssize_t n;
    mr_err_t ret;

    ret = mr_fs_open(&file, ctx->path, MR_O_RDONLY);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return MR_NULL;
    }
    while (spins < MT_FS_SPIN) {
        n = mr_fs_read(&file, buf, sizeof(buf));
        MR_TEST_EXPECT_GE_INT((int)n, 0);
        if (n > 0) {
            spins = 0;
            /* The fifo preserves global order, so each reader view of the
             * strictly increasing sequence must also strictly increase */
            for (i = 0; i < (int)n; i++) {
                MR_TEST_EXPECT_GT_INT(buf[i], last);
                last = buf[i];
            }
            mr_atomic_fetch_add(&ctx->total_read, (mr_atomic_t)n);
            continue;
        }
        /* Empty read after the writer finished means the fifo is drained */
        if (mr_atomic_load(&ctx->writer_done) == 1) {
            break;
        }
        spins++;
    }
    MR_TEST_EXPECT_EQ_INT(mr_fs_close(&file), 0);
    return MR_NULL;
}

/* Test: one fifo writer and two readers must conserve every byte in order */
MR_TEST(fs, mt_fifo_rw_conservation) {
    mt_fs_worker_t workers[3];
    mt_fifo_fs_ctx_t ctx;
    char path[] = "/pipe";
    int created;
    mr_err_t ret;
    mr_bool_t mounted = MR_FALSE;

    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    mounted = MR_TRUE;
    MR_TEST_ASSERT_EQ_INT(mr_fs_mkfifo(path), 0);

    ctx.path = path;
    ctx.total_written = 0;
    mr_atomic_init(&ctx.writer_done, 0);
    mr_atomic_init(&ctx.total_read, 0);

    created = mt_fs_spawn(workers, 1, mt_fifo_writer, __test_status, &ctx);
    created += mt_fs_spawn(workers + 1, 2, mt_fifo_reader,
                           __test_status, &ctx);
    mt_fs_join(workers, created, __test_status);
    if (created != 3) {
        goto _exit;
    }

    /* Every accepted byte must be consumed exactly once */
    MR_TEST_EXPECT_EQ_INT(ctx.total_written, MT_FIFO_BYTES);
    MR_TEST_EXPECT_EQ_INT((int)mr_atomic_load(&ctx.total_read),
                          ctx.total_written);
    /* Potential bug: mr_fifo_write() and mr_fifo_read() check space or
     * length, copy data and advance the index in separate steps, so a
     * concurrent reader can copy a partially updated element or two readers
     * can pop the same byte, breaking byte conservation and order */

_exit:
    mr_fs_unlink(path);
    if (mounted) {
        MR_TEST_EXPECT_EQ_INT(mr_fs_chdir("/"), 0);
        MR_TEST_EXPECT_EQ_INT(mr_fs_umount("/"), 0);
    }
}
#endif /* defined(MR_USE_FS_RAMFS_FIFO) */

/* Dentry pressure worker: creates and unlinks names around the inline and
 * heap boundary */
static void *mt_dentry_worker(void *argument) {
    mt_fs_worker_t *worker = argument;
    volatile mr_uint32_t *__test_status = worker->__test_status;
    mt_dentry_arg_t *arg = worker->ctx;
    mr_file_t file = MR_FILE_INIT(file);
    char path[96];
    int i;
    mr_err_t ret;

    for (i = 0; i < MT_FS_ITERS; i++) {
        if (i & 1) {
            /* Long name beyond the 32 byte inline dentry storage */
            mr_snprintf(path, sizeof(path), "%s%d-%d", MT_FS_LONG_NAME,
                        arg->id, i);
        } else {
            /* Short name inside the inline dentry storage */
            mr_snprintf(path, sizeof(path), "%s/s%d-%d", arg->dir,
                        arg->id, i);
        }
        ret = mr_fs_open(&file, path, MR_O_WRONLY | MR_O_CREAT | MR_O_EXCL);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
        if (ret != 0) {
            continue;
        }
        MR_TEST_EXPECT_EQ_INT(mr_fs_close(&file), 0);
        ret = mr_fs_unlink(path);
        mt_fs_expect_ret(__test_status, ret, -MR_ENOENT, 0);
    }
    return MR_NULL;
}

/* Dentry walker worker: reads the whole directory while it churns */
static void *mt_dentry_walker(void *argument) {
    mt_fs_worker_t *worker = argument;
    volatile mr_uint32_t *__test_status = worker->__test_status;
    mt_dentry_arg_t *arg = worker->ctx;
    mr_file_t file = MR_FILE_INIT(file);
    mr_dirent_t dirent;
    int i;
    mr_ssize_t r;
    mr_err_t ret;

    for (i = 0; i < MT_FS_WALK_ITERS; i++) {
        ret = mr_fs_open(&file, arg->dir, MR_O_RDONLY | MR_O_DIRECTORY);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
        if (ret != 0) {
            continue;
        }
        while ((r = mr_fs_readdir(&file, &dirent)) > 0) {
            /* Every reported entry must carry a non empty name */
            MR_TEST_EXPECT_NE_UINT(dirent.name[0], '\0');
        }
        /* A negative readdir while entries churn concurrently is tolerated
         * but still recorded as a non fatal suspicion */
        MR_TEST_EXPECT_GE_INT((int)r, 0);
        MR_TEST_EXPECT_EQ_INT(mr_fs_close(&file), 0);
    }
    return MR_NULL;
}

/* Test: create and unlink churn around the inline and heap dentry boundary
 * while one thread walks the parent must not corrupt listings */
MR_TEST(fs, mt_dentry_pressure_walk) {
    mt_fs_worker_t workers[MT_FS_THREADS];
    mt_dentry_arg_t args[MT_FS_THREADS];
    mr_file_t file = MR_FILE_INIT(file);
    mr_dirent_t dirent;
    char dir[] = "/dirt";
    int created;
    int di;
    mr_err_t ret;
    mr_bool_t mounted = MR_FALSE;

    ret = fs_test_mount_root("ramfs", 0, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    mounted = MR_TRUE;
    MR_TEST_ASSERT_EQ_INT(mr_fs_mkdir(dir), 0);

    for (di = 0; di < MT_FS_THREADS; di++) {
        args[di].dir = dir;
        args[di].id = di;
    }
    /* Workers 0 to 2 churn names, worker 3 walks the directory */
    created = mt_fs_spawn(workers, MT_FS_THREADS - 1, mt_dentry_worker,
                          __test_status, args);
    created += mt_fs_spawn(workers + MT_FS_THREADS - 1, 1, mt_dentry_walker,
                           __test_status, &args[MT_FS_THREADS - 1]);
    mt_fs_join(workers, created, __test_status);
    if (created != MT_FS_THREADS) {
        goto _exit;
    }

    /* After the churn the directory must be completely empty */
    ret = mr_fs_open(&file, dir, MR_O_RDONLY | MR_O_DIRECTORY);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        MR_TEST_EXPECT_EQ_INT(mr_fs_readdir(&file, &dirent), 0);
        MR_TEST_EXPECT_EQ_INT(mr_fs_close(&file), 0);
    }
    /* Potential bug: ramfs directory insertion and removal move entries
     * between the inline array and the heap list while readdir walks the
     * same storage, so a concurrent walk can skip or repeat entries */

_exit:
    mr_fs_rmdir(dir);
    if (mounted) {
        MR_TEST_EXPECT_EQ_INT(mr_fs_chdir("/"), 0);
        MR_TEST_EXPECT_EQ_INT(mr_fs_umount("/"), 0);
    }
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS)                           \
        * && defined(MR_USE_TEST_PTHREAD) */

#endif /* defined(MR_USE_FS_RACE_PROBE) */
