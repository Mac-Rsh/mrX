/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-01        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_MODULE_RPC)
#include <mrX/modules/rpc/mr_rpc.h>
#include <mrX/libc/mr_malloc.h>
#include <mrX/libc/mr_errno.h>
#include <mrX/libc/mr_string.h>

#define TEST_RPC_OUTPUT_SIZE   (4096U)
#define TEST_RPC_PACKET_SIZE   (384U)
#define TEST_RPC_FRAME_SIZE    ((mr_uint32_t)sizeof(mr_rpc_frame_t))
#define TEST_RPC_CHECKSUM_SIZE (2U)

typedef struct test_rpc_io {
    mr_uint8_t output[TEST_RPC_OUTPUT_SIZE];
    mr_uint32_t size;
    mr_uint32_t limit;
    mr_err_t send_error;
    mr_uint32_t error_after;
    mr_rpc_t *submit_rpc;
    mr_rpc_session_t *submit_on_error;
    mr_err_t submit_result;
    mr_err_t flush_result;
    mr_uint32_t send_calls;
    mr_uint32_t flush_calls;
    mr_bool_t blocked;
} test_rpc_io_t;

typedef struct test_rpc_packet {
    mr_uint8_t data[TEST_RPC_PACKET_SIZE];
    mr_uint32_t size;
} test_rpc_packet_t;

typedef struct test_rpc_owner {
    mr_rpc_session_t session;
    mr_err_t result;
    mr_uint32_t complete_count;
    mr_uint32_t release_count;
    mr_uint32_t entry_calls_at_complete;
    mr_bool_t cancel_on_complete;
    mr_err_t cancel_result;
    mr_uint32_t release_count_after_cancel;
} test_rpc_owner_t;

typedef enum test_rpc_mode {
    TEST_RPC_MODE_NORMAL,
    TEST_RPC_MODE_ERROR,
    TEST_RPC_MODE_HOLD,
    TEST_RPC_MODE_DIRECT_REPLY,
    TEST_RPC_MODE_EMPTY_STREAM,
    TEST_RPC_MODE_MIX_BAD_TITLE,
    TEST_RPC_MODE_MIX_BAD_NOTE,
    TEST_RPC_MODE_MIX_BAD_BLOB,
    TEST_RPC_MODE_NEST_BAD_NAME,
    TEST_RPC_MODE_TABLE_BAD_KEY0,
    TEST_RPC_MODE_TABLE_BAD_KEY1,
} test_rpc_mode_t;

static test_rpc_mode_t test_rpc_mode;
static mr_rpc_session_t *test_rpc_held[MR_CFG_MODULE_RPC_SESSION_NUM + 1U];
static mr_uint32_t test_rpc_held_count;
static mr_uint32_t test_rpc_entry_calls;
static __mr_rpc_request_lab_mix_t test_rpc_last_mix;
static __mr_rpc_request_lab_nest_t test_rpc_last_nest;
static __mr_rpc_request_lab_table_t test_rpc_last_table;
static __mr_rpc_request_lab_log_t test_rpc_last_log;

static void test_rpc_owner_complete(mr_rpc_session_t *session, mr_err_t result,
                                    void *owner) {
    test_rpc_owner_t *ctx = owner;

    ctx->result = result;
    ctx->complete_count++;
    ctx->entry_calls_at_complete = test_rpc_entry_calls;
    if (ctx->cancel_on_complete) {
        ctx->cancel_result = mr_rpc_session_cancel(session);
        ctx->release_count_after_cancel = ctx->release_count;
    }
}

static void test_rpc_owner_release(mr_rpc_session_t *session, void *owner) {
    test_rpc_owner_t *ctx = owner;

    MR_IGNORE(session);
    ctx->release_count++;
}

static const mr_rpc_session_ops_t test_rpc_external_ops = {
    .complete = test_rpc_owner_complete,
    .release = test_rpc_owner_release,
};

typedef struct test_rpc_dynamic_owner {
    mr_rpc_session_t *session;
    mr_err_t result;
    mr_uint32_t complete_count;
    mr_uint32_t release_count;
    mr_bool_t callback_failed;
    mr_bool_t consume_on_complete;
    mr_err_t consume_result;
} test_rpc_dynamic_owner_t;

static void test_rpc_dynamic_complete(mr_rpc_session_t *session,
                                      mr_err_t result, void *owner) {
    test_rpc_dynamic_owner_t *ctx = owner;

    if (ctx->session != session) {
        ctx->callback_failed = MR_TRUE;
    }
    ctx->result = result;
    ctx->complete_count++;
    if (ctx->consume_on_complete) {
        ctx->consume_result = mr_rpc_session_consume(session);
        if (ctx->release_count != 0U) {
            ctx->callback_failed = MR_TRUE;
        }
    }
}

static void test_rpc_dynamic_release(mr_rpc_session_t *session, void *owner) {
    test_rpc_dynamic_owner_t *ctx = owner;

    if (ctx->session != session) {
        ctx->callback_failed = MR_TRUE;
    }
    ctx->session = MR_NULL;
    ctx->release_count++;
    mr_free(session);
}

static const mr_rpc_session_ops_t test_rpc_dynamic_ops = {
    .complete = test_rpc_dynamic_complete,
    .release = test_rpc_dynamic_release,
};

static mr_err_t test_rpc_dynamic_submit(mr_rpc_t *rpc,
                                        test_rpc_dynamic_owner_t *owner,
                                        mr_uint16_t method, const void *data,
                                        mr_uint32_t size) {
    mr_err_t ret;

    owner->session = mr_malloc(sizeof(*owner->session));
    if (!owner->session) {
        return -MR_ENOMEM;
    }
    mr_memset(owner->session, 0, sizeof(*owner->session));
    mr_rpc_session_init(owner->session, &test_rpc_dynamic_ops, owner);
    owner->session->request.type = MR_RPC_MSG_REQUEST;
    owner->session->request.service = 1U;
    owner->session->request.method = method;
    owner->session->recv.size = size;
    if (size != 0U) {
        mr_memcpy(&owner->session->request.payload, data, size);
    }
    ret = mr_rpc_submit(rpc, owner->session);
    if (ret < 0) {
        mr_free(owner->session);
        owner->session = MR_NULL;
    }
    return ret;
}

#if defined(MR_USE_MODULE_RPC_HOOK)
static void test_rpc_dynamic_wakeup(mr_rpc_t *rpc, mr_rpc_event_t event,
                                    void *param) {
    if (event == MR_RPC_EVENT_EXECUTE) {
        *(mr_err_t *)param = mr_rpc_execute(rpc);
    }
}
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */

static mr_uint16_t test_rpc_fletcher16(const void *data, mr_uint32_t size) {
    const mr_uint8_t *bytes;
    mr_uint32_t sum1, sum2, n, x;

    /* Fletcher-16, delayed modulo 255, empty data is 0 */
    bytes = data;
    sum1 = 0U;
    sum2 = 0U;
    while (size != 0U) {
        n = size;
        if (n > 32U) {
            n = 32U;
        }
        size -= n;
        while (n != 0U) {
            sum1 += *bytes++;
            sum2 += sum1;
            n--;
        }
        x = (sum1 & 0xffU) + (sum1 >> 8);
        x = (x & 0xffU) + (x >> 8);
        if (x >= 255U) {
            x -= 255U;
        }
        sum1 = x;
        x = (sum2 & 0xffU) + (sum2 >> 8);
        x = (x & 0xffU) + (x >> 8);
        if (x >= 255U) {
            x -= 255U;
        }
        sum2 = x;
    }
    return (mr_uint16_t)((sum2 << 8) | sum1);
}

static mr_ssize_t test_rpc_send(void *priv, const void *buf, mr_uint32_t size) {
    test_rpc_io_t *io = priv;
    mr_uint32_t count;

    io->send_calls++;
    if ((io->send_error != 0)
        && ((io->error_after == 0U) || (io->size >= io->error_after))) {
        if (io->submit_on_error) {
            io->submit_result
                = mr_rpc_submit(io->submit_rpc, io->submit_on_error);
            io->submit_on_error = MR_NULL;
        }
        return io->send_error;
    }
    if (io->blocked) {
        return 0;
    }
    count = size;
    if ((io->limit != 0U) && (count > io->limit)) {
        count = io->limit;
    }
    if ((io->send_error != 0) && (io->error_after != 0U)) {
        count = MR_MIN(count, io->error_after - io->size);
    }
    if ((io->size + count) > sizeof(io->output)) {
        return -MR_ENOMEM;
    }
    mr_memcpy(&io->output[io->size], buf, count);
    io->size += count;
    return (mr_ssize_t)count;
}

static mr_err_t test_rpc_transport_flush(void *priv) {
    test_rpc_io_t *io = priv;

    io->flush_calls++;
    return io->flush_result;
}

static const mr_rpc_ops_t test_rpc_ops = {
    .send = test_rpc_send,
    .flush = test_rpc_transport_flush,
};

static const mr_rpc_ops_t test_rpc_ops_no_flush = {
    .send = test_rpc_send,
    .flush = MR_NULL,
};

static void test_rpc_io_reset(test_rpc_io_t *io) {
    mr_memset(io, 0, sizeof(*io));
}

static void test_rpc_output_reset(test_rpc_io_t *io) {
    io->size = 0U;
    io->send_calls = 0U;
    io->flush_calls = 0U;
}

static void test_rpc_entries_reset(void) {
    test_rpc_mode = TEST_RPC_MODE_NORMAL;
    test_rpc_held_count = 0U;
    test_rpc_entry_calls = 0U;
    mr_memset(test_rpc_held, 0, sizeof(test_rpc_held));
    mr_memset(&test_rpc_last_mix, 0, sizeof(test_rpc_last_mix));
    mr_memset(&test_rpc_last_nest, 0, sizeof(test_rpc_last_nest));
    mr_memset(&test_rpc_last_table, 0, sizeof(test_rpc_last_table));
    mr_memset(&test_rpc_last_log, 0, sizeof(test_rpc_last_log));
}

static mr_uint32_t test_rpc_packet_build(
    test_rpc_packet_t *packet, mr_rpc_msg_type_t type, mr_uint16_t flags,
    mr_uint32_t session, mr_uint16_t service, mr_uint16_t method,
    const void *payload, mr_uint32_t payload_size) {
    mr_rpc_frame_t frame;
    mr_uint16_t checksum;

    mr_memset(&frame, 0, sizeof(frame));
    frame.header[0] = 'M';
    frame.header[1] = 'R';
    frame.version = 1U;
    frame.type = (mr_uint8_t)type;
    frame.flags = flags;
    frame.session = session;
    frame.service = service;
    frame.method = method;
    frame.payload_size = payload_size;
    frame.hdr_checksum = test_rpc_fletcher16(&frame, sizeof(frame));

    mr_memcpy(packet->data, &frame, sizeof(frame));
    if (payload_size != 0U) {
        mr_memcpy(packet->data + sizeof(frame), payload, payload_size);
    }
    packet->size = sizeof(frame) + payload_size;
    if (flags & MR_RPC_FRAME_FLAG_CHECKSUM) {
        checksum = test_rpc_fletcher16(payload, payload_size);
        mr_memcpy(packet->data + packet->size, &checksum, sizeof(checksum));
        packet->size += sizeof(checksum);
    }
    return packet->size;
}

