/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mr-X/mr_log.h>
#if defined(MR_USE_LOG)

/* Log level mask */
static mr_uint32_t __level = 0xffffffffU;

void mr_log(mr_uint32_t level, const char *fmt, ...) {
    mr_va_list args;

    /* Check parameter */
    if ((level >= 32) || (!fmt) || (fmt[0] == '\0')) {
        return;
    }

    /* Check if log level is enabled */
    if (!((1U << level) & __level)) {
        /* Output is disabled */
        return;
    }

    /* Printf */
    mr_va_start(args, fmt);
    mr_vprintf(fmt, args);
    mr_va_end(args);
}

void mr_log_mask_set(mr_uint32_t mask) {
    /* Set log level mask */
    __level = mask;
}

mr_uint32_t mr_log_mask(void) {
    /* Get log level mask */
    return __level;
}
#endif /* defined(MR_USE_LOG) */
