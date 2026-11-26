/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_MODULE_XBUS_H__
#define __MR_MODULE_XBUS_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_MODULE_XBUS)
#include <mrX/kernel/mr_list.h>
#include <mrX/kernel/mr_object.h>
#include <mrX/kernel/mr_clock.h>
#endif /* defined(MR_USE_MODULE_XBUS) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup XBus
 * @{
 */

#if defined(MR_USE_MODULE_XBUS)
struct mr_xbus_domain;
struct mr_xbus_topic;
struct mr_xbus_sub;

/* XBus host domain ID definition */
#define MR_XBUS_DOMAIN_HOST_ID          (0U)

/* XBus ACL type */
#if !defined(MR_CFG_XBUS_DOMAIN_MAX)
#define MR_CFG_XBUS_DOMAIN_MAX          (32U)
#endif /* !defined(MR_CFG_XBUS_DOMAIN_MAX) */
#if (MR_CFG_XBUS_DOMAIN_MAX < 1U) || (MR_CFG_XBUS_DOMAIN_MAX > 32U)
#error "MR_CFG_XBUS_DOMAIN_MAX must be in range 1..32"
#endif /* (MR_CFG_XBUS_DOMAIN_MAX < 1U) || (MR_CFG_XBUS_DOMAIN_MAX > 32U) */
#if (MR_CFG_XBUS_DOMAIN_MAX <= 8U)
typedef mr_uint8_t mr_xbus_acl_t;
#define MR_XBUS_ACL_ALL                 ((mr_xbus_acl_t)0xffU)
#elif (MR_CFG_XBUS_DOMAIN_MAX <= 16U)
typedef mr_uint16_t mr_xbus_acl_t;
#define MR_XBUS_ACL_ALL                 ((mr_xbus_acl_t)0xffffU)
#else
typedef mr_uint32_t mr_xbus_acl_t;
#define MR_XBUS_ACL_ALL                 ((mr_xbus_acl_t)0xffffffffU)
#endif /* (MR_CFG_XBUS_DOMAIN_MAX <= 8U) */
#define MR_XBUS_ACL_NONE                ((mr_xbus_acl_t)0U)

/* XBus topic flag definition */
#define MR_XBUS_TOPIC_FIXED             (1U << 0)

/* XBus message flag definition */
#define MR_XBUS_FLAG_STALE              (1U << 0)
#define MR_XBUS_FLAG_TRUNCATED          (1U << 1)
#define MR_XBUS_FLAG_DROPPED            (1U << 2)

/* XBus subscription flag definition */
#define MR_XBUS_SUB_RETAIN              (1U << 0)
#define MR_XBUS_SUB_NOTIFY_RETAIN       (1U << 1)
#define MR_XBUS_SUB_OVERWRITE           (1U << 2)

/* XBus subscription mode definition */
typedef enum mr_xbus_sub_mode {
    MR_XBUS_SUB_STATE,
    MR_XBUS_SUB_QUEUE,
} mr_xbus_sub_mode_t;

/* XBus name type */
typedef struct mr_xbus_name {
    const char *str;
    mr_uint32_t hash;
    mr_uint16_t len;
} mr_xbus_name_t;

/* XBus buffer type */
typedef struct mr_xbus_buf {
#if !defined(MR_CFG_XBUS_INLINE_DATA_LEN)
#define MR_CFG_XBUS_INLINE_DATA_LEN     (16U)
#endif /* !defined(MR_CFG_XBUS_INLINE_DATA_LEN) */
#if (MR_CFG_XBUS_INLINE_DATA_LEN > 0U)
    mr_uint8_t idata[MR_CFG_XBUS_INLINE_DATA_LEN];
#endif /* (MR_CFG_XBUS_INLINE_DATA_LEN > 0U) */
    mr_uint8_t *data;
    mr_uint16_t cap;
    mr_uint16_t len;
} mr_xbus_buf_t;

/* XBus metadata type */
typedef struct mr_xbus_meta {
    struct mr_xbus_topic *topic;
    mr_tick_t stamp;
    mr_uint32_t seq;
    mr_uint16_t len;
    mr_uint16_t dropped;
    mr_uint16_t type_id;
    mr_uint8_t owner;
    mr_uint8_t flags;
} mr_xbus_meta_t;

/* XBus notification callback type */
typedef void(mr_xbus_notify_t)(struct mr_xbus_sub *sub, void *param);

/* XBus topic iteration callback type */
typedef int(mr_xbus_topic_iter_t)(struct mr_xbus_topic *topic, void *param);

/* XBus topic configuration type */
typedef struct mr_xbus_topic_cfg {
    mr_xbus_acl_t read_mask;
    mr_xbus_acl_t write_mask;
    mr_uint16_t max_size;
    mr_uint16_t type_id;
    mr_uint8_t flags;
} mr_xbus_topic_cfg_t;

/* XBus subscription configuration type */
typedef struct mr_xbus_sub_cfg {
    mr_xbus_notify_t *notify;
    void *param;
    mr_uint16_t queue_depth;
    mr_uint16_t msg_size;
    mr_uint8_t mode;
    mr_uint8_t flags;
} mr_xbus_sub_cfg_t;

/* XBus domain type */
MR_CLAZZES_EXPORT(xbus_domain);
typedef struct mr_xbus_domain {
    mr_object_t parent;
#if !defined(MR_CFG_XBUS_INLINE_NAME_LEN)
#define MR_CFG_XBUS_INLINE_NAME_LEN     (16U)
#endif /* !defined(MR_CFG_XBUS_INLINE_NAME_LEN) */
#if (MR_CFG_XBUS_INLINE_NAME_LEN > 0U)
    char iname[MR_CFG_XBUS_INLINE_NAME_LEN];
#endif /* (MR_CFG_XBUS_INLINE_NAME_LEN > 0U) */
    const char *name;
    mr_list_t node;
    mr_list_t subs;
    mr_uint16_t name_len;
    mr_uint8_t id;
    mr_uint8_t flags;
} mr_xbus_domain_t;

/* XBus topic type */
MR_CLAZZES_EXPORT(xbus_topic);
typedef struct mr_xbus_topic {
    mr_object_t parent;
#if (MR_CFG_XBUS_INLINE_NAME_LEN > 0U)
    char iname[MR_CFG_XBUS_INLINE_NAME_LEN];
#endif /* (MR_CFG_XBUS_INLINE_NAME_LEN > 0U) */
    mr_xbus_name_t name;
    mr_list_t hash_node;
    mr_list_t binds;
    mr_xbus_buf_t buf;
    mr_tick_t stamp;
    mr_uint32_t seq;
    mr_xbus_acl_t read_mask;
    mr_xbus_acl_t write_mask;
    mr_uint16_t max_size;
    mr_uint16_t type_id;
    mr_uint8_t owner;
    mr_uint8_t flags;
} mr_xbus_topic_t;

/* XBus subscription state type */
typedef struct mr_xbus_sub_state {
    mr_list_t dirty_list;
    mr_uint16_t dirty_count;
} mr_xbus_sub_state_t;

/* XBus subscription queue type */
typedef struct mr_xbus_sub_queue {
    void *slots;
    mr_uint16_t depth;
    mr_uint16_t head;
    mr_uint16_t tail;
    mr_uint16_t count;
    mr_uint16_t msg_size;
    mr_uint16_t pending_dropped;
} mr_xbus_sub_queue_t;

/* XBus subscription storage type */
typedef union mr_xbus_sub_storage {
    mr_xbus_sub_state_t state;
    mr_xbus_sub_queue_t queue;
} mr_xbus_sub_storage_t;

/* XBus subscription type */
MR_CLAZZES_EXPORT(xbus_sub);
typedef struct mr_xbus_sub {
    mr_object_t parent;
#if (MR_CFG_XBUS_INLINE_NAME_LEN > 0U)
    char iname[MR_CFG_XBUS_INLINE_NAME_LEN];
#endif /* (MR_CFG_XBUS_INLINE_NAME_LEN > 0U) */
    const char *pattern;
    mr_xbus_domain_t *domain;
    mr_list_t bus_node;
    mr_list_t domain_node;
    mr_list_t binds;
    mr_xbus_notify_t *notify;
    void *param;
    mr_xbus_sub_storage_t u;
    mr_uint16_t pattern_len;
    mr_uint8_t flags;
} mr_xbus_sub_t;

/**
 * @brief This macro function builds an ACL bitmask for a domain.
 *
 * @param _domain The domain.
 * @return The ACL bitmask.
 */
#define MR_XBUS_ACL_DOMAIN(_domain)                                            \
    ((mr_xbus_acl_t)(1U << ((mr_uint32_t)(_domain)->id)))

/**
 * @brief This function initializes a domain.
 *
 * @param domain The domain.
 * @param name The domain name.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xbus_domain_init(mr_xbus_domain_t *domain, const char *name);

/**
 * @brief This function creates a domain.
 *
 * @param name The domain name.
 * @return The domain on success, MR_NULL on failure.
 */
mr_xbus_domain_t *mr_xbus_domain_create(const char *name);

/**
 * @brief This function deletes a domain.
 *
 * @param domain The domain.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xbus_domain_del(mr_xbus_domain_t *domain);

/**
 * @brief This function finds a domain by name.
 *
 * @param name The domain name.
 * @return The domain on success, MR_NULL on failure.
 */
mr_xbus_domain_t *mr_xbus_domain_find(const char *name);

/**
 * @brief This function gets the default host domain.
 *
 * @return The default host domain.
 */
mr_xbus_domain_t *mr_xbus_domain_default(void);

/**
 * @brief This function gets the current domain.
 *
 * @return The current domain on success, MR_NULL on failure.
 */
mr_xbus_domain_t *mr_xbus_domain_current(void);

/**
 * @brief This function resets a domain.
 *
 * @param domain The domain.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xbus_domain_reset(mr_xbus_domain_t *domain);

/**
 * @brief This macro function gets a domain name.
 *
 * @param _domain The domain.
 * @return The domain name.
 */
#define MR_XBUS_DOMAIN_NAME(_domain)    ((_domain)->name)

/**
 * @brief This macro function gets a domain ID.
 *
 * @param _domain The domain.
 * @return The domain ID.
 */
#define MR_XBUS_DOMAIN_ID(_domain)      ((_domain)->id)

/**
 * @brief This function gets a domain reference.
 *
 * @param domain The domain.
 * @return The domain on success, MR_NULL on failure.
 */
MR_INLINE mr_xbus_domain_t *mr_xbus_domain_get(mr_xbus_domain_t *domain) {
    /* Get domain reference */
    return (mr_xbus_domain_t *)mr_object_get((mr_object_t *)domain);
}

/**
 * @brief This function puts a domain reference.
 *
 * @param domain The domain.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_xbus_domain_put(mr_xbus_domain_t *domain) {
    /* Put domain reference */
    return mr_object_put((mr_object_t *)domain);
}

