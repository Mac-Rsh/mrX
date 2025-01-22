/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <libc/mr_printfk.h>
#if (!defined(MR_USE_LIBC_PRINTFK)) && (!defined(MR_USE_3PARTY_PRINTFK))
#include <stdio.h>

int mr_printfk(const char *fmt, ...) {
    mr_va_list args;
    int ret;

    mr_va_start(args, fmt);
    ret = vprintf(fmt, args);
    mr_va_end(args);
    return ret;
}

int mr_snprintfk(char *buf, mr_size_t size, const char *fmt, ...) {
    mr_va_list args;
    int ret;

    mr_va_start(args, fmt);
    ret = vsnprintf(buf, size, fmt, args);
    mr_va_end(args);
    return ret;
}
#endif /* (!defined(MR_USE_LIBC_PRINTFK)) && (!defined(MR_USE_3PARTY_PRINTFK)) */
