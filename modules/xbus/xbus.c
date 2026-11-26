/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/modules/xbus/mr_xbus.h>
#if defined(MR_USE_MODULE_XBUS)
#include <mrX/kernel/mr_initcall.h>
#include <mrX/kernel/mr_spinlock.h>
#include <mrX/libc/mr_atomic.h>
#include <mrX/libc/mr_malloc.h>
#include <mrX/libc/mr_string.h>
#if defined(MR_USE_MODULE_XBUS_TRACE)
#define MR_TRACE_TAG                    "xbus"
#endif /* defined(MR_USE_MODULE_XBUS_TRACE) */
#include <mrX/kernel/mr_trace.h>

/* XBus topic bucket count definition */
#if !defined(MR_CFG_XBUS_TOPIC_BUCKETS)
#define MR_CFG_XBUS_TOPIC_BUCKETS       (8U)
#endif /* !defined(MR_CFG_XBUS_TOPIC_BUCKETS) */

/* XBus hash seed definition */
#define XBUS_HASH_SEED                  (5381U)

/* XBus stack collector capacity definition */
#define XBUS_NOTIFY_STACK_CAP           (4U)
#define XBUS_TOPIC_REF_STACK_CAP        (4U)

/* XBus host domain name definition */
#define XBUS_HOST_DOMAIN_NAME           "host"

/* XBus domain flag definition */
#define XBUS_DOMAIN_F_REGISTERED        (1U << 0)
#define XBUS_DOMAIN_F_CLOSING           (1U << 1)

/* XBus topic flag definition */
#define XBUS_TOPIC_F_FIXED              MR_XBUS_TOPIC_FIXED
#define XBUS_TOPIC_F_STALE              (1U << 1)
#define XBUS_TOPIC_F_REGISTERED         (1U << 2)

/* XBus subscription flag definition */
#define XBUS_SUB_F_PUBLIC_MASK                                                 \
    (MR_XBUS_SUB_RETAIN | MR_XBUS_SUB_NOTIFY_RETAIN | MR_XBUS_SUB_OVERWRITE)
#define XBUS_SUB_F_QUEUE                (1U << 3)
#define XBUS_SUB_F_WILDCARD             (1U << 4)
#define XBUS_SUB_F_DETACHED             (1U << 5)
#define XBUS_SUB_F_REGISTERED           (1U << 6)

/* XBus binding flag definition */
#define XBUS_BIND_F_DIRTY               (1U << 0)

/* XBus inline name helper definition */
#if (MR_CFG_XBUS_INLINE_NAME_LEN > 0U)
#define XBUS_INAME_PTR(_obj)            ((_obj)->iname)
#define XBUS_INAME_CAP(_obj)            sizeof((_obj)->iname)
#define XBUS_FIND_SCRATCH_LEN           (MR_CFG_XBUS_INLINE_NAME_LEN * 2U)
#else
#define XBUS_INAME_PTR(_obj)            MR_NULL
#define XBUS_INAME_CAP(_obj)            (0U)
#define XBUS_FIND_SCRATCH_LEN           (32U)
#endif /* (MR_CFG_XBUS_INLINE_NAME_LEN > 0U) */

/* XBus inline data helper definition */
#if (MR_CFG_XBUS_INLINE_DATA_LEN > 0U)
#define XBUS_IDATA_PTR(_buf)            ((_buf)->idata)
#define XBUS_IDATA_CAP(_buf)            ((mr_uint16_t)sizeof((_buf)->idata))
#else
#define XBUS_IDATA_PTR(_buf)            MR_NULL
#define XBUS_IDATA_CAP(_buf)            (0U)
#endif /* (MR_CFG_XBUS_INLINE_DATA_LEN > 0U) */

/* XBus binding type */
typedef struct xbus_bind {
    mr_xbus_topic_t *topic;
    mr_xbus_sub_t *sub;
    mr_list_t topic_node;
    mr_list_t sub_node;
    mr_list_t dirty_node;
    mr_uint32_t last_seq;
    mr_uint16_t dropped;
    mr_uint8_t flags;
} xbus_bind_t;

/* XBus queue slot type */
typedef struct xbus_slot {
    mr_xbus_topic_t *topic;
    mr_tick_t stamp;
    mr_uint32_t seq;
    mr_uint16_t len;
    mr_uint8_t owner;
    mr_uint8_t flags;
} xbus_slot_t;

/* XBus variable-length queue slot buffer type */
typedef struct xbus_vbuf {
    mr_uint8_t *data;
    mr_uint16_t cap;
} xbus_vbuf_t;

/* XBus notify item type */
typedef struct xbus_notify_item {
    mr_xbus_sub_t *sub;
    mr_xbus_notify_t *notify;
    void *param;
} xbus_notify_item_t;

/* XBus notify list type */
typedef struct xbus_notify_list {
    xbus_notify_item_t stack[XBUS_NOTIFY_STACK_CAP];
    xbus_notify_item_t *items;
    mr_size_t count;
    mr_size_t cap;
} xbus_notify_list_t;

/* XBus clazzes(static & dynamic) */
static void xbus_domain_statik_release(mr_object_t *obj);
static void xbus_domain_dynamic_release(mr_object_t *obj);
static void xbus_topic_statik_release(mr_object_t *obj);
static void xbus_topic_dynamic_release(mr_object_t *obj);
static void xbus_sub_statik_release(mr_object_t *obj);
static void xbus_sub_dynamic_release(mr_object_t *obj);
MR_CLAZZES_DEFINE(xbus_domain, xbus_domain_statik_release,
                  xbus_domain_dynamic_release, MR_NULL);
MR_CLAZZES_DEFINE(xbus_topic, xbus_topic_statik_release,
                  xbus_topic_dynamic_release, MR_NULL);
MR_CLAZZES_DEFINE(xbus_sub, xbus_sub_statik_release, xbus_sub_dynamic_release,
                  MR_NULL);

/* XBus lock */
static mr_spinlock_t __xbus_lock = MR_SPINLOCK_INIT();
/* XBus topic buckets */
static mr_list_t __xbus_topic_buckets[MR_CFG_XBUS_TOPIC_BUCKETS];
/* XBus domain list */
static mr_list_t __xbus_domain_list = MR_LIST_INIT(&__xbus_domain_list);
/* XBus subscription list */
static mr_list_t __xbus_sub_list = MR_LIST_INIT(&__xbus_sub_list);
/* XBus domain ID bitmap */
static mr_xbus_acl_t __xbus_domain_bitmap = 0U;
/* XBus built-in host domain */
static mr_xbus_domain_t __xbus_host_domain;
/* XBus init flag */
static mr_atomic_t __xbus_inited = MR_ATOMIC_INIT(0);

static mr_uint32_t xbus_str_hash(const char *str, mr_uint16_t len) {
    mr_uint32_t hash;
    mr_uint16_t i;

    /* Compute djb2-xor hash */
    hash = XBUS_HASH_SEED;
    for (i = 0U; i < len; i++) {
        hash = ((hash << 5) + hash) ^ (mr_uint8_t)str[i];
    }
    return hash;
}

static mr_err_t xbus_str_alloc(const char **out_str, mr_uint16_t *out_len,
                               char *iname, mr_size_t iname_cap,
                               const char *str, mr_size_t len) {
    char *dst;

    /* Check length */
    if (len > 0xffffU) {
        return -MR_EINVAL;
    }

    /* Select inline or dynamic string buffer */
    if (iname && (len < iname_cap)) {
        dst = iname;
    } else {
        dst = mr_malloc(len + 1U);
        if (!dst) {
            return -MR_ENOMEM;
        }
    }

    /* Copy string and terminate */
    if (len > 0U) {
        mr_memcpy(dst, str, len);
    }
    dst[len] = '\0';
    *out_str = dst;
    *out_len = (mr_uint16_t)len;
    return 0;
}

static void xbus_str_free(const char **str, mr_uint16_t *len,
                          const char *iname) {
    /* Free dynamic string buffer */
    if ((*str) && ((*str) != iname)) {
        mr_free((void *)(*str));
    }
    *str = MR_NULL;
    *len = 0U;
}

static void xbus_buf_init(mr_xbus_buf_t *buf) {
    /* Init inline buffer */
    buf->data = XBUS_IDATA_PTR(buf);
    buf->cap = XBUS_IDATA_CAP(buf);
    buf->len = 0U;
}

static void xbus_buf_free(mr_xbus_buf_t *buf) {
    /* Free dynamic buffer */
    if (buf->data && (buf->data != XBUS_IDATA_PTR(buf))) {
        mr_free(buf->data);
    }
    buf->data = XBUS_IDATA_PTR(buf);
    buf->cap = XBUS_IDATA_CAP(buf);
    buf->len = 0U;
}

static void xbus_buf_install_locked(mr_xbus_buf_t *buf, mr_uint8_t *mem,
                                    mr_uint16_t cap, mr_uint8_t **old_out) {
    *old_out = MR_NULL;

    /* Preserve existing payload and replace buffer if new capacity is larger */
    if (cap > buf->cap) {
        if ((buf->len > 0U) && buf->data) {
            mr_memcpy(mem, buf->data, buf->len);
        }
        if (buf->data && (buf->data != XBUS_IDATA_PTR(buf))) {
            *old_out = buf->data;
        }
        buf->data = mem;
        buf->cap = cap;
    } else {
        *old_out = mem;
    }
}

static xbus_slot_t *xbus_sub_slot(mr_xbus_sub_t *sub, mr_uint16_t idx) {
    /* Get queue slot header */
    return &((xbus_slot_t *)sub->u.queue.slots)[idx];
}

static xbus_vbuf_t *xbus_sub_vbuf(mr_xbus_sub_t *sub, mr_uint16_t idx) {
    xbus_slot_t *slots;

    /* Get variable-length descriptor after slot headers */
    slots = (xbus_slot_t *)sub->u.queue.slots;
    return &((xbus_vbuf_t *)(slots + sub->u.queue.depth))[idx];
}

static mr_uint8_t *xbus_sub_slot_data(mr_xbus_sub_t *sub, mr_uint16_t idx) {
    xbus_slot_t *slots;

    /* Return fixed-size pool entry or variable-length buffer */
    if (sub->u.queue.msg_size > 0U) {
        slots = (xbus_slot_t *)sub->u.queue.slots;
        return ((mr_uint8_t *)(slots + sub->u.queue.depth))
               + ((mr_size_t)idx * sub->u.queue.msg_size);
    }
    return xbus_sub_vbuf(sub, idx)->data;
}

static void *xbus_sub_slots_alloc(mr_uint16_t depth, mr_uint16_t msg_size) {
    mr_size_t slots_bytes, extra_bytes, depth_sz;
    void *mem;

    /* Compute slot header array size with overflow check */
    depth_sz = (mr_size_t)depth;
    if (depth_sz > (MR_SIZE_MAX / sizeof(xbus_slot_t))) {
        return MR_NULL;
    }
    slots_bytes = depth_sz * sizeof(xbus_slot_t);

    /* Compute payload pool or vbuf descriptor array size */
    if (msg_size > 0U) {
        if (depth_sz > (MR_SIZE_MAX / (mr_size_t)msg_size)) {
            return MR_NULL;
        }
        extra_bytes = depth_sz * (mr_size_t)msg_size;
    } else {
        if (depth_sz > (MR_SIZE_MAX / sizeof(xbus_vbuf_t))) {
            return MR_NULL;
        }
        extra_bytes = depth_sz * sizeof(xbus_vbuf_t);
    }

    /* Allocate contiguous slot pool */
    if (slots_bytes > (MR_SIZE_MAX - extra_bytes)) {
        return MR_NULL;
    }
    mem = mr_malloc(slots_bytes + extra_bytes);
    if (!mem) {
        return MR_NULL;
    }

    /* Zero slot headers and vbuf descriptors */
    mr_memset(mem, 0,
              (msg_size > 0U) ? slots_bytes : (slots_bytes + extra_bytes));
    return mem;
}

