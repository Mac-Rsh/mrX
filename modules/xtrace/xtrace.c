/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/modules/xtrace/mr_xtrace.h>
#if defined(MR_USE_MODULE_XTRACE)
#include <mrX/kernel/mr_initcall.h>
#include <mrX/kernel/mr_spinlock.h>
#include <mrX/kernel/mr_clock.h>
#include <mrX/kernel/mr_fifo.h>

/* XTrace protocol and output clazzes */
static void xtrace_release(mr_object_t *obj);
static void xtrace_output_release(mr_object_t *obj);
MR_CLAZZES_DEFINE(xtrace, xtrace_release, MR_NULL, MR_NULL);
MR_CLAZZES_DEFINE(xtrace_output, xtrace_output_release, MR_NULL, MR_NULL);

/* XTrace locks */
static mr_spinlock_t __xtrace_write_lock = MR_SPINLOCK_INIT();
static mr_spinlock_t __xtrace_lock = MR_SPINLOCK_INIT();

/* XTrace state */
static mr_atomic_t __xtrace_notify_pending = MR_ATOMIC_INIT(0);
static mr_atomic_t __xtrace_notify_armed = MR_ATOMIC_INIT(1);
static mr_atomic_t __xtrace_in_notify = MR_ATOMIC_INIT(0);
static mr_atomic_t __xtrace_dropped = MR_ATOMIC_INIT(0);
static mr_bool_t __xtrace_running = MR_FALSE;
static mr_bool_t __xtrace_inited = MR_FALSE;

/* XTrace active protocol and output objects */
static mr_xtrace_output_t *__xtrace_output = MR_NULL;
static mr_xtrace_t *__xtrace_proto = MR_NULL;

/* XTrace read state definition */
#define XTRACE_READ_IDLE                (0)
#define XTRACE_READ_BUSY                (1)
#define XTRACE_READ_CLAIMED             (2)

/* XTrace asynchronous FIFO */
#if !defined(MR_CFG_XTRACE_BUF_SIZE)
#define MR_CFG_XTRACE_BUF_SIZE          (4096U)
#endif /* !defined(MR_CFG_XTRACE_BUF_SIZE) */
static mr_uint8_t __xtrace_fifo_buf[MR_CFG_XTRACE_BUF_SIZE];
static mr_atomic_t __xtrace_reading = MR_ATOMIC_INIT(XTRACE_READ_IDLE);
static mr_size_t __xtrace_claimed = 0U;
static mr_fifo_t __xtrace_fifo = {
    .ridx = MR_ATOMIC_INIT(0),
    .widx = MR_ATOMIC_INIT(0),
    .elemsz = 1U,
    .capacity = MR_CFG_XTRACE_BUF_SIZE,
    .buf = __xtrace_fifo_buf,
};

/* XTrace system information */
#if !defined(MR_CFG_XTRACE_APP_NAME)
#define MR_CFG_XTRACE_APP_NAME          "mrX"
#endif /* !defined(MR_CFG_XTRACE_APP_NAME) */
#if !defined(MR_CFG_XTRACE_DEVICE_NAME)
#define MR_CFG_XTRACE_DEVICE_NAME       ""
#endif /* !defined(MR_CFG_XTRACE_DEVICE_NAME) */
#if defined(MR_USE_CLOCK_HIRES)
#define XTRACE_DEFAULT_TS_FREQ                                                 \
    ((mr_uint32_t)MR_CFG_HIRES_TICK_PER_SECOND)
#else
#define XTRACE_DEFAULT_TS_FREQ          ((mr_uint32_t)MR_CFG_TICK_PER_SECOND)
#endif /* defined(MR_USE_CLOCK_HIRES) */
static mr_xtrace_sysinfo_t __xtrace_sysinfo = {
    .app_name = MR_CFG_XTRACE_APP_NAME,
    .os_name = "mrX",
    .device_name = MR_CFG_XTRACE_DEVICE_NAME,
    .core_name = MR_NULL,
    .ts_freq = XTRACE_DEFAULT_TS_FREQ,
    .cpu_freq = 0U,
    .ram_base = 0U,
    .anchor_addr = 0U,
};

