/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/mr_printf.h>
#if defined(MR_USE_PRINTF)
#include <port/mr_port_output.h>
#include <libc/mr_sprintf.h>
#if defined(MR_USE_PRINTF_SAFE)
#include <libc/mr_malloc.h>
#endif /* defined(MR_USE_PRINTF_SAFE) */

int mr_printf(const char *fmt, ...) {
#if !defined(MR_CFG_PRINTF_BUF_SIZE)
#define MR_CFG_PRINTF_BUF_SIZE          (64)
#endif /* !defined(MR_CFG_PRINTF_BUF_SIZE) */
#if !defined(MR_USE_PRINTF_SAFE)
    static char buf[MR_CFG_PRINTF_BUF_SIZE];
#else
    char *buf;
#endif /* !defined(MR_USE_PRINTF_SAFE) */
    mr_size_t output_size;
    mr_va_list args;
    int ret;

    /* Check parameter */
    if ((!fmt) || (fmt[0] == '\0')) {
        return -MR_EINVAL;
    }

#if defined(MR_USE_PRINTF_SAFE)
    /* Allocate buffer */
    buf = mr_malloc(MR_CFG_PRINTF_BUF_SIZE);
    if (!buf) {
        return -MR_ENOMEM;
    }
#endif /* defined(MR_USE_PRINTF_SAFE) */

    /* Format string */
    mr_va_start(args, fmt);
    ret = mr_vsnprintf(buf, MR_CFG_PRINTF_BUF_SIZE, fmt, args);
    mr_va_end(args);
    if (ret < 0) {
        ret = -MR_EINVAL;
        goto _exit;
    } else {
        output_size = (mr_size_t)ret;
    }

    /* Output string */
    ret = mr_port_output(buf, output_size);
_exit:
#if defined(MR_USE_PRINTF_SAFE)
    mr_free(buf);
#endif /* defined(MR_USE_PRINTF_SAFE) */
    return ret;
}
#endif /* defined(MR_USE_PRINTF) */
