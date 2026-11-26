/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-19        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#if defined(MR_USE_MODULE_XVM_GDB)
#if defined(MR_USE_MODULE_XVM_RISCV32)
#include <mrX/libc/mr_sprintf.h>
#include <mrX/libc/mr_string.h>
#include <mrX/modules/xvm/mr_xvm_gdb.h>

#define TEST_GDB_DATA_SIZE (2048U)
#define TEST_GDB_ENTRY     (0x100U)
#define TEST_GDB_PC_NR     (32U)
#define TEST_GDB_ADDI_X1   (0x00100093U)
#define TEST_GDB_ADDI_X1_1 (0x00108093U)
#define TEST_GDB_ECALL     (0x00000073U)

static const char test_gdb_hex[] = "0123456789abcdef";

typedef struct test_gdb_io {
    char buf[MR_CFG_XVM_GDB_REPLY_SIZE];
    mr_size_t len;
    mr_size_t limit;
    mr_bool_t stall;
    mr_bool_t closed;
} test_gdb_io_t;

static void test_gdb_put_word(mr_uint8_t *image, mr_uint32_t index,
                              mr_uint32_t value) {
    mr_memcpy(image + (index * sizeof(value)), &value, sizeof(value));
}

static mr_err_t test_gdb_prepare_xvm(mr_xvm_t *xvm, mr_uint8_t *data,
                                     mr_uint8_t *image, mr_size_t size) {
    mr_err_t ret;

    ret = mr_xvm_init(xvm, "riscv32", data, TEST_GDB_DATA_SIZE);
    if (ret != 0) {
        return ret;
    }
    ret = mr_xvm_load(xvm, image, size, TEST_GDB_ENTRY);
    if (ret != 0) {
        mr_xvm_del(xvm);
        return ret;
    }
    return mr_xvm_reg_store(xvm, TEST_GDB_PC_NR, 0);
}

static void test_gdb_io_reset(test_gdb_io_t *io) {
    io->len = 0;
    io->limit = 0;
    io->stall = MR_FALSE;
    io->closed = MR_FALSE;
}

static mr_ssize_t test_gdb_send(void *priv, const void *buf, mr_size_t len) {
    test_gdb_io_t *io;
    mr_size_t room;

    io = (test_gdb_io_t *)priv;
    if (io->stall) {
        return 0;
    }
    if (io->limit != 0) {
        if (len > io->limit) {
            len = io->limit;
        }
        io->limit = 0;
        io->stall = MR_TRUE;
    }
    room = sizeof(io->buf) - io->len;
    if (len > room) {
        return -1;
    }
    mr_memcpy(io->buf + io->len, buf, len);
    io->len += len;
    return (mr_ssize_t)len;
}

static void test_gdb_closed(void *priv) {
    ((test_gdb_io_t *)priv)->closed = MR_TRUE;
}

static const mr_xvm_gdb_ops_t test_gdb_ops = {
    .send = test_gdb_send,
    .flush = MR_NULL,
    .closed = test_gdb_closed,
};

static mr_size_t test_gdb_enc_packet(char *out, mr_size_t cap,
                                     const char *payload) {
    mr_uint8_t sum = 0;
    mr_size_t n;
    mr_size_t i;

    n = mr_strlen(payload);
    if ((n + 4U) > cap) {
        return 0;
    }
    out[0] = '$';
    for (i = 0; i < n; i++) {
        out[1U + i] = payload[i];
        sum = (mr_uint8_t)(sum + (mr_uint8_t)payload[i]);
    }
    out[1U + n] = '#';
    out[2U + n] = test_gdb_hex[sum >> 4];
    out[3U + n] = test_gdb_hex[sum & 0x0fU];
    return n + 4U;
}

static mr_ssize_t test_gdb_xfer(mr_xvm_gdb_t *gdb, test_gdb_io_t *io,
                                const char *payload) {
    char packet[1024];
    mr_size_t n;

    io->len = 0;
    n = test_gdb_enc_packet(packet, sizeof(packet), payload);
    if (n == 0) {
        return -1;
    }
    return mr_xvm_gdb_recv(gdb, packet, n);
}

