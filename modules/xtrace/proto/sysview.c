/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/modules/xtrace/proto/mr_xtrace_sysview.h>
#if defined(MR_USE_MODULE_XTRACE_PROTO_SYSVIEW)
#include <mrX/kernel/mr_initcall.h>
#include <mrX/kernel/mr_spinlock.h>
#include <mrX/libc/mr_string.h>

/* SystemView protocol operations */
static void sysview_emit(void *priv, const mr_trace_event_t *event);
static void sysview_start(void *priv);
static void sysview_stop(void *priv);
static void sysview_flush(void *priv);
static const mr_xtrace_ops_t __sysview_ops = {
    .emit = sysview_emit,
    .init = MR_NULL,
    .deinit = MR_NULL,
    .start = sysview_start,
    .stop = sysview_stop,
    .flush = sysview_flush,
};
static mr_xtrace_t __sysview_proto = MR_XTRACE_INIT(&__sysview_ops, MR_NULL);

/* SystemView internal definition */
#define SYSVIEW_RECORD_MAX              (160U)
#define SYSVIEW_EXT_EVTID_MIN           (24U)
#define SYSVIEW_MARKER_LABEL_MAX        (48U)

/* SystemView marker entry type */
typedef struct sysview_marker {
    char label[SYSVIEW_MARKER_LABEL_MAX];
} sysview_marker_t;

/* SystemView staging state */
#if !defined(MR_CFG_SYSVIEW_MARKERS)
#define MR_CFG_SYSVIEW_MARKERS          (32U)
#endif /* !defined(MR_CFG_SYSVIEW_MARKERS) */
#if (MR_CFG_SYSVIEW_MARKERS < 8U)
#error "MR_CFG_SYSVIEW_MARKERS must be at least 8"
#endif /* (MR_CFG_SYSVIEW_MARKERS < 8U) */
static sysview_marker_t __sysview_markers[MR_CFG_SYSVIEW_MARKERS];
static mr_spinlock_t __sysview_lock = MR_SPINLOCK_INIT();
static mr_bool_t __sysview_started = MR_FALSE;
static mr_bool_t __sysview_has_ts = MR_FALSE;
static mr_size_t __sysview_marker_count = 0U;
static mr_uint64_t __sysview_last_ts = 0U;

static mr_size_t sysview_encode_u32(mr_uint8_t *buf, mr_uint32_t val) {
    mr_size_t pos;

    /* Encode ULEB128 */
    pos = 0U;
    while (val >= 0x80U) {
        buf[pos++] = (mr_uint8_t)((val & 0x7fU) | 0x80U);
        val >>= 7U;
    }
    buf[pos++] = (mr_uint8_t)(val & 0x7fU);
    return pos;
}

static mr_size_t sysview_append_str(char *dst, mr_size_t pos, mr_size_t max,
                                    const char *str) {
    mr_size_t i;

    /* Check parameter */
    if (!str) {
        return pos;
    }

    /* Copy string bytes */
    for (i = 0U; (str[i] != '\0') && (pos < max); i++) {
        dst[pos++] = str[i];
    }
    return pos;
}

static mr_size_t sysview_append_hex(char *dst, mr_size_t pos, mr_size_t max,
                                    mr_uintptr_t val) {
    static const char hex_digits[] = "0123456789abcdef";
    char tmp[sizeof(mr_uintptr_t) * 2U];
    mr_size_t n;

    /* Append prefix */
    pos = sysview_append_str(dst, pos, max, "0x");

    /* Collect hex digits in reverse */
    n = 0U;
    do {
        tmp[n++] = hex_digits[val & 0x0fU];
        val >>= 4U;
    } while (val != 0U);

    /* Append digits in order */
    while ((n > 0U) && (pos < max)) {
        dst[pos++] = tmp[--n];
    }
    return pos;
}

static mr_size_t sysview_append_u64(char *dst, mr_size_t pos, mr_size_t max,
                                    mr_uint64_t val) {
    char tmp[20];
    mr_size_t n;

    /* Collect decimal digits in reverse */
    n = 0U;
    do {
        tmp[n++] = (char)('0' + (val % 10U));
        val /= 10U;
    } while (val != 0U);

    /* Append digits in order */
    while ((n > 0U) && (pos < max)) {
        dst[pos++] = tmp[--n];
    }
    return pos;
}

