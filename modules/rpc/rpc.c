/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/modules/rpc/mr_rpc.h>
#if defined(MR_USE_MODULE_RPC)
#include <mrX/kernel/mr_initcall.h>
#include <mrX/kernel/mr_spinlock.h>
#include <mrX/libc/mr_malloc.h>
#include <mrX/libc/mr_string.h>

/* RPC clazzes(static & dynamic) */
static void rpc_statik_release(mr_object_t *obj);
static void rpc_dynamic_release(mr_object_t *obj);
MR_CLAZZES_DEFINE(rpc, rpc_statik_release, rpc_dynamic_release, MR_NULL);

/* RPC session operations */
static void rpc_session_complete(mr_rpc_session_t *session, mr_err_t result,
                                 void *owner);
static void rpc_session_release(mr_rpc_session_t *session, void *owner);
static const mr_rpc_session_ops_t __rpc_session_ops = {
    .complete = rpc_session_complete,
    .release = rpc_session_release,
};

/* RPC call table */
#if !defined(MR_CFG_RPC_CALL_BUCKET_NUM)
#define MR_CFG_RPC_CALL_BUCKET_NUM      (16U)
#endif /* !defined(MR_CFG_RPC_CALL_BUCKET_NUM) */
static mr_list_t __rpc_call_table_buckets[MR_CFG_RPC_CALL_BUCKET_NUM];
static mr_hashmap_t __rpc_call_table;
/* RPC lock */
static mr_spinlock_t __rpc_lock = MR_SPINLOCK_INIT();

/* RPC init start and end definition */
__MR_RPC_EXPORT(start, MR_NULL, MR_NULL, MR_NULL, 0, 0, "0.end");
__MR_RPC_EXPORT(end, MR_NULL, MR_NULL, MR_NULL, 0, 0, "8.end");

/* RPC frame definition */
#define RPC_SYNC_HEADER_MARKER_0        ('M')
#define RPC_SYNC_HEADER_MARKER_1        ('R')
#define RPC_FRAME_VERSION               (1U)
#define RPC_CHECKSUM_SIZE               (2U)
#define RPC_CALL_ID(_service, _method)                                         \
    ((mr_uint32_t)(_service) << 16U | (_method))

static void rpc_session_reset(mr_rpc_session_t *session) {
    /* Reset session data */
    session->action = MR_RPC_SESSION_ACTION_NONE;
    session->call = MR_NULL;
    session->recv.size = 0;
    session->recv.off = 0;
    session->send.size = 0;
    session->send.off = 0;
    session->deadline = 0U;
    session->dispatching = MR_FALSE;
}

static void rpc_session_init(mr_rpc_t *rpc) {
    mr_rpc_session_t *session;
    mr_uint32_t i;

    /* Init session pool */
    mr_list_init(&rpc->session_pending);
    mr_list_init(&rpc->session_free);
    for (i = 0; i < MR_ARRAY_SIZE(rpc->sessions); i++) {
        session = &rpc->sessions[i];
        mr_rpc_session_init(session, &__rpc_session_ops, MR_NULL);
        mr_list_add(&rpc->session_free, &session->list);
    }
}

static void rpc_recv_init(mr_rpc_t *rpc) {
    const mr_uint32_t size = sizeof(rpc->recv.buffer);

    /* Init receive */
    mr_fifo_init(&rpc->recv.fifo, rpc->recv.buffer, size, 1U);
    rpc->recv.state = MR_RPC_RECV_STATE_SYNC;
    rpc->recv.session = MR_NULL;
}

static void rpc_send_init(mr_rpc_t *rpc) {
    /* Init send */
    mr_list_init(&rpc->send.ready);
    rpc->send.session = MR_NULL;
    mr_atomic_init(&rpc->send.running, MR_FALSE);
}

#if defined(MR_USE_MODULE_RPC_HOOK)
static void rpc_wakeup(mr_rpc_t *rpc, mr_rpc_event_t event) {
    mr_atomic_t last;

    /* Increase event depth */
    last = mr_atomic_fetch_add(&rpc->depth[event], 1);
    if (last != 0) {
        /* No need to wakeup */
        return;
    }

    /* Wakeup RPC executor */
    if (rpc->hook && rpc->hook->wakeup) {
        rpc->hook->wakeup(rpc, event, rpc->hook->param);
    }
}

static void rpc_suspend(mr_rpc_t *rpc, mr_rpc_event_t event) {
    mr_atomic_t last;

    /* Decrease event depth */
    last = mr_atomic_fetch_sub(&rpc->depth[event], 1);
    if (last != 1) {
        return;
    }

    /* Suspend RPC executor */
    if (rpc->hook && rpc->hook->suspend) {
        rpc->hook->suspend(rpc, event, rpc->hook->param);
    }
}
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */

