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

static void rpc_session_pool_init(mr_rpc_t *rpc) {
    mr_rpc_session_t *session;
    mr_uint32_t i;

    /* Init session pool */
    mr_list_init(&rpc->session_free);
    for (i = 0; i < MR_ARRAY_SIZE(rpc->sessions); i++) {
        session = &rpc->sessions[i];
        session->state = MR_RPC_SESSION_STATE_FREE;
        session->rpc = MR_NULL;
        rpc_session_reset(session);
        mr_list_init(&session->list);
        mr_list_add(&rpc->session_free, &session->list);
    }
}

static void rpc_recv_init(mr_rpc_t *rpc) {
    mr_uint32_t size;

    /* Init receive */
    size = sizeof(rpc->recv.buffer);
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
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */

static void rpc_subs_timer_entry(mr_timer_t *timer, void *param) {
    mr_tick_t tickless, tick, timer_tick;
    mr_rpc_session_t *session;
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
    }

    /* Check if subscription timer needs a new tick */
    timer_tick = mr_timer_tick(&rpc->subs.timer) & (~MR_TIMER_PERIODIC);
    if ((tickless != 0U) && (tickless != timer_tick)) {
        /* Set to optimal tick */
        mr_timer_tick_set(&rpc->subs.timer, tickless | MR_TIMER_PERIODIC);
        mr_timer_start(&rpc->subs.timer);

        /* Check if subscription is stopped(timing) for the last time */
    } else if (mr_list_is_empty(&rpc->subs.tlist)) {
        /* Don't need timer */
        mr_timer_stop(&rpc->subs.timer);
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

#if defined(MR_USE_MODULE_RPC_HOOK)
    /* Wakeup subscription poll */
    rpc_wakeup(rpc, MR_RPC_EVENT_POLL);
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
    rpc_session_pool_init(rpc);
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
        mr_timer_del(&rpc->subs.timer);
        return ret;
    }
    return 0;
}

mr_err_t mr_rpc_init(mr_rpc_t *rpc, const mr_rpc_ops_t *ops, void *priv) {
    const mr_clazz_t *clazz = MR_CLAZZES_STATIK(rpc);

    /* Check parameter */
    MR_ASSERT(rpc != MR_NULL);
    MR_ASSERT((ops != MR_NULL) && (ops->send != MR_NULL));

    /* Init RPC */
    return rpc_init(rpc, clazz, ops, priv);
}

