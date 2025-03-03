/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/os/mr_kmutex.h>
#if defined(MR_USE_KOS) && defined(MR_USE_KMUTEX)
#include <kernel/mr_initcall.h>
#include <libc/mr_malloc.h>
#include <port/mr_port_mutex.h>

/* Kmutex ktype */
static mr_ktype_t ktype1, ktype2;
/* Kmutex kset */
static mr_kset_t kroot = MR_KSET_INIT(&kroot, MR_NULL);

MR_INLINE mr_err_t kmutex_init(mr_kmutex_t *kmutex, const char *name) {
    mr_err_t ret;

    /* Init mutex */
    ret = mr_port_mutex_init(kmutex->imutex, name);
    if (ret != 0) {
        return ret;
    }

    /* Init kmutex */
    kmutex->mutex = kmutex->imutex;
    return 0;
}

MR_INLINE mr_err_t kmutex_del(mr_kmutex_t *kmutex) {
    /* Delete mutex */
    return mr_port_mutex_del(kmutex->mutex);
}

MR_INLINE mr_err_t kmutex_init_add(mr_kmutex_t *kmutex, mr_ktype_t *ktype,
                                   const char *name) {
    mr_err_t ret;

    /* Init kmutex */
    kmutex_init(kmutex, name);

    /* Init kobject */
    mr_kobject_init((mr_kobject_t *)kmutex, ktype);

    /* Add kobject to kroot */
    ret = mr_kobject_add((mr_kobject_t *)kmutex, (mr_kobject_t *)&kroot, name);
    if (ret != 0) {
        kmutex_del(kmutex);
    }
    return ret;
}

mr_err_t mr_kmutex_init(mr_kmutex_t *kmutex, const char *name) {
    /* Check arguments */
    if ((!kmutex) || MR_KMUTEX_IS_INITED(kmutex) || (!name)) {
        return -MR_EINVAL;
    }

    /* Init and add kmutex */
    return kmutex_init_add(kmutex, &ktype1, name);
}

mr_kmutex_t *mr_kmutex_create(const char *name) {
    mr_kmutex_t *kmutex;
    mr_err_t ret;

    /* Check arguments */
    if (!name) {
        return MR_NULL;
    }

    /* Create kmutex */
    kmutex = mr_malloc(sizeof(mr_kmutex_t));
    if (!kmutex) {
        return MR_NULL;
    }

    /* Init and add kmutex */
    ret = kmutex_init_add(kmutex, &ktype2, name);
    if (ret != 0) {
        mr_free(kmutex);
        return MR_NULL;
    }
    return kmutex;
}

mr_err_t mr_kmutex_del(mr_kmutex_t *kmutex) {
    /* Check arguments */
    if ((!kmutex) || (!MR_KMUTEX_IS_INITED(kmutex))) {
        return -MR_EINVAL;
    }

    /* Delete kobject */
    mr_kobject_del((mr_kobject_t *)kmutex);
    return 0;
}

mr_err_t mr_kmutex_take(mr_kmutex_t *kmutex, mr_tick_t timeout) {
    /* Check arguments */
    if ((!kmutex) || (!MR_KMUTEX_IS_INITED(kmutex))) {
        return -MR_EINVAL;
    }

    /* Take mutex */
    return mr_port_mutex_take(kmutex->mutex, timeout);
}

mr_err_t mr_kmutex_trytake(mr_kmutex_t *kmutex) {
    /* Check arguments */
    if ((!kmutex) || (!MR_KMUTEX_IS_INITED(kmutex))) {
        return -MR_EINVAL;
    }

    /* Try take mutex(timeout is '0') */
    return mr_port_mutex_take(kmutex->mutex, 0);
}

mr_err_t mr_kmutex_give(mr_kmutex_t *kmutex) {
    /* Check arguments */
    if ((!kmutex) || (!MR_KMUTEX_IS_INITED(kmutex))) {
        return -MR_EINVAL;
    }

    /* Give mutex */
    return mr_port_mutex_give(kmutex->mutex);
}

mr_kmutex_t *mr_kmutex_find(const char *name) {
    /* Lookup kmutex in kroot */
    return (mr_kmutex_t *)mr_kobject_lookup((mr_kobject_t *)&kroot, name);
}

MR_INLINE mr_kmutex_t *kmutex_release_kobj(mr_kobject_t *kobj) {
    mr_kmutex_t *kmutex;

    /* Get kmutex */
    kmutex = MR_CONTAINER_OF(kobj, mr_kmutex_t, parent);

    /* Delete kmutex */
    kmutex_del(kmutex);
    return kmutex;
}

MR_INLINE void kmutex_release1_kobj(mr_kobject_t *kobj) {
    /* Release kmutex */
    kmutex_release_kobj(kobj);
}

MR_INLINE void kmutex_release2_kobj(mr_kobject_t *kobj) {
    mr_kmutex_t *kmutex;

    /* Release kmutex */
    kmutex = kmutex_release_kobj(kobj);

    /* Free kmutex */
    mr_free(kmutex);
}

MR_INLINE void mr_kmutex_kset_init(void) {
    /* Register kset */
    mr_kset_register(&kroot, "mutex");

    /* Init ktype */
    mr_ktype_init(&ktype1, kmutex_release1_kobj);
    mr_ktype_init(&ktype2, kmutex_release2_kobj);
}
MR_INIT_EXPORT(mr_kmutex_kset_init, MR_INIT_LEVEL_KERNEL);
#endif /* defined(MR_USE_KOS) && defined(MR_USE_KMUTEX) */
