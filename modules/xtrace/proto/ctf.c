/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/modules/xtrace/proto/mr_xtrace_ctf.h>
#if defined(MR_USE_MODULE_XTRACE_PROTO_CTF)
#include <mrX/kernel/mr_initcall.h>
#include <mrX/kernel/mr_spinlock.h>
#include <mrX/libc/mr_sprintf.h>
#include <mrX/libc/mr_string.h>

/* CTF protocol operations */
static void ctf_emit(void *priv, const mr_trace_event_t *event);
static void ctf_start(void *priv);
static void ctf_stop(void *priv);
static void ctf_flush(void *priv);
static const mr_xtrace_ops_t __ctf_ops = {
    .emit = ctf_emit,
    .init = MR_NULL,
    .deinit = MR_NULL,
    .start = ctf_start,
    .stop = ctf_stop,
    .flush = ctf_flush,
};
static mr_xtrace_t __ctf_proto = MR_XTRACE_INIT(&__ctf_ops, MR_NULL);

/* CTF packet staging state */
#if !defined(MR_CFG_XTRACE_CTF_PACKET_SIZE)
#define MR_CFG_XTRACE_CTF_PACKET_SIZE   (256U)
#endif /* !defined(MR_CFG_XTRACE_CTF_PACKET_SIZE) */
#if (MR_CFG_XTRACE_CTF_PACKET_SIZE < 256U)
#error "MR_CFG_XTRACE_CTF_PACKET_SIZE must be at least 256 bytes"
#endif /* (MR_CFG_XTRACE_CTF_PACKET_SIZE < 256U) */
static MR_ALIGN(MR_ALIGN_SIZE)
    mr_uint8_t __ctf_pkt[MR_CFG_XTRACE_CTF_PACKET_SIZE];
static mr_spinlock_t __ctf_lock = MR_SPINLOCK_INIT();
static mr_bool_t __ctf_has_events = MR_FALSE;
static mr_uint32_t __ctf_seq = 0U;
static mr_size_t __ctf_pos = 0U;

static mr_size_t ctf_strnlen(const char *str, mr_size_t max) {
    mr_size_t len;

    /* Check parameter */
    if (!str) {
        return 0U;
    }

    /* Get bounded string length */
    for (len = 0U; (len < max) && (str[len] != '\0'); len++) {
    }
    return len;
}

static void ctf_begin_packet_locked(void) {
    mr_ctf_packet_prefix_t *prefix;
    mr_xtrace_sysinfo_t info;

    /* Get stream metadata */
    mr_xtrace_sysinfo_get(&info);

    /* Init CTF 1.8 packet header and context */
    prefix = (mr_ctf_packet_prefix_t *)__ctf_pkt;
    prefix->hdr.magic = MR_CTF_MAGIC;
    prefix->hdr.stream_id = MR_CTF_STREAM_ID;
    prefix->ctx.timestamp_begin = 0U;
    prefix->ctx.timestamp_end = 0U;
    prefix->ctx.content_size = 0U;
    prefix->ctx.packet_size = 0U;
    prefix->ctx.events_discarded = 0U;
    prefix->ctx.seq = __ctf_seq++;
    prefix->ctx.anchor_addr = (mr_uint64_t)info.anchor_addr;
    prefix->ctx.ts_freq = info.ts_freq;
    prefix->ctx.cpu_freq = info.cpu_freq;
    prefix->ctx.ptr_bits = (mr_uint32_t)(sizeof(mr_uintptr_t) * 8U);
    __ctf_pos = sizeof(mr_ctf_packet_prefix_t);
    __ctf_has_events = MR_FALSE;
}

static void ctf_commit_packet_locked(void) {
    mr_ctf_packet_prefix_t *prefix;
    mr_uint32_t size_bits;
    mr_ssize_t written;

    /* Skip packets without event records */
    if (__ctf_pos <= sizeof(mr_ctf_packet_prefix_t)) {
        return;
    }

    /* Finalize context (sizes in bits) */
    prefix = (mr_ctf_packet_prefix_t *)__ctf_pkt;
    size_bits = (mr_uint32_t)(__ctf_pos * 8U);
    prefix->ctx.content_size = size_bits;
    prefix->ctx.packet_size = size_bits;
    prefix->ctx.events_discarded = mr_xtrace_dropped_count();

    /* Write packet into FIFO and count failures */
    written = mr_xtrace_write(__ctf_pkt, __ctf_pos);
    if ((written < 0) || ((mr_size_t)written < __ctf_pos)) {
        mr_xtrace_dropped_inc();
    }
    __ctf_pos = 0U;
    __ctf_has_events = MR_FALSE;
}

static void ctf_write_str_locked(const char *str, mr_size_t len) {
    /* Write NUL-terminated string */
    if ((len > 0U) && str) {
        mr_memcpy(&__ctf_pkt[__ctf_pos], str, len);
        __ctf_pos += len;
    }
    __ctf_pkt[__ctf_pos++] = '\0';
}