mr_rpc_t *mr_rpc_create(const mr_rpc_ops_t *ops, void *priv) {
    const mr_clazz_t *clazz = MR_CLAZZES_DYNAMIC(rpc);
    mr_rpc_t *rpc;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((ops != MR_NULL) && (ops->send != MR_NULL));

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

mr_err_t mr_rpc_del(mr_rpc_t *rpc) {
    /* Check parameter */
    MR_ASSERT((rpc != MR_NULL) && MR_OBJECT_IS_INITED(rpc));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(rpc, rpc));

    /* Mark RPC as dying */
    mr_atomic_store(&rpc->dying, MR_TRUE);

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

    /* Check if RPC is dying */
    if (mr_atomic_load(&rpc->dying)) {
        return -MR_ENOENT;
    }

    /* Ignore empty input */
    if (size == 0U) {
        return 0;
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

static mr_uint16_t rpc_fletcher16(const void *data, mr_uint32_t size) {
    mr_uint32_t sum1, sum2, n, x;
    const mr_uint8_t *bytes;

    /* Fletcher-16, delayed modulo 255, empty data is 0 */
    bytes = data;
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
        x = (sum1 & 0xffU) + (sum1 >> 8);
        x = (x & 0xffU) + (x >> 8);
        if (x >= 255U) {
            x -= 255U;
        }
        sum1 = x;
        x = (sum2 & 0xffU) + (sum2 >> 8);
        x = (x & 0xffU) + (x >> 8);
        if (x >= 255U) {
            x -= 255U;
        }
        sum2 = x;
    }
    return (mr_uint16_t)((sum2 << 8) | sum1);
}

static void rpc_session_encode_frame(mr_rpc_session_t *session,
                                     mr_rpc_msg_type_t type, mr_err_t result,
                                     mr_uint32_t payload_size) {
    mr_rpc_frame_t *frame;
    mr_uint8_t *checksum;
    mr_uint16_t csum;

    /* Build response header in front of the encoded payload */
    frame = &session->response.frame;
    frame->header[0] = RPC_SYNC_HEADER_MARKER_0;
    frame->header[1] = RPC_SYNC_HEADER_MARKER_1;
    frame->version = RPC_FRAME_VERSION;
    frame->type = (mr_uint8_t)type;
    frame->flags = session->request.flags;
    frame->session = session->request.session;
    frame->service = session->request.service;
    frame->method = session->request.method;
    frame->payload_size = payload_size;
    frame->value.result = result;
    frame->hdr_crc16 = 0;
    frame->hdr_crc16 = rpc_fletcher16(frame, sizeof(*frame));

    /* Send the packed frame and payload as one buffer */
    session->send.size = sizeof(*frame) + payload_size;
    if (session->request.flags & MR_RPC_FRAME_FLAG_CHECKSUM) {
        checksum = (mr_uint8_t *)&session->response.payload + payload_size;
        csum = rpc_fletcher16(&session->response.payload, payload_size);
        mr_memcpy(checksum, &csum, sizeof(csum));
        session->send.size += RPC_CHECKSUM_SIZE;
    }
    session->send.off = 0U;
}

static mr_rpc_session_t *rpc_session_acquire(mr_rpc_t *rpc) {
    mr_rpc_session_t *session;
    mr_list_t *l;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Acquire free session */
    if (mr_list_is_empty(&rpc->session_free)) {
        mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);
        return MR_NULL;
    }
    l = rpc->session_free.next;
    session = MR_LIST_ENTRY(l, mr_rpc_session_t, list);
    session->state = MR_RPC_SESSION_STATE_RECEIVING;
    session->rpc = rpc;
    rpc_session_reset(session);
    mr_list_del(&session->list);

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
        if ((frame.header[0] == RPC_SYNC_HEADER_MARKER_0)
            && (frame.header[1] == RPC_SYNC_HEADER_MARKER_1)) {
            /* Check version and type */
            if ((frame.version != RPC_FRAME_VERSION)
                || ((frame.type != MR_RPC_MSG_REQUEST)
                    && (frame.type != MR_RPC_MSG_ONEWAY)
                    && (frame.type != MR_RPC_MSG_SUBSCRIBE)
                    && (frame.type != MR_RPC_MSG_UNSUBSCRIBE))) {
                /* Skip unsupported header */
                mr_fifo_skip(&rpc->recv.fifo, 2U);
                continue;
            }

            /* Check header checksum */
            checksum = frame.hdr_crc16;
            frame.hdr_crc16 = 0;
            if (rpc_fletcher16(&frame, sizeof(frame)) != checksum) {
                /* Skip invalid header */
                mr_fifo_skip(&rpc->recv.fifo, 2U);
                continue;
            }
        } else {
            /* Skip invalid byte */
            mr_fifo_skip(&rpc->recv.fifo, 1U);
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

        /* Store used header fields */
        session->recv.size = frame.payload_size;
        session->request.type = frame.type;
        session->request.flags = frame.flags;
        session->request.session = frame.session;
        session->request.service = frame.service;
        session->request.method = frame.method;
        session->request.value = frame.value;

        /* Discard oversized payloads */
        if (session->recv.size > sizeof(session->request.payload)) {
            rpc->recv.state = MR_RPC_RECV_STATE_DISCARD;
            break;
        }

        /* Receive payload */
        rpc->recv.state = MR_RPC_RECV_STATE_PAYLOAD;
        break;
    }
}

static void rpc_frame_payload_parse(mr_rpc_t *rpc) {
    mr_rpc_session_t *session;
    mr_uint32_t remain, ret;
    mr_uint8_t *payload;

    /* Read payload */
    session = rpc->recv.session;
    payload = (mr_uint8_t *)&session->request.payload + session->recv.off;
    remain = (mr_uint32_t)(session->recv.size - session->recv.off);
    ret = mr_fifo_read(&rpc->recv.fifo, payload, remain);
    session->recv.off += ret;
    if (ret < remain) {
        /* Wait for more payload */
        return;
    }

    /* Receive checksum trailer */
    if (session->request.flags & MR_RPC_FRAME_FLAG_CHECKSUM) {
        rpc->recv.state = MR_RPC_RECV_STATE_PAYLOAD_CHECKSUM;
        return;
    }

    /* Complete payload */
    rpc->recv.state = MR_RPC_RECV_STATE_COMPLETE;
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

    /* Oversized payloads were discarded and must not be checksum */
    session = rpc->recv.session;
    if (rpc->recv.state != MR_RPC_RECV_STATE_DISCARD_CHECKSUM) {
        expect = rpc_fletcher16(&session->request.payload, session->recv.size);
        *error = (checksum != expect) ? -MR_EINVAL : 0;
    } else {
        *error = -MR_ENOMEM;
    }

    /* Consume checksum trailer */
    mr_fifo_skip(&rpc->recv.fifo, sizeof(checksum));
    return MR_TRUE;
}