void mr_xtrace_sysinfo_set(const mr_xtrace_sysinfo_t *info) {
    int mask;

    /* Check parameter */
    if (!info) {
        return;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xtrace_lock);

    /* Merge supplied fields */
    if (info->app_name && (info->app_name[0] != '\0')) {
        __xtrace_sysinfo.app_name = info->app_name;
    }
    if (info->os_name && (info->os_name[0] != '\0')) {
        __xtrace_sysinfo.os_name = info->os_name;
    }
    if (info->device_name && (info->device_name[0] != '\0')) {
        __xtrace_sysinfo.device_name = info->device_name;
    }
    if (info->core_name && (info->core_name[0] != '\0')) {
        __xtrace_sysinfo.core_name = info->core_name;
    }
    if (info->ts_freq != 0U) {
        __xtrace_sysinfo.ts_freq = info->ts_freq;
    }
    if (info->cpu_freq != 0U) {
        __xtrace_sysinfo.cpu_freq = info->cpu_freq;
    }
    if (info->ram_base != 0U) {
        __xtrace_sysinfo.ram_base = info->ram_base;
    }
    if (info->anchor_addr != 0U) {
        __xtrace_sysinfo.anchor_addr = info->anchor_addr;
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xtrace_lock, mask);
}

void mr_xtrace_sysinfo_get(mr_xtrace_sysinfo_t *info) {
    int mask;

    /* Check parameter */
    if (!info) {
        return;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xtrace_lock);

    /* Snapshot system information */
    *info = __xtrace_sysinfo;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xtrace_lock, mask);

    /* Fill in derived defaults */
    if (info->cpu_freq == 0U) {
        info->cpu_freq = info->ts_freq;
    }
    if (info->anchor_addr == 0U) {
        info->anchor_addr = (mr_uintptr_t)&mr_xtrace_init;
    }
}

mr_uint32_t mr_xtrace_timestamp_freq(void) {
    mr_uint32_t freq;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xtrace_lock);

    /* Get timestamp frequency */
    freq = __xtrace_sysinfo.ts_freq;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xtrace_lock, mask);
    return freq;
}

mr_uint32_t mr_xtrace_dropped_count(void) {
    /* Get dropped count */
    return (mr_uint32_t)mr_atomic_load(&__xtrace_dropped);
}

void mr_xtrace_dropped_inc(void) {
    mr_atomic_fetch_add(&__xtrace_dropped, 1);
}

static mr_xtrace_t *xtrace_get_locked(void) {
    mr_xtrace_t *xtrace;

    /* Get active protocol reference */
    xtrace = __xtrace_proto;
    if (xtrace && xtrace->ops && mr_object_get(&xtrace->parent)) {
        return xtrace;
    }
    return MR_NULL;
}

static mr_xtrace_output_t *xtrace_output_get_locked(void) {
    mr_xtrace_output_t *output;

    /* Get active output reference */
    output = __xtrace_output;
    if (output && output->ops && mr_object_get(&output->parent)) {
        return output;
    }
    return MR_NULL;
}

static void xtrace_notify(void) {
    mr_xtrace_output_t *output;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xtrace_lock);

    /* Get active output reference */
    output = xtrace_output_get_locked();

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xtrace_lock, mask);

    /* Invoke non-blocking adapter notify outside all locks */
    if (output) {
        if (output->ops->notify) {
            mr_atomic_fetch_add(&__xtrace_in_notify, 1);
            output->ops->notify(output->priv);
            mr_atomic_fetch_sub(&__xtrace_in_notify, 1);
        }
        mr_object_put(&output->parent);
    }
}

static void xtrace_dispatch_notify(void) {
    mr_atomic_t expected;

    /* Init expected value */
    mr_atomic_init(&expected, 1);

    /* Dispatch deferred edge notification outside protocol locks */
    if (mr_atomic_compare_exchange(&__xtrace_notify_pending, &expected, 0)) {
        xtrace_notify();
    }
}

mr_err_t mr_xtrace_init(mr_xtrace_t *xtrace, const mr_xtrace_ops_t *ops,
                        void *priv) {
    mr_err_t ret;

    /* Check parameter */
    if ((!xtrace) || (!ops) || (!ops->emit)) {
        return -MR_EINVAL;
    }
    if (MR_OBJECT_IS_INITED(xtrace)) {
        return -MR_EBUSY;
    }

    /* Init protocol implementation */
    if (ops->init) {
        ret = ops->init(priv);
        if (ret != 0) {
            return ret;
        }
    }

    /* Init protocol object */
    xtrace->priv = priv;
    xtrace->ops = ops;
    ret = mr_object_init(&xtrace->parent, MR_CLAZZES_STATIK(xtrace));
    if ((ret != 0) && ops->deinit) {
        ops->deinit(priv);
    }
    return ret;
}

