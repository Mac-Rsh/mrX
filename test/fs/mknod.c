/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-07-20        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS)
#include <mrX.h>
#include "fs_test.h"

#define MKNOD_FS_NAME_MAX (16)

typedef struct mknod_fs_context {
    mr_inode_t *last_inode;
    void *last_priv;
    char last_name[MKNOD_FS_NAME_MAX];
    mr_inode_type_t last_type;
    mr_size_t create_calls;
    mr_size_t mknod_calls;
    mr_size_t destroy_calls;
    mr_bool_t mknod_error;
} mknod_fs_context_t;

static mknod_fs_context_t *__mknod_fs_context;

static mr_err_t mknod_fs_inode_create(mr_inode_t *dir, const char *name,
                                      mr_inode_type_t type, mr_inode_t **out);
static mr_err_t mknod_fs_inode_mknod(mr_inode_t *dir, const char *name,
                                     mr_inode_type_t type, void *priv,
                                     mr_inode_t **out);
static void mknod_fs_inode_destroy(mr_inode_t *inode);
static mr_err_t mknod_fs_mount(mr_super_block_t *sb, const char *source,
                               int flags, void *data);

static const mr_inode_ops_t __mknod_fs_root_inode_ops = {
    .create = mknod_fs_inode_create,
    .mknod = mknod_fs_inode_mknod,
};

static const mr_inode_ops_t __mknod_fs_node_inode_ops = {
    .destroy = mknod_fs_inode_destroy,
};

static const mr_file_ops_t __mknod_fs_file_ops;

static mr_file_system_t __mknod_fs
    = MR_FILE_SYSTEM_INIT("mknodfs", mknod_fs_mount, MR_NULL, MR_NULL);

static int mknod_fs_expect(const char *name, mr_int64_t actual,
                           mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
    return (actual == expected) ? 0 : 1;
}

static int mknod_fs_size_expect(const char *name, mr_size_t actual,
                                mr_size_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_UINT(actual, expected);
    return (actual == expected) ? 0 : 1;
}

static int mknod_fs_stat_expect(const char *path, mr_err_t expected,
                                mr_inode_type_t type) {
    mr_stat_t stat;
    mr_err_t ret;
    int failures;

    ret = fs_test_stat(path, &stat);
    failures = mknod_fs_expect(path, ret, expected);
    if ((ret == 0) && (stat.type != type)) {
        MR_TEST_EXPECT_EQ_INT(stat.type, type);
        failures++;
    }
    return failures;
}

static mr_err_t mknod_fs_inode_create(mr_inode_t *dir, const char *name,
                                      mr_inode_type_t type, mr_inode_t **out) {
    mknod_fs_context_t *context = dir->sb->priv;

    MR_IGNORE(name);
    MR_IGNORE(type);

    /* Record an unexpected mknod fallback */
    context->create_calls++;
    *out = MR_NULL;
    return -MR_ENOSYS;
}

static mr_err_t mknod_fs_inode_mknod(mr_inode_t *dir, const char *name,
                                     mr_inode_type_t type, void *priv,
                                     mr_inode_t **out) {
    mknod_fs_context_t *context = dir->sb->priv;
    mr_inode_t *inode;

    /* Record callback parameters */
    *out = MR_NULL;
    context->mknod_calls++;
    context->last_priv = priv;
    context->last_type = type;
    mr_strncpy(context->last_name, name, sizeof(context->last_name) - 1);
    context->last_name[sizeof(context->last_name) - 1] = '\0';

    /* Inject callback failure */
    if (context->mknod_error) {
        return -MR_EIO;
    }

    /* Create node inode */
    inode = mr_inode_create(type, &__mknod_fs_node_inode_ops,
                            &__mknod_fs_file_ops, priv);
    if (!inode) {
        return -MR_ENOMEM;
    }
    context->last_inode = inode;
    *out = inode;
    return 0;
}

static void mknod_fs_inode_destroy(mr_inode_t *inode) {
    if (__mknod_fs_context) {
        __mknod_fs_context->destroy_calls++;
        if (__mknod_fs_context->last_inode == inode) {
            __mknod_fs_context->last_inode = MR_NULL;
        }
    }
}

static mr_err_t mknod_fs_mount(mr_super_block_t *sb, const char *source,
                               int flags, void *data) {
    mknod_fs_context_t *context = data;
    mr_inode_t *root;

    MR_IGNORE(source);
    MR_IGNORE(flags);

    /* Check mount data */
    if (!context) {
        return -MR_EINVAL;
    }

    /* Create root inode */
    root = mr_inode_create(MR_INODE_DIR, &__mknod_fs_root_inode_ops, MR_NULL,
                           MR_NULL);
    if (!root) {
        return -MR_ENOMEM;
    }
    sb->priv = context;
    sb->root = root;
    __mknod_fs_context = context;
    return 0;
}