static mr_bool_t test_gdb_has_packet(const test_gdb_io_t *io,
                                     const char *payload) {
    char packet[256];
    mr_size_t n;
    mr_size_t i;

    n = test_gdb_enc_packet(packet, sizeof(packet), payload);
    if ((n == 0) || (io->len < n)) {
        return MR_FALSE;
    }
    for (i = 0; (i + n) <= io->len; i++) {
        if (mr_memcmp(io->buf + i, packet, n) == 0) {
            return MR_TRUE;
        }
    }
    return MR_FALSE;
}

static mr_bool_t test_gdb_last_payload(const test_gdb_io_t *io, char *out,
                                       mr_size_t cap) {
    mr_size_t i;
    mr_size_t start;
    mr_size_t n;

    if (io->len == 0) {
        return MR_FALSE;
    }
    i = io->len;
    do {
        i--;
        if (io->buf[i] == '$') {
            break;
        }
    } while (i != 0);
    if (io->buf[i] != '$') {
        return MR_FALSE;
    }
    start = i + 1U;
    for (i = start; i < io->len; i++) {
        if (io->buf[i] == '#') {
            n = i - start;
            if ((n + 1U) > cap) {
                return MR_FALSE;
            }
            mr_memcpy(out, io->buf + start, n);
            out[n] = '\0';
            return MR_TRUE;
        }
    }
    return MR_FALSE;
}

static mr_bool_t test_gdb_contains(const char *text, const char *token) {
    mr_size_t text_n;
    mr_size_t token_n;
    mr_size_t i;

    text_n = mr_strlen(text);
    token_n = mr_strlen(token);
    if ((token_n == 0) || (text_n < token_n)) {
        return MR_FALSE;
    }
    for (i = 0; (i + token_n) <= text_n; i++) {
        if (mr_memcmp(text + i, token, token_n) == 0) {
            return MR_TRUE;
        }
    }
    return MR_FALSE;
}

static mr_err_t test_gdb_open(mr_xvm_gdb_t *gdb, test_gdb_io_t *io,
                              mr_xvm_t *xvm, mr_uint8_t *data,
                              mr_uint8_t *image, mr_size_t image_size) {
    mr_err_t ret;

    test_gdb_io_reset(io);
    ret = test_gdb_prepare_xvm(xvm, data, image, image_size);
    if (ret != 0) {
        return ret;
    }
    ret = mr_xvm_gdb_init(gdb, &test_gdb_ops, io);
    if (ret != 0) {
        mr_xvm_del(xvm);
        return ret;
    }
    ret = mr_xvm_gdb_attach(gdb, xvm);
    if (ret != 0) {
        mr_xvm_gdb_del(gdb);
        mr_xvm_del(xvm);
        return ret;
    }
    return 0;
}

static void test_gdb_close(mr_xvm_gdb_t *gdb, mr_xvm_t *xvm) {
    mr_xvm_gdb_del(gdb);
    mr_xvm_del(xvm);
}

MR_TEST(xvm_gdb, lifecycle) {
    mr_uint8_t data[TEST_GDB_DATA_SIZE];
    mr_uint8_t image[4] = {0};
    test_gdb_io_t io;
    mr_xvm_gdb_t gdb;
    mr_xvm_gdb_t *created;
    mr_xvm_t xvm;
    mr_err_t ret;

    test_gdb_put_word(image, 0, TEST_GDB_ECALL);
    test_gdb_io_reset(&io);
    ret = mr_xvm_gdb_init(&gdb, &test_gdb_ops, &io);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_state(&gdb), MR_XVM_GDB_DETACHED);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_want_send(&gdb), MR_FALSE);

    ret = test_gdb_prepare_xvm(&xvm, data, image, sizeof(image));
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_xvm_gdb_attach(&gdb, &xvm);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_state(&gdb), MR_XVM_GDB_ATTACHED);
    ret = mr_xvm_gdb_attach(&gdb, &xvm);
    MR_TEST_ASSERT_EQ_INT(ret, -MR_EBUSY);

    mr_xvm_gdb_detach(&gdb);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_state(&gdb), MR_XVM_GDB_DETACHED);
    ret = mr_xvm_gdb_attach(&gdb, &xvm);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_xvm_gdb_del(&gdb);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_del(&xvm), 0);

    created = mr_xvm_gdb_create(&test_gdb_ops, &io);
    MR_TEST_ASSERT_NE_PTR(created, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_del(created), 0);
}

