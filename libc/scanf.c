/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <libc/mr_scanf.h>
#if (!defined(MR_USE_LIBC_SCANF)) && (!defined(MR_USE_3PARTY_SCANF))
#include <libc/mr_types.h>
#include <stdio.h>

int mr_sscanf(const char *buf, const char *fmt, ...) {
    mr_va_list args;
    int ret;

    /* Scan string */
    mr_va_start(args, fmt);
    ret = vsscanf(buf, fmt, args);
    mr_va_end(args);
    return ret;
}
#endif /* !defined(MR_USE_LIBC_SCANF) && !defined(MR_USE_3PARTY_SCANF) */