static void rpc_subs_timer_entry(mr_timer_t *timer, void *param) {
    mr_tick_t tickless, tick, timer_tick;
    mr_rpc_session_t *session;
#if defined(MR_USE_MODULE_RPC_HOOK)
    mr_bool_t ready;
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */
    mr_rpc_t *rpc;
    int mask;

    /* Get RPC */
    rpc = mr_rpc_get(param);
    if (!rpc) {
        /* RPC is not exist */
        mr_timer_del(timer);
        return;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Check timeout subscription */
    tickless = 0U;
#if defined(MR_USE_MODULE_RPC_HOOK)
    ready = MR_FALSE;
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */
    for (tick = mr_clock_tick(); !mr_list_is_empty(&rpc->subs.tlist);) {
        session = MR_CONTAINER_OF(rpc->subs.tlist.next, mr_rpc_session_t, list);

        /* Check timeout tick */
        if ((tick - session->deadline) >= (MR_TICK_MAX / 2U)) {
            /* There will be no timeout subscription after that */
            tickless = session->deadline - tick;
            break;
        }

        /* Move subscription to ready list */
        mr_list_move(&session->list, &rpc->subs.list);
#if defined(MR_USE_MODULE_RPC_HOOK)
        ready = MR_TRUE;
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */
    }

    /* Check whether subscription timer needs a new tick */
    timer_tick = mr_timer_tick(&rpc->subs.timer) & (~MR_TIMER_PERIODIC);
    if ((tickless != 0U) && (tickless != timer_tick)) {
        /* Set to optimal tick */
        mr_timer_tick_set(&rpc->subs.timer, tickless | MR_TIMER_PERIODIC);
        mr_timer_start(&rpc->subs.timer);

        /* Check whether subscription is stopped(timing) for the last time */
    } else if (mr_list_is_empty(&rpc->subs.tlist)) {
        /* Don't need timer */
        mr_timer_stop(&rpc->subs.timer);
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

#if defined(MR_USE_MODULE_RPC_HOOK)
    /* Wakeup subscription poll */
    if (ready) {
        rpc_wakeup(rpc, MR_RPC_EVENT_POLL);
    }
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */

    /* Put RPC */
    mr_rpc_put(rpc);
}

static mr_err_t rpc_subs_init(mr_rpc_t *rpc) {
    mr_uint32_t i;
    mr_err_t ret;

    /* Init subscription timer */
    ret = mr_timer_init(&rpc->subs.timer, rpc_subs_timer_entry, rpc, 1U);
    if (ret != 0) {
        return ret;
    }

    /* Init subscriptions */
    for (i = 0; i < MR_ARRAY_SIZE(rpc->subs.sessions); i++) {
        rpc->subs.sessions[i] = MR_NULL;
    }
    mr_list_init(&rpc->subs.tlist);
    mr_list_init(&rpc->subs.list);
    return 0;
}

static void rpc_subs_del(mr_rpc_t *rpc) {
    mr_rpc_session_t *session;
    mr_uint32_t i;
    int mask;

    /* Delete subscription timer */
    mr_timer_del(&rpc->subs.timer);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Remove scheduled subscriptions */
    while (!mr_list_is_empty(&rpc->subs.tlist)) {
        session = MR_LIST_ENTRY(rpc->subs.tlist.next, mr_rpc_session_t, list);
        mr_list_del(&session->list);
    }
    while (!mr_list_is_empty(&rpc->subs.list)) {
        session = MR_LIST_ENTRY(rpc->subs.list.next, mr_rpc_session_t, list);
        mr_list_del(&session->list);
    }
    for (i = 0; i < MR_ARRAY_SIZE(rpc->subs.sessions); i++) {
        rpc->subs.sessions[i] = MR_NULL;
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);
}

#if defined(MR_USE_MODULE_RPC_HOOK)
static void rpc_hook_init(mr_rpc_t *rpc) {
    mr_uint32_t i;

    /* Init RPC hook */
    for (i = 0; i < MR_ARRAY_SIZE(rpc->depth); i++) {
        mr_atomic_init(&rpc->depth[i], 0);
    }
    rpc->hook = MR_NULL;
}
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */

static mr_err_t rpc_init(mr_rpc_t *rpc, const mr_clazz_t *clazz,
                         const mr_rpc_ops_t *ops, void *priv) {
    mr_err_t ret;

    /* Init RPC */
    mr_atomic_init(&rpc->dying, MR_FALSE);
    rpc_session_init(rpc);
    rpc_recv_init(rpc);
    rpc_send_init(rpc);
    rpc->ops = ops;
    rpc->priv = priv;
#if defined(MR_USE_MODULE_RPC_HOOK)
    rpc_hook_init(rpc);
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */
    ret = rpc_subs_init(rpc);
    if (ret != 0) {
        return ret;
    }

    /* Init object */
    ret = mr_object_init((mr_object_t *)rpc, clazz);
    if (ret != 0) {
        rpc_subs_del(rpc);
        return ret;
    }
    return 0;
}

mr_err_t mr_rpc_init(mr_rpc_t *rpc, const mr_rpc_ops_t *ops, void *priv) {
    const mr_clazz_t *clazz = MR_CLAZZES_STATIK(rpc);

    /* Check parameter */
    MR_ASSERT(rpc != MR_NULL);
    MR_ASSERT((ops == MR_NULL) || (ops->send != MR_NULL));

    /* Init RPC */
    return rpc_init(rpc, clazz, ops, priv);
}

mr_rpc_t *mr_rpc_create(const mr_rpc_ops_t *ops, void *priv) {
    const mr_clazz_t *clazz = MR_CLAZZES_DYNAMIC(rpc);
    mr_rpc_t *rpc;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((ops == MR_NULL) || (ops->send != MR_NULL));

    /* Create RPC */
    rpc = mr_malloc(sizeof(mr_rpc_t));
    if (!rpc) {
        return MR_NULL;
    }

    /* Init RPC */
    ret = rpc_init(rpc, clazz, ops, priv);
    if (ret != 0) {
        mr_free(rpc);
        return MR_NULL;
    }
    return rpc;
}

static mr_tick_t rpc_subs_period(mr_rpc_session_t *session) {
    mr_uint32_t msec;
    mr_tick_t period;

    /* Clear the continuous bit, then reject a zero period */
    msec = session->request.value.subscribe & (~MR_RPC_SUBSCRIBE_CONTINUOUS);
    if (msec == 0U) {
        return 0U;
    }
    period = MR_TICK_MSEC(msec);
    return (period > 0U) ? period : 1U;
}

static void rpc_subs_rearm(mr_rpc_t *rpc, mr_rpc_session_t *session) {
    mr_rpc_session_t *current;
    mr_tick_t period;
    mr_list_t *l;

    /* A consumed continuous result is ready for the next poll */
    if (session->action == MR_RPC_SESSION_ACTION_REPOLL) {
        session->action = MR_RPC_SESSION_ACTION_POLL;
        mr_list_move(&session->list, &rpc->subs.list);
        return;
    }

    /* Set timeout tick */
    period = rpc_subs_period(session);
    session->deadline = mr_clock_tick() + period;

    /* Insert subscription into timer list */
    MR_LIST_FOR_EACH(l, &rpc->subs.tlist) {
        current = MR_LIST_ENTRY(l, mr_rpc_session_t, list);
        if ((session->deadline - current->deadline) < (MR_TICK_MAX / 2U)) {
            continue;
        }
        break;
    }
    mr_list_move(&session->list, l);

    /* Set to optimal tick when the nearest deadline changes */
    if (&session->list == rpc->subs.tlist.next) {
        period = session->deadline - mr_clock_tick();
        if ((period == 0U) || (period >= (MR_TICK_MAX / 2U))) {
            period = 1U;
        }
        mr_timer_tick_set(&rpc->subs.timer, period | MR_TIMER_PERIODIC);
        mr_timer_start(&rpc->subs.timer);
    }
}

static void rpc_session_consume_locked(mr_rpc_session_t *session) {
    /* End one-shot or cancelled sessions after result consumption */
    if (session->action != MR_RPC_SESSION_ACTION_POLL) {
        session->state = MR_RPC_SESSION_STATE_DONE;
        return;
    }

    /* Keep subscription storage for the next stream result */
    session->request.type = MR_RPC_MSG_STREAM;
    if (session->request.value.subscribe & MR_RPC_SUBSCRIBE_CONTINUOUS) {
        session->action = MR_RPC_SESSION_ACTION_REPOLL;
    }
    session->state = MR_RPC_SESSION_STATE_WAITING;
}

static mr_rpc_t *rpc_session_detach_locked(mr_rpc_session_t *session) {
    mr_uint32_t i;
    mr_rpc_t *rpc;

    /* Remove subscription registration */
    rpc = session->rpc;
    if ((session->request.type == MR_RPC_MSG_SUBSCRIBE)
        || (session->request.type == MR_RPC_MSG_STREAM)) {
        for (i = 0; i < MR_ARRAY_SIZE(rpc->subs.sessions); i++) {
            if (rpc->subs.sessions[i] == session) {
                rpc->subs.sessions[i] = MR_NULL;
                break;
            }
        }
    }

    /* Detach session before handing storage back to its owner */
    mr_list_del(&session->list);
    session->state = MR_RPC_SESSION_STATE_RELEASING;
    return rpc;
}

static void rpc_session_finish(mr_rpc_session_t *session) {
    mr_err_t result;
    mr_rpc_t *rpc;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Defer completion while a handler or callback still owns the session */
    if (((session->state != MR_RPC_SESSION_STATE_WAITING)
         && (session->state != MR_RPC_SESSION_STATE_READY)
         && (session->state != MR_RPC_SESSION_STATE_DONE))
        || session->dispatching) {
        goto _exit;
    }

    /* Deliver each result before scheduling or releasing the session */
    if (session->state == MR_RPC_SESSION_STATE_READY) {
        if ((session->request.type == MR_RPC_MSG_STREAM)
            && (session->response.frame.value.result == 0)
            && (session->response.frame.payload_size == 0U)) {
            /* Retry empty streams after the subscription period */
            session->state = MR_RPC_SESSION_STATE_WAITING;
        } else if ((session->action != MR_RPC_SESSION_ACTION_UNSUBSCRIBE)
                   && (!mr_atomic_load(&session->rpc->dying))) {
            /* Prevent consume or cancel from releasing callback storage */
            result = session->response.frame.value.result;
            session->dispatching = MR_TRUE;

            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

            /* Complete session */
            session->ops->complete(session, result, session->owner);

            /* Lock */
            mask = mr_spinlock_lock_irqsave(&__rpc_lock);

            /* Oneway results need no explicit consumption */
            session->dispatching = MR_FALSE;
            if (session->request.type == MR_RPC_MSG_ONEWAY) {
                session->state = MR_RPC_SESSION_STATE_DONE;
            }
        }
    }

    /* Recheck cancellation and deletion after the completion callback */
    if ((session->action == MR_RPC_SESSION_ACTION_UNSUBSCRIBE)
        || mr_atomic_load(&session->rpc->dying)) {
        session->state = MR_RPC_SESSION_STATE_DONE;
    }

    /* Schedule consumed results or retry empty streams */
    if (session->state == MR_RPC_SESSION_STATE_WAITING) {
#if defined(MR_USE_MODULE_RPC_HOOK)
        /* Retain RPC across the unlocked poll notification */
        rpc = MR_NULL;
        if (session->action == MR_RPC_SESSION_ACTION_REPOLL) {
            rpc = mr_rpc_get(session->rpc);
        }
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */

        rpc_subs_rearm(session->rpc, session);

        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

#if defined(MR_USE_MODULE_RPC_HOOK)
        /* Wakeup subscription poll */
        if (rpc) {
            rpc_wakeup(rpc, MR_RPC_EVENT_POLL);
            mr_rpc_put(rpc);
        }
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */
        return;
    }

    /* Retain ready results until transport or owner consumption */
    if (session->state != MR_RPC_SESSION_STATE_DONE) {
        goto _exit;
    }

    /* Detach session while retaining its RPC reference */
    rpc = rpc_session_detach_locked(session);

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

    /* Release session */
    session->ops->release(session, session->owner);

    /* Session storage may be freed by the release callback */
    mr_rpc_put(rpc);
    return;
_exit:
    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);
}

static void rpc_queue_release(mr_list_t *queue) {
    mr_rpc_session_t *session;
    int mask;

    /* Mark queued sessions for release */
    while (1) {
        /* Lock */
        mask = mr_spinlock_lock_irqsave(&__rpc_lock);

        /* Stop when the queue is empty */
        if (mr_list_is_empty(queue)) {
            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);
            break;
        }

        /* Detach the next session for release */
        session = MR_LIST_ENTRY(queue->next, mr_rpc_session_t, list);
        mr_list_del(&session->list);
        session->state = MR_RPC_SESSION_STATE_DONE;

        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);
        rpc_session_finish(session);
    }
}