static mr_bool_t rpc_frame_payload_discard(mr_rpc_t *rpc) {
    mr_rpc_session_t *session;
    mr_uint32_t remain, ret;

    /* Discard payload */
    session = rpc->recv.session;
    remain = (mr_uint32_t)(session->recv.size - session->recv.off);
    ret = mr_fifo_skip(&rpc->recv.fifo, remain);
    session->recv.off += ret;
    if (ret < remain) {
        /* Wait for more payload */
        return MR_FALSE;
    }

    /* Discard checksum separately */
    if (session->request.flags & MR_RPC_FRAME_FLAG_CHECKSUM) {
        rpc->recv.state = MR_RPC_RECV_STATE_DISCARD_CHECKSUM;
        return MR_FALSE;
    }

    /* Complete discard */
    return MR_TRUE;
}

static mr_rpc_session_t *rpc_frame_complete(mr_rpc_t *rpc) {
    mr_rpc_session_t *session;

    /* Detach completed session */
    session = rpc->recv.session;
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
                rpc_frame_payload_parse(rpc);
                break;
            }
            case MR_RPC_RECV_STATE_DISCARD_CHECKSUM:
            case MR_RPC_RECV_STATE_PAYLOAD_CHECKSUM: {
                /* Report invalid data */
                if (rpc_frame_checksum_parse(rpc, error)) {
                    return rpc_frame_complete(rpc);
                }
                break;
            }
            case MR_RPC_RECV_STATE_COMPLETE: {
                /* Complete frame */
                *error = 0;
                return rpc_frame_complete(rpc);
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

    /* No complete frame */
    return MR_NULL;
}

