/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-11-25        MacRsh        First version
 */

#include <mrX/port/mr_port_trace.h>
#if defined(MR_USE_TRACE)
#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_MODULE_XTRACE)
#include <mrX/kernel/mr_clock.h>
#include <mrX/kernel/mr_initcall.h>
#include <mrX/kernel/mr_service.h>
#include <mrX/libc/mr_atomic.h>
#include <mrX/libc/mr_errno.h>
#include <mrX/libc/mr_string.h>
#include <mrX/modules/xtrace/mr_xtrace.h>
#if defined(MR_USE_DCACHE)
#include <mrX/port/mr_port_cache.h>
#endif /* defined(MR_USE_DCACHE) */

/* STM32 RTT buffer size configuration */
#if !defined(MR_CFG_STM32_XTRACE_RTT_UP_SIZE)
#define MR_CFG_STM32_XTRACE_RTT_UP_SIZE   (1024U)
#endif /* !defined(MR_CFG_STM32_XTRACE_RTT_UP_SIZE) */
#if !defined(MR_CFG_STM32_XTRACE_RTT_DOWN_SIZE)
#define MR_CFG_STM32_XTRACE_RTT_DOWN_SIZE (32U)
#endif /* !defined(MR_CFG_STM32_XTRACE_RTT_DOWN_SIZE) */
#if (MR_CFG_STM32_XTRACE_RTT_UP_SIZE < 64U)
#error "MR_CFG_STM32_XTRACE_RTT_UP_SIZE must be >= 64"
#endif /* (MR_CFG_STM32_XTRACE_RTT_UP_SIZE < 64U) */
#if (MR_CFG_STM32_XTRACE_RTT_DOWN_SIZE < 16U)
#error "MR_CFG_STM32_XTRACE_RTT_DOWN_SIZE must be >= 16"
#endif /* (MR_CFG_STM32_XTRACE_RTT_DOWN_SIZE < 16U) */

/* STM32 RTT transfer chunk size and cache-line alignment */
#define STM32_RTT_CHUNK_MAX             (256U)
#define STM32_RTT_ALIGN                 (32U)
#define STM32_RTT_MODE_NO_BLOCK_SKIP    (0U)

/* STM32 RTT channel layout (SystemView uses channel 1 named "SysView") */
#if defined(MR_USE_MODULE_XTRACE_PROTO_SYSVIEW)
#define STM32_RTT_NUM_UP                (2)
#define STM32_RTT_NUM_DOWN              (2)
#define STM32_RTT_CHAN                  (1U)
#define STM32_RTT_CHAN_NAME             "SysView"
#else
#define STM32_RTT_NUM_UP                (1)
#define STM32_RTT_NUM_DOWN              (1)
#define STM32_RTT_CHAN                  (0U)
#define STM32_RTT_CHAN_NAME             "XTrace"
#endif /* defined(MR_USE_MODULE_XTRACE_PROTO_SYSVIEW) */

/* STM32 Cortex-M core name */
#if defined(__CORTEX_M)
#if (__CORTEX_M == 0U)
#if defined(__VTOR_PRESENT) && (__VTOR_PRESENT == 1U)
#define STM32_XTRACE_CORE_NAME          "Cortex-M0+"
#else
#define STM32_XTRACE_CORE_NAME          "Cortex-M0"
#endif /* defined(__VTOR_PRESENT) && (__VTOR_PRESENT == 1U) */
#elif (__CORTEX_M == 3U)
#define STM32_XTRACE_CORE_NAME          "Cortex-M3"
#elif (__CORTEX_M == 4U)
#define STM32_XTRACE_CORE_NAME          "Cortex-M4"
#elif (__CORTEX_M == 7U)
#define STM32_XTRACE_CORE_NAME          "Cortex-M7"
#elif (__CORTEX_M == 23U)
#define STM32_XTRACE_CORE_NAME          "Cortex-M23"
#elif (__CORTEX_M == 33U)
#define STM32_XTRACE_CORE_NAME          "Cortex-M33"
#elif (__CORTEX_M == 55U)
#define STM32_XTRACE_CORE_NAME          "Cortex-M55"
#else
#define STM32_XTRACE_CORE_NAME          "Cortex-M"
#endif
#else
#define STM32_XTRACE_CORE_NAME          "Cortex-M"
#endif /* defined(__CORTEX_M) */