mr_err_t mr_rpc_del(mr_rpc_t *rpc) {
    mr_rpc_session_t *recv_session;
    mr_rpc_session_t *send_session;
    int mask;

    /* Check parameter */
    MR_ASSERT((rpc != MR_NULL) && MR_OBJECT_IS_INITED(rpc));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(rpc, rpc));

    /* Set RPC dying */
    mr_atomic_store(&rpc->dying, MR_TRUE);

    /* Release queued sessions */
    rpc_queue_release(&rpc->session_pending);
    rpc_queue_release(&rpc->subs.tlist);
    rpc_queue_release(&rpc->subs.list);
    rpc_queue_release(&rpc->send.ready);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Remove receive session */
    rpc->recv.state = MR_RPC_RECV_STATE_SYNC;
    recv_session = rpc->recv.session;
    rpc->recv.session = MR_NULL;
    if (recv_session) {
        recv_session->state = MR_RPC_SESSION_STATE_DONE;
    }

    /* Remove send session */
    send_session = MR_NULL;
    if (!mr_atomic_load(&rpc->send.running)) {
        send_session = rpc->send.session;
        rpc->send.session = MR_NULL;
        if (send_session) {
            send_session->state = MR_RPC_SESSION_STATE_DONE;
        }
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

    /* Release receive session */
    if (recv_session) {
        rpc_session_finish(recv_session);
    }

    /* Release send session */
    if (send_session) {
        rpc_session_finish(send_session);
    }

    /* Delete object */
    mr_object_del((mr_object_t *)rpc);
    return 0;
}

mr_ssize_t mr_rpc_input(mr_rpc_t *rpc, const void *data, mr_uint32_t size) {
    mr_uint32_t written;

    /* Check parameter */
    MR_ASSERT((rpc != MR_NULL) && MR_OBJECT_IS_INITED(rpc));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(rpc, rpc));
    MR_ASSERT((data != MR_NULL) || (size == 0U));

    /* Check whether RPC is dying */
    if (mr_atomic_load(&rpc->dying)) {
        return -MR_ENOENT;
    }

    /* Ignore empty input */
    if (size == 0U) {
        return 0;
    }
    /* Local-only RPC cannot accept framed transport input */
    if (!rpc->ops) {
        return -MR_ENOSYS;
    }

    /* Check FIFO capacity before committing any data */
    if (mr_fifo_avail(&rpc->recv.fifo) < size) {
        return -MR_EAGAIN;
    }

    /* Commit complete input */
    written = mr_fifo_write(&rpc->recv.fifo, data, size);
    if (written != size) {
        return -MR_EAGAIN;
    }

#if defined(MR_USE_MODULE_RPC_HOOK)
    /* Wakeup RPC */
    rpc_wakeup(rpc, MR_RPC_EVENT_EXECUTE);
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */
    return (mr_ssize_t)written;
}

