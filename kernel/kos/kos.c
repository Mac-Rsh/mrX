/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/kos/mr_kos.h>

static mr_kos_t kos;

MR_INLINE void kos_init(mr_kos_t *os, mr_kos_type_t *type) {
    /* Init kos */
    os->type = type;
}

MR_INLINE mr_err_t kos_init_register(mr_kos_t *os, const char *name,
                                     mr_kos_type_t *type) {
    /* Init kos */
    kos_init(os, type);

    /* Init kset */
    mr_kset_init((mr_kset_t *)os, MR_NULL);

    /* Register kset */
    return mr_kset_register((mr_kset_t *)os, name);
}

mr_err_t mr_kos_register(mr_kos_type_t *type) {
    /* Check arguments */
    if (MR_KOBJECT_IS_INITED(&kos) || (!type)) {
        return -MR_EINVAL;
    }

    /* Init and register kos */
    return kos_init_register(&kos, "os", type);
}

mr_kos_t *mr_kos_get(void) {
    /* Get kos */
    return (mr_kos_t *)mr_kobject_get((mr_kobject_t *)&kos);
}

void mr_kos_put(void) {
    /* Put kos */
    mr_kobject_put((mr_kobject_t *)&kos);
}