static mr_size_t sysview_append_i64(char *dst, mr_size_t pos, mr_size_t max,
                                    mr_int64_t val) {
    mr_uint64_t mag;

    /* Avoid negating the signed minimum */
    if (val < 0) {
        if (pos < max) {
            dst[pos++] = '-';
        }
        mag = (mr_uint64_t)(-(val + 1)) + 1U;
    } else {
        mag = (mr_uint64_t)val;
    }
    return sysview_append_u64(dst, pos, max, mag);
}

static mr_size_t sysview_format_label(char *dst, mr_size_t max, const char *tag,
                                      const char *name) {
    mr_size_t pos;

    /* Join tag and name with a colon */
    pos = 0U;
    if (tag && (tag[0] != '\0')) {
        pos = sysview_append_str(dst, pos, max, tag);
        if (name && (name[0] != '\0') && (pos < max)) {
            dst[pos++] = ':';
        }
    }
    pos = sysview_append_str(dst, pos, max, name);
    return pos;
}

static mr_size_t sysview_format_event_text(char *dst, mr_size_t max,
                                           const mr_trace_event_t *event,
                                           mr_size_t argc) {
    mr_size_t len, i;

    /* Format event label */
    len = sysview_format_label(dst, max, event->tag, event->name);

    /* Format counter value */
    if (event->type == MR_TRACE_TYPE_COUNTER) {
        len = sysview_append_str(dst, len, max, "=");
        len = sysview_append_i64(
            dst, len, max,
            (argc > 0U) ? (mr_int64_t)(mr_intptr_t)event->args[0] : 0);
        return len;
    }

    /* Format event arguments */
    if (argc > 0U) {
        len = sysview_append_str(dst, len, max, "(");
        for (i = 0U; i < argc; i++) {
            if (i > 0U) {
                len = sysview_append_str(dst, len, max, ",");
            }
            len = sysview_append_hex(dst, len, max, event->args[i]);
        }
        len = sysview_append_str(dst, len, max, ")");
    }
    return len;
}

static void sysview_write_locked(const mr_uint8_t *data, mr_size_t size) {
    mr_ssize_t written;

    /* Check parameter */
    if ((!data) || (size == 0U)) {
        return;
    }

    /* Write encoded record directly into xtrace FIFO */
    written = mr_xtrace_write(data, size);
    if ((written < 0) || ((mr_size_t)written < size)) {
        mr_xtrace_dropped_inc();
    }
}

static void sysview_emit_packet_locked(mr_uint32_t evtid,
                                       const mr_uint8_t *payload,
                                       mr_size_t payload_len,
                                       mr_uint32_t ts_delta) {
    mr_uint8_t pkt[SYSVIEW_RECORD_MAX];
    mr_size_t pos;

    /* Encode header with optional length */
    pos = sysview_encode_u32(pkt, evtid);
    if (evtid >= SYSVIEW_EXT_EVTID_MIN) {
        pos += sysview_encode_u32(&pkt[pos], (mr_uint32_t)payload_len);
    }

    /* Reserve five bytes for ULEB128 delta */
    if ((pos + payload_len + 5U) > sizeof(pkt)) {
        mr_xtrace_dropped_inc();
        return;
    }

    /* Copy payload */
    if ((payload_len > 0U) && payload) {
        mr_memcpy(&pkt[pos], payload, payload_len);
        pos += payload_len;
    }

    /* Encode timestamp delta */
    pos += sysview_encode_u32(&pkt[pos], ts_delta);

    /* Write packet into FIFO */
    sysview_write_locked(pkt, pos);
}

static mr_size_t sysview_append_desc_field(char *dst, mr_size_t pos,
                                           mr_size_t max, const char *key,
                                           const char *val) {
    /* Omit empty system description fields */
    if ((!val) || (val[0] == '\0')) {
        return pos;
    }

    /* Append field separator */
    if ((pos > 0U) && (pos < max)) {
        dst[pos++] = ',';
    }

    /* Append key and value */
    pos = sysview_append_str(dst, pos, max, key);
    pos = sysview_append_str(dst, pos, max, val);
    return pos;
}

static void sysview_send_header_locked(void) {
    mr_uint8_t payload[MR_SYSVIEW_STR_MAX + 1U];
    mr_uint8_t sync[MR_SYSVIEW_SYNC_LEN];
    char desc[MR_SYSVIEW_STR_MAX];
    mr_xtrace_sysinfo_t info;
    mr_size_t pos, desc_len;

    /* Get stream metadata */
    mr_xtrace_sysinfo_get(&info);

    /* Emit sync and TRACE_START */
    mr_memset(sync, 0, sizeof(sync));
    sysview_write_locked(sync, sizeof(sync));
    sysview_emit_packet_locked(MR_SYSVIEW_ID_START, MR_NULL, 0U, 0U);

    /* Emit INIT packet */
    pos = 0U;
    pos += sysview_encode_u32(&payload[pos], info.ts_freq);
    pos += sysview_encode_u32(&payload[pos], info.cpu_freq);
    pos += sysview_encode_u32(&payload[pos], (mr_uint32_t)info.ram_base);
    pos += sysview_encode_u32(&payload[pos], 0U);
    sysview_emit_packet_locked(MR_SYSVIEW_ID_INIT, payload, pos, 0U);

    /* Format system description fields */
    desc_len = 0U;
    desc_len = sysview_append_desc_field(desc, desc_len, sizeof(desc),
                                         "N=", info.app_name);
    desc_len = sysview_append_desc_field(desc, desc_len, sizeof(desc),
                                         "O=", info.os_name);
    desc_len = sysview_append_desc_field(desc, desc_len, sizeof(desc),
                                         "D=", info.device_name);
    desc_len = sysview_append_desc_field(desc, desc_len, sizeof(desc),
                                         "C=", info.core_name);

    /* Emit length-prefixed SYSDESC */
    payload[0] = (mr_uint8_t)desc_len;
    mr_memcpy(&payload[1], desc, desc_len);
    sysview_emit_packet_locked(MR_SYSVIEW_ID_SYSDESC, payload, 1U + desc_len,
                               0U);
    __sysview_started = MR_TRUE;
}

static mr_uint32_t sysview_get_delta_locked(mr_uint64_t ts) {
    mr_uint64_t delta;

    /* Set initial timestamp baseline */
    if (!__sysview_has_ts) {
        __sysview_last_ts = ts;
        __sysview_has_ts = MR_TRUE;
        return 0U;
    }

    /* Clamp delta to 32 bits */
    delta = (ts >= __sysview_last_ts) ? (ts - __sysview_last_ts) : 0U;
    __sysview_last_ts = ts;
    if (delta > 0xffffffffU) {
        delta = 0xffffffffU;
    }
    return (mr_uint32_t)delta;
}

static mr_uint32_t sysview_resolve_marker_locked(const char *tag,
                                                 const char *name) {
    mr_uint8_t payload[MR_SYSVIEW_STR_MAX + 8U];
    char key[SYSVIEW_MARKER_LABEL_MAX];
    char label[MR_SYSVIEW_STR_MAX];
    mr_size_t i, key_len, label_len, pos;
    mr_uint32_t marker_id;

    /* Build marker lookup key */
    key_len = sysview_format_label(key, sizeof(key) - 1U, tag, name);
    key[key_len] = '\0';

    /* Reuse the ID for a registered label */
    for (i = 0U; i < __sysview_marker_count; i++) {
        if (mr_strcmp(__sysview_markers[i].label, key) == 0) {
            return (mr_uint32_t)(i + 1U);
        }
    }

    /* Use zero for a full marker table */
    if (__sysview_marker_count >= MR_CFG_SYSVIEW_MARKERS) {
        return 0U;
    }

    /* Copy label into marker table */
    i = __sysview_marker_count++;
    mr_memcpy(__sysview_markers[i].label, key, key_len + 1U);
    marker_id = (mr_uint32_t)(i + 1U);

    /* Announce name before marker use */
    label_len = sysview_format_label(label, sizeof(label), tag, name);
    pos = sysview_encode_u32(payload, MR_SYSVIEW_EX_NAME_MARKER);
    pos += sysview_encode_u32(&payload[pos], marker_id);
    payload[pos++] = (mr_uint8_t)label_len;
    if (label_len > 0U) {
        mr_memcpy(&payload[pos], label, label_len);
        pos += label_len;
    }
    sysview_emit_packet_locked(MR_SYSVIEW_ID_EX, payload, pos, 0U);
    return marker_id;
}

