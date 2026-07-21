/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/fs/mr_fs.h>
#if defined(MR_USE_FS)
#include <mrX/kernel/mr_spinlock.h>
#include <mrX/libc/mr_string.h>

/* File-system clazzes(static only) */
static void file_system_statik_release(mr_object_t *obj);
MR_CLAZZES_DEFINE(file_system, file_system_statik_release, MR_NULL, MR_NULL);

/* File-system list lock */
static mr_spinlock_t __fs_lock = MR_SPINLOCK_INIT();
/* File-system list */
static mr_file_system_t *__fs_list = MR_NULL;

static mr_err_t file_system_init(mr_file_system_t *fs, const mr_clazz_t *clazz,
                                 const char *name,
                                 mr_file_system_mount_t *mount,
                                 mr_file_system_kill_sb_t *kill_sb) {
    /* Init file-system */
    fs->name = name;
    fs->next = MR_NULL;
    fs->mount = mount;
    fs->kill_sb = kill_sb;

    /* Init object */
    return mr_object_init((mr_object_t *)fs, clazz);
}

mr_err_t mr_file_system_init(mr_file_system_t *fs, const char *name,
                             mr_file_system_mount_t *mount,
                             mr_file_system_kill_sb_t *kill_sb) {
    const mr_clazz_t *clazz = MR_CLAZZES_STATIK(file_system);

    /* Check parameter */
    MR_ASSERT(fs != MR_NULL);
    MR_ASSERT(mount != MR_NULL);
    MR_ASSERT(name != MR_NULL);

    /* Init file-system */
    return file_system_init(fs, clazz, name, mount, kill_sb);
}

static mr_bool_t file_system_is_registered(mr_file_system_t *fs) {
    mr_file_system_t *cur;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__fs_lock);

    /* Check if file-system already registered */
    for (cur = __fs_list; cur; cur = cur->next) {
        if (cur == fs) {
            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__fs_lock, mask);

            /* Already mounted */
            return MR_TRUE;
        }
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__fs_lock, mask);
    return MR_FALSE;
}

mr_err_t mr_file_system_del(mr_file_system_t *fs) {
    /* Check parameter */
    MR_ASSERT((fs != MR_NULL) && MR_OBJECT_IS_INITED(fs));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(fs, file_system));

    /* Check if file-system already registered */
    if (file_system_is_registered(fs)) {
        return -MR_EBUSY;
    }

    /* Delete object */
    mr_object_del((mr_object_t *)fs);
    return 0;
}

static mr_err_t file_system_add(mr_file_system_t *fs) {
    mr_file_system_t *existing;
    mr_file_system_t **link;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__fs_lock);

    /* Check if file-system already registered */
    for (link = &__fs_list; *link; link = &(*link)->next) {
        existing = *link;

        /* Already registered */
        if (mr_strcmp(existing->name, fs->name) == 0) {
            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__fs_lock, mask);
            return -MR_EEXIST;
        }
    }

    /* Add to the list */
    fs->next = MR_NULL;
    *link = fs;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__fs_lock, mask);
    return 0;
}

mr_err_t mr_file_system_register(mr_file_system_t *fs) {
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((fs != MR_NULL) && MR_OBJECT_IS_INITED(fs));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(fs, file_system));

    /* Get file-system */
    fs = mr_file_system_get(fs);
    if (!fs) {
        return -MR_ENOENT;
    }

    /* Add file-system */
    ret = file_system_add(fs);
    if (ret != 0) {
        /* Put file-system */
        mr_file_system_put(fs);
        return ret;
    }
    return 0;
}

static mr_err_t file_system_remove(mr_file_system_t *fs) {
    mr_file_system_t **link;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__fs_lock);

    /* Find the registered entry by address. A never-registered object may have
     * an arbitrary next pointer, so do not trust fs->next here */
    for (link = &__fs_list; *link; link = &(*link)->next) {
        if (*link == fs) {
            break;
        }
    }

    /* Not registered */
    if (!(*link)) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__fs_lock, mask);
        return -MR_ENOENT;
    }

    /* Remove from the list */
    *link = fs->next;
    fs->next = MR_NULL;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__fs_lock, mask);
    return 0;
}

mr_err_t mr_file_system_unregister(mr_file_system_t *fs) {
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((fs != MR_NULL) && MR_OBJECT_IS_INITED(fs));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(fs, file_system));

    /* Remove file-system */
    ret = file_system_remove(fs);
    if (ret != 0) {
        return ret;
    }

    /* Put file-system */
    mr_file_system_put(fs);
    return 0;
}

mr_file_system_t *mr_file_system_find(const char *name) {
    mr_file_system_t *fs;
    int mask;

    /* Check parameter */
    MR_ASSERT(name);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__fs_lock);

    /* Find the file-system */
    for (fs = __fs_list; fs; fs = fs->next) {
        if (mr_strcmp(fs->name, name) == 0) {
            /* Get file-system */
            fs = mr_file_system_get(fs);

            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__fs_lock, mask);
            return fs;
        }
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__fs_lock, mask);
    return MR_NULL;
}

static void file_system_statik_release(mr_object_t *obj) {
    mr_file_system_t *fs;

    /* Release file-system */
    fs = MR_CONTAINER_OF(obj, mr_file_system_t, parent);

    /* Detach from the list if still linked */
    (void)file_system_remove(fs);
}
#endif /* defined(MR_USE_FS) */
