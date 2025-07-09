/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mr-X/mr_async.h>
#if defined(MR_USE_ASYNC)
#include <mr-X/mr_spinlock.h>
#include <libc/mr_malloc.h>

/* Await class(static & dynamic) */
static void await_obj_release1(mr_object_t *obj);
static void await_obj_release2(mr_object_t *obj);
static const mr_class_t __await_class1
    = MR_CLASS_INIT("await", MR_NULL, await_obj_release1);
static const mr_class_t __await_class2
    = MR_CLASS_INIT("await", MR_NULL, await_obj_release2);

/* Async class(static & dynamic) */
static void async_obj_release1(mr_object_t *obj);
static void async_obj_release2(mr_object_t *obj);
static const mr_class_t __async_class1
    = MR_CLASS_INIT("async", MR_NULL, async_obj_release1);
static const mr_class_t __async_class2
    = MR_CLASS_INIT("async", MR_NULL, async_obj_release2);

/* Async lock */
static mr_spinlock_t __lock = MR_SPINLOCK_INIT();

static mr_err_t await_init(mr_await_t *await, const mr_class_t *class,
                           mr_uint32_t ref) {
    /* Init await */
    mr_list_init(&await->list);
    await->ref = ref;

    /* Init object */
    return mr_object_init((mr_object_t *)await, class);
}

mr_err_t mr_await_init(mr_await_t *await, mr_uint32_t ref) {
    /* Check parameter */
    MR_ASSERT((await != MR_NULL) && (!MR_OBJECT_IS_INITED(await)));

    /* Init await */
    return await_init(await, &__await_class1, ref);
}

mr_await_t *mr_await_create(mr_uint32_t ref) {
    mr_await_t *await;
    mr_err_t ret;

    /* Create await */
    await = mr_malloc(sizeof(mr_await_t));
    if (!await) {
        return MR_NULL;
    }

    /* Init await */
    ret = await_init(await, &__await_class2, ref);
    if (ret != 0) {
        mr_free(await);
        return MR_NULL;
    }
    return await;
}

static void await_remove(mr_await_t *await) {
    mr_async_t *async;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__lock);

    /* Check await waiting list */
    while (!mr_list_is_empty(&await->list)) {
        async = MR_CONTAINER_OF(await->list.next, mr_async_t, list);

        /* Resume async */
        mr_work_schedule(&async->work);

        /* Remove async from waiting list */
        mr_list_del(&async->list);
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__lock, mask);
}

mr_err_t mr_await_del(mr_await_t *await) {
    /* Check parameter */
    MR_ASSERT((await != MR_NULL) && MR_OBJECT_IS_INITED(await));
    MR_ASSERT(MR_OBJECT_CLASS_IS_OR(await, &__await_class1, &__await_class2));

    /* Remove await */
    await_remove(await);

    /* Delete object */
    mr_object_del((mr_object_t *)await);
    return 0;
}

mr_err_t mr_await_wait(mr_await_t *await, mr_async_t *async) {
    int mask;

    /* Check parameter */
    MR_ASSERT((await != MR_NULL) && MR_OBJECT_IS_INITED(await));
    MR_ASSERT(MR_OBJECT_CLASS_IS_OR(await, &__await_class1, &__await_class2));
    MR_ASSERT((async != MR_NULL) && MR_OBJECT_IS_INITED(async));
    MR_ASSERT(MR_OBJECT_CLASS_IS_OR(async, &__async_class1, &__async_class2));

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__lock);

    /* Check if await reference is still some left */
    if (await->ref > 0) {
        /* Decrease await reference */
        if (await->ref != MR_AWAIT_INFINITE) {
            await->ref--;
        }

        /* Move async to waiting list */
        mr_list_move(&async->list, &await->list);
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__lock, mask);
    return 0;
}

mr_err_t mr_await_complete(mr_await_t *await) {
    mr_async_t *async;
    int mask;

    /* Check parameter */
    MR_ASSERT((await != MR_NULL) && MR_OBJECT_IS_INITED(await));
    MR_ASSERT(MR_OBJECT_CLASS_IS_OR(await, &__await_class1, &__await_class2));

    /* Reduce unnecessary lock operations */
    if (mr_list_is_empty(&await->list)) {
        return 0;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__lock);

    /* Check await waiting list */
    while (!mr_list_is_empty(&await->list)) {
        async = MR_LIST_ENTRY(await->list.next, mr_async_t, list);

        /* Remove async from waiting list */
        mr_list_del(&async->list);

        /* Force resume async(it might be running a timer) */
        mr_work_cancel(&async->work);
        mr_work_schedule(&async->work);
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__lock, mask);
    return 0;
}

