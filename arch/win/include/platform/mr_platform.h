/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-01-08        MacRsh        First version
 */

#ifndef __MR_PLATFORM_H__
#define __MR_PLATFORM_H__

#include <mrX/mr_config.h>
#include <mrX/libc/mr_errno.h>
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Platform
 * @{
 */

#if defined(MR_USE_MODULE_XTRACE)
/**
 * @brief Register the Windows file transport for xtrace.
 *
 * @param[in] path Output file path, or MR_NULL to use the default path.
 *
 * @return 0 on success, otherwise an error code.
 */
extern mr_err_t mr_win_xtrace_file_register(const char *path);
#endif /* defined(MR_USE_MODULE_XTRACE) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_PLATFORM_H__ */
