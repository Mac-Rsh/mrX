/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_PORT_CACHE_H__
#define __MR_PORT_CACHE_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_DCACHE) || defined(MR_USE_ICACHE)
#include <mrX/libc/mr_errno.h>
#include <mrX/libc/mr_types.h>
#endif /* defined(MR_USE_DCACHE) || defined(MR_USE_ICACHE) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Port-cache
 * @{
 */

#if defined(MR_USE_DCACHE)
/**
 * @brief This function enables the data cache.
 *
 * @return 0 on success, a negative error code on failure.
 *
 * @note Platform implementation.
 */
extern mr_err_t mr_port_dcache_enable(void);

/**
 * @brief This function disables the data cache.
 *
 * @return 0 on success, a negative error code on failure.
 *
 * @note Platform implementation.
 */
extern mr_err_t mr_port_dcache_disable(void);

/**
 * @brief This function gets the data-cache line size.
 *
 * @return The line size in bytes, or 0 if no data cache exists.
 *
 * @note Platform implementation.
 */
extern mr_size_t mr_port_dcache_line_size(void);

/**
 * @brief This function cleans a data-cache range.
 *
 * @param addr The start address of the range.
 * @param size The range size in bytes.
 * @return 0 on success, a negative error code on failure.
 *
 * @note Platform implementation.
 */
extern mr_err_t mr_port_dcache_clean(const void *addr, mr_size_t size);

/**
 * @brief This function invalidates a data-cache range.
 *
 * @param addr The start address of the range.
 * @param size The range size in bytes.
 * @return 0 on success, a negative error code on failure.
 *
 * @note Platform implementation.
 */
extern mr_err_t mr_port_dcache_invalidate(const void *addr, mr_size_t size);

/**
 * @brief This function cleans and invalidates a data-cache range.
 *
 * @param addr The start address of the range.
 * @param size The range size in bytes.
 * @return 0 on success, a negative error code on failure.
 *
 * @note Platform implementation.
 */
extern mr_err_t mr_port_dcache_clean_invalidate(const void *addr,
                                                mr_size_t size);

/**
 * @brief This function cleans the entire data cache.
 *
 * @return 0 on success, a negative error code on failure.
 *
 * @note Platform implementation.
 */
extern mr_err_t mr_port_dcache_clean_all(void);

/**
 * @brief This function invalidates the entire data cache.
 *
 * @return 0 on success, a negative error code on failure.
 *
 * @note All affected dirty data must be disposable.
 * @note Platform implementation.
 */
extern mr_err_t mr_port_dcache_invalidate_all(void);

/**
 * @brief This function cleans and invalidates the entire data cache.
 *
 * @return 0 on success, a negative error code on failure.
 *
 * @note Platform implementation.
 */
extern mr_err_t mr_port_dcache_clean_invalidate_all(void);
#endif /* defined(MR_USE_DCACHE) */

#if defined(MR_USE_ICACHE)
/**
 * @brief This function enables the instruction cache.
 *
 * @return 0 on success, a negative error code on failure.
 *
 * @note Platform implementation.
 */
extern mr_err_t mr_port_icache_enable(void);

/**
 * @brief This function disables the instruction cache.
 *
 * @return 0 on success, a negative error code on failure.
 *
 * @note Platform implementation.
 */
extern mr_err_t mr_port_icache_disable(void);

/**
 * @brief This function gets the instruction-cache line size.
 *
 * @return The line size in bytes, or 0 if no instruction cache exists.
 *
 * @note Platform implementation.
 */
extern mr_size_t mr_port_icache_line_size(void);

/**
 * @brief This function invalidates an instruction-cache range.
 *
 * @param addr The start address of the range.
 * @param size The range size in bytes.
 * @return 0 on success, a negative error code on failure.
 *
 * @note Platform implementation.
 */
extern mr_err_t mr_port_icache_invalidate(const void *addr, mr_size_t size);

/**
 * @brief This function invalidates the entire instruction cache.
 *
 * @return 0 on success, a negative error code on failure.
 *
 * @note Platform implementation.
 */
extern mr_err_t mr_port_icache_invalidate_all(void);
#endif /* defined(MR_USE_ICACHE) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_PORT_CACHE_H__ */