static void ctf_start(void *priv) {
    int mask;

    MR_IGNORE(priv);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__ctf_lock);

    /* Begin packet */
    if (__ctf_pos == 0U) {
        ctf_begin_packet_locked();
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__ctf_lock, mask);
}

static void ctf_flush(void *priv) {
    int mask;

    MR_IGNORE(priv);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__ctf_lock);

    /* Commit packet */
    ctf_commit_packet_locked();

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__ctf_lock, mask);
}

static void ctf_stop(void *priv) {
    ctf_flush(priv);
}

static void ctf_emit(void *priv, const mr_trace_event_t *event) {
    mr_size_t tag_len, name_len, argc, args_size, record_size;
    mr_ctf_packet_prefix_t *prefix;
    mr_ctf_event_hdr_t hdr;
    int mask;

    MR_IGNORE(priv);

    /* Check parameter */
    if (!event) {
        return;
    }

    /* Reject unknown event IDs */
    if (event->type > MR_TRACE_TYPE_COUNTER) {
        mr_xtrace_dropped_inc();
        return;
    }

    /* Limit argument count */
    argc = (event->args && (event->argc > 0U)) ? event->argc : 0U;
    if (argc > MR_CTF_ARGS_MAX) {
        argc = MR_CTF_ARGS_MAX;
    }

    /* Size record with NULs and count byte */
    tag_len = ctf_strnlen(event->tag, MR_CTF_STR_MAX - 1U);
    name_len = ctf_strnlen(event->name, MR_CTF_STR_MAX - 1U);
    args_size = argc * sizeof(mr_uintptr_t);
    record_size
        = sizeof(hdr) + (tag_len + 1U) + (name_len + 1U) + 1U + args_size;

    /* Check empty packet capacity */
    if (record_size
        > (MR_CFG_XTRACE_CTF_PACKET_SIZE - sizeof(mr_ctf_packet_prefix_t))) {
        mr_xtrace_dropped_inc();
        return;
    }

    /* Init event header */
    hdr.id = event->type;
    hdr.timestamp = event->timestamp;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__ctf_lock);

    /* Begin packet */
    if (__ctf_pos == 0U) {
        ctf_begin_packet_locked();
    }

    /* Commit packet to make room */
    if ((__ctf_pos + record_size) > MR_CFG_XTRACE_CTF_PACKET_SIZE) {
        ctf_commit_packet_locked();
        ctf_begin_packet_locked();
    }

    /* Track packet timestamp range */
    prefix = (mr_ctf_packet_prefix_t *)__ctf_pkt;
    if (!__ctf_has_events) {
        prefix->ctx.timestamp_begin = event->timestamp;
        prefix->ctx.timestamp_end = event->timestamp;
        __ctf_has_events = MR_TRUE;
    } else {
        if (event->timestamp < prefix->ctx.timestamp_begin) {
            prefix->ctx.timestamp_begin = event->timestamp;
        }
        if (event->timestamp > prefix->ctx.timestamp_end) {
            prefix->ctx.timestamp_end = event->timestamp;
        }
    }

    /* Write record in metadata order */
    mr_memcpy(&__ctf_pkt[__ctf_pos], &hdr, sizeof(hdr));
    __ctf_pos += sizeof(hdr);
    ctf_write_str_locked(event->tag, tag_len);
    ctf_write_str_locked(event->name, name_len);
    __ctf_pkt[__ctf_pos++] = (mr_uint8_t)argc;
    if (args_size > 0U) {
        mr_memcpy(&__ctf_pkt[__ctf_pos], event->args, args_size);
        __ctf_pos += args_size;
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__ctf_lock, mask);
}

static void ctf_escape_str(char *dst, mr_size_t size, const char *src) {
    mr_size_t pos;
    char ch;

    /* Check parameter */
    if (size == 0U) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }

    /* Escape and sanitize TSDL string */
    pos = 0U;
    while ((*src != '\0') && ((pos + 1U) < size)) {
        ch = *src++;
        if ((ch == '"') || (ch == '\\')) {
            if ((pos + 2U) >= size) {
                break;
            }
            dst[pos++] = '\\';
            dst[pos++] = ch;
        } else if (((mr_uint8_t)ch < 0x20U) || ((mr_uint8_t)ch == 0x7fU)) {
            dst[pos++] = ' ';
        } else {
            dst[pos++] = ch;
        }
    }
    dst[pos] = '\0';
}

