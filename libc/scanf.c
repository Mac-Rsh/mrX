/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <libc/mr_scanf.h>
#if defined(MR_USE_SCANF)
#include <stdarg.h>
#include <stdio.h>

int mr_sscanf(const char *buf, const char *fmt, ...) {
    va_list args;
    int ret;

    /* Check parameter */
    if ((!buf) || (!fmt) || (fmt[0] == '\0')) {
        return -1;
    }

    /* Scan string */
    va_start(args, fmt);
    ret = vsscanf(buf, fmt, args);
    va_end(args);
    return ret;
}
#endif /* defined(MR_USE_SCANF) */
