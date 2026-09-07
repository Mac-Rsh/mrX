/* Generated at: 2026-09-07 10:58:23 */

#include <mrX/modules/rpc/mr_rpc.h>
#if defined(MR_USE_MODULE_RPC)
#include <mrX/libc/mr_string.h>

static mr_err_t rpc_decode_empty(mr_rpc_session_t *session) {
    if (session->recv.size != 0U) {
        return -MR_EINVAL;
    }
    return 0;
}

static mr_err_t rpc_encode_empty(mr_rpc_session_t *session) {
    MR_IGNORE(session);
    return 0;
}

/* ================================ lab/mix ============================= */
static mr_err_t rpc_request_lab_mix_unpack(
    __mr_rpc_request_lab_mix_t *request, mr_uint32_t compact_size) {
    mr_uint32_t src[3];
    mr_uint32_t n[3];
    mr_uint32_t off;
    mr_uint8_t *p;

    p = (mr_uint8_t *)request;
    off = 0U;
    if ((off + 1U) > compact_size) {
        return -MR_EINVAL;
    }
    n[0] = p[off];
    if ((n[0] > 16U) || ((off + 1U + n[0]) > compact_size)) {
        return -MR_EINVAL;
    }
    off += 1U + n[0];
    if ((off + sizeof(request->count)) > compact_size) {
        return -MR_EINVAL;
    }
    src[0] = off;
    off += sizeof(request->count);
    if ((off + 1U) > compact_size) {
        return -MR_EINVAL;
    }
    n[1] = p[off];
    if ((n[1] > 32U) || ((off + 1U + n[1]) > compact_size)) {
        return -MR_EINVAL;
    }
    src[1] = off;
    off += 1U + n[1];
    if ((off + 1U) > compact_size) {
        return -MR_EINVAL;
    }
    n[2] = p[off];
    if ((n[2] > 40U) || ((off + 1U + n[2]) > compact_size)) {
        return -MR_EINVAL;
    }
    src[2] = off;
    off += 1U + n[2];
    if (off != compact_size) {
        return -MR_EINVAL;
    }

    mr_memmove(&request->blob, p + src[2], 1U + n[2]);
    mr_memmove(&request->note, p + src[1], 1U + n[1]);
    mr_memmove(&request->count, p + src[0], sizeof(request->count));
    request->title.data[request->title.size] = '\0';
    request->note.data[request->note.size] = '\0';
    return 0;
}

static mr_err_t rpc_response_lab_mix_pack(
    __mr_rpc_response_lab_mix_t *response, mr_uint32_t *compact_size) {
    mr_uint32_t n[3];
    mr_uint32_t off;
    mr_uint8_t *p;

    p = (mr_uint8_t *)response;
    off = 0U;
    n[0] = response->title.size;
    if (n[0] > 16U) {
        return -MR_EINVAL;
    }
    off += 1U + n[0];
    mr_memmove(p + off, &response->count, sizeof(response->count));
    off += sizeof(response->count);
    n[1] = response->note.size;
    if (n[1] > 32U) {
        return -MR_EINVAL;
    }
    mr_memmove(p + off, &response->note, 1U + n[1]);
    off += 1U + n[1];
    n[2] = response->blob.size;
    if (n[2] > 40U) {
        return -MR_EINVAL;
    }
    mr_memmove(p + off, &response->blob, 1U + n[2]);
    off += 1U + n[2];
    *compact_size = off;
    return 0;
}

static mr_err_t rpc_decode_lab_mix(mr_rpc_session_t *session) {
    __mr_rpc_request_lab_mix_t *request;
    mr_err_t ret;

    /* Request */
    request = &session->request.payload.lab_mix;

    /* Unpack */
    ret = rpc_request_lab_mix_unpack(request, session->recv.size);
    if (ret < 0) {
        return ret;
    }
    return 0;
}

static mr_err_t rpc_invoke_lab_mix(mr_rpc_session_t *session) {
    extern mr_err_t __mr_rpc_entry_lab_mix(mr_rpc_session_t *,
                                           __mr_rpc_request_lab_mix_t *,
                                           __mr_rpc_response_lab_mix_t *);
    __mr_rpc_response_lab_mix_t *response;
    __mr_rpc_request_lab_mix_t *request;

    /* Request & response */
    request = &session->request.payload.lab_mix;
    response = &session->response.payload.lab_mix;

    /* Init response */
    response->title.size = 0U;
    response->note.size = 0U;
    response->blob.size = 0U;

    /* Call user entry */
    return __mr_rpc_entry_lab_mix(session, request, response);
}