void mr_xtrace_del(mr_xtrace_t *xtrace) {
    /* Check parameter */
    if ((!xtrace) || (!MR_OBJECT_IS_INITED(xtrace))
        || (!MR_OBJECT_IS_CLAZZES(xtrace, xtrace))) {
        return;
    }

    /* Release caller's ownership reference */
    mr_object_del(&xtrace->parent);
}

mr_err_t mr_xtrace_register(mr_xtrace_t *xtrace) {
    mr_bool_t running, start_ref;
    mr_xtrace_t *old_xtrace;
    int mask;

    /* Check parameter */
    if ((!xtrace) || (!MR_OBJECT_IS_INITED(xtrace))
        || (!MR_OBJECT_IS_CLAZZES(xtrace, xtrace)) || (!xtrace->ops)
        || (!xtrace->ops->emit)) {
        return -MR_EINVAL;
    }

    /* Hold registration reference */
    if (!mr_object_get(&xtrace->parent)) {
        return -MR_EINVAL;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xtrace_lock);

    /* Ignore duplicate registration of the same protocol object */
    if (__xtrace_proto == xtrace) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xtrace_lock, mask);
        mr_object_put(&xtrace->parent);
        return 0;
    }

    /* Swap active protocol */
    old_xtrace = __xtrace_proto;
    __xtrace_proto = xtrace;
    running = __xtrace_running;
    start_ref = MR_FALSE;
    if (running && xtrace->ops->start) {
        start_ref = mr_object_get(&xtrace->parent) ? MR_TRUE : MR_FALSE;
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xtrace_lock, mask);

    /* Stop, flush, and release previous protocol without spinning */
    if (old_xtrace) {
        if (running && old_xtrace->ops && old_xtrace->ops->stop) {
            old_xtrace->ops->stop(old_xtrace->priv);
        }
        if (old_xtrace->ops && old_xtrace->ops->flush) {
            old_xtrace->ops->flush(old_xtrace->priv);
        }
        mr_object_put(&old_xtrace->parent);
    }

    /* Start new protocol if tracing is already active */
    if (start_ref) {
        xtrace->ops->start(xtrace->priv);
        mr_object_put(&xtrace->parent);
    }

    /* Dispatch deferred notification outside locks */
    xtrace_dispatch_notify();
    return 0;
}

mr_err_t mr_xtrace_unregister(void) {
    mr_xtrace_t *old_xtrace;
    mr_bool_t running;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xtrace_lock);

    /* Detach active protocol */
    old_xtrace = __xtrace_proto;
    __xtrace_proto = MR_NULL;
    running = __xtrace_running;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xtrace_lock, mask);

    if (!old_xtrace) {
        return 0;
    }

    /* Stop, flush, and release protocol reference without spinning */
    if (running && old_xtrace->ops && old_xtrace->ops->stop) {
        old_xtrace->ops->stop(old_xtrace->priv);
    }
    if (old_xtrace->ops && old_xtrace->ops->flush) {
        old_xtrace->ops->flush(old_xtrace->priv);
    }
    mr_object_put(&old_xtrace->parent);

    /* Notify output if flush produced data */
    xtrace_dispatch_notify();
    return 0;
}

mr_err_t mr_xtrace_output_init(mr_xtrace_output_t *output,
                               const mr_xtrace_output_ops_t *ops,
                               void *priv) {
    mr_err_t ret;

    /* Check parameter */
    if (!output) {
        return -MR_EINVAL;
    }
    if (MR_OBJECT_IS_INITED(output)) {
        return -MR_EBUSY;
    }

    /* Init output implementation with reentry guard */
    if (ops && ops->init) {
        mr_atomic_fetch_add(&__xtrace_in_notify, 1);
        ret = ops->init(priv);
        mr_atomic_fetch_sub(&__xtrace_in_notify, 1);
        if (ret != 0) {
            return ret;
        }
    }

    /* Init output object */
    output->ops = ops;
    output->priv = priv;
    ret = mr_object_init(&output->parent,
                         MR_CLAZZES_STATIK(xtrace_output));
    if ((ret != 0) && ops && ops->deinit) {
        mr_atomic_fetch_add(&__xtrace_in_notify, 1);
        ops->deinit(priv);
        mr_atomic_fetch_sub(&__xtrace_in_notify, 1);
    }
    return ret;
}