mr_err_t mr_rpc_submit(mr_rpc_t *rpc, mr_rpc_session_t *session) {
    mr_err_t ret;
    int mask;

    /* Check parameter */
    MR_ASSERT((rpc != MR_NULL) && MR_OBJECT_IS_INITED(rpc));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(rpc, rpc));
    MR_ASSERT(session != MR_NULL);
    MR_ASSERT(session->ops != MR_NULL);
    MR_ASSERT(session->ops->complete != MR_NULL);
    MR_ASSERT(session->ops->release != MR_NULL);

    /* Get RPC */
    if (!mr_rpc_get(rpc)) {
        return -MR_ENOENT;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Check whether RPC is dying */
    if (mr_atomic_load(&rpc->dying)) {
        ret = -MR_ENOENT;
        goto _exit;
    }

    /* Check session state */
    if ((session->rpc != MR_NULL) || (!mr_list_is_empty(&session->list))
        || (session->state != MR_RPC_SESSION_STATE_FREE)) {
        ret = -MR_EINVAL;
        goto _exit;
    }

    /* Check request */
    if ((session->recv.size > sizeof(session->request.payload))
        || ((session->request.type != MR_RPC_MSG_SUBSCRIBE)
            && (session->request.type != MR_RPC_MSG_ONEWAY)
            && (session->request.type != MR_RPC_MSG_REQUEST))) {
        ret = -MR_EINVAL;
        goto _exit;
    }

    /* Submit session */
    mr_list_add(&rpc->session_pending, &session->list);
    session->state = MR_RPC_SESSION_STATE_PENDING;
    session->rpc = rpc;
    ret = 0;
_exit:
    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

    /* Put RPC */
    if (ret != 0) {
        mr_rpc_put(rpc);
        return ret;
    }

#if defined(MR_USE_MODULE_RPC_HOOK)
    /* Wakeup RPC */
    rpc_wakeup(rpc, MR_RPC_EVENT_EXECUTE);
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */
    return 0;
}

static mr_uint16_t rpc_fletcher16(const void *data, mr_uint32_t size) {
    const mr_uint8_t *bytes = data;
    mr_uint32_t sum1, sum2, n, x;

    /* Fletcher-16, delayed modulo 255, empty data is 0 */
    sum1 = 0U;
    sum2 = 0U;
    while (size != 0U) {
        n = size;
        if (n > 32U) {
            n = 32U;
        }
        size -= n;
        while (n != 0U) {
            sum1 += *bytes++;
            sum2 += sum1;
            n--;
        }
        x = (sum1 & 0xffU) + (sum1 >> 8U);
        x = (x & 0xffU) + (x >> 8U);
        if (x >= 255U) {
            x -= 255U;
        }
        sum1 = x;
        x = (sum2 & 0xffU) + (sum2 >> 8U);
        x = (x & 0xffU) + (x >> 8U);
        if (x >= 255U) {
            x -= 255U;
        }
        sum2 = x;
    }
    return (mr_uint16_t)((sum2 << 8U) | sum1);
}

static mr_rpc_session_t *rpc_session_acquire(mr_rpc_t *rpc) {
    mr_rpc_session_t *session;
    int mask;

    /* Init session */
    session = MR_NULL;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Check whether RPC is dying */
    if (mr_atomic_load(&rpc->dying)) {
        goto _exit;
    }

    /* Check session pool */
    if (mr_list_is_empty(&rpc->session_free)) {
        goto _exit;
    }

    /* Get RPC */
    if (!mr_rpc_get(rpc)) {
        goto _exit;
    }

    /* Acquire session */
    session = MR_LIST_ENTRY(rpc->session_free.next, mr_rpc_session_t, list);
    session->state = MR_RPC_SESSION_STATE_PENDING;
    mr_list_del(&session->list);
    session->rpc = rpc;
    rpc_session_reset(session);
_exit:
    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);
    return session;
}

static void rpc_frame_header_parse(mr_rpc_t *rpc) {
    mr_rpc_session_t *session;
    mr_rpc_frame_t frame;
    mr_uint16_t checksum;

    /* Search header */
    while (mr_fifo_peek(&rpc->recv.fifo, &frame, sizeof(frame))
           == sizeof(frame)) {
        /* Check marker */
        if ((frame.header[0] != RPC_SYNC_HEADER_MARKER_0)
            || (frame.header[1] != RPC_SYNC_HEADER_MARKER_1)) {
            mr_fifo_skip(&rpc->recv.fifo, 1U);
            continue;
        }

        /* Check header checksum */
        checksum = frame.hdr_checksum;
        frame.hdr_checksum = 0;
        if (rpc_fletcher16(&frame, sizeof(frame)) != checksum) {
            mr_fifo_skip(&rpc->recv.fifo, 2U);
            continue;
        }

        /* Check protocol */
        if ((frame.version != RPC_FRAME_VERSION)
            || ((frame.type != MR_RPC_MSG_REQUEST)
                && (frame.type != MR_RPC_MSG_ONEWAY)
                && (frame.type != MR_RPC_MSG_SUBSCRIBE)
                && (frame.type != MR_RPC_MSG_UNSUBSCRIBE))) {
            mr_fifo_skip(&rpc->recv.fifo, 2U);
            continue;
        }

        /* Acquire receive session */
        session = rpc_session_acquire(rpc);
        if (!session) {
            /* Wait for a free session */
            return;
        }

        /* Consume header */
        mr_fifo_skip(&rpc->recv.fifo, sizeof(frame));
        rpc->recv.session = session;

        /* Store request */
        session->recv.size = frame.payload_size;
        session->request.type = frame.type;
        session->request.flags = frame.flags;
        session->request.session = frame.session;
        session->request.service = frame.service;
        session->request.method = frame.method;
        session->request.value = frame.value;

        /* Check payload size */
        if (session->recv.size > sizeof(session->request.payload)) {
            rpc->recv.state = MR_RPC_RECV_STATE_DISCARD;
            break;
        }

        /* Receive payload */
        rpc->recv.state = MR_RPC_RECV_STATE_PAYLOAD;
        break;
    }
}

static mr_bool_t rpc_frame_payload_parse(mr_rpc_t *rpc) {
    mr_rpc_session_t *session;
    mr_uint32_t remain, ret;
    mr_uint8_t *payload;

    /* Read payload */
    session = rpc->recv.session;
    payload = (mr_uint8_t *)&session->request.payload + session->recv.off;
    remain = session->recv.size - session->recv.off;
    ret = mr_fifo_read(&rpc->recv.fifo, payload, remain);
    session->recv.off += ret;
    if (ret < remain) {
        /* Wait for more payload */
        return MR_FALSE;
    }

    /* Check checksum */
    if (session->request.flags & MR_RPC_FRAME_FLAG_CHECKSUM) {
        rpc->recv.state = MR_RPC_RECV_STATE_CHECKSUM;
        return MR_FALSE;
    }
    return MR_TRUE;
}

static mr_bool_t rpc_frame_checksum_parse(mr_rpc_t *rpc, mr_err_t *error) {
    mr_uint16_t checksum, expect;
    mr_rpc_session_t *session;

    /* Peek checksum trailer */
    if (mr_fifo_peek(&rpc->recv.fifo, &checksum, sizeof(checksum))
        != sizeof(checksum)) {
        /* Wait for checksum */
        return MR_FALSE;
    }

    /* Check payload */
    session = rpc->recv.session;
    if (session->recv.size <= sizeof(session->request.payload)) {
        expect = rpc_fletcher16(&session->request.payload, session->recv.size);
        *error = (checksum != expect) ? -MR_EINVAL : 0;
    } else {
        *error = -MR_ENOMEM;
    }

    /* Skip checksum */
    mr_fifo_skip(&rpc->recv.fifo, sizeof(checksum));
    return MR_TRUE;
}