/**
 * @brief This function initializes a topic.
 *
 * @param topic The topic.
 * @param domain The domain.
 * @param path The topic path.
 * @param cfg The topic configuration.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xbus_topic_init(mr_xbus_topic_t *topic, mr_xbus_domain_t *domain,
                            const char *path, const mr_xbus_topic_cfg_t *cfg);

/**
 * @brief This function creates a topic.
 *
 * @param domain The domain.
 * @param path The topic path.
 * @param cfg The topic configuration.
 * @return The topic on success, MR_NULL on failure.
 */
mr_xbus_topic_t *mr_xbus_topic_create(mr_xbus_domain_t *domain,
                                      const char *path,
                                      const mr_xbus_topic_cfg_t *cfg);

/**
 * @brief This function deletes a topic.
 *
 * @param topic The topic.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xbus_topic_del(mr_xbus_topic_t *topic);

/**
 * @brief This function finds a topic by path.
 *
 * @param domain The domain.
 * @param path The topic path.
 * @return The topic on success, MR_NULL on failure.
 */
mr_xbus_topic_t *mr_xbus_topic_find(mr_xbus_domain_t *domain, const char *path);

/**
 * @brief This function sets topic ACL masks.
 *
 * @param topic The topic.
 * @param read_mask The read ACL mask.
 * @param write_mask The write ACL mask.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xbus_topic_acl_set(mr_xbus_topic_t *topic, mr_xbus_acl_t read_mask,
                               mr_xbus_acl_t write_mask);

/**
 * @brief This function grants topic ACL permissions to a domain.
 *
 * @param topic The topic.
 * @param domain The domain.
 * @param read MR_TRUE to grant read permission.
 * @param write MR_TRUE to grant write permission.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xbus_topic_acl_grant(mr_xbus_topic_t *topic,
                                 mr_xbus_domain_t *domain, mr_bool_t read,
                                 mr_bool_t write);

/**
 * @brief This function revokes topic ACL permissions from a domain.
 *
 * @param topic The topic.
 * @param domain The domain.
 * @param read MR_TRUE to revoke read permission.
 * @param write MR_TRUE to revoke write permission.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xbus_topic_acl_revoke(mr_xbus_topic_t *topic,
                                  mr_xbus_domain_t *domain, mr_bool_t read,
                                  mr_bool_t write);

/**
 * @brief This function publishes a message to a topic.
 *
 * @param domain The domain.
 * @param topic The topic.
 * @param data The message payload.
 * @param len The message payload length.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xbus_publish(mr_xbus_domain_t *domain, mr_xbus_topic_t *topic,
                         const void *data, mr_size_t len);

/**
 * @brief This function publishes a message to a topic by path.
 *
 * @param domain The domain.
 * @param path The topic path.
 * @param data The message payload.
 * @param len The message payload length.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xbus_publish_path(mr_xbus_domain_t *domain, const char *path,
                              const void *data, mr_size_t len);

/**
 * @brief This function reads the latest message from a topic.
 *
 * @param domain The domain.
 * @param topic The topic.
 * @param buf The destination buffer.
 * @param size The destination buffer size.
 * @param meta The metadata output.
 * @return The number of bytes copied on success, a negative error code on failure.
 */
mr_ssize_t mr_xbus_topic_read(mr_xbus_domain_t *domain, mr_xbus_topic_t *topic,
                              void *buf, mr_size_t size, mr_xbus_meta_t *meta);

/**
 * @brief This function iterates over all registered topics.
 *
 * @param iter The iteration callback.
 * @param param The callback parameter.
 * @return 0 on success, or the non-zero return value from iter.
 */
mr_err_t mr_xbus_topic_foreach(mr_xbus_topic_iter_t *iter, void *param);

/**
 * @brief This macro function gets a topic name.
 *
 * @param _topic The topic.
 * @return The topic name.
 */
#define MR_XBUS_TOPIC_NAME(_topic)      ((_topic)->name.str)

/**
 * @brief This function gets a topic reference.
 *
 * @param topic The topic.
 * @return The topic on success, MR_NULL on failure.
 */
MR_INLINE mr_xbus_topic_t *mr_xbus_topic_get(mr_xbus_topic_t *topic) {
    /* Get topic reference */
    return (mr_xbus_topic_t *)mr_object_get((mr_object_t *)topic);
}

/**
 * @brief This function puts a topic reference.
 *
 * @param topic The topic.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_xbus_topic_put(mr_xbus_topic_t *topic) {
    /* Put topic reference */
    return mr_object_put((mr_object_t *)topic);
}