void mr_xtrace_output_del(mr_xtrace_output_t *output) {
    /* Check parameter */
    if ((!output) || (!MR_OBJECT_IS_INITED(output))
        || (!MR_OBJECT_IS_CLAZZES(output, xtrace_output))) {
        return;
    }

    /* Release caller's ownership reference */
    mr_object_del(&output->parent);
}

mr_err_t mr_xtrace_output_register(mr_xtrace_output_t *output) {
    mr_xtrace_output_t *old_output;
    int mask;

    /* Check parameter */
    if ((!output) || (!MR_OBJECT_IS_INITED(output))
        || (!MR_OBJECT_IS_CLAZZES(output, xtrace_output))) {
        return -MR_EINVAL;
    }

    /* Hold registration reference */
    if (!mr_object_get(&output->parent)) {
        return -MR_EINVAL;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xtrace_lock);

    /* Ignore duplicate registration of the same output object */
    if (__xtrace_output == output) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xtrace_lock, mask);
        mr_object_put(&output->parent);
        return 0;
    }

    /* Swap active output */
    old_output = __xtrace_output;
    __xtrace_output = output;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xtrace_lock, mask);

    /* Release previous output without spinning */
    if (old_output) {
        mr_object_put(&old_output->parent);
    }

    /* Wake new adapter if early FIFO data is already waiting */
    if (!mr_fifo_is_empty(&__xtrace_fifo)) {
        mr_atomic_store(&__xtrace_notify_armed, 0);
        mr_atomic_store(&__xtrace_notify_pending, 0);
        xtrace_notify();
    }
    return 0;
}

mr_err_t mr_xtrace_output_unregister(void) {
    mr_xtrace_output_t *old_output;
    int mask;

    /* Flush protocol staging data into FIFO */
    mr_xtrace_flush();

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xtrace_lock);

    /* Detach active output */
    old_output = __xtrace_output;
    __xtrace_output = MR_NULL;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xtrace_lock, mask);

    /* Release output reference without spinning */
    if (old_output) {
        mr_object_put(&old_output->parent);
    }
    return 0;
}

mr_ssize_t mr_xtrace_write(const void *data, mr_size_t size) {
    mr_atomic_t armed;
    mr_ssize_t written;
    int mask;

    /* Check parameter */
    if ((!data) || (size == 0U)) {
        return 0;
    }
    if (size > (mr_size_t)MR_CFG_XTRACE_BUF_SIZE) {
        return -MR_ENOMEM;
    }

    /* Lock FIFO write side (short memory-only critical section) */
    mask = mr_spinlock_lock_irqsave(&__xtrace_write_lock);

    /* Enqueue whole packet atomically to avoid corrupting binary framing */
    if (mr_fifo_avail(&__xtrace_fifo) >= (mr_uint32_t)size) {
        mr_fifo_write(&__xtrace_fifo, data, (mr_uint32_t)size);
        mr_atomic_init(&armed, 1);
        if (mr_atomic_compare_exchange(&__xtrace_notify_armed, &armed, 0)) {
            mr_atomic_store(&__xtrace_notify_pending, 1);
        }
        written = (mr_ssize_t)size;
    } else {
        written = -MR_ENOMEM;
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xtrace_write_lock, mask);
    return written;
}

static mr_bool_t xtrace_read_try_acquire(mr_atomic_t state) {
    mr_atomic_t expected;

    /* Init expected value */
    mr_atomic_init(&expected, XTRACE_READ_IDLE);

    /* Claim single-consumer read ownership */
    return mr_atomic_compare_exchange(&__xtrace_reading, &expected, state)
             ? MR_TRUE
             : MR_FALSE;
}

static void xtrace_read_release(void) {
    mr_atomic_store(&__xtrace_reading, XTRACE_READ_IDLE);
}