static mr_bool_t test_rpc_response_read(const test_rpc_io_t *io,
                                        mr_rpc_frame_t *response,
                                        const mr_uint8_t **payload) {
    mr_rpc_frame_t frame;
    mr_uint16_t actual_crc, checksum;
    mr_uint32_t total;

    if (io->size < sizeof(frame)) {
        return MR_FALSE;
    }
    mr_memcpy(&frame, io->output, sizeof(frame));
    actual_crc = frame.hdr_checksum;
    frame.hdr_checksum = 0U;
    if (test_rpc_fletcher16(&frame, sizeof(frame)) != actual_crc) {
        return MR_FALSE;
    }
    total = sizeof(frame) + frame.payload_size;
    if (frame.flags & MR_RPC_FRAME_FLAG_CHECKSUM) {
        total += TEST_RPC_CHECKSUM_SIZE;
    }
    if (io->size != total) {
        return MR_FALSE;
    }
    if (frame.flags & MR_RPC_FRAME_FLAG_CHECKSUM) {
        mr_memcpy(&checksum, io->output + sizeof(frame) + frame.payload_size,
                  sizeof(checksum));
        if (checksum
            != test_rpc_fletcher16(io->output + sizeof(frame),
                                   frame.payload_size)) {
            return MR_FALSE;
        }
    }
    frame.hdr_checksum = actual_crc;
    *response = frame;
    *payload = io->output + sizeof(frame);
    return MR_TRUE;
}

static mr_err_t test_rpc_exchange(mr_rpc_t *rpc, test_rpc_io_t *io,
                                  mr_rpc_msg_type_t type, mr_uint16_t flags,
                                  mr_uint32_t session, mr_uint16_t service,
                                  mr_uint16_t method, const void *payload,
                                  mr_uint32_t payload_size) {
    test_rpc_packet_t packet;
    mr_ssize_t ret;

    test_rpc_output_reset(io);
    test_rpc_packet_build(&packet, type, flags, session, service, method,
                          payload, payload_size);
    ret = mr_rpc_input(rpc, packet.data, packet.size);
    if (ret != (mr_ssize_t)packet.size) {
        return (ret < 0) ? (mr_err_t)ret : -MR_EIO;
    }
    return mr_rpc_execute(rpc);
}

static mr_bool_t test_rpc_result_is(const test_rpc_io_t *io,
                                    mr_uint32_t session, mr_err_t result) {
    const mr_uint8_t *payload;
    mr_rpc_frame_t response;

    if (!test_rpc_response_read(io, &response, &payload)) {
        return MR_FALSE;
    }
    MR_IGNORE(payload);
    return (response.header[0] == 'M') && (response.header[1] == 'R')
        && (response.version == 1U) && (response.type == MR_RPC_MSG_RESPONSE)
        && (response.session == session) && (response.value.result == result);
}

static mr_uint32_t test_rpc_mix_payload(mr_uint8_t *payload) {
    const char title[] = "mix";
    const char note[] = "note";
    const mr_uint8_t blob[] = {1U, 2U, 3U};
    mr_uint32_t count = 0x12345678U;
    mr_uint32_t off = 0U;

    payload[off++] = 3U;
    mr_memcpy(payload + off, title, 3U);
    off += 3U;
    mr_memcpy(payload + off, &count, sizeof(count));
    off += sizeof(count);
    payload[off++] = 4U;
    mr_memcpy(payload + off, note, 4U);
    off += 4U;
    payload[off++] = 3U;
    mr_memcpy(payload + off, blob, 3U);
    off += 3U;
    return off;
}

static mr_uint32_t test_rpc_nest_payload(mr_uint8_t *payload) {
    const char name[] = "node";
    __mr_rpc_idl_point_t point = {.x = -7, .y = 12};
    mr_uint32_t id = 42U;
    mr_uint32_t off = 0U;

    mr_memcpy(payload + off, &id, sizeof(id));
    off += sizeof(id);
    payload[off++] = 4U;
    mr_memcpy(payload + off, name, 4U);
    off += 4U;
    mr_memcpy(payload + off, &point, sizeof(point));
    off += sizeof(point);
    payload[off++] = 1U;
    payload[off++] = 2U;
    payload[off++] = 3U;
    payload[off++] = 4U;
    payload[off++] = MR_RPC_IDL_STATUS_BUSY;
    return off;
}

static mr_uint32_t test_rpc_table_payload(mr_uint8_t *payload) {
    mr_uint32_t first = 11U;
    mr_uint32_t second = 22U;
    mr_uint32_t off = 0U;

    payload[off++] = 1U;
    payload[off++] = 'a';
    mr_memcpy(payload + off, &first, sizeof(first));
    off += sizeof(first);
    payload[off++] = 2U;
    payload[off++] = 'b';
    payload[off++] = 'b';
    mr_memcpy(payload + off, &second, sizeof(second));
    off += sizeof(second);
    return off;
}

MR_RPC(lab, mix) {
    test_rpc_entry_calls++;
    test_rpc_last_mix = *request;
    if (test_rpc_mode == TEST_RPC_MODE_HOLD) {
        test_rpc_held[test_rpc_held_count++] = session;
        return mr_rpc_session_hold(session);
    }
    if (test_rpc_mode == TEST_RPC_MODE_DIRECT_REPLY) {
        return mr_rpc_session_reply(session, 23);
    }
    if (test_rpc_mode == TEST_RPC_MODE_ERROR) {
        return -MR_EIO;
    }
    response->title.size = 2U;
    mr_memcpy(response->title.data, "ok", 2U);
    response->count = request->count + 1U;
    response->note.size = 3U;
    mr_memcpy(response->note.data, "ack", 3U);
    response->blob.size = 2U;
    response->blob.data[0] = 0xa5U;
    response->blob.data[1] = 0x5aU;
    if (test_rpc_mode == TEST_RPC_MODE_MIX_BAD_TITLE) {
        response->title.size = 17U;
    } else if (test_rpc_mode == TEST_RPC_MODE_MIX_BAD_NOTE) {
        response->note.size = 33U;
    } else if (test_rpc_mode == TEST_RPC_MODE_MIX_BAD_BLOB) {
        response->blob.size = 41U;
    }
    return 7;
}

MR_RPC(lab, nest) {
    test_rpc_entry_calls++;
    test_rpc_last_nest = *request;
    if (test_rpc_mode == TEST_RPC_MODE_HOLD) {
        test_rpc_held[test_rpc_held_count++] = session;
        return mr_rpc_session_hold(session);
    }
    if (test_rpc_mode == TEST_RPC_MODE_ERROR) {
        return -MR_EIO;
    }
    response->item = request->item;
    if (test_rpc_mode == TEST_RPC_MODE_NEST_BAD_NAME) {
        response->item.name.size = 25U;
    }
    return 8;
}

MR_RPC(lab, table) {
    test_rpc_entry_calls++;
    test_rpc_last_table = *request;
    if (test_rpc_mode == TEST_RPC_MODE_HOLD) {
        test_rpc_held[test_rpc_held_count++] = session;
        return mr_rpc_session_hold(session);
    }
    if (test_rpc_mode == TEST_RPC_MODE_ERROR) {
        return -MR_EIO;
    }
    response->rows[0] = request->rows[0];
    response->rows[1] = request->rows[1];
    if (test_rpc_mode == TEST_RPC_MODE_TABLE_BAD_KEY0) {
        response->rows[0].key.size = 13U;
    } else if (test_rpc_mode == TEST_RPC_MODE_TABLE_BAD_KEY1) {
        response->rows[1].key.size = 13U;
    }
    return 9;
}

MR_RPC(lab, ping) {
    test_rpc_entry_calls++;
    if (test_rpc_mode == TEST_RPC_MODE_HOLD) {
        test_rpc_held[test_rpc_held_count++] = session;
        return mr_rpc_session_hold(session);
    }
    if (test_rpc_mode == TEST_RPC_MODE_DIRECT_REPLY) {
        return mr_rpc_session_reply(session, 23);
    }
    if (test_rpc_mode == TEST_RPC_MODE_EMPTY_STREAM) {
        return 0;
    }
    return (test_rpc_mode == TEST_RPC_MODE_ERROR) ? -MR_EIO : 10;
}

MR_RPC(lab, log) {
    test_rpc_entry_calls++;
    test_rpc_last_log = *request;
    if (test_rpc_mode == TEST_RPC_MODE_HOLD) {
        test_rpc_held[test_rpc_held_count++] = session;
        return mr_rpc_session_hold(session);
    }
    return 0;
}

MR_TEST(rpc, lifecycle_and_input) {
    mr_uint8_t input[MR_CFG_MODULE_RPC_FIFO_SIZE];
    test_rpc_io_t io;
    mr_rpc_t rpc;
    mr_rpc_t *dynamic;

    test_rpc_io_reset(&io);
    mr_memset(&rpc, 0, sizeof(rpc));
    MR_TEST_ASSERT_EQ_INT(mr_rpc_init(&rpc, &test_rpc_ops, &io), 0);
    MR_TEST_EXPECT_EQ_PTR(rpc.ops, &test_rpc_ops);
    MR_TEST_EXPECT_EQ_PTR(rpc.priv, &io);
    MR_TEST_EXPECT_EQ_INT(rpc.recv.state, MR_RPC_RECV_STATE_SYNC);
    MR_TEST_EXPECT_EQ_PTR(rpc.recv.session, MR_NULL);
    MR_TEST_EXPECT_EQ_PTR(rpc.send.session, MR_NULL);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_input(&rpc, MR_NULL, 0U), 0);
    mr_memset(input, 0x5a, sizeof(input));
    MR_TEST_ASSERT_EQ_INT(mr_rpc_input(&rpc, input, sizeof(input)),
                          sizeof(input));
    MR_TEST_EXPECT_EQ_INT(mr_rpc_input(&rpc, input, 1U), -MR_EAGAIN);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_del(&rpc), 0);
    MR_TEST_EXPECT_EQ_PTR(rpc.ops, MR_NULL);
    MR_TEST_EXPECT_EQ_PTR(rpc.priv, MR_NULL);

    dynamic = mr_rpc_create(&test_rpc_ops, &io);
    MR_TEST_ASSERT_NE_PTR(dynamic, MR_NULL);
    MR_TEST_EXPECT_EQ_PTR(mr_rpc_get(dynamic), dynamic);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_put(dynamic), MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_del(dynamic), 0);
}