/* Standard SEGGER RTT buffer descriptor layout */
typedef struct stm32_rtt_buf {
    const char *sName;
    char *pBuffer;
    uint32_t SizeOfBuffer;
    volatile uint32_t WrOff;
    volatile uint32_t RdOff;
    uint32_t Flags;
} stm32_rtt_buf_t;

/* Standard SEGGER RTT control block layout */
typedef struct stm32_rtt_cb {
    char acID[16];
    int32_t MaxNumUpBuffers;
    int32_t MaxNumDownBuffers;
    stm32_rtt_buf_t aUp[STM32_RTT_NUM_UP];
    stm32_rtt_buf_t aDown[STM32_RTT_NUM_DOWN];
} stm32_rtt_cb_t;

/* STM32 RTT xtrace output context type */
typedef struct mr_stm32_xtrace_rtt {
    mr_xtrace_output_t output;
    mr_atomic_t pending;
    mr_atomic_t active;
    mr_bool_t hook_added;
} mr_stm32_xtrace_rtt_t;

/* Exported SEGGER RTT control block for J-Link / OpenOCD / probe-rs / pyOCD */
MR_USED MR_ALIGN(STM32_RTT_ALIGN) stm32_rtt_cb_t _SEGGER_RTT;

/* Static RTT up/down ring buffers */
static MR_ALIGN(STM32_RTT_ALIGN) char
    __stm32_rtt_up_buf[MR_ALIGN_UP(MR_CFG_STM32_XTRACE_RTT_UP_SIZE,
                                   STM32_RTT_ALIGN)];
static MR_ALIGN(STM32_RTT_ALIGN) char
    __stm32_rtt_down_buf[MR_ALIGN_UP(MR_CFG_STM32_XTRACE_RTT_DOWN_SIZE,
                                     STM32_RTT_ALIGN)];

static mr_err_t stm32_rtt_init(void *priv);
static void stm32_rtt_deinit(void *priv);
static void stm32_rtt_notify(void *priv);
static mr_ssize_t stm32_rtt_read(void *priv, void *buf, mr_size_t size);

/* STM32 RTT output operations */
static const mr_xtrace_output_ops_t __stm32_rtt_ops = {
    .notify = stm32_rtt_notify,
    .init = stm32_rtt_init,
    .deinit = stm32_rtt_deinit,
    .read = stm32_rtt_read,
};

/* STM32 RTT output context */
static mr_stm32_xtrace_rtt_t __stm32_xtrace_rtt = {
    .pending = MR_ATOMIC_INIT(0),
    .active = MR_ATOMIC_INIT(0),
    .hook_added = MR_FALSE,
};

static void stm32_rtt_dcache_clean(const void *addr, mr_size_t size) {
#if defined(MR_USE_DCACHE)
    mr_port_dcache_clean(addr, size);
#else
    MR_IGNORE(addr);
    MR_IGNORE(size);
#endif /* defined(MR_USE_DCACHE) */
}

static void stm32_rtt_dcache_clean_invalidate(const void *addr,
                                              mr_size_t size) {
#if defined(MR_USE_DCACHE)
    mr_port_dcache_clean_invalidate(addr, size);
#else
    MR_IGNORE(addr);
    MR_IGNORE(size);
#endif /* defined(MR_USE_DCACHE) */
}

static void stm32_rtt_dcache_invalidate(const void *addr, mr_size_t size) {
#if defined(MR_USE_DCACHE)
    mr_port_dcache_invalidate(addr, size);
#else
    MR_IGNORE(addr);
    MR_IGNORE(size);
#endif /* defined(MR_USE_DCACHE) */
}

