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

int mr_kprintf(const char *fmt, ...) {
#if !defined(MR_CFG_KPRINTF_BUF_SIZE)
#define MR_CFG_KPRINTF_BUF_SIZE         (128)
#endif /* !defined(MR_CFG_KPRINTF_BUF_SIZE) */
    char buf[MR_CFG_KPRINTF_BUF_SIZE];
    mr_va_list args;
    int ret;

    /* Check arguments */
    if (!fmt) {
        return -MR_EINVAL;
    }

    /* Format string */
    mr_va_start(args, fmt);
    ret = mr_vsnprintf(buf, sizeof(buf), fmt, args);
    mr_va_end(args);

    /* Output string */
    return mr_port_output(buf, ret);
}
#endif /* defined(MR_USE_KPRINTF) */