static void xbus_sub_slots_free(void *mem, mr_uint16_t depth,
                                mr_uint16_t msg_size) {
    xbus_slot_t *slots;
    xbus_vbuf_t *vbufs;
    mr_uint16_t i;

    /* Check parameter */
    if (!mem) {
        return;
    }

    /* Release queued topic references and variable-length buffers */
    slots = (xbus_slot_t *)mem;
    vbufs = (msg_size == 0U) ? (xbus_vbuf_t *)(slots + depth) : MR_NULL;
    for (i = 0U; i < depth; i++) {
        if (slots[i].topic) {
            mr_xbus_topic_put(slots[i].topic);
            slots[i].topic = MR_NULL;
        }
        if (vbufs && vbufs[i].data) {
            mr_free(vbufs[i].data);
            vbufs[i].data = MR_NULL;
        }
    }

    /* Free slot pool */
    mr_free(mem);
}

static mr_bool_t xbus_domain_name_valid(const char *name, mr_size_t *out_len) {
    mr_size_t len;
    char ch;

    /* Check parameter */
    if (!name) {
        return MR_FALSE;
    }

    /* Scan name for forbidden separator and wildcard characters */
    for (len = 0U; name[len] != '\0'; len++) {
        ch = name[len];
        if ((ch == '/') || (ch == '*') || (ch == '?') || (ch == '#')) {
            return MR_FALSE;
        }
    }

    /* Reject empty, overlong, and dot-only names */
    if ((len == 0U) || (len > 0xffffU)
        || ((len == 1U) && (name[0] == '.'))
        || ((len == 2U) && (name[0] == '.') && (name[1] == '.'))) {
        return MR_FALSE;
    }

    /* Output length */
    if (out_len) {
        *out_len = len;
    }
    return MR_TRUE;
}

static mr_err_t xbus_path_validate(const char *path, mr_bool_t is_pattern,
                                   mr_bool_t *is_wildcard, mr_size_t *out_len) {
    const char *seg_start, *p;
    mr_bool_t has_wild;
    mr_size_t seg_len;
    mr_size_t i;
    char ch;

    /* Check parameter */
    if ((!path) || (path[0] == '\0')) {
        return -MR_EINVAL;
    }

    /* Skip optional leading slash */
    has_wild = MR_FALSE;
    p = (path[0] == '/') ? (path + 1) : path;
    if (*p == '\0') {
        return -MR_EINVAL;
    }

    /* Validate each path segment */
    while (*p != '\0') {
        seg_start = p;
        seg_len = 0U;
        while ((*p != '\0') && (*p != '/')) {
            ch = *p;
            if ((ch == '*') || (ch == '?') || (ch == '#')) {
                if (!is_pattern) {
                    return -MR_EINVAL;
                }
                has_wild = MR_TRUE;
            }
            p++;
            seg_len++;
        }

        /* Reject empty and dot segments */
        if ((seg_len == 0U) || ((seg_len == 1U) && (seg_start[0] == '.'))
            || ((seg_len == 2U) && (seg_start[0] == '.')
                && (seg_start[1] == '.'))) {
            return -MR_EINVAL;
        }

        /* '#' is only allowed as a standalone trailing segment */
        if (is_pattern) {
            for (i = 0U; i < seg_len; i++) {
                if ((seg_start[i] == '#')
                    && ((seg_len != 1U) || (*p != '\0'))) {
                    return -MR_EINVAL;
                }
            }
        }

        /* Advance past segment separator and reject trailing slash */
        if (*p == '/') {
            p++;
            if (*p == '\0') {
                return -MR_EINVAL;
            }
        }
    }

    /* Output wildcard flag and path length */
    if (is_wildcard) {
        *is_wildcard = has_wild;
    }
    if (out_len) {
        *out_len = (mr_size_t)(p - path);
    }
    return 0;
}

static mr_err_t xbus_path_resolve(const mr_xbus_domain_t *domain,
                                  const char *raw_path, mr_bool_t is_pattern,
                                  const char **out_str, mr_uint16_t *out_len,
                                  mr_uint32_t *out_hash, char *iname,
                                  mr_size_t iname_cap,
                                  mr_bool_t *out_is_wildcard) {
    mr_size_t raw_len, full_len;
    mr_err_t ret;
    char *dst;

    /* Validate path syntax */
    ret = xbus_path_validate(raw_path, is_pattern, out_is_wildcard, &raw_len);
    if (ret != 0) {
        return ret;
    }

    /* Copy absolute path directly */
    if (raw_path[0] == '/') {
        ret = xbus_str_alloc(out_str, out_len, iname, iname_cap, raw_path,
                             raw_len);
        if ((ret == 0) && out_hash) {
            *out_hash = xbus_str_hash(*out_str, *out_len);
        }
        return ret;
    }

    /* Check domain for relative path */
    if (!domain) {
        return -MR_EINVAL;
    }

    /* Compute "/<domain>/<path>" length */
    full_len = 1U + (mr_size_t)domain->name_len + 1U + raw_len;
    if (full_len > 0xffffU) {
        return -MR_EINVAL;
    }

    /* Select inline or dynamic destination buffer */
    if (iname && (full_len < iname_cap)) {
        dst = iname;
    } else {
        dst = mr_malloc(full_len + 1U);
        if (!dst) {
            return -MR_ENOMEM;
        }
    }

    /* Format normalized absolute path */
    dst[0] = '/';
    mr_memcpy(dst + 1U, domain->name, domain->name_len);
    dst[1U + domain->name_len] = '/';
    mr_memcpy(dst + 2U + domain->name_len, raw_path, raw_len);
    dst[full_len] = '\0';

    /* Output path and hash */
    *out_str = dst;
    *out_len = (mr_uint16_t)full_len;
    if (out_hash) {
        *out_hash = xbus_str_hash(dst, (mr_uint16_t)full_len);
    }
    return 0;
}

static mr_bool_t xbus_seg_match(const char *pat, const char *pat_end,
                                const char *str, const char *str_end) {
    const char *star_pat, *star_str;

    /* Match single segment with '?' and '*' wildcards */
    star_pat = MR_NULL;
    star_str = MR_NULL;
    while (str < str_end) {
        if ((pat < pat_end) && ((*pat == '?') || (*pat == *str))) {
            pat++;
            str++;
        } else if ((pat < pat_end) && (*pat == '*')) {
            star_pat = pat++;
            star_str = str;
        } else if (star_pat != MR_NULL) {
            pat = star_pat + 1;
            str = ++star_str;
        } else {
            return MR_FALSE;
        }
    }

    /* Consume trailing '*' wildcards */
    while ((pat < pat_end) && (*pat == '*')) {
        pat++;
    }
    return (pat == pat_end) ? MR_TRUE : MR_FALSE;
}

static mr_bool_t xbus_pattern_match(const char *pat, const char *str) {
    const char *ps, *pe, *ss, *se;

    /* Check normalized leading slash */
    if ((!pat) || (!str) || (*pat != '/') || (*str != '/')) {
        return MR_FALSE;
    }

    /* Match pattern segment by segment */
    ps = pat + 1;
    ss = str + 1;
    for (;;) {
        if ((ps[0] == '#') && (ps[1] == '\0')) {
            return MR_TRUE;
        }
        if ((*ps == '\0') && (*ss == '\0')) {
            return MR_TRUE;
        }
        if ((*ps == '\0') || (*ss == '\0')) {
            return MR_FALSE;
        }

        /* Find end of current pattern and topic segments */
        pe = ps;
        while ((*pe != '\0') && (*pe != '/')) {
            pe++;
        }
        se = ss;
        while ((*se != '\0') && (*se != '/')) {
            se++;
        }

        /* Compare segments */
        if (!xbus_seg_match(ps, pe, ss, se)) {
            return MR_FALSE;
        }

        /* Advance to next segment */
        ps = (*pe == '/') ? (pe + 1) : pe;
        ss = (*se == '/') ? (se + 1) : se;
    }
}

static void xbus_ensure_init_locked(void) {
    mr_uint32_t b;
    mr_err_t ret;

    /* Skip if already initialized */
    if (mr_atomic_load(&__xbus_inited) != 0) {
        return;
    }

    /* Init global topic buckets and registries */
    for (b = 0U; b < MR_CFG_XBUS_TOPIC_BUCKETS; b++) {
        mr_list_init(&__xbus_topic_buckets[b]);
    }
    mr_list_init(&__xbus_domain_list);
    mr_list_init(&__xbus_sub_list);
    __xbus_domain_bitmap = 0U;

    /* Init built-in host domain */
    mr_memset(&__xbus_host_domain, 0, sizeof(__xbus_host_domain));
    __xbus_host_domain.name = XBUS_HOST_DOMAIN_NAME;
    __xbus_host_domain.name_len
        = (mr_uint16_t)(sizeof(XBUS_HOST_DOMAIN_NAME) - 1U);
    mr_list_init(&__xbus_host_domain.node);
    mr_list_init(&__xbus_host_domain.subs);
    __xbus_host_domain.id = MR_XBUS_DOMAIN_HOST_ID;
    __xbus_host_domain.flags = XBUS_DOMAIN_F_REGISTERED;

    /* Init host domain object */
    ret = mr_object_init(&__xbus_host_domain.parent,
                         MR_CLAZZES_STATIK(xbus_domain));
    MR_ASSERT(ret == 0);
    MR_IGNORE(ret);

    /* Register host domain */
    __xbus_domain_bitmap |= (mr_xbus_acl_t)(1U << MR_XBUS_DOMAIN_HOST_ID);
    mr_list_add(&__xbus_domain_list, &__xbus_host_domain.node);
    mr_atomic_store(&__xbus_inited, 1);
}

static void xbus_ensure_init(void) {
    int mask;

    /* Fast path when already initialized */
    if (mr_atomic_load(&__xbus_inited) != 0) {
        return;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xbus_lock);

    /* Init XBus globals */
    xbus_ensure_init_locked();

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
}

static void xbus_notify_list_init(xbus_notify_list_t *list) {
    /* Init notify collector on stack buffer */
    list->items = list->stack;
    list->count = 0U;
    list->cap = XBUS_NOTIFY_STACK_CAP;
}

static void xbus_notify_list_push_locked(xbus_notify_list_t *list,
                                         mr_xbus_sub_t *sub) {
    mr_size_t i;

    /* Skip subscriptions without callback or when collector is full */
    if ((!sub->notify) || (list->count >= list->cap)) {
        return;
    }

    /* Deduplicate subscription within current wave */
    for (i = 0U; i < list->count; i++) {
        if (list->items[i].sub == sub) {
            return;
        }
    }

    /* Acquire subscription reference for unlocked callback */
    if (!mr_xbus_sub_get(sub)) {
        return;
    }

    /* Append notify entry */
    list->items[list->count].sub = sub;
    list->items[list->count].notify = sub->notify;
    list->items[list->count].param = sub->param;
    list->count++;
}

static void xbus_notify_list_flush(xbus_notify_list_t *list) {
    mr_size_t i;

    /* Invoke notify callbacks and release subscription references */
    for (i = 0U; i < list->count; i++) {
        list->items[i].notify(list->items[i].sub, list->items[i].param);
        mr_xbus_sub_put(list->items[i].sub);
    }

    /* Free heap collector buffer if allocated */
    if (list->items != list->stack) {
        mr_free(list->items);
    }
    list->items = list->stack;
    list->count = 0U;
    list->cap = XBUS_NOTIFY_STACK_CAP;
}

static mr_xbus_domain_t *xbus_domain_resolve(mr_xbus_domain_t *domain) {
    /* Fall back to current domain when MR_NULL */
    if (domain != MR_NULL) {
        return domain;
    }
    return mr_xbus_domain_current();
}

static mr_xbus_domain_t *xbus_domain_find_locked(const char *name,
                                                 mr_uint16_t len) {
    mr_xbus_domain_t *dom;
    mr_list_t *pos;

    /* Search domain list by exact length and bytes */
    MR_LIST_FOR_EACH(pos, &__xbus_domain_list) {
        dom = MR_LIST_ENTRY(pos, mr_xbus_domain_t, node);
        if ((dom->name_len == len) && (mr_memcmp(dom->name, name, len) == 0)) {
            return dom;
        }
    }
    return MR_NULL;
}

static mr_list_t *xbus_topic_bucket(mr_uint32_t hash) {
    /* Select bucket by hash modulo bucket count */
    return &__xbus_topic_buckets[hash % MR_CFG_XBUS_TOPIC_BUCKETS];
}