static void stm32_rtt_cb_init(void) {
    volatile char *id;

    /* Clear control block first so no partial ID exists in RAM */
    mr_memset(&_SEGGER_RTT, 0, sizeof(_SEGGER_RTT));

    _SEGGER_RTT.MaxNumUpBuffers = STM32_RTT_NUM_UP;
    _SEGGER_RTT.MaxNumDownBuffers = STM32_RTT_NUM_DOWN;

#if defined(MR_USE_MODULE_XTRACE_PROTO_SYSVIEW)
    /* Channel 0 placeholder for standard SEGGER SystemView discovery */
    _SEGGER_RTT.aUp[0].sName = "Terminal";
    _SEGGER_RTT.aUp[0].pBuffer = MR_NULL;
    _SEGGER_RTT.aUp[0].SizeOfBuffer = 0U;
    _SEGGER_RTT.aUp[0].Flags = STM32_RTT_MODE_NO_BLOCK_SKIP;

    _SEGGER_RTT.aDown[0].sName = "Terminal";
    _SEGGER_RTT.aDown[0].pBuffer = MR_NULL;
    _SEGGER_RTT.aDown[0].SizeOfBuffer = 0U;
    _SEGGER_RTT.aDown[0].Flags = STM32_RTT_MODE_NO_BLOCK_SKIP;
#endif /* defined(MR_USE_MODULE_XTRACE_PROTO_SYSVIEW) */

    /* Configure active trace up/down channels */
    _SEGGER_RTT.aUp[STM32_RTT_CHAN].sName = STM32_RTT_CHAN_NAME;
    _SEGGER_RTT.aUp[STM32_RTT_CHAN].pBuffer = __stm32_rtt_up_buf;
    _SEGGER_RTT.aUp[STM32_RTT_CHAN].SizeOfBuffer = MR_CFG_STM32_XTRACE_RTT_UP_SIZE;
    _SEGGER_RTT.aUp[STM32_RTT_CHAN].WrOff = 0U;
    _SEGGER_RTT.aUp[STM32_RTT_CHAN].RdOff = 0U;
    _SEGGER_RTT.aUp[STM32_RTT_CHAN].Flags = STM32_RTT_MODE_NO_BLOCK_SKIP;

    _SEGGER_RTT.aDown[STM32_RTT_CHAN].sName = STM32_RTT_CHAN_NAME;
    _SEGGER_RTT.aDown[STM32_RTT_CHAN].pBuffer = __stm32_rtt_down_buf;
    _SEGGER_RTT.aDown[STM32_RTT_CHAN].SizeOfBuffer =
        MR_CFG_STM32_XTRACE_RTT_DOWN_SIZE;
    _SEGGER_RTT.aDown[STM32_RTT_CHAN].WrOff = 0U;
    _SEGGER_RTT.aDown[STM32_RTT_CHAN].RdOff = 0U;
    _SEGGER_RTT.aDown[STM32_RTT_CHAN].Flags = STM32_RTT_MODE_NO_BLOCK_SKIP;

    /* Commit descriptors before publishing the magic ID */
    __DMB();

    /*
     * Write "SEGGER RTT" byte-by-byte in reverse so the literal string does
     * not appear in Flash (.rodata) and confuse debug probe RAM scanning.
     */
    id = _SEGGER_RTT.acID;
    id[10] = '\0';
    id[9] = 'T';
    id[8] = 'T';
    id[7] = 'R';
    id[6] = ' ';
    id[5] = 'R';
    id[4] = 'E';
    id[3] = 'G';
    id[2] = 'G';
    id[1] = 'E';
    id[0] = 'S';
    __DMB();
    stm32_rtt_dcache_clean(&_SEGGER_RTT, sizeof(_SEGGER_RTT));
}

static mr_size_t stm32_rtt_write_up(const void *data, mr_size_t size) {
    stm32_rtt_buf_t *up;
    uint32_t buf_size, rd, wr, avail, first, second;
    mr_size_t n;
    const mr_uint8_t *src;

    up = &_SEGGER_RTT.aUp[STM32_RTT_CHAN];
    buf_size = up->SizeOfBuffer;
    if ((!data) || (size == 0U) || (buf_size <= 1U)) {
        return 0U;
    }

    /* Refresh host-updated RdOff from SRAM when D-cache is enabled */
    stm32_rtt_dcache_clean_invalidate(up, sizeof(*up));
    rd = up->RdOff;
    wr = up->WrOff;
    if ((rd >= buf_size) || (wr >= buf_size)) {
        return 0U;
    }

    /* Compute free space leaving 1 byte empty to distinguish full vs empty */
    if (rd > wr) {
        avail = rd - wr - 1U;
    } else {
        avail = (buf_size - 1U) - wr + rd;
    }
    if (avail == 0U) {
        return 0U;
    }

    n = MR_MIN(size, (mr_size_t)avail);
    src = (const mr_uint8_t *)data;
    first = (uint32_t)MR_MIN(n, (mr_size_t)(buf_size - wr));
    mr_memcpy(&up->pBuffer[wr], src, first);
    stm32_rtt_dcache_clean(&up->pBuffer[wr], first);

    second = (uint32_t)n - first;
    if (second > 0U) {
        mr_memcpy(&up->pBuffer[0], src + first, second);
        stm32_rtt_dcache_clean(&up->pBuffer[0], second);
        wr = second;
    } else {
        wr += first;
        if (wr >= buf_size) {
            wr = 0U;
        }
    }

    /* Ensure payload is visible before advancing WrOff */
    __DMB();
    up->WrOff = wr;
    __DMB();
    stm32_rtt_dcache_clean(up, sizeof(*up));
    return n;
}

