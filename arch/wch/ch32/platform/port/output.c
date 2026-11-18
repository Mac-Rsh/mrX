/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#include <mrX/port/mr_port_output.h>
#include <mrX/port/mr_port_irq.h>

#if defined(MR_USE_CH32_DEBUG_OUTPUT)
#include "debug.h"
extern int _write(int fd, char *buf, int size);
#endif /* defined(MR_USE_CH32_DEBUG_OUTPUT) */

/* Output busy flag */
static mr_bool_t __output_busy;

MR_WEAK int mr_ch32_platform_output(const char *buf, mr_size_t size) {
#if defined(MR_USE_CH32_DEBUG_OUTPUT)
#if (SDI_PRINT == SDI_PR_OPEN)
    int count, written, ret, i;
    char packet[7];

    written = 0;
    while ((mr_size_t)written < size) {
        count = (int)(size - (mr_size_t)written);
        if (count > (int)sizeof(packet)) {
            count = (int)sizeof(packet);
        }
        /* Older EVT SDI writers read all seven bytes of the final packet */
        for (i = 0; i < (int)sizeof(packet); i++) {
            packet[i] = (i < count) ? buf[written + i] : 0;
        }
        ret = _write(1, packet, count);
        if (ret < 0) {
            return (written != 0) ? written : ret;
        }
        written += ret;
        if (ret != count) {
            break;
        }
    }
    return written;
#else
    /* The EVT syscall takes a mutable pointer but reads the buffer only */
    return _write(1, (char *)buf, (int)size);
#endif /* (SDI_PRINT == SDI_PR_OPEN) */
#else
    /* Output unsupported */
    MR_IGNORE(buf);
    MR_IGNORE(size);
    return -MR_ENOSYS;
#endif /* defined(MR_USE_CH32_DEBUG_OUTPUT) */
}

int mr_port_output(const char *buf, mr_size_t size) {
    int mask, ret;

    /* Check parameter */
    if ((!buf && (size != 0U)) || (size > (mr_size_t)MR_INT32_MAX)) {
        return -MR_EINVAL;
    }
    if (size == 0U) {
        return 0;
    }

    /* Prevent interleaving without masking interrupts during polled writes */
    mask = mr_port_irq_disable();
    if (__output_busy) {
        mr_port_irq_enable(mask);
        return -MR_EBUSY;
    }
    __output_busy = MR_TRUE;
    mr_port_irq_enable(mask);

    /* Output data */
    ret = mr_ch32_platform_output(buf, size);

    /* Clear output busy flag */
    mask = mr_port_irq_disable();
    __output_busy = MR_FALSE;
    mr_port_irq_enable(mask);
    return ret;
}