MR_TEST(rpc, valid_idl_roundtrip) {
    const mr_uint8_t *response_payload;
    mr_uint8_t payload[128];
    mr_rpc_frame_t response;
    test_rpc_io_t io;
    mr_rpc_t rpc;
    mr_uint32_t size;

    test_rpc_io_reset(&io);
    test_rpc_entries_reset();
    mr_memset(&rpc, 0, sizeof(rpc));
    MR_TEST_ASSERT_EQ_INT(mr_rpc_init(&rpc, &test_rpc_ops, &io), 0);

    size = test_rpc_mix_payload(payload);
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST,
                                            MR_RPC_FRAME_FLAG_CHECKSUM, 101U,
                                            1U, 0U, payload, size),
                          0);
    MR_TEST_ASSERT_EQ_INT(
        test_rpc_response_read(&io, &response, &response_payload), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(response.value.result, 7);
    MR_TEST_EXPECT_EQ_UINT(response.session, 101U);
    MR_TEST_EXPECT_EQ_UINT(response.flags, MR_RPC_FRAME_FLAG_CHECKSUM);
    MR_TEST_EXPECT_EQ_UINT(test_rpc_last_mix.count, 0x12345678U);
    MR_TEST_EXPECT_EQ_UINT(test_rpc_last_mix.title.size, 3U);
    MR_TEST_EXPECT_EQ_INT(test_rpc_last_mix.title.data[2], 'x');
    MR_TEST_EXPECT_EQ_UINT(test_rpc_last_mix.note.size, 4U);
    MR_TEST_EXPECT_EQ_INT(test_rpc_last_mix.note.data[3], 'e');
    MR_TEST_EXPECT_EQ_UINT(response_payload[0], 2U);

    size = test_rpc_nest_payload(payload);
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST, 0U,
                                            102U, 1U, 1U, payload, size),
                          0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 102U, 8), MR_TRUE);
    MR_TEST_EXPECT_EQ_UINT(test_rpc_last_nest.item.id, 42U);
    MR_TEST_EXPECT_EQ_UINT(test_rpc_last_nest.item.name.size, 4U);
    MR_TEST_EXPECT_EQ_INT(test_rpc_last_nest.item.name.data[3], 'e');

    size = test_rpc_table_payload(payload);
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST, 0U,
                                            103U, 1U, 2U, payload, size),
                          0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 103U, 9), MR_TRUE);
    MR_TEST_EXPECT_EQ_UINT(test_rpc_last_table.rows[0].val, 11U);
    MR_TEST_EXPECT_EQ_UINT(test_rpc_last_table.rows[1].val, 22U);

    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST, 0U,
                                            104U, 1U, 3U, MR_NULL, 0U),
                          0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 104U, 10), MR_TRUE);
    MR_TEST_EXPECT_EQ_UINT(test_rpc_entry_calls, 4U);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_del(&rpc), 0);
}

MR_TEST(rpc, stream_resync_and_checksum_errors) {
    test_rpc_packet_t packet;
    mr_rpc_frame_t *frame;
    test_rpc_io_t io;
    mr_rpc_t rpc;
    mr_uint8_t noise[3] = {'x', 'M', 'x'};
    mr_uint16_t bad_checksum = 1U;

    test_rpc_io_reset(&io);
    test_rpc_entries_reset();
    mr_memset(&rpc, 0, sizeof(rpc));
    MR_TEST_ASSERT_EQ_INT(mr_rpc_init(&rpc, &test_rpc_ops, &io), 0);

    MR_TEST_ASSERT_EQ_INT(mr_rpc_input(&rpc, noise, sizeof(noise)),
                          sizeof(noise));
    MR_TEST_EXPECT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_EXPECT_EQ_UINT(io.size, 0U);

    test_rpc_packet_build(&packet, MR_RPC_MSG_REQUEST, 0U, 201U, 1U, 3U,
                          MR_NULL, 0U);
    frame = (mr_rpc_frame_t *)packet.data;
    frame->version = 2U;
    MR_TEST_ASSERT_EQ_INT(mr_rpc_input(&rpc, packet.data, packet.size),
                          packet.size);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_execute(&rpc), 0);

    test_rpc_packet_build(&packet, MR_RPC_MSG_REQUEST, 0U, 202U, 1U, 3U,
                          MR_NULL, 0U);
    frame = (mr_rpc_frame_t *)packet.data;
    frame->type = 0xffU;
    MR_TEST_ASSERT_EQ_INT(mr_rpc_input(&rpc, packet.data, packet.size),
                          packet.size);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_execute(&rpc), 0);

    test_rpc_packet_build(&packet, MR_RPC_MSG_REQUEST, 0U, 203U, 1U, 3U,
                          MR_NULL, 0U);
    frame = (mr_rpc_frame_t *)packet.data;
    frame->hdr_checksum ^= 1U;
    MR_TEST_ASSERT_EQ_INT(mr_rpc_input(&rpc, packet.data, packet.size),
                          packet.size);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_execute(&rpc), 0);

    test_rpc_output_reset(&io);
    test_rpc_packet_build(&packet, MR_RPC_MSG_REQUEST,
                          MR_RPC_FRAME_FLAG_CHECKSUM, 204U, 1U, 3U, MR_NULL,
                          0U);
    mr_memcpy(packet.data + TEST_RPC_FRAME_SIZE, &bad_checksum,
              sizeof(bad_checksum));
    MR_TEST_ASSERT_EQ_INT(mr_rpc_input(&rpc, packet.data, TEST_RPC_FRAME_SIZE),
                          TEST_RPC_FRAME_SIZE);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_EXPECT_EQ_UINT(io.size, 0U);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_input(&rpc, packet.data + TEST_RPC_FRAME_SIZE,
                                       TEST_RPC_CHECKSUM_SIZE),
                          TEST_RPC_CHECKSUM_SIZE);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 204U, -MR_EINVAL), MR_TRUE);
    MR_TEST_EXPECT_EQ_UINT(test_rpc_entry_calls, 0U);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_del(&rpc), 0);
}

MR_TEST(rpc, oversized_payload_discard) {
    mr_uint8_t payload[sizeof(mr_rpc_idl_request_t) + 1U];
    test_rpc_packet_t packet;
    test_rpc_io_t io;
    mr_rpc_t rpc;

    test_rpc_io_reset(&io);
    test_rpc_entries_reset();
    mr_memset(payload, 0x33, sizeof(payload));
    mr_memset(&rpc, 0, sizeof(rpc));
    MR_TEST_ASSERT_EQ_INT(mr_rpc_init(&rpc, &test_rpc_ops, &io), 0);

    test_rpc_packet_build(&packet, MR_RPC_MSG_REQUEST, 0U, 301U, 1U, 0U,
                          payload, sizeof(payload));
    MR_TEST_ASSERT_EQ_INT(mr_rpc_input(&rpc, packet.data, TEST_RPC_FRAME_SIZE),
                          TEST_RPC_FRAME_SIZE);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_input(&rpc, packet.data + TEST_RPC_FRAME_SIZE,
                                       sizeof(payload) - 1U),
                          sizeof(payload) - 1U);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_EXPECT_EQ_UINT(io.size, 0U);
    MR_TEST_ASSERT_EQ_INT(
        mr_rpc_input(&rpc, packet.data + packet.size - 1U, 1U), 1);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 301U, -MR_ENOMEM), MR_TRUE);

    test_rpc_output_reset(&io);
    test_rpc_packet_build(&packet, MR_RPC_MSG_REQUEST,
                          MR_RPC_FRAME_FLAG_CHECKSUM, 302U, 1U, 0U, payload,
                          sizeof(payload));
    MR_TEST_ASSERT_EQ_INT(mr_rpc_input(&rpc, packet.data, packet.size - 1U),
                          packet.size - 1U);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_EXPECT_EQ_UINT(io.size, 0U);
    MR_TEST_ASSERT_EQ_INT(
        mr_rpc_input(&rpc, packet.data + packet.size - 1U, 1U), 1);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 302U, -MR_ENOMEM), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_del(&rpc), 0);
}

MR_TEST(rpc, send_flow_control) {
    test_rpc_io_t io;
    mr_rpc_t rpc;

    test_rpc_io_reset(&io);
    test_rpc_entries_reset();
    mr_memset(&rpc, 0, sizeof(rpc));
    MR_TEST_ASSERT_EQ_INT(mr_rpc_init(&rpc, &test_rpc_ops, &io), 0);

    io.blocked = MR_TRUE;
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST, 0U,
                                            401U, 1U, 3U, MR_NULL, 0U),
                          -MR_EAGAIN);
    MR_TEST_EXPECT_EQ_UINT(io.size, 0U);
    MR_TEST_EXPECT_NE_PTR(rpc.send.session, MR_NULL);
    io.blocked = MR_FALSE;
    io.limit = 3U;
    MR_TEST_EXPECT_EQ_INT(mr_rpc_flush(&rpc), 0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 401U, 10), MR_TRUE);
    MR_TEST_EXPECT_GT_UINT(io.send_calls, 1U);
    MR_TEST_EXPECT_EQ_UINT(io.flush_calls, 2U);

    io.send_error = -MR_EIO;
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST, 0U,
                                            402U, 1U, 3U, MR_NULL, 0U),
                          -MR_EIO);
    MR_TEST_EXPECT_EQ_UINT(io.size, 0U);
    io.send_error = 0;
    MR_TEST_EXPECT_EQ_INT(mr_rpc_flush(&rpc), 0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 402U, 10), MR_TRUE);

    test_rpc_output_reset(&io);
    io.flush_result = -MR_EIO;
    MR_TEST_EXPECT_EQ_INT(mr_rpc_flush(&rpc), -MR_EIO);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_del(&rpc), 0);

    test_rpc_io_reset(&io);
    mr_memset(&rpc, 0, sizeof(rpc));
    MR_TEST_ASSERT_EQ_INT(mr_rpc_init(&rpc, &test_rpc_ops_no_flush, &io), 0);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_flush(&rpc), 0);
    MR_TEST_EXPECT_EQ_UINT(io.flush_calls, 0U);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_del(&rpc), 0);
}

MR_TEST(rpc, session_hold_reply_drop_and_pool) {
    test_rpc_packet_t packet;
    test_rpc_io_t io;
    mr_rpc_t rpc;
    mr_uint32_t i;

    test_rpc_io_reset(&io);
    test_rpc_entries_reset();
    mr_memset(&rpc, 0, sizeof(rpc));
    MR_TEST_ASSERT_EQ_INT(mr_rpc_init(&rpc, &test_rpc_ops, &io), 0);
    test_rpc_mode = TEST_RPC_MODE_HOLD;

    for (i = 0U; i < MR_CFG_MODULE_RPC_SESSION_NUM; i++) {
        MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST,
                                                0U, 500U + i, 1U, 3U, MR_NULL,
                                                0U),
                              0);
        MR_TEST_EXPECT_EQ_UINT(io.size, 0U);
    }
    MR_TEST_ASSERT_EQ_UINT(test_rpc_held_count, MR_CFG_MODULE_RPC_SESSION_NUM);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_session_hold(test_rpc_held[0]), -MR_EPERM);

    test_rpc_packet_build(&packet, MR_RPC_MSG_REQUEST, 0U, 599U, 1U, 3U,
                          MR_NULL, 0U);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_input(&rpc, packet.data, packet.size),
                          packet.size);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_EXPECT_EQ_UINT(test_rpc_held_count, MR_CFG_MODULE_RPC_SESSION_NUM);

    MR_TEST_ASSERT_EQ_INT(mr_rpc_session_reply(test_rpc_held[0], 77), 0);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_session_reply(test_rpc_held[0], 77),
                          -MR_EPERM);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_flush(&rpc), 0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 500U, 77), MR_TRUE);

    test_rpc_output_reset(&io);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_EXPECT_EQ_UINT(test_rpc_held_count,
                           MR_CFG_MODULE_RPC_SESSION_NUM + 1U);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_session_drop(test_rpc_held[1]), 0);
    MR_TEST_EXPECT_EQ_INT(
        mr_rpc_session_drop(test_rpc_held[MR_CFG_MODULE_RPC_SESSION_NUM]), 0);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_del(&rpc), 0);
}