static void stm32_rtt_drain(mr_stm32_xtrace_rtt_t *ctx) {
    mr_size_t claimed, written;
    void *ptr;

    if ((!ctx) || (mr_atomic_load(&ctx->active) == 0)) {
        return;
    }

    /* Drain xtrace FIFO into RTT up buffer without blocking */
    while ((claimed = mr_xtrace_read_claim(&ptr, STM32_RTT_CHUNK_MAX)) > 0U) {
        written = stm32_rtt_write_up(ptr, claimed);
        mr_xtrace_read_finish(written);
        if (written < claimed) {
            mr_atomic_store(&ctx->pending, 1);
            break;
        }
    }
}

static mr_tick_t stm32_rtt_clock_hook(void) {
    mr_stm32_xtrace_rtt_t *ctx;

    ctx = &__stm32_xtrace_rtt;
    if (mr_atomic_load(&ctx->active) == 0) {
        return 0U;
    }

    /* Drain when notified or when residual bytes remain in xtrace FIFO */
    if ((mr_atomic_exchange(&ctx->pending, 0) != 0)
        || (mr_xtrace_pending() > 0U)) {
        stm32_rtt_drain(ctx);
        if (mr_xtrace_pending() > 0U) {
            mr_atomic_store(&ctx->pending, 1);
            return 1U;
        }
    }
    return 0U;
}

static void stm32_rtt_notify(void *priv) {
    mr_stm32_xtrace_rtt_t *ctx;

    ctx = (mr_stm32_xtrace_rtt_t *)priv;
    if (ctx) {
        mr_atomic_store(&ctx->pending, 1);
    }
}

static mr_ssize_t stm32_rtt_read(void *priv, void *buf, mr_size_t size) {
    stm32_rtt_buf_t *down;
    uint32_t buf_size, wr, rd, avail, first, second;
    mr_size_t n;
    mr_uint8_t *dst;

    MR_IGNORE(priv);
    down = &_SEGGER_RTT.aDown[STM32_RTT_CHAN];
    buf_size = down->SizeOfBuffer;
    if ((!buf) || (size == 0U) || (buf_size <= 1U)) {
        return 0;
    }

    /* Refresh host-updated WrOff from SRAM when D-cache is enabled */
    stm32_rtt_dcache_clean_invalidate(down, sizeof(*down));
    wr = down->WrOff;
    rd = down->RdOff;
    if ((wr >= buf_size) || (rd >= buf_size)) {
        return 0;
    }

    if (wr >= rd) {
        avail = wr - rd;
    } else {
        avail = buf_size - rd + wr;
    }
    if (avail == 0U) {
        return 0;
    }

    n = MR_MIN(size, (mr_size_t)avail);
    dst = (mr_uint8_t *)buf;
    first = (uint32_t)MR_MIN(n, (mr_size_t)(buf_size - rd));
    stm32_rtt_dcache_invalidate(&down->pBuffer[rd], first);
    mr_memcpy(dst, &down->pBuffer[rd], first);

    second = (uint32_t)n - first;
    if (second > 0U) {
        stm32_rtt_dcache_invalidate(&down->pBuffer[0], second);
        mr_memcpy(dst + first, &down->pBuffer[0], second);
        rd = second;
    } else {
        rd += first;
        if (rd >= buf_size) {
            rd = 0U;
        }
    }

    /* Ensure payload read completes before advancing RdOff */
    __DMB();
    down->RdOff = rd;
    __DMB();
    stm32_rtt_dcache_clean(down, sizeof(*down));
    return (mr_ssize_t)n;
}

