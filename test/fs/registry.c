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

/* Mock FS that returns a non-directory root from mount */
static mr_err_t registry_badroot_mount(mr_super_block_t *sb,
                                       const char *source, int flags,
                                       void *data);
static mr_err_t registry_good_mount(mr_super_block_t *sb, const char *source,
                                    int flags, void *data);
static void registry_good_kill_sb(mr_super_block_t *sb);

static mr_file_system_t __registry_badroot_fs
    = MR_FILE_SYSTEM_INIT("badrootfs", registry_badroot_mount, MR_NULL,
                          MR_NULL);
static mr_file_system_t __registry_good_fs = MR_FILE_SYSTEM_INIT(
    "registryfs", registry_good_mount, MR_NULL, registry_good_kill_sb);

static mr_size_t __registry_kill_calls;

static int registry_expect(const char *name, mr_int64_t actual,
                           mr_int64_t expected) {
    MR_IGNORE(name);

    MR_TEST_EXPECT_EQ_INT(actual, expected);
    return (actual == expected) ? 0 : 1;
}

static mr_err_t registry_badroot_mount(mr_super_block_t *sb,
                                       const char *source, int flags,
                                       void *data) {
    MR_IGNORE(source);
    MR_IGNORE(flags);
    MR_IGNORE(data);

    sb->root = mr_inode_create(MR_INODE_REG, MR_NULL, MR_NULL, MR_NULL);
    return sb->root ? 0 : -MR_ENOMEM;
}

static mr_err_t registry_good_mount(mr_super_block_t *sb, const char *source,
                                    int flags, void *data) {
    MR_IGNORE(source);
    MR_IGNORE(flags);
    MR_IGNORE(data);

    sb->root = mr_inode_create(MR_INODE_DIR, MR_NULL, MR_NULL, MR_NULL);
    return sb->root ? 0 : -MR_ENOMEM;
}

static void registry_good_kill_sb(mr_super_block_t *sb) {
    MR_IGNORE(sb);

    __registry_kill_calls++;
}

static int fs_registry_test(void) {
    mr_file_system_t *found;
    mr_dentry_t *dentry;
    mr_inode_t *inode;
    int failures = 0;
    mr_err_t ret;

    /* Register the mock file-systems */
    ret = mr_file_system_register(&__registry_badroot_fs);
    failures += registry_expect("register badrootfs", ret, 0);
    ret = mr_file_system_register(&__registry_good_fs);
    failures += registry_expect("register registryfs", ret, 0);

    /* Duplicate name or pointer is rejected */
    ret = mr_file_system_register(&__registry_good_fs);
    failures += registry_expect("duplicate pointer", ret, -MR_EEXIST);
    ret = mr_file_system_register(&__registry_badroot_fs);
    failures += registry_expect("duplicate name", ret, -MR_EEXIST);

    /* Linear registration: name lookup */
    found = mr_file_system_find("registryfs");
    failures += registry_expect("find registryfs", found != MR_NULL, MR_TRUE);
    found = mr_file_system_find("no-such-fs");
    failures += registry_expect("find missing", found == MR_NULL, MR_TRUE);

    /* A mount whose root inode is not a directory raises -ENOTDIR */
    ret = mr_fs_mount(MR_NULL, "/", "badrootfs", 0, MR_NULL);
    failures += registry_expect("mount badrootfs", ret, -MR_ENOTDIR);

    /* A well-formed mount completes and kill_sb runs on unmount */
    __registry_kill_calls = 0;
    ret = mr_fs_mount(MR_NULL, "/", "registryfs", 0, MR_NULL);
    failures += registry_expect("mount registryfs", ret, 0);
    if (ret != 0) {
        return failures;
    }
    failures += registry_expect("unmount registryfs",
                                mr_fs_umount("/"), 0);
    failures += registry_expect("kill_sb called",
                                (mr_int64_t)__registry_kill_calls, 1);

    /* Static dentry lifecycle */
    dentry = mr_malloc(sizeof(mr_dentry_t));
    if (dentry) {
        ret = mr_dentry_init(dentry, MR_NULL, "%s", "static-name");
        failures += registry_expect("static dentry init", ret, 0);
        if (ret == 0) {
            failures += registry_expect("static dentry del",
                                        mr_dentry_del(dentry), 0);
        }
        mr_free(dentry);
    }

    /* Static inode lifecycle */
    inode = mr_malloc(sizeof(mr_inode_t));
    if (inode) {
        ret = mr_inode_init(inode, MR_INODE_REG, MR_NULL, MR_NULL, MR_NULL);
        failures += registry_expect("static inode init", ret, 0);
        if (ret == 0) {
            mr_inode_put(inode);
        }
        mr_free(inode);
    }

    return failures;
}

static void registry(void) {
    MR_TEST_EXPECT_EQ_INT(fs_registry_test(), 0);
}
MR_TEST_EXPORT(fs, registry, registry);
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */