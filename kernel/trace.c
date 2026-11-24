/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/kernel/mr_trace.h>
#if defined(MR_USE_TRACE)
#include <mrX/port/mr_port_trace.h>
#include <mrX/kernel/mr_clock.h>
#include <mrX/libc/mr_atomic.h>

/* Trace type mask */
static mr_atomic_t __trace_mask = MR_ATOMIC_INIT((mr_atomic_t)0xffffffffU);

void mr_trace(mr_uint32_t type, const char *tag, const char *name,
              mr_size_t argc, ...) {
#if !defined(MR_CFG_TRACE_ARGS_MAX)
#define MR_CFG_TRACE_ARGS_MAX           (8U)
#endif /* !defined(MR_CFG_TRACE_ARGS_MAX) */
#if (MR_CFG_TRACE_ARGS_MAX < 4U)
#error "MR_CFG_TRACE_ARGS_MAX must be at least 1"
#endif /* (MR_CFG_TRACE_ARGS_MAX < 4U) */
    mr_uintptr_t args[MR_CFG_TRACE_ARGS_MAX];
    mr_uint64_t timestamp;
    va_list ap;
    mr_size_t i;

    /* Check parameter */
    if ((type >= 32U) || (!tag) || (tag[0] == '\0') || (!name)
        || (name[0] == '\0')) {
        return;
    }

    /* Check whether trace type is enabled */
    if (!((1U << type) & (mr_uint32_t)mr_atomic_load(&__trace_mask))) {
        /* Trace is disabled */
        return;
    }

    /* Capture trace timestamp */
#if defined(MR_USE_CLOCK_HIRES)
    timestamp = (mr_uint64_t)mr_clock_hires_tick();
#else
    timestamp = (mr_uint64_t)mr_clock_tick();
#endif /* defined(MR_USE_CLOCK_HIRES) */

    /* Collect variadic arguments */
    if (argc > MR_CFG_TRACE_ARGS_MAX) {
        argc = MR_CFG_TRACE_ARGS_MAX;
    }
    if (argc > 0U) {
        va_start(ap, argc);
        for (i = 0U; i < argc; i++) {
            args[i] = va_arg(ap, mr_uintptr_t);
        }
        va_end(ap);
    }

    /* Output trace event */
    mr_port_trace(timestamp, type, tag, name, (argc > 0U) ? args : MR_NULL,
                  argc);
}

void mr_trace_mask_set(mr_uint32_t mask) {
    /* Set trace type mask */
    mr_atomic_store(&__trace_mask, (mr_atomic_t)mask);
}

mr_uint32_t mr_trace_mask(void) {
    /* Get trace type mask */
    return (mr_uint32_t)mr_atomic_load(&__trace_mask);
}
#endif /* defined(MR_USE_TRACE) */