static int fs_mknod_test(void) {
    mknod_fs_context_t context = {0};
    mr_file_t fallback_file = MR_FILE_INIT(fallback_file);
    mr_heap_info_t heap_before;
    mr_heap_info_t heap_after;
    int object;
    int failures = 0;
    mr_err_t ret;

    /* Register test filesystem */
    failures += mknod_fs_expect("register mknodfs",
                                mr_file_system_register(&__mknod_fs), 0);
    if (failures != 0) {
        return failures;
    }

    /* Record heap state */
    heap_before = mr_heap_information(MR_TRUE);

    /* Fall back to create when mknod is not implemented */
    ret = mr_fs_mount(MR_NULL, "/", "ramfs", 0, MR_NULL);
    failures += mknod_fs_expect("mknod mount ramfs", ret, 0);
    if (ret == 0) {
        ret = mr_fs_mknod("/fallback", MR_INODE_CHR, &object);
        failures += mknod_fs_expect("fallback mknod to create", ret, 0);
        if (ret == 0) {
            failures += mknod_fs_stat_expect("/fallback", 0, MR_INODE_CHR);
            ret = mr_fs_open(&fallback_file, "/fallback", MR_O_RDONLY);
            failures += mknod_fs_expect("open fallback node", ret, 0);
            if (ret == 0) {
                failures += mknod_fs_expect(
                    "fallback ignored private data",
                    fallback_file.inode->priv != &object, MR_TRUE);
                failures += mknod_fs_expect("close fallback node",
                                             mr_fs_close(&fallback_file), 0);
            }
            failures += mknod_fs_expect("unlink fallback node",
                                         fs_test_unlink("/fallback"), 0);
        }
        failures += mknod_fs_expect("mknod umount ramfs", mr_fs_umount("/"), 0);
    }

    /* Mount test filesystem */
    ret = mr_fs_mount(MR_NULL, "/", "mknodfs", 0, &context);
    failures += mknod_fs_expect("mount mknodfs", ret, 0);
    if (ret != 0) {
        return failures;
    }

    /* Reject requests before invoking the filesystem */
    failures += mknod_fs_expect("reject invalid mknod type",
                                mr_fs_mknod("/invalid", MR_INODE_NONE, &object),
                                -MR_EINVAL);
    failures += mknod_fs_expect(
        "reject missing mknod parent",
        mr_fs_mknod("/missing/node", MR_INODE_CHR, &object), -MR_ENOENT);
    failures += mknod_fs_size_expect("mknod callback not called",
                                     context.mknod_calls, 0);
    failures += mknod_fs_size_expect("create callback not called",
                                     context.create_calls, 0);

    /* Roll back callback failure */
    context.mknod_error = MR_TRUE;
    ret = mr_fs_mknod("/failed", MR_INODE_CHR, &object);
    context.mknod_error = MR_FALSE;
    failures += mknod_fs_expect("propagate mknod error", ret, -MR_EIO);
    failures += mknod_fs_stat_expect("/failed", -MR_ENOENT, MR_INODE_NONE);
    failures += mknod_fs_size_expect("failed mknod callback count",
                                     context.mknod_calls, 1);
    failures += mknod_fs_size_expect("failed mknod did not fall back",
                                     context.create_calls, 0);

    /* Publish a node */
    ret = mr_fs_mknod("/node", MR_INODE_CHR, &object);
    failures += mknod_fs_expect("create mknod node", ret, 0);
    failures
        += mknod_fs_size_expect("mknod callback count", context.mknod_calls, 2);
    failures += mknod_fs_expect("mknod private data",
                                context.last_priv == &object, MR_TRUE);
    failures
        += mknod_fs_expect("mknod inode type", context.last_type, MR_INODE_CHR);
    failures += mknod_fs_expect(
        "mknod inode private data",
        context.last_inode && (context.last_inode->priv == &object), MR_TRUE);
    failures += mknod_fs_expect(
        "mknod inode super-block",
        context.last_inode && (context.last_inode->sb != MR_NULL), MR_TRUE);
    if (mr_strcmp(context.last_name, "node") != 0) {
        MR_TEST_EXPECT_EQ_INT(mr_strcmp(context.last_name, "node"), 0);
        failures++;
    }
    failures += mknod_fs_stat_expect("/node", 0, MR_INODE_CHR);

    /* Reject duplicate names before invoking the filesystem */
    failures += mknod_fs_expect("reject duplicate mknod",
                                mr_fs_mknod("/node", MR_INODE_BLK, MR_NULL),
                                -MR_EEXIST);
    failures += mknod_fs_size_expect("duplicate mknod callback count",
                                     context.mknod_calls, 2);

    /* Rename and remove without optional filesystem callbacks */
    failures += mknod_fs_expect("rename mknod node",
                                 fs_test_rename("/node", "/renamed"), 0);
    failures += mknod_fs_stat_expect("/node", -MR_ENOENT, MR_INODE_NONE);
    failures += mknod_fs_stat_expect("/renamed", 0, MR_INODE_CHR);
    failures
        += mknod_fs_expect("unlink mknod node", fs_test_unlink("/renamed"), 0);
    failures += mknod_fs_stat_expect("/renamed", -MR_ENOENT, MR_INODE_NONE);
    failures += mknod_fs_size_expect("destroy mknod inode",
                                     context.destroy_calls, 1);
    failures += mknod_fs_expect("clear mknod inode",
                                context.last_inode == MR_NULL, MR_TRUE);

    /* Unmount test filesystem */
    failures += mknod_fs_expect("umount mknodfs", mr_fs_umount("/"), 0);
    __mknod_fs_context = MR_NULL;
    heap_after = mr_heap_information(MR_TRUE);
    failures += mknod_fs_size_expect(
        "mknod heap restored", heap_after.used_bytes, heap_before.used_bytes);
    return failures;
}

static void mknod(void) {
    MR_TEST_EXPECT_EQ_INT(fs_mknod_test(), 0);
}
MR_TEST_EXPORT(fs, mknod, mknod);
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
