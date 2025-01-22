/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_HW_IRQ_H__
#define __MR_HW_IRQ_H__

#ifdef __cplusplus
"C" {
#endif /* __cplusplus */

/**
 * @addtogroup IRQ hardware interface
 * @{
 */

/**
 * @brief This function saves the current interrupt mask.
 *
 * @return The current interrupt mask.
 */
int mr_hw_irq_save(void);

/**
 * @brief This function restores the interrupt mask.
 *
 * @param mask The interrupt mask to restore.
 */
void mr_hw_irq_restore(int mask);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_HW_IRQ_H__ */