MR_TEST(rpc, reply_and_hold_rejections) {
    test_rpc_io_t io;
    mr_rpc_t rpc;
    mr_rpc_session_t *session;

    test_rpc_io_reset(&io);
    mr_memset(&rpc, 0, sizeof(rpc));
    MR_TEST_ASSERT_EQ_INT(mr_rpc_init(&rpc, &test_rpc_ops, &io), 0);
    session = &rpc.sessions[0];
    session->rpc = &rpc;
    session->request.type = MR_RPC_MSG_REQUEST;
    session->state = MR_RPC_SESSION_STATE_FREE;
    MR_TEST_EXPECT_EQ_INT(mr_rpc_session_reply(session, 0), -MR_EPERM);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_session_hold(session), -MR_EPERM);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_session_drop(session), -MR_EPERM);
    session->request.type = MR_RPC_MSG_ONEWAY;
    MR_TEST_EXPECT_EQ_INT(mr_rpc_session_reply(session, 0), -MR_EPERM);
    session->rpc = MR_NULL;
    MR_TEST_EXPECT_EQ_INT(mr_rpc_del(&rpc), 0);
}

MR_TEST(rpc, mix_idl_error_matrix) {
    mr_uint8_t valid[128];
    mr_uint8_t payload[128];
    test_rpc_io_t io;
    mr_rpc_t rpc;
    mr_uint32_t valid_size;
    mr_uint32_t count = 1U;
    mr_uint32_t off;
    mr_uint32_t i;
    const mr_uint32_t truncated[] = {0U, 1U, 5U, 6U};

    test_rpc_io_reset(&io);
    test_rpc_entries_reset();
    mr_memset(&rpc, 0, sizeof(rpc));
    MR_TEST_ASSERT_EQ_INT(mr_rpc_init(&rpc, &test_rpc_ops, &io), 0);
    valid_size = test_rpc_mix_payload(valid);

    for (i = 0U; i < MR_ARRAY_SIZE(truncated); i++) {
        MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST,
                                                0U, 600U + i, 1U, 0U, valid,
                                                truncated[i]),
                              0);
        MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 600U + i, -MR_EINVAL),
                              MR_TRUE);
    }

    payload[0] = 17U;
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST, 0U,
                                            610U, 1U, 0U, payload, 1U),
                          0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 610U, -MR_EINVAL), MR_TRUE);

    off = 0U;
    payload[off++] = 0U;
    mr_memcpy(payload + off, &count, sizeof(count));
    off += sizeof(count);
    payload[off++] = 33U;
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST, 0U,
                                            611U, 1U, 0U, payload, off),
                          0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 611U, -MR_EINVAL), MR_TRUE);
    payload[off - 1U] = 2U;
    payload[off++] = 'x';
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST, 0U,
                                            612U, 1U, 0U, payload, off),
                          0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 612U, -MR_EINVAL), MR_TRUE);

    payload[off - 2U] = 1U;
    payload[off - 1U] = 'x';
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST, 0U,
                                            613U, 1U, 0U, payload, off),
                          0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 613U, -MR_EINVAL), MR_TRUE);
    payload[off++] = 41U;
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST, 0U,
                                            614U, 1U, 0U, payload, off),
                          0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 614U, -MR_EINVAL), MR_TRUE);
    payload[off - 1U] = 2U;
    payload[off++] = 1U;
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST, 0U,
                                            615U, 1U, 0U, payload, off),
                          0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 615U, -MR_EINVAL), MR_TRUE);

    mr_memcpy(payload, valid, valid_size);
    payload[valid_size] = 0U;
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST, 0U,
                                            616U, 1U, 0U, payload,
                                            valid_size + 1U),
                          0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 616U, -MR_EINVAL), MR_TRUE);

    test_rpc_mode = TEST_RPC_MODE_ERROR;
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST, 0U,
                                            620U, 1U, 0U, valid, valid_size),
                          0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 620U, -MR_EIO), MR_TRUE);
    for (i = TEST_RPC_MODE_MIX_BAD_TITLE; i <= TEST_RPC_MODE_MIX_BAD_BLOB;
         i++) {
        test_rpc_mode = (test_rpc_mode_t)i;
        MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST,
                                                0U, 621U + i, 1U, 0U, valid,
                                                valid_size),
                              0);
        MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 621U + i, -MR_EINVAL),
                              MR_TRUE);
    }
    test_rpc_mode = TEST_RPC_MODE_DIRECT_REPLY;
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST, 0U,
                                            630U, 1U, 0U, valid, valid_size),
                          0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 630U, 23), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_del(&rpc), 0);
}

MR_TEST(rpc, nest_idl_error_matrix) {
    mr_uint8_t valid[64];
    mr_uint8_t payload[64];
    test_rpc_io_t io;
    mr_rpc_t rpc;
    mr_uint32_t valid_size;
    mr_uint32_t i;
    const mr_uint32_t truncated[] = {0U, 4U, 5U, 9U, 13U, 17U};

    test_rpc_io_reset(&io);
    test_rpc_entries_reset();
    mr_memset(&rpc, 0, sizeof(rpc));
    MR_TEST_ASSERT_EQ_INT(mr_rpc_init(&rpc, &test_rpc_ops, &io), 0);
    valid_size = test_rpc_nest_payload(valid);

    for (i = 0U; i < MR_ARRAY_SIZE(truncated); i++) {
        MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST,
                                                0U, 700U + i, 1U, 1U, valid,
                                                truncated[i]),
                              0);
        MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 700U + i, -MR_EINVAL),
                              MR_TRUE);
    }
    mr_memcpy(payload, valid, valid_size);
    payload[4] = 25U;
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST, 0U,
                                            710U, 1U, 1U, payload, 5U),
                          0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 710U, -MR_EINVAL), MR_TRUE);
    mr_memcpy(payload, valid, valid_size);
    payload[valid_size] = 0U;
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST, 0U,
                                            711U, 1U, 1U, payload,
                                            valid_size + 1U),
                          0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 711U, -MR_EINVAL), MR_TRUE);

    test_rpc_mode = TEST_RPC_MODE_ERROR;
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST, 0U,
                                            712U, 1U, 1U, valid, valid_size),
                          0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 712U, -MR_EIO), MR_TRUE);
    test_rpc_mode = TEST_RPC_MODE_NEST_BAD_NAME;
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST, 0U,
                                            713U, 1U, 1U, valid, valid_size),
                          0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 713U, -MR_EINVAL), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_del(&rpc), 0);
}

MR_TEST(rpc, table_idl_error_matrix) {
    mr_uint8_t valid[64];
    mr_uint8_t payload[64];
    test_rpc_io_t io;
    mr_rpc_t rpc;
    mr_uint32_t valid_size;
    mr_uint32_t i;
    const mr_uint32_t truncated[] = {0U, 1U, 2U, 6U, 7U, 9U};

    test_rpc_io_reset(&io);
    test_rpc_entries_reset();
    mr_memset(&rpc, 0, sizeof(rpc));
    MR_TEST_ASSERT_EQ_INT(mr_rpc_init(&rpc, &test_rpc_ops, &io), 0);
    valid_size = test_rpc_table_payload(valid);

    for (i = 0U; i < MR_ARRAY_SIZE(truncated); i++) {
        MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST,
                                                0U, 800U + i, 1U, 2U, valid,
                                                truncated[i]),
                              0);
        MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 800U + i, -MR_EINVAL),
                              MR_TRUE);
    }
    payload[0] = 13U;
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST, 0U,
                                            810U, 1U, 2U, payload, 1U),
                          0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 810U, -MR_EINVAL), MR_TRUE);
    mr_memcpy(payload, valid, valid_size);
    payload[6] = 13U;
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST, 0U,
                                            811U, 1U, 2U, payload, 7U),
                          0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 811U, -MR_EINVAL), MR_TRUE);
    mr_memcpy(payload, valid, valid_size);
    payload[valid_size] = 0U;
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST, 0U,
                                            812U, 1U, 2U, payload,
                                            valid_size + 1U),
                          0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 812U, -MR_EINVAL), MR_TRUE);

    test_rpc_mode = TEST_RPC_MODE_ERROR;
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST, 0U,
                                            813U, 1U, 2U, valid, valid_size),
                          0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 813U, -MR_EIO), MR_TRUE);
    test_rpc_mode = TEST_RPC_MODE_TABLE_BAD_KEY0;
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST, 0U,
                                            814U, 1U, 2U, valid, valid_size),
                          0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 814U, -MR_EINVAL), MR_TRUE);
    test_rpc_mode = TEST_RPC_MODE_TABLE_BAD_KEY1;
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST, 0U,
                                            815U, 1U, 2U, valid, valid_size),
                          0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 815U, -MR_EINVAL), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_del(&rpc), 0);
}

static mr_uint32_t test_rpc_subscribe_packet_build(test_rpc_packet_t *packet,
                                                   mr_rpc_msg_type_t type,
                                                   mr_uint32_t session,
                                                   mr_uint32_t subscribe) {
    mr_rpc_frame_t *frame;
    mr_uint32_t size;

    size
        = test_rpc_packet_build(packet, type, 0U, session, 1U, 3U, MR_NULL, 0U);
    frame = (mr_rpc_frame_t *)packet->data;
    frame->value.subscribe = subscribe;
    frame->hdr_checksum = 0U;
    frame->hdr_checksum = test_rpc_fletcher16(frame, sizeof(*frame));
    return size;
}

