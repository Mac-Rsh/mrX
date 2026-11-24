/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_PORT_TRACE_H__
#define __MR_PORT_TRACE_H__

#include <mrX/libc/mr_types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Port-trace
 * @{
 */

/**
 * @brief This function outputs a trace event.
 *
 * @param timestamp The event timestamp.
 * @param type The trace event type.
 * @param tag The trace tag.
 * @param name The trace event name.
 * @param args The argument array.
 * @param argc The number of arguments.
 *
 * @note Platform implementation.
 */
extern void mr_port_trace(mr_uint64_t timestamp, mr_uint32_t type,
                          const char *tag, const char *name,
                          const mr_uintptr_t *args, mr_size_t argc);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_PORT_TRACE_H__ */