static mr_xbus_topic_t *xbus_topic_find_locked(const char *name,
                                               mr_uint16_t len,
                                               mr_uint32_t hash) {
    mr_xbus_topic_t *topic;
    mr_list_t *bucket;
    mr_list_t *pos;

    /* Search bucket by hash and full path */
    bucket = xbus_topic_bucket(hash);
    MR_LIST_FOR_EACH(pos, bucket) {
        topic = MR_LIST_ENTRY(pos, mr_xbus_topic_t, hash_node);
        if ((topic->name.hash == hash) && (topic->name.len == len)
            && (mr_memcmp(topic->name.str, name, len) == 0)) {
            return topic;
        }
    }
    return MR_NULL;
}

static xbus_bind_t *xbus_bind_find_locked(mr_xbus_sub_t *sub,
                                          mr_xbus_topic_t *topic) {
    xbus_bind_t *bind;
    mr_list_t *pos;

    /* Search subscription bindings for target topic */
    MR_LIST_FOR_EACH(pos, &sub->binds) {
        bind = MR_LIST_ENTRY(pos, xbus_bind_t, sub_node);
        if (bind->topic == topic) {
            return bind;
        }
    }
    return MR_NULL;
}

static mr_bool_t xbus_sub_is_active_locked(const mr_xbus_sub_t *sub) {
    /* Check subscription and owning domain are active */
    return (((sub->flags & XBUS_SUB_F_REGISTERED) != 0U)
            && ((sub->flags & XBUS_SUB_F_DETACHED) == 0U)
            && ((sub->domain->flags & XBUS_DOMAIN_F_REGISTERED) != 0U))
             ? MR_TRUE
             : MR_FALSE;
}

static mr_bool_t xbus_sub_matches_topic_locked(const mr_xbus_sub_t *sub,
                                               const mr_xbus_topic_t *topic) {
    /* Enforce owning domain registration and topic read ACL */
    if (((sub->domain->flags & XBUS_DOMAIN_F_REGISTERED) == 0U)
        || ((topic->read_mask & MR_XBUS_ACL_DOMAIN(sub->domain)) == 0U)) {
        return MR_FALSE;
    }

    /* Compare exact path or wildcard pattern */
    if ((sub->flags & XBUS_SUB_F_WILDCARD) == 0U) {
        return ((sub->pattern_len == topic->name.len)
                && (mr_memcmp(sub->pattern, topic->name.str, topic->name.len)
                    == 0))
                 ? MR_TRUE
                 : MR_FALSE;
    }
    return xbus_pattern_match(sub->pattern, topic->name.str);
}

static void xbus_bind_attach_locked(xbus_bind_t *bind, mr_xbus_topic_t *topic,
                                    mr_xbus_sub_t *sub,
                                    xbus_notify_list_t *nlist) {
    /* Init binding */
    bind->topic = topic;
    bind->sub = sub;
    bind->last_seq = 0U;
    bind->dropped = 0U;
    bind->flags = 0U;
    mr_list_init(&bind->topic_node);
    mr_list_init(&bind->sub_node);
    mr_list_init(&bind->dirty_node);

    /* Link binding to topic and subscription */
    mr_list_add(&topic->binds, &bind->topic_node);
    mr_list_add(&sub->binds, &bind->sub_node);

    /* Deliver retained STATE snapshot */
    if (((sub->flags & XBUS_SUB_F_QUEUE) == 0U)
        && ((sub->flags & MR_XBUS_SUB_RETAIN) != 0U) && (topic->seq != 0U)) {
        bind->flags |= XBUS_BIND_F_DIRTY;
        mr_list_add(&sub->u.state.dirty_list, &bind->dirty_node);
        sub->u.state.dirty_count++;
        if (nlist && ((sub->flags & MR_XBUS_SUB_NOTIFY_RETAIN) != 0U)) {
            xbus_notify_list_push_locked(nlist, sub);
        }
    }
}

static void xbus_bind_detach_locked(xbus_bind_t *bind, mr_list_t *free_list) {
    mr_xbus_sub_t *sub;

    sub = bind->sub;

    /* Remove from STATE dirty list */
    if ((bind->flags & XBUS_BIND_F_DIRTY) != 0U) {
        mr_list_del(&bind->dirty_node);
        bind->flags &= (mr_uint8_t)(~XBUS_BIND_F_DIRTY);
        if (sub && ((sub->flags & XBUS_SUB_F_QUEUE) == 0U)
            && (sub->u.state.dirty_count > 0U)) {
            sub->u.state.dirty_count--;
        }
    }

    /* Unlink from topic and subscription */
    mr_list_del(&bind->topic_node);
    mr_list_del(&bind->sub_node);
    bind->topic = MR_NULL;
    bind->sub = MR_NULL;

    /* Defer heap free until lock is released */
    if (free_list) {
        mr_list_add(free_list, &bind->sub_node);
    }
}

static void xbus_bind_free_list(mr_list_t *free_list) {
    mr_list_t *pos, *next;
    xbus_bind_t *bind;

    /* Free all deferred binding nodes */
    MR_LIST_FOR_EACH_SAFE(pos, next, free_list) {
        bind = MR_LIST_ENTRY(pos, xbus_bind_t, sub_node);
        mr_list_del(&bind->sub_node);
        mr_free(bind);
    }
}

static mr_err_t xbus_topic_sync_subs(mr_xbus_topic_t *topic) {
    mr_xbus_sub_t *sub, *target_sub;
    xbus_bind_t *bind, *new_bind;
    xbus_notify_list_t nlist;
    mr_list_t *pos, *next;
    mr_list_t free_list;
    int mask;

    /* Init collectors */
    xbus_notify_list_init(&nlist);
    mr_list_init(&free_list);

    for (;;) {
        target_sub = MR_NULL;

        /* Lock */
        mask = mr_spinlock_lock_irqsave(&__xbus_lock);

        /* Stop if topic was unregistered */
        if ((topic->flags & XBUS_TOPIC_F_REGISTERED) == 0U) {
            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
            break;
        }

        /* Detach bindings no longer active or permitted by ACL */
        MR_LIST_FOR_EACH_SAFE(pos, next, &topic->binds) {
            bind = MR_LIST_ENTRY(pos, xbus_bind_t, topic_node);
            if ((!xbus_sub_is_active_locked(bind->sub))
                || (!xbus_sub_matches_topic_locked(bind->sub, topic))) {
                xbus_bind_detach_locked(bind, &free_list);
            }
        }

        /* Find next unbound matching subscription */
        MR_LIST_FOR_EACH(pos, &__xbus_sub_list) {
            sub = MR_LIST_ENTRY(pos, mr_xbus_sub_t, bus_node);
            if ((!xbus_sub_is_active_locked(sub))
                || (!xbus_sub_matches_topic_locked(sub, topic))) {
                continue;
            }
            if (xbus_bind_find_locked(sub, topic) != MR_NULL) {
                continue;
            }
            if (mr_xbus_sub_get(sub) != MR_NULL) {
                target_sub = sub;
                break;
            }
        }

        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);

        if (!target_sub) {
            break;
        }

        /* Allocate binding outside lock */
        new_bind = mr_malloc(sizeof(xbus_bind_t));
        if (!new_bind) {
            mr_xbus_sub_put(target_sub);
            xbus_bind_free_list(&free_list);
            xbus_notify_list_flush(&nlist);
            return -MR_ENOMEM;
        }

        /* Lock */
        mask = mr_spinlock_lock_irqsave(&__xbus_lock);

        /* Re-validate and attach binding */
        if (((topic->flags & XBUS_TOPIC_F_REGISTERED) != 0U)
            && xbus_sub_is_active_locked(target_sub)
            && xbus_sub_matches_topic_locked(target_sub, topic)
            && (xbus_bind_find_locked(target_sub, topic) == MR_NULL)) {
            xbus_bind_attach_locked(new_bind, topic, target_sub, &nlist);
            new_bind = MR_NULL;
        }

        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);

        /* Free unused binding and release subscription reference */
        if (new_bind) {
            mr_free(new_bind);
        }
        mr_xbus_sub_put(target_sub);
    }

    /* Free detached bindings and flush notifications */
    xbus_bind_free_list(&free_list);
    xbus_notify_list_flush(&nlist);
    return 0;
}

static mr_err_t xbus_sub_sync_topics(mr_xbus_sub_t *sub) {
    mr_xbus_topic_t *topic, *target_topic;
    xbus_notify_list_t nlist;
    xbus_bind_t *new_bind;
    mr_uint32_t hash, b;
    mr_list_t *pos;
    int mask;

    /* Init notify collector and hash for exact patterns */
    xbus_notify_list_init(&nlist);
    hash = ((sub->flags & XBUS_SUB_F_WILDCARD) == 0U)
             ? xbus_str_hash(sub->pattern, sub->pattern_len)
             : 0U;

    for (;;) {
        target_topic = MR_NULL;

        /* Lock */
        mask = mr_spinlock_lock_irqsave(&__xbus_lock);

        /* Stop if subscription or domain is no longer active */
        if (!xbus_sub_is_active_locked(sub)) {
            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
            break;
        }

        /* Find next unbound matching topic */
        if ((sub->flags & XBUS_SUB_F_WILDCARD) == 0U) {
            topic = xbus_topic_find_locked(sub->pattern, sub->pattern_len,
                                           hash);
            if (topic && ((topic->flags & XBUS_TOPIC_F_REGISTERED) != 0U)
                && xbus_sub_matches_topic_locked(sub, topic)
                && (xbus_bind_find_locked(sub, topic) == MR_NULL)
                && (mr_xbus_topic_get(topic) != MR_NULL)) {
                target_topic = topic;
            }
        } else {
            for (b = 0U; (b < MR_CFG_XBUS_TOPIC_BUCKETS) && (!target_topic);
                 b++) {
                MR_LIST_FOR_EACH(pos, &__xbus_topic_buckets[b]) {
                    topic = MR_LIST_ENTRY(pos, mr_xbus_topic_t, hash_node);
                    if (((topic->flags & XBUS_TOPIC_F_REGISTERED) == 0U)
                        || (!xbus_sub_matches_topic_locked(sub, topic))) {
                        continue;
                    }
                    if (xbus_bind_find_locked(sub, topic) != MR_NULL) {
                        continue;
                    }
                    if (mr_xbus_topic_get(topic) != MR_NULL) {
                        target_topic = topic;
                        break;
                    }
                }
            }
        }

        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);

        if (!target_topic) {
            break;
        }

        /* Allocate binding outside lock */
        new_bind = mr_malloc(sizeof(xbus_bind_t));
        if (!new_bind) {
            mr_xbus_topic_put(target_topic);
            xbus_notify_list_flush(&nlist);
            return -MR_ENOMEM;
        }

        /* Lock */
        mask = mr_spinlock_lock_irqsave(&__xbus_lock);

        /* Re-validate and attach binding */
        if (xbus_sub_is_active_locked(sub)
            && ((target_topic->flags & XBUS_TOPIC_F_REGISTERED) != 0U)
            && xbus_sub_matches_topic_locked(sub, target_topic)
            && (xbus_bind_find_locked(sub, target_topic) == MR_NULL)) {
            xbus_bind_attach_locked(new_bind, target_topic, sub, &nlist);
            new_bind = MR_NULL;
        }

        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);

        /* Free unused binding and release topic reference */
        if (new_bind) {
            mr_free(new_bind);
        }
        mr_xbus_topic_put(target_topic);

        if ((sub->flags & XBUS_SUB_F_WILDCARD) == 0U) {
            break;
        }
    }

    /* Flush notifications */
    xbus_notify_list_flush(&nlist);
    return 0;
}

static void xbus_sub_queue_drain_locked(mr_xbus_sub_t *sub,
                                        mr_xbus_topic_t **put_buf,
                                        mr_size_t put_cap,
                                        mr_size_t *put_count) {
    xbus_slot_t *slot;

    /* Pop queued slots up to put_cap topic references */
    while (sub->u.queue.count > 0U) {
        slot = xbus_sub_slot(sub, sub->u.queue.head);
        if (slot->topic) {
            if (*put_count < put_cap) {
                put_buf[(*put_count)++] = slot->topic;
            } else {
                break;
            }
            slot->topic = MR_NULL;
        }
        slot->len = 0U;
        sub->u.queue.head
            = (mr_uint16_t)((sub->u.queue.head + 1U) % sub->u.queue.depth);
        sub->u.queue.count--;
    }

    /* Reset ring indices when queue is empty */
    if (sub->u.queue.count == 0U) {
        sub->u.queue.head = 0U;
        sub->u.queue.tail = 0U;
        sub->u.queue.pending_dropped = 0U;
    }
}

static void xbus_sub_queue_clear(mr_xbus_sub_t *sub) {
    mr_xbus_topic_t *puts[XBUS_TOPIC_REF_STACK_CAP];
    mr_size_t count, i;
    int mask;

    /* Drain queue in batches so topic references are released outside lock */
    for (;;) {
        count = 0U;

        /* Lock */
        mask = mr_spinlock_lock_irqsave(&__xbus_lock);

        if (((sub->flags & XBUS_SUB_F_QUEUE) == 0U) || (!sub->u.queue.slots)
            || (sub->u.queue.count == 0U)) {
            if ((sub->flags & XBUS_SUB_F_QUEUE) != 0U) {
                sub->u.queue.pending_dropped = 0U;
            }
            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
            break;
        }
        xbus_sub_queue_drain_locked(sub, puts, XBUS_TOPIC_REF_STACK_CAP,
                                    &count);

        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);

        /* Release topic references outside lock */
        for (i = 0U; i < count; i++) {
            mr_xbus_topic_put(puts[i]);
        }
    }
}

mr_xbus_domain_t *mr_xbus_domain_default(void) {
    /* Ensure bus is initialized */
    xbus_ensure_init();
    return &__xbus_host_domain;
}

#if !defined(MR_USE_MODULE_XBUS_MULTIUSER)
mr_xbus_domain_t *mr_xbus_domain_current(void) {
    /* Ensure bus is initialized */
    xbus_ensure_init();
    return &__xbus_host_domain;
}
#endif /* !defined(MR_USE_MODULE_XBUS_MULTIUSER) */

static mr_err_t xbus_domain_init_impl(mr_xbus_domain_t *domain,
                                      const mr_clazz_t *clazz,
                                      const char *name) {
    mr_size_t len;
    mr_uint8_t id;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    if (!xbus_domain_name_valid(name, &len)) {
        return -MR_EINVAL;
    }

    /* Ensure bus is initialized */
    xbus_ensure_init();

    /* Init domain storage */
    mr_memset(domain, 0, sizeof(*domain));
    ret = xbus_str_alloc(&domain->name, &domain->name_len,
                         XBUS_INAME_PTR(domain), XBUS_INAME_CAP(domain), name,
                         len);
    if (ret != 0) {
        return ret;
    }
    mr_list_init(&domain->node);
    mr_list_init(&domain->subs);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xbus_lock);

    /* Reject duplicate domain name */
    if (xbus_domain_find_locked(domain->name, domain->name_len) != MR_NULL) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
        xbus_str_free(&domain->name, &domain->name_len, XBUS_INAME_PTR(domain));
        return -MR_EEXIST;
    }

    /* Allocate lowest free domain ID */
    for (id = 0U; id < MR_CFG_XBUS_DOMAIN_MAX; id++) {
        if ((__xbus_domain_bitmap & ((mr_xbus_acl_t)1U << id)) == 0U) {
            break;
        }
    }
    if (id >= MR_CFG_XBUS_DOMAIN_MAX) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
        xbus_str_free(&domain->name, &domain->name_len, XBUS_INAME_PTR(domain));
        return -MR_ENOMEM;
    }

    /* Init object before publishing to registry */
    ret = mr_object_init(&domain->parent, clazz);
    if (ret != 0) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
        xbus_str_free(&domain->name, &domain->name_len, XBUS_INAME_PTR(domain));
        return ret;
    }

    /* Register domain */
    domain->id = id;
    domain->flags = XBUS_DOMAIN_F_REGISTERED;
    __xbus_domain_bitmap |= (mr_xbus_acl_t)(1U << id);
    mr_list_add(&__xbus_domain_list, &domain->node);

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
    return 0;
}

mr_err_t mr_xbus_domain_init(mr_xbus_domain_t *domain, const char *name) {
    const mr_clazz_t *clazz = MR_CLAZZES_STATIK(xbus_domain);

    /* Check parameter */
    MR_ASSERT(domain != MR_NULL);
    MR_ASSERT(name != MR_NULL);

    /* Init domain */
    return xbus_domain_init_impl(domain, clazz, name);
}

mr_xbus_domain_t *mr_xbus_domain_create(const char *name) {
    const mr_clazz_t *clazz = MR_CLAZZES_DYNAMIC(xbus_domain);
    mr_xbus_domain_t *domain;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(name != MR_NULL);

    /* Create domain */
    domain = mr_malloc(sizeof(mr_xbus_domain_t));
    if (!domain) {
        return MR_NULL;
    }

    /* Init domain */
    ret = xbus_domain_init_impl(domain, clazz, name);
    if (ret != 0) {
        mr_free(domain);
        return MR_NULL;
    }
    return domain;
}

static mr_err_t xbus_domain_reset_impl(mr_xbus_domain_t *domain,
                                       mr_bool_t allow_closing) {
    mr_xbus_sub_t *sub, *target_sub;
    mr_list_t *pos, *next, *bpos;
    xbus_notify_list_t nlist;
    mr_xbus_topic_t *topic;
    mr_list_t free_list;
    mr_uint8_t valid_mask;
    xbus_bind_t *bind;
    mr_size_t need;
    mr_uint32_t b;
    int mask;

    valid_mask = allow_closing
                   ? (mr_uint8_t)(XBUS_DOMAIN_F_REGISTERED
                                  | XBUS_DOMAIN_F_CLOSING)
                   : (mr_uint8_t)XBUS_DOMAIN_F_REGISTERED;

    /* Detach and drain all subscriptions owned by this domain */
    for (;;) {
        target_sub = MR_NULL;
        mr_list_init(&free_list);

        /* Lock */
        mask = mr_spinlock_lock_irqsave(&__xbus_lock);

        /* Validate domain registration state */
        if ((domain->flags & valid_mask) == 0U) {
            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
            return -MR_ENOENT;
        }

        MR_LIST_FOR_EACH(pos, &domain->subs) {
            sub = MR_LIST_ENTRY(pos, mr_xbus_sub_t, domain_node);
            if ((sub->flags & XBUS_SUB_F_DETACHED) == 0U) {
                sub->flags |= XBUS_SUB_F_DETACHED;
                MR_LIST_FOR_EACH_SAFE(bpos, next, &sub->binds) {
                    bind = MR_LIST_ENTRY(bpos, xbus_bind_t, sub_node);
                    xbus_bind_detach_locked(bind, &free_list);
                }
                if (mr_xbus_sub_get(sub) != MR_NULL) {
                    target_sub = sub;
                }
                break;
            }
        }

        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);

        xbus_bind_free_list(&free_list);
        if (!target_sub) {
            break;
        }
        xbus_sub_queue_clear(target_sub);
        mr_xbus_sub_put(target_sub);
    }

    /* Size notify collector for remaining STATE subscribers */
    xbus_notify_list_init(&nlist);
    for (;;) {
        need = 0U;

        /* Lock */
        mask = mr_spinlock_lock_irqsave(&__xbus_lock);

        /* Re-validate domain registration state before marking stale */
        if ((domain->flags & valid_mask) == 0U) {
            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
            if (nlist.items != nlist.stack) {
                mr_free(nlist.items);
            }
            return -MR_ENOENT;
        }

        for (b = 0U; b < MR_CFG_XBUS_TOPIC_BUCKETS; b++) {
            MR_LIST_FOR_EACH(pos, &__xbus_topic_buckets[b]) {
                topic = MR_LIST_ENTRY(pos, mr_xbus_topic_t, hash_node);
                if (((topic->flags & XBUS_TOPIC_F_REGISTERED) == 0U)
                    || (topic->seq == 0U) || (topic->owner != domain->id)) {
                    continue;
                }
                MR_LIST_FOR_EACH(bpos, &topic->binds) {
                    need++;
                }
            }
        }
        if (need <= nlist.cap) {
            break;
        }

        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);

        if (nlist.items != nlist.stack) {
            mr_free(nlist.items);
            nlist.items = nlist.stack;
            nlist.cap = XBUS_NOTIFY_STACK_CAP;
        }
        nlist.items = mr_malloc(need * sizeof(xbus_notify_item_t));
        if (!nlist.items) {
            nlist.items = nlist.stack;
            /* Fall back to stack capacity on OOM so reset still completes */
            mask = mr_spinlock_lock_irqsave(&__xbus_lock);
            if ((domain->flags & valid_mask) == 0U) {
                /* Unlock */
                mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
                return -MR_ENOENT;
            }
            break;
        }
        nlist.cap = need;
    }

    /* Mark topics last published by this domain as stale */
    for (b = 0U; b < MR_CFG_XBUS_TOPIC_BUCKETS; b++) {
        MR_LIST_FOR_EACH(pos, &__xbus_topic_buckets[b]) {
            topic = MR_LIST_ENTRY(pos, mr_xbus_topic_t, hash_node);
            if (((topic->flags & XBUS_TOPIC_F_REGISTERED) == 0U)
                || (topic->seq == 0U) || (topic->owner != domain->id)) {
                continue;
            }
            topic->flags |= XBUS_TOPIC_F_STALE;

            /* Notify remaining STATE subscribers of staleness */
            MR_LIST_FOR_EACH(bpos, &topic->binds) {
                bind = MR_LIST_ENTRY(bpos, xbus_bind_t, topic_node);
                sub = bind->sub;
                if ((!xbus_sub_is_active_locked(sub))
                    || ((sub->flags & XBUS_SUB_F_QUEUE) != 0U)) {
                    continue;
                }
                if ((bind->flags & XBUS_BIND_F_DIRTY) == 0U) {
                    bind->flags |= XBUS_BIND_F_DIRTY;
                    mr_list_add(&sub->u.state.dirty_list, &bind->dirty_node);
                    sub->u.state.dirty_count++;
                }
                xbus_notify_list_push_locked(&nlist, sub);
            }
        }
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);

    /* Trace domain reset */
    MR_TRACE_EVENT("reset", domain->id);

    /* Flush notifications outside lock */
    xbus_notify_list_flush(&nlist);
    return 0;
}

mr_err_t mr_xbus_domain_del(mr_xbus_domain_t *domain) {
    mr_xbus_topic_t *topic;
    mr_xbus_acl_t bit;
    mr_uint32_t b;
    mr_list_t *pos;
    int mask;

    /* Check parameter */
    MR_ASSERT((domain != MR_NULL) && MR_OBJECT_IS_INITED(domain));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(domain, xbus_domain));

    /* Built-in host domain cannot be deleted */
    if (domain == &__xbus_host_domain) {
        return -MR_EPERM;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xbus_lock);

    /* Unregister domain first while retaining ID bit until cleanup completes */
    if ((domain->flags & XBUS_DOMAIN_F_REGISTERED) == 0U) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
        return -MR_ENOENT;
    }
    mr_list_del(&domain->node);
    domain->flags &= (mr_uint8_t)(~XBUS_DOMAIN_F_REGISTERED);
    domain->flags |= XBUS_DOMAIN_F_CLOSING;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);

    /* Reset existing subscriptions and mark owned topics stale */
    xbus_domain_reset_impl(domain, MR_TRUE);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xbus_lock);

    /* Scrub domain ACL bits and release domain ID for reuse */
    bit = MR_XBUS_ACL_DOMAIN(domain);
    for (b = 0U; b < MR_CFG_XBUS_TOPIC_BUCKETS; b++) {
        MR_LIST_FOR_EACH(pos, &__xbus_topic_buckets[b]) {
            topic = MR_LIST_ENTRY(pos, mr_xbus_topic_t, hash_node);
            if (topic->read_mask != MR_XBUS_ACL_ALL) {
                topic->read_mask &= (mr_xbus_acl_t)(~bit);
            }
            if (topic->write_mask != MR_XBUS_ACL_ALL) {
                topic->write_mask &= (mr_xbus_acl_t)(~bit);
            }
        }
    }
    domain->flags &= (mr_uint8_t)(~XBUS_DOMAIN_F_CLOSING);
    __xbus_domain_bitmap &= (mr_xbus_acl_t)(~bit);

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);

    /* Delete object */
    mr_object_del(&domain->parent);
    return 0;
}

mr_err_t mr_xbus_domain_reset(mr_xbus_domain_t *domain) {
    /* Ensure bus is initialized */
    xbus_ensure_init();

    /* Resolve domain */
    domain = xbus_domain_resolve(domain);
    if (!domain) {
        return -MR_EINVAL;
    }
    MR_ASSERT(MR_OBJECT_IS_INITED(domain));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(domain, xbus_domain));

    /* Reset active domain */
    return xbus_domain_reset_impl(domain, MR_FALSE);
}

mr_xbus_domain_t *mr_xbus_domain_find(const char *name) {
    mr_xbus_domain_t *dom;
    mr_size_t len;
    int mask;

    /* Check parameter */
    if (!xbus_domain_name_valid(name, &len)) {
        return MR_NULL;
    }

    /* Ensure bus is initialized */
    xbus_ensure_init();

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xbus_lock);

    /* Find domain and acquire reference */
    dom = xbus_domain_find_locked(name, (mr_uint16_t)len);
    if (dom) {
        dom = mr_xbus_domain_get(dom);
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
    return dom;
}

static mr_xbus_domain_t *xbus_domain_obj_release(mr_object_t *obj) {
    mr_xbus_domain_t *domain;

    /* Get domain */
    domain = MR_CONTAINER_OF(obj, mr_xbus_domain_t, parent);

    /* Free domain name */
    xbus_str_free(&domain->name, &domain->name_len, XBUS_INAME_PTR(domain));
    return domain;
}

static void xbus_domain_statik_release(mr_object_t *obj) {
    /* Release domain */
    xbus_domain_obj_release(obj);
}

static void xbus_domain_dynamic_release(mr_object_t *obj) {
    mr_xbus_domain_t *domain;

    /* Release domain */
    domain = xbus_domain_obj_release(obj);

    /* Free domain */
    mr_free(domain);
}

static void xbus_topic_default_acl_locked(const mr_xbus_name_t *name,
                                          mr_xbus_acl_t *read_mask,
                                          mr_xbus_acl_t *write_mask) {
    mr_xbus_domain_t *dom;
    const char *seg, *end;
    mr_uint16_t len;

    /* Extract top-level namespace segment */
    seg = name->str + 1;
    end = seg;
    while ((*end != '\0') && (*end != '/')) {
        end++;
    }
    len = (mr_uint16_t)(end - seg);

    /* Derive ACL from matching domain prefix or shared namespace */
    dom = xbus_domain_find_locked(seg, len);
    if (dom) {
        if (dom->id == MR_XBUS_DOMAIN_HOST_ID) {
            *write_mask = (mr_xbus_acl_t)(1U << MR_XBUS_DOMAIN_HOST_ID);
            *read_mask = MR_XBUS_ACL_ALL;
        } else {
            *write_mask = MR_XBUS_ACL_DOMAIN(dom);
            *read_mask = MR_XBUS_ACL_DOMAIN(dom);
        }
        return;
    }

    /* Default shared namespace ACL */
    *read_mask = MR_XBUS_ACL_ALL;
    *write_mask = MR_XBUS_ACL_ALL;
}

static mr_err_t xbus_topic_init_impl(mr_xbus_topic_t *topic,
                                     const mr_clazz_t *clazz,
                                     mr_xbus_domain_t *domain, const char *path,
                                     const mr_xbus_topic_cfg_t *cfg) {
    mr_xbus_acl_t read_mask, write_mask;
    mr_uint16_t max_size, type_id;
    mr_uint8_t *pre_buf;
    mr_uint8_t cfg_flags;
    mr_err_t ret;
    int mask;

    /* Ensure bus is initialized */
    xbus_ensure_init();

    /* Resolve domain */
    domain = xbus_domain_resolve(domain);
    if (!domain) {
        return -MR_EINVAL;
    }
    MR_ASSERT(MR_OBJECT_IS_INITED(domain));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(domain, xbus_domain));

    /* Load topic configuration */
    max_size = 0U;
    type_id = 0U;
    cfg_flags = 0U;
    read_mask = MR_XBUS_ACL_NONE;
    write_mask = MR_XBUS_ACL_NONE;
    pre_buf = MR_NULL;
    if (cfg) {
        max_size = cfg->max_size;
        type_id = cfg->type_id;
        cfg_flags = (mr_uint8_t)(cfg->flags & XBUS_TOPIC_F_FIXED);
        read_mask = cfg->read_mask;
        write_mask = cfg->write_mask;
    }

    /* Pre-allocate payload buffer outside lock */
    if (max_size > MR_CFG_XBUS_INLINE_DATA_LEN) {
        pre_buf = mr_malloc(max_size);
        if (!pre_buf) {
            return -MR_ENOMEM;
        }
    }

    /* Init topic storage */
    mr_memset(topic, 0, sizeof(*topic));
    ret = xbus_path_resolve(domain, path, MR_FALSE, &topic->name.str,
                            &topic->name.len, &topic->name.hash,
                            XBUS_INAME_PTR(topic), XBUS_INAME_CAP(topic),
                            MR_NULL);
    if (ret != 0) {
        if (pre_buf) {
            mr_free(pre_buf);
        }
        return ret;
    }
    xbus_buf_init(&topic->buf);
    if (pre_buf) {
        topic->buf.data = pre_buf;
        topic->buf.cap = max_size;
    }
    mr_list_init(&topic->hash_node);
    mr_list_init(&topic->binds);
    topic->max_size = max_size;
    topic->type_id = type_id;
    topic->flags = cfg_flags;
    topic->owner = domain->id;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xbus_lock);

    /* Ensure owning domain is still registered */
    if ((domain->flags & XBUS_DOMAIN_F_REGISTERED) == 0U) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
        xbus_buf_free(&topic->buf);
        xbus_str_free(&topic->name.str, &topic->name.len,
                      XBUS_INAME_PTR(topic));
        return -MR_ENOENT;
    }

    /* Reject duplicate topic name */
    if (xbus_topic_find_locked(topic->name.str, topic->name.len,
                               topic->name.hash)
        != MR_NULL) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
        xbus_buf_free(&topic->buf);
        xbus_str_free(&topic->name.str, &topic->name.len,
                      XBUS_INAME_PTR(topic));
        return -MR_EEXIST;
    }

    /* Apply explicit or prefix-derived ACL */
    if ((read_mask == MR_XBUS_ACL_NONE) && (write_mask == MR_XBUS_ACL_NONE)) {
        xbus_topic_default_acl_locked(&topic->name, &read_mask, &write_mask);
    }
    topic->read_mask = read_mask;
    topic->write_mask = write_mask;

    /* Init object before exposing in registry */
    ret = mr_object_init(&topic->parent, clazz);
    if (ret != 0) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
        xbus_buf_free(&topic->buf);
        xbus_str_free(&topic->name.str, &topic->name.len,
                      XBUS_INAME_PTR(topic));
        return ret;
    }

    /* Register topic */
    mr_list_add(xbus_topic_bucket(topic->name.hash), &topic->hash_node);
    topic->flags |= XBUS_TOPIC_F_REGISTERED;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
    return 0;
}

mr_err_t mr_xbus_topic_init(mr_xbus_topic_t *topic, mr_xbus_domain_t *domain,
                            const char *path, const mr_xbus_topic_cfg_t *cfg) {
    const mr_clazz_t *clazz = MR_CLAZZES_STATIK(xbus_topic);
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(topic != MR_NULL);
    MR_ASSERT(path != MR_NULL);

    /* Init topic */
    ret = xbus_topic_init_impl(topic, clazz, domain, path, cfg);
    if (ret != 0) {
        return ret;
    }

    /* Bind matching subscriptions */
    ret = xbus_topic_sync_subs(topic);
    if (ret != 0) {
        mr_xbus_topic_del(topic);
        return ret;
    }
    return 0;
}

mr_xbus_topic_t *mr_xbus_topic_create(mr_xbus_domain_t *domain,
                                      const char *path,
                                      const mr_xbus_topic_cfg_t *cfg) {
    const mr_clazz_t *clazz = MR_CLAZZES_DYNAMIC(xbus_topic);
    mr_xbus_topic_t *topic;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(path != MR_NULL);

    /* Create topic */
    topic = mr_malloc(sizeof(mr_xbus_topic_t));
    if (!topic) {
        return MR_NULL;
    }

    /* Init topic */
    ret = xbus_topic_init_impl(topic, clazz, domain, path, cfg);
    if (ret != 0) {
        mr_free(topic);
        return MR_NULL;
    }

    /* Bind matching subscriptions */
    ret = xbus_topic_sync_subs(topic);
    if (ret != 0) {
        /* Dynamic release frees topic; do not call mr_free() again */
        mr_xbus_topic_del(topic);
        return MR_NULL;
    }
    return topic;
}

mr_err_t mr_xbus_topic_del(mr_xbus_topic_t *topic) {
    mr_list_t *pos, *next;
    mr_list_t free_list;
    xbus_bind_t *bind;
    int mask;

    /* Check parameter */
    MR_ASSERT((topic != MR_NULL) && MR_OBJECT_IS_INITED(topic));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(topic, xbus_topic));

    mr_list_init(&free_list);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xbus_lock);

    /* Unregister topic from bucket */
    if ((topic->flags & XBUS_TOPIC_F_REGISTERED) == 0U) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
        return -MR_ENOENT;
    }
    mr_list_del(&topic->hash_node);
    topic->flags &= (mr_uint8_t)(~XBUS_TOPIC_F_REGISTERED);

    /* Detach all subscriber bindings */
    MR_LIST_FOR_EACH_SAFE(pos, next, &topic->binds) {
        bind = MR_LIST_ENTRY(pos, xbus_bind_t, topic_node);
        xbus_bind_detach_locked(bind, &free_list);
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);

    /* Free bindings outside lock */
    xbus_bind_free_list(&free_list);

    /* Delete object */
    mr_object_del(&topic->parent);
    return 0;
}

mr_xbus_topic_t *mr_xbus_topic_find(mr_xbus_domain_t *domain,
                                    const char *path) {
    char scratch[XBUS_FIND_SCRATCH_LEN];
    mr_xbus_name_t resolved;
    mr_xbus_topic_t *topic;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    if (!path) {
        return MR_NULL;
    }

    /* Ensure bus is initialized */
    xbus_ensure_init();

    /* Resolve domain */
    domain = xbus_domain_resolve(domain);
    if (domain != MR_NULL) {
        MR_ASSERT(MR_OBJECT_IS_INITED(domain));
        MR_ASSERT(MR_OBJECT_IS_CLAZZES(domain, xbus_domain));
    }

    /* Resolve relative or absolute path */
    ret = xbus_path_resolve(domain, path, MR_FALSE, &resolved.str,
                            &resolved.len, &resolved.hash, scratch,
                            sizeof(scratch), MR_NULL);
    if (ret != 0) {
        return MR_NULL;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xbus_lock);

    /* Find topic and acquire reference */
    topic = xbus_topic_find_locked(resolved.str, resolved.len, resolved.hash);
    if (topic) {
        topic = mr_xbus_topic_get(topic);
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);

    /* Free temporary resolved path */
    xbus_str_free(&resolved.str, &resolved.len, scratch);
    return topic;
}

mr_err_t mr_xbus_topic_acl_set(mr_xbus_topic_t *topic, mr_xbus_acl_t read_mask,
                               mr_xbus_acl_t write_mask) {
    int mask;

    /* Check parameter */
    MR_ASSERT((topic != MR_NULL) && MR_OBJECT_IS_INITED(topic));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(topic, xbus_topic));

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xbus_lock);

    /* Update ACL masks */
    topic->read_mask = read_mask;
    topic->write_mask = write_mask;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);

    /* Re-sync bindings against updated ACL */
    return xbus_topic_sync_subs(topic);
}

mr_err_t mr_xbus_topic_acl_grant(mr_xbus_topic_t *topic,
                                 mr_xbus_domain_t *domain, mr_bool_t read,
                                 mr_bool_t write) {
    mr_xbus_acl_t bit;
    int mask;

    /* Check parameter */
    MR_ASSERT((topic != MR_NULL) && MR_OBJECT_IS_INITED(topic));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(topic, xbus_topic));

    /* Ensure bus is initialized */
    xbus_ensure_init();

    /* Resolve domain */
    domain = xbus_domain_resolve(domain);
    if (!domain) {
        return -MR_EINVAL;
    }
    MR_ASSERT(MR_OBJECT_IS_INITED(domain));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(domain, xbus_domain));

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xbus_lock);

    /* Validate topic and domain registration */
    if (((topic->flags & XBUS_TOPIC_F_REGISTERED) == 0U)
        || ((domain->flags & XBUS_DOMAIN_F_REGISTERED) == 0U)) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
        return -MR_ENOENT;
    }

    /* Grant requested ACL bits */
    bit = MR_XBUS_ACL_DOMAIN(domain);
    if (read) {
        topic->read_mask |= bit;
    }
    if (write) {
        topic->write_mask |= bit;
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);

    /* Re-sync bindings if read permission changed */
    return read ? xbus_topic_sync_subs(topic) : 0;
}

mr_err_t mr_xbus_topic_acl_revoke(mr_xbus_topic_t *topic,
                                  mr_xbus_domain_t *domain, mr_bool_t read,
                                  mr_bool_t write) {
    mr_xbus_acl_t bit;
    int mask;

    /* Check parameter */
    MR_ASSERT((topic != MR_NULL) && MR_OBJECT_IS_INITED(topic));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(topic, xbus_topic));

    /* Ensure bus is initialized */
    xbus_ensure_init();

    /* Resolve domain */
    domain = xbus_domain_resolve(domain);
    if (!domain) {
        return -MR_EINVAL;
    }
    MR_ASSERT(MR_OBJECT_IS_INITED(domain));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(domain, xbus_domain));

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xbus_lock);

    /* Validate topic and domain registration */
    if (((topic->flags & XBUS_TOPIC_F_REGISTERED) == 0U)
        || ((domain->flags & XBUS_DOMAIN_F_REGISTERED) == 0U)) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
        return -MR_ENOENT;
    }

    /* Revoke requested ACL bits */
    bit = MR_XBUS_ACL_DOMAIN(domain);
    if (read) {
        topic->read_mask &= (mr_xbus_acl_t)(~bit);
    }
    if (write) {
        topic->write_mask &= (mr_xbus_acl_t)(~bit);
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);

    /* Re-sync bindings if read permission changed */
    return read ? xbus_topic_sync_subs(topic) : 0;
}

static mr_xbus_sub_t *xbus_find_queue_grow_locked(mr_xbus_topic_t *topic,
                                                  mr_uint16_t len,
                                                  mr_uint16_t *slot_idx) {
    mr_xbus_sub_t *sub;
    xbus_vbuf_t *vbuf;
    xbus_bind_t *bind;
    mr_uint16_t idx;
    mr_list_t *pos;

    /* Find bound variable-length QUEUE subscription needing larger tail slot */
    MR_LIST_FOR_EACH(pos, &topic->binds) {
        bind = MR_LIST_ENTRY(pos, xbus_bind_t, topic_node);
        sub = bind->sub;
        if ((!xbus_sub_is_active_locked(sub))
            || ((sub->flags & XBUS_SUB_F_QUEUE) == 0U)
            || (sub->u.queue.msg_size > 0U)) {
            continue;
        }
        if ((topic->read_mask & MR_XBUS_ACL_DOMAIN(sub->domain)) == 0U) {
            continue;
        }
        if ((sub->u.queue.count == sub->u.queue.depth)
            && ((sub->flags & MR_XBUS_SUB_OVERWRITE) == 0U)) {
            continue;
        }

        idx = sub->u.queue.tail;
        vbuf = xbus_sub_vbuf(sub, idx);
        if ((vbuf->cap < len) && (mr_xbus_sub_get(sub) != MR_NULL)) {
            *slot_idx = idx;
            return sub;
        }
    }
    return MR_NULL;
}

mr_err_t mr_xbus_publish(mr_xbus_domain_t *domain, mr_xbus_topic_t *topic,
                         const void *data, mr_size_t len) {
    mr_xbus_topic_t *overwritten_stack[XBUS_TOPIC_REF_STACK_CAP];
    mr_size_t overwritten_cap, overwritten_cnt, need_cap;
    mr_uint8_t *old_buf, *new_buf, *slot_dst;
    mr_uint16_t len16, grow_idx, store_len;
    mr_xbus_topic_t **overwritten;
    mr_xbus_sub_t *grow_sub, *sub;
    xbus_notify_list_t nlist;
    xbus_vbuf_t *vbuf;
    xbus_bind_t *bind;
    xbus_slot_t *slot;
    mr_list_t *pos;
    mr_tick_t now;
    int mask;

    /* Check parameter */
    MR_ASSERT((topic != MR_NULL) && MR_OBJECT_IS_INITED(topic));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(topic, xbus_topic));
    if ((len > 0xffffU) || ((len > 0U) && (!data))) {
        return -MR_EINVAL;
    }

    /* Ensure bus is initialized */
    xbus_ensure_init();

    /* Resolve domain */
    domain = xbus_domain_resolve(domain);
    if (!domain) {
        return -MR_EINVAL;
    }
    MR_ASSERT(MR_OBJECT_IS_INITED(domain));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(domain, xbus_domain));

    /* Validate payload size */
    len16 = (mr_uint16_t)len;
    if (((topic->flags & XBUS_TOPIC_F_FIXED) != 0U)
        && (len16 != topic->max_size)) {
        return -MR_EINVAL;
    }
    if ((topic->max_size > 0U) && (len16 > topic->max_size)) {
        return -MR_EINVAL;
    }

    /* Init publish collectors */
    overwritten = overwritten_stack;
    overwritten_cap = XBUS_TOPIC_REF_STACK_CAP;
    overwritten_cnt = 0U;
    old_buf = MR_NULL;
    new_buf = MR_NULL;
    grow_idx = 0U;
    xbus_notify_list_init(&nlist);

    for (;;) {
        /* Lock */
        mask = mr_spinlock_lock_irqsave(&__xbus_lock);

        /* Check topic and domain registration */
        if (((topic->flags & XBUS_TOPIC_F_REGISTERED) == 0U)
            || ((domain->flags & XBUS_DOMAIN_F_REGISTERED) == 0U)) {
            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
            if (nlist.items != nlist.stack) {
                mr_free(nlist.items);
            }
            if (overwritten != overwritten_stack) {
                mr_free(overwritten);
            }
            return -MR_ENOENT;
        }

        /* Enforce write ACL */
        if ((topic->write_mask & MR_XBUS_ACL_DOMAIN(domain)) == 0U) {
            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
            if (nlist.items != nlist.stack) {
                mr_free(nlist.items);
            }
            if (overwritten != overwritten_stack) {
                mr_free(overwritten);
            }
            return -MR_EPERM;
        }

        /* Ensure notify and overwritten collectors fit all bound subs */
        need_cap = 0U;
        MR_LIST_FOR_EACH(pos, &topic->binds) {
            need_cap++;
        }
        if (need_cap > nlist.cap) {
            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);

            if (nlist.items != nlist.stack) {
                mr_free(nlist.items);
                nlist.items = nlist.stack;
                nlist.cap = XBUS_NOTIFY_STACK_CAP;
            }
            nlist.items = mr_malloc(need_cap * sizeof(xbus_notify_item_t));
            if (!nlist.items) {
                nlist.items = nlist.stack;
                if (overwritten != overwritten_stack) {
                    mr_free(overwritten);
                }
                return -MR_ENOMEM;
            }
            nlist.cap = need_cap;

            if (need_cap > overwritten_cap) {
                if (overwritten != overwritten_stack) {
                    mr_free(overwritten);
                }
                overwritten = mr_malloc(need_cap * sizeof(mr_xbus_topic_t *));
                if (!overwritten) {
                    mr_free(nlist.items);
                    return -MR_ENOMEM;
                }
                overwritten_cap = need_cap;
            }
            continue;
        }

        /* Grow topic buffer outside lock if payload exceeds capacity */
        if (topic->buf.cap < len16) {
            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);

            new_buf = mr_malloc(len16);
            if (!new_buf) {
                if (nlist.items != nlist.stack) {
                    mr_free(nlist.items);
                }
                if (overwritten != overwritten_stack) {
                    mr_free(overwritten);
                }
                return -MR_ENOMEM;
            }

            /* Lock */
            mask = mr_spinlock_lock_irqsave(&__xbus_lock);
            xbus_buf_install_locked(&topic->buf, new_buf, len16, &old_buf);
            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);

            if (old_buf) {
                mr_free(old_buf);
                old_buf = MR_NULL;
            }
            continue;
        }

        /* Grow variable-length QUEUE tail slot outside lock if needed */
        if (len16 > 0U) {
            grow_sub = xbus_find_queue_grow_locked(topic, len16, &grow_idx);
            if (grow_sub != MR_NULL) {
                /* Unlock */
                mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);

                new_buf = mr_malloc(len16);
                if (!new_buf) {
                    mr_xbus_sub_put(grow_sub);
                    if (nlist.items != nlist.stack) {
                        mr_free(nlist.items);
                    }
                    if (overwritten != overwritten_stack) {
                        mr_free(overwritten);
                    }
                    return -MR_ENOMEM;
                }

                /* Lock */
                mask = mr_spinlock_lock_irqsave(&__xbus_lock);
                vbuf = xbus_sub_vbuf(grow_sub, grow_idx);
                if (len16 > vbuf->cap) {
                    slot = xbus_sub_slot(grow_sub, grow_idx);
                    if ((slot->topic != MR_NULL) && (slot->len > 0U)
                        && vbuf->data) {
                        mr_memcpy(new_buf, vbuf->data,
                                  MR_MIN(slot->len, vbuf->cap));
                    }
                    old_buf = vbuf->data;
                    vbuf->data = new_buf;
                    vbuf->cap = len16;
                } else {
                    old_buf = new_buf;
                }
                /* Unlock */
                mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);

                if (old_buf) {
                    mr_free(old_buf);
                    old_buf = MR_NULL;
                }
                mr_xbus_sub_put(grow_sub);
                continue;
            }
        }

        /* Commit payload and metadata under lock */
        now = mr_clock_tick();
        if (len16 > 0U) {
            mr_memcpy(topic->buf.data, data, len16);
        }
        topic->buf.len = len16;
        topic->seq++;
        if (topic->seq == 0U) {
            topic->seq = 1U;
        }
        topic->stamp = now;
        topic->owner = domain->id;
        topic->flags &= (mr_uint8_t)(~XBUS_TOPIC_F_STALE);

        /* Deliver message to bound subscribers */
        MR_LIST_FOR_EACH(pos, &topic->binds) {
            bind = MR_LIST_ENTRY(pos, xbus_bind_t, topic_node);
            sub = bind->sub;
            if (!xbus_sub_is_active_locked(sub)) {
                continue;
            }
            if ((topic->read_mask & MR_XBUS_ACL_DOMAIN(sub->domain)) == 0U) {
                continue;
            }

            if ((sub->flags & XBUS_SUB_F_QUEUE) == 0U) {
                /* STATE: coalesce onto dirty list */
                if ((bind->flags & XBUS_BIND_F_DIRTY) == 0U) {
                    bind->flags |= XBUS_BIND_F_DIRTY;
                    mr_list_add(&sub->u.state.dirty_list, &bind->dirty_node);
                    sub->u.state.dirty_count++;
                } else if (bind->dropped < 0xffffU) {
                    bind->dropped++;
                }
                xbus_notify_list_push_locked(&nlist, sub);
            } else if (sub->u.queue.slots) {
                /* QUEUE: drop or overwrite oldest slot when full */
                if (sub->u.queue.count == sub->u.queue.depth) {
                    if ((sub->flags & MR_XBUS_SUB_OVERWRITE) != 0U) {
                        slot = xbus_sub_slot(sub, sub->u.queue.head);
                        if (slot->topic) {
                            if (overwritten_cnt < overwritten_cap) {
                                overwritten[overwritten_cnt++] = slot->topic;
                            }
                            slot->topic = MR_NULL;
                        }
                        slot->len = 0U;
                        sub->u.queue.head
                            = (mr_uint16_t)((sub->u.queue.head + 1U)
                                            % sub->u.queue.depth);
                        sub->u.queue.count--;
                        if (sub->u.queue.pending_dropped < 0xffffU) {
                            sub->u.queue.pending_dropped++;
                        }
                    } else {
                        if (sub->u.queue.pending_dropped < 0xffffU) {
                            sub->u.queue.pending_dropped++;
                        }
                        continue;
                    }
                }

                /* Enqueue message into tail slot */
                slot = xbus_sub_slot(sub, sub->u.queue.tail);
                slot_dst = xbus_sub_slot_data(sub, sub->u.queue.tail);
                slot->flags = 0U;
                store_len = len16;
                if ((sub->u.queue.msg_size > 0U)
                    && (store_len > sub->u.queue.msg_size)) {
                    store_len = sub->u.queue.msg_size;
                    slot->flags |= MR_XBUS_FLAG_TRUNCATED;
                }
                if (store_len > 0U) {
                    mr_memcpy(slot_dst, data, store_len);
                }
                slot->len = store_len;
                slot->topic = mr_xbus_topic_get(topic);
                slot->seq = topic->seq;
                slot->stamp = topic->stamp;
                slot->owner = topic->owner;

                sub->u.queue.tail
                    = (mr_uint16_t)((sub->u.queue.tail + 1U)
                                    % sub->u.queue.depth);
                sub->u.queue.count++;
                xbus_notify_list_push_locked(&nlist, sub);
            }
        }

        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
        break;
    }

    /* Release overwritten topic references outside lock */
    while (overwritten_cnt > 0U) {
        mr_xbus_topic_put(overwritten[--overwritten_cnt]);
    }
    if (overwritten != overwritten_stack) {
        mr_free(overwritten);
    }

    /* Trace topic publish */
    MR_TRACE_EVENT("publish", domain->id, topic, len);

    /* Flush notifications outside lock */
    xbus_notify_list_flush(&nlist);
    return 0;
}

