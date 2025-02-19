/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include "kernel/kos/mr_kos.h"
#include "kernel/kos/mr_kthread.h"
#include <rtthread.h>

MR_INLINE mr_err_t rtt_err_shift(rt_err_t err) {
    switch (-err) {
        case RT_EOK: {
            return 0;
        }
        case RT_ETIMEOUT: {
            return -MR_ETIMEDOUT;
        }
        case RT_ENOMEM: {
            return -MR_ENOMEM;
        }
        case RT_ENOSYS: {
            return -MR_ENOSYS;
        }
        case RT_EBUSY: {
            return -MR_EBUSY;
        }
        case RT_EIO: {
            return -MR_EIO;
        }
        case RT_EINVAL: {
            return -MR_EINVAL;
        }
        case RT_ENOENT: {
            return -MR_ENOENT;
        }
        case RT_EPERM: {
            return -MR_EPERM;
        }
        default: {
            return -MR_EIO;
        }
    }
}

MR_INLINE void rtt_kthread_entry(void *kth) {
    mr_kthread_entry(kth);
}

MR_INLINE void rtt_kthread_cleanup(struct rt_thread *thread) {
    void *kth;

    /* Cleanup thread */
    kth = thread->parameter;
    mr_kthread_cleanup(kth);
}

MR_INLINE mr_err_t rtt_kthread_init(void *thread, const char *name, void *kth,
                                    void *stack, mr_size_t stack_size,
                                    mr_uint32_t priority, mr_tick_t tick) {
    rt_err_t ret;

    /* Init thread */
    ret = rt_thread_init(thread, name, rtt_kthread_entry, kth, stack,
                         stack_size, priority, tick);
    if (ret == RT_EOK) {
        /* Hook thread cleanup */
        ((struct rt_thread *)thread)->cleanup = rtt_kthread_cleanup;
    }

    /* Shift error code */
    return rtt_err_shift(ret);
}

MR_INLINE mr_err_t rtt_kthread_del(void *thread) {
    rt_uint8_t stat;
    rt_err_t ret;

    /* Get thread stat */
    stat = rt_sched_thread_get_stat(thread);

    /* Delete thread */
    ret = rt_thread_detach(thread);

    /* If thread is not started, Recycle it now */
    if ((ret == RT_EOK) && (stat == RT_THREAD_INIT)) {
        /* This processing was not required originally, but RTT does not handle
         * thread state, and all resources are reclaimed by idle threads, which
         * takes too long */
        ((struct rt_thread *)thread)->cleanup = RT_NULL;
        rt_defunct_execute();
    }

    /* Shift error code */
    return rtt_err_shift(ret);
}

MR_INLINE mr_err_t rtt_kthread_startup(void *thread) {
    rt_err_t ret;

    /* Startup thread */
    ret = rt_thread_startup(thread);

    /* Shift error code */
    return rtt_err_shift(ret);
}

MR_INLINE mr_err_t rtt_kthread_resume(void *thread) {
    rt_err_t ret;

    /* Resume thread */
    ret = rt_thread_resume(thread);

    /* Shift error code */
    return rtt_err_shift(ret);
}

MR_INLINE mr_err_t rtt_kthread_yield(void) {
    rt_err_t ret;

    /* Yield thread */
    ret = rt_thread_yield();

    /* Shift error code */
    return rtt_err_shift(ret);
}

MR_INLINE mr_err_t rtt_kthread_suspend(void *thread) {
    rt_err_t ret;

    /* Suspend thread */
    rt_thread_suspend(thread);

    /* Shift error code */
    return rtt_err_shift(ret);
}

MR_INLINE mr_err_t rtt_kthread_sleep(mr_tick_t tick) {
    rt_err_t ret;

    /* Delay thread */
    rt_thread_delay(tick);

    /* Shift error code */
    return rtt_err_shift(ret);
}

MR_INLINE mr_err_t rtt_kthread_exit(void *thread) {
    rt_uint8_t stat;
    rt_err_t ret;

    /* Get thread stat */
    stat = rt_sched_thread_get_stat(thread);

    /* Close thread */
    ret = rt_thread_close(thread);

    /* If thread is not started, Recycle it now */
    if ((ret == RT_EOK) && (stat == RT_THREAD_INIT)) {
        /* This processing was not required originally, but RTT does not handle
         * thread state, and all resources are reclaimed by idle threads, which
         * takes too long */
        ((struct rt_thread *)thread)->cleanup = RT_NULL;
        rt_defunct_execute();
    }

    /* Shift error code */
    return rtt_err_shift(ret);
}

MR_INLINE void *rtt_kthread_self(void) {
    void *thread;

    /* Get thread self */
    thread = rt_thread_self();

    /* Get kthread */
    return ((struct rt_thread *)thread)->parameter;
}

static mr_kthread_ops_t rtt_kthread_ops = {
    .init = rtt_kthread_init,
    .del = rtt_kthread_del,
    .startup = rtt_kthread_startup,
    .resume = rtt_kthread_resume,
    .yield = rtt_kthread_yield,
    .suspend = rtt_kthread_suspend,
    .sleep = rtt_kthread_sleep,
    .exit = rtt_kthread_exit,
    .self = rtt_kthread_self,
};
static mr_kthread_type_t rtt_kthread_type = {
    .priority_max = 32,
    .ops = &rtt_kthread_ops,
};
static mr_kos_type_t rtt_kos_type = {
    .kth_type = &rtt_kthread_type,
};

static int rtt_kos_register(void) {
    mr_kos_register(&rtt_kos_type);
}
