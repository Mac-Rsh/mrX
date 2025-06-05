/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/mr_object.h>
#include <libc/mr_string.h>

mr_err_t mr_object_init(mr_object_t *obj, const mr_class_t *class) {
    /* Check parameter */
    if ((!obj) || MR_OBJECT_IS_INITED(obj)) {
        return -MR_EINVAL;
    }

    /* Init object */
    obj->class = class;
    mr_ref_init(&obj->ref);
    obj->magic = MR_OBJECT_MAGIC;
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
    return mr_ref_get(&obj->ref) ? obj : MR_NULL;
}

static void object_ref_release(mr_ref_t *ref) {
    mr_object_t *obj;

    /* Mark as released */
    obj = MR_CONTAINER_OF(ref, mr_object_t, ref);
    obj->magic = ~MR_OBJECT_MAGIC;

    /* Release object */
    if (obj->class && obj->class->release) {
        obj->class->release(obj);
    }
}

mr_bool_t mr_object_put(mr_object_t *obj) {
    /* Check parameter */
    if ((!obj) || (!MR_OBJECT_IS_INITED(obj))) {
        return MR_TRUE;
    }

    /* Put object reference */
    return mr_ref_put(&obj->ref, object_ref_release);
}

static const mr_attr_t *object_attr_lookup(mr_object_t *obj, const char *name) {
    const mr_attr_t **attrs;
    mr_size_t i;

    /* Check class and attributes */
    if ((!obj->class) || (!obj->class->attrs)) {
        return MR_NULL;
    }

    /* Lookup attribute */
    attrs = obj->class->attrs;
    for (i = 0; attrs[i]; i++) {
        /* Match attribute name */
        if (mr_strcmp(name, attrs[i]->name)) {
            continue;
        }

        /* Found */
        return attrs[i];
    }

    /* Not found */
    return MR_NULL;
}

mr_ssize_t mr_object_attr_show(mr_object_t *obj, const char *name, char *buf,
                               mr_size_t size) {
    const mr_attr_t *attr;

    /* Check parameter */
    if ((!obj) || (!MR_OBJECT_IS_INITED(obj)) || (!name) || (name[0] == '\0')
        || ((!buf) && (size > 0))) {
        return -MR_EINVAL;
    }

    /* Lookup attribute */
    attr = object_attr_lookup(obj, name);
    if (!attr) {
        return -MR_ENOENT;
    }

    /* Show attribute */
    return attr->show(obj, attr, buf, size);
}

mr_ssize_t mr_object_attr_store(mr_object_t *obj, const char *name,
                                const char *buf, mr_size_t size) {
    const mr_attr_t *attr;

    /* Check parameter */
    if ((!obj) || (!MR_OBJECT_IS_INITED(obj)) || (!name) || (name[0] == '\0')
        || ((!buf) && (size > 0))) {
        return -MR_EINVAL;
    }

    /* Lookup attribute */
    attr = object_attr_lookup(obj, name);
    if (!attr) {
        return -MR_ENOENT;
    }

    /* Store attribute */
    return attr->store(obj, attr, buf, size);
}
