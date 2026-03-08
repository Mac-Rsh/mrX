/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-11-12        MacRsh        First version
 */

#ifndef __MR_PLATFORM_H__
#define __MR_PLATFORM_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_WCH_SERIES_V00X)
#include "ch32v00x.h"
#elif defined(MR_USE_WCH_SERIES_V10X)
#include "ch32v10x.h"
#elif defined(MR_USE_WCH_SERIES_V20X)
#include "ch32v20x.h"
#elif defined(MR_USE_WCH_SERIES_V30X)
#include "ch32v30x.h"
#else
#error "Series not supported, please select a series"
#endif /* defined(MR_USE_WCH_SERIES_V00X) */
#include "core_riscv.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Platform
 * @{
 */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_PLATFORM_H__ */