static mr_bool_t rpc_frame_payload_discard(mr_rpc_t *rpc) {
    mr_rpc_session_t *session;
    mr_uint32_t remain, ret;

    /* Discard payload */
    session = rpc->recv.session;
    remain = session->recv.size - session->recv.off;
    ret = mr_fifo_skip(&rpc->recv.fifo, remain);
    session->recv.off += ret;
    if (ret < remain) {
        /* Wait for more payload */
        return MR_FALSE;
    }

    /* Check checksum */
    if (session->request.flags & MR_RPC_FRAME_FLAG_CHECKSUM) {
        rpc->recv.state = MR_RPC_RECV_STATE_CHECKSUM;
        return MR_FALSE;
    }
    return MR_TRUE;
}

static mr_rpc_session_t *rpc_frame_complete(mr_rpc_t *rpc) {
    mr_rpc_session_t *session;

    /* Get receive session */
    session = rpc->recv.session;

    /* Complete frame */
    rpc->recv.state = MR_RPC_RECV_STATE_SYNC;
    rpc->recv.session = MR_NULL;
    return session;
}

static mr_rpc_session_t *rpc_frame_parse(mr_rpc_t *rpc, mr_err_t *error) {
    mr_rpc_recv_state_t last_state;

    do {
        /* Save RPC state */
        last_state = rpc->recv.state;

        switch (rpc->recv.state) {
            case MR_RPC_RECV_STATE_SYNC: {
                /* Parse header */
                rpc_frame_header_parse(rpc);
                break;
            }
            case MR_RPC_RECV_STATE_PAYLOAD: {
                /* Parse payload */
                if (rpc_frame_payload_parse(rpc)) {
                    *error = 0;
                    return rpc_frame_complete(rpc);
                }
                break;
            }
            case MR_RPC_RECV_STATE_CHECKSUM: {
                /* Check payload trailer */
                if (rpc_frame_checksum_parse(rpc, error)) {
                    return rpc_frame_complete(rpc);
                }
                break;
            }
            case MR_RPC_RECV_STATE_DISCARD: {
                /* Report overflow */
                if (rpc_frame_payload_discard(rpc)) {
                    *error = -MR_ENOMEM;
                    return rpc_frame_complete(rpc);
                }
                break;
            }
        }

        /* Stop when parsing makes no progress */
    } while (rpc->recv.state != last_state);
    return MR_NULL;
}

static mr_err_t rpc_subs_add(mr_rpc_t *rpc, mr_rpc_session_t *session) {
    mr_rpc_session_t **free_slot;
    mr_rpc_session_t *current;
    mr_uint32_t count, i;
    mr_err_t ret;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Check session action */
    if (session->action == MR_RPC_SESSION_ACTION_UNSUBSCRIBE) {
        ret = -MR_ENOENT;
        goto _exit;
    }

    /* Find subscription slot */
    free_slot = MR_NULL;
    count = 0U;
    for (i = 0; i < MR_ARRAY_SIZE(rpc->subs.sessions); i++) {
        current = rpc->subs.sessions[i];
        if (!current) {
            if (!free_slot) {
                free_slot = &rpc->subs.sessions[i];
            }
            continue;
        }
        if (current->request.session == session->request.session) {
            ret = -MR_EEXIST;
            goto _exit;
        }
        count++;
    }

    /* Reserve at least one session for unsubscribe and normal requests */
    if ((!free_slot) || ((count + 1U) >= MR_ARRAY_SIZE(rpc->sessions))) {
        ret = -MR_ENOMEM;
        goto _exit;
    }

    /* Add subscription */
    *free_slot = session;
    ret = 0;
_exit:
    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);
    return ret;
}

