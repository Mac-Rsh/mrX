/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-14        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS)
#include <mrX.h>
#include "fs_test.h"

/* Mock FS behavior flags */
typedef struct mock_error_context {
    mr_size_t lookup_calls;
    mr_size_t create_calls;
    mr_size_t destroy_calls;
    mr_bool_t lookup_null;
    mr_bool_t create_wrong_type;
    mr_bool_t trunc_error;
} mock_error_context_t;

static mock_error_context_t *__mock_ctx;

static mr_err_t mock_error_inode_create(mr_inode_t *dir, const char *name,
                                        mr_inode_type_t type,
                                        mr_inode_t **out);
static mr_err_t mock_error_inode_lookup(mr_inode_t *dir, const char *name,
                                        mr_inode_t **out);
static mr_err_t mock_error_inode_stat(mr_inode_t *inode, mr_stat_t *stat);
static mr_err_t mock_error_inode_truncate(mr_inode_t *inode, mr_file_t *file,
                                          mr_int64_t length);
static void mock_error_inode_destroy(mr_inode_t *inode);
static mr_ssize_t mock_error_file_read(mr_file_t *file, void *buf,
                                       mr_size_t size);
static mr_ssize_t mock_error_file_write(mr_file_t *file, const void *buf,
                                        mr_size_t size);
static mr_err_t mock_error_mount(mr_super_block_t *sb, const char *source,
                                 int flags, void *data);

static const mr_inode_ops_t __mock_error_inode_ops = {
    .create = mock_error_inode_create,
    .lookup = mock_error_inode_lookup,
    .stat = mock_error_inode_stat,
    .truncate = mock_error_inode_truncate,
    .destroy = mock_error_inode_destroy,
};

static const mr_file_ops_t __mock_error_file_ops = {
    .read = mock_error_file_read,
    .write = mock_error_file_write,
};

static mr_file_system_t __mock_error_fs
    = MR_FILE_SYSTEM_INIT("errfs", mock_error_mount, MR_NULL, MR_NULL);

static int mock_error_expect(const char *name, mr_int64_t actual,
                             mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
    return (actual == expected) ? 0 : 1;
}

static mr_err_t mock_error_inode_create(mr_inode_t *dir, const char *name,
                                        mr_inode_type_t type,
                                        mr_inode_t **out) {
    mock_error_context_t *context = dir->sb->priv;
    mr_inode_type_t actual_type;
    mr_inode_t *inode;

    MR_IGNORE(name);

    *out = MR_NULL;
    context->create_calls++;

    actual_type = type;
    if (context->create_wrong_type && (type == MR_INODE_DIR)) {
        actual_type = MR_INODE_REG;
    }
    inode = mr_inode_create(actual_type, &__mock_error_inode_ops,
                            &__mock_error_file_ops, MR_NULL);
    if (!inode) {
        return -MR_ENOMEM;
    }
    *out = inode;
    return 0;
}

static mr_err_t mock_error_inode_lookup(mr_inode_t *dir, const char *name,
                                        mr_inode_t **out) {
    mock_error_context_t *context = dir->sb->priv;
    mr_inode_t *inode;

    *out = MR_NULL;
    context->lookup_calls++;

    if (context->lookup_null) {
        return 0;
    }
    if (mr_strcmp(name, "ghost") != 0) {
        return -MR_ENOENT;
    }
    inode = mr_inode_create(MR_INODE_REG, &__mock_error_inode_ops,
                            &__mock_error_file_ops, MR_NULL);
    if (!inode) {
        return -MR_ENOMEM;
    }
    *out = inode;
    return 0;
}

static mr_err_t mock_error_inode_stat(mr_inode_t *inode, mr_stat_t *stat) {
    stat->type = inode->type;
    stat->mode = 0;
    stat->nlink = inode->nlink;
    stat->size = inode->size;
    return 0;
}

static mr_err_t mock_error_inode_truncate(mr_inode_t *inode, mr_file_t *file,
                                          mr_int64_t length) {
    mock_error_context_t *context = inode->sb->priv;

    MR_IGNORE(file);

    if (context->trunc_error) {
        return -MR_EIO;
    }
    inode->size = length;
    return 0;
}

static void mock_error_inode_destroy(mr_inode_t *inode) {
    MR_IGNORE(inode);

    if (__mock_ctx) {
        __mock_ctx->destroy_calls++;
    }
}

static mr_ssize_t mock_error_file_read(mr_file_t *file, void *buf,
                                       mr_size_t size) {
    MR_IGNORE(buf);

    file->pos += (mr_int64_t)size;
    if (file->pos > file->inode->size) {
        file->pos = file->inode->size;
    }
    return (mr_ssize_t)size;
}

static mr_ssize_t mock_error_file_write(mr_file_t *file, const void *buf,
                                        mr_size_t size) {
    MR_IGNORE(buf);

    file->pos += (mr_int64_t)size;
    if (file->pos > file->inode->size) {
        file->inode->size = file->pos;
    }
    return (mr_ssize_t)size;
}

