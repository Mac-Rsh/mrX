/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_MODULE_RPC_H__
#define __MR_MODULE_RPC_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_MODULE_RPC)
#include <mrX/modules/rpc/mr_rpc_idl.h>
#include <mrX/kernel/mr_hashmap.h>
#include <mrX/kernel/mr_timer.h>
#include <mrX/kernel/mr_fifo.h>
#endif /* defined(MR_USE_MODULE_RPC) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup RPC
 * @{
 */

#if defined(MR_USE_MODULE_RPC)
struct mr_rpc_call;
struct mr_rpc;

/* RPC frame flags definition */
#define MR_RPC_FRAME_FLAG_CHECKSUM      (1U << 0)

/* RPC subscribe value definition */
#define MR_RPC_SUBSCRIBE_CONTINUOUS     (1U << 31)

/* RPC message type definition */
typedef enum mr_rpc_msg_type {
    MR_RPC_MSG_REQUEST,
    MR_RPC_MSG_RESPONSE,
    MR_RPC_MSG_ONEWAY,
    MR_RPC_MSG_SUBSCRIBE,
    MR_RPC_MSG_UNSUBSCRIBE,
    MR_RPC_MSG_STREAM,
} mr_rpc_msg_type_t;

/* RPC session state type */
typedef enum mr_rpc_session_state {
    MR_RPC_SESSION_STATE_FREE,
    MR_RPC_SESSION_STATE_RECEIVING,
    MR_RPC_SESSION_STATE_DISPATCHING,
    MR_RPC_SESSION_STATE_HELD,
    MR_RPC_SESSION_STATE_ENCODING,
    MR_RPC_SESSION_STATE_WAITING,
    MR_RPC_SESSION_STATE_READY,
    MR_RPC_SESSION_STATE_SENDING,
    MR_RPC_SESSION_STATE_DONE,
} mr_rpc_session_state_t;

/* RPC session action type */
typedef enum mr_rpc_session_action {
    MR_RPC_SESSION_ACTION_NONE,
    MR_RPC_SESSION_ACTION_RELEASE,
    MR_RPC_SESSION_ACTION_POLL,
    MR_RPC_SESSION_ACTION_UNSUBSCRIBE,
} mr_rpc_session_action_t;

/* RPC receive state type */
typedef enum mr_rpc_recv_state {
    MR_RPC_RECV_STATE_SYNC,
    MR_RPC_RECV_STATE_PAYLOAD,
    MR_RPC_RECV_STATE_COMPLETE,
    MR_RPC_RECV_STATE_PAYLOAD_CHECKSUM,
    MR_RPC_RECV_STATE_DISCARD,
    MR_RPC_RECV_STATE_DISCARD_CHECKSUM,
} mr_rpc_recv_state_t;

/* RPC frame value type */
typedef union mr_rpc_frame_value {
    mr_uint32_t subscribe;
    mr_int32_t result;
} mr_rpc_frame_value_t;

/* RPC frame type */
typedef struct mr_rpc_frame {
    mr_uint8_t header[2];
    mr_uint8_t version;
    mr_uint8_t type;
    mr_uint16_t flags;
    mr_uint16_t hdr_crc16;
    mr_uint32_t session;
    mr_uint16_t service;
    mr_uint16_t method;
    mr_uint32_t payload_size;
    mr_rpc_frame_value_t value;
} MR_PACKED mr_rpc_frame_t;

/* RPC buffer progress type */
typedef struct mr_rpc_buf {
    mr_uint32_t size;
    mr_uint32_t off;
} mr_rpc_buf_t;

/* RPC request type */
typedef struct mr_rpc_request {
    mr_rpc_msg_type_t type;
    mr_uint16_t flags;
    mr_uint32_t session;
    mr_uint16_t service;
    mr_uint16_t method;
    mr_rpc_frame_value_t value;
    mr_rpc_idl_request_t payload;
} mr_rpc_request_t;

/* RPC response type */
typedef struct mr_rpc_response {
    mr_rpc_frame_t frame;
    mr_rpc_idl_response_t payload;
    mr_uint16_t checksum;
} MR_PACKED mr_rpc_response_t;

/* RPC session type */
typedef struct mr_rpc_session {
    mr_rpc_session_state_t state;
    mr_rpc_session_action_t action;
    const struct mr_rpc_call *call;
    mr_rpc_response_t response;
    mr_rpc_request_t request;
    struct mr_rpc *rpc;
    mr_rpc_buf_t recv;
    mr_rpc_buf_t send;
    mr_tick_t deadline;
    mr_bool_t dispatching;
    mr_list_t list;
} mr_rpc_session_t;

/* RPC receive type */
typedef struct mr_rpc_recv {
#if !defined(MR_CFG_MODULE_RPC_FIFO_SIZE)
#define MR_CFG_MODULE_RPC_FIFO_SIZE     (256U)
#endif /* !defined(MR_CFG_MODULE_RPC_FIFO_SIZE) */
#if (MR_CFG_MODULE_RPC_FIFO_SIZE < 24U)
#error "RPC receive FIFO must hold a frame header (24 bytes)"
#endif /* (MR_CFG_MODULE_RPC_FIFO_SIZE < 24U) */
    mr_uint8_t buffer[MR_CFG_MODULE_RPC_FIFO_SIZE];
    mr_rpc_recv_state_t state;
    mr_rpc_session_t *session;
    mr_fifo_t fifo;
} mr_rpc_recv_t;

/* RPC send type */
typedef struct mr_rpc_send {
    mr_rpc_session_t *session;
    mr_list_t ready;
    mr_atomic_t running;
} mr_rpc_send_t;

/* RPC subscription type */
typedef struct mr_rpc_subs {
#if !defined(MR_CFG_MODULE_RPC_SESSION_NUM)
#define MR_CFG_MODULE_RPC_SESSION_NUM   (2U)
#endif /* !defined(MR_CFG_MODULE_RPC_SESSION_NUM) */
    mr_rpc_session_t *sessions[MR_CFG_MODULE_RPC_SESSION_NUM];
    mr_timer_t timer;
    mr_list_t tlist;
    mr_list_t list;
} mr_rpc_subs_t;

/* RPC operations type */
typedef struct mr_rpc_ops {
    mr_ssize_t (*send)(void *priv, const void *buf, mr_uint32_t size);
    mr_err_t (*flush)(void *priv);
} mr_rpc_ops_t;

#if defined(MR_USE_MODULE_RPC_HOOK)
/* RPC executor event type */
typedef enum mr_rpc_event {
    MR_RPC_EVENT_EXECUTE,
    MR_RPC_EVENT_POLL,
    MR_RPC_EVENT_MAX,
} mr_rpc_event_t;

/* RPC hook type */
typedef struct mr_rpc_hook {
    void (*suspend)(struct mr_rpc *rpc, mr_rpc_event_t event, void *param);
    void (*wakeup)(struct mr_rpc *rpc, mr_rpc_event_t event, void *param);
    void *param;
} mr_rpc_hook_t;
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */

/* RPC type */
MR_CLAZZES_EXPORT(rpc);
typedef struct mr_rpc {
    mr_object_t parent;
    mr_atomic_t dying;
#if defined(MR_USE_MODULE_RPC_HOOK)
    mr_atomic_t depth[MR_RPC_EVENT_MAX];
    const mr_rpc_hook_t *hook;
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */
    mr_rpc_session_t sessions[MR_CFG_MODULE_RPC_SESSION_NUM];
    mr_list_t session_free;
    mr_rpc_recv_t recv;
    mr_rpc_send_t send;
    mr_rpc_subs_t subs;
    const mr_rpc_ops_t *ops;
    void *priv;
} mr_rpc_t;

/* RPC method type */
typedef mr_err_t(mr_rpc_decode_t)(mr_rpc_session_t *session);
typedef mr_err_t(mr_rpc_invoke_t)(mr_rpc_session_t *session);
typedef mr_err_t(mr_rpc_encode_t)(mr_rpc_session_t *session);

/* RPC call type */
typedef struct mr_rpc_call {
    mr_hashnode_t *hashnode;
    mr_rpc_decode_t *decode;
    mr_rpc_invoke_t *invoke;
    mr_rpc_encode_t *encode;
    mr_uint16_t service;
    mr_uint16_t method;
} mr_rpc_call_t;

/**
 * @brief This macro function defines and exports an RPC call.
 *
 * @param _name The RPC call name.
 * @param _decode The RPC decode function.
 * @param _invoke The RPC invoke function.
 * @param _encode The RPC encode function.
 * @param _service_id The RPC service ID.
 * @param _method_id The RPC method ID.
 */
#define MR_RPC_EXPORT(_name, _decode, _invoke, _encode, _service_id,           \
                      _method_id)                                              \
    __MR_RPC_EXPORT(_name, _decode, _invoke, _encode, _service_id, _method_id, \
                    "1")
