/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/fs/mr_fs.h>
#if defined(MR_USE_FS)
#include <mrX/libc/mr_string.h>
#include <mrX/libc/mr_assert.h>
#include <mrX/fs/mr_nsp.h>

/* File-system list */
static mr_file_system_t *__fs_list = MR_NULL;

mr_err_t mr_file_system_register(mr_file_system_t *fs) {
    mr_file_system_t **link;
    int mask;

    /* Check parameter */
    MR_ASSERT(fs != MR_NULL);
    MR_ASSERT((fs->name != MR_NULL) && (fs->name[0] != '\0'));
    MR_ASSERT(fs->mount != MR_NULL);

    /* Lock */
    mask = mr_fs_nsp_lock();

    /* Check if file-system already registered */
    for (link = &__fs_list; *link; link = &(*link)->next) {
        if ((*link == fs) || (mr_strcmp((*link)->name, fs->name) == 0)) {
            /* Unlock */
            mr_fs_nsp_unlock(mask);
            return -MR_EEXIST;
        }
    }

    /* Add file-system */
    fs->next = MR_NULL;
    *link = fs;

    /* Unlock */
    mr_fs_nsp_unlock(mask);
    return 0;
}

mr_file_system_t *mr_file_system_find(const char *name) {
    mr_file_system_t *fs;
    int mask;

    /* Check parameter */
    MR_ASSERT(name != MR_NULL);

    /* Lock */
    mask = mr_fs_nsp_lock();

    /* Find file-system */
    for (fs = __fs_list; fs; fs = fs->next) {
        if (mr_strcmp(fs->name, name) == 0) {
            break;
        }
    }

    /* Unlock */
    mr_fs_nsp_unlock(mask);
    return fs;
}
#endif /* defined(MR_USE_FS) */