static mr_err_t rpc_encode_lab_mix(mr_rpc_session_t *session) {
    __mr_rpc_response_lab_mix_t *response;
    mr_uint32_t payload_size;
    mr_err_t ret;

    /* Response */
    response = &session->response.payload.lab_mix;

    /* Pack */
    ret = rpc_response_lab_mix_pack(response, &payload_size);
    if (ret < 0) {
        return ret;
    }
    return (mr_err_t)payload_size;
}

MR_RPC_EXPORT(lab_mix,
              rpc_decode_lab_mix,
              rpc_invoke_lab_mix,
              rpc_encode_lab_mix,
              1,
              0);

/* ================================ lab/nest ============================ */
static mr_err_t rpc_request_lab_nest_unpack(
    __mr_rpc_request_lab_nest_t *request, mr_uint32_t compact_size) {
    mr_uint32_t src[3];
    mr_uint32_t n;
    mr_uint32_t off;
    mr_uint8_t *p;

    p = (mr_uint8_t *)request;
    off = 0U;
    if ((off + sizeof(request->item.id)) > compact_size) {
        return -MR_EINVAL;
    }
    off += sizeof(request->item.id);
    if ((off + 1U) > compact_size) {
        return -MR_EINVAL;
    }
    n = p[off];
    if ((n > 24U) || ((off + 1U + n) > compact_size)) {
        return -MR_EINVAL;
    }
    off += 1U + n;
    if ((off + sizeof(request->item.pos)) > compact_size) {
        return -MR_EINVAL;
    }
    src[0] = off;
    off += sizeof(request->item.pos);
    if ((off + sizeof(request->item.tags)) > compact_size) {
        return -MR_EINVAL;
    }
    src[1] = off;
    off += sizeof(request->item.tags);
    if ((off + sizeof(request->item.st)) > compact_size) {
        return -MR_EINVAL;
    }
    src[2] = off;
    off += sizeof(request->item.st);
    if (off != compact_size) {
        return -MR_EINVAL;
    }

    mr_memmove(&request->item.st, p + src[2], sizeof(request->item.st));
    mr_memmove(&request->item.tags, p + src[1], sizeof(request->item.tags));
    mr_memmove(&request->item.pos, p + src[0], sizeof(request->item.pos));
    request->item.name.data[request->item.name.size] = '\0';
    return 0;
}

static mr_err_t rpc_response_lab_nest_pack(
    __mr_rpc_response_lab_nest_t *response, mr_uint32_t *compact_size) {
    mr_uint32_t n;
    mr_uint32_t off;
    mr_uint8_t *p;

    p = (mr_uint8_t *)response;
    off = 0U;
    off += sizeof(response->item.id);
    n = response->item.name.size;
    if (n > 24U) {
        return -MR_EINVAL;
    }
    off += 1U + n;
    mr_memmove(p + off, &response->item.pos, sizeof(response->item.pos));
    off += sizeof(response->item.pos);
    mr_memmove(p + off, &response->item.tags, sizeof(response->item.tags));
    off += sizeof(response->item.tags);
    mr_memmove(p + off, &response->item.st, sizeof(response->item.st));
    off += sizeof(response->item.st);
    *compact_size = off;
    return 0;
}

static mr_err_t rpc_decode_lab_nest(mr_rpc_session_t *session) {
    __mr_rpc_request_lab_nest_t *request;
    mr_err_t ret;

    /* Request */
    request = &session->request.payload.lab_nest;

    /* Unpack */
    ret = rpc_request_lab_nest_unpack(request, session->recv.size);
    if (ret < 0) {
        return ret;
    }
    return 0;
}

static mr_err_t rpc_invoke_lab_nest(mr_rpc_session_t *session) {
    extern mr_err_t __mr_rpc_entry_lab_nest(mr_rpc_session_t *,
                                            __mr_rpc_request_lab_nest_t *,
                                            __mr_rpc_response_lab_nest_t *);
    __mr_rpc_response_lab_nest_t *response;
    __mr_rpc_request_lab_nest_t *request;

    /* Request & response */
    request = &session->request.payload.lab_nest;
    response = &session->response.payload.lab_nest;

    /* Init response */
    response->item.name.size = 0U;

    /* Call user entry */
    return __mr_rpc_entry_lab_nest(session, request, response);
}

static mr_err_t rpc_encode_lab_nest(mr_rpc_session_t *session) {
    __mr_rpc_response_lab_nest_t *response;
    mr_uint32_t payload_size;
    mr_err_t ret;

    /* Response */
    response = &session->response.payload.lab_nest;

    /* Pack */
    ret = rpc_response_lab_nest_pack(response, &payload_size);
    if (ret < 0) {
        return ret;
    }
    return (mr_err_t)payload_size;
}

MR_RPC_EXPORT(lab_nest,
              rpc_decode_lab_nest,
              rpc_invoke_lab_nest,
              rpc_encode_lab_nest,
              1,
              1);

/* ================================ lab/table =========================== */
static mr_err_t rpc_request_lab_table_unpack(
    __mr_rpc_request_lab_table_t *request, mr_uint32_t compact_size) {
    mr_uint32_t src[3];
    mr_uint32_t n[2];
    mr_uint32_t off;
    mr_uint8_t *p;

    p = (mr_uint8_t *)request;
    off = 0U;
    if ((off + 1U) > compact_size) {
        return -MR_EINVAL;
    }
    n[0] = p[off];
    if ((n[0] > 12U) || ((off + 1U + n[0]) > compact_size)) {
        return -MR_EINVAL;
    }
    off += 1U + n[0];
    if ((off + sizeof(request->rows[0].val)) > compact_size) {
        return -MR_EINVAL;
    }
    src[0] = off;
    off += sizeof(request->rows[0].val);
    if ((off + 1U) > compact_size) {
        return -MR_EINVAL;
    }
    n[1] = p[off];
    if ((n[1] > 12U) || ((off + 1U + n[1]) > compact_size)) {
        return -MR_EINVAL;
    }
    src[1] = off;
    off += 1U + n[1];
    if ((off + sizeof(request->rows[1].val)) > compact_size) {
        return -MR_EINVAL;
    }
    src[2] = off;
    off += sizeof(request->rows[1].val);
    if (off != compact_size) {
        return -MR_EINVAL;
    }

    mr_memmove(&request->rows[1].val, p + src[2], sizeof(request->rows[1].val));
    mr_memmove(&request->rows[1].key, p + src[1], 1U + n[1]);
    mr_memmove(&request->rows[0].val, p + src[0], sizeof(request->rows[0].val));
    request->rows[0].key.data[request->rows[0].key.size] = '\0';
    request->rows[1].key.data[request->rows[1].key.size] = '\0';
    return 0;
}

static mr_err_t rpc_response_lab_table_pack(
    __mr_rpc_response_lab_table_t *response, mr_uint32_t *compact_size) {
    mr_uint32_t n[2];
    mr_uint32_t off;
    mr_uint8_t *p;

    p = (mr_uint8_t *)response;
    off = 0U;
    n[0] = response->rows[0].key.size;
    if (n[0] > 12U) {
        return -MR_EINVAL;
    }
    off += 1U + n[0];
    mr_memmove(p + off, &response->rows[0].val, sizeof(response->rows[0].val));
    off += sizeof(response->rows[0].val);
    n[1] = response->rows[1].key.size;
    if (n[1] > 12U) {
        return -MR_EINVAL;
    }
    mr_memmove(p + off, &response->rows[1].key, 1U + n[1]);
    off += 1U + n[1];
    mr_memmove(p + off, &response->rows[1].val, sizeof(response->rows[1].val));
    off += sizeof(response->rows[1].val);
    *compact_size = off;
    return 0;
}

static mr_err_t rpc_decode_lab_table(mr_rpc_session_t *session) {
    __mr_rpc_request_lab_table_t *request;
    mr_err_t ret;

    /* Request */
    request = &session->request.payload.lab_table;

    /* Unpack */
    ret = rpc_request_lab_table_unpack(request, session->recv.size);
    if (ret < 0) {
        return ret;
    }
    return 0;
}

static mr_err_t rpc_invoke_lab_table(mr_rpc_session_t *session) {
    extern mr_err_t __mr_rpc_entry_lab_table(mr_rpc_session_t *,
                                             __mr_rpc_request_lab_table_t *,
                                             __mr_rpc_response_lab_table_t *);
    __mr_rpc_response_lab_table_t *response;
    __mr_rpc_request_lab_table_t *request;

    /* Request & response */
    request = &session->request.payload.lab_table;
    response = &session->response.payload.lab_table;

    /* Init response */
    response->rows[0].key.size = 0U;
    response->rows[1].key.size = 0U;

    /* Call user entry */
    return __mr_rpc_entry_lab_table(session, request, response);
}

static mr_err_t rpc_encode_lab_table(mr_rpc_session_t *session) {
    __mr_rpc_response_lab_table_t *response;
    mr_uint32_t payload_size;
    mr_err_t ret;

    /* Response */
    response = &session->response.payload.lab_table;

    /* Pack */
    ret = rpc_response_lab_table_pack(response, &payload_size);
    if (ret < 0) {
        return ret;
    }
    return (mr_err_t)payload_size;
}

MR_RPC_EXPORT(lab_table,
              rpc_decode_lab_table,
              rpc_invoke_lab_table,
              rpc_encode_lab_table,
              1,
              2);

/* ================================ lab/ping ============================ */
static mr_err_t rpc_invoke_lab_ping(mr_rpc_session_t *session) {
    extern mr_err_t __mr_rpc_entry_lab_ping(mr_rpc_session_t *);

    /* Call user entry */
    return __mr_rpc_entry_lab_ping(session);
}

MR_RPC_EXPORT(lab_ping,
              rpc_decode_empty,
              rpc_invoke_lab_ping,
              rpc_encode_empty,
              1,
              3);

/* ================================ lab/log ============================= */
static mr_err_t rpc_request_lab_log_unpack(
    __mr_rpc_request_lab_log_t *request, mr_uint32_t compact_size) {
    mr_uint32_t n;
    mr_uint32_t off;
    mr_uint8_t *p;

    p = (mr_uint8_t *)request;
    off = 0U;
    if ((off + 1U) > compact_size) {
        return -MR_EINVAL;
    }
    n = p[off];
    if ((n > 48U) || ((off + 1U + n) > compact_size)) {
        return -MR_EINVAL;
    }
    off += 1U + n;
    if (off != compact_size) {
        return -MR_EINVAL;
    }
    request->msg.data[request->msg.size] = '\0';
    return 0;
}

static mr_err_t rpc_decode_lab_log(mr_rpc_session_t *session) {
    __mr_rpc_request_lab_log_t *request;
    mr_err_t ret;

    /* Request */
    request = &session->request.payload.lab_log;

    /* Unpack */
    ret = rpc_request_lab_log_unpack(request, session->recv.size);
    if (ret < 0) {
        return ret;
    }
    return 0;
}

static mr_err_t rpc_invoke_lab_log(mr_rpc_session_t *session) {
    extern mr_err_t __mr_rpc_entry_lab_log(mr_rpc_session_t *,
                                           __mr_rpc_request_lab_log_t *);
    __mr_rpc_request_lab_log_t *request;

    /* Request */
    request = &session->request.payload.lab_log;

    /* Call user entry */
    return __mr_rpc_entry_lab_log(session, request);
}

MR_RPC_EXPORT(lab_log,
              rpc_decode_lab_log,
              rpc_invoke_lab_log,
              rpc_encode_empty,
              1,
              4);

/* ================================ demo/add ============================ */
static mr_err_t rpc_decode_demo_add(mr_rpc_session_t *session) {
    __mr_rpc_request_demo_add_t *request;

    /* Request */
    request = &session->request.payload.demo_add;

    /* Unpack */
    if (session->recv.size != sizeof(*request)) {
        return -MR_EINVAL;
    }
    return 0;
}

static mr_err_t rpc_invoke_demo_add(mr_rpc_session_t *session) {
    extern mr_err_t __mr_rpc_entry_demo_add(mr_rpc_session_t *,
                                            __mr_rpc_request_demo_add_t *,
                                            __mr_rpc_response_demo_add_t *);
    __mr_rpc_response_demo_add_t *response;
    __mr_rpc_request_demo_add_t *request;

    /* Request & response */
    request = &session->request.payload.demo_add;
    response = &session->response.payload.demo_add;

    /* Call user entry */
    return __mr_rpc_entry_demo_add(session, request, response);
}

static mr_err_t rpc_encode_demo_add(mr_rpc_session_t *session) {
    __mr_rpc_response_demo_add_t *response;

    /* Response */
    response = &session->response.payload.demo_add;
    return (mr_err_t)sizeof(*response);
}

MR_RPC_EXPORT(demo_add,
              rpc_decode_demo_add,
              rpc_invoke_demo_add,
              rpc_encode_demo_add,
              2,
              0);

/* ================================ demo/sub ============================ */
static mr_err_t rpc_decode_demo_sub(mr_rpc_session_t *session) {
    __mr_rpc_request_demo_sub_t *request;

    /* Request */
    request = &session->request.payload.demo_sub;

    /* Unpack */
    if (session->recv.size != sizeof(*request)) {
        return -MR_EINVAL;
    }
    return 0;
}

static mr_err_t rpc_invoke_demo_sub(mr_rpc_session_t *session) {
    extern mr_err_t __mr_rpc_entry_demo_sub(mr_rpc_session_t *,
                                            __mr_rpc_request_demo_sub_t *,
                                            __mr_rpc_response_demo_sub_t *);
    __mr_rpc_response_demo_sub_t *response;
    __mr_rpc_request_demo_sub_t *request;

    /* Request & response */
    request = &session->request.payload.demo_sub;
    response = &session->response.payload.demo_sub;

    /* Call user entry */
    return __mr_rpc_entry_demo_sub(session, request, response);
}

static mr_err_t rpc_encode_demo_sub(mr_rpc_session_t *session) {
    __mr_rpc_response_demo_sub_t *response;

    /* Response */
    response = &session->response.payload.demo_sub;
    return (mr_err_t)sizeof(*response);
}

MR_RPC_EXPORT(demo_sub,
              rpc_decode_demo_sub,
              rpc_invoke_demo_sub,
              rpc_encode_demo_sub,
              2,
              1);

/* ================================ demo/mul ============================ */
static mr_err_t rpc_decode_demo_mul(mr_rpc_session_t *session) {
    __mr_rpc_request_demo_mul_t *request;

    /* Request */
    request = &session->request.payload.demo_mul;

    /* Unpack */
    if (session->recv.size != sizeof(*request)) {
        return -MR_EINVAL;
    }
    return 0;
}

static mr_err_t rpc_invoke_demo_mul(mr_rpc_session_t *session) {
    extern mr_err_t __mr_rpc_entry_demo_mul(mr_rpc_session_t *,
                                            __mr_rpc_request_demo_mul_t *,
                                            __mr_rpc_response_demo_mul_t *);
    __mr_rpc_response_demo_mul_t *response;
    __mr_rpc_request_demo_mul_t *request;

    /* Request & response */
    request = &session->request.payload.demo_mul;
    response = &session->response.payload.demo_mul;

    /* Call user entry */
    return __mr_rpc_entry_demo_mul(session, request, response);
}

static mr_err_t rpc_encode_demo_mul(mr_rpc_session_t *session) {
    __mr_rpc_response_demo_mul_t *response;

    /* Response */
    response = &session->response.payload.demo_mul;
    return (mr_err_t)sizeof(*response);
}

MR_RPC_EXPORT(demo_mul,
              rpc_decode_demo_mul,
              rpc_invoke_demo_mul,
              rpc_encode_demo_mul,
              2,
              2);

/* ================================ demo/div ============================ */
static mr_err_t rpc_decode_demo_div(mr_rpc_session_t *session) {
    __mr_rpc_request_demo_div_t *request;

    /* Request */
    request = &session->request.payload.demo_div;

    /* Unpack */
    if (session->recv.size != sizeof(*request)) {
        return -MR_EINVAL;
    }
    return 0;
}

static mr_err_t rpc_invoke_demo_div(mr_rpc_session_t *session) {
    extern mr_err_t __mr_rpc_entry_demo_div(mr_rpc_session_t *,
                                            __mr_rpc_request_demo_div_t *,
                                            __mr_rpc_response_demo_div_t *);
    __mr_rpc_response_demo_div_t *response;
    __mr_rpc_request_demo_div_t *request;

    /* Request & response */
    request = &session->request.payload.demo_div;
    response = &session->response.payload.demo_div;

    /* Call user entry */
    return __mr_rpc_entry_demo_div(session, request, response);
}

static mr_err_t rpc_encode_demo_div(mr_rpc_session_t *session) {
    __mr_rpc_response_demo_div_t *response;

    /* Response */
    response = &session->response.payload.demo_div;
    return (mr_err_t)sizeof(*response);
}

MR_RPC_EXPORT(demo_div,
              rpc_decode_demo_div,
              rpc_invoke_demo_div,
              rpc_encode_demo_div,
              2,
              3);

/* ================================ demo/concat ========================= */
static mr_err_t rpc_request_demo_concat_unpack(
    __mr_rpc_request_demo_concat_t *request, mr_uint32_t compact_size) {
    mr_uint32_t src;
    mr_uint32_t n[2];
    mr_uint32_t off;
    mr_uint8_t *p;

    p = (mr_uint8_t *)request;
    off = 0U;
    if ((off + 1U) > compact_size) {
        return -MR_EINVAL;
    }
    n[0] = p[off];
    if ((n[0] > 16U) || ((off + 1U + n[0]) > compact_size)) {
        return -MR_EINVAL;
    }
    off += 1U + n[0];
    if ((off + 1U) > compact_size) {
        return -MR_EINVAL;
    }
    n[1] = p[off];
    if ((n[1] > 16U) || ((off + 1U + n[1]) > compact_size)) {
        return -MR_EINVAL;
    }
    src = off;
    off += 1U + n[1];
    if (off != compact_size) {
        return -MR_EINVAL;
    }

    mr_memmove(&request->right, p + src, 1U + n[1]);
    request->left.data[request->left.size] = '\0';
    request->right.data[request->right.size] = '\0';
    return 0;
}

static mr_err_t rpc_response_demo_concat_pack(
    __mr_rpc_response_demo_concat_t *response, mr_uint32_t *compact_size) {
    mr_uint32_t n;
    mr_uint32_t off;

    off = 0U;
    n = response->text.size;
    if (n > 32U) {
        return -MR_EINVAL;
    }
    off += 1U + n;
    *compact_size = off;
    return 0;
}

static mr_err_t rpc_decode_demo_concat(mr_rpc_session_t *session) {
    __mr_rpc_request_demo_concat_t *request;
    mr_err_t ret;

    /* Request */
    request = &session->request.payload.demo_concat;

    /* Unpack */
    ret = rpc_request_demo_concat_unpack(request, session->recv.size);
    if (ret < 0) {
        return ret;
    }
    return 0;
}

static mr_err_t rpc_invoke_demo_concat(mr_rpc_session_t *session) {
    extern mr_err_t __mr_rpc_entry_demo_concat(mr_rpc_session_t *,
                                               __mr_rpc_request_demo_concat_t *,
                                               __mr_rpc_response_demo_concat_t *);
    __mr_rpc_response_demo_concat_t *response;
    __mr_rpc_request_demo_concat_t *request;

    /* Request & response */
    request = &session->request.payload.demo_concat;
    response = &session->response.payload.demo_concat;

    /* Init response */
    response->text.size = 0U;

    /* Call user entry */
    return __mr_rpc_entry_demo_concat(session, request, response);
}

static mr_err_t rpc_encode_demo_concat(mr_rpc_session_t *session) {
    __mr_rpc_response_demo_concat_t *response;
    mr_uint32_t payload_size;
    mr_err_t ret;

    /* Response */
    response = &session->response.payload.demo_concat;

    /* Pack */
    ret = rpc_response_demo_concat_pack(response, &payload_size);
    if (ret < 0) {
        return ret;
    }
    return (mr_err_t)payload_size;
}

MR_RPC_EXPORT(demo_concat,
              rpc_decode_demo_concat,
              rpc_invoke_demo_concat,
              rpc_encode_demo_concat,
              2,
              4);

/* ================================ demo/scale ========================== */
static mr_err_t rpc_decode_demo_scale(mr_rpc_session_t *session) {
    __mr_rpc_request_demo_scale_t *request;

    /* Request */
    request = &session->request.payload.demo_scale;

    /* Unpack */
    if (session->recv.size != sizeof(*request)) {
        return -MR_EINVAL;
    }
    return 0;
}

static mr_err_t rpc_invoke_demo_scale(mr_rpc_session_t *session) {
    extern mr_err_t __mr_rpc_entry_demo_scale(mr_rpc_session_t *,
                                              __mr_rpc_request_demo_scale_t *,
                                              __mr_rpc_response_demo_scale_t *);
    __mr_rpc_response_demo_scale_t *response;
    __mr_rpc_request_demo_scale_t *request;

    /* Request & response */
    request = &session->request.payload.demo_scale;
    response = &session->response.payload.demo_scale;

    /* Call user entry */
    return __mr_rpc_entry_demo_scale(session, request, response);
}

static mr_err_t rpc_encode_demo_scale(mr_rpc_session_t *session) {
    __mr_rpc_response_demo_scale_t *response;

    /* Response */
    response = &session->response.payload.demo_scale;
    return (mr_err_t)sizeof(*response);
}

MR_RPC_EXPORT(demo_scale,
              rpc_decode_demo_scale,
              rpc_invoke_demo_scale,
              rpc_encode_demo_scale,
              2,
              5);
#endif /* defined(MR_USE_MODULE_RPC) */