mr_err_t mr_xbus_publish_path(mr_xbus_domain_t *domain, const char *path,
                              const void *data, mr_size_t len) {
    mr_xbus_topic_t *topic;
    mr_err_t ret;

    /* Find topic by path */
    topic = mr_xbus_topic_find(domain, path);
    if (!topic) {
        return -MR_ENOENT;
    }

    /* Publish message and release topic reference */
    ret = mr_xbus_publish(domain, topic, data, len);
    mr_xbus_topic_put(topic);
    return ret;
}

mr_ssize_t mr_xbus_topic_read(mr_xbus_domain_t *domain, mr_xbus_topic_t *topic,
                              void *buf, mr_size_t size, mr_xbus_meta_t *meta) {
    mr_size_t copy_len;
    mr_uint8_t flags;
    int mask;

    /* Check parameter */
    MR_ASSERT((topic != MR_NULL) && MR_OBJECT_IS_INITED(topic));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(topic, xbus_topic));
    if ((size > 0U) && (!buf)) {
        return -MR_EINVAL;
    }

    /* Ensure bus is initialized */
    xbus_ensure_init();

    /* Resolve domain */
    domain = xbus_domain_resolve(domain);
    if (!domain) {
        return -MR_EINVAL;
    }
    MR_ASSERT(MR_OBJECT_IS_INITED(domain));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(domain, xbus_domain));

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xbus_lock);

    /* Validate registration, read ACL, and published state */
    if (((topic->flags & XBUS_TOPIC_F_REGISTERED) == 0U)
        || ((domain->flags & XBUS_DOMAIN_F_REGISTERED) == 0U)) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
        return -MR_ENOENT;
    }
    if ((topic->read_mask & MR_XBUS_ACL_DOMAIN(domain)) == 0U) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
        return -MR_EPERM;
    }
    if (topic->seq == 0U) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
        return -MR_EAGAIN;
    }

    /* Copy latest snapshot and metadata */
    copy_len = MR_MIN(size, (mr_size_t)topic->buf.len);
    if (copy_len > 0U) {
        mr_memcpy(buf, topic->buf.data, copy_len);
    }
    flags = ((topic->flags & XBUS_TOPIC_F_STALE) != 0U) ? MR_XBUS_FLAG_STALE
                                                        : 0U;
    if (size < (mr_size_t)topic->buf.len) {
        flags |= MR_XBUS_FLAG_TRUNCATED;
    }
    if (meta) {
        meta->topic = topic;
        meta->seq = topic->seq;
        meta->stamp = topic->stamp;
        meta->len = topic->buf.len;
        meta->dropped = 0U;
        meta->type_id = topic->type_id;
        meta->owner = topic->owner;
        meta->flags = flags;
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
    return (mr_ssize_t)copy_len;
}

mr_err_t mr_xbus_topic_foreach(mr_xbus_topic_iter_t *iter, void *param) {
    mr_xbus_topic_t *stack_buf[XBUS_TOPIC_REF_STACK_CAP];
    mr_size_t count, cap, i;
    mr_xbus_topic_t **topics;
    mr_xbus_topic_t *topic;
    mr_uint32_t b;
    mr_list_t *pos;
    int ret, mask;

    /* Check parameter */
    MR_ASSERT(iter != MR_NULL);

    /* Ensure bus is initialized */
    xbus_ensure_init();

    topics = stack_buf;
    count = 0U;
    cap = XBUS_TOPIC_REF_STACK_CAP;
    ret = 0;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xbus_lock);

    /* Count registered topics across topic buckets */
    for (b = 0U; b < MR_CFG_XBUS_TOPIC_BUCKETS; b++) {
        MR_LIST_FOR_EACH(pos, &__xbus_topic_buckets[b]) {
            count++;
        }
    }
    if (count > cap) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);

        topics = mr_malloc(count * sizeof(mr_xbus_topic_t *));
        if (!topics) {
            return -MR_ENOMEM;
        }
        cap = count;

        /* Lock */
        mask = mr_spinlock_lock_irqsave(&__xbus_lock);
    }

    /* Acquire topic references under lock */
    count = 0U;
    for (b = 0U; (b < MR_CFG_XBUS_TOPIC_BUCKETS) && (count < cap); b++) {
        MR_LIST_FOR_EACH(pos, &__xbus_topic_buckets[b]) {
            if (count >= cap) {
                break;
            }
            topic = MR_LIST_ENTRY(pos, mr_xbus_topic_t, hash_node);
            if (((topic->flags & XBUS_TOPIC_F_REGISTERED) != 0U)
                && (mr_xbus_topic_get(topic) != MR_NULL)) {
                topics[count++] = topic;
            }
        }
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);

    /* Invoke iterator and release topic references outside lock */
    for (i = 0U; i < count; i++) {
        if (ret == 0) {
            ret = iter(topics[i], param);
        }
        mr_xbus_topic_put(topics[i]);
    }

    /* Free temporary topic array */
    if (topics != stack_buf) {
        mr_free(topics);
    }
    return ret;
}

static mr_xbus_topic_t *xbus_topic_obj_release(mr_object_t *obj) {
    mr_xbus_topic_t *topic;

    /* Get topic */
    topic = MR_CONTAINER_OF(obj, mr_xbus_topic_t, parent);

    /* Free topic buffer and name */
    xbus_buf_free(&topic->buf);
    xbus_str_free(&topic->name.str, &topic->name.len, XBUS_INAME_PTR(topic));
    return topic;
}

static void xbus_topic_statik_release(mr_object_t *obj) {
    /* Release topic */
    xbus_topic_obj_release(obj);
}

static void xbus_topic_dynamic_release(mr_object_t *obj) {
    mr_xbus_topic_t *topic;

    /* Release topic */
    topic = xbus_topic_obj_release(obj);

    /* Free topic */
    mr_free(topic);
}

static mr_err_t xbus_sub_init_impl(mr_xbus_sub_t *sub, const mr_clazz_t *clazz,
                                   mr_xbus_domain_t *domain,
                                   const char *pattern,
                                   const mr_xbus_sub_cfg_t *cfg) {
    mr_uint16_t q_depth, q_msg_size;
    mr_xbus_topic_t *existing;
    mr_xbus_notify_t *notify;
    mr_uint8_t mode, flags;
    mr_bool_t is_wildcard;
    void *slots, *param;
    mr_uint32_t hash;
    mr_err_t ret;
    int mask;

    /* Ensure bus is initialized */
    xbus_ensure_init();

    /* Resolve domain */
    domain = xbus_domain_resolve(domain);
    if (!domain) {
        return -MR_EINVAL;
    }
    MR_ASSERT(MR_OBJECT_IS_INITED(domain));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(domain, xbus_domain));

    /* Load subscription configuration */
    mode = MR_XBUS_SUB_STATE;
    flags = MR_XBUS_SUB_RETAIN;
    q_depth = 0U;
    q_msg_size = 0U;
    notify = MR_NULL;
    param = MR_NULL;
    slots = MR_NULL;
    if (cfg) {
        mode = cfg->mode;
        flags = (mr_uint8_t)(cfg->flags & XBUS_SUB_F_PUBLIC_MASK);
        q_depth = cfg->queue_depth;
        q_msg_size = cfg->msg_size;
        notify = cfg->notify;
        param = cfg->param;
    }

    /* Validate subscription mode and queue depth */
    if ((mode != MR_XBUS_SUB_STATE) && (mode != MR_XBUS_SUB_QUEUE)) {
        return -MR_EINVAL;
    }
    if ((mode == MR_XBUS_SUB_QUEUE) && (q_depth == 0U)) {
        return -MR_EINVAL;
    }

    /* Pre-allocate queue slot pool outside lock */
    if (mode == MR_XBUS_SUB_QUEUE) {
        slots = xbus_sub_slots_alloc(q_depth, q_msg_size);
        if (!slots) {
            return -MR_ENOMEM;
        }
    }

    /* Init subscription storage */
    mr_memset(sub, 0, sizeof(*sub));
    is_wildcard = MR_FALSE;
    hash = 0U;
    ret = xbus_path_resolve(domain, pattern, MR_TRUE, &sub->pattern,
                            &sub->pattern_len, &hash, XBUS_INAME_PTR(sub),
                            XBUS_INAME_CAP(sub), &is_wildcard);
    if (ret != 0) {
        xbus_sub_slots_free(slots, q_depth, q_msg_size);
        return ret;
    }
    mr_list_init(&sub->bus_node);
    mr_list_init(&sub->domain_node);
    mr_list_init(&sub->binds);
    sub->flags = flags;
    if (is_wildcard) {
        sub->flags |= XBUS_SUB_F_WILDCARD;
    }
    if (mode == MR_XBUS_SUB_QUEUE) {
        sub->flags |= XBUS_SUB_F_QUEUE;
        sub->u.queue.slots = slots;
        sub->u.queue.depth = q_depth;
        sub->u.queue.msg_size = q_msg_size;
    } else {
        mr_list_init(&sub->u.state.dirty_list);
    }
    sub->notify = notify;
    sub->param = param;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xbus_lock);

    /* Ensure owning domain is still registered */
    if ((domain->flags & XBUS_DOMAIN_F_REGISTERED) == 0U) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
        xbus_sub_slots_free(slots, q_depth, q_msg_size);
        xbus_str_free(&sub->pattern, &sub->pattern_len, XBUS_INAME_PTR(sub));
        return -MR_ENOENT;
    }

    /* Reject exact subscription when read ACL forbids it */
    if (!is_wildcard) {
        existing = xbus_topic_find_locked(sub->pattern, sub->pattern_len, hash);
        if (existing && ((existing->flags & XBUS_TOPIC_F_REGISTERED) != 0U)
            && ((existing->read_mask & MR_XBUS_ACL_DOMAIN(domain)) == 0U)) {
            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
            xbus_sub_slots_free(slots, q_depth, q_msg_size);
            xbus_str_free(&sub->pattern, &sub->pattern_len,
                          XBUS_INAME_PTR(sub));
            return -MR_EPERM;
        }
    }

    /* Acquire domain reference while subscription is alive */
    if (!mr_xbus_domain_get(domain)) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
        xbus_sub_slots_free(slots, q_depth, q_msg_size);
        xbus_str_free(&sub->pattern, &sub->pattern_len, XBUS_INAME_PTR(sub));
        return -MR_ENOENT;
    }

    /* Init object */
    ret = mr_object_init(&sub->parent, clazz);
    if (ret != 0) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
        mr_xbus_domain_put(domain);
        xbus_sub_slots_free(slots, q_depth, q_msg_size);
        xbus_str_free(&sub->pattern, &sub->pattern_len, XBUS_INAME_PTR(sub));
        return ret;
    }

    /* Register subscription */
    sub->domain = domain;
    sub->flags |= XBUS_SUB_F_REGISTERED;
    mr_list_add(&__xbus_sub_list, &sub->bus_node);
    mr_list_add(&domain->subs, &sub->domain_node);

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
    return 0;
}