MR_TEST(xvm_gdb, unattached_and_checksum) {
    test_gdb_io_t io;
    mr_xvm_gdb_t gdb;
    char bad[8];
    mr_err_t ret;

    test_gdb_io_reset(&io);
    ret = mr_xvm_gdb_init(&gdb, &test_gdb_ops, &io);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "?"), 0);
    MR_TEST_ASSERT_EQ_INT(io.buf[0], '+');
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "E01"), MR_TRUE);

    bad[0] = '$';
    bad[1] = '?';
    bad[2] = '#';
    bad[3] = '0';
    bad[4] = '0';
    io.len = 0;
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_recv(&gdb, bad, 5), 5);
    MR_TEST_ASSERT_EQ_INT(io.len, 1);
    MR_TEST_ASSERT_EQ_INT(io.buf[0], '-');
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_del(&gdb), 0);
}

MR_TEST(xvm_gdb, queries) {
    mr_uint8_t data[TEST_GDB_DATA_SIZE];
    mr_uint8_t image[4] = {0};
    char payload[4096];
    char doc[4096];
    char range[64];
    char size_token[32];
    mr_size_t packet_size;
    mr_size_t doc_len;
    mr_size_t offset;
    mr_size_t n;
    test_gdb_io_t io;
    mr_xvm_gdb_t gdb;
    mr_xvm_t xvm;
    mr_err_t ret;

    test_gdb_put_word(image, 0, TEST_GDB_ECALL);
    ret = test_gdb_open(&gdb, &io, &xvm, data, image, sizeof(image));
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "qSupported"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_last_payload(&io, payload, sizeof(payload)),
                          MR_TRUE);
    packet_size = (mr_size_t)MR_CFG_XVM_GDB_PACKET_SIZE;
    if (packet_size > ((mr_size_t)MR_CFG_XVM_GDB_REPLY_SIZE - 4U)) {
        packet_size = (mr_size_t)MR_CFG_XVM_GDB_REPLY_SIZE - 4U;
    }
    MR_TEST_ASSERT_GT_INT(mr_snprintf(size_token, sizeof(size_token),
                                      "PacketSize=%zx", packet_size),
                          0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_contains(payload, size_token), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(test_gdb_contains(payload, "qXfer:features:read+"),
                          MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(test_gdb_contains(payload, "QStartNoAckMode+"),
                          MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(test_gdb_contains(payload, "vContSupported+"),
                          MR_TRUE);

    /* Read the whole target document slice by slice */
    offset = 0;
    doc_len = 0;
    while (1) {
        ret = mr_snprintf(range, sizeof(range),
                          "qXfer:features:read:target.xml:%zx,80", offset);
        MR_TEST_ASSERT_GT_INT(ret, 0);
        MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, range), 0);
        MR_TEST_ASSERT_EQ_INT(
            test_gdb_last_payload(&io, payload, sizeof(payload)), MR_TRUE);
        n = mr_strlen(payload + 1);
        if ((doc_len + n) >= sizeof(doc)) {
            MR_TEST_EXPECT_LT_UINT(doc_len + n, sizeof(doc));
            break;
        }
        mr_memcpy(doc + doc_len, payload + 1, n);
        doc_len += n;
        offset += n;
        if (payload[0] == 'l') {
            break;
        }
        MR_TEST_ASSERT_EQ_INT(payload[0], 'm');
    }
    doc[doc_len] = '\0';
    MR_TEST_ASSERT_GT_UINT(doc_len, 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_contains(doc, "riscv:rv32"), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(test_gdb_contains(doc, "org.gnu.gdb.riscv.cpu"),
                          MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(test_gdb_contains(doc, "name=\"pc\""), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(test_gdb_contains(doc, "regnum=\"32\""), MR_TRUE);

    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "?"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "S05"), MR_TRUE);
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "qC"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "QC1"), MR_TRUE);
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "qfThreadInfo"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "m1"), MR_TRUE);
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "qsThreadInfo"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "l"), MR_TRUE);
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "qAttached"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "1"), MR_TRUE);
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "Hc-1"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "OK"), MR_TRUE);
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "T1"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "OK"), MR_TRUE);
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "T2"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, ""), MR_TRUE);
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "vCont?"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "vCont;c;s"), MR_TRUE);
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "qSymbol::"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "OK"), MR_TRUE);
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "Z2,0,4"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, ""), MR_TRUE);
    test_gdb_close(&gdb, &xvm);
}

