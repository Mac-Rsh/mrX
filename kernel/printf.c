/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/kernel/mr_printf.h>
#if defined(MR_USE_PRINTF)
#include <mrX/port/mr_port_output.h>
#include <mrX/libc/mr_sprintf.h>
#if defined(MR_USE_PRINTF_SAFE)
#include <mrX/libc/mr_malloc.h>
#endif /* defined(MR_USE_PRINTF_SAFE) */

int mr_vprintf(const char *fmt, mr_va_list args) {
#if !defined(MR_CFG_PRINTF_BUF_SIZE)
#define MR_CFG_PRINTF_BUF_SIZE          (64)
#endif /* !defined(MR_CFG_PRINTF_BUF_SIZE) */
#if !defined(MR_USE_PRINTF_SAFE)
    static char buf[MR_CFG_PRINTF_BUF_SIZE];
#else
    char *buf;
#endif /* !defined(MR_USE_PRINTF_SAFE) */
    int outsz, ret;

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
    ret = mr_vsnprintf(buf, MR_CFG_PRINTF_BUF_SIZE, fmt, args);
    if (ret < 0) {
        ret = -MR_EINVAL;
        goto _exit;
    } else {
        outsz = ret;
    }

    /* Output string */
    ret = mr_port_output(buf, (mr_size_t)outsz);
_exit:
#if defined(MR_USE_PRINTF_SAFE)
    mr_free(buf);
#endif /* defined(MR_USE_PRINTF_SAFE) */
    return ret;
}

int mr_printf(const char *fmt, ...) {
    mr_va_list args;
    int ret;

    /* Check parameter */
    if ((!fmt) || (fmt[0] == '\0')) {
        return -MR_EINVAL;
    }

    /* Print */
    mr_va_start(args, fmt);
    ret = mr_vprintf(fmt, args);
    mr_va_end(args);
    return ret;
}
#endif /* defined(MR_USE_PRINTF) */