static const mr_rpc_call_t *rpc_call_find(mr_uint32_t call_id) {
    mr_hashnode_t *node;

    /* Find RPC call by ID */
    node = mr_hashmap_find(&__rpc_call_table, call_id);
    if (!node) {
        /* Not found */
        return MR_NULL;
    }

    /* Found */
    return (const mr_rpc_call_t *)mr_hashnode_value(node);
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

static mr_err_t rpc_subs_add(mr_rpc_t *rpc, mr_rpc_session_t *session) {
    mr_rpc_session_t **free_slot;
    mr_rpc_session_t *current;
    mr_uint32_t i;
    mr_err_t ret;
    int mask;

    /* Reject an invalid subscription period */
    if (rpc_subs_period(session) == 0U) {
        return -MR_EINVAL;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Find a free slot and reject a duplicate session ID */
    free_slot = MR_NULL;
    for (i = 0; i < MR_ARRAY_SIZE(rpc->subs.sessions); i++) {
        current = rpc->subs.sessions[i];
        if (!current) {
            if (!free_slot) {
                free_slot = &rpc->subs.sessions[i];
            }
            continue;
        }
        if (current->request.session == session->request.session) {
            free_slot = MR_NULL;
            break;
        }
    }

    /* Add subscription */
    if (session->action == MR_RPC_SESSION_ACTION_UNSUBSCRIBE) {
        ret = -MR_ENOENT;
    } else if (i < MR_ARRAY_SIZE(rpc->subs.sessions)) {
        ret = -MR_EEXIST;
    } else if ((!free_slot) || mr_list_is_empty(&rpc->session_free)) {
        /* Keep one session available for non-subscription traffic */
        ret = -MR_ENOMEM;
    } else {
        *free_slot = session;
        ret = 0;
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);
    return ret;
}

static void rpc_session_recycle_locked(mr_rpc_session_t *session) {
    mr_uint32_t i;
    mr_rpc_t *rpc;

    /* Remove the session from subscription and queue ownership */
    rpc = session->rpc;
    for (i = 0; i < MR_ARRAY_SIZE(rpc->subs.sessions); i++) {
        if (rpc->subs.sessions[i] == session) {
            rpc->subs.sessions[i] = MR_NULL;
            break;
        }
    }
    mr_list_del(&session->list);

    /* Return the session to the pool */
    session->rpc = MR_NULL;
    session->state = MR_RPC_SESSION_STATE_FREE;
    mr_list_add(&rpc->session_free, &session->list);
}

static mr_err_t rpc_subs_cancel(mr_rpc_t *rpc, mr_rpc_session_t *request) {
    mr_rpc_session_t *session;
    mr_uint32_t i;
    mr_err_t ret;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Remove the subscription identified by the cancellation request */
    ret = -MR_ENOENT;
    for (i = 0; i < MR_ARRAY_SIZE(rpc->subs.sessions); i++) {
        session = rpc->subs.sessions[i];
        if (!session) {
            continue;
        }

        /* Match the cancellation to this subscription ID */
        if (session->request.session != request->request.session) {
            continue;
        }

        /* Detach first so later recycle cannot find this slot again */
        rpc->subs.sessions[i] = MR_NULL;
        if (((session->state == MR_RPC_SESSION_STATE_READY)
             || (session->state == MR_RPC_SESSION_STATE_WAITING))
            && (!session->dispatching)) {
            /* Recycle idle subscriptions without sending another reply */
            rpc_session_recycle_locked(session);
        } else {
            /* Let the current callback, encoder, sender, or holder finish */
            session->action = MR_RPC_SESSION_ACTION_UNSUBSCRIBE;
        }
        ret = 0;
        break;
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);
    return ret;
}

static void rpc_subs_schedule(mr_rpc_t *rpc, mr_rpc_session_t *session) {
    mr_rpc_session_t *current;
    mr_tick_t period;
    mr_list_t *l;

    /* Set timeout tick */
    period = rpc_subs_period(session);
    session->deadline = mr_clock_tick() + period;

    /* Insert subscription into timer list */
    MR_LIST_FOR_EACH(l, &rpc->subs.tlist) {
        current = MR_LIST_ENTRY(l, mr_rpc_session_t, list);
        if ((session->deadline - current->deadline) < (MR_TICK_MAX / 2U)) {
            continue;
        }
        mr_list_move(&session->list, &current->list);
        goto _set;
    }
    mr_list_move(&session->list, &rpc->subs.tlist);
_set:
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

static mr_bool_t rpc_session_complete_locked(mr_rpc_session_t *session) {
    /* The dispatch callback may still be using the response storage */
    if (session->dispatching) {
        return MR_FALSE;
    }

    /* Publish only completed replies or completed ownership transfers */
    if ((session->state != MR_RPC_SESSION_STATE_READY)
        && (session->state != MR_RPC_SESSION_STATE_WAITING)
        && (session->state != MR_RPC_SESSION_STATE_DONE)) {
        return MR_FALSE;
    }
    /* Closing is checked once when completed work is published */
    if (mr_atomic_load(&session->rpc->dying)
        || (session->action == MR_RPC_SESSION_ACTION_UNSUBSCRIBE)
        || (session->state == MR_RPC_SESSION_STATE_DONE)) {
        rpc_session_recycle_locked(session);
        return MR_TRUE;
    }
    if (session->state == MR_RPC_SESSION_STATE_WAITING) {
        rpc_subs_schedule(session->rpc, session);
    } else {
        mr_list_add(&session->rpc->send.ready, &session->list);
    }
    return MR_FALSE;
}

static void rpc_session_wakeup(mr_rpc_t *rpc, mr_bool_t released) {
#if defined(MR_USE_MODULE_RPC_HOOK)
    /* Resume input that was waiting for a free session */
    if (released && (!mr_fifo_is_empty(&rpc->recv.fifo))) {
        rpc_wakeup(rpc, MR_RPC_EVENT_EXECUTE);
    }
#else
    MR_IGNORE(rpc);
    MR_IGNORE(released);
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */
}

static mr_err_t rpc_reply(mr_rpc_session_t *session, mr_err_t result,
                          mr_uint32_t payload_size) {
    mr_rpc_session_action_t action;
    mr_rpc_session_state_t state;
    mr_rpc_msg_type_t type;
    mr_bool_t released;
    mr_rpc_t *rpc;
    int mask;

    /* Errors never carry a response payload */
    if (payload_size > sizeof(session->response.payload)) {
        result = -MR_ENOMEM;
    }
    if (result < 0) {
        payload_size = 0U;
    }

    /* Select the post-send action */
    action = MR_RPC_SESSION_ACTION_RELEASE;
    type = MR_RPC_MSG_RESPONSE;
    if (((session->request.type == MR_RPC_MSG_SUBSCRIBE)
         || (session->request.type == MR_RPC_MSG_STREAM))
        && (result >= 0)) {
        action = MR_RPC_SESSION_ACTION_POLL;
    }
    if ((session->request.type == MR_RPC_MSG_STREAM) && (result >= 0)) {
        type = MR_RPC_MSG_STREAM;
    }

    /* ENCODING owns the buffer; keep checksum work outside the queue lock */
    if (session->request.type == MR_RPC_MSG_ONEWAY) {
        state = MR_RPC_SESSION_STATE_DONE;
    } else if ((type == MR_RPC_MSG_STREAM) && (result == 0)
               && (payload_size == 0U)) {
        state = MR_RPC_SESSION_STATE_WAITING;
    } else {
        rpc_session_encode_frame(session, type, result, payload_size);
        state = MR_RPC_SESSION_STATE_READY;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Preserve cancellation; completion handles RPC shutdown */
    rpc = session->rpc;
    if (session->action == MR_RPC_SESSION_ACTION_UNSUBSCRIBE) {
        state = MR_RPC_SESSION_STATE_DONE;
    }
    session->state = state;
    session->action = action;
    released = rpc_session_complete_locked(session);

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

    /* Wakeup session */
    rpc_session_wakeup(rpc, released);
    return 0;
}

static mr_err_t rpc_session_encode(mr_rpc_session_t *session, mr_err_t result,
                                   mr_bool_t automatic) {
    mr_uint32_t payload_size;
    mr_bool_t cancelled;
    mr_err_t ret;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Claim the response before an encoder can modify its storage */
    if (session->state != MR_RPC_SESSION_STATE_DISPATCHING) {
        if (automatic) {
            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);
            return 0;
        }
        if (session->state != MR_RPC_SESSION_STATE_HELD) {
            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);
            return -MR_EPERM;
        }
    }
    cancelled = (session->action == MR_RPC_SESSION_ACTION_UNSUBSCRIBE)
             || mr_atomic_load(&session->rpc->dying);
    session->state = MR_RPC_SESSION_STATE_ENCODING;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

    /* Subscription control and method output share one completion path */
    payload_size = 0;
    if ((!cancelled) && (result >= 0)) {
        switch (session->request.type) {
            case MR_RPC_MSG_SUBSCRIBE: {
                result = rpc_subs_add(session->rpc, session);
                break;
            }
            case MR_RPC_MSG_UNSUBSCRIBE: {
                result = (session->recv.size == 0U)
                           ? rpc_subs_cancel(session->rpc, session)
                           : -MR_EINVAL;
                break;
            }
            case MR_RPC_MSG_REQUEST:
            case MR_RPC_MSG_STREAM: {
                if ((!session->call) || (!session->call->encode)) {
                    result = -MR_ENOSYS;
                    break;
                }
                ret = session->call->encode(session);
                if (ret >= 0) {
                    payload_size = (mr_uint32_t)ret;
                } else {
                    payload_size = 0;
                    result = ret;
                }
                break;
            }
            case MR_RPC_MSG_ONEWAY: {
                break;
            }
            default: {
                result = -MR_EINVAL;
                break;
            }
        }
    }
    return rpc_reply(session, result, payload_size);
}

static void rpc_session_dispatch(mr_rpc_session_t *session, mr_err_t result) {
    mr_bool_t invoke, released;
    mr_uint32_t id;
    mr_rpc_t *rpc;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Keep callback storage private until dispatch returns */
    rpc = session->rpc;
    session->state = MR_RPC_SESSION_STATE_DISPATCHING;
    session->dispatching = MR_TRUE;
    if (mr_atomic_load(&rpc->dying)) {
        result = -MR_ENOENT;
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

    /* Resolve and decode call requests once; stream polls reuse the call */
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

    /* An admitted dispatch may finish; control requests never invoke methods */
    invoke = (result >= 0)
          && (session->state == MR_RPC_SESSION_STATE_DISPATCHING)
          && (session->action != MR_RPC_SESSION_ACTION_UNSUBSCRIBE)
          && ((session->request.type == MR_RPC_MSG_REQUEST)
              || (session->request.type == MR_RPC_MSG_ONEWAY)
              || (session->request.type == MR_RPC_MSG_STREAM));

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

    /* Invoke unlocked, then encode */
    if (invoke) {
        result = session->call->invoke(session);
    }
    rpc_session_encode(session, result, MR_TRUE);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Transfer queued output or recycle only after the callback has returned */
    session->dispatching = MR_FALSE;
    released = rpc_session_complete_locked(session);

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

    /* Wakeup session */
    rpc_session_wakeup(rpc, released);
}

static mr_err_t rpc_send_execute(mr_rpc_t *rpc) {
    mr_rpc_session_t *session;
    mr_bool_t released, poll;
    mr_uint32_t remain;
    mr_uint8_t *buf;
    mr_ssize_t sent;
    mr_list_t *l;
    mr_err_t ret;
    int mask;

    /* Only the sender owns the send cursor */
    if (mr_atomic_exchange(&rpc->send.running, MR_TRUE)) {
        return 0;
    }

    while (1) {
        /* Lock */
        mask = mr_spinlock_lock_irqsave(&__rpc_lock);

        /* Lock shared queues and subscription transitions */
        session = rpc->send.session;
        if (mr_atomic_load(&rpc->dying)) {
            if (session) {
                rpc_session_recycle_locked(session);
                rpc->send.session = MR_NULL;
            }

            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);
            ret = -MR_ENOENT;
            break;
        }

        /* Complete a sent frame before selecting the next one */
        released = MR_FALSE;
        poll = MR_FALSE;
        if (session && (session->send.off == session->send.size)) {
            rpc->send.session = MR_NULL;

            /* Poll keeps the session; anything else is finished */
            if (session->action == MR_RPC_SESSION_ACTION_POLL) {
                session->request.type = MR_RPC_MSG_STREAM;
                session->state = MR_RPC_SESSION_STATE_WAITING;
            } else {
                session->state = MR_RPC_SESSION_STATE_DONE;
            }

            /* Continuous: poll again now; otherwise schedule or recycle */
            if ((session->state == MR_RPC_SESSION_STATE_WAITING)
                && (session->request.value.subscribe
                    & MR_RPC_SUBSCRIBE_CONTINUOUS)) {
                mr_list_add(&rpc->subs.list, &session->list);
                poll = MR_TRUE;
            } else {
                released = rpc_session_complete_locked(session);
            }
            session = MR_NULL;
        }
        if ((!session) && (!mr_list_is_empty(&rpc->send.ready))) {
            l = rpc->send.ready.next;
            session = MR_LIST_ENTRY(l, mr_rpc_session_t, list);
            mr_list_del(&session->list);
            session->state = MR_RPC_SESSION_STATE_SENDING;
            rpc->send.session = session;
        }

        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

        /* Wakeup session */
        rpc_session_wakeup(rpc, released);

#if defined(MR_USE_MODULE_RPC_HOOK)
        /* Run user callbacks in the subscription executor */
        if (poll) {
            rpc_wakeup(rpc, MR_RPC_EVENT_POLL);
        }
#else
        MR_IGNORE(poll);
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */

        if (!session) {
            /* Keep sender ownership while the transport flushes */
            ret = rpc->ops->flush ? rpc->ops->flush(rpc->priv) : 0;

            /* Pair queue emptiness with sender handoff to avoid lost output */
            mask = mr_spinlock_lock_irqsave(&__rpc_lock);
            if ((ret < 0) || mr_list_is_empty(&rpc->send.ready)) {
                mr_atomic_store(&rpc->send.running, MR_FALSE);
                mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);
                return ret;
            }
            mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);
            continue;
        }

        /* Preserve partial frames until the transport accepts the remainder */
        buf = (mr_uint8_t *)&session->response + session->send.off;
        remain = session->send.size - session->send.off;
        sent = rpc->ops->send(rpc->priv, buf, remain);
        if (sent <= 0) {
            ret = (sent < 0) ? (mr_err_t)sent : -MR_EAGAIN;
            break;
        }
        if ((mr_uint64_t)sent > remain) {
            ret = -MR_EIO;
            break;
        }
        session->send.off += (mr_uint32_t)sent;
    }

    /* No shared queue operation is needed to relinquish the sender */
    mr_atomic_store(&rpc->send.running, MR_FALSE);
    return ret;
}