MR_TEST(xvm_gdb, registers) {
    mr_uint8_t data[TEST_GDB_DATA_SIZE];
    mr_uint8_t image[4] = {0};
    char payload[512];
    test_gdb_io_t io;
    mr_xvm_gdb_t gdb;
    mr_xvm_t xvm;
    mr_uint64_t value;
    mr_err_t ret;

    test_gdb_put_word(image, 0, TEST_GDB_ECALL);
    ret = test_gdb_open(&gdb, &io, &xvm, data, image, sizeof(image));
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_store(&xvm, 1, 0x12345678U), 0);
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "p1"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "78563412"), MR_TRUE);

    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "P1=efcdab90"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "OK"), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 1, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 0x90abcdefU);

    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "g"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_last_payload(&io, payload, sizeof(payload)),
                          MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_strlen(payload), 33U * 8U);
    MR_TEST_ASSERT_EQ_INT(mr_strncmp(payload, "00000000", 8), 0);
    MR_TEST_ASSERT_EQ_INT(mr_strncmp(payload + 8, "efcdab90", 8), 0);

    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "P20=04000000"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "OK"), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, TEST_GDB_PC_NR, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 4);
    test_gdb_close(&gdb, &xvm);
}

MR_TEST(xvm_gdb, memory) {
    mr_uint8_t data[TEST_GDB_DATA_SIZE];
    mr_uint8_t image[8] = {0};
    test_gdb_io_t io;
    mr_xvm_gdb_t gdb;
    mr_xvm_t xvm;
    mr_uint8_t *mem;
    mr_bool_t writable;
    mr_err_t ret;

    test_gdb_put_word(image, 0, TEST_GDB_ADDI_X1);
    test_gdb_put_word(image, 1, TEST_GDB_ECALL);
    ret = test_gdb_open(&gdb, &io, &xvm, data, image, sizeof(image));
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "m0,4"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "93001000"), MR_TRUE);
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "M0,2:ffff"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "E01"), MR_TRUE);

    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "M100,2:5a5a"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "OK"), MR_TRUE);
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "m100,2"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "5a5a"), MR_TRUE);
    MR_TEST_ASSERT_GT_INT(
        mr_xvm_datamem_map(&xvm, TEST_GDB_ENTRY, (void **)&mem, &writable), 0);
    MR_TEST_ASSERT_EQ_INT(writable, MR_TRUE);
    MR_TEST_ASSERT_EQ_UINT(mem[0], 0x5aU);
    MR_TEST_ASSERT_EQ_UINT(mem[1], 0x5aU);
    test_gdb_close(&gdb, &xvm);
}

MR_TEST(xvm_gdb, breakpoint_continue) {
    mr_uint8_t data[TEST_GDB_DATA_SIZE];
    mr_uint8_t image[12] = {0};
    mr_xvm_event_t event = {0};
    test_gdb_io_t io;
    mr_xvm_gdb_t gdb;
    mr_xvm_t xvm;
    mr_uint64_t value;
    mr_err_t ret;

    test_gdb_put_word(image, 0, TEST_GDB_ADDI_X1);
    test_gdb_put_word(image, 1, TEST_GDB_ADDI_X1_1);
    test_gdb_put_word(image, 2, TEST_GDB_ECALL);
    ret = test_gdb_open(&gdb, &io, &xvm, data, image, sizeof(image));
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 4), MR_XVM_NONE);
    MR_TEST_ASSERT_EQ_UINT(event.none.step, 0);

    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "Z0,4,4"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "OK"), MR_TRUE);
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "c"), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_state(&gdb), MR_XVM_GDB_RUNNING);
    MR_TEST_ASSERT_EQ_INT(io.len, 1);
    MR_TEST_ASSERT_EQ_INT(io.buf[0], '+');

    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 8), MR_XVM_NONE);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_state(&gdb), MR_XVM_GDB_ATTACHED);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "S05"), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 1, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 1);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, TEST_GDB_PC_NR, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 4);

    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "z0,4,4"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "OK"), MR_TRUE);
    test_gdb_close(&gdb, &xvm);
}

MR_TEST(xvm_gdb, step_and_interrupt) {
    mr_uint8_t data[TEST_GDB_DATA_SIZE];
    mr_uint8_t image[12] = {0};
    mr_xvm_event_t event = {0};
    test_gdb_io_t io;
    mr_xvm_gdb_t gdb;
    mr_xvm_t xvm;
    mr_uint64_t value;
    char brk;
    mr_err_t ret;

    test_gdb_put_word(image, 0, TEST_GDB_ADDI_X1);
    test_gdb_put_word(image, 1, TEST_GDB_ADDI_X1_1);
    test_gdb_put_word(image, 2, TEST_GDB_ECALL);
    ret = test_gdb_open(&gdb, &io, &xvm, data, image, sizeof(image));
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "vCont;s"), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_state(&gdb), MR_XVM_GDB_RUNNING);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 8), MR_XVM_NONE);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_state(&gdb), MR_XVM_GDB_ATTACHED);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "S05"), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 1, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 1);

    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "vCont;c"), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_state(&gdb), MR_XVM_GDB_RUNNING);
    brk = 0x03;
    io.len = 0;
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_recv(&gdb, &brk, 1), 1);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_state(&gdb), MR_XVM_GDB_ATTACHED);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "S02"), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 4), MR_XVM_NONE);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 1, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 1);
    test_gdb_close(&gdb, &xvm);
}

MR_TEST(xvm_gdb, detach_kill_and_noack) {
    mr_uint8_t data[TEST_GDB_DATA_SIZE];
    mr_uint8_t image[4] = {0};
    test_gdb_io_t io;
    mr_xvm_gdb_t gdb;
    mr_xvm_t xvm;
    mr_err_t ret;

    test_gdb_put_word(image, 0, TEST_GDB_ECALL);
    ret = test_gdb_open(&gdb, &io, &xvm, data, image, sizeof(image));
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "QStartNoAckMode"), 0);
    MR_TEST_ASSERT_EQ_INT(io.buf[0], '+');
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "OK"), MR_TRUE);
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "qC"), 0);
    MR_TEST_ASSERT_NE_INT(io.buf[0], '+');
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "QC1"), MR_TRUE);

    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "D"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "OK"), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_state(&gdb), MR_XVM_GDB_CLOSED);
    MR_TEST_ASSERT_EQ_INT(io.closed, MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_recv(&gdb, "k", 1), 0);
    test_gdb_close(&gdb, &xvm);

    ret = test_gdb_open(&gdb, &io, &xvm, data, image, sizeof(image));
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    io.closed = MR_FALSE;
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "k"), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_state(&gdb), MR_XVM_GDB_CLOSED);
    MR_TEST_ASSERT_EQ_INT(io.closed, MR_TRUE);
    test_gdb_close(&gdb, &xvm);
}

MR_TEST(xvm_gdb, short_send) {
    mr_uint8_t data[TEST_GDB_DATA_SIZE];
    mr_uint8_t image[4] = {0};
    test_gdb_io_t io;
    mr_xvm_gdb_t gdb;
    mr_xvm_t xvm;
    mr_err_t ret;

    test_gdb_put_word(image, 0, TEST_GDB_ECALL);
    ret = test_gdb_open(&gdb, &io, &xvm, data, image, sizeof(image));
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    io.limit = 1;
    io.stall = MR_FALSE;
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "?"), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_want_send(&gdb), MR_TRUE);
    io.stall = MR_FALSE;
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_flush(&gdb), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_want_send(&gdb), MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "S05"), MR_TRUE);
    test_gdb_close(&gdb, &xvm);
}

