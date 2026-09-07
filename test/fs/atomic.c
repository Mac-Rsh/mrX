/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-07-12        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS)
#include <mrX.h>
#include <mrX/test/fs/fs_test.h>

#define ATOMIC_FS_ENTRY_MAX (8)
#define ATOMIC_FS_NAME_MAX  (16)

typedef struct atomic_fs_entry {
    struct atomic_fs_entry *parent;
    char name[ATOMIC_FS_NAME_MAX];
    mr_inode_type_t type;
    mr_bool_t used;
} atomic_fs_entry_t;

typedef struct atomic_fs_context {
    atomic_fs_entry_t entries[ATOMIC_FS_ENTRY_MAX];
    mr_inode_t *last_inode;
    mr_size_t create_calls;
    mr_size_t rename_calls;
    mr_size_t destroy_calls;
    mr_bool_t create_error;
    mr_bool_t lookup_error;
    mr_bool_t unlink_error;
    mr_bool_t rename_error;
    mr_bool_t readdir_error;
} atomic_fs_context_t;

static atomic_fs_context_t *__atomic_fs_context;

static mr_err_t atomic_fs_inode_create(mr_inode_t *dir, const char *name,
                                       mr_inode_type_t type, mr_inode_t **out);
static mr_err_t atomic_fs_inode_unlink(mr_inode_t *dir, const char *name);
static mr_err_t atomic_fs_inode_lookup(mr_inode_t *dir, const char *name,
                                       mr_inode_t **out);
static mr_err_t atomic_fs_inode_rename(mr_inode_t *old_dir,
                                       mr_dentry_t *old_dentry,
                                       mr_inode_t *new_dir,
                                       mr_dentry_t *new_dentry);
static void atomic_fs_inode_destroy(mr_inode_t *inode);
static mr_err_t atomic_fs_file_readdir(mr_file_t *file, mr_dirent_t *dirent);
static mr_err_t atomic_fs_mount(mr_sblock_t *sb, const char *source,
                                int flags, void *data);
static void atomic_fs_unmount(mr_sblock_t *sb);
static mr_uint32_t atomic_fs_dentry_hash(const char *name, mr_uint32_t len);
static mr_bool_t atomic_fs_dentry_compare(const char *a, const char *b,
                                          mr_uint32_t len);

static const mr_dentry_ops_t __atomic_fs_dentry_ops = {
    .hash = atomic_fs_dentry_hash,
    .compare = atomic_fs_dentry_compare,
};

static const mr_inode_ops_t __atomic_fs_inode_ops = {
    .create = atomic_fs_inode_create,
    .lookup = atomic_fs_inode_lookup,
    .unlink = atomic_fs_inode_unlink,
    .rename = atomic_fs_inode_rename,
    .destroy = atomic_fs_inode_destroy,
};

static const mr_file_ops_t __atomic_fs_file_ops = {
    .readdir = atomic_fs_file_readdir,
};

static mr_file_system_t __atomic_fs = MR_FILE_SYSTEM_INIT(
    "atomicfs", atomic_fs_mount, atomic_fs_unmount);

static mr_uint8_t atomic_fs_dentry_fold(char ch) {
    if ((ch >= 'A') && (ch <= 'Z')) {
        return (mr_uint8_t)(ch + ('a' - 'A'));
    }
    return (mr_uint8_t)ch;
}

static mr_uint32_t atomic_fs_dentry_hash(const char *name, mr_uint32_t len) {
    mr_uint32_t hash;
    mr_uint32_t i;

    /* Hash names without ASCII case distinctions */
    hash = MR_DENTRY_HASH_SEED;
    for (i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) ^ atomic_fs_dentry_fold(name[i]);
    }
    return hash;
}

static mr_bool_t atomic_fs_dentry_compare(const char *a, const char *b,
                                          mr_uint32_t len) {
    mr_uint32_t i;

    /* Compare names without ASCII case distinctions */
    for (i = 0; i < len; i++) {
        if (atomic_fs_dentry_fold(a[i]) != atomic_fs_dentry_fold(b[i])) {
            return MR_FALSE;
        }
    }
    return MR_TRUE;
}