#if defined(MR_USE_MODULE_RPC_HOOK)
static void rpc_suspend(mr_rpc_t *rpc, mr_rpc_event_t event) {
    mr_atomic_t last;

    /* Decrease event depth */
    last = mr_atomic_fetch_sub(&rpc->depth[event], 1);
    if (last != 1) {
        /* No need to suspend */
        return;
    }

    /* Suspend RPC executor */
    if (rpc->hook && rpc->hook->suspend) {
        rpc->hook->suspend(rpc, event, rpc->hook->param);
    }
}
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */

mr_err_t mr_rpc_execute(mr_rpc_t *rpc) {
    mr_rpc_session_t *session;
    mr_err_t error, ret;

    /* Borrow the caller's RPC reference */
    MR_ASSERT((rpc != MR_NULL) && MR_OBJECT_IS_INITED(rpc));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(rpc, rpc));
    if (mr_atomic_load(&rpc->dying)) {
        return -MR_ENOENT;
    }

#if defined(MR_USE_MODULE_RPC_HOOK)
    /* Mark execute event as running */
    mr_atomic_store(&rpc->depth[MR_RPC_EVENT_EXECUTE], 1);
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */

    /* Free completed output before acquiring receive sessions */
    ret = rpc_send_execute(rpc);
    while ((ret >= 0) || (ret == (-MR_EAGAIN))) {
        session = rpc_frame_parse(rpc, &error);
        if (!session) {
            break;
        }
        rpc_session_dispatch(session, error);
        ret = rpc_send_execute(rpc);
    }