static mr_err_t rpc_subs_cancel(mr_rpc_t *rpc, mr_rpc_session_t *request) {
    mr_rpc_session_t *released;
    mr_rpc_session_t *session;
    mr_uint32_t i;
    mr_err_t ret;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Find subscription */
    released = MR_NULL;
    ret = -MR_ENOENT;
    for (i = 0; i < MR_ARRAY_SIZE(rpc->subs.sessions); i++) {
        session = rpc->subs.sessions[i];
        if (!session) {
            continue;
        }
        if (session->request.session != request->request.session) {
            continue;
        }

        /* Remove subscription */
        rpc->subs.sessions[i] = MR_NULL;

        /* Cancel subscription */
        if (((session->state == MR_RPC_SESSION_STATE_READY)
             || (session->state == MR_RPC_SESSION_STATE_WAITING))
            && (!session->dispatching)) {
            mr_list_del(&session->list);
            session->state = MR_RPC_SESSION_STATE_DONE;
            released = session;
        } else {
            session->action = MR_RPC_SESSION_ACTION_UNSUBSCRIBE;
        }
        ret = 0;
        break;
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

    /* Finish session */
    if (released) {
        rpc_session_finish(released);
    }
    return ret;
}

static void rpc_session_encode_frame(mr_rpc_session_t *session,
                                     mr_rpc_msg_type_t type) {
    const mr_uint32_t payload_size = session->response.frame.payload_size;
    mr_rpc_frame_t *frame;
    mr_uint16_t checksum;
    mr_uint8_t *buf;

    /* Encode frame */
    frame = &session->response.frame;
    frame->header[0] = RPC_SYNC_HEADER_MARKER_0;
    frame->header[1] = RPC_SYNC_HEADER_MARKER_1;
    frame->version = RPC_FRAME_VERSION;
    frame->type = (mr_uint8_t)type;
    frame->flags = session->request.flags;
    frame->session = session->request.session;
    frame->service = session->request.service;
    frame->method = session->request.method;
    frame->hdr_checksum = 0;
    frame->hdr_checksum = rpc_fletcher16(frame, sizeof(*frame));

    /* Set send buffer */
    buf = (mr_uint8_t *)&session->response;
    session->send.size = sizeof(*frame) + payload_size;

    /* Encode checksum */
    if (session->request.flags & MR_RPC_FRAME_FLAG_CHECKSUM) {
        checksum = rpc_fletcher16(&session->response.payload, payload_size);
        mr_memcpy(buf + session->send.size, &checksum, sizeof(checksum));
        session->send.size += RPC_CHECKSUM_SIZE;
    }
    session->send.off = 0U;
}

static mr_err_t rpc_result_prepare(mr_rpc_session_t *session, mr_err_t result,
                                   mr_uint32_t *payload_size) {
    mr_err_t ret;

    /* Prepare result */
    switch (session->request.type) {
        case MR_RPC_MSG_SUBSCRIBE: {
            if (rpc_subs_period(session) == 0U) {
                return -MR_EINVAL;
            }
            break;
        }
        case MR_RPC_MSG_UNSUBSCRIBE: {
            if (session->recv.size != 0U) {
                return -MR_EINVAL;
            }
            return rpc_subs_cancel(session->rpc, session);
        }
        case MR_RPC_MSG_REQUEST:
        case MR_RPC_MSG_STREAM: {
            ret = session->call->encode(session);
            if (ret < 0) {
                return ret;
            }
            *payload_size = (mr_uint32_t)ret;
            break;
        }
        case MR_RPC_MSG_ONEWAY: {
            break;
        }
        default: {
            return -MR_EINVAL;
        }
    }
    return result;
}

static mr_err_t rpc_session_respond(mr_rpc_session_t *session, mr_err_t result,
                                    mr_bool_t automatic) {
    mr_rpc_session_action_t action;
    mr_uint32_t payload_size;
    mr_bool_t cancelled;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Claim the response storage before encoding outside the lock */
    if ((session->state != MR_RPC_SESSION_STATE_DISPATCHING)
        && (automatic || (session->state != MR_RPC_SESSION_STATE_HELD))) {
        mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);
        return automatic ? 0 : -MR_EPERM;
    }
    cancelled = (session->action == MR_RPC_SESSION_ACTION_UNSUBSCRIBE)
             || mr_atomic_load(&session->rpc->dying);
    session->state = MR_RPC_SESSION_STATE_ENCODING;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

    /* Prepare compact response data */
    payload_size = 0U;
    if ((!cancelled) && (result >= 0)) {
        result = rpc_result_prepare(session, result, &payload_size);
    }
    if (payload_size > sizeof(session->response.payload)) {
        result = -MR_ENOMEM;
    }
    if (result < 0) {
        payload_size = 0U;
    }

    /* Continue subscriptions only after successful results */
    action = MR_RPC_SESSION_ACTION_NONE;
    if (((session->request.type == MR_RPC_MSG_SUBSCRIBE)
         || (session->request.type == MR_RPC_MSG_STREAM))
        && (result >= 0)) {
        action = MR_RPC_SESSION_ACTION_POLL;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Publish the result without overwriting cancellation during encoding */
    session->response.frame.payload_size = payload_size;
    session->response.frame.value.result = result;
    if (session->action != MR_RPC_SESSION_ACTION_UNSUBSCRIBE) {
        session->state = MR_RPC_SESSION_STATE_READY;
        session->action = action;
    } else {
        session->state = MR_RPC_SESSION_STATE_DONE;
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

    /* Deliver or release session */
    rpc_session_finish(session);
    return 0;
}

static const mr_rpc_call_t *rpc_call_find(mr_uint32_t call_id) {
    mr_hashnode_t *node;

    /* Find RPC call by ID */
    node = mr_hashmap_find(&__rpc_call_table, call_id);
    return node ? (const mr_rpc_call_t *)mr_hashnode_value(node) : MR_NULL;
}

static void rpc_session_dispatch(mr_rpc_session_t *session, mr_err_t result) {
    mr_bool_t invoke;
    mr_uint32_t id;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Set session dispatching */
    session->state = MR_RPC_SESSION_STATE_DISPATCHING;
    session->dispatching = MR_TRUE;

    /* Check whether RPC is dying */
    if (mr_atomic_load(&session->rpc->dying)) {
        result = -MR_ENOENT;
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

    /* Decode request */
    if ((result >= 0) && (session->request.type != MR_RPC_MSG_UNSUBSCRIBE)
        && (session->request.type != MR_RPC_MSG_STREAM)) {
        id = RPC_CALL_ID(session->request.service, session->request.method);
        session->call = rpc_call_find(id);
        if (session->call) {
            result = session->call->decode(session);
        } else {
            result = -MR_ENOSYS;
        }
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Check result */
    invoke = (result >= 0);

    /* Check session state */
    if (session->state != MR_RPC_SESSION_STATE_DISPATCHING) {
        invoke = MR_FALSE;
    }

    /* Check session action */
    if (session->action == MR_RPC_SESSION_ACTION_UNSUBSCRIBE) {
        invoke = MR_FALSE;
    }

    /* Check request type */
    if ((session->request.type != MR_RPC_MSG_REQUEST)
        && (session->request.type != MR_RPC_MSG_ONEWAY)
        && (session->request.type != MR_RPC_MSG_STREAM)) {
        invoke = MR_FALSE;
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

    /* Invoke session */
    if (invoke) {
        result = session->call->invoke(session);
    }

    /* Respond session */
    rpc_session_respond(session, result, MR_TRUE);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Clear session dispatching */
    session->dispatching = MR_FALSE;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

    /* Finish session */
    rpc_session_finish(session);
}

static mr_err_t rpc_send_bytes(mr_rpc_t *rpc, mr_rpc_session_t *session) {
    mr_uint32_t remain;
    mr_uint8_t *buf;
    mr_ssize_t sent;

    /* Get send buffer */
    buf = (mr_uint8_t *)&session->response + session->send.off;
    remain = session->send.size - session->send.off;

    /* Send data */
    sent = rpc->ops->send(rpc->priv, buf, remain);
    if (sent <= 0) {
        return (sent < 0) ? (mr_err_t)sent : -MR_EAGAIN;
    }
    if ((mr_uint64_t)sent > remain) {
        return -MR_EIO;
    }
    session->send.off += (mr_uint32_t)sent;
    return 0;
}

static mr_err_t rpc_send_execute(mr_rpc_t *rpc, mr_bool_t flush) {
    mr_rpc_session_t *finished;
    mr_rpc_session_t *session;
    mr_err_t flush_ret, ret;
    mr_list_t *l;
    int mask;

    /* Check transport */
    if (!rpc->ops) {
        return 0;
    }

    /* Acquire sender */
    if (mr_atomic_exchange(&rpc->send.running, MR_TRUE)) {
        return 0;
    }

    while (1) {
        /* Lock */
        mask = mr_spinlock_lock_irqsave(&__rpc_lock);

        /* Get send session */
        session = rpc->send.session;
        finished = MR_NULL;

        /* Check whether RPC is dying */
        if (mr_atomic_load(&rpc->dying)) {
            if (session) {
                rpc->send.session = MR_NULL;
                session->state = MR_RPC_SESSION_STATE_DONE;
                finished = session;
            }

            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

            /* Finish session */
            if (finished) {
                rpc_session_finish(finished);
            }
            ret = -MR_ENOENT;
            break;
        }

        /* Complete send session */
        if (session && (session->send.off == session->send.size)) {
            rpc->send.session = MR_NULL;

            /* Consume session */
            rpc_session_consume_locked(session);
            finished = session;
            session = MR_NULL;
        }

        /* Get ready session */
        if ((!session) && (!mr_list_is_empty(&rpc->send.ready))) {
            l = rpc->send.ready.next;
            session = MR_LIST_ENTRY(l, mr_rpc_session_t, list);
            session->state = MR_RPC_SESSION_STATE_SENDING;
            mr_list_del(&session->list);
            rpc->send.session = session;
        }

        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

        /* Finish session */
        if (finished) {
            rpc_session_finish(finished);
        }

        if (!session) {
            /* Flush transport */
            if (flush && rpc->ops->flush) {
                ret = rpc->ops->flush(rpc->priv);
            } else {
                ret = 0;
            }

            /* Lock */
            mask = mr_spinlock_lock_irqsave(&__rpc_lock);

            /* Check send queue */
            if ((ret < 0) || mr_list_is_empty(&rpc->send.ready)) {
                mr_atomic_store(&rpc->send.running, MR_FALSE);

                /* Unlock */
                mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);
                return ret;
            }

            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);
            continue;
        }

        /* Send session */
        ret = rpc_send_bytes(rpc, session);
        if (ret < 0) {
            break;
        }
    }

    /* Flush transport */
    if ((ret == (-MR_EAGAIN)) && flush && rpc->ops->flush) {
        flush_ret = rpc->ops->flush(rpc->priv);
        if (flush_ret < 0) {
            ret = flush_ret;
        }
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Release active send session if RPC became dying during send */
    if (mr_atomic_load(&rpc->dying) && rpc->send.session) {
        finished = rpc->send.session;
        rpc->send.session = MR_NULL;
        finished->state = MR_RPC_SESSION_STATE_DONE;
        ret = -MR_ENOENT;
    } else {
        finished = MR_NULL;
    }

    /* Release sender */
    mr_atomic_store(&rpc->send.running, MR_FALSE);

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

    /* Finish session */
    if (finished) {
        rpc_session_finish(finished);
    }
    return ret;
}

mr_err_t mr_rpc_execute(mr_rpc_t *rpc) {
    mr_rpc_session_t *session;
    mr_err_t error, ret;
#if defined(MR_USE_MODULE_RPC_HOOK)
    mr_bool_t pending;
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */
    mr_list_t oplist;
    int mask;

    /* Check parameter */
    MR_ASSERT((rpc != MR_NULL) && MR_OBJECT_IS_INITED(rpc));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(rpc, rpc));

    /* Check whether RPC is dying */
    if (mr_atomic_load(&rpc->dying)) {
        return -MR_ENOENT;
    }

    /* Send pending responses */
    ret = rpc_send_execute(rpc, MR_FALSE);

    /* Init operation list */
    mr_list_init(&oplist);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Snapshot pending external sessions for this batch */
    mr_list_replace(&oplist, &rpc->session_pending);

#if defined(MR_USE_MODULE_RPC_HOOK)
    /* Set execute running */
    mr_atomic_store(&rpc->depth[MR_RPC_EVENT_EXECUTE], 1);
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */

    /* Dispatch snapshotted external sessions */
    while (!mr_list_is_empty(&oplist)) {
        session = MR_LIST_ENTRY(oplist.next, mr_rpc_session_t, list);
        session->state = MR_RPC_SESSION_STATE_DISPATCHING;
        mr_list_del(&session->list);

        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

        /* Dispatch session */
        rpc_session_dispatch(session, 0);

        /* Lock */
        mask = mr_spinlock_lock_irqsave(&__rpc_lock);
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

    /* Parse and dispatch remote frames */
    while (1) {
        session = rpc_frame_parse(rpc, &error);
        if (!session) {
            break;
        }
        rpc_session_dispatch(session, error);
        /* A transport failure must not stop local dispatch. Preserve the
         * error and retry sending on the next execute/flush call. */
        if ((ret >= 0) || (ret == -MR_EAGAIN)) {
            ret = rpc_send_execute(rpc, MR_FALSE);
        }
    }

    /* Flush responses */
    if ((ret == 0) || (ret == -MR_EAGAIN)) {
        ret = rpc_send_execute(rpc, MR_TRUE);
    }

#if defined(MR_USE_MODULE_RPC_HOOK)
    /* Check whether callbacks committed the next batch */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);
    pending = !mr_list_is_empty(&rpc->session_pending);
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

    /* Suspend execute */
    if (!pending) {
        rpc_suspend(rpc, MR_RPC_EVENT_EXECUTE);
    }
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */
    return ret;
}

mr_err_t mr_rpc_flush(mr_rpc_t *rpc) {
    /* Check parameter */
    MR_ASSERT((rpc != MR_NULL) && MR_OBJECT_IS_INITED(rpc));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(rpc, rpc));

    /* Flush responses */
    return rpc_send_execute(rpc, MR_TRUE);
}

mr_err_t mr_rpc_poll(mr_rpc_t *rpc) {
    mr_rpc_session_t *session;
#if defined(MR_USE_MODULE_RPC_HOOK)
    mr_bool_t pending;
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */
    mr_list_t oplist;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    MR_ASSERT((rpc != MR_NULL) && MR_OBJECT_IS_INITED(rpc));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(rpc, rpc));

    /* Check whether RPC is dying */
    if (mr_atomic_load(&rpc->dying)) {
        return -MR_ENOENT;
    }

    /* Init operation list */
    mr_list_init(&oplist);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Get subscriptions */
    mr_list_replace(&oplist, &rpc->subs.list);

#if defined(MR_USE_MODULE_RPC_HOOK)
    /* Set poll running */
    mr_atomic_store(&rpc->depth[MR_RPC_EVENT_POLL], 1);
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */

    while (!mr_list_is_empty(&oplist)) {
        session = MR_LIST_ENTRY(oplist.next, mr_rpc_session_t, list);
        session->state = MR_RPC_SESSION_STATE_DISPATCHING;
        mr_list_del(&session->list);

        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

        /* Dispatch subscription */
        rpc_session_dispatch(session, 0);

        /* Lock */
        mask = mr_spinlock_lock_irqsave(&__rpc_lock);
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

    /* Send streams */
    ret = rpc_send_execute(rpc, MR_TRUE);

#if defined(MR_USE_MODULE_RPC_HOOK)
    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Check for the next pending batch */
    pending = !mr_list_is_empty(&rpc->subs.list);

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

    /* Suspend poll */
    if (!pending) {
        rpc_suspend(rpc, MR_RPC_EVENT_POLL);
    }
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */
    return ret;
}

void mr_rpc_session_init(mr_rpc_session_t *session,
                         const mr_rpc_session_ops_t *ops, void *owner) {
    /* Check parameter */
    MR_ASSERT(session != MR_NULL);
    MR_ASSERT(ops != MR_NULL);
    MR_ASSERT(ops->complete != MR_NULL);
    MR_ASSERT(ops->release != MR_NULL);

    /* Init session */
    session->state = MR_RPC_SESSION_STATE_FREE;
    session->rpc = MR_NULL;
    session->ops = ops;
    session->owner = owner;
    rpc_session_reset(session);
    mr_list_init(&session->list);
}

mr_err_t mr_rpc_session_hold(mr_rpc_session_t *session) {
    mr_err_t ret;
    int mask;

    /* Check parameter */
    MR_ASSERT(session != MR_NULL);
    MR_ASSERT(session->rpc != MR_NULL);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Check session state */
    if ((session->state != MR_RPC_SESSION_STATE_DISPATCHING)
        || (session->action == MR_RPC_SESSION_ACTION_UNSUBSCRIBE)) {
        ret = -MR_EPERM;
        goto _exit;
    }

    /* Check whether RPC is dying */
    if (mr_atomic_load(&session->rpc->dying)) {
        ret = -MR_ENOENT;
        goto _exit;
    }

    /* Hold session */
    session->state = MR_RPC_SESSION_STATE_HELD;
    ret = 0;
_exit:
    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);
    return ret;
}

mr_err_t mr_rpc_session_reply(mr_rpc_session_t *session, mr_err_t result) {
    /* Check parameter */
    MR_ASSERT(session != MR_NULL);
    MR_ASSERT(session->rpc != MR_NULL);

    /* Check request type */
    if ((session->request.type != MR_RPC_MSG_REQUEST)
        && (session->request.type != MR_RPC_MSG_STREAM)
        && (session->request.type != MR_RPC_MSG_ONEWAY)) {
        return -MR_EPERM;
    }

    /* Reply session */
    return rpc_session_respond(session, result, MR_FALSE);
}

mr_err_t mr_rpc_session_consume(mr_rpc_session_t *session) {
    mr_err_t ret;
    int mask;

    /* Check parameter */
    MR_ASSERT(session != MR_NULL);
    MR_ASSERT(session->rpc != MR_NULL);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Check session state */
    if (session->state == MR_RPC_SESSION_STATE_READY) {
        /* Consume session */
        rpc_session_consume_locked(session);
        ret = 0;
    } else {
        ret = -MR_EPERM;
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

    /* Finish session */
    if (ret == 0) {
        rpc_session_finish(session);
    }
    return ret;
}

mr_err_t mr_rpc_session_cancel(mr_rpc_session_t *session) {
    int mask;

    /* Check parameter */
    MR_ASSERT(session != MR_NULL);
    MR_ASSERT(session->rpc != MR_NULL);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Check session state */
    switch (session->state) {
        case MR_RPC_SESSION_STATE_FREE:
        case MR_RPC_SESSION_STATE_DONE:
        case MR_RPC_SESSION_STATE_RELEASING: {
            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);
            return -MR_EPERM;
        }
        case MR_RPC_SESSION_STATE_PENDING:
        case MR_RPC_SESSION_STATE_READY:
        case MR_RPC_SESSION_STATE_WAITING: {
            session->state = MR_RPC_SESSION_STATE_DONE;
            mr_list_del(&session->list);
            break;
        }
        default: {
            break;
        }
    }

    /* Cancel session */
    session->action = MR_RPC_SESSION_ACTION_UNSUBSCRIBE;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

    /* Finish session */
    rpc_session_finish(session);
    return 0;
}

mr_err_t mr_rpc_session_drop(mr_rpc_session_t *session) {
    int mask;

    /* Check parameter */
    MR_ASSERT(session != MR_NULL);
    MR_ASSERT(session->rpc != MR_NULL);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Check session state */
    if (session->state != MR_RPC_SESSION_STATE_HELD) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);
        return -MR_EPERM;
    }

    /* Drop session */
    session->state = MR_RPC_SESSION_STATE_DONE;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

    /* Finish session */
    rpc_session_finish(session);
    return 0;
}