MR_TEST(xvm_gdb, remaining_commands) {
    mr_uint8_t data[TEST_GDB_DATA_SIZE];
    mr_uint8_t image[12] = {0};
    mr_xvm_event_t event = {0};
    char payload[512];
    test_gdb_io_t io;
    mr_xvm_gdb_t gdb;
    mr_xvm_t xvm;
    mr_uint64_t value;
    mr_err_t ret;

    test_gdb_put_word(image, 0, TEST_GDB_ADDI_X1);
    test_gdb_put_word(image, 1, TEST_GDB_ADDI_X1_1);
    test_gdb_put_word(image, 2, TEST_GDB_ECALL);
    ret = test_gdb_open(&gdb, &io, &xvm, data, image, sizeof(image));
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    /* G writes every target register */
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_store(&xvm, 1, 0x11U), 0);
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "g"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_last_payload(&io, payload, sizeof(payload)),
                          MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_strlen(payload), 33U * 8U);
#if (MR_CFG_XVM_GDB_PACKET_SIZE > (1U + (33U * 8U)))
    {
        char gcmd[280];

        gcmd[0] = 'G';
        mr_memcpy(gcmd + 1, payload, 33U * 8U);
        mr_memcpy(gcmd + 1U + 8U, "44332211", 8);
        gcmd[1U + (33U * 8U)] = '\0';
        MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, gcmd), 0);
        MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "OK"), MR_TRUE);
        MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 1, &value), 0);
        MR_TEST_ASSERT_EQ_UINT(value, 0x11223344U);
    }
#else
    /* RV32 G is 265 bytes; a smaller packet buffer drops it, which is
     * expected for this configuration */
#endif /* MR_CFG_XVM_GDB_PACKET_SIZE > (1U + (33U * 8U)) */
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "G00"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "E01"), MR_TRUE);

    /* Hardware breakpoint shares the software table */
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "Z1,4,4"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "OK"), MR_TRUE);
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "z1,4,4"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "OK"), MR_TRUE);
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "z0,8,4"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "OK"), MR_TRUE);
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "Z0"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, ""), MR_TRUE);

    /* Legacy step and addressed continue/step */
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "s"), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_state(&gdb), MR_XVM_GDB_RUNNING);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 8), MR_XVM_NONE);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_state(&gdb), MR_XVM_GDB_ATTACHED);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "S05"), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 1, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 1);

    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "s4"), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 8), MR_XVM_NONE);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "S05"), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, 1, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 2);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_reg_load(&xvm, TEST_GDB_PC_NR, &value), 0);
    MR_TEST_ASSERT_EQ_UINT(value, 8);

    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "czz"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "E01"), MR_TRUE);
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "c0"), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_state(&gdb), MR_XVM_GDB_RUNNING);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_execute(&xvm, &event, 8), MR_XVM_EXIT);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_status(&xvm), MR_XVM_STATUS_ENDED);
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "?"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "W00"), MR_TRUE);
    test_gdb_close(&gdb, &xvm);
}

