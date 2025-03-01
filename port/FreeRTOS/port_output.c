/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include "FreeRTOS.h"
#if defined(FreeRTOS_printf)
#include <port/mr_port_output.h>

int mr_port_output(const char *buf, mr_size_t size) {
    /* Output string */
    return FreeRTOS_printf(("%s", buf));
}
#endif /* defined(FreeRTOS_printf) */