static void rpc_del(mr_rpc_t *rpc) {
    mr_rpc_session_t *session;
    int mask;

    /* Set RPC dying */
    mr_atomic_store(&rpc->dying, MR_TRUE);

    /* Delete subscriptions */
    rpc_subs_del(rpc);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Remove ready sessions */
    while (!mr_list_is_empty(&rpc->send.ready)) {
        session = MR_LIST_ENTRY(rpc->send.ready.next, mr_rpc_session_t, list);
        mr_list_del(&session->list);
    }

    /* Clear active sessions */
    rpc->recv.session = MR_NULL;
    rpc->send.session = MR_NULL;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

    /* Clear RPC */
#if defined(MR_USE_MODULE_RPC_HOOK)
    rpc->hook = MR_NULL;
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */
    rpc->ops = MR_NULL;
    rpc->priv = MR_NULL;
}

static void rpc_session_complete(mr_rpc_session_t *session, mr_err_t result,
                                 void *owner) {
    mr_rpc_msg_type_t type;
    mr_err_t ret;
    int mask;

    MR_IGNORE(owner);

    /* Add subscription */
    if ((session->request.type == MR_RPC_MSG_SUBSCRIBE) && (result >= 0)) {
        ret = rpc_subs_add(session->rpc, session);
        if (ret < 0) {
            session->response.frame.value.result = ret;

            /* Lock */
            mask = mr_spinlock_lock_irqsave(&__rpc_lock);

            /* Clear session action */
            if (session->action != MR_RPC_SESSION_ACTION_UNSUBSCRIBE) {
                session->action = MR_RPC_SESSION_ACTION_NONE;
            }

            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);
        }
    }

    /* Encode frame */
    if (session->request.type != MR_RPC_MSG_ONEWAY) {
        if ((session->request.type == MR_RPC_MSG_STREAM)
            && (session->response.frame.value.result >= 0)) {
            type = MR_RPC_MSG_STREAM;
        } else {
            type = MR_RPC_MSG_RESPONSE;
        }
        rpc_session_encode_frame(session, type);
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Queue session */
    if ((session->action != MR_RPC_SESSION_ACTION_UNSUBSCRIBE)
        && (session->request.type != MR_RPC_MSG_ONEWAY)
        && (session->state == MR_RPC_SESSION_STATE_READY)) {
        mr_list_add(&session->rpc->send.ready, &session->list);
    } else {
        session->state = MR_RPC_SESSION_STATE_DONE;
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);
}