MR_TEST(rpc, subscribe_poll_and_unsubscribe) {
    const mr_uint8_t *response_payload;
    mr_uint8_t payload[4];
    test_rpc_packet_t packet;
    mr_rpc_frame_t response;
    test_rpc_io_t io;
    mr_rpc_t rpc;

    test_rpc_io_reset(&io);
    test_rpc_entries_reset();
    mr_memset(&rpc, 0, sizeof(rpc));
    MR_TEST_ASSERT_EQ_INT(mr_rpc_init(&rpc, &test_rpc_ops, &io), 0);

    /* A continuous subscription answers once and then polls as a stream */
    test_rpc_subscribe_packet_build(&packet, MR_RPC_MSG_SUBSCRIBE, 1100U,
                                    50U | MR_RPC_SUBSCRIBE_CONTINUOUS);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_input(&rpc, packet.data, packet.size),
                          packet.size);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_ASSERT_EQ_INT(
        test_rpc_response_read(&io, &response, &response_payload), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(response.type, MR_RPC_MSG_RESPONSE);
    MR_TEST_EXPECT_EQ_UINT(response.session, 1100U);
    MR_TEST_EXPECT_EQ_INT(response.value.result, 0);
    MR_TEST_EXPECT_EQ_UINT(test_rpc_entry_calls, 0U);

    /* Polling dispatches the subscription and answers with a stream frame */
    test_rpc_output_reset(&io);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_poll(&rpc), 0);
    MR_TEST_ASSERT_EQ_INT(
        test_rpc_response_read(&io, &response, &response_payload), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(response.type, MR_RPC_MSG_STREAM);
    MR_TEST_EXPECT_EQ_UINT(response.session, 1100U);
    MR_TEST_EXPECT_EQ_INT(response.value.result, 10);
    MR_TEST_EXPECT_EQ_UINT(test_rpc_entry_calls, 1U);

    /* Every poll re-serves the same continuous subscription */
    test_rpc_output_reset(&io);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_poll(&rpc), 0);
    MR_TEST_ASSERT_EQ_INT(
        test_rpc_response_read(&io, &response, &response_payload), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(response.type, MR_RPC_MSG_STREAM);
    MR_TEST_EXPECT_EQ_UINT(test_rpc_entry_calls, 2U);

    /* A duplicate subscription id is rejected */
    test_rpc_output_reset(&io);
    test_rpc_subscribe_packet_build(&packet, MR_RPC_MSG_SUBSCRIBE, 1100U,
                                    50U | MR_RPC_SUBSCRIBE_CONTINUOUS);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_input(&rpc, packet.data, packet.size),
                          packet.size);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_ASSERT_EQ_INT(
        test_rpc_response_read(&io, &response, &response_payload), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(response.type, MR_RPC_MSG_RESPONSE);
    MR_TEST_EXPECT_EQ_INT(response.value.result, -MR_EEXIST);

    /* A zero subscription period is rejected */
    test_rpc_output_reset(&io);
    test_rpc_subscribe_packet_build(&packet, MR_RPC_MSG_SUBSCRIBE, 1101U, 0U);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_input(&rpc, packet.data, packet.size),
                          packet.size);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_ASSERT_EQ_INT(
        test_rpc_response_read(&io, &response, &response_payload), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(response.value.result, -MR_EINVAL);

    /* The original subscription keeps polling */
    test_rpc_output_reset(&io);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_poll(&rpc), 0);
    MR_TEST_ASSERT_EQ_INT(
        test_rpc_response_read(&io, &response, &response_payload), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(response.type, MR_RPC_MSG_STREAM);
    MR_TEST_EXPECT_EQ_UINT(response.session, 1100U);

    /* Cancel the continuous subscription */
    test_rpc_output_reset(&io);
    test_rpc_subscribe_packet_build(&packet, MR_RPC_MSG_UNSUBSCRIBE, 1100U, 0U);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_input(&rpc, packet.data, packet.size),
                          packet.size);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_ASSERT_EQ_INT(
        test_rpc_response_read(&io, &response, &response_payload), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(response.value.result, 0);
    test_rpc_output_reset(&io);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_poll(&rpc), 0);
    MR_TEST_EXPECT_EQ_UINT(io.size, 0U);

    /* Cancelling an unknown subscription id is rejected */
    test_rpc_output_reset(&io);
    test_rpc_subscribe_packet_build(&packet, MR_RPC_MSG_UNSUBSCRIBE, 1100U, 0U);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_input(&rpc, packet.data, packet.size),
                          packet.size);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_ASSERT_EQ_INT(
        test_rpc_response_read(&io, &response, &response_payload), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(response.value.result, -MR_ENOENT);

    /* Cancellation with a payload is invalid */
    test_rpc_output_reset(&io);
    payload[0] = 0U;
    test_rpc_packet_build(&packet, MR_RPC_MSG_UNSUBSCRIBE, 0U, 1102U, 1U, 3U,
                          payload, 1U);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_input(&rpc, packet.data, packet.size),
                          packet.size);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_ASSERT_EQ_INT(
        test_rpc_response_read(&io, &response, &response_payload), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(response.value.result, -MR_EINVAL);

    /* A periodic subscription waits on the timer and never polls early */
    test_rpc_output_reset(&io);
    test_rpc_subscribe_packet_build(&packet, MR_RPC_MSG_SUBSCRIBE, 1200U, 50U);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_input(&rpc, packet.data, packet.size),
                          packet.size);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_ASSERT_EQ_INT(
        test_rpc_response_read(&io, &response, &response_payload), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(response.value.result, 0);
    test_rpc_output_reset(&io);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_poll(&rpc), 0);
    MR_TEST_EXPECT_EQ_UINT(io.size, 0U);
    MR_TEST_EXPECT_EQ_UINT(test_rpc_entry_calls, 3U);

    /* Cancellation recycles the waiting subscription */
    test_rpc_subscribe_packet_build(&packet, MR_RPC_MSG_UNSUBSCRIBE, 1200U, 0U);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_input(&rpc, packet.data, packet.size),
                          packet.size);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_ASSERT_EQ_INT(
        test_rpc_response_read(&io, &response, &response_payload), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(response.value.result, 0);
    test_rpc_output_reset(&io);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_poll(&rpc), 0);
    MR_TEST_EXPECT_EQ_UINT(io.size, 0U);

    /* The pool accepts a new subscription after cancellation */
    test_rpc_output_reset(&io);
    test_rpc_subscribe_packet_build(&packet, MR_RPC_MSG_SUBSCRIBE, 1200U, 50U);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_input(&rpc, packet.data, packet.size),
                          packet.size);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_ASSERT_EQ_INT(
        test_rpc_response_read(&io, &response, &response_payload), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(response.value.result, 0);
    test_rpc_subscribe_packet_build(&packet, MR_RPC_MSG_UNSUBSCRIBE, 1200U, 0U);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_input(&rpc, packet.data, packet.size),
                          packet.size);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_del(&rpc), 0);
}

MR_TEST(rpc, ping_log_and_unknown_types) {
    mr_uint8_t payload[64];
    test_rpc_io_t io;
    mr_rpc_t rpc;
    mr_uint32_t i;
    const mr_uint32_t bad_log_sizes[] = {0U, 1U, 3U};

    test_rpc_io_reset(&io);
    test_rpc_entries_reset();
    mr_memset(&rpc, 0, sizeof(rpc));
    MR_TEST_ASSERT_EQ_INT(mr_rpc_init(&rpc, &test_rpc_ops, &io), 0);

    payload[0] = 0U;
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST, 0U,
                                            900U, 1U, 3U, payload, 1U),
                          0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 900U, -MR_EINVAL), MR_TRUE);
    test_rpc_mode = TEST_RPC_MODE_ERROR;
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST, 0U,
                                            901U, 1U, 3U, MR_NULL, 0U),
                          0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 901U, -MR_EIO), MR_TRUE);
    test_rpc_mode = TEST_RPC_MODE_DIRECT_REPLY;
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST, 0U,
                                            902U, 1U, 3U, MR_NULL, 0U),
                          0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 902U, 23), MR_TRUE);

    test_rpc_mode = TEST_RPC_MODE_NORMAL;
    payload[0] = 3U;
    mr_memcpy(payload + 1U, "log", 3U);
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_ONEWAY, 0U,
                                            903U, 1U, 4U, payload, 4U),
                          0);
    MR_TEST_EXPECT_EQ_UINT(io.size, 0U);
    MR_TEST_EXPECT_EQ_UINT(test_rpc_last_log.msg.size, 3U);
    MR_TEST_EXPECT_EQ_INT(test_rpc_last_log.msg.data[2], 'g');

    for (i = 0U; i < MR_ARRAY_SIZE(bad_log_sizes); i++) {
        payload[0] = (i == 1U) ? 49U : 1U;
        if (i == 2U) {
            payload[1] = 'x';
            payload[2] = 'y';
        }
        MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_ONEWAY,
                                                0U, 910U + i, 1U, 4U, payload,
                                                bad_log_sizes[i]),
                              0);
        MR_TEST_EXPECT_EQ_UINT(io.size, 0U);
    }

    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST, 0U,
                                            920U, 99U, 99U, MR_NULL, 0U),
                          0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 920U, -MR_ENOSYS), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_ONEWAY, 0U,
                                            921U, 99U, 99U, MR_NULL, 0U),
                          0);
    MR_TEST_EXPECT_EQ_UINT(io.size, 0U);
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_RESPONSE, 0U,
                                            922U, 1U, 3U, MR_NULL, 0U),
                          0);
    MR_TEST_EXPECT_EQ_UINT(io.size, 0U);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_del(&rpc), 0);
}

MR_TEST(rpc, delete_with_held_session) {
    test_rpc_io_t io;
    mr_rpc_t rpc;

    test_rpc_io_reset(&io);
    test_rpc_entries_reset();
    mr_memset(&rpc, 0, sizeof(rpc));
    MR_TEST_ASSERT_EQ_INT(mr_rpc_init(&rpc, &test_rpc_ops, &io), 0);
    test_rpc_mode = TEST_RPC_MODE_HOLD;
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST, 0U,
                                            1001U, 1U, 3U, MR_NULL, 0U),
                          0);
    MR_TEST_ASSERT_EQ_UINT(test_rpc_held_count, 1U);

    /* The held session keeps RPC alive until drop */
    MR_TEST_EXPECT_EQ_INT(mr_rpc_del(&rpc), 0);
    MR_TEST_EXPECT_EQ_PTR(rpc.ops, &test_rpc_ops);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_session_drop(test_rpc_held[0]), 0);
    MR_TEST_EXPECT_EQ_PTR(rpc.ops, MR_NULL);
}

MR_TEST(rpc, external_session_delivery) {
    test_rpc_owner_t owner;
    mr_rpc_t rpc;

    mr_memset(&owner, 0, sizeof(owner));
    mr_memset(&rpc, 0, sizeof(rpc));
    test_rpc_entries_reset();
    MR_TEST_ASSERT_EQ_INT(mr_rpc_init(&rpc, MR_NULL, MR_NULL), 0);

    /* A local request delivers one result before release */
    mr_rpc_session_init(&owner.session, &test_rpc_external_ops, &owner);
    owner.session.request.type = MR_RPC_MSG_REQUEST;
    owner.session.request.service = 1U;
    owner.session.request.method = 3U;
    MR_TEST_ASSERT_EQ_INT(mr_rpc_submit(&rpc, &owner.session), 0);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_EXPECT_EQ_UINT(owner.complete_count, 1U);
    MR_TEST_EXPECT_EQ_UINT(owner.release_count, 0U);
    MR_TEST_EXPECT_EQ_INT(owner.result, 10);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_session_consume(&owner.session), 0);
    MR_TEST_EXPECT_EQ_UINT(owner.release_count, 1U);

    /* ONEWAY completes and releases without a consume step */
    mr_rpc_session_init(&owner.session, &test_rpc_external_ops, &owner);
    owner.session.request.type = MR_RPC_MSG_ONEWAY;
    owner.session.request.service = 1U;
    owner.session.request.method = 3U;
    MR_TEST_ASSERT_EQ_INT(mr_rpc_submit(&rpc, &owner.session), 0);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_EXPECT_EQ_UINT(owner.complete_count, 2U);
    MR_TEST_EXPECT_EQ_UINT(owner.release_count, 2U);
    MR_TEST_EXPECT_EQ_INT(owner.result, 10);

    /* A continuous subscription repolls after its result is consumed */
    mr_rpc_session_init(&owner.session, &test_rpc_external_ops, &owner);
    owner.session.request.type = MR_RPC_MSG_SUBSCRIBE;
    owner.session.request.service = 1U;
    owner.session.request.method = 3U;
    owner.session.request.value.subscribe = 1U | MR_RPC_SUBSCRIBE_CONTINUOUS;
    MR_TEST_ASSERT_EQ_INT(mr_rpc_submit(&rpc, &owner.session), 0);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_EXPECT_EQ_UINT(owner.complete_count, 3U);
    MR_TEST_EXPECT_EQ_INT(owner.result, 0);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_session_consume(&owner.session), 0);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_poll(&rpc), 0);
    MR_TEST_EXPECT_EQ_UINT(owner.complete_count, 4U);
    MR_TEST_EXPECT_EQ_INT(owner.result, 10);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_session_cancel(&owner.session), 0);
    MR_TEST_EXPECT_EQ_UINT(owner.release_count, 3U);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_del(&rpc), 0);
}

