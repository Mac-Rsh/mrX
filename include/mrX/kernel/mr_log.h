/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_LOG_H__
#define __MR_LOG_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_LOG)
#include <mrX/libc/mr_types.h>
#endif /* defined(MR_USE_LOG) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Log
 * @{
 */

#if defined(MR_USE_LOG)
/* Log level mask definition */
#define MR_LOG_MASK(_level)             (1U << (_level))

/* Log level definition */
typedef enum mr_log_level {
    MR_LOG_LEVEL_ERR,
    MR_LOG_LEVEL_WRN,
    MR_LOG_LEVEL_INF,
    MR_LOG_LEVEL_DBG,
    MR_LOG_LEVEL_TRC,
} mr_log_level_t;

/* Log default level definition */
#if !defined(MR_CFG_LOG_LEVEL)
#define MR_CFG_LOG_LEVEL                MR_LOG_LEVEL_TRC
#endif /* !defined(MR_CFG_LOG_LEVEL) */

/**
 * @brief This function logs a message.
 *
 * @param level The log level.
 * @param fmt The format string.
 * @param ... The arguments.
 */
void mr_log(mr_uint32_t level, const char *fmt, ...);

/**
 * @brief This function sets the log level mask.
 *
 * @param mask The log level mask.
 */
void mr_log_mask_set(mr_uint32_t mask);

/**
 * @brief This function gets the log level mask.
 *
 * @return The log level mask.
 */
mr_uint32_t mr_log_mask(void);

/* Log format macro definition */
#define __MR_LOG_FORMAT(_color, _level, _fmt)                                  \
    (_color " [" _level "/" MR_LOG_TAG "] " _fmt MR_COLOR_RESET "\n")

/* Log macro definition */
#if defined(MR_LOG_TAG) && (MR_CFG_LOG_LEVEL >= MR_LOG_LEVEL_TRC)
#define MR_LOG_TRC(_fmt, ...)                                                  \
    mr_log(MR_LOG_LEVEL_TRC, __MR_LOG_FORMAT(MR_COLOR_BLACK, "TRC", _fmt),     \
           ##__VA_ARGS__)
#else
#define MR_LOG_TRC(_fmt, ...)           ((void)0)
#endif /* defined(MR_LOG_TAG) && (MR_CFG_LOG_LEVEL >= MR_LOG_LEVEL_TRC) */
#if defined(MR_LOG_TAG) && (MR_CFG_LOG_LEVEL >= MR_LOG_LEVEL_DBG)
#define MR_LOG_DBG(_fmt, ...)                                                  \
    mr_log(MR_LOG_LEVEL_DBG, __MR_LOG_FORMAT(MR_COLOR_BLUE, "DBG", _fmt),      \
           ##__VA_ARGS__)
#else
#define MR_LOG_DBG(_fmt, ...)           ((void)0)
#endif /* defined(MR_LOG_TAG) && (MR_CFG_LOG_LEVEL >= MR_LOG_LEVEL_DBG) */
#if defined(MR_LOG_TAG) && (MR_CFG_LOG_LEVEL >= MR_LOG_LEVEL_INF)
#define MR_LOG_INF(_fmt, ...)                                                  \
    mr_log(MR_LOG_LEVEL_INF, __MR_LOG_FORMAT(MR_COLOR_GREEN, "INF", _fmt),     \
           ##__VA_ARGS__)
#else
#define MR_LOG_INF(_fmt, ...)           ((void)0)
#endif /* (MR_LOG_TAG) && (MR_CFG_LOG_LEVEL >= MR_LOG_LEVEL_INF) */
#if defined(MR_LOG_TAG) && (MR_CFG_LOG_LEVEL >= MR_LOG_LEVEL_WRN)
#define MR_LOG_WRN(_fmt, ...)                                                  \
    mr_log(MR_LOG_LEVEL_WRN, __MR_LOG_FORMAT(MR_COLOR_YELLOW, "WRN", _fmt),    \
           ##__VA_ARGS__)
#else
#define MR_LOG_WRN(_fmt, ...)           ((void)0)
#endif /* (MR_LOG_TAG) && (MR_CFG_LOG_LEVEL >= MR_LOG_LEVEL_WRN) */
#if defined(MR_LOG_TAG) && (MR_CFG_LOG_LEVEL >= MR_LOG_LEVEL_ERR)
#define MR_LOG_ERR(_fmt, ...)                                                  \
    mr_log(MR_LOG_LEVEL_ERR, __MR_LOG_FORMAT(MR_COLOR_RED, "ERR", _fmt),       \
           ##__VA_ARGS__)
#else
#define MR_LOG_ERR(_fmt, ...)           ((void)0)
#endif /* (MR_LOG_TAG) && (MR_CFG_LOG_LEVEL >= MR_LOG_LEVEL_ERR) */
#endif /* defined(MR_USE_LOG) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_LOG_H__ */
