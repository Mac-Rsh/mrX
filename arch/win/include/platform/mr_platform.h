/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-01-08        MacRsh        First version
 * @date 2026-11-25        MacRsh        Add Winsock2 & simulation drivers
 */

#ifndef __MR_PLATFORM_H__
#define __MR_PLATFORM_H__

#include <mrX/mr_config.h>
#include <mrX/libc/mr_errno.h>
#if defined(MR_USE_WIN_UART_TCP)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif /* WIN32_LEAN_AND_MEAN */
#include <winsock2.h>
#include <ws2tcpip.h>
#if defined(_MSC_VER)
#pragma comment(lib, "ws2_32.lib")
#endif /* defined(_MSC_VER) */
#endif /* defined(MR_USE_WIN_UART_TCP) */
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

#include <mrX/platform/mr_win_sim.h>

#endif /* __MR_PLATFORM_H__ */
