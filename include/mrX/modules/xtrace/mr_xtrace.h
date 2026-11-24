/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_MODULE_XTRACE_H__
#define __MR_MODULE_XTRACE_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_MODULE_XTRACE)
#include <mrX/kernel/mr_object.h>
#include <mrX/kernel/mr_trace.h>
#endif /* defined(MR_USE_MODULE_XTRACE) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup XTrace
 * @{
 */

#if defined(MR_USE_MODULE_XTRACE)
/* XTrace operations type */
typedef struct mr_xtrace_ops {
    void (*emit)(void *priv, const mr_trace_event_t *event);
    mr_err_t (*init)(void *priv);
    void (*deinit)(void *priv);
    void (*start)(void *priv);
    void (*stop)(void *priv);
    void (*flush)(void *priv);
} mr_xtrace_ops_t;

/* XTrace object type */
MR_CLAZZES_EXPORT(xtrace);
typedef struct mr_xtrace {
    mr_object_t parent;
    const mr_xtrace_ops_t *ops;
    void *priv;
} mr_xtrace_t;

/* XTrace output operations type */
typedef struct mr_xtrace_output_ops {
    void (*notify)(void *priv);
    mr_err_t (*init)(void *priv);
    void (*deinit)(void *priv);
    mr_ssize_t (*read)(void *priv, void *buf, mr_size_t size);
} mr_xtrace_output_ops_t;

/* XTrace output object type */
MR_CLAZZES_EXPORT(xtrace_output);
typedef struct mr_xtrace_output {
    mr_object_t parent;
    const mr_xtrace_output_ops_t *ops;
    void *priv;
} mr_xtrace_output_t;

/* XTrace system information type */
typedef struct mr_xtrace_sysinfo {
    const char *app_name;
    const char *os_name;
    const char *device_name;
    const char *core_name;
    mr_uint32_t ts_freq;
    mr_uint32_t cpu_freq;
    mr_uintptr_t ram_base;
    mr_uintptr_t anchor_addr;
} mr_xtrace_sysinfo_t;

/**
 * @brief This macro function initializes an xtrace.
 *
 * @param _ops The xtrace operations.
 * @param _priv The xtrace private data.
 */
#define MR_XTRACE_INIT(_ops, _priv)                                            \
    {                                                                          \
        .parent = MR_OBJECT_INIT(MR_CLAZZES_STATIK(xtrace)),                   \
        .ops = (_ops),                                                         \
        .priv = (_priv),                                                       \
    }

/**
 * @brief This macro function initializes an xtrace output.
 *
 * @param _ops The xtrace output operations.
 * @param _priv The xtrace output private data.
 */
#define MR_XTRACE_OUTPUT_INIT(_ops, _priv)                                     \
    {                                                                          \
        .parent = MR_OBJECT_INIT(MR_CLAZZES_STATIK(xtrace_output)),            \
        .ops = (_ops),                                                         \
        .priv = (_priv),                                                       \
    }

/**
 * @brief This function starts trace recording.
 */
void mr_xtrace_start(void);

/**
 * @brief This function stops trace recording.
 */
void mr_xtrace_stop(void);

/**
 * @brief This function checks if xtrace is running.
 *
 * @return MR_TRUE on running, MR_FALSE otherwise.
 */
mr_bool_t mr_xtrace_is_running(void);

/**
 * @brief This function emits a trace event.
 *
 * @param event The trace event.
 */
void mr_xtrace_emit(const mr_trace_event_t *event);

/**
 * @brief This function flushes pending xtrace data into the FIFO.
 *
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xtrace_flush(void);

/**
 * @brief This function initializes an xtrace.
 *
 * @param xtrace The xtrace.
 * @param ops The xtrace operations.
 * @param priv The xtrace private data.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xtrace_init(mr_xtrace_t *xtrace, const mr_xtrace_ops_t *ops,
                        void *priv);

/**
 * @brief This function deletes an xtrace.
 *
 * @param xtrace The xtrace.
 */
void mr_xtrace_del(mr_xtrace_t *xtrace);

/**
 * @brief This function registers an xtrace.
 *
 * @param xtrace The xtrace.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xtrace_register(mr_xtrace_t *xtrace);

/**
 * @brief This function unregisters the active xtrace.
 *
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xtrace_unregister(void);

/**
 * @brief This function initializes an xtrace output.
 *
 * @param output The xtrace output.
 * @param ops The xtrace output operations.
 * @param priv The xtrace output private data.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xtrace_output_init(mr_xtrace_output_t *output,
                               const mr_xtrace_output_ops_t *ops, void *priv);

/**
 * @brief This function deletes an xtrace output.
 *
 * @param output The xtrace output.
 */
void mr_xtrace_output_del(mr_xtrace_output_t *output);

/**
 * @brief This function registers an xtrace output.
 *
 * @param output The xtrace output.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xtrace_output_register(mr_xtrace_output_t *output);

/**
 * @brief This function unregisters the active xtrace output.
 *
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xtrace_output_unregister(void);

/**
 * @brief This function writes encoded trace data into the FIFO.
 *
 * @param data The data buffer.
 * @param size The data size in bytes.
 * @return The number of bytes written on success, a negative error code on
 * failure.
 */
mr_ssize_t mr_xtrace_write(const void *data, mr_size_t size);

/**
 * @brief This function reads encoded trace data from the FIFO.
 *
 * @param buf The output buffer.
 * @param size The maximum number of bytes to read.
 * @return The number of bytes read.
 */
mr_size_t mr_xtrace_read(void *buf, mr_size_t size);

/**
 * @brief This function claims a contiguous readable region from the FIFO.
 *
 * @param buf The pointer to receive the contiguous data address.
 * @param want The maximum number of bytes to claim.
 * @return The number of bytes claimed, 0 if empty or busy.
 */
mr_size_t mr_xtrace_read_claim(void **buf, mr_size_t want);

/**
 * @brief This function releases a previously claimed FIFO region.
 *
 * @param len The number of bytes actually consumed.
 */
void mr_xtrace_read_finish(mr_size_t len);

/**
 * @brief This function gets the number of pending bytes in the FIFO.
 *
 * @return The number of pending bytes.
 */
mr_size_t mr_xtrace_pending(void);

/**
 * @brief This function clears all unread data from the FIFO.
 *
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xtrace_clear(void);

/**
 * @brief This function reads host-to-target data from the active xtrace output.
 *
 * @param buf The receive buffer.
 * @param size The buffer size in bytes.
 * @return The number of bytes read on success, a negative error code on
 * failure.
 */
mr_ssize_t mr_xtrace_output_read(void *buf, mr_size_t size);

/**
 * @brief This function sets xtrace system information.
 *
 * @param info The system information.
 */
void mr_xtrace_sysinfo_set(const mr_xtrace_sysinfo_t *info);

/**
 * @brief This function gets xtrace system information.
 *
 * @param info The system information.
 */
void mr_xtrace_sysinfo_get(mr_xtrace_sysinfo_t *info);

/**
 * @brief This function gets xtrace timestamp frequency.
 *
 * @return The timestamp frequency in Hz.
 */
mr_uint32_t mr_xtrace_timestamp_freq(void);

/**
 * @brief This function gets xtrace dropped event count.
 *
 * @return The dropped event count.
 */
mr_uint32_t mr_xtrace_dropped_count(void);

/**
 * @brief This function increments xtrace dropped event count.
 */
void mr_xtrace_dropped_inc(void);
#endif /* defined(MR_USE_MODULE_XTRACE) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_MODULE_XTRACE_H__ */