#if defined(MR_USE_MODULE_RPC_HOOK)
    /* Suspend execute event */
    rpc_suspend(rpc, MR_RPC_EVENT_EXECUTE);
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */
    return ret;
}

mr_err_t mr_rpc_flush(mr_rpc_t *rpc) {
    /* Borrow the caller's RPC reference */
    MR_ASSERT((rpc != MR_NULL) && MR_OBJECT_IS_INITED(rpc));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(rpc, rpc));

    /* Send remaining responses */
    return rpc_send_execute(rpc);
}

mr_err_t mr_rpc_reply(mr_rpc_session_t *session, mr_err_t result) {
    /* Borrow the caller's RPC reference and consume the reply opportunity */
    MR_ASSERT(session != MR_NULL);
    MR_ASSERT(session->rpc != MR_NULL);
    if ((session->request.type != MR_RPC_MSG_REQUEST)
        && (session->request.type != MR_RPC_MSG_STREAM)) {
        return -MR_EPERM;
    }

    /* Encode typed output before framing and queueing it */
    return rpc_session_encode(session, result, MR_FALSE);
}

mr_err_t mr_rpc_poll(mr_rpc_t *rpc) {
    mr_rpc_session_t *session;
    mr_list_t oplist;
    mr_err_t ret;
    int mask;

    /* Borrow the caller's RPC reference */
    MR_ASSERT((rpc != MR_NULL) && MR_OBJECT_IS_INITED(rpc));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(rpc, rpc));
    if (mr_atomic_load(&rpc->dying)) {
        return -MR_ENOENT;
    }

    mr_list_init(&oplist);
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);
#if defined(MR_USE_MODULE_RPC_HOOK)
    /* Mark poll event as running */
    mr_atomic_store(&rpc->depth[MR_RPC_EVENT_POLL], 1);
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */

    /* Cancellation may remove waiting subscriptions from this batch */
    mr_list_replace(&oplist, &rpc->subs.list);
    while (!mr_list_is_empty(&oplist)) {
        session = MR_LIST_ENTRY(oplist.next, mr_rpc_session_t, list);
        mr_list_del(&session->list);
        session->state = MR_RPC_SESSION_STATE_DISPATCHING;
        mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

        rpc_session_dispatch(session, 0);

        mask = mr_spinlock_lock_irqsave(&__rpc_lock);
    }
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

    /* Send stream frames */
    ret = rpc_send_execute(rpc);
