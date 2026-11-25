/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_TRACE_H__
#define __MR_TRACE_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_TRACE)
#include <mrX/kernel/mr_service.h>
#endif /* defined(MR_USE_TRACE) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Trace
 * @{
 */

#if defined(MR_USE_TRACE)
/* Trace type mask definition */
#define MR_TRACE_MASK(_type)            (1U << (_type))

/* Trace type definition */
#define MR_TRACE_TYPE_BEGIN             (0U)
#define MR_TRACE_TYPE_END               (1U)
#define MR_TRACE_TYPE_EVENT             (2U)
#define MR_TRACE_TYPE_COUNTER           (3U)

/* Trace event type */
typedef struct mr_trace_event {
    mr_uint64_t timestamp;
    mr_uint32_t type;
    const char *tag;
    const char *name;
    const mr_uintptr_t *args;
    mr_size_t argc;
} mr_trace_event_t;

/**
 * @brief This function emits a trace event.
 *
 * @param type The trace event type.
 * @param tag The trace tag.
 * @param name The trace event name.
 * @param argc The number of arguments.
 * @param ... The arguments (each of type mr_uintptr_t).
 */
void mr_trace(mr_uint32_t type, const char *tag, const char *name,
              mr_size_t argc, ...);

/**
 * @brief This function sets the trace type mask.
 *
 * @param mask The trace type mask.
 */
void mr_trace_mask_set(mr_uint32_t mask);

/**
 * @brief This function gets the trace type mask.
 *
 * @return The trace type mask.
 */
mr_uint32_t mr_trace_mask(void);
#endif /* defined(MR_USE_TRACE) */

/* Trace macro definition */
#if defined(MR_USE_TRACE) && defined(MR_TRACE_TAG)
#define __MR_TRACE_ARGS_0()
#define __MR_TRACE_ARGS_1(_a0)          , (mr_uintptr_t)(_a0)
#define __MR_TRACE_ARGS_2(_a0, ...)                                            \
    , (mr_uintptr_t)(_a0) __MR_TRACE_ARGS_1(__VA_ARGS__)
#define __MR_TRACE_ARGS_3(_a0, ...)                                            \
    , (mr_uintptr_t)(_a0) __MR_TRACE_ARGS_2(__VA_ARGS__)
#define __MR_TRACE_ARGS_4(_a0, ...)                                            \
    , (mr_uintptr_t)(_a0) __MR_TRACE_ARGS_3(__VA_ARGS__)
#define __MR_TRACE_ARGS_5(_a0, ...)                                            \
    , (mr_uintptr_t)(_a0) __MR_TRACE_ARGS_4(__VA_ARGS__)
#define __MR_TRACE_ARGS_6(_a0, ...)                                            \
    , (mr_uintptr_t)(_a0) __MR_TRACE_ARGS_5(__VA_ARGS__)
#define __MR_TRACE_ARGS_7(_a0, ...)                                            \
    , (mr_uintptr_t)(_a0) __MR_TRACE_ARGS_6(__VA_ARGS__)
#define __MR_TRACE_ARGS_8(_a0, ...)                                            \
    , (mr_uintptr_t)(_a0) __MR_TRACE_ARGS_7(__VA_ARGS__)
#define __MR_TRACE_0(_type, _name)                                             \
    mr_trace((_type), MR_TRACE_TAG, (_name), 0U)
#define __MR_TRACE_1(_type, _name, ...)                                        \
    mr_trace((_type), MR_TRACE_TAG, (_name),                                   \
             (mr_size_t)MR_VA_ARGS_NUM(__VA_ARGS__)                            \
                 MR_CAT(__MR_TRACE_ARGS_,                                      \
                        MR_VA_ARGS_NUM(__VA_ARGS__))(__VA_ARGS__))
#define __MR_TRACE(_type, ...)                                                 \
    __MR_CAT(__MR_TRACE_, __MR_VA_ARGS_HAS_COMMA(__VA_ARGS__))(                \
        (_type), __VA_ARGS__)

/**
 * @brief This macro traces the beginning of a span.
 *
 * @param _name The event name.
 * @param ... Optional arguments (0 to 8).
 */
#define MR_TRACE_BEGIN(...)             __MR_TRACE(MR_TRACE_TYPE_BEGIN, __VA_ARGS__)

/**
 * @brief This macro traces the end of a span.
 *
 * @param _name The event name.
 * @param ... Optional arguments (0 to 8).
 */
#define MR_TRACE_END(...)               __MR_TRACE(MR_TRACE_TYPE_END, __VA_ARGS__)

/**
 * @brief This macro traces an instant event.
 *
 * @param _name The event name.
 * @param ... Optional arguments (0 to 8).
 */
#define MR_TRACE_EVENT(...)             __MR_TRACE(MR_TRACE_TYPE_EVENT, __VA_ARGS__)

/**
 * @brief This macro traces a counter value sample.
 *
 * @param _name The counter name.
 * @param _val The counter value.
 */
#define MR_TRACE_COUNTER(_name, _val)                                          \
    mr_trace(MR_TRACE_TYPE_COUNTER, MR_TRACE_TAG, (_name), 1U,                 \
             (mr_uintptr_t)(mr_intptr_t)(_val))
#else
#define MR_TRACE_BEGIN(...)             ((void)0)
#define MR_TRACE_END(...)               ((void)0)
#define MR_TRACE_EVENT(...)             ((void)0)
#define MR_TRACE_COUNTER(_name, _val)   ((void)0)
#endif /* defined(MR_USE_TRACE) && defined(MR_TRACE_TAG) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_TRACE_H__ */