static mr_await_t *await_obj_release(mr_object_t *obj) {
    mr_await_t *await;

    /* Get await */
    await = MR_CONTAINER_OF(obj, mr_await_t, parent);

    /* Remove await */
    await_remove(await);
    return await;
}

static void await_obj_release1(mr_object_t *obj) {
    /* Release await */
    await_obj_release(obj);
}

static void await_obj_release2(mr_object_t *obj) {
    mr_await_t *await;

    /* Release await */
    await = await_obj_release(obj);

    /* Free await */
    mr_free(await);
}

static void async_work_entry(mr_work_t *work, void *param) {
    mr_async_entry_t *entry;
    mr_async_t *async;

    /* Get async */
    async = MR_CONTAINER_OF(work, mr_async_t, work);

    /* Save async entry */
    entry = (mr_async_entry_t *)async->entry;

    /* Call async entry */
    entry(async, param);
}

static mr_err_t async_init(mr_async_t *async, const mr_class_t *class,
                           mr_ptr_t entry, mr_ptr_t param) {
    /* Init async */
    mr_work_init(&async->work, async_work_entry, param);
    mr_await_init(&async->await, 1);
    mr_list_init(&async->list);
    async->entry = entry;
    async->label = 0;

    /* Init object */
    return mr_object_init((mr_object_t *)async, class);
}

mr_err_t mr_async_init(mr_async_t *async, mr_async_entry_t *entry,
                       void *param) {
    /* Check parameter */
    MR_ASSERT((async != MR_NULL) && (!MR_OBJECT_IS_INITED(async)));
    MR_ASSERT(entry != MR_NULL);

    /* Init async */
    return async_init(async, &__async_class1, entry, param);
}

mr_async_t *mr_async_create(mr_async_entry_t *entry, void *param) {
    mr_async_t *async;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(entry != MR_NULL);

    /* Create async */
    async = mr_malloc(sizeof(mr_async_t));
    if (!async) {
        return MR_NULL;
    }

    /* Init async */
    ret = async_init(async, &__async_class2, entry, param);
    if (ret != 0) {
        mr_free(async);
        return MR_NULL;
    }
    return async;
}

static void async_remove(mr_async_t *async) {
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__lock);

    /* Remove async from waiting list */
    mr_list_del(&async->list);

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__lock, mask);
}

mr_err_t mr_async_del(mr_async_t *async) {
    /* Check parameter */
    MR_ASSERT((async != MR_NULL) && MR_OBJECT_IS_INITED(async));
    MR_ASSERT(MR_OBJECT_CLASS_IS_OR(async, &__async_class1, &__async_class2));

    /* Cancel async */
    mr_work_cancel(&async->work);
    async_remove(async);

    /* Delete object */
    mr_object_del((mr_object_t *)async);
    return 0;
}

mr_err_t mr_async_cancel(mr_async_t *async) {
    /* Check parameter */
    MR_ASSERT((async != MR_NULL) && MR_OBJECT_IS_INITED(async));
    MR_ASSERT(MR_OBJECT_CLASS_IS_OR(async, &__async_class1, &__async_class2));

    /* Cancel async */
    mr_work_cancel(&async->work);
    async_remove(async);
    return 0;
}

static mr_async_t *async_obj_release(mr_object_t *obj) {
    mr_async_t *async;

    /* Get async */
    async = MR_CONTAINER_OF(obj, mr_async_t, parent);

    /* Complete await */
    mr_await_complete(&async->await);

    /* Delete async */
    mr_work_del(&async->work);
    async_remove(async);
    return async;
}

static void async_obj_release1(mr_object_t *obj) {
    /* Release async */
    async_obj_release(obj);
}

static void async_obj_release2(mr_object_t *obj) {
    mr_async_t *async;

    /* Release async */
    async = async_obj_release(obj);

    /* Free async */
    mr_free(async);
}
#endif /* defined(MR_USE_ASYNC) */