#if defined(MR_USE_MODULE_RPC_HOOK)
    /* Suspend poll event */
    rpc_suspend(rpc, MR_RPC_EVENT_POLL);
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */
    return ret;
}

mr_err_t mr_rpc_session_hold(mr_rpc_session_t *session) {
    mr_err_t ret;
    int mask;

    /* Holding reserves a session without acquiring an RPC reference */
    MR_ASSERT(session != MR_NULL);
    MR_ASSERT(session->rpc != MR_NULL);

    mask = mr_spinlock_lock_irqsave(&__rpc_lock);
    if ((session->state == MR_RPC_SESSION_STATE_DISPATCHING)
        && (session->action != MR_RPC_SESSION_ACTION_UNSUBSCRIBE)) {
        session->state = MR_RPC_SESSION_STATE_HELD;
        ret = 0;
    } else {
        ret = -MR_EPERM;
    }
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);
    return ret;
}

mr_err_t mr_rpc_session_drop(mr_rpc_session_t *session) {
    mr_bool_t released;
    mr_rpc_t *rpc;
    int mask;

    /* Dropping returns a session without releasing the caller's RPC reference
     */
    MR_ASSERT(session != MR_NULL);
    MR_ASSERT(session->rpc != MR_NULL);

    mask = mr_spinlock_lock_irqsave(&__rpc_lock);
    if (session->state != MR_RPC_SESSION_STATE_HELD) {
        mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);
        return -MR_EPERM;
    }
    rpc = session->rpc;
    session->state = MR_RPC_SESSION_STATE_DONE;
    released = rpc_session_complete_locked(session);
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);
    rpc_session_wakeup(rpc, released);
    return 0;
}