static void atomic_fs_expect(volatile mr_uint32_t *__test_status,
                             const char *name, mr_err_t actual,
                             mr_err_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
}

static void atomic_fs_stat_expect(volatile mr_uint32_t *__test_status,
                                  const char *path, mr_err_t expected) {
    mr_stat_t stat;

    atomic_fs_expect(__test_status, path, fs_test_stat(path, &stat), expected);
}

static void atomic_fs_bool_expect(volatile mr_uint32_t *__test_status,
                                  const char *name, mr_bool_t actual,
                                  mr_bool_t expected) {
    atomic_fs_expect(__test_status, name, actual, expected);
}

static void atomic_fs_size_expect(volatile mr_uint32_t *__test_status,
                                  const char *name, mr_size_t actual,
                                  mr_size_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_UINT(actual, expected);
}

static atomic_fs_entry_t *atomic_fs_entry_find(atomic_fs_context_t *context,
                                               atomic_fs_entry_t *parent,
                                               const char *name) {
    atomic_fs_entry_t *entry;
    mr_size_t i;

    for (i = 0; i < ATOMIC_FS_ENTRY_MAX; i++) {
        entry = &context->entries[i];
        if ((entry->used == MR_TRUE) && (entry->parent == parent)
            && (mr_strcmp(entry->name, name) == 0)) {
            return entry;
        }
    }
    return MR_NULL;
}

static atomic_fs_entry_t *atomic_fs_entry_alloc(atomic_fs_context_t *context) {
    mr_size_t i;

    for (i = 0; i < ATOMIC_FS_ENTRY_MAX; i++) {
        if (context->entries[i].used == MR_FALSE) {
            return &context->entries[i];
        }
    }
    return MR_NULL;
}

static mr_bool_t atomic_fs_has(atomic_fs_context_t *context, const char *name) {
    return atomic_fs_entry_find(context, MR_NULL, name) != MR_NULL;
}

static mr_bool_t atomic_fs_has_child(atomic_fs_context_t *context,
                                     const char *parent, const char *name) {
    atomic_fs_entry_t *entry;

    entry = atomic_fs_entry_find(context, MR_NULL, parent);
    if (!entry) {
        return MR_FALSE;
    }
    return atomic_fs_entry_find(context, entry, name) != MR_NULL;
}

static mr_err_t atomic_fs_inode_create(mr_inode_t *dir, const char *name,
                                       mr_inode_type_t type, mr_inode_t **out) {
    atomic_fs_context_t *context = dir->sb->priv;
    atomic_fs_entry_t *entry;
    mr_inode_t *inode;

    /* Init output */
    *out = MR_NULL;
    context->create_calls++;

    /* Inject callback failure */
    if (context->create_error) {
        return -MR_EIO;
    }

    /* Check name */
    if (mr_strlen(name) >= ATOMIC_FS_NAME_MAX) {
        return -MR_ENAMETOOLONG;
    }

    /* Create backing entry */
    entry = atomic_fs_entry_alloc(context);
    if (!entry) {
        return -MR_ENOMEM;
    }
    entry->parent = dir->priv;
    entry->type = type;
    entry->used = MR_TRUE;
    mr_strncpy(entry->name, name, sizeof(entry->name) - 1);
    entry->name[sizeof(entry->name) - 1] = '\0';

    /* Create inode */
    inode = mr_inode_create(type, &__atomic_fs_inode_ops, &__atomic_fs_file_ops,
                            entry);
    if (!inode) {
        entry->used = MR_FALSE;
        return -MR_ENOMEM;
    }

    context->last_inode = inode;
    *out = inode;
    return 0;
}

