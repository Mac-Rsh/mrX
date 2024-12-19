/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_LOG_H__
#define __MR_LOG_H__

#include <mr_config.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Log level definition */
#define MR_LOG_LEVEL_NONE               (0)
#define MR_LOG_LEVEL_ERROR              (1)
#define MR_LOG_LEVEL_WARNING            (2)
#define MR_LOG_LEVEL_INFO               (3)
#define MR_LOG_LEVEL_DEBUG              (4)

/* Log level configuration */
#ifndef MR_USE_LOG
#define MR_CFG_LOG_LEVEL                (MR_LOG_LEVEL_NONE)
#endif /* MR_USE_LOG */
#ifndef MR_CFG_LOG_LEVEL
#define MR_CFG_LOG_LEVEL                (MR_LOG_LEVEL_NONE)
#endif /* MR_CFG_LOG_LEVEL */

/* Error log definition */
#if (MR_CFG_LOG_LEVEL >= MR_LOG_LEVEL_ERROR)
#define MR_LOG_E(_fmt, ...)             printf("[E] "_fmt"\r\n", ##__VA_ARGS__)
#else
#define MR_LOG_E(_fmt, ...)
#endif /* MR_CFG_LOG_LEVEL >= MR_LOG_LEVEL_ERROR */

/* Warning log definition */
#if (MR_CFG_LOG_LEVEL >= MR_LOG_LEVEL_WARNING)
#define MR_LOG_W(_fmt, ...)             printf("[W] "_fmt"\r\n", ##__VA_ARGS__)
#else
#define MR_LOG_W(_fmt, ...)
#endif /* MR_CFG_LOG_LEVEL >= MR_LOG_LEVEL_WARNING */

/* Info log definition */
#if (MR_CFG_LOG_LEVEL >= MR_LOG_LEVEL_INFO)
#define MR_LOG_I(_fmt, ...)             printf("[I] "_fmt"\r\n", ##__VA_ARGS__)
#else
#define MR_LOG_I(_fmt, ...)
#endif /* MR_CFG_LOG_LEVEL >= MR_LOG_LEVEL_INFO */

/* Debug log definition */
#if (MR_CFG_LOG_LEVEL >= MR_LOG_LEVEL_DEBUG)
#define MR_LOG_D(_fmt, ...)             printf("[D] "_fmt"\r\n", ##__VA_ARGS__)
#else
#define MR_LOG_D(_fmt, ...)
#endif /* MR_CFG_LOG_LEVEL >= MR_LOG_LEVEL_DEBUG */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_LOG_H__ */
