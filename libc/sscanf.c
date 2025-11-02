/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mr-X/libc/mr_sscanf.h>
#if defined(MR_USE_SSCANF)
#include <stdarg.h>
#include <stdio.h>

int mr_sscanf(const char *buf, const char *fmt, ...) {
    va_list args;
    int ret;

    /* Check parameter */
    if ((!buf) || (!fmt)) {
        return -1;
    }

    /* Scan string */
    va_start(args, fmt);
    ret = vsscanf(buf, fmt, args);
    va_end(args);
    return ret;
}
#endif /* defined(MR_USE_SSCANF) */
