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
#include <mrX/test/fs/fs_test.h>

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
static mr_err_t mknod_fs_mount(mr_sblock_t *sb, const char *source,
                               int flags, void *data);

static mr_err_t mknod_fs_inode_create_only(mr_inode_t *dir, const char *name,
                                           mr_inode_type_t type,
                                           mr_inode_t **out);
static mr_err_t mknod_fs_create_only_mount(mr_sblock_t *sb,
                                           const char *source, int flags,
                                           void *data);

static const mr_inode_ops_t __mknod_fs_root_inode_ops = {
    .create = mknod_fs_inode_create,
    .mknod = mknod_fs_inode_mknod,
};

static const mr_inode_ops_t __mknod_fs_create_only_root_ops = {
    .create = mknod_fs_inode_create_only,
};

static const mr_inode_ops_t __mknod_fs_node_inode_ops = {
    .destroy = mknod_fs_inode_destroy,
};

static const mr_file_ops_t __mknod_fs_file_ops;

static mr_file_system_t __mknod_fs
    = MR_FILE_SYSTEM_INIT("mknodfs", mknod_fs_mount, MR_NULL);
static mr_file_system_t __mknod_create_only_fs
    = MR_FILE_SYSTEM_INIT("mknodcreatefs", mknod_fs_create_only_mount,
                          MR_NULL);