mr_ssize_t mr_xtrace_ctf_metadata(char *buf, mr_size_t size) {
    static const mr_uint16_t endian_test = 1U;
    char app_name[64], os_name[64], device_name[64], core_name[64];
    const char *byte_order;
    mr_xtrace_sysinfo_t info;
    mr_uint32_t ptr_bits;
    int len;

    /* Check parameter */
    if ((!buf) || (size == 0U)) {
        return -MR_EINVAL;
    }

    /* Get stream metadata and layout */
    mr_xtrace_sysinfo_get(&info);
    byte_order = (*((const mr_uint8_t *)&endian_test) == 1U) ? "le" : "be";
    ctf_escape_str(app_name, sizeof(app_name), info.app_name);
    ctf_escape_str(os_name, sizeof(os_name), info.os_name);
    ctf_escape_str(device_name, sizeof(device_name), info.device_name);
    ctf_escape_str(core_name, sizeof(core_name), info.core_name);
    ptr_bits = (mr_uint32_t)(sizeof(mr_uintptr_t) * 8U);

    /* Format CTF 1.8 metadata */
    len = mr_snprintf(
        buf, size,
        "/* CTF 1.8 */\n\n"
        "typealias integer { size = 8; align = 8; signed = false; }"
        " := uint8_t;\n"
        "typealias integer { size = 32; align = 8; signed = false; }"
        " := uint32_t;\n"
        "typealias integer { size = 64; align = 8; signed = false; }"
        " := uint64_t;\n"
        "typealias integer { size = 64; align = 8; signed = false; base = 16; }"
        " := uint64_hex_t;\n"
        "typealias integer { size = %u; align = 8; signed = false; base = 16; }"
        " := uintptr_t;\n\n"
        "clock {\n"
        "    name = monotonic;\n"
        "    freq = %u;\n"
        "};\n\n"
        "typealias integer {\n"
        "    size = 64; align = 8; signed = false;\n"
        "    map = clock.monotonic.value;\n"
        "} := uint64_clock_t;\n\n"
        "trace {\n"
        "    major = 1;\n"
        "    minor = 8;\n"
        "    byte_order = %s;\n"
        "    packet.header := struct {\n"
        "        uint32_t magic;\n"
        "        uint32_t stream_id;\n"
        "    };\n"
        "};\n\n"
        "env {\n"
        "    tracer_name = \"mrX-xtrace\";\n"
        "    app_name = \"%s\";\n"
        "    os_name = \"%s\";\n"
        "    device_name = \"%s\";\n"
        "    core_name = \"%s\";\n"
        "};\n\n"
        "stream {\n"
        "    id = 0;\n"
        "    event.header := struct {\n"
        "        uint32_t id;\n"
        "        uint64_clock_t timestamp;\n"
        "    };\n"
        "    packet.context := struct {\n"
        "        uint64_clock_t timestamp_begin;\n"
        "        uint64_clock_t timestamp_end;\n"
        "        uint32_t content_size;\n"
        "        uint32_t packet_size;\n"
        "        uint32_t events_discarded;\n"
        "        uint32_t seq;\n"
        "        uint64_hex_t anchor_addr;\n"
        "        uint32_t ts_freq;\n"
        "        uint32_t cpu_freq;\n"
        "        uint32_t ptr_bits;\n"
        "    };\n"
        "};\n\n"
        "event {\n"
        "    name = trace_begin;\n"
        "    id = 0;\n"
        "    stream_id = 0;\n"
        "    fields := struct {\n"
        "        string tag;\n"
        "        string name;\n"
        "        uint8_t argc;\n"
        "        uintptr_t args[argc];\n"
        "    };\n"
        "};\n\n"
        "event {\n"
        "    name = trace_end;\n"
        "    id = 1;\n"
        "    stream_id = 0;\n"
        "    fields := struct {\n"
        "        string tag;\n"
        "        string name;\n"
        "        uint8_t argc;\n"
        "        uintptr_t args[argc];\n"
        "    };\n"
        "};\n\n"
        "event {\n"
        "    name = trace_event;\n"
        "    id = 2;\n"
        "    stream_id = 0;\n"
        "    fields := struct {\n"
        "        string tag;\n"
        "        string name;\n"
        "        uint8_t argc;\n"
        "        uintptr_t args[argc];\n"
        "    };\n"
        "};\n\n"
        "event {\n"
        "    name = trace_counter;\n"
        "    id = 3;\n"
        "    stream_id = 0;\n"
        "    fields := struct {\n"
        "        string tag;\n"
        "        string name;\n"
        "        uint8_t argc;\n"
        "        uintptr_t args[argc];\n"
        "    };\n"
        "};\n",
        ptr_bits, info.ts_freq, byte_order, app_name, os_name,
        device_name, core_name);

    /* Reject incomplete metadata */
    if ((len < 0) || ((mr_size_t)len >= size)) {
        return -MR_ENOMEM;
    }
    return (mr_ssize_t)len;
}

mr_err_t mr_xtrace_ctf_register(void) {
    /* Register CTF protocol */
    return mr_xtrace_register(&__ctf_proto);
}

static void mr_xtrace_ctf_auto_init(void) {
    mr_xtrace_ctf_register();
}
MR_INIT_EXPORT(mr_xtrace_ctf_auto_init, MR_INIT_LEVEL_MODULE);
#endif /* defined(MR_USE_MODULE_XTRACE_PROTO_CTF) */
