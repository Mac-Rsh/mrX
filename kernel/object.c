/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/kernel/mr_object.h>

mr_err_t mr_object_init(mr_object_t *obj, const mr_clazz_t *clazz) {
    /* Check parameter */
    if (!obj) {
        return -MR_EINVAL;
    }

    /* Init object */
    obj->clazz = clazz;
    mr_ref_init(&obj->magic_ref);
    return 0;
}

mr_err_t mr_object_derive(mr_object_t *obj, const mr_clazz_t *clazz) {
    /* Check parameter */
    if ((!obj) || (!clazz) || (!MR_OBJECT_IS_INITED(obj))) {
        return -MR_EINVAL;
    }

    /* Check whether clazz is a sub clazz of the object's current clazz */
    if (!mr_clazz_is_kind_of(clazz, MR_OBJECT_CLAZZ(obj))) {
        return -MR_EPERM;
    }

    /* Derive to sub clazz */
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

static void object_clazz_release(mr_object_t *obj, const mr_clazz_t *clazz) {
    /* Check parameter */
    if (!clazz) {
        return;
    }

    /* Call super clazz release */
    object_clazz_release(obj, clazz->super);

    /* Call current clazz release */
    if (clazz->release) {
        clazz->release(obj);
    }
}

static void object_ref_release(mr_ref_t *ref) {
    mr_object_t *obj;

    /* Get object */
    obj = MR_CONTAINER_OF(ref, mr_object_t, magic_ref);

    /* Call clazz release chain */
    object_clazz_release(obj, obj->clazz);
}

mr_bool_t mr_object_put(mr_object_t *obj) {
    /* Check parameter */
    if ((!obj) || (!MR_OBJECT_IS_INITED(obj))) {
        return MR_TRUE;
    }

    /* Put object reference */
    return mr_ref_put(&obj->magic_ref, object_ref_release);
}
