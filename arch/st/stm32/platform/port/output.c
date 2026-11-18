/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#include <mrX/port/mr_port_output.h>

int mr_port_output(const char *buf, mr_size_t size) {
#if defined(ITM)
    mr_size_t i;
#endif /* defined(ITM) */

    /* Check parameter */
    if (!buf) {
        return -1;
    }

#if defined(ITM)
    /* Output data */
    for (i = 0U; i < size; i++) {
        ITM_SendChar((uint32_t)(uint8_t)buf[i]);
    }
    return (int)size;
#else
    /* Output unsupported */
    MR_IGNORE(size);
    return -1;
#endif /* defined(ITM) */
}