#define __MR_RPC_EXPORT(_name, _decode, _invoke, _encode, _service_id,         \
                        _method_id, _level)                                    \
    static mr_hashnode_t MR_CAT(__mr_rpc_call_hashnode_, _name);               \
    MR_USED const mr_rpc_call_t MR_CAT(__mr_rpc_call_, _name)                  \
        MR_SECTION_ALIGN("mr_rpc_call." _level)                                \
        = {                                                                    \
            .decode = (_decode),                                               \
            .invoke = (_invoke),                                               \
            .encode = (_encode),                                               \
            .hashnode = &MR_CAT(__mr_rpc_call_hashnode_, _name),               \
            .service = (_service_id),                                          \
            .method = (_method_id),                                            \
    }

/**
 * @brief This macro function defines an RPC entry.
 *
 * @param _service The RPC service name.
 * @param _method The RPC method name.
 */
#define MR_RPC(_service, _method)                                              \
    MR_CAT(__MR_RPC_ENTRY_, _service, _, _method)

/**
 * @brief This function initializes a static RPC object.
 *
 * @param rpc The RPC object.
 * @param ops The RPC operations.
 * @param priv The RPC private data.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_rpc_init(mr_rpc_t *rpc, const mr_rpc_ops_t *ops, void *priv);

/**
 * @brief This function creates an RPC object.
 *
 * @param ops The RPC operations.
 * @param priv The RPC private data.
 * @return The RPC object on success, MR_NULL on failure.
 */