static void rpc_session_release(mr_rpc_session_t *session, void *owner) {
    mr_rpc_t *rpc;
    int mask;

    MR_IGNORE(owner);

    /* Get RPC */
    rpc = session->rpc;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Release session */
    session->rpc = MR_NULL;
    session->state = MR_RPC_SESSION_STATE_FREE;
    mr_list_add(&rpc->session_free, &session->list);

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

#if defined(MR_USE_MODULE_RPC_HOOK)
    /* Wakeup RPC */
    if (!mr_fifo_is_empty(&rpc->recv.fifo)) {
        rpc_wakeup(rpc, MR_RPC_EVENT_EXECUTE);
    }
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */
}

static mr_rpc_t *rpc_obj_release(mr_object_t *obj) {
    mr_rpc_t *rpc;

    /* Get RPC */
    rpc = MR_CONTAINER_OF(obj, mr_rpc_t, parent);

    /* Delete RPC */
    rpc_del(rpc);
    return rpc;
}

static void rpc_statik_release(mr_object_t *obj) {
    /* Release RPC */
    rpc_obj_release(obj);
}

static void rpc_dynamic_release(mr_object_t *obj) {
    mr_rpc_t *rpc;

    /* Release RPC */
    rpc = rpc_obj_release(obj);

    /* Free RPC */
    mr_free(rpc);
}

static void rpc_module_init(void) {
    const mr_uintptr_t start = (mr_uintptr_t)&__mr_rpc_call_start;
    const mr_uintptr_t end = (mr_uintptr_t)&__mr_rpc_call_end;
    const mr_rpc_call_t *call;
    mr_uintptr_t addr;
    mr_uint32_t key;
    mr_err_t ret;

    /* Check call section */
    MR_ASSERT(start < end);

    /* Init call table */
    mr_hashmap_init(&__rpc_call_table, __rpc_call_table_buckets,
                    MR_ARRAY_SIZE(__rpc_call_table_buckets));

    /* Add calls */
    for (addr = start + sizeof(*call); addr < end; addr += sizeof(*call)) {
        call = (const mr_rpc_call_t *)addr;
        MR_ASSERT(call->decode != MR_NULL);
        MR_ASSERT(call->invoke != MR_NULL);
        MR_ASSERT(call->encode != MR_NULL);

        /* Add call */
        key = RPC_CALL_ID(call->service, call->method);
        mr_hashnode_init(call->hashnode, key, (void *)call);
        ret = mr_hashmap_add(&__rpc_call_table, call->hashnode);
        MR_ASSERT(ret == 0);
        MR_IGNORE(ret);
    }
}
MR_INIT_EXPORT(rpc_module_init, MR_INIT_LEVEL_MODULE);
#endif /* defined(MR_USE_MODULE_RPC) */