static mr_err_t xbus_sub_sync_and_check(mr_xbus_sub_t *sub) {
    mr_bool_t active;
    mr_err_t ret;
    int mask;

    /* Hold temporary reference across retain notifications */
    if (!mr_xbus_sub_get(sub)) {
        return -MR_ENOENT;
    }

    /* Bind matching topics and flush retain notifications */
    ret = xbus_sub_sync_topics(sub);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xbus_lock);

    /* Check whether subscription and owning domain remain active */
    active = xbus_sub_is_active_locked(sub);

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);

    /* Ensure subscription is unregistered if sync failed or sub became inactive */
    if ((ret != 0) || (!active)) {
        mr_xbus_sub_del(sub);
    }

    /* Release temporary reference; may free dynamic sub if deleted */
    mr_xbus_sub_put(sub);
    if (ret != 0) {
        return ret;
    }
    return active ? 0 : -MR_ENOENT;
}

mr_err_t mr_xbus_sub_init(mr_xbus_sub_t *sub, mr_xbus_domain_t *domain,
                          const char *pattern, const mr_xbus_sub_cfg_t *cfg) {
    const mr_clazz_t *clazz = MR_CLAZZES_STATIK(xbus_sub);
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(sub != MR_NULL);
    MR_ASSERT(pattern != MR_NULL);

    /* Init subscription */
    ret = xbus_sub_init_impl(sub, clazz, domain, pattern, cfg);
    if (ret != 0) {
        return ret;
    }

    /* Bind matching topics */
    return xbus_sub_sync_and_check(sub);
}

mr_xbus_sub_t *mr_xbus_sub_create(mr_xbus_domain_t *domain, const char *pattern,
                                  const mr_xbus_sub_cfg_t *cfg) {
    const mr_clazz_t *clazz = MR_CLAZZES_DYNAMIC(xbus_sub);
    mr_xbus_sub_t *sub;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(pattern != MR_NULL);

    /* Create subscription */
    sub = mr_malloc(sizeof(mr_xbus_sub_t));
    if (!sub) {
        return MR_NULL;
    }

    /* Init subscription */
    ret = xbus_sub_init_impl(sub, clazz, domain, pattern, cfg);
    if (ret != 0) {
        mr_free(sub);
        return MR_NULL;
    }

    /* Bind matching topics */
    ret = xbus_sub_sync_and_check(sub);
    if (ret != 0) {
        return MR_NULL;
    }
    return sub;
}

mr_err_t mr_xbus_sub_del(mr_xbus_sub_t *sub) {
    mr_list_t *pos, *next;
    mr_list_t free_list;
    xbus_bind_t *bind;
    int mask;

    /* Check parameter */
    MR_ASSERT((sub != MR_NULL) && MR_OBJECT_IS_INITED(sub));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(sub, xbus_sub));

    mr_list_init(&free_list);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xbus_lock);

    /* Unregister subscription */
    if ((sub->flags & XBUS_SUB_F_REGISTERED) == 0U) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
        return -MR_ENOENT;
    }
    sub->flags |= XBUS_SUB_F_DETACHED;
    mr_list_del(&sub->bus_node);
    mr_list_del(&sub->domain_node);
    sub->flags &= (mr_uint8_t)(~XBUS_SUB_F_REGISTERED);

    /* Detach all bindings */
    MR_LIST_FOR_EACH_SAFE(pos, next, &sub->binds) {
        bind = MR_LIST_ENTRY(pos, xbus_bind_t, sub_node);
        xbus_bind_detach_locked(bind, &free_list);
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);

    /* Free bindings and drain queued topic references outside lock */
    xbus_bind_free_list(&free_list);
    xbus_sub_queue_clear(sub);

    /* Delete object */
    mr_object_del(&sub->parent);
    return 0;
}

static mr_ssize_t xbus_sub_pull(mr_xbus_sub_t *sub, void *buf, mr_size_t size,
                                mr_xbus_meta_t *meta, mr_bool_t is_peek) {
    mr_xbus_topic_t *put_topic, *topic;
    mr_uint8_t *slot_src;
    mr_size_t copy_len;
    xbus_bind_t *bind;
    xbus_slot_t *slot;
    mr_uint8_t flags;
    int mask;

    /* Check parameter */
    MR_ASSERT((sub != MR_NULL) && MR_OBJECT_IS_INITED(sub));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(sub, xbus_sub));
    if ((size > 0U) && (!buf)) {
        return -MR_EINVAL;
    }

    put_topic = MR_NULL;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xbus_lock);

    /* Reject inactive subscription or unregistered owning domain */
    if (!xbus_sub_is_active_locked(sub)) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
        return -MR_ECANCELED;
    }

    if ((sub->flags & XBUS_SUB_F_QUEUE) == 0U) {
        /* STATE: check for pending dirty binding */
        if (mr_list_is_empty(&sub->u.state.dirty_list)) {
            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
            return -MR_EAGAIN;
        }

        bind = MR_LIST_ENTRY(sub->u.state.dirty_list.next, xbus_bind_t,
                             dirty_node);
        topic = bind->topic;

        /* Copy payload and metadata */
        copy_len = MR_MIN(size, (mr_size_t)topic->buf.len);
        if (copy_len > 0U) {
            mr_memcpy(buf, topic->buf.data, copy_len);
        }
        flags = ((topic->flags & XBUS_TOPIC_F_STALE) != 0U)
                  ? MR_XBUS_FLAG_STALE
                  : 0U;
        if (size < (mr_size_t)topic->buf.len) {
            flags |= MR_XBUS_FLAG_TRUNCATED;
        }
        if (bind->dropped > 0U) {
            flags |= MR_XBUS_FLAG_DROPPED;
        }
        if (meta) {
            meta->topic = topic;
            meta->seq = topic->seq;
            meta->stamp = topic->stamp;
            meta->len = topic->buf.len;
            meta->dropped = bind->dropped;
            meta->type_id = topic->type_id;
            meta->owner = topic->owner;
            meta->flags = flags;
        }

        /* Consume dirty state when reading */
        if (!is_peek) {
            bind->last_seq = topic->seq;
            bind->dropped = 0U;
            bind->flags &= (mr_uint8_t)(~XBUS_BIND_F_DIRTY);
            mr_list_del(&bind->dirty_node);
            if (sub->u.state.dirty_count > 0U) {
                sub->u.state.dirty_count--;
            }
        }
    } else {
        /* QUEUE: check for pending queued slot */
        if (sub->u.queue.count == 0U) {
            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
            return -MR_EAGAIN;
        }

        slot = xbus_sub_slot(sub, sub->u.queue.head);
        slot_src = xbus_sub_slot_data(sub, sub->u.queue.head);

        /* Copy payload and metadata */
        copy_len = MR_MIN(size, (mr_size_t)slot->len);
        if (copy_len > 0U) {
            mr_memcpy(buf, slot_src, copy_len);
        }
        flags = slot->flags;
        if (size < (mr_size_t)slot->len) {
            flags |= MR_XBUS_FLAG_TRUNCATED;
        }
        if (sub->u.queue.pending_dropped > 0U) {
            flags |= MR_XBUS_FLAG_DROPPED;
        }
        if (meta) {
            meta->topic = (slot->topic
                           && ((slot->topic->flags & XBUS_TOPIC_F_REGISTERED)
                               != 0U))
                            ? slot->topic
                            : MR_NULL;
            meta->seq = slot->seq;
            meta->stamp = slot->stamp;
            meta->len = slot->len;
            meta->dropped = sub->u.queue.pending_dropped;
            meta->type_id = slot->topic ? slot->topic->type_id : 0U;
            meta->owner = slot->owner;
            meta->flags = flags;
        }

        /* Pop head slot when reading */
        if (!is_peek) {
            put_topic = slot->topic;
            slot->topic = MR_NULL;
            slot->len = 0U;
            sub->u.queue.pending_dropped = 0U;
            sub->u.queue.head
                = (mr_uint16_t)((sub->u.queue.head + 1U) % sub->u.queue.depth);
            sub->u.queue.count--;
        }
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);

    /* Release dequeued topic reference outside lock */
    if (put_topic) {
        mr_xbus_topic_put(put_topic);
    }

    /* Trace subscription read */
    if (!is_peek) {
        MR_TRACE_EVENT("read", sub->domain->id, sub, copy_len);
    }
    return (mr_ssize_t)copy_len;
}

mr_ssize_t mr_xbus_sub_read(mr_xbus_sub_t *sub, void *buf, mr_size_t size,
                            mr_xbus_meta_t *meta) {
    /* Read next unread message */
    return xbus_sub_pull(sub, buf, size, meta, MR_FALSE);
}

mr_ssize_t mr_xbus_sub_peek(mr_xbus_sub_t *sub, void *buf, mr_size_t size,
                            mr_xbus_meta_t *meta) {
    /* Peek next unread message */
    return xbus_sub_pull(sub, buf, size, meta, MR_TRUE);
}

mr_uint32_t mr_xbus_sub_pending(mr_xbus_sub_t *sub) {
    mr_uint32_t pending;
    int mask;

    /* Check parameter */
    MR_ASSERT((sub != MR_NULL) && MR_OBJECT_IS_INITED(sub));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(sub, xbus_sub));

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xbus_lock);

    /* Read pending item count */
    pending = ((sub->flags & XBUS_SUB_F_QUEUE) == 0U)
                ? sub->u.state.dirty_count
                : sub->u.queue.count;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
    return pending;
}

mr_err_t mr_xbus_sub_clear(mr_xbus_sub_t *sub) {
    mr_list_t *pos, *next;
    xbus_bind_t *bind;
    int mask;

    /* Check parameter */
    MR_ASSERT((sub != MR_NULL) && MR_OBJECT_IS_INITED(sub));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(sub, xbus_sub));

    /* Clear QUEUE slots outside lock */
    if ((sub->flags & XBUS_SUB_F_QUEUE) != 0U) {
        xbus_sub_queue_clear(sub);
        return 0;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xbus_lock);

    /* Clear STATE dirty list */
    MR_LIST_FOR_EACH_SAFE(pos, next, &sub->u.state.dirty_list) {
        bind = MR_LIST_ENTRY(pos, xbus_bind_t, dirty_node);
        mr_list_del(&bind->dirty_node);
        bind->flags &= (mr_uint8_t)(~XBUS_BIND_F_DIRTY);
        bind->dropped = 0U;
    }
    sub->u.state.dirty_count = 0U;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xbus_lock, mask);
    return 0;
}

static mr_xbus_sub_t *xbus_sub_obj_release(mr_object_t *obj) {
    mr_xbus_sub_t *sub;

    /* Get subscription */
    sub = MR_CONTAINER_OF(obj, mr_xbus_sub_t, parent);

    /* Free queue slot pool */
    if ((sub->flags & XBUS_SUB_F_QUEUE) != 0U) {
        xbus_sub_slots_free(sub->u.queue.slots, sub->u.queue.depth,
                            sub->u.queue.msg_size);
        sub->u.queue.slots = MR_NULL;
    }

    /* Free pattern string and release domain reference */
    xbus_str_free(&sub->pattern, &sub->pattern_len, XBUS_INAME_PTR(sub));
    if (sub->domain) {
        mr_xbus_domain_put(sub->domain);
        sub->domain = MR_NULL;
    }
    return sub;
}

static void xbus_sub_statik_release(mr_object_t *obj) {
    /* Release subscription */
    xbus_sub_obj_release(obj);
}

static void xbus_sub_dynamic_release(mr_object_t *obj) {
    mr_xbus_sub_t *sub;

    /* Release subscription */
    sub = xbus_sub_obj_release(obj);

    /* Free subscription */
    mr_free(sub);
}

static void mr_xbus_auto_init(void) {
    /* Ensure bus is initialized */
    xbus_ensure_init();
}
MR_INIT_EXPORT(mr_xbus_auto_init, MR_INIT_LEVEL_MODULE);
#endif /* defined(MR_USE_MODULE_XBUS) */