/**
 * @brief This function initializes a subscription.
 *
 * @param sub The subscription.
 * @param domain The domain.
 * @param pattern The topic path or wildcard pattern.
 * @param cfg The subscription configuration.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xbus_sub_init(mr_xbus_sub_t *sub, mr_xbus_domain_t *domain,
                          const char *pattern, const mr_xbus_sub_cfg_t *cfg);

/**
 * @brief This function creates a subscription.
 *
 * @param domain The domain.
 * @param pattern The topic path or wildcard pattern.
 * @param cfg The subscription configuration.
 * @return The subscription on success, MR_NULL on failure.
 */
mr_xbus_sub_t *mr_xbus_sub_create(mr_xbus_domain_t *domain, const char *pattern,
                                  const mr_xbus_sub_cfg_t *cfg);

/**
 * @brief This function deletes a subscription.
 *
 * @param sub The subscription.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xbus_sub_del(mr_xbus_sub_t *sub);

/**
 * @brief This function reads the next unread message from a subscription.
 *
 * @param sub The subscription.
 * @param buf The destination buffer.
 * @param size The destination buffer size.
 * @param meta The metadata output.
 * @return The number of bytes copied on success, a negative error code on failure.
 */
mr_ssize_t mr_xbus_sub_read(mr_xbus_sub_t *sub, void *buf, mr_size_t size,
                            mr_xbus_meta_t *meta);

/**
 * @brief This function peeks at the next unread message from a subscription.
 *
 * @param sub The subscription.
 * @param buf The destination buffer.
 * @param size The destination buffer size.
 * @param meta The metadata output.
 * @return The number of bytes copied on success, a negative error code on failure.
 */
mr_ssize_t mr_xbus_sub_peek(mr_xbus_sub_t *sub, void *buf, mr_size_t size,
                            mr_xbus_meta_t *meta);

/**
 * @brief This function gets the number of pending messages in a subscription.
 *
 * @param sub The subscription.
 * @return The number of pending messages.
 */
mr_uint32_t mr_xbus_sub_pending(mr_xbus_sub_t *sub);

/**
 * @brief This function clears all unread messages from a subscription.
 *
 * @param sub The subscription.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xbus_sub_clear(mr_xbus_sub_t *sub);

/**
 * @brief This macro function gets a subscription pattern.
 *
 * @param _sub The subscription.
 * @return The subscription pattern.
 */
#define MR_XBUS_SUB_PATTERN(_sub)       ((_sub)->pattern)

/**
 * @brief This function gets a subscription reference.
 *
 * @param sub The subscription.
 * @return The subscription on success, MR_NULL on failure.
 */
MR_INLINE mr_xbus_sub_t *mr_xbus_sub_get(mr_xbus_sub_t *sub) {
    /* Get subscription reference */
    return (mr_xbus_sub_t *)mr_object_get((mr_object_t *)sub);
}

/**
 * @brief This function puts a subscription reference.
 *
 * @param sub The subscription.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_xbus_sub_put(mr_xbus_sub_t *sub) {
    /* Put subscription reference */
    return mr_object_put((mr_object_t *)sub);
}
#endif /* defined(MR_USE_MODULE_XBUS) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_MODULE_XBUS_H__ */