static void mknod_fs_expect(volatile mr_uint32_t *__test_status,
                            const char *name, mr_int64_t actual,
                            mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

static void mknod_fs_size_expect(volatile mr_uint32_t *__test_status,
                                 const char *name, mr_size_t actual,
                                 mr_size_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_UINT(actual, expected);
}

static void mknod_fs_stat_expect(volatile mr_uint32_t *__test_status,
                                 const char *path, mr_err_t expected,
                                 mr_inode_type_t type) {
    mr_stat_t stat;
    mr_err_t ret;

    ret = fs_test_stat(path, &stat);
    mknod_fs_expect(__test_status, path, ret, expected);
    if ((ret == 0) && (stat.type != type)) {
        MR_TEST_EXPECT_EQ_INT(stat.type, type);
    }
    return;
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

static mr_err_t mknod_fs_inode_create_only(mr_inode_t *dir, const char *name,
                                           mr_inode_type_t type,
                                           mr_inode_t **out) {
    mknod_fs_context_t *context = dir->sb->priv;
    mr_inode_t *inode;

    /* Record that VFS fell back to create */
    *out = MR_NULL;
    context->create_calls++;
    context->last_type = type;
    mr_strncpy(context->last_name, name, sizeof(context->last_name) - 1);
    context->last_name[sizeof(context->last_name) - 1] = '\0';

    /* Create does not receive priv, so the inode cannot borrow it */
    inode = mr_inode_create(type, &__mknod_fs_node_inode_ops,
                            &__mknod_fs_file_ops, MR_NULL);
    if (!inode) {
        return -MR_ENOMEM;
    }
    context->last_inode = inode;
    *out = inode;
    return 0;
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

static mr_err_t mknod_fs_mount(mr_sblock_t *sb, const char *source,
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

static mr_err_t mknod_fs_create_only_mount(mr_sblock_t *sb,
                                           const char *source, int flags,
                                           void *data) {
    mknod_fs_context_t *context = data;
    mr_inode_t *root;

    MR_IGNORE(source);
    MR_IGNORE(flags);

    /* Check mount data */
    if (!context) {
        return -MR_EINVAL;
    }

    /* Create root inode without a mknod callback */
    root = mr_inode_create(MR_INODE_DIR, &__mknod_fs_create_only_root_ops,
                           MR_NULL, MR_NULL);
    if (!root) {
        return -MR_ENOMEM;
    }
    sb->priv = context;
    sb->root = root;
    __mknod_fs_context = context;
    return 0;
}

MR_TEST(fs, mknod) {
    mknod_fs_context_t fallback_context = {0};
    mknod_fs_context_t context = {0};
    mr_file_t fallback_file = MR_FILE_INIT(fallback_file);
    mr_heap_info_t heap_before;
    mr_heap_info_t heap_after;
    int object;
    mr_err_t ret;

    /* Register test filesystems */
    ret = mr_file_system_register(&__mknod_create_only_fs);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_file_system_register(&__mknod_fs);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    /* Record heap state */
    heap_before = mr_heap_information(MR_TRUE);

    /* Fall back to create when mknod is not implemented */
    ret = fs_test_mount_root("mknodcreatefs", 0, &fallback_context);
    mknod_fs_expect(__test_status, "mknod mount create-only fs", ret, 0);
    if (ret == 0) {
        ret = mr_fs_mknod("/fallback", MR_INODE_CHR, &object);
        mknod_fs_expect(__test_status, "fallback mknod to create", ret, 0);
        if (ret == 0) {
            mknod_fs_stat_expect(__test_status, "/fallback", 0, MR_INODE_CHR);
            ret = mr_fs_open(&fallback_file, "/fallback", MR_O_RDONLY);
            mknod_fs_expect(__test_status, "open fallback node", ret, 0);
            if (ret == 0) {
                mknod_fs_expect(__test_status, "fallback ignored private data",
                                fallback_file.inode->priv != &object, MR_TRUE);
                mknod_fs_expect(__test_status, "close fallback node",
                                mr_fs_close(&fallback_file), 0);
            }
            mknod_fs_expect(__test_status, "unlink fallback node",
                            mr_fs_unlink("/fallback"), 0);
        }
        mknod_fs_expect(__test_status, "mknod umount create-only fs",
                        mr_fs_umount("/"), 0);
        __mknod_fs_context = MR_NULL;
    }

    /* Mount test filesystem */
    ret = fs_test_mount_root("mknodfs", 0, &context);
    mknod_fs_expect(__test_status, "mount mknodfs", ret, 0);
    if (ret != 0) {
        return;
    }

    /* Reject requests before invoking the filesystem */
    mknod_fs_expect(__test_status, "reject invalid mknod type",
                    mr_fs_mknod("/invalid", MR_INODE_NONE, &object),
                    -MR_EINVAL);
    mknod_fs_expect(__test_status, "reject missing mknod parent",
                    mr_fs_mknod("/missing/node", MR_INODE_CHR, &object),
                    -MR_ENOENT);
    mknod_fs_size_expect(__test_status, "mknod callback not called",
                         context.mknod_calls, 0);
    mknod_fs_size_expect(__test_status, "create callback not called",
                         context.create_calls, 0);

    /* Roll back callback failure */
    context.mknod_error = MR_TRUE;
    ret = mr_fs_mknod("/failed", MR_INODE_CHR, &object);
    context.mknod_error = MR_FALSE;
    mknod_fs_expect(__test_status, "propagate mknod error", ret, -MR_EIO);
    mknod_fs_stat_expect(__test_status, "/failed", -MR_ENOENT, MR_INODE_NONE);
    mknod_fs_size_expect(__test_status, "failed mknod callback count",
                         context.mknod_calls, 1);
    mknod_fs_size_expect(__test_status, "failed mknod did not fall back",
                         context.create_calls, 0);

    /* Publish a node */
    ret = mr_fs_mknod("/node", MR_INODE_CHR, &object);
    mknod_fs_expect(__test_status, "create mknod node", ret, 0);
    mknod_fs_size_expect(__test_status, "mknod callback count",
                         context.mknod_calls, 2);
    mknod_fs_expect(__test_status, "mknod private data",
                    context.last_priv == &object, MR_TRUE);
    mknod_fs_expect(__test_status, "mknod inode type", context.last_type,
                    MR_INODE_CHR);
    mknod_fs_expect(__test_status, "mknod inode private data",
                    context.last_inode && (context.last_inode->priv == &object),
                    MR_TRUE);
    mknod_fs_expect(__test_status, "mknod inode super-block",
                    context.last_inode && (context.last_inode->sb != MR_NULL),
                    MR_TRUE);
    if (mr_strcmp(context.last_name, "node") != 0) {
        MR_TEST_EXPECT_EQ_INT(mr_strcmp(context.last_name, "node"), 0);
    }
    mknod_fs_stat_expect(__test_status, "/node", 0, MR_INODE_CHR);

    /* Reject duplicate names before invoking the filesystem */
    mknod_fs_expect(__test_status, "reject duplicate mknod",
                    mr_fs_mknod("/node", MR_INODE_BLK, MR_NULL), -MR_EEXIST);
    mknod_fs_size_expect(__test_status, "duplicate mknod callback count",
                         context.mknod_calls, 2);

    /* Rename and remove without optional filesystem callbacks */
    mknod_fs_expect(__test_status, "rename mknod node",
                    mr_fs_rename("/node", "/renamed"), 0);
    mknod_fs_stat_expect(__test_status, "/node", -MR_ENOENT, MR_INODE_NONE);
    mknod_fs_stat_expect(__test_status, "/renamed", 0, MR_INODE_CHR);
    mknod_fs_expect(__test_status, "unlink mknod node",
                    mr_fs_unlink("/renamed"), 0);
    mknod_fs_stat_expect(__test_status, "/renamed", -MR_ENOENT, MR_INODE_NONE);
    mknod_fs_size_expect(__test_status, "destroy mknod inode",
                         context.destroy_calls, 1);
    mknod_fs_expect(__test_status, "clear mknod inode",
                    context.last_inode == MR_NULL, MR_TRUE);

    /* Unmount test filesystem */
    mknod_fs_expect(__test_status, "umount mknodfs", mr_fs_umount("/"), 0);
    __mknod_fs_context = MR_NULL;
    heap_after = mr_heap_information(MR_TRUE);
    mknod_fs_size_expect(__test_status, "mknod heap restored",
                         heap_after.used_bytes, heap_before.used_bytes);
    return;
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