mr_rpc_t *mr_rpc_create(const mr_rpc_ops_t *ops, void *priv);

/**
 * @brief This function deletes an RPC object.
 *
 * @param rpc The RPC object.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_rpc_del(mr_rpc_t *rpc);

/**
 * @brief This function inputs data to an RPC object.
 *
 * @param rpc The RPC object.
 * @param data The input data.
 * @param size The input data size.
 * @return The input size on success, -MR_EAGAIN if the FIFO is full, or
 * another negative error code on failure.
 */
mr_ssize_t mr_rpc_input(mr_rpc_t *rpc, const void *data, mr_uint32_t size);

/**
 * @brief This function parses received frames and dispatches calls.
 *
 * @param rpc The RPC object.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_rpc_execute(mr_rpc_t *rpc);

/**
 * @brief This function sends queued responses.
 *
 * @param rpc The RPC object.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_rpc_flush(mr_rpc_t *rpc);

/**
 * @brief This function encodes and queues a typed RPC response.
 *
 * @param session The RPC session.
 * @param result The RPC result.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_rpc_reply(mr_rpc_session_t *session, mr_err_t result);

/**
 * @brief This function polls active RPC subscriptions.
 *
 * @param rpc The RPC object.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_rpc_poll(mr_rpc_t *rpc);

/**
 * @brief This function holds an RPC session.
 *
 * @param session The RPC session.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_rpc_session_hold(mr_rpc_session_t *session);

/**
 * @brief This function drops a held RPC session.
 *
 * @param session The RPC session.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_rpc_session_drop(mr_rpc_session_t *session);

#if defined(MR_USE_MODULE_RPC_HOOK)
/**
 * @brief This function sets an RPC hook.
 *
 * @param rpc The RPC object.
 * @param hook The RPC hook.
 * @return 0 on success, a negative error code on failure.
 *
 * @note Recommend to use semaphores with an initial value of 0 as the hook.
 */
MR_INLINE mr_err_t mr_rpc_hook_set(mr_rpc_t *rpc, const mr_rpc_hook_t *hook) {
    /* Check parameter */
    MR_ASSERT((rpc != MR_NULL) && MR_OBJECT_IS_INITED(rpc));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(rpc, rpc));
    MR_ASSERT(hook != MR_NULL);

    /* Set RPC hook */
    rpc->hook = hook;
    return 0;
}
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */

/**
 * @brief This function gets an RPC object.
 *
 * @param rpc The RPC object.
 * @return The RPC object on success, MR_NULL on failure.
 */
MR_INLINE mr_rpc_t *mr_rpc_get(mr_rpc_t *rpc) {
    /* Get RPC reference */
    return (mr_rpc_t *)mr_object_get((mr_object_t *)rpc);
}

/**
 * @brief This function puts an RPC object.
 *
 * @param rpc The RPC object.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_rpc_put(mr_rpc_t *rpc) {
    /* Put RPC reference */
    return mr_object_put((mr_object_t *)rpc);
}
#endif /* defined(MR_USE_MODULE_RPC) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_MODULE_RPC_H__ */