mr_size_t mr_xtrace_read(void *buf, mr_size_t size) {
    mr_size_t n;

    /* Check parameter */
    if ((!buf) || (size == 0U)) {
        return 0U;
    }
    if (size > (mr_size_t)MR_CFG_XTRACE_BUF_SIZE) {
        size = (mr_size_t)MR_CFG_XTRACE_BUF_SIZE;
    }

    /* Acquire single-consumer read ownership */
    if (!xtrace_read_try_acquire(XTRACE_READ_BUSY)) {
        return 0U;
    }

    /* Read from FIFO; arm edge notify and re-check when empty */
    n = (mr_size_t)mr_fifo_read(&__xtrace_fifo, buf, (mr_uint32_t)size);
    if (n == 0U) {
        mr_atomic_store(&__xtrace_notify_armed, 1);
        n = (mr_size_t)mr_fifo_read(&__xtrace_fifo, buf, (mr_uint32_t)size);
    }

    /* Release read ownership */
    xtrace_read_release();
    return n;
}

mr_size_t mr_xtrace_read_claim(void **buf, mr_size_t want) {
    mr_size_t n;

    /* Check parameter */
    if ((!buf) || (want == 0U)) {
        return 0U;
    }
    if (want > (mr_size_t)MR_CFG_XTRACE_BUF_SIZE) {
        want = (mr_size_t)MR_CFG_XTRACE_BUF_SIZE;
    }

    /* Acquire single-consumer claim ownership */
    if (!xtrace_read_try_acquire(XTRACE_READ_CLAIMED)) {
        return 0U;
    }

    /* Claim contiguous region; arm edge notify and re-check when empty */
    n = (mr_size_t)mr_fifo_read_claim(&__xtrace_fifo, 0U, buf,
                                      (mr_uint32_t)want);
    if (n == 0U) {
        mr_atomic_store(&__xtrace_notify_armed, 1);
        n = (mr_size_t)mr_fifo_read_claim(&__xtrace_fifo, 0U, buf,
                                          (mr_uint32_t)want);
        if (n == 0U) {
            xtrace_read_release();
            return 0U;
        }
    }

    __xtrace_claimed = n;
    return n;
}

void mr_xtrace_read_finish(mr_size_t len) {
    /* Ignore unless a claim is currently active */
    if (mr_atomic_load(&__xtrace_reading) != XTRACE_READ_CLAIMED) {
        return;
    }

    /* Clamp consumed length to claimed region */
    if (len > __xtrace_claimed) {
        len = __xtrace_claimed;
    }
    if (len > 0U) {
        mr_fifo_read_finish(&__xtrace_fifo, (mr_uint32_t)len);
    }
    __xtrace_claimed = 0U;

    /* Release single-consumer read ownership */
    xtrace_read_release();
}

mr_size_t mr_xtrace_pending(void) {
    return (mr_size_t)mr_fifo_len(&__xtrace_fifo);
}

mr_err_t mr_xtrace_clear(void) {
    int mask;

    /* Refuse to reset while a consumer holds an active read/claim */
    if (!xtrace_read_try_acquire(XTRACE_READ_BUSY)) {
        return -MR_EBUSY;
    }

    /* Lock write side and reset FIFO */
    mask = mr_spinlock_lock_irqsave(&__xtrace_write_lock);
    mr_fifo_reset(&__xtrace_fifo);
    mr_atomic_store(&__xtrace_notify_pending, 0);
    mr_atomic_store(&__xtrace_notify_armed, 1);
    mr_spinlock_unlock_irqrestore(&__xtrace_write_lock, mask);

    /* Release read ownership */
    xtrace_read_release();
    return 0;
}

mr_err_t mr_xtrace_flush(void) {
    mr_xtrace_t *proto;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xtrace_lock);

    /* Get active protocol reference */
    proto = xtrace_get_locked();

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xtrace_lock, mask);

    /* Flush protocol staging data into FIFO (non-blocking) */
    if (proto) {
        if (proto->ops->flush) {
            proto->ops->flush(proto->priv);
        }
        mr_object_put(&proto->parent);
    }

    /* Notify output if any bytes are pending in FIFO */
    if (!mr_fifo_is_empty(&__xtrace_fifo)) {
        mr_atomic_store(&__xtrace_notify_armed, 0);
        mr_atomic_store(&__xtrace_notify_pending, 0);
        xtrace_notify();
    }
    return 0;
}

mr_ssize_t mr_xtrace_output_read(void *buf, mr_size_t size) {
    mr_xtrace_output_t *output;
    mr_ssize_t ret;
    int mask;

    /* Check parameter */
    if ((!buf) || (size == 0U)) {
        return 0;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xtrace_lock);

    /* Get active output reference */
    output = xtrace_output_get_locked();

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xtrace_lock, mask);

    /* Call output host-to-target read */
    ret = 0;
    if (output) {
        if (output->ops->read) {
            ret = output->ops->read(output->priv, buf, size);
        }
        mr_object_put(&output->parent);
    }
    return ret;
}