MR_TEST(xvm_gdb, query_edges_and_errors) {
    mr_uint8_t data[TEST_GDB_DATA_SIZE];
    mr_uint8_t image[4] = {0};
    char payload[128];
    char packet[8];
    test_gdb_io_t io;
    mr_xvm_gdb_t gdb;
    mr_xvm_t xvm;
    char ack;
    char brk;
    mr_err_t ret;

    test_gdb_put_word(image, 0, TEST_GDB_ECALL);
    ret = test_gdb_open(&gdb, &io, &xvm, data, image, sizeof(image));
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    /* qXfer slices, past-end last slice and a bad offset */
    MR_TEST_ASSERT_GT_INT(
        test_gdb_xfer(&gdb, &io, "qXfer:features:read:target.xml:0,20"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_last_payload(&io, payload, sizeof(payload)),
                          MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(payload[0], 'm');
    MR_TEST_ASSERT_GT_INT(
        test_gdb_xfer(&gdb, &io, "qXfer:features:read:target.xml:20,20"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_last_payload(&io, payload, sizeof(payload)),
                          MR_TRUE);
    MR_TEST_ASSERT_EQ_INT((payload[0] == 'm') || (payload[0] == 'l'), MR_TRUE);
    MR_TEST_ASSERT_GT_INT(
        test_gdb_xfer(&gdb, &io, "qXfer:features:read:target.xml:ffff,10"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "l"), MR_TRUE);
    MR_TEST_ASSERT_GT_INT(
        test_gdb_xfer(&gdb, &io, "qXfer:features:read:target.xml:zz,1"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "E01"), MR_TRUE);

    /* Unsupported query/set/v/other packets stay empty */
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "qFoo"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, ""), MR_TRUE);
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "QFoo"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, ""), MR_TRUE);
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "vKill"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, ""), MR_TRUE);
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "X"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, ""), MR_TRUE);

    /* Bad register and memory packets */
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "p"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "E01"), MR_TRUE);
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "p99"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "E01"), MR_TRUE);
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "P1=zz"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "E01"), MR_TRUE);
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "m"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "E01"), MR_TRUE);
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "m2000,4"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "E01"), MR_TRUE);
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "M100,2:ff"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "E01"), MR_TRUE);
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "M100,2:xxxx"), 0);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "E01"), MR_TRUE);

    /* ACK bytes are ignored; packets can arrive in pieces */
    ack = '+';
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_recv(&gdb, &ack, 1), 1);
    ack = '-';
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_recv(&gdb, &ack, 1), 1);
    io.len = 0;
    packet[0] = '$';
    packet[1] = 'q';
    packet[2] = 'C';
    packet[3] = '#';
    packet[4] = test_gdb_hex[(('q' + 'C') >> 4) & 0x0fU];
    packet[5] = test_gdb_hex[('q' + 'C') & 0x0fU];
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_recv(&gdb, packet, 2), 2);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_recv(&gdb, packet + 2, 2), 2);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_recv(&gdb, packet + 4, 2), 2);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "QC1"), MR_TRUE);
    test_gdb_close(&gdb, &xvm);

    /* Interrupt without an attach does not reply */
    test_gdb_io_reset(&io);
    ret = mr_xvm_gdb_init(&gdb, &test_gdb_ops, &io);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    brk = 0x03;
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_recv(&gdb, &brk, 1), 1);
    MR_TEST_ASSERT_EQ_INT(io.len, 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_del(&gdb), 0);
}

MR_TEST(xvm_gdb, reply_compaction_on_partial_send) {
    mr_uint8_t data[TEST_GDB_DATA_SIZE];
    mr_uint8_t image[8] = {0};
    test_gdb_io_t io;
    mr_xvm_gdb_t gdb;
    mr_xvm_t xvm;

    test_gdb_put_word(image, 0, TEST_GDB_ADDI_X1);
    test_gdb_put_word(image, 1, TEST_GDB_ECALL);
    MR_TEST_ASSERT_EQ_INT(
        test_gdb_open(&gdb, &io, &xvm, data, image, sizeof(image)), 0);

    /* Partial send stalls mid-reply; remaining bytes must compact to offset 0 */
    io.len = 0;
    io.limit = 3;
    MR_TEST_ASSERT_GT_INT(test_gdb_xfer(&gdb, &io, "qC"), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_want_send(&gdb), MR_TRUE);
    MR_TEST_ASSERT_EQ_UINT(gdb.reply.off, 0U);
    MR_TEST_ASSERT_GT_UINT(gdb.reply.len, 0U);

    /* Resume transport and verify full packet arrives intact */
    io.stall = MR_FALSE;
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_flush(&gdb), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xvm_gdb_want_send(&gdb), MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(test_gdb_has_packet(&io, "QC1"), MR_TRUE);
    test_gdb_close(&gdb, &xvm);
}

#endif /* defined(MR_USE_MODULE_XVM_RISCV32) */
#endif /* defined(MR_USE_MODULE_XVM_GDB) */
#endif /* defined(MR_USE_TEST) */