static mr_err_t atomic_fs_inode_unlink(mr_inode_t *dir, const char *name) {
    atomic_fs_context_t *context = dir->sb->priv;
    atomic_fs_entry_t *entry;

    /* Inject callback failure */
    if (context->unlink_error) {
        return -MR_EIO;
    }

    /* Remove backing entry */
    entry = atomic_fs_entry_find(context, dir->priv, name);
    if (!entry) {
        return -MR_ENOENT;
    }
    entry->used = MR_FALSE;
    return 0;
}

static mr_err_t atomic_fs_inode_lookup(mr_inode_t *dir, const char *name,
                                       mr_inode_t **out) {
    atomic_fs_context_t *context = dir->sb->priv;

    MR_IGNORE(name);

    /* Init output */
    *out = MR_NULL;

    /* Inject callback failure */
    if (context->lookup_error) {
        return -MR_EIO;
    }
    return -MR_ENOENT;
}

static mr_err_t atomic_fs_inode_rename(mr_inode_t *old_dir,
                                       mr_dentry_t *old_dentry,
                                       mr_inode_t *new_dir,
                                       mr_dentry_t *new_dentry) {
    atomic_fs_context_t *context = old_dir->sb->priv;
    atomic_fs_entry_t *entry;

    /* Count callback */
    context->rename_calls++;

    /* Inject callback failure */
    if (context->rename_error) {
        return -MR_EIO;
    }

    /* Check callback context */
    if ((!old_dentry->inode) || (!old_dentry->upper)
        || (old_dentry->upper->inode != old_dir) || new_dentry->inode
        || (!new_dentry->upper) || (new_dentry->upper->inode != new_dir)) {
        return -MR_EINVAL;
    }

    /* Check name */
    if (MR_DENTRY_NAME_LEN(new_dentry) >= ATOMIC_FS_NAME_MAX) {
        return -MR_ENAMETOOLONG;
    }

    /* Find backing entry */
    entry = atomic_fs_entry_find(context, old_dir->priv,
                                 MR_DENTRY_NAME(old_dentry));
    if ((!entry) || (old_dentry->inode->priv != entry)) {
        return -MR_ENOENT;
    }

    /* Move backing entry */
    entry->parent = new_dir->priv;
    mr_strncpy(entry->name, MR_DENTRY_NAME(new_dentry),
               sizeof(entry->name) - 1);
    entry->name[sizeof(entry->name) - 1] = '\0';

    return 0;
}

static void atomic_fs_inode_destroy(mr_inode_t *inode) {
    atomic_fs_context_t *context = inode->sb->priv;

    context->destroy_calls++;
    if (context->last_inode == inode) {
        context->last_inode = MR_NULL;
    }
}

static mr_err_t atomic_fs_file_readdir(mr_file_t *file, mr_dirent_t *dirent) {
    atomic_fs_context_t *context = file->inode->sb->priv;
    mr_dentry_t *child;
    mr_size_t index;

    /* Inject an error after one entry */
    if (context->readdir_error && (file->pos > 0)) {
        return -MR_EIO;
    }

    /* Get child dentry */
    index = (mr_size_t)file->pos;
    child = mr_dentry_child_get(file->dentry, index);
    if (!child) {
        return 0;
    }

    /* Read directory entry */
    mr_strncpy(dirent->name, MR_DENTRY_NAME(child), MR_CFG_FS_PATH_LEN - 1);
    dirent->name[MR_CFG_FS_PATH_LEN - 1] = '\0';
    dirent->type = child->inode ? child->inode->type : MR_INODE_NONE;
    mr_dentry_put(child);
    file->pos++;
    return 1;
}

static mr_err_t atomic_fs_mount(mr_sblock_t *sb, const char *source,
                                int flags, void *data) {
    atomic_fs_context_t *context;
    mr_inode_t *root;

    MR_IGNORE(source);
    MR_IGNORE(flags);
    MR_IGNORE(data);

    /* Create backing context */
    context = mr_calloc(1, sizeof(*context));
    if (!context) {
        return -MR_ENOMEM;
    }

    /* Create root inode */
    root = mr_inode_create(MR_INODE_DIR, &__atomic_fs_inode_ops,
                           &__atomic_fs_file_ops, MR_NULL);
    if (!root) {
        mr_free(context);
        return -MR_ENOMEM;
    }
    sb->priv = context;
    sb->root = root;
    sb->dops = &__atomic_fs_dentry_ops;
    __atomic_fs_context = context;
    return 0;
}

