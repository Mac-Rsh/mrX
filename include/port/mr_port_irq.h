/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_PORT_IRQ_H__
#define __MR_PORT_IRQ_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Port-IRQ
 * @{
 */

/**
 * @brief This function disables the interrupt.
 *
 * @return The current interrupt mask or nesting level.
 *
 * @note User implementation.
 * @note Need support nesting.
 */
extern int mr_port_irq_disable(void);

/**
 * @brief This function enables the interrupt.
 *
 * @param mask The interrupt mask or nesting level.
 *
 * @note User implementation.
 * @note Need support nesting.
 */
extern void mr_port_irq_enable(int mask);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_PORT_IRQ_H__ */
