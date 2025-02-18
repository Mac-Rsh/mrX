/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/kos/mr_kos.h>
#if defined(MR_USE_KOS)

/* Kos */
static mr_kos_t kroot;

MR_INLINE void kos_init(mr_kos_t *os, mr_kos_type_t *ktype) {
    /* Init kos */
    os->type = ktype;
}

MR_INLINE mr_err_t kos_init_register(mr_kos_t *kos, const char *name,
                                     mr_kos_type_t *ktype) {
    /* Init kos */
    kos_init(kos, ktype);

    /* Init kset */
    mr_kset_init((mr_kset_t *)kos, MR_NULL);

    /* Register kset */
    return mr_kset_register((mr_kset_t *)kos, name);
}

mr_err_t mr_kos_register(mr_kos_type_t *type) {
    /* Check arguments */
    if (MR_KOBJECT_IS_INITED(&kroot) || (!type)) {
        return -MR_EINVAL;
    }

    /* Init and register kos */
    return kos_init_register(&kroot, "os", type);
}

mr_kos_t *mr_kos_find(void) {
    /* Find kos in kroot */
    return &kroot;
}
#endif /* defined(MR_USE_KOS) */