static mr_err_t stm32_rtt_init(void *priv) {
    mr_stm32_xtrace_rtt_t *ctx;
    mr_err_t ret;

    ctx = (mr_stm32_xtrace_rtt_t *)priv;
    if (!ctx) {
        return -MR_EINVAL;
    }

    /* Initialize SEGGER RTT control block */
    stm32_rtt_cb_init();

    /* Attach background clock hook once */
    if (!ctx->hook_added) {
        ret = mr_clock_hook_add(stm32_rtt_clock_hook);
        if (ret != 0) {
            return ret;
        }
        ctx->hook_added = MR_TRUE;
    }

    mr_atomic_store(&ctx->pending, 0);
    mr_atomic_store(&ctx->active, 1);
    return 0;
}

static void stm32_rtt_deinit(void *priv) {
    mr_stm32_xtrace_rtt_t *ctx;

    ctx = (mr_stm32_xtrace_rtt_t *)priv;
    if (!ctx) {
        return;
    }

    /* Final drain before deactivating */
    stm32_rtt_drain(ctx);
    mr_atomic_store(&ctx->active, 0);
    mr_atomic_store(&ctx->pending, 0);
}

mr_err_t mr_stm32_xtrace_rtt_register(void) {
    mr_err_t ret;

    /* Unregister previous output before re-initializing */
    mr_xtrace_output_unregister();
    if (MR_OBJECT_IS_INITED(&__stm32_xtrace_rtt.output)) {
        return -MR_EBUSY;
    }

    /* Initialize RTT output object */
    ret = mr_xtrace_output_init(&__stm32_xtrace_rtt.output, &__stm32_rtt_ops,
                                &__stm32_xtrace_rtt);
    if (ret != 0) {
        return ret;
    }

    /* Register output and hand ownership reference to xtrace */
    ret = mr_xtrace_output_register(&__stm32_xtrace_rtt.output);
    mr_xtrace_output_del(&__stm32_xtrace_rtt.output);
    return ret;
}

void mr_stm32_xtrace_rtt_poll(void) {
    /* Flush any buffered bytes immediately into RTT */
    mr_atomic_store(&__stm32_xtrace_rtt.pending, 0);
    stm32_rtt_drain(&__stm32_xtrace_rtt);
}

static void stm32_xtrace_sysinfo_init(void) {
    mr_xtrace_sysinfo_t info = {
        .device_name = "STM32",
        .core_name = STM32_XTRACE_CORE_NAME,
        .cpu_freq = (mr_uint32_t)SystemCoreClock,
    };

    /* Register STM32 platform trace metadata */
    mr_xtrace_sysinfo_set(&info);
}
MR_INIT_EXPORT(stm32_xtrace_sysinfo_init, MR_INIT_LEVEL_DRIVER);

static void stm32_xtrace_rtt_auto_init(void) {
    /* Register default STM32 RTT output */
    mr_stm32_xtrace_rtt_register();
}
MR_INIT_EXPORT(stm32_xtrace_rtt_auto_init, MR_INIT_LEVEL_MODULE);
#endif /* defined(MR_USE_MODULE_XTRACE) */

void mr_port_trace(mr_uint64_t timestamp, mr_uint32_t type, const char *tag,
                   const char *name, const mr_uintptr_t *args, mr_size_t argc) {
#if defined(MR_USE_MODULE_XTRACE)
    mr_trace_event_t event;

    /* Build trace event */
    event.timestamp = timestamp;
    event.type = type;
    event.tag = tag;
    event.name = name;
    event.args = args;
    event.argc = argc;

    /* Emit trace event to xtrace */
    mr_xtrace_emit(&event);
#else
    MR_IGNORE(timestamp);
    MR_IGNORE(type);
    MR_IGNORE(tag);
    MR_IGNORE(name);
    MR_IGNORE(args);
    MR_IGNORE(argc);
#endif /* defined(MR_USE_MODULE_XTRACE) */
}
#endif /* defined(MR_USE_TRACE) */