MR_TEST(rpc, external_cancel_during_complete) {
    test_rpc_owner_t owner;
    mr_rpc_t rpc;

    mr_memset(&owner, 0, sizeof(owner));
    mr_memset(&rpc, 0, sizeof(rpc));
    test_rpc_entries_reset();
    MR_TEST_ASSERT_EQ_INT(mr_rpc_init(&rpc, MR_NULL, MR_NULL), 0);

    /* Cancellation inside complete defers release until it returns */
    mr_rpc_session_init(&owner.session, &test_rpc_external_ops, &owner);
    owner.cancel_on_complete = MR_TRUE;
    owner.session.request.type = MR_RPC_MSG_REQUEST;
    owner.session.request.service = 1U;
    owner.session.request.method = 3U;
    MR_TEST_ASSERT_EQ_INT(mr_rpc_submit(&rpc, &owner.session), 0);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_EXPECT_EQ_UINT(owner.complete_count, 1U);
    MR_TEST_EXPECT_EQ_INT(owner.cancel_result, 0);
    MR_TEST_EXPECT_EQ_UINT(owner.release_count_after_cancel, 0U);
    MR_TEST_EXPECT_EQ_UINT(owner.release_count, 1U);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_del(&rpc), 0);
    MR_TEST_EXPECT_EQ_PTR(rpc.ops, MR_NULL);
}

MR_TEST(rpc, external_empty_stream) {
    test_rpc_owner_t owner;
    mr_rpc_t rpc;

    mr_memset(&owner, 0, sizeof(owner));
    mr_memset(&rpc, 0, sizeof(rpc));
    test_rpc_entries_reset();
    MR_TEST_ASSERT_EQ_INT(mr_rpc_init(&rpc, MR_NULL, MR_NULL), 0);

    /* Consume the initial subscription result to enable the first stream */
    mr_rpc_session_init(&owner.session, &test_rpc_external_ops, &owner);
    owner.session.request.type = MR_RPC_MSG_SUBSCRIBE;
    owner.session.request.service = 1U;
    owner.session.request.method = 3U;
    owner.session.request.value.subscribe = 1U | MR_RPC_SUBSCRIBE_CONTINUOUS;
    MR_TEST_ASSERT_EQ_INT(mr_rpc_submit(&rpc, &owner.session), 0);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_ASSERT_EQ_UINT(owner.complete_count, 1U);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_session_consume(&owner.session), 0);

    /* An empty successful stream waits without producing a completion */
    test_rpc_mode = TEST_RPC_MODE_EMPTY_STREAM;
    MR_TEST_ASSERT_EQ_INT(mr_rpc_poll(&rpc), 0);
    MR_TEST_EXPECT_EQ_UINT(test_rpc_entry_calls, 1U);
    MR_TEST_EXPECT_EQ_UINT(owner.complete_count, 1U);
    MR_TEST_EXPECT_EQ_UINT(owner.release_count, 0U);
    MR_TEST_EXPECT_EQ_INT(owner.session.state, MR_RPC_SESSION_STATE_WAITING);
    MR_TEST_EXPECT_EQ_INT(owner.session.request.type, MR_RPC_MSG_STREAM);
    MR_TEST_EXPECT_EQ_INT(owner.session.response.frame.value.result, 0);
    MR_TEST_EXPECT_EQ_UINT(owner.session.response.frame.payload_size, 0U);

    /* The waiting external subscription remains cancellable and releasable */
    MR_TEST_EXPECT_EQ_INT(mr_rpc_session_cancel(&owner.session), 0);
    MR_TEST_EXPECT_EQ_UINT(owner.complete_count, 1U);
    MR_TEST_EXPECT_EQ_UINT(owner.release_count, 1U);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_del(&rpc), 0);
}

MR_TEST(rpc, external_compact_codec) {
    test_rpc_owner_t owner;
    mr_rpc_t rpc;
    mr_uint8_t *payload;
    mr_uint32_t count;

    mr_memset(&owner, 0, sizeof(owner));
    mr_memset(&rpc, 0, sizeof(rpc));
    test_rpc_entries_reset();
    MR_TEST_ASSERT_EQ_INT(mr_rpc_init(&rpc, MR_NULL, MR_NULL), 0);

    /* Decode a compact local request and encode a compact response */
    mr_rpc_session_init(&owner.session, &test_rpc_external_ops, &owner);
    owner.session.request.type = MR_RPC_MSG_REQUEST;
    owner.session.request.service = 1U;
    owner.session.request.method = 0U;
    payload = (mr_uint8_t *)&owner.session.request.payload;
    owner.session.recv.size = test_rpc_mix_payload(payload);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_submit(&rpc, &owner.session), 0);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_EXPECT_EQ_UINT(test_rpc_last_mix.title.size, 3U);
    MR_TEST_EXPECT_EQ_UINT(test_rpc_last_mix.count, 0x12345678U);
    MR_TEST_EXPECT_EQ_UINT(test_rpc_last_mix.blob.size, 3U);
    MR_TEST_EXPECT_EQ_INT(owner.result, 7);
    MR_TEST_EXPECT_EQ_UINT(owner.session.response.frame.payload_size, 14U);
    payload = (mr_uint8_t *)&owner.session.response.payload;
    MR_TEST_EXPECT_EQ_UINT(payload[0], 2U);
    MR_TEST_EXPECT_EQ_UINT(payload[1], 'o');
    MR_TEST_EXPECT_EQ_UINT(payload[2], 'k');
    mr_memcpy(&count, payload + 3U, sizeof(count));
    MR_TEST_EXPECT_EQ_UINT(count, 0x12345679U);
    MR_TEST_EXPECT_EQ_UINT(payload[7], 3U);
    MR_TEST_EXPECT_EQ_UINT(payload[8], 'a');
    MR_TEST_EXPECT_EQ_UINT(payload[11], 2U);
    MR_TEST_EXPECT_EQ_UINT(payload[13], 0x5aU);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_session_consume(&owner.session), 0);
    MR_TEST_EXPECT_EQ_UINT(owner.release_count, 1U);

    /* An invalid compact request does not reach the method */
    mr_rpc_session_init(&owner.session, &test_rpc_external_ops, &owner);
    owner.session.request.type = MR_RPC_MSG_REQUEST;
    owner.session.request.service = 1U;
    owner.session.request.method = 0U;
    owner.session.recv.size = 1U;
    MR_TEST_ASSERT_EQ_INT(mr_rpc_submit(&rpc, &owner.session), 0);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_EXPECT_EQ_UINT(test_rpc_entry_calls, 1U);
    MR_TEST_EXPECT_EQ_INT(owner.result, -MR_EINVAL);
    MR_TEST_EXPECT_EQ_UINT(owner.session.response.frame.payload_size, 0U);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_session_consume(&owner.session), 0);
    MR_TEST_EXPECT_EQ_UINT(owner.release_count, 2U);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_del(&rpc), 0);
}

MR_TEST(rpc, dynamic_session_compact_codec) {
    test_rpc_dynamic_owner_t owner = {0};
    test_rpc_io_t io;
    mr_rpc_t rpc = {0};
    mr_rpc_frame_t response;
    const mr_uint8_t *response_payload;
    mr_uint8_t payload[128], compact[14];
    mr_uint32_t size, count;
    mr_size_t used_before;

    test_rpc_io_reset(&io);
    test_rpc_entries_reset();
    used_before = mr_heap_information(MR_FALSE).used_bytes;
    MR_TEST_ASSERT_EQ_INT(mr_rpc_init(&rpc, &test_rpc_ops, &io), 0);

    /* Use the generated variable-length codec through both RPC paths. */
    size = test_rpc_mix_payload(payload);
    io.send_error = -MR_EIO;
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST,
                                            MR_RPC_FRAME_FLAG_CHECKSUM, 1303U,
                                            1U, 0U, payload, size),
                          -MR_EIO);
    MR_TEST_ASSERT_EQ_INT(
        test_rpc_dynamic_submit(&rpc, &owner, 0U, payload, size), 0);
    /* The input buffer is disposable immediately after submission. */
    mr_memset(payload, 0, sizeof(payload));
    MR_TEST_EXPECT_EQ_INT(owner.session->state, MR_RPC_SESSION_STATE_PENDING);
    MR_TEST_EXPECT_EQ_PTR(owner.session->ops, &test_rpc_dynamic_ops);
    MR_TEST_EXPECT_EQ_PTR(owner.session->owner, &owner);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_execute(&rpc), -MR_EIO);
    MR_TEST_EXPECT_EQ_UINT(owner.complete_count, 1U);
    MR_TEST_EXPECT_EQ_UINT(owner.release_count, 0U);
    MR_TEST_EXPECT_EQ_INT(owner.result, 7);
    MR_TEST_EXPECT_EQ_UINT(test_rpc_last_mix.title.size, 3U);
    MR_TEST_EXPECT_EQ_UINT(test_rpc_last_mix.count, 0x12345678U);
    MR_TEST_ASSERT_EQ_INT(owner.session->state, MR_RPC_SESSION_STATE_READY);
    MR_TEST_EXPECT_EQ_INT(owner.session->response.frame.value.result, 7);
    MR_TEST_ASSERT_EQ_UINT(owner.session->response.frame.payload_size,
                           sizeof(compact));
    mr_memcpy(compact, &owner.session->response.payload, sizeof(compact));
    mr_memcpy(&count, compact + 3U, sizeof(count));
    MR_TEST_EXPECT_EQ_UINT(count, 0x12345679U);
    MR_TEST_EXPECT_EQ_UINT(compact[0], 2U);
    MR_TEST_EXPECT_EQ_UINT(compact[13], 0x5aU);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_session_consume(owner.session), 0);
    MR_TEST_EXPECT_EQ_PTR(owner.session, MR_NULL);
    MR_TEST_EXPECT_EQ_UINT(owner.release_count, 1U);

    MR_TEST_EXPECT_EQ_INT(owner.callback_failed, MR_FALSE);

    /* Recovery produces the same compact bytes in the remote response. */
    io.send_error = 0;
    MR_TEST_ASSERT_EQ_INT(mr_rpc_flush(&rpc), 0);
    MR_TEST_ASSERT_EQ_INT(
        test_rpc_response_read(&io, &response, &response_payload), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(response.value.result, 7);
    MR_TEST_ASSERT_EQ_UINT(response.payload_size, sizeof(compact));
    MR_TEST_EXPECT_EQ_INT(mr_memcmp(response_payload, compact, sizeof(compact)),
                          0);
    MR_TEST_EXPECT_EQ_UINT(test_rpc_entry_calls, 2U);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_del(&rpc), 0);
    MR_TEST_EXPECT_EQ_UINT(mr_heap_information(MR_FALSE).used_bytes,
                           used_before);
}

MR_TEST(rpc, dynamic_session_lifecycle) {
    test_rpc_dynamic_owner_t owner = {0}, rejected = {0};
    mr_rpc_t rpc = {0};
    mr_rpc_session_t *held;
    mr_size_t used_before;
    mr_uint32_t i;

    test_rpc_entries_reset();
    used_before = mr_heap_information(MR_FALSE).used_bytes;
    MR_TEST_ASSERT_EQ_INT(mr_rpc_init(&rpc, MR_NULL, MR_NULL), 0);

    /* Cancellation uses the core API and the supplied release frees storage. */
    MR_TEST_ASSERT_EQ_INT(
        test_rpc_dynamic_submit(&rpc, &owner, 3U, MR_NULL, 0U), 0);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_session_cancel(owner.session), 0);
    MR_TEST_EXPECT_EQ_PTR(owner.session, MR_NULL);
    MR_TEST_EXPECT_EQ_UINT(owner.complete_count, 0U);
    MR_TEST_EXPECT_EQ_UINT(owner.release_count, 1U);

    /* Each completion can consume immediately; release waits for its return. */
    for (i = 0U; i < 16U; i++) {
        mr_memset(&owner, 0, sizeof(owner));
        owner.consume_on_complete = MR_TRUE;
        MR_TEST_ASSERT_EQ_INT(
            test_rpc_dynamic_submit(&rpc, &owner, 3U, MR_NULL, 0U), 0);
        MR_TEST_ASSERT_EQ_INT(mr_rpc_execute(&rpc), 0);
        MR_TEST_EXPECT_EQ_PTR(owner.session, MR_NULL);
        MR_TEST_EXPECT_EQ_UINT(owner.complete_count, 1U);
        MR_TEST_EXPECT_EQ_UINT(owner.release_count, 1U);
        MR_TEST_EXPECT_EQ_INT(owner.result, 10);
        MR_TEST_EXPECT_EQ_INT(owner.consume_result, 0);
        MR_TEST_EXPECT_EQ_INT(owner.callback_failed, MR_FALSE);
    }