static void atomic_fs_unmount(mr_sblock_t *sb) {
    if (sb->priv) {
        mr_free(sb->priv);
        sb->priv = MR_NULL;
    }
    __atomic_fs_context = MR_NULL;
}

MR_TEST(fs, atomic) {
    atomic_fs_context_t *context;
    mr_file_t directory = MR_FILE_INIT(directory);
    mr_heap_info_t heap_before;
    mr_heap_info_t heap_after;
    mr_dirent_t dirent;
    mr_stat_t stat;
    mr_size_t destroy_calls;
    mr_err_t ret;

    /* Register test filesystem */
    ret = mr_file_system_register(&__atomic_fs);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    /* Record heap state */
    heap_before = mr_heap_information(MR_TRUE);

    /* Mount test filesystem */
    ret = fs_test_mount_root("atomicfs", 0, MR_NULL);
    atomic_fs_expect(__test_status, "mount atomicfs", ret, 0);
    if (ret != 0) {
        return;
    }
    context = __atomic_fs_context;

    /* Apply mount dentry operations at the global root */
    atomic_fs_expect(__test_status, "mkdir mixed-case",
                     mr_fs_mkdir("MixedCase"), 0);
    atomic_fs_stat_expect(__test_status, "mixedcase", 0);
    atomic_fs_expect(__test_status, "rmdir mixed-case",
                     mr_fs_rmdir("mixedcase"), 0);

    /* Fail create callback */
    context->create_error = MR_TRUE;
    ret = mr_fs_mkdir("createerror");
    context->create_error = MR_FALSE;
    atomic_fs_expect(__test_status, "create callback failure", ret, -MR_EIO);
    atomic_fs_bool_expect(__test_status, "backing createerror absent",
                          atomic_fs_has(context, "createerror"), MR_FALSE);
    atomic_fs_stat_expect(__test_status, "createerror", -MR_ENOENT);

    /* Preserve lookup callback errors */
    context->lookup_error = MR_TRUE;
    atomic_fs_stat_expect(__test_status, "lookuperror", -MR_EIO);
    context->lookup_error = MR_FALSE;

    /* Commit create */
    ret = mr_fs_mkdir("created");
    atomic_fs_expect(__test_status, "create commit", ret, 0);
    atomic_fs_bool_expect(__test_status, "backing created present",
                          atomic_fs_has(context, "created"), MR_TRUE);
    atomic_fs_stat_expect(__test_status, "created", 0);

    /* Keep one inode reference owned by the dentry */
    atomic_fs_expect(__test_status, "mkdir lifetime", mr_fs_mkdir("lifetime"),
                     0);
    atomic_fs_size_expect(
        __test_status, "created inode references",
        context->last_inode
            ? MR_REF_COUNT(context->last_inode->parent.magic_ref)
            : 0,
        1);
    destroy_calls = context->destroy_calls;
    atomic_fs_expect(__test_status, "rmdir lifetime",
                     mr_fs_rmdir("lifetime"), 0);
    atomic_fs_size_expect(__test_status, "destroy unlinked inode",
                          context->destroy_calls, destroy_calls + 1);
    atomic_fs_bool_expect(__test_status, "backing lifetime absent",
                          atomic_fs_has(context, "lifetime"), MR_FALSE);

    /* Create rename source */
    atomic_fs_expect(__test_status, "mkdir old", mr_fs_mkdir("old"), 0);

    /* Fail rename callback */
    context->rename_error = MR_TRUE;
    ret = mr_fs_rename("old", "renameerror");
    context->rename_error = MR_FALSE;
    atomic_fs_expect(__test_status, "rename callback failure", ret, -MR_EIO);
    atomic_fs_bool_expect(__test_status, "backing old preserved",
                          atomic_fs_has(context, "old"), MR_TRUE);
    atomic_fs_bool_expect(__test_status, "backing renameerror absent",
                          atomic_fs_has(context, "renameerror"), MR_FALSE);
    atomic_fs_stat_expect(__test_status, "old", 0);
    atomic_fs_stat_expect(__test_status, "renameerror", -MR_ENOENT);

    /* Commit rename */
    ret = mr_fs_rename("old", "renamed");
    atomic_fs_expect(__test_status, "rename commit", ret, 0);
    atomic_fs_bool_expect(__test_status, "backing old absent",
                          atomic_fs_has(context, "old"), MR_FALSE);
    atomic_fs_bool_expect(__test_status, "backing renamed present",
                          atomic_fs_has(context, "renamed"), MR_TRUE);
    atomic_fs_stat_expect(__test_status, "old", -MR_ENOENT);
    atomic_fs_stat_expect(__test_status, "renamed", 0);

    /* Rename and remove an open directory by path */
    atomic_fs_expect(__test_status, "mkdir fopen", mr_fs_mkdir("fopen"), 0);
    ret = mr_fs_open(&directory, "fopen", MR_O_RDONLY | MR_O_DIRECTORY);
    atomic_fs_expect(__test_status, "open rename directory", ret, 0);
    if (ret == 0) {
        atomic_fs_expect(__test_status, "stat open directory",
                         mr_fs_fstat(&directory, &stat), 0);
        atomic_fs_expect(__test_status, "open directory type", stat.type,
                         MR_INODE_DIR);

        context->rename_error = MR_TRUE;
        ret = mr_fs_rename("fopen", "frenameerror");
        context->rename_error = MR_FALSE;
        atomic_fs_expect(__test_status, "frename callback failure", ret,
                         -MR_EIO);
        atomic_fs_bool_expect(__test_status, "backing fopen preserved",
                              atomic_fs_has(context, "fopen"), MR_TRUE);
        atomic_fs_bool_expect(__test_status, "backing frenameerror absent",
                              atomic_fs_has(context, "frenameerror"), MR_FALSE);

        atomic_fs_expect(__test_status, "rename commit",
                         mr_fs_rename("fopen", "frenamed"), 0);
        atomic_fs_stat_expect(__test_status, "fopen", -MR_ENOENT);
        atomic_fs_stat_expect(__test_status, "frenamed", 0);

        context->unlink_error = MR_TRUE;
        ret = mr_fs_rmdir("frenamed");
        context->unlink_error = MR_FALSE;
        atomic_fs_expect(__test_status, "funlink callback failure", ret,
                         -MR_EIO);
        atomic_fs_stat_expect(__test_status, "frenamed", 0);
        ret = mr_fs_fstat(&directory, &stat);
        atomic_fs_expect(__test_status, "stat failed funlink directory", ret,
                         0);

        atomic_fs_expect(__test_status, "rmdir open directory",
                         mr_fs_rmdir("frenamed"), 0);
        atomic_fs_stat_expect(__test_status, "frenamed", -MR_ENOENT);
        ret = mr_fs_fstat(&directory, &stat);
        atomic_fs_expect(__test_status, "stat unlinked directory", ret, 0);
        atomic_fs_expect(__test_status, "close unlinked directory",
                         mr_fs_close(&directory), 0);
    }

    /* Read an empty directory */
    ret = mr_fs_open(&directory, "created", MR_O_RDONLY | MR_O_DIRECTORY);
    atomic_fs_expect(__test_status, "open empty directory", ret, 0);
    if (ret == 0) {
        atomic_fs_expect(__test_status, "read empty directory",
                         mr_fs_readdir(&directory, &dirent), 0);
        atomic_fs_expect(__test_status, "close empty directory",
                         mr_fs_close(&directory), 0);
    }

    /* Commit a cross-directory rename */
    atomic_fs_expect(__test_status, "mkdir from", mr_fs_mkdir("from"), 0);
    atomic_fs_expect(__test_status, "mkdir to", mr_fs_mkdir("to"), 0);
    atomic_fs_expect(__test_status, "mkdir from/item", mr_fs_mkdir("from/item"),
                     0);
    atomic_fs_expect(__test_status, "rename across directories",
                     mr_fs_rename("from/item", "to/moved"), 0);
    atomic_fs_bool_expect(__test_status, "backing from/item absent",
                          atomic_fs_has_child(context, "from", "item"),
                          MR_FALSE);
    atomic_fs_bool_expect(__test_status, "backing to/moved present",
                          atomic_fs_has_child(context, "to", "moved"), MR_TRUE);
    atomic_fs_stat_expect(__test_status, "from/item", -MR_ENOENT);
    atomic_fs_stat_expect(__test_status, "to/moved", 0);

    /* Distinguish directory entries, EOF, and callback errors */
    ret = mr_fs_open(&directory, "to", MR_O_RDONLY | MR_O_DIRECTORY);
    atomic_fs_expect(__test_status, "open non-empty directory", ret, 0);
    if (ret == 0) {
        atomic_fs_expect(__test_status, "read directory entry",
                         mr_fs_readdir(&directory, &dirent), 1);
        atomic_fs_expect(__test_status, "read directory EOF",
                         mr_fs_readdir(&directory, &dirent), 0);
        atomic_fs_expect(__test_status, "close non-empty directory",
                         mr_fs_close(&directory), 0);
    }

    ret = mr_fs_open(&directory, "/", MR_O_RDONLY | MR_O_DIRECTORY);
    atomic_fs_expect(__test_status, "open failing directory", ret, 0);
    if (ret == 0) {
        context->readdir_error = MR_TRUE;
        atomic_fs_expect(__test_status, "read before directory error",
                         mr_fs_readdir(&directory, &dirent), 1);
        atomic_fs_expect(__test_status, "propagate directory error",
                         mr_fs_readdir(&directory, &dirent), -MR_EIO);
        context->readdir_error = MR_FALSE;
        atomic_fs_expect(__test_status, "close failing directory",
                         mr_fs_close(&directory), 0);
    }

    /* An open root handle blocks graceful unmount */
    ret = mr_fs_open(&directory, "/", MR_O_RDONLY | MR_O_DIRECTORY);
    atomic_fs_expect(__test_status, "open root directory", ret, 0);
    if (ret == 0) {
        atomic_fs_expect(__test_status, "reject umount with open root",
                         mr_fs_umount("/"), -MR_EBUSY);
        atomic_fs_expect(__test_status, "stat root after busy umount",
                         mr_fs_fstat(&directory, &stat), 0);
        atomic_fs_expect(__test_status, "close root directory",
                         mr_fs_close(&directory), 0);
    }
    atomic_fs_expect(__test_status, "umount atomicfs after close",
                     mr_fs_umount("/"), 0);

    /* A replacement mount uses its own case-sensitive dentry operations */
    atomic_fs_expect(__test_status, "mount replacement ramfs",
                     fs_test_mount_root("ramfs", 0, MR_NULL), 0);
    atomic_fs_expect(__test_status, "mkdir replacement entry",
                     mr_fs_mkdir("replacement"), 0);
    atomic_fs_expect(__test_status, "mkdir replacement upper-case",
                     mr_fs_mkdir("CaseName"), 0);
    atomic_fs_expect(__test_status, "mkdir replacement lower-case",
                     mr_fs_mkdir("casename"), 0);
    atomic_fs_expect(__test_status, "umount replacement ramfs",
                     mr_fs_umount("/"), 0);
    atomic_fs_expect(__test_status, "restore rootfs",
                     fs_test_mount_root("rootfs", 0, MR_NULL), 0);
    heap_after = mr_heap_information(MR_TRUE);
    atomic_fs_size_expect(__test_status, "heap restored", heap_after.used_bytes,
                          heap_before.used_bytes);
    return;
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */
