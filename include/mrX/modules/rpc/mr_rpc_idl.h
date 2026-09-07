#ifndef __MR_RPC_IDL_H__
#define __MR_RPC_IDL_H__

/* Generated at: 2026-09-07 10:58:23 */

#include <mrX/libc/mr_compiler.h>
#include <mrX/libc/mr_types.h>
#include <mrX/libc/mr_errno.h>

struct mr_rpc_session;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* ================================ INTERN ============================== */
typedef struct __mr_rpc_idl_string_24 {
    mr_uint8_t size;
    char data[25];
} MR_PACKED __mr_rpc_idl_string_24_t;

typedef struct __mr_rpc_idl_string_12 {
    mr_uint8_t size;
    char data[13];
} MR_PACKED __mr_rpc_idl_string_12_t;

typedef struct __mr_rpc_idl_string_16 {
    mr_uint8_t size;
    char data[17];
} MR_PACKED __mr_rpc_idl_string_16_t;

typedef struct __mr_rpc_idl_string_32 {
    mr_uint8_t size;
    char data[33];
} MR_PACKED __mr_rpc_idl_string_32_t;

typedef struct __mr_rpc_idl_string_48 {
    mr_uint8_t size;
    char data[49];
} MR_PACKED __mr_rpc_idl_string_48_t;

typedef struct __mr_rpc_idl_bytes_40 {
    mr_uint8_t size;
    mr_uint8_t data[40];
} MR_PACKED __mr_rpc_idl_bytes_40_t;

/* ================================ ENUM/status ========================= */
typedef mr_uint8_t __mr_rpc_idl_status_t;
#define MR_RPC_IDL_STATUS_OK      (0U)
#define MR_RPC_IDL_STATUS_BUSY    (1U)
#define MR_RPC_IDL_STATUS_FAIL    (2U)

/* ================================ STRUCT/point ======================== */
typedef struct __mr_rpc_idl_point {
    mr_int16_t x;
    mr_int16_t y;
} MR_PACKED __mr_rpc_idl_point_t;

/* ================================ STRUCT/sample ======================= */
typedef struct __mr_rpc_idl_sample {
    mr_uint32_t id;
    __mr_rpc_idl_string_24_t name;
    __mr_rpc_idl_point_t pos;
    mr_uint8_t tags[4];
    __mr_rpc_idl_status_t st;
} MR_PACKED __mr_rpc_idl_sample_t;

/* ================================ STRUCT/kv =========================== */
typedef struct __mr_rpc_idl_kv {
    __mr_rpc_idl_string_12_t key;
    mr_uint32_t val;
} MR_PACKED __mr_rpc_idl_kv_t;

/* ================================ SERVICE/lab ========================= */
typedef struct __mr_rpc_request_lab_mix {
    __mr_rpc_idl_string_16_t title;
    mr_uint32_t count;
    __mr_rpc_idl_string_32_t note;
    __mr_rpc_idl_bytes_40_t blob;
} MR_PACKED __mr_rpc_request_lab_mix_t;
typedef struct __mr_rpc_response_lab_mix {
    __mr_rpc_idl_string_16_t title;
    mr_uint32_t count;
    __mr_rpc_idl_string_32_t note;
    __mr_rpc_idl_bytes_40_t blob;
} MR_PACKED __mr_rpc_response_lab_mix_t;

typedef struct __mr_rpc_request_lab_nest {
    __mr_rpc_idl_sample_t item;
} MR_PACKED __mr_rpc_request_lab_nest_t;
typedef struct __mr_rpc_response_lab_nest {
    __mr_rpc_idl_sample_t item;
} MR_PACKED __mr_rpc_response_lab_nest_t;

typedef struct __mr_rpc_request_lab_table {
    __mr_rpc_idl_kv_t rows[2];
} MR_PACKED __mr_rpc_request_lab_table_t;
typedef struct __mr_rpc_response_lab_table {
    __mr_rpc_idl_kv_t rows[2];
} MR_PACKED __mr_rpc_response_lab_table_t;

typedef struct __mr_rpc_request_lab_log {
    __mr_rpc_idl_string_48_t msg;
} MR_PACKED __mr_rpc_request_lab_log_t;

/* ================================ SERVICE/demo ======================== */
typedef struct __mr_rpc_request_demo_add {
    mr_int32_t a;
    mr_int32_t b;
} MR_PACKED __mr_rpc_request_demo_add_t;
typedef struct __mr_rpc_response_demo_add {
    mr_int32_t result;
} MR_PACKED __mr_rpc_response_demo_add_t;

typedef struct __mr_rpc_request_demo_sub {
    mr_int32_t a;
    mr_int32_t b;
} MR_PACKED __mr_rpc_request_demo_sub_t;
typedef struct __mr_rpc_response_demo_sub {
    mr_int32_t result;
} MR_PACKED __mr_rpc_response_demo_sub_t;

typedef struct __mr_rpc_request_demo_mul {
    mr_int32_t a;
    mr_int32_t b;
} MR_PACKED __mr_rpc_request_demo_mul_t;
typedef struct __mr_rpc_response_demo_mul {
    mr_int32_t result;
} MR_PACKED __mr_rpc_response_demo_mul_t;

typedef struct __mr_rpc_request_demo_div {
    mr_int32_t a;
    mr_int32_t b;
} MR_PACKED __mr_rpc_request_demo_div_t;
typedef struct __mr_rpc_response_demo_div {
    mr_int32_t result;
} MR_PACKED __mr_rpc_response_demo_div_t;

typedef struct __mr_rpc_request_demo_concat {
    __mr_rpc_idl_string_16_t left;
    __mr_rpc_idl_string_16_t right;
} MR_PACKED __mr_rpc_request_demo_concat_t;
typedef struct __mr_rpc_response_demo_concat {
    __mr_rpc_idl_string_32_t text;
} MR_PACKED __mr_rpc_response_demo_concat_t;

typedef struct __mr_rpc_request_demo_scale {
    mr_int32_t values[4];
    mr_int32_t factor;
} MR_PACKED __mr_rpc_request_demo_scale_t;
typedef struct __mr_rpc_response_demo_scale {
    mr_int32_t values[4];
    mr_int32_t sum;
} MR_PACKED __mr_rpc_response_demo_scale_t;

/* ================================ UNIONS ============================== */
typedef union mr_rpc_idl_request {
    __mr_rpc_request_lab_mix_t lab_mix;
    __mr_rpc_request_lab_nest_t lab_nest;
    __mr_rpc_request_lab_table_t lab_table;
    __mr_rpc_request_lab_log_t lab_log;
    __mr_rpc_request_demo_add_t demo_add;
    __mr_rpc_request_demo_sub_t demo_sub;
    __mr_rpc_request_demo_mul_t demo_mul;
    __mr_rpc_request_demo_div_t demo_div;
    __mr_rpc_request_demo_concat_t demo_concat;
    __mr_rpc_request_demo_scale_t demo_scale;
} mr_rpc_idl_request_t;

typedef union mr_rpc_idl_response {
    __mr_rpc_response_lab_mix_t lab_mix;
    __mr_rpc_response_lab_nest_t lab_nest;
    __mr_rpc_response_lab_table_t lab_table;
    __mr_rpc_response_demo_add_t demo_add;
    __mr_rpc_response_demo_sub_t demo_sub;
    __mr_rpc_response_demo_mul_t demo_mul;
    __mr_rpc_response_demo_div_t demo_div;
    __mr_rpc_response_demo_concat_t demo_concat;
    __mr_rpc_response_demo_scale_t demo_scale;
} mr_rpc_idl_response_t;

/* ================================ EXPORT/lab ========================== */
#define __MR_RPC_ENTRY_lab_mix                                                 \
    mr_err_t __mr_rpc_entry_lab_mix(MR_UNUSED mr_rpc_session_t *session,       \
                                    __mr_rpc_request_lab_mix_t *request,       \
                                    __mr_rpc_response_lab_mix_t *response)
#define __MR_RPC_ENTRY_lab_nest                                                \
    mr_err_t __mr_rpc_entry_lab_nest(MR_UNUSED mr_rpc_session_t *session,      \
                                     __mr_rpc_request_lab_nest_t *request,     \
                                     __mr_rpc_response_lab_nest_t *response)
#define __MR_RPC_ENTRY_lab_table                                               \
    mr_err_t __mr_rpc_entry_lab_table(MR_UNUSED mr_rpc_session_t *session,     \
                                      __mr_rpc_request_lab_table_t *request,   \
                                      __mr_rpc_response_lab_table_t *response)
#define __MR_RPC_ENTRY_lab_ping                                                \
    mr_err_t __mr_rpc_entry_lab_ping(MR_UNUSED mr_rpc_session_t *session)
#define __MR_RPC_ENTRY_lab_log                                                 \
    mr_err_t __mr_rpc_entry_lab_log(MR_UNUSED mr_rpc_session_t *session,       \
                                    __mr_rpc_request_lab_log_t *request)

/* ================================ EXPORT/demo ========================= */
#define __MR_RPC_ENTRY_demo_add                                                \
    mr_err_t __mr_rpc_entry_demo_add(MR_UNUSED mr_rpc_session_t *session,      \
                                     __mr_rpc_request_demo_add_t *request,     \
                                     __mr_rpc_response_demo_add_t *response)
#define __MR_RPC_ENTRY_demo_sub                                                \
    mr_err_t __mr_rpc_entry_demo_sub(MR_UNUSED mr_rpc_session_t *session,      \
                                     __mr_rpc_request_demo_sub_t *request,     \
                                     __mr_rpc_response_demo_sub_t *response)
#define __MR_RPC_ENTRY_demo_mul                                                \
    mr_err_t __mr_rpc_entry_demo_mul(MR_UNUSED mr_rpc_session_t *session,      \
                                     __mr_rpc_request_demo_mul_t *request,     \
                                     __mr_rpc_response_demo_mul_t *response)
#define __MR_RPC_ENTRY_demo_div                                                \
    mr_err_t __mr_rpc_entry_demo_div(MR_UNUSED mr_rpc_session_t *session,      \
                                     __mr_rpc_request_demo_div_t *request,     \
                                     __mr_rpc_response_demo_div_t *response)
#define __MR_RPC_ENTRY_demo_concat                                             \
    mr_err_t __mr_rpc_entry_demo_concat(MR_UNUSED mr_rpc_session_t *session,   \
                                        __mr_rpc_request_demo_concat_t *request, \
                                        __mr_rpc_response_demo_concat_t *response)
#define __MR_RPC_ENTRY_demo_scale                                              \
    mr_err_t __mr_rpc_entry_demo_scale(MR_UNUSED mr_rpc_session_t *session,    \
                                       __mr_rpc_request_demo_scale_t *request, \
                                       __mr_rpc_response_demo_scale_t *response)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_RPC_IDL_H__ */