#if defined(MR_USE_MODULE_RPC_HOOK)
    {
        mr_err_t execute_result = -MR_EIO;
        const mr_rpc_hook_t hook = {
            .wakeup = test_rpc_dynamic_wakeup,
            .param = &execute_result,
        };
        static const mr_rpc_hook_t idle_hook = {0};

        mr_memset(&owner, 0, sizeof(owner));
        owner.consume_on_complete = MR_TRUE;
        MR_TEST_ASSERT_EQ_INT(mr_rpc_hook_set(&rpc, &hook), 0);
        /* The session may already be freed when submit returns. */
        MR_TEST_ASSERT_EQ_INT(
            test_rpc_dynamic_submit(&rpc, &owner, 3U, MR_NULL, 0U), 0);
        MR_TEST_EXPECT_EQ_INT(execute_result, 0);
        MR_TEST_EXPECT_EQ_PTR(owner.session, MR_NULL);
        MR_TEST_EXPECT_EQ_UINT(owner.complete_count, 1U);
        MR_TEST_EXPECT_EQ_UINT(owner.release_count, 1U);
        MR_TEST_EXPECT_EQ_INT(owner.consume_result, 0);
        MR_TEST_EXPECT_EQ_INT(owner.callback_failed, MR_FALSE);
        MR_TEST_ASSERT_EQ_INT(mr_rpc_hook_set(&rpc, &idle_hook), 0);
    }
#endif /* defined(MR_USE_MODULE_RPC_HOOK) */

    /* A held session must remain allocated after cancellation. */
    mr_memset(&owner, 0, sizeof(owner));
    test_rpc_mode = TEST_RPC_MODE_HOLD;
    MR_TEST_ASSERT_EQ_INT(
        test_rpc_dynamic_submit(&rpc, &owner, 3U, MR_NULL, 0U), 0);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_ASSERT_EQ_UINT(test_rpc_held_count, 1U);
    MR_TEST_ASSERT_EQ_PTR(test_rpc_held[0], owner.session);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_session_cancel(owner.session), 0);
    MR_TEST_EXPECT_EQ_UINT(owner.release_count, 0U);
    held = test_rpc_held[0];
    test_rpc_held[0] = MR_NULL;
    MR_TEST_ASSERT_EQ_INT(mr_rpc_session_drop(held), 0);
    MR_TEST_EXPECT_EQ_PTR(owner.session, MR_NULL);
    MR_TEST_EXPECT_EQ_UINT(owner.complete_count, 0U);
    MR_TEST_EXPECT_EQ_UINT(owner.release_count, 1U);
    MR_TEST_EXPECT_EQ_INT(owner.callback_failed, MR_FALSE);

    /* Ready storage retains the dying RPC while another submit is rejected. */
    mr_memset(&owner, 0, sizeof(owner));
    test_rpc_mode = TEST_RPC_MODE_NORMAL;
    MR_TEST_ASSERT_EQ_INT(
        test_rpc_dynamic_submit(&rpc, &owner, 3U, MR_NULL, 0U), 0);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_del(&rpc), 0);
    MR_TEST_EXPECT_EQ_INT(
        test_rpc_dynamic_submit(&rpc, &rejected, 3U, MR_NULL, 0U), -MR_ENOENT);
    MR_TEST_EXPECT_EQ_PTR(rejected.session, MR_NULL);
    MR_TEST_EXPECT_EQ_UINT(rejected.complete_count, 0U);
    MR_TEST_EXPECT_EQ_UINT(rejected.release_count, 0U);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_session_consume(owner.session), 0);
    MR_TEST_EXPECT_EQ_PTR(owner.session, MR_NULL);
    MR_TEST_EXPECT_EQ_UINT(owner.release_count, 1U);
    MR_TEST_EXPECT_EQ_UINT(mr_heap_information(MR_FALSE).used_bytes,
                           used_before);
}

MR_TEST(rpc, local_dispatch_with_failed_transport) {
    test_rpc_owner_t owners[2];
    test_rpc_io_t io;
    mr_rpc_t rpc;
    mr_uint32_t calls, i;

    test_rpc_io_reset(&io);
    test_rpc_entries_reset();
    mr_memset(owners, 0, sizeof(owners));
    mr_memset(&rpc, 0, sizeof(rpc));
    MR_TEST_ASSERT_EQ_INT(mr_rpc_init(&rpc, &test_rpc_ops, &io), 0);

    /* Keep a remote response pending before submitting local requests. */
    io.send_error = -MR_EIO;
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST,
                                            MR_RPC_FRAME_FLAG_CHECKSUM, 1301U,
                                            1U, 3U, MR_NULL, 0U),
                          -MR_EIO);
    MR_TEST_ASSERT_NE_PTR(rpc.send.session, MR_NULL);
    calls = io.send_calls;
    for (i = 0U; i < MR_ARRAY_SIZE(owners); i++) {
        mr_rpc_session_init(&owners[i].session, &test_rpc_external_ops,
                            &owners[i]);
        owners[i].session.request.type = MR_RPC_MSG_REQUEST;
        owners[i].session.request.service = 1U;
        owners[i].session.request.method = 3U;
        MR_TEST_ASSERT_EQ_INT(mr_rpc_submit(&rpc, &owners[i].session), 0);
    }

    /* One failed send attempt must not block either local completion. */
    MR_TEST_EXPECT_EQ_INT(mr_rpc_execute(&rpc), -MR_EIO);
    MR_TEST_EXPECT_EQ_UINT(io.send_calls, calls + 1U);
    MR_TEST_EXPECT_EQ_UINT(io.flush_calls, 0U);
    MR_TEST_EXPECT_EQ_UINT(io.size, 0U);
    MR_TEST_EXPECT_EQ_UINT(test_rpc_entry_calls, 3U);
    for (i = 0U; i < MR_ARRAY_SIZE(owners); i++) {
        MR_TEST_EXPECT_EQ_UINT(owners[i].complete_count, 1U);
        MR_TEST_EXPECT_EQ_UINT(owners[i].release_count, 0U);
        MR_TEST_EXPECT_EQ_INT(owners[i].result, 10);
        MR_TEST_EXPECT_EQ_INT(mr_rpc_session_consume(&owners[i].session), 0);
        MR_TEST_EXPECT_EQ_UINT(owners[i].release_count, 1U);
    }

    /* Recovery sends the original response without invoking it again. */
    io.send_error = 0;
    io.limit = 3U;
    MR_TEST_EXPECT_EQ_INT(mr_rpc_flush(&rpc), 0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 1301U, 10), MR_TRUE);
    MR_TEST_EXPECT_EQ_UINT(test_rpc_entry_calls, 3U);
    MR_TEST_EXPECT_EQ_PTR(rpc.send.session, MR_NULL);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_del(&rpc), 0);
}

MR_TEST(rpc, local_dispatch_after_partial_send_failure) {
    test_rpc_owner_t owner;
    test_rpc_packet_t packet;
    mr_uint8_t payload[128];
    const mr_uint8_t *response_payload;
    mr_rpc_frame_t response;
    test_rpc_io_t io;
    mr_rpc_t rpc;
    mr_uint32_t count, size;

    test_rpc_io_reset(&io);
    test_rpc_entries_reset();
    mr_memset(&owner, 0, sizeof(owner));
    mr_memset(&rpc, 0, sizeof(rpc));
    MR_TEST_ASSERT_EQ_INT(mr_rpc_init(&rpc, &test_rpc_ops, &io), 0);

    /* Submit a local request when a remote send fails during this batch. */
    mr_rpc_session_init(&owner.session, &test_rpc_external_ops, &owner);
    owner.session.request.type = MR_RPC_MSG_REQUEST;
    owner.session.request.service = 1U;
    owner.session.request.method = 3U;
    io.submit_rpc = &rpc;
    io.submit_on_error = &owner.session;

    /* Fail inside a variable-length response, after sending its header. */
    size = test_rpc_mix_payload(payload);
    io.limit = 3U;
    io.error_after = TEST_RPC_FRAME_SIZE + 5U;
    io.send_error = -MR_EIO;
    test_rpc_packet_build(&packet, MR_RPC_MSG_REQUEST,
                          MR_RPC_FRAME_FLAG_CHECKSUM, 1302U, 1U, 0U, payload,
                          size);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_input(&rpc, packet.data, packet.size),
                          packet.size);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_execute(&rpc), -MR_EIO);
    MR_TEST_ASSERT_NE_PTR(rpc.send.session, MR_NULL);
    MR_TEST_EXPECT_EQ_UINT(rpc.send.session->send.off, io.error_after);
    MR_TEST_EXPECT_EQ_UINT(io.size, io.error_after);

    /* The newly queued local request is deferred to the next batch and still
     * runs despite the ongoing transport failure. */
    MR_TEST_EXPECT_EQ_INT(io.submit_result, 0);
    MR_TEST_EXPECT_EQ_UINT(owner.complete_count, 0U);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_execute(&rpc), -MR_EIO);
    MR_TEST_EXPECT_EQ_UINT(owner.complete_count, 1U);
    MR_TEST_EXPECT_EQ_INT(owner.result, 10);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_session_consume(&owner.session), 0);
    MR_TEST_EXPECT_EQ_UINT(owner.release_count, 1U);

    /* Continue from the saved cursor; frame parsing validates the checksum. */
    io.send_error = 0;
    MR_TEST_EXPECT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_ASSERT_EQ_INT(
        test_rpc_response_read(&io, &response, &response_payload), MR_TRUE);
    MR_TEST_EXPECT_EQ_UINT(response.session, 1302U);
    MR_TEST_EXPECT_EQ_INT(response.value.result, 7);
    MR_TEST_EXPECT_EQ_UINT(response.payload_size, 14U);
    MR_TEST_EXPECT_EQ_UINT(response_payload[0], 2U);
    mr_memcpy(&count, response_payload + 3U, sizeof(count));
    MR_TEST_EXPECT_EQ_UINT(count, 0x12345679U);
    MR_TEST_EXPECT_EQ_UINT(response_payload[12], 0xa5U);
    MR_TEST_EXPECT_EQ_UINT(response_payload[13], 0x5aU);
    MR_TEST_EXPECT_EQ_UINT(test_rpc_entry_calls, 2U);
    MR_TEST_EXPECT_EQ_PTR(rpc.send.session, MR_NULL);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_del(&rpc), 0);
}

