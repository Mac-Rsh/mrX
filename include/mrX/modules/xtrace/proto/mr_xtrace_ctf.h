/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_MODULE_XTRACE_CTF_H__
#define __MR_MODULE_XTRACE_CTF_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_MODULE_XTRACE_PROTO_CTF)
#include <mrX/modules/xtrace/mr_xtrace.h>
#endif /* defined(MR_USE_MODULE_XTRACE_PROTO_CTF) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup XTrace-CTF
 * @{
 */

#if defined(MR_USE_MODULE_XTRACE_PROTO_CTF)
/* XTrace CTF protocol definition */
#define MR_CTF_MAGIC                    (0xc1fc1fc1U)
#define MR_CTF_STREAM_ID                (0U)
#define MR_CTF_ARGS_MAX                 (8U)
#define MR_CTF_STR_MAX                  (128U)

/* XTrace CTF packet header type (CTF 1.8) */
typedef struct mr_ctf_packet_hdr {
    mr_uint32_t magic;
    mr_uint32_t stream_id;
} MR_PACKED mr_ctf_packet_hdr_t;

/* XTrace CTF packet context type (CTF 1.8) */
typedef struct mr_ctf_packet_ctx {
    mr_uint64_t timestamp_begin;
    mr_uint64_t timestamp_end;
    mr_uint32_t content_size;
    mr_uint32_t packet_size;
    mr_uint32_t events_discarded;
    mr_uint32_t seq;
    mr_uint64_t anchor_addr;
    mr_uint32_t ts_freq;
    mr_uint32_t cpu_freq;
    mr_uint32_t ptr_bits;
} MR_PACKED mr_ctf_packet_ctx_t;

/* XTrace CTF packet prefix type */
typedef struct mr_ctf_packet_prefix {
    mr_ctf_packet_hdr_t hdr;
    mr_ctf_packet_ctx_t ctx;
} MR_PACKED mr_ctf_packet_prefix_t;

/* XTrace CTF event header type (CTF 1.8) */
typedef struct mr_ctf_event_hdr {
    mr_uint32_t id;
    mr_uint64_t timestamp;
} MR_PACKED mr_ctf_event_hdr_t;

/**
 * @brief This function formats the CTF 1.8 TSDL metadata text.
 *
 * @param buf The output buffer.
 * @param size The output buffer size in bytes.
 * @return The number of bytes written (excluding NUL), or negative error code.
 */
mr_ssize_t mr_xtrace_ctf_metadata(char *buf, mr_size_t size);

/**
 * @brief This function registers the CTF protocol encoder with xtrace.
 *
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_xtrace_ctf_register(void);
#endif /* defined(MR_USE_MODULE_XTRACE_PROTO_CTF) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_MODULE_XTRACE_CTF_H__ */
