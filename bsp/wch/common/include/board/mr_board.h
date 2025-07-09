/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-06-12        MacRsh        First version
 */

#ifndef __MR_BOARD_H__
#define __MR_BOARD_H__

#include <mr_config.h>
#if defined(MR_USE_SERIES_V00X)
#include "ch32v00x.h"
#elif defined(MR_USE_SERIES_V10X)
#include "ch32v10x.h"
#elif defined(MR_USE_SERIES_V20X)
#include "ch32v20x.h"
#elif defined(MR_USE_SERIES_V30X)
#include "ch32v30x.h"
#else
#error "Series not supported"
#endif /* defined(MR_USE_SERIES_V00X) */
#include "core_riscv.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Board
 * @{
 */

/**
 * @brief This function initializes the board systick.
 */
void mr_board_systick_init(void);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_BOARD_H__ */