static mr_err_t mock_error_mount(mr_super_block_t *sb, const char *source,
                                 int flags, void *data) {
    mock_error_context_t *context;

    MR_IGNORE(source);
    MR_IGNORE(flags);
    MR_IGNORE(data);

    sb->root = mr_inode_create(MR_INODE_DIR, &__mock_error_inode_ops,
                               &__mock_error_file_ops, MR_NULL);
    if (!sb->root) {
        return -MR_ENOMEM;
    }
    context = mr_malloc(sizeof(mock_error_context_t));
    if (!context) {
        return -MR_ENOMEM;
    }
    mr_memset(context, 0, sizeof(mock_error_context_t));
    sb->priv = context;
    __mock_ctx = context;
    return 0;
}

static int fs_mock_error_test(void) {
    mr_file_t file = MR_FILE_INIT(file);
    mock_error_context_t *context;
    char buffer[8];
    mr_stat_t stat;
    int failures = 0;
    mr_err_t ret;

    /* Register the mock FS */
    failures += mock_error_expect("register errfs",
                                  mr_file_system_register(&__mock_error_fs),
                                  0);

    /* Mount the mock FS */
    ret = mr_fs_mount(MR_NULL, "/", "errfs", 0, MR_NULL);
    failures += mock_error_expect("mount errfs", ret, 0);
    if (ret != 0) {
        return failures;
    }
    context = __mock_ctx;

    /* A cache miss is filled by the lookup callback */
    failures += mock_error_expect("stat ghost", fs_test_stat("/ghost", &stat),
                                  0);
    failures += mock_error_expect("ghost lookup calls",
                                  (mr_int64_t)context->lookup_calls, 1);
    failures += mock_error_expect("destroy calls",
                                  (mr_int64_t)context->destroy_calls, 0);

    /* Read/write through the mock op layers, incl. stat callback */
    ret = mr_fs_open(&file, "/data", MR_O_RDWR | MR_O_CREAT);
    failures += mock_error_expect("open errfs data", ret, 0);
    if (ret == 0) {
        failures += mock_error_expect("write errfs data",
                                      mr_fs_write(&file, "abc", 3), 3);
        failures += mock_error_expect("truncate errfs data 4",
                                      mr_fs_truncate(&file, 4), 0);
        failures += mock_error_expect("read errfs data",
                                      mr_fs_read(&file, buffer, 0), 0);
        ret = mr_fs_stat(&file, &stat);
        failures += mock_error_expect("stat errfs data", ret, 0);
        if (ret == 0) {
            failures += mock_error_expect("errfs data size", stat.size, 4);
        }
        failures += mock_error_expect("close errfs data",
                                      mr_fs_close(&file), 0);
    }

    /* O_TRUNC failure after open must not leave the handle open */
    context->trunc_error = MR_TRUE;
    ret = mr_fs_open(&file, "/data", MR_O_RDWR | MR_O_TRUNC);
    context->trunc_error = MR_FALSE;
    failures += mock_error_expect("open trunc fail", ret, -MR_EIO);
    failures += mock_error_expect("trunc fail handle closed",
                                  file.inode == MR_NULL, MR_TRUE);
    failures += mock_error_expect("stat after trunc fail",
                                  fs_test_stat("/data", &stat), 0);
    failures += mock_error_expect("size after trunc fail", stat.size, 4);

    /* Rename/unlink stay within the dentry namespace layer */
    failures += mock_error_expect("mkdir errfs dir", mr_fs_mkdir("/dir"), 0);
    failures += mock_error_expect("rename dir",
                                  fs_test_rename("/dir", "/dir2"), 0);
    failures += mock_error_expect("unlink dir2",
                                  fs_test_unlink("/dir2"), 0);
    failures += mock_error_expect("umount errfs", mr_fs_umount("/"), 0);

    /* A create callback returning the wrong inode type -> -MR_EINVAL */
    ret = mr_fs_mount(MR_NULL, "/", "errfs", 0, MR_NULL);
    failures += mock_error_expect("mount wrong-type", ret, 0);
    if (ret == 0) {
        context = __mock_ctx;
        context->create_wrong_type = MR_TRUE;
        failures += mock_error_expect("create wrong type",
                                      mr_fs_mkdir("/bad"), -MR_EINVAL);
        failures += mock_error_expect("wrong type umount",
                                      mr_fs_umount("/"), 0);
    }

    /* A lookup callback returning a NULL inode -> -MR_EINVAL */
    ret = mr_fs_mount(MR_NULL, "/", "errfs", 0, MR_NULL);
    failures += mock_error_expect("mount bad-lookup", ret, 0);
    if (ret == 0) {
        context = __mock_ctx;
        context->lookup_null = MR_TRUE;
        failures += mock_error_expect("open bad lookup",
                                      fs_test_stat("/ghost", &stat),
                                      -MR_EINVAL);
        failures += mock_error_expect("bad lookup umount",
                                      mr_fs_umount("/"), 0);
    }

    /* Final mount/umount round */
    ret = mr_fs_mount(MR_NULL, "/", "errfs", 0, MR_NULL);
    failures += mock_error_expect("remount errfs final", ret, 0);
    if (ret == 0) {
        failures += mock_error_expect("errfs final umount",
                                      mr_fs_umount("/"), 0);
    }
    return failures;
}

static void mock_errors(void) {
    MR_TEST_EXPECT_EQ_INT(fs_mock_error_test(), 0);
}
MR_TEST_EXPORT(fs, mock_errors, mock_errors);
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