void mr_xtrace_start(void) {
    mr_xtrace_t *proto;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xtrace_lock);

    /* Check whether xtrace is already running */
    if (__xtrace_running) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xtrace_lock, mask);
        return;
    }

    /* Set running and get protocol reference */
    __xtrace_running = MR_TRUE;
    proto = xtrace_get_locked();

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xtrace_lock, mask);

    /* Call protocol start */
    if (proto) {
        if (proto->ops->start) {
            proto->ops->start(proto->priv);
        }
        mr_object_put(&proto->parent);
    }

    /* Dispatch deferred notification outside locks */
    xtrace_dispatch_notify();
}

void mr_xtrace_stop(void) {
    mr_xtrace_t *proto;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xtrace_lock);

    /* Check whether xtrace is already stopped */
    if (!__xtrace_running) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xtrace_lock, mask);
        return;
    }

    /* Clear running and get protocol reference */
    __xtrace_running = MR_FALSE;
    proto = xtrace_get_locked();

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xtrace_lock, mask);

    /* Stop protocol and flush staging data into FIFO (non-blocking) */
    if (proto) {
        if (proto->ops->stop) {
            proto->ops->stop(proto->priv);
        }
        mr_object_put(&proto->parent);
    }

    /* Notify output if any bytes remain in FIFO */
    if (!mr_fifo_is_empty(&__xtrace_fifo)) {
        mr_atomic_store(&__xtrace_notify_armed, 0);
        mr_atomic_store(&__xtrace_notify_pending, 0);
        xtrace_notify();
    }
}

mr_bool_t mr_xtrace_is_running(void) {
    mr_bool_t running;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xtrace_lock);

    /* Get running state */
    running = __xtrace_running;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xtrace_lock, mask);
    return running;
}

void mr_xtrace_emit(const mr_trace_event_t *event) {
    mr_xtrace_t *proto;
    int mask;

    /* Check parameter */
    if (!event) {
        return;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xtrace_lock);

    /* Check running state and notify reentry guard */
    if ((!__xtrace_running) || (mr_atomic_load(&__xtrace_in_notify) != 0)) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xtrace_lock, mask);
        return;
    }

    /* Get active protocol reference */
    proto = xtrace_get_locked();

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xtrace_lock, mask);

    /* Call protocol emit (non-blocking, encodes into FIFO) */
    if (proto) {
        if (proto->ops->emit) {
            proto->ops->emit(proto->priv, event);
        }
        mr_object_put(&proto->parent);
    }

    /* Dispatch deferred notification outside all protocol locks */
    xtrace_dispatch_notify();
}

static void xtrace_release(mr_object_t *obj) {
    mr_xtrace_t *xtrace;

    /* Get protocol object */
    xtrace = MR_CONTAINER_OF(obj, mr_xtrace_t, parent);

    /* Deinit protocol */
    if (xtrace->ops && xtrace->ops->deinit) {
        xtrace->ops->deinit(xtrace->priv);
    }
}

static void xtrace_output_release(mr_object_t *obj) {
    mr_xtrace_output_t *output;

    /* Get output object */
    output = MR_CONTAINER_OF(obj, mr_xtrace_output_t, parent);

    /* Deinit output with reentry guard */
    if (output->ops && output->ops->deinit) {
        mr_atomic_fetch_add(&__xtrace_in_notify, 1);
        output->ops->deinit(output->priv);
        mr_atomic_fetch_sub(&__xtrace_in_notify, 1);
    }
}

static void xtrace_module_init(void) {
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xtrace_lock);

    /* Check whether xtrace is initialized */
    if (__xtrace_inited) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xtrace_lock, mask);
        return;
    }
    __xtrace_inited = MR_TRUE;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xtrace_lock, mask);

#if defined(MR_USE_MODULE_XTRACE_AUTO_START)
    /* Start xtrace */
    mr_xtrace_start();
#endif /* defined(MR_USE_MODULE_XTRACE_AUTO_START) */
}
MR_INIT_EXPORT(xtrace_module_init, MR_INIT_LEVEL_MODULE);
#endif /* defined(MR_USE_MODULE_XTRACE) */
