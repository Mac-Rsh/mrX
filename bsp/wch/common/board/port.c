/**
 * @copyright (c) 2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2025-06-11        MacRsh        First version
 */

#include <board/mr_board.h>
#include <mr_x.h>

static volatile int __irq_nest = 0;

void mr_port_assert(const char *file, int line, const char *expr) {
    MR_UNUSED(file);
    MR_UNUSED(line);
    MR_UNUSED(expr);
}

int mr_port_output(const char *buf, mr_size_t size) {
    MR_UNUSED(buf);
    MR_UNUSED(size);
    return 0;
}

int mr_port_irq_disable(void) {
    /* Disable interrupt */
    __disable_irq();

    /* Increment irq nest */
    return (++__irq_nest);
}

void mr_port_irq_enable(int mask) {
    MR_UNUSED(mask);

    /* Decrement irq nest */
    if ((--__irq_nest) > 0) {
        return;
    }

    /* Enable interrupt */
    __enable_irq();
}
