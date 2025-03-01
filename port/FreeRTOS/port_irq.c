/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include "FreeRTOS.h"
#include "task.h"
#include <port/mr_port_irq.h>

int mr_port_irq_save(void) {
    /* Enter critical */
    taskENTER_CRITICAL();
    return 0;
}

void mr_port_irq_restore(int level) {
    /* Exit critical */
    taskEXIT_CRITICAL();
}