MR_TEST(rpc, mixed_session_priority) {
    test_rpc_packet_t packet;
    test_rpc_owner_t owner;
    test_rpc_io_t io;
    mr_rpc_t rpc;

    mr_memset(&owner, 0, sizeof(owner));
    mr_memset(&rpc, 0, sizeof(rpc));
    test_rpc_io_reset(&io);
    test_rpc_entries_reset();
    MR_TEST_ASSERT_EQ_INT(mr_rpc_init(&rpc, &test_rpc_ops, &io), 0);

    /* Queue a remote frame before submitting a local session */
    test_rpc_packet_build(&packet, MR_RPC_MSG_REQUEST, 0U, 1101U, 1U, 3U,
                          MR_NULL, 0U);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_input(&rpc, packet.data, packet.size),
                          packet.size);
    mr_rpc_session_init(&owner.session, &test_rpc_external_ops, &owner);
    owner.session.request.type = MR_RPC_MSG_REQUEST;
    owner.session.request.service = 1U;
    owner.session.request.method = 3U;
    MR_TEST_ASSERT_EQ_INT(mr_rpc_submit(&rpc, &owner.session), 0);

    /* The local result arrives before the remote method runs */
    MR_TEST_ASSERT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_EXPECT_EQ_UINT(owner.complete_count, 1U);
    MR_TEST_EXPECT_EQ_UINT(owner.entry_calls_at_complete, 1U);
    MR_TEST_EXPECT_EQ_UINT(test_rpc_entry_calls, 2U);
    MR_TEST_EXPECT_EQ_INT(owner.result, 10);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 1101U, 10), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_session_consume(&owner.session), 0);
    MR_TEST_EXPECT_EQ_UINT(owner.release_count, 1U);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_del(&rpc), 0);
}

MR_TEST(rpc, external_session_delete) {
    test_rpc_owner_t ready_owner;
    test_rpc_owner_t owner;
    test_rpc_io_t io;
    mr_rpc_t rpc;

    mr_memset(&ready_owner, 0, sizeof(ready_owner));
    mr_memset(&owner, 0, sizeof(owner));
    mr_memset(&rpc, 0, sizeof(rpc));
    test_rpc_io_reset(&io);
    test_rpc_entries_reset();
    MR_TEST_ASSERT_EQ_INT(mr_rpc_init(&rpc, &test_rpc_ops, &io), 0);

    /* Deletion releases a submitted session before dispatch */
    mr_rpc_session_init(&owner.session, &test_rpc_external_ops, &owner);
    owner.session.request.type = MR_RPC_MSG_REQUEST;
    owner.session.request.service = 1U;
    owner.session.request.method = 3U;
    MR_TEST_ASSERT_EQ_INT(mr_rpc_submit(&rpc, &owner.session), 0);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_del(&rpc), 0);
    MR_TEST_EXPECT_EQ_UINT(owner.complete_count, 0U);
    MR_TEST_EXPECT_EQ_UINT(owner.release_count, 1U);
    MR_TEST_EXPECT_EQ_PTR(rpc.ops, MR_NULL);

    /* A delivered result retains RPC until the owner cancels it */
    mr_memset(&rpc, 0, sizeof(rpc));
    MR_TEST_ASSERT_EQ_INT(mr_rpc_init(&rpc, &test_rpc_ops, &io), 0);
    mr_rpc_session_init(&ready_owner.session, &test_rpc_external_ops,
                        &ready_owner);
    ready_owner.session.request.type = MR_RPC_MSG_REQUEST;
    ready_owner.session.request.service = 1U;
    ready_owner.session.request.method = 3U;
    MR_TEST_ASSERT_EQ_INT(mr_rpc_submit(&rpc, &ready_owner.session), 0);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_EXPECT_EQ_UINT(ready_owner.complete_count, 1U);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_del(&rpc), 0);
    MR_TEST_EXPECT_EQ_PTR(rpc.ops, &test_rpc_ops);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_session_cancel(&ready_owner.session), 0);
    MR_TEST_EXPECT_EQ_UINT(ready_owner.release_count, 1U);
    MR_TEST_EXPECT_EQ_PTR(rpc.ops, MR_NULL);

    /* A held session retains the RPC until it is dropped */
    mr_memset(&rpc, 0, sizeof(rpc));
    MR_TEST_ASSERT_EQ_INT(mr_rpc_init(&rpc, &test_rpc_ops, &io), 0);
    test_rpc_mode = TEST_RPC_MODE_HOLD;
    mr_rpc_session_init(&owner.session, &test_rpc_external_ops, &owner);
    owner.session.request.type = MR_RPC_MSG_REQUEST;
    owner.session.request.service = 1U;
    owner.session.request.method = 3U;
    MR_TEST_ASSERT_EQ_INT(mr_rpc_submit(&rpc, &owner.session), 0);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_ASSERT_EQ_UINT(test_rpc_held_count, 1U);
    MR_TEST_EXPECT_EQ_PTR(test_rpc_held[0], &owner.session);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_del(&rpc), 0);
    MR_TEST_EXPECT_EQ_PTR(rpc.ops, &test_rpc_ops);
    MR_TEST_EXPECT_EQ_UINT(owner.release_count, 1U);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_session_drop(&owner.session), 0);
    MR_TEST_EXPECT_EQ_UINT(owner.complete_count, 0U);
    MR_TEST_EXPECT_EQ_UINT(owner.release_count, 2U);
    MR_TEST_EXPECT_EQ_PTR(rpc.ops, MR_NULL);
}

MR_TEST(rpc, frame_header_rejections_and_resync) {
    const mr_uint8_t invalid_types[] = {
        MR_RPC_MSG_RESPONSE,
        MR_RPC_MSG_STREAM,
        0xffU,
    };
    test_rpc_packet_t packet;
    mr_rpc_frame_t *frame;
    test_rpc_io_t io;
    mr_rpc_t rpc;
    mr_uint32_t i;

    test_rpc_io_reset(&io);
    test_rpc_entries_reset();
    mr_memset(&rpc, 0, sizeof(rpc));
    MR_TEST_ASSERT_EQ_INT(mr_rpc_init(&rpc, &test_rpc_ops, &io), 0);

    /* A valid checksum cannot make an unsupported version acceptable */
    test_rpc_packet_build(&packet, MR_RPC_MSG_REQUEST, 0U, 1500U, 1U, 3U,
                          MR_NULL, 0U);
    frame = (mr_rpc_frame_t *)packet.data;
    frame->version = 2U;
    frame->hdr_checksum = 0U;
    frame->hdr_checksum = test_rpc_fletcher16(frame, sizeof(*frame));
    MR_TEST_ASSERT_EQ_INT(mr_rpc_input(&rpc, packet.data, packet.size),
                          packet.size);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_EXPECT_EQ_UINT(io.size, 0U);
    MR_TEST_EXPECT_EQ_UINT(test_rpc_entry_calls, 0U);

    /* Incoming responses and streams cannot enter server dispatch */
    for (i = 0U; i < MR_ARRAY_SIZE(invalid_types); i++) {
        test_rpc_packet_build(&packet, (mr_rpc_msg_type_t)invalid_types[i], 0U,
                              1501U + i, 1U, 3U, MR_NULL, 0U);
        MR_TEST_ASSERT_EQ_INT(mr_rpc_input(&rpc, packet.data, packet.size),
                              packet.size);
        MR_TEST_EXPECT_EQ_INT(mr_rpc_execute(&rpc), 0);
        MR_TEST_EXPECT_EQ_UINT(io.size, 0U);
        MR_TEST_EXPECT_EQ_UINT(test_rpc_entry_calls, 0U);
    }

    /* The parser still finds the next valid request */
    MR_TEST_ASSERT_EQ_INT(test_rpc_exchange(&rpc, &io, MR_RPC_MSG_REQUEST, 0U,
                                            1510U, 1U, 3U, MR_NULL, 0U),
                          0);
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 1510U, 10), MR_TRUE);
    MR_TEST_EXPECT_EQ_UINT(test_rpc_entry_calls, 1U);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_del(&rpc), 0);
}

MR_TEST(rpc, framed_request_byte_by_byte) {
    mr_uint8_t payload[128];
    test_rpc_packet_t packet;
    test_rpc_io_t io;
    mr_rpc_t rpc;
    mr_uint32_t size, i;

    test_rpc_io_reset(&io);
    test_rpc_entries_reset();
    mr_memset(&rpc, 0, sizeof(rpc));
    MR_TEST_ASSERT_EQ_INT(mr_rpc_init(&rpc, &test_rpc_ops, &io), 0);

    /* Header, compact payload and checksum may arrive separately */
    size = test_rpc_mix_payload(payload);
    test_rpc_packet_build(&packet, MR_RPC_MSG_REQUEST,
                          MR_RPC_FRAME_FLAG_CHECKSUM, 1520U, 1U, 0U, payload,
                          size);
    for (i = 0U; i < packet.size; i++) {
        MR_TEST_ASSERT_EQ_INT(mr_rpc_input(&rpc, &packet.data[i], 1U), 1);
        MR_TEST_ASSERT_EQ_INT(mr_rpc_execute(&rpc), 0);
        if ((i + 1U) < packet.size) {
            MR_TEST_EXPECT_EQ_UINT(io.size, 0U);
        }
    }
    MR_TEST_EXPECT_EQ_INT(test_rpc_result_is(&io, 1520U, 7), MR_TRUE);
    MR_TEST_EXPECT_EQ_UINT(test_rpc_last_mix.count, 0x12345678U);
    MR_TEST_EXPECT_EQ_UINT(test_rpc_entry_calls, 1U);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_del(&rpc), 0);
}

MR_TEST(rpc, external_submit_validation) {
    test_rpc_owner_t owner;
    mr_rpc_t rpc;

    mr_memset(&owner, 0, sizeof(owner));
    mr_memset(&rpc, 0, sizeof(rpc));
    test_rpc_entries_reset();
    MR_TEST_ASSERT_EQ_INT(mr_rpc_init(&rpc, MR_NULL, MR_NULL), 0);
    mr_rpc_session_init(&owner.session, &test_rpc_external_ops, &owner);
    owner.session.request.service = 1U;
    owner.session.request.method = 3U;

    /* Rejected submissions retain ownership with the caller */
    owner.session.request.type = MR_RPC_MSG_RESPONSE;
    MR_TEST_EXPECT_EQ_INT(mr_rpc_submit(&rpc, &owner.session), -MR_EINVAL);
    MR_TEST_EXPECT_EQ_PTR(owner.session.rpc, MR_NULL);
    owner.session.request.type = MR_RPC_MSG_REQUEST;
    owner.session.recv.size = sizeof(owner.session.request.payload) + 1U;
    MR_TEST_EXPECT_EQ_INT(mr_rpc_submit(&rpc, &owner.session), -MR_EINVAL);
    MR_TEST_EXPECT_EQ_PTR(owner.session.rpc, MR_NULL);

    /* A valid request can be submitted only once until released */
    owner.session.recv.size = 0U;
    MR_TEST_ASSERT_EQ_INT(mr_rpc_submit(&rpc, &owner.session), 0);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_submit(&rpc, &owner.session), -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_execute(&rpc), 0);
    MR_TEST_EXPECT_EQ_UINT(owner.complete_count, 1U);
    MR_TEST_EXPECT_EQ_INT(owner.result, 10);
    MR_TEST_EXPECT_EQ_UINT(owner.release_count, 0U);
    MR_TEST_ASSERT_EQ_INT(mr_rpc_session_consume(&owner.session), 0);
    MR_TEST_EXPECT_EQ_UINT(owner.release_count, 1U);
    MR_TEST_EXPECT_EQ_INT(mr_rpc_del(&rpc), 0);
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_MODULE_RPC) */