static void sysview_emit_print_locked(const char *msg, mr_size_t msg_len,
                                      mr_uint32_t ts_delta) {
    mr_uint8_t payload[MR_SYSVIEW_STR_MAX + 8U];
    mr_size_t pos;

    /* Limit print message length */
    if (msg_len > MR_SYSVIEW_STR_MAX) {
        msg_len = MR_SYSVIEW_STR_MAX;
    }

    /* Encode PRINT_FORMATTED payload */
    pos = 0U;
    payload[pos++] = (mr_uint8_t)msg_len;
    if (msg_len > 0U) {
        mr_memcpy(&payload[pos], msg, msg_len);
        pos += msg_len;
    }
    pos += sysview_encode_u32(&payload[pos], 0U);
    pos += sysview_encode_u32(&payload[pos], 0U);
    sysview_emit_packet_locked(MR_SYSVIEW_ID_PRINT, payload, pos, ts_delta);
}

static void sysview_start(void *priv) {
    int mask;

    MR_IGNORE(priv);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__sysview_lock);

    /* Reset marker IDs and timestamps */
    __sysview_marker_count = 0U;
    __sysview_last_ts = 0U;
    __sysview_has_ts = MR_FALSE;

    /* Emit stream header */
    sysview_send_header_locked();

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__sysview_lock, mask);
}

static void sysview_flush(void *priv) {
    /* Records are written directly to the xtrace FIFO without staging */
    MR_IGNORE(priv);
}

static void sysview_stop(void *priv) {
    int mask;

    MR_IGNORE(priv);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__sysview_lock);

    /* Emit TRACE_STOP once per active stream */
    if (__sysview_started) {
        sysview_emit_packet_locked(MR_SYSVIEW_ID_STOP, MR_NULL, 0U, 0U);
        __sysview_started = MR_FALSE;
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__sysview_lock, mask);
}

static void sysview_emit(void *priv, const mr_trace_event_t *event) {
    mr_uint32_t delta, marker_id, evtid;
    char text[MR_SYSVIEW_STR_MAX];
    mr_size_t argc, text_len, pos;
    mr_uint8_t payload[8];
    int mask;

    MR_IGNORE(priv);

    /* Check parameter */
    if (!event) {
        return;
    }

    /* Limit argument count */
    argc = (event->args && (event->argc > 0U)) ? event->argc : 0U;
    if (argc > MR_SYSVIEW_ARGS_MAX) {
        argc = MR_SYSVIEW_ARGS_MAX;
    }

    /* Format text before locking */
    if ((event->type != MR_TRACE_TYPE_BEGIN)
        && (event->type != MR_TRACE_TYPE_END)) {
        text_len = sysview_format_event_text(text, sizeof(text), event, argc);
    } else {
        text_len = 0U;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__sysview_lock);

    /* Emit header before first event */
    if (!__sysview_started) {
        sysview_send_header_locked();
    }

    /* Get timestamp delta */
    delta = sysview_get_delta_locked(event->timestamp);

    /* Encode interval boundaries as markers */
    if ((event->type == MR_TRACE_TYPE_BEGIN)
        || (event->type == MR_TRACE_TYPE_END)) {
        /* Resolve marker ID */
        evtid = (event->type == MR_TRACE_TYPE_BEGIN) ? MR_SYSVIEW_ID_MARK_START
                                                     : MR_SYSVIEW_ID_MARK_STOP;
        marker_id = sysview_resolve_marker_locked(event->tag, event->name);

        if (marker_id != 0U) {
            /* Emit marker packet */
            pos = sysview_encode_u32(payload, marker_id);
            sysview_emit_packet_locked(evtid, payload, pos, delta);
        } else {
            /* Print when marker table is full */
            text_len
                = sysview_format_event_text(text, sizeof(text), event, argc);
            sysview_emit_print_locked(text, text_len, delta);
        }
    } else {
        sysview_emit_print_locked(text, text_len, delta);
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__sysview_lock, mask);
}

mr_err_t mr_xtrace_sysview_register(void) {
    /* Register SystemView protocol */
    return mr_xtrace_register(&__sysview_proto);
}

static void mr_xtrace_sysview_auto_init(void) {
    mr_xtrace_sysview_register();
}
MR_INIT_EXPORT(mr_xtrace_sysview_auto_init, MR_INIT_LEVEL_MODULE);
#endif /* defined(MR_USE_MODULE_XTRACE_PROTO_SYSVIEW) */
