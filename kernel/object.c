/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mr-X/mr_service.h>
#include <mr-X/mr_object.h>

mr_err_t mr_object_init(mr_object_t *obj, const mr_clazz_t *clazz) {
    /* Check parameter */
    if ((!obj) || MR_OBJECT_IS_INITED(obj)) {
        return -MR_EINVAL;
    }

    /* Init object */
    mr_ref_init(&obj->magic_ref);
    obj->clazz = clazz;
    return 0;
}

void mr_object_del(mr_object_t *obj) {
    /* Check parameter */
    if ((!obj) || (!MR_OBJECT_IS_INITED(obj))) {
        return;
    }

    /* Put object */
    mr_object_put(obj);
}

mr_object_t *mr_object_get(mr_object_t *obj) {
    /* Check parameter */
    if ((!obj) || (!MR_OBJECT_IS_INITED(obj))) {
        return MR_NULL;
    }

    /* Get object reference */
    return mr_ref_get(&obj->magic_ref) ? obj : MR_NULL;
}

static void object_ref_release(mr_ref_t *ref) {
    const mr_clazz_t *clazz;
    mr_object_t *obj;

    /* Get object and clazz */
    obj = MR_CONTAINER_OF(ref, mr_object_t, magic_ref);
    clazz = obj->clazz;

    /* Call clazz release */
    if (clazz && clazz->release) {
        clazz->release(obj);
    }
}

mr_bool_t mr_object_put(mr_object_t *obj) {
    /* Check parameter */
    if ((!obj) || (!MR_OBJECT_IS_INITED(obj))) {
        return MR_TRUE;
    }

    /* Put object reference */
    return mr_ref_put(&obj->magic_ref, object_ref_release);
}
