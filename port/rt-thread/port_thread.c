/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/os/mr_kthread.h>
#if defined(MR_USE_KOS)
#include <kernel/mr_initcall.h>
#include <port/mr_port_thread.h>
#include <rtthread.h>

static mr_err_t err_shift(rt_err_t err) {
    switch (-err) {
        case RT_EOK: {
            return 0;
        }
#ifdef RT_ETIMEOUT
        case RT_ETIMEOUT: {
            return -MR_ETIMEDOUT;
        }
#endif /* RT_ETIMEOUT */
#ifdef RT_ENOMEM
        case RT_ENOMEM: {
            return -MR_ENOMEM;
        }
#endif /* RT_ENOMEM */
#ifdef RT_ENOSYS
        case RT_ENOSYS: {
            return -MR_ENOSYS;
        }
#endif /* RT_ENOSYS */
#ifdef RT_EBUSY
        case RT_EBUSY: {
            return -MR_EBUSY;
        }
#endif /* RT_EBUSY */
#ifdef RT_EIO
        case RT_EIO: {
            return -MR_EIO;
        }
#endif /* RT_EIO */
#ifdef RT_EINVAL
        case RT_EINVAL: {
            return -MR_EINVAL;
        }
#endif /* RT_EINVAL */
#ifdef RT_ENOENT
        case RT_ENOENT: {
            return -MR_ENOENT;
        }
#endif /* RT_ENOENT */
#ifdef RT_EPERM
        case RT_EPERM: {
            return -MR_EPERM;
        }
#endif /* RT_EPERM */
        default: {
            return -MR_EIO;
        }
    }
}

static void thread_entry(void *kthread) {
    mr_kthread_entry(kthread);
}

mr_err_t mr_port_thread_init(void *thread, const char *name, void *kthread,
                             void *stack, mr_size_t stack_size,
                             mr_uint32_t priority, mr_tick_t tick) {
    rt_err_t ret;

    /* Check inline thread size */
    MR_STATIC_ASSERT(MR_CFG_KTHREAD_ITHREAD_SIZE >= sizeof(struct rt_thread));

    /* Init thread */
    ret = rt_thread_init(thread, name, thread_entry, kthread, stack, stack_size,
                         priority, tick);

    /* Shift error code */
    return err_shift(ret);
}

void *mr_port_thread_create(const char *name, void *kthread,
                            mr_size_t stack_size, mr_uint32_t priority,
                            mr_tick_t tick) {
    /* Create thread */
#if !defined(RT_USING_HEAP)
#error "Please enable 'RT_USING_HEAP'"
#endif /* !defined(RT_USING_HEAP) */
    return rt_thread_create(name, thread_entry, kthread, stack_size, priority,
                            tick);
}

mr_err_t mr_port_thread_del(void *thread) {
    rt_err_t ret;

    /* Delete thread */
    if (rt_object_is_systemobject((rt_object_t)thread) == RT_TRUE) {
        ret = rt_thread_detach(thread);
    } else {
        ret = rt_thread_delete(thread);
    }

    /* Shift error code */
    return err_shift(ret);
}

mr_err_t mr_port_thread_startup(void *thread) {
    rt_err_t ret;

    /* Startup thread */
    ret = rt_thread_startup(thread);

    /* Shift error code */
    return err_shift(ret);
}

mr_err_t mr_port_thread_resume(void *thread) {
    rt_err_t ret;

    /* Resume thread */
    ret = rt_thread_resume(thread);

    /* Shift error code */
    return err_shift(ret);
}

mr_err_t mr_port_thread_yield() {
    rt_err_t ret;

    /* Yield thread */
    ret = rt_thread_yield();

    /* Shift error code */
    return err_shift(ret);
}

mr_err_t mr_port_thread_suspend(void *thread) {
    rt_err_t ret;

    /* Suspend thread */
    ret = rt_thread_suspend(thread);

    /* Shift error code */
    return err_shift(ret);
}

mr_err_t mr_port_thread_sleep(mr_tick_t tick) {
    rt_err_t ret;

    /* Delay thread */
    ret = rt_thread_delay(tick);

    /* Shift error code */
    return err_shift(ret);
}

void *mr_port_thread_self(void) {
    rt_thread_t thread;

    /* Get thread self */
    thread = rt_thread_self();

    /* Return kthread */
    return thread->parameter;
}

static void mr_main_thread_takeover(void) {
    static mr_kthread_t kth;
    rt_thread_t main;

    /* Find main thread */
    main = rt_thread_find("main");
    if (!main) {
        return;
    }

    /* Takeover main thread */
    main->parameter = &kth;
    mr_kthread_takeover(&kth, "main", main);
}
MR_INIT_EXPORT(mr_main_thread_takeover, MR_INIT_LEVEL_USER("0"));
#endif /* defined(MR_USE_KOS) */
