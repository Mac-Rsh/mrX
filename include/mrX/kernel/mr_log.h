/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_LOG_H__
#define __MR_LOG_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_LOG)
#include <mrX/kernel/mr_service.h>
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
#define MR_LOG_LEVEL_ERR                (0)
#define MR_LOG_LEVEL_WRN                (1)
#define MR_LOG_LEVEL_INF                (2)
#define MR_LOG_LEVEL_DBG                (3)
#define MR_LOG_LEVEL_TRC                (4)

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
#define __MR_LOG_FORMAT(_level, _fmt)                                          \
    (" [" _level "/" MR_LOG_TAG "] " _fmt "\r\n")
#define __MR_LOG_0(_level, _level_str, _fmt)                                   \
    mr_log((_level), __MR_LOG_FORMAT(_level_str, _fmt))
#define __MR_LOG_1(_level, _level_str, _fmt, ...)                              \
    mr_log((_level), __MR_LOG_FORMAT(_level_str, _fmt), __VA_ARGS__)
#define __MR_LOG(_level, _level_str, ...)                                      \
    __MR_CAT(__MR_LOG_, __MR_VA_ARGS_HAS_COMMA(__VA_ARGS__))(                  \
        (_level), _level_str, __VA_ARGS__)

/* Log macro definition */
#if defined(MR_LOG_TAG) && (MR_CFG_LOG_LEVEL >= MR_LOG_LEVEL_TRC)
#define MR_LOG_TRC(...)                 __MR_LOG(MR_LOG_LEVEL_TRC, "TRC", __VA_ARGS__)
#else
#define MR_LOG_TRC(...)                 ((void)0)
#endif /* defined(MR_LOG_TAG) && (MR_CFG_LOG_LEVEL >= MR_LOG_LEVEL_TRC) */
#if defined(MR_LOG_TAG) && (MR_CFG_LOG_LEVEL >= MR_LOG_LEVEL_DBG)
#define MR_LOG_DBG(...)                 __MR_LOG(MR_LOG_LEVEL_DBG, "DBG", __VA_ARGS__)
#else
#define MR_LOG_DBG(...)                 ((void)0)
#endif /* defined(MR_LOG_TAG) && (MR_CFG_LOG_LEVEL >= MR_LOG_LEVEL_DBG) */
#if defined(MR_LOG_TAG) && (MR_CFG_LOG_LEVEL >= MR_LOG_LEVEL_INF)
#define MR_LOG_INF(...)                 __MR_LOG(MR_LOG_LEVEL_INF, "INF", __VA_ARGS__)
#else
#define MR_LOG_INF(...)                 ((void)0)
#endif /* (MR_LOG_TAG) && (MR_CFG_LOG_LEVEL >= MR_LOG_LEVEL_INF) */
#if defined(MR_LOG_TAG) && (MR_CFG_LOG_LEVEL >= MR_LOG_LEVEL_WRN)
#define MR_LOG_WRN(...)                 __MR_LOG(MR_LOG_LEVEL_WRN, "WRN", __VA_ARGS__)
#else
#define MR_LOG_WRN(...)                 ((void)0)
#endif /* (MR_LOG_TAG) && (MR_CFG_LOG_LEVEL >= MR_LOG_LEVEL_WRN) */
#if defined(MR_LOG_TAG) && (MR_CFG_LOG_LEVEL >= MR_LOG_LEVEL_ERR)
#define MR_LOG_ERR(...)                 __MR_LOG(MR_LOG_LEVEL_ERR, "ERR", __VA_ARGS__)
#else
#define MR_LOG_ERR(...)                 ((void)0)
#endif /* (MR_LOG_TAG) && (MR_CFG_LOG_LEVEL >= MR_LOG_LEVEL_ERR) */
#endif /* defined(MR_USE_LOG) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_LOG_H__ */