static void rpc_del(mr_rpc_t *rpc) {
    mr_rpc_session_t *session;
    mr_uint32_t i;
    int mask;

    /* Mark RPC as dying */
    mr_atomic_store(&rpc->dying, MR_TRUE);

    /* Delete RPC timer */
    mr_timer_del(&rpc->subs.timer);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__rpc_lock);

    /* Remove sessions from subscription timing list */
    while (!mr_list_is_empty(&rpc->subs.tlist)) {
        session = MR_LIST_ENTRY(rpc->subs.tlist.next, mr_rpc_session_t, list);
        mr_list_del(&session->list);
    }

    /* Remove sessions from subscription ready list */
    while (!mr_list_is_empty(&rpc->subs.list)) {
        session = MR_LIST_ENTRY(rpc->subs.list.next, mr_rpc_session_t, list);
        mr_list_del(&session->list);
    }

    /* Remove sessions from send ready list */
    while (!mr_list_is_empty(&rpc->send.ready)) {
        session = MR_LIST_ENTRY(rpc->send.ready.next, mr_rpc_session_t, list);
        mr_list_del(&session->list);
    }

    /* Drop in-flight and subscription session pointers */
    rpc->recv.session = MR_NULL;
    rpc->send.session = MR_NULL;
    for (i = 0; i < MR_ARRAY_SIZE(rpc->subs.sessions); i++) {
        rpc->subs.sessions[i] = MR_NULL;
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__rpc_lock, mask);

#if defined(MR_USE_MODULE_RPC_HOOK)
    rpc->hook = MR_NULL;
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */
    rpc->ops = MR_NULL;
    rpc->priv = MR_NULL;
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
    mr_uintptr_t start = (mr_uintptr_t)&__mr_rpc_call_start;
    mr_uintptr_t end = (mr_uintptr_t)&__mr_rpc_call_end;
    const mr_rpc_call_t *call;
    mr_uintptr_t addr;
    mr_uint32_t key;
    mr_err_t ret;

    /* Check call section */
    MR_ASSERT(start < end);

    /* Init RPC call hashmap */
    mr_hashmap_init(&__rpc_call_table, __rpc_call_table_buckets,
                    MR_ARRAY_SIZE(__rpc_call_table_buckets));

    /* Add calls to table */
    for (addr = start + sizeof(*call); addr < end; addr += sizeof(*call)) {
        call = (const mr_rpc_call_t *)addr;
        MR_ASSERT(call->decode != MR_NULL);
        MR_ASSERT(call->invoke != MR_NULL);
        MR_ASSERT(call->encode != MR_NULL);

        /* Generate key and add to table */
        key = RPC_CALL_ID(call->service, call->method);
        mr_hashnode_init(call->hashnode, key, (void *)call);
        ret = mr_hashmap_add(&__rpc_call_table, call->hashnode);
        MR_ASSERT(ret == 0);
        MR_IGNORE(ret);
    }
}
MR_INIT_EXPORT(rpc_module_init, MR_INIT_LEVEL_MODULE);
#endif /* defined(MR_USE_MODULE_RPC) */
