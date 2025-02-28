/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_PORT_IRQ_H__
#define __MR_PORT_IRQ_H__

#ifdef __cplusplus
"C" {
#endif /* __cplusplus */

/**
 * @addtogroup Port-IRQ
 * @{
 */

/**
 * @brief This function saves the current interrupt mask.
 *
 * @return The current interrupt mask.
 *
 * @note User implementation.
 * @note Need to clear the hardware interrupt mask.
 */
extern int mr_port_irq_save(void);

/**
 * @brief This function restores the interrupt mask.
 *
 * @param mask The interrupt mask to restore.
 *
 * @note User implementation.
 */
extern void mr_port_irq_restore(int mask);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_PORT_IRQ_H__ */
