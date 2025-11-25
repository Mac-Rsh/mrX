/**
* @copyright (c) 2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2025-11-11        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>

int mr_port_irq_disable(void) {
    int mask;

#if defined(MR_USE_WCH_SERIES_V00X)
    asm("csrrw %0, mstatus, %1":"=r"(mask):"r"(0x1880));
#elif defined(MR_USE_WCH_SERIES_V10X)
    asm("csrrw %0, mstatus, %1":"=r"(mask):"r"(0x1800));
#elif defined(MR_USE_WCH_SERIES_V20X)
    asm("csrrw %0, mstatus, %1":"=r"(mask):"r"(0x1800));
#elif defined(MR_USE_WCH_SERIES_V30X)
    asm("csrrw %0, mstatus, %1":"=r"(mask):"r"(0x7800));
#else
#error "Series not supported, please select a series"
#endif /* defined(MR_USE_WCH_SERIES_V00X) */
    return mask;
}

void mr_port_irq_enable(int mask) {
    asm("csrw mstatus, %0": :"r"(mask));
}
