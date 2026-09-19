/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 */

#include <mrX/platform/mr_platform.h>
#include <mrX/port/mr_port_output.h>

int mr_port_output(const char *buf, mr_size_t size) {
    mr_size_t i;

    if (!buf) {
        return -1;
    }

    /* Write output through the Cortex-M ITM stimulus port */
    for (i = 0U; i < size; i++) {
        (void)ITM_SendChar((uint32_t)(uint8_t)buf[i]);
    }
    return (int)size;
}
