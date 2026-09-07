/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_MODULE_XVM_GDB_H__
#define __MR_MODULE_XVM_GDB_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_MODULE_XVM_GDB)
#include <mrX/modules/xvm/mr_xvm.h>
#endif /* defined(MR_USE_MODULE_XVM_GDB) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup XVM-GDB
 * @{
 */

#if defined(MR_USE_MODULE_XVM_GDB)
/* XVM GDB session state definition */
typedef enum mr_xvm_gdb_state {
    MR_XVM_GDB_DETACHED,
    MR_XVM_GDB_ATTACHED,
    MR_XVM_GDB_RUNNING,
    MR_XVM_GDB_CLOSED,
} mr_xvm_gdb_state_t;

/* XVM GDB parse state definition */
typedef enum mr_xvm_gdb_parse_state {
    MR_XVM_GDB_PARSE_IDLE,
    MR_XVM_GDB_PARSE_DATA,
    MR_XVM_GDB_PARSE_CKSUM,
} mr_xvm_gdb_parse_state_t;

/* XVM GDB parse type */
typedef struct mr_xvm_gdb_parse {
#if !defined(MR_CFG_XVM_GDB_PACKET_SIZE)
#define MR_CFG_XVM_GDB_PACKET_SIZE      (512)
#endif /* !defined(MR_CFG_XVM_GDB_PACKET_SIZE) */
    char buf[MR_CFG_XVM_GDB_PACKET_SIZE];
    mr_xvm_gdb_parse_state_t state;
    mr_uint8_t csum_n;
    mr_size_t len;
    char csum[2];
} mr_xvm_gdb_parse_t;

/* XVM GDB reply type */
typedef struct mr_xvm_gdb_reply {
#if !defined(MR_CFG_XVM_GDB_REPLY_SIZE)
#define MR_CFG_XVM_GDB_REPLY_SIZE       (512)
#endif /* !defined(MR_CFG_XVM_GDB_REPLY_SIZE) */
    char buf[MR_CFG_XVM_GDB_REPLY_SIZE];
    mr_size_t len;
    mr_size_t off;
} mr_xvm_gdb_reply_t;

/* XVM GDB operations type */
typedef struct mr_xvm_gdb_ops {
    mr_ssize_t (*send)(void *priv, const void *buf, mr_size_t len);
    /* Optional */
    mr_err_t (*flush)(void *priv);
    void (*closed)(void *priv);
} mr_xvm_gdb_ops_t;

/* XVM GDB session type */
MR_CLAZZES_EXPORT(xvm_gdb);
typedef struct mr_xvm_gdb {
    mr_object_t parent;
    mr_xvm_gdb_state_t state;
    mr_xvm_gdb_parse_t parse;
    mr_xvm_gdb_reply_t reply;
    mr_bool_t no_ack;
    mr_xvm_t *xvm;
    mr_uint32_t pc_nr;
    const mr_xvm_gdb_ops_t *ops;
    void *priv;
} mr_xvm_gdb_t;

/**
 * @brief This function initializes a static XVM GDB session.
 *
 * @param gdb The GDB session.
 * @param ops The session operations.
 * @param priv The session private data.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xvm_gdb_init(mr_xvm_gdb_t *gdb, const mr_xvm_gdb_ops_t *ops,
                         void *priv);

/**
 * @brief This function creates an XVM GDB session.
 *
 * @param ops The session operations.
 * @param priv The session private data.
 * @return The GDB session on success, MR_NULL on failure.
 */
mr_xvm_gdb_t *mr_xvm_gdb_create(const mr_xvm_gdb_ops_t *ops, void *priv);

/**
 * @brief This function deletes an XVM GDB session.
 *
 * @param gdb The GDB session.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xvm_gdb_del(mr_xvm_gdb_t *gdb);

/**
 * @brief This function attaches a GDB session to an XVM.
 *
 * @param gdb The GDB session.
 * @param xvm The XVM.
 * @return 0 on success, a negative error code on failure.
 *
 * @note Inserts the GDB probe and holds the step gate
 */
mr_err_t mr_xvm_gdb_attach(mr_xvm_gdb_t *gdb, mr_xvm_t *xvm);

/**
 * @brief This function detaches a GDB session from its XVM.
 *
 * @param gdb The GDB session.
 *
 * @note Does not destroy the XVM. The session object stays alive
 */
void mr_xvm_gdb_detach(mr_xvm_gdb_t *gdb);

/**
 * @brief This function feeds received bytes into a GDB session.
 *
 * @param gdb The GDB session.
 * @param buf The received bytes.
 * @param len The number of bytes.
 * @return The number of bytes consumed, or a negative error code on failure.
 */
mr_ssize_t mr_xvm_gdb_recv(mr_xvm_gdb_t *gdb, const void *buf, mr_size_t len);

/**
 * @brief This function retries a pending reply.
 *
 * @param gdb The GDB session.
 * @return 0 on success, a negative error code on failure.
 *
 * @note Call when the transport becomes writable after a short send. After
 * the pending bytes are handed to send, ops->flush is invoked if present
 */
mr_err_t mr_xvm_gdb_flush(mr_xvm_gdb_t *gdb);

/**
 * @brief This function checks whether a reply is still pending.
 *
 * @param gdb The GDB session.
 * @return MR_TRUE if a reply is still waiting to be sent.
 */
mr_bool_t mr_xvm_gdb_want_send(const mr_xvm_gdb_t *gdb);

/**
 * @brief This function gets the GDB session state.
 *
 * @param gdb The GDB session.
 * @return The session state.
 */
mr_xvm_gdb_state_t mr_xvm_gdb_state(const mr_xvm_gdb_t *gdb);

/**
 * @brief This function gets an XVM GDB object.
 *
 * @param gdb The GDB session.
 * @return The GDB session on success, MR_NULL on failure.
 */
MR_INLINE mr_xvm_gdb_t *mr_xvm_gdb_get(mr_xvm_gdb_t *gdb) {
    /* Get GDB reference */
    return (mr_xvm_gdb_t *)mr_object_get((mr_object_t *)gdb);
}

/**
 * @brief This function puts an XVM GDB object.
 *
 * @param gdb The GDB session.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_xvm_gdb_put(mr_xvm_gdb_t *gdb) {
    /* Put GDB reference */
    return mr_object_put((mr_object_t *)gdb);
}
#endif /* defined(MR_USE_MODULE_XVM_GDB) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_MODULE_XVM_GDB_H__ */
