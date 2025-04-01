/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/mr_kprintf.h>
#if defined(MR_USE_KPRINTF)
#include <libc/mr_printf.h>
#include <port/mr_port_output.h>
#if defined(MR_USE_KPRINTF_SAFE)
#include <libc/mr_malloc.h>
#endif /* defined(MR_USE_KPRINTF_SAFE) */

int mr_kprintf(const char *fmt, ...) {
#if !defined(MR_CFG_KPRINTF_BUF_SIZE)
#define MR_CFG_KPRINTF_BUF_SIZE         (64)
#endif /* !defined(MR_CFG_KPRINTF_BUF_SIZE) */
#if !defined(MR_USE_KPRINTF_SAFE)
    static char buf[MR_CFG_KPRINTF_BUF_SIZE];
#else
    char *buf;
#endif /* !defined(MR_USE_KPRINTF_SAFE) */
    mr_size_t output_size;
    mr_va_list args;
    int ret;

    /* Check arguments */
    if (!fmt) {
        return -MR_EINVAL;
    }

#if defined(MR_USE_KPRINTF_SAFE)
    /* Allocate buffer */
    buf = mr_malloc(MR_CFG_KPRINTF_BUF_SIZE);
    if (!buf) {
        return -MR_ENOMEM;
    }
#endif /* defined(MR_USE_KPRINTF_SAFE) */

    /* Format string */
    mr_va_start(args, fmt);
    ret = mr_vsnprintf(buf, MR_CFG_KPRINTF_BUF_SIZE, fmt, args);
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
#if defined(MR_USE_KPRINTF_SAFE)
    mr_free(buf);
#endif /* defined(MR_USE_KPRINTF_SAFE) */
    return ret;
}
#endif /* defined(MR_USE_KPRINTF) */
