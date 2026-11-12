/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-30        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/kernel/mr_fifo.h>

/* Helpers */

typedef struct {
    int id;
} test_deep_elem_t;

MR_TEST(fifo, capacity_one_edge) {
    mr_uint8_t buf[sizeof(test_deep_elem_t)];
    mr_fifo_t fifo;
    test_deep_elem_t in;
    test_deep_elem_t out;
    test_deep_elem_t *p;
    mr_uint32_t cap;

    in.id = 1;
    cap = mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(test_deep_elem_t));
    MR_TEST_ASSERT_EQ_UINT(cap, 1);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_size(&fifo), 1);

    MR_TEST_ASSERT_EQ_UINT(mr_fifo_push(&fifo, &in), 1);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), 1);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_is_full(&fifo), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_is_empty(&fifo), MR_FALSE);
    in.id = 2;
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_push(&fifo, &in), 0);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), 1);

    MR_TEST_ASSERT_EQ_UINT(mr_fifo_pop(&fifo, &out), 1);
    MR_TEST_ASSERT_EQ_INT(out.id, 1);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_is_empty(&fifo), MR_TRUE);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_pop(&fifo, &out), 0);

    /* The single slot must be reusable after draining */
    in.id = 3;
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_push(&fifo, &in), 1);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_pop(&fifo, &out), 1);
    MR_TEST_ASSERT_EQ_INT(out.id, 3);

    /* Claim boundaries on a one-slot fifo */
    mr_fifo_reset(&fifo);
    in.id = 4;
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_write_claim(&fifo, 0, (void **)&p, 1), 1);
    p[0].id = 4;
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_write_finish(&fifo, 1), 1);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_write_claim(&fifo, 0, (void **)&p, 1), 0);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_read_claim(&fifo, 0, (void **)&p, 1), 1);
    MR_TEST_ASSERT_EQ_INT(p[0].id, 4);
    mr_fifo_read_finish(&fifo, 1);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_read_claim(&fifo, 0, (void **)&p, 1), 0);
}

MR_TEST(fifo, elemsz_one_byte_wrap) {
    mr_uint8_t buf[8];
    mr_uint8_t out[8];
    mr_fifo_t fifo;
    mr_uint32_t ret;

    MR_TEST_ASSERT_EQ_UINT(mr_fifo_init(&fifo, buf, sizeof(buf), 1), 8);

    ret = mr_fifo_write(&fifo, "abcdef", 6);
    MR_TEST_ASSERT_EQ_UINT(ret, 6);
    ret = mr_fifo_read(&fifo, out, 4);
    MR_TEST_ASSERT_EQ_UINT(ret, 4);
    MR_TEST_ASSERT_EQ_UINT(out[0], 'a');
    MR_TEST_ASSERT_EQ_UINT(out[1], 'b');
    MR_TEST_ASSERT_EQ_UINT(out[2], 'c');
    MR_TEST_ASSERT_EQ_UINT(out[3], 'd');
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), 2);

    /* This write splits across the backing buffer boundary */
    ret = mr_fifo_write(&fifo, "ghijkl", 6);
    MR_TEST_ASSERT_EQ_UINT(ret, 6);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), 8);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_is_full(&fifo), MR_TRUE);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_write(&fifo, "x", 1), 0);

    ret = mr_fifo_peek(&fifo, out, 8);
    MR_TEST_ASSERT_EQ_UINT(ret, 8);
    MR_TEST_ASSERT_EQ_UINT(out[0], 'e');
    MR_TEST_ASSERT_EQ_UINT(out[1], 'f');
    MR_TEST_ASSERT_EQ_UINT(out[2], 'g');
    MR_TEST_ASSERT_EQ_UINT(out[3], 'h');
    MR_TEST_ASSERT_EQ_UINT(out[4], 'i');
    MR_TEST_ASSERT_EQ_UINT(out[5], 'j');
    MR_TEST_ASSERT_EQ_UINT(out[6], 'k');
    MR_TEST_ASSERT_EQ_UINT(out[7], 'l');
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), 8);

    ret = mr_fifo_read(&fifo, out, 8);
    MR_TEST_ASSERT_EQ_UINT(ret, 8);
    MR_TEST_ASSERT_EQ_UINT(out[0], 'e');
    MR_TEST_ASSERT_EQ_UINT(out[7], 'l');
    MR_TEST_ASSERT_EQ_INT(mr_fifo_is_empty(&fifo), MR_TRUE);
}

MR_TEST(fifo, uninitialized_operations) {
    mr_fifo_t fifo = {0};
    test_deep_elem_t e = {.id = 1};
    test_deep_elem_t out;
    test_deep_elem_t sentinel;
    test_deep_elem_t *p = &sentinel;

    MR_TEST_ASSERT_EQ_UINT(mr_fifo_push(&fifo, &e), 0);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_pop(&fifo, &out), 0);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_write(&fifo, &e, 1), 0);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_read(&fifo, &out, 1), 0);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_peek(&fifo, &out, 1), 0);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_overwrite(&fifo, &e, 1), 0);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_skip(&fifo, 1), 0);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), 0);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_size(&fifo), 0);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_avail(&fifo), 0);

    /* A zero-capacity fifo is reported as both empty and full */
    MR_TEST_ASSERT_EQ_INT(mr_fifo_is_empty(&fifo), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_is_full(&fifo), MR_TRUE);

    /* Claims must fail without touching the output pointer */
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_write_claim(&fifo, 0, (void **)&p, 1), 0);
    MR_TEST_ASSERT_EQ_PTR(p, &sentinel);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_read_claim(&fifo, 0, (void **)&p, 1), 0);
    MR_TEST_ASSERT_EQ_PTR(p, &sentinel);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_overwrite_claim(&fifo, 0, (void **)&p, 1),
                           0);
    MR_TEST_ASSERT_EQ_PTR(p, &sentinel);

    /* Finish helpers clamp to zero capacity */
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_write_finish(&fifo, 1), 0);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_overwrite_finish(&fifo, 1), 0);
    mr_fifo_read_finish(&fifo, 1);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), 0);
}

MR_TEST(fifo, reinit_and_failed_reinit) {
    mr_uint8_t buf1[16];
    mr_uint8_t buf2[16];
    mr_fifo_t fifo;
    test_deep_elem_t e1 = {.id = 1};
    test_deep_elem_t e2 = {.id = 2};
    test_deep_elem_t out;
    mr_uint32_t cap;

    cap = mr_fifo_init(&fifo, buf1, sizeof(buf1), sizeof(test_deep_elem_t));
    MR_TEST_ASSERT_EQ_UINT(cap, 16 / sizeof(test_deep_elem_t));
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_push(&fifo, &e1), 1);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_push(&fifo, &e2), 1);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), 2);

    /* Re-init resets the cursors and binds the new buffer */
    cap = mr_fifo_init(&fifo, buf2, sizeof(buf2), sizeof(test_deep_elem_t));
    MR_TEST_ASSERT_EQ_UINT(cap, 16 / sizeof(test_deep_elem_t));
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), 0);
    MR_TEST_ASSERT_EQ_PTR(fifo.buf, buf2);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_push(&fifo, &e1), 1);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_pop(&fifo, &out), 1);
    MR_TEST_ASSERT_EQ_INT(out.id, 1);

    /* A failed re-init must keep the fifo intact and usable */
    cap = mr_fifo_init(&fifo, buf2, 2, sizeof(test_deep_elem_t));
    MR_TEST_ASSERT_EQ_UINT(cap, 0);
    MR_TEST_ASSERT_EQ_PTR(fifo.buf, buf2);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_push(&fifo, &e2), 1);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_pop(&fifo, &out), 1);
    MR_TEST_ASSERT_EQ_INT(out.id, 2);
}

MR_TEST(fifo, elemsz_truncation_sentinel) {
    mr_uint8_t buf[10];
    mr_fifo_t fifo;
    test_deep_elem_t e = {.id = 1};
    test_deep_elem_t out;
    mr_uint32_t cap;

    buf[8] = 0xAA;
    buf[9] = 0x55;
    cap = mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(test_deep_elem_t));
    MR_TEST_ASSERT_EQ_UINT(cap, 2);

    MR_TEST_ASSERT_EQ_UINT(mr_fifo_push(&fifo, &e), 1);
    e.id = 2;
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_push(&fifo, &e), 1);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_is_full(&fifo), MR_TRUE);
    e.id = 3;
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_push(&fifo, &e), 0);

    MR_TEST_ASSERT_EQ_UINT(mr_fifo_pop(&fifo, &out), 1);
    MR_TEST_ASSERT_EQ_INT(out.id, 1);
    e.id = 3;
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_push(&fifo, &e), 1);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_pop(&fifo, &out), 1);
    MR_TEST_ASSERT_EQ_INT(out.id, 2);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_pop(&fifo, &out), 1);
    MR_TEST_ASSERT_EQ_INT(out.id, 3);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_is_empty(&fifo), MR_TRUE);

    /* Unused bytes beyond capacity * elemsz must be untouched */
    MR_TEST_EXPECT_EQ_UINT(buf[8], 0xAA);
    MR_TEST_EXPECT_EQ_UINT(buf[9], 0x55);
}

MR_TEST(fifo, write_split_wrap_elemsz) {
    mr_uint8_t buf[3 * sizeof(test_deep_elem_t)];
    mr_fifo_t fifo;
    test_deep_elem_t in[3];
    test_deep_elem_t out[3];

    MR_TEST_ASSERT_EQ_UINT(
        mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(test_deep_elem_t)), 3);
    in[0].id = 1;
    in[1].id = 2;
    in[2].id = 3;
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_write(&fifo, in, 2), 2);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_read(&fifo, out, 1), 1);
    MR_TEST_ASSERT_EQ_INT(out[0].id, 1);

    /* Only two of three fit, the second element wraps to the buffer start */
    in[0].id = 3;
    in[1].id = 4;
    in[2].id = 5;
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_write(&fifo, in, 3), 2);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), 3);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_read(&fifo, out, 3), 3);
    MR_TEST_ASSERT_EQ_INT(out[0].id, 2);
    MR_TEST_ASSERT_EQ_INT(out[1].id, 3);
    MR_TEST_ASSERT_EQ_INT(out[2].id, 4);
}

MR_TEST(fifo, overwrite_last_elements) {
    mr_uint8_t buf[4 * sizeof(test_deep_elem_t)];
    mr_fifo_t fifo;
    test_deep_elem_t src[5];
    test_deep_elem_t out[4];
    test_deep_elem_t e;
    mr_uint32_t ret;
    mr_uint32_t i;

    MR_TEST_ASSERT_EQ_UINT(
        mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(test_deep_elem_t)), 4);
    for (i = 0; i < 4; i++) {
        e.id = (int)i;
        MR_TEST_ASSERT_EQ_UINT(mr_fifo_push(&fifo, &e), 1);
    }

    src[0].id = 10;
    src[1].id = 11;
    src[2].id = 12;
    src[3].id = 13;
    src[4].id = 14;
    ret = mr_fifo_overwrite(&fifo, src, 5);
    MR_TEST_ASSERT_EQ_UINT(ret, 4);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), 4);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_read(&fifo, out, 4), 4);
    MR_TEST_ASSERT_EQ_INT(out[0].id, 11);
    MR_TEST_ASSERT_EQ_INT(out[1].id, 12);
    MR_TEST_ASSERT_EQ_INT(out[2].id, 13);
    MR_TEST_ASSERT_EQ_INT(out[3].id, 14);

    src[0].id = 20;
    src[1].id = 21;
    src[2].id = 22;
    src[3].id = 23;
    src[4].id = 24;
    ret = mr_fifo_overwrite(&fifo, src, 5);
    MR_TEST_ASSERT_EQ_UINT(ret, 4);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_read(&fifo, out, 4), 4);
    MR_TEST_ASSERT_EQ_INT(out[0].id, 21);
    MR_TEST_ASSERT_EQ_INT(out[1].id, 22);
    MR_TEST_ASSERT_EQ_INT(out[2].id, 23);
    MR_TEST_ASSERT_EQ_INT(out[3].id, 24);
}

MR_TEST(fifo, overwrite_zero_and_empty) {
    mr_uint8_t buf[4 * sizeof(test_deep_elem_t)];
    mr_fifo_t fifo;
    test_deep_elem_t src[2];
    test_deep_elem_t out[2];
    test_deep_elem_t e;
    mr_uint32_t i;

    MR_TEST_ASSERT_EQ_UINT(
        mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(test_deep_elem_t)), 4);
    src[0].id = 1;
    src[1].id = 2;

    MR_TEST_ASSERT_EQ_UINT(mr_fifo_overwrite(&fifo, src, 0), 0);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), 0);

    MR_TEST_ASSERT_EQ_UINT(mr_fifo_overwrite(&fifo, src, 2), 2);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), 2);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_read(&fifo, out, 2), 2);
    MR_TEST_ASSERT_EQ_INT(out[0].id, 1);
    MR_TEST_ASSERT_EQ_INT(out[1].id, 2);

    for (i = 0; i < 4; i++) {
        e.id = (int)i;
        mr_fifo_push(&fifo, &e);
    }
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_overwrite(&fifo, src, 0), 0);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), 4);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_is_full(&fifo), MR_TRUE);
}

MR_TEST(fifo, claim_offset_boundaries) {
    mr_uint8_t buf[4 * sizeof(test_deep_elem_t)];
    mr_fifo_t fifo;
    test_deep_elem_t in;
    test_deep_elem_t out[4];
    test_deep_elem_t *p;
    mr_uint32_t ret;

    MR_TEST_ASSERT_EQ_UINT(
        mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(test_deep_elem_t)), 4);
    in.id = 1;
    mr_fifo_push(&fifo, &in);
    in.id = 2;
    mr_fifo_push(&fifo, &in);
    in.id = 3;
    mr_fifo_push(&fifo, &in);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_read(&fifo, out, 1), 1);

    /* Free space spans the wrap, each claim returns one contiguous run */
    ret = mr_fifo_write_claim(&fifo, 0, (void **)&p, 2);
    MR_TEST_ASSERT_EQ_UINT(ret, 1);
    p[0].id = 4;
    ret = mr_fifo_write_claim(&fifo, 1, (void **)&p, 2);
    MR_TEST_ASSERT_EQ_UINT(ret, 1);
    p[0].id = 5;
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_write_finish(&fifo, 2), 2);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), 4);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_read(&fifo, out, 4), 4);
    MR_TEST_ASSERT_EQ_INT(out[0].id, 2);
    MR_TEST_ASSERT_EQ_INT(out[1].id, 3);
    MR_TEST_ASSERT_EQ_INT(out[2].id, 4);
    MR_TEST_ASSERT_EQ_INT(out[3].id, 5);

    /* Offset at the availability boundary gets rejected */
    in.id = 6;
    mr_fifo_push(&fifo, &in);
    in.id = 7;
    mr_fifo_push(&fifo, &in);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), 2);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_write_claim(&fifo, 2, (void **)&p, 1), 0);
    ret = mr_fifo_write_claim(&fifo, 0, (void **)&p, 1);
    MR_TEST_ASSERT_EQ_UINT(ret, 1);
    p[0].id = 8;
    ret = mr_fifo_write_claim(&fifo, 1, (void **)&p, 1);
    MR_TEST_ASSERT_EQ_UINT(ret, 1);
    p[0].id = 9;
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_write_finish(&fifo, 2), 2);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), 4);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_read(&fifo, out, 4), 4);
    MR_TEST_ASSERT_EQ_INT(out[0].id, 6);
    MR_TEST_ASSERT_EQ_INT(out[1].id, 7);
    MR_TEST_ASSERT_EQ_INT(out[2].id, 8);
    MR_TEST_ASSERT_EQ_INT(out[3].id, 9);
}

MR_TEST(fifo, claim_finish_clamping) {
    mr_uint8_t buf[4 * sizeof(test_deep_elem_t)];
    mr_fifo_t fifo;
    test_deep_elem_t e = {.id = 1};
    test_deep_elem_t *p;
    mr_uint32_t ret;

    MR_TEST_ASSERT_EQ_UINT(
        mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(test_deep_elem_t)), 4);

    /* Finishing more than avail clamps instead of corrupting the cursor */
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_write_claim(&fifo, 0, (void **)&p, 3), 3);
    ret = mr_fifo_write_finish(&fifo, 10);
    MR_TEST_ASSERT_EQ_UINT(ret, 4);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), 4);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_is_full(&fifo), MR_TRUE);
    mr_fifo_reset(&fifo);

    /* Read finish clamps to the fifo length */
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_push(&fifo, &e), 1);
    e.id = 2;
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_push(&fifo, &e), 1);
    ret = mr_fifo_read_claim(&fifo, 0, (void **)&p, 2);
    MR_TEST_ASSERT_EQ_UINT(ret, 2);
    MR_TEST_ASSERT_EQ_INT(p[0].id, 1);
    mr_fifo_read_finish(&fifo, 99);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), 0);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_is_empty(&fifo), MR_TRUE);
    mr_fifo_reset(&fifo);

    /* Overwrite finish clamps to the fifo capacity */
    ret = mr_fifo_overwrite_finish(&fifo, 99);
    MR_TEST_ASSERT_EQ_UINT(ret, 4);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), 4);
}

MR_TEST(fifo, overwrite_claim_wrap_protocol) {
    mr_uint8_t buf[4 * sizeof(test_deep_elem_t)];
    mr_fifo_t fifo;
    test_deep_elem_t in[3];
    test_deep_elem_t out[4];
    test_deep_elem_t *p;
    mr_uint32_t ret;

    MR_TEST_ASSERT_EQ_UINT(
        mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(test_deep_elem_t)), 4);
    in[0].id = 1;
    in[1].id = 2;
    in[2].id = 3;
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_write(&fifo, in, 3), 3);

    /* Only one free slot, the claim is limited by contiguity */
    ret = mr_fifo_overwrite_claim(&fifo, 0, (void **)&p, 2);
    MR_TEST_ASSERT_EQ_UINT(ret, 1);
    p[0].id = 4;
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_overwrite_finish(&fifo, 1), 1);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), 4);

    /* Offset claim overwrites unread data and spans the wrap */
    ret = mr_fifo_overwrite_claim(&fifo, 1, (void **)&p, 3);
    MR_TEST_ASSERT_EQ_UINT(ret, 3);
    p[0].id = 5;
    p[1].id = 6;
    p[2].id = 7;
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_overwrite_finish(&fifo, 4), 4);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), 4);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_read(&fifo, out, 4), 4);
    MR_TEST_ASSERT_EQ_INT(out[0].id, 1);
    MR_TEST_ASSERT_EQ_INT(out[1].id, 5);
    MR_TEST_ASSERT_EQ_INT(out[2].id, 6);
    MR_TEST_ASSERT_EQ_INT(out[3].id, 7);

    /* Offset at the capacity boundary gets rejected */
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_overwrite_claim(&fifo, 4, (void **)&p, 1),
                           0);
}

MR_TEST(fifo, overwrite_finish_after_drain) {
    mr_uint8_t buf[4 * sizeof(test_deep_elem_t)];
    mr_fifo_t fifo;
    test_deep_elem_t e = {.id = 1};
    test_deep_elem_t out[2];
    test_deep_elem_t *p;

    MR_TEST_ASSERT_EQ_UINT(
        mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(test_deep_elem_t)), 4);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_push(&fifo, &e), 1);
    e.id = 2;
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_push(&fifo, &e), 1);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_pop(&fifo, &out[0]), 1);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_pop(&fifo, &out[1]), 1);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_is_empty(&fifo), MR_TRUE);

    MR_TEST_ASSERT_EQ_UINT(mr_fifo_overwrite_claim(&fifo, 0, (void **)&p, 2),
                           2);
    p[0].id = 3;
    p[1].id = 4;
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_overwrite_finish(&fifo, 2), 2);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), 2);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_read(&fifo, out, 2), 2);
    MR_TEST_ASSERT_EQ_INT(out[0].id, 3);
    MR_TEST_ASSERT_EQ_INT(out[1].id, 4);
}

MR_TEST(fifo, peek_partial_no_advance) {
    mr_uint8_t buf[4 * sizeof(test_deep_elem_t)];
    mr_fifo_t fifo;
    test_deep_elem_t e = {.id = 1};
    test_deep_elem_t one;
    test_deep_elem_t out[2];
    mr_uint32_t ret;

    MR_TEST_ASSERT_EQ_UINT(
        mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(test_deep_elem_t)), 4);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_push(&fifo, &e), 1);
    e.id = 2;
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_push(&fifo, &e), 1);

    ret = mr_fifo_peek(&fifo, &one, 1);
    MR_TEST_ASSERT_EQ_UINT(ret, 1);
    MR_TEST_ASSERT_EQ_INT(one.id, 1);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), 2);

    /* Peeking more than available clamps to the length */
    ret = mr_fifo_peek(&fifo, out, 5);
    MR_TEST_ASSERT_EQ_UINT(ret, 2);
    MR_TEST_ASSERT_EQ_INT(out[0].id, 1);
    MR_TEST_ASSERT_EQ_INT(out[1].id, 2);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), 2);

    MR_TEST_ASSERT_EQ_UINT(mr_fifo_peek(&fifo, out, 0), 0);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), 2);

    MR_TEST_ASSERT_EQ_UINT(mr_fifo_read(&fifo, out, 2), 2);
    MR_TEST_ASSERT_EQ_INT(out[0].id, 1);
    MR_TEST_ASSERT_EQ_INT(out[1].id, 2);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_is_empty(&fifo), MR_TRUE);
}

MR_TEST(fifo, skip_clamp_partial) {
    mr_uint8_t buf[4 * sizeof(test_deep_elem_t)];
    mr_fifo_t fifo;
    test_deep_elem_t e = {.id = 1};
    test_deep_elem_t out;
    mr_uint32_t i;

    MR_TEST_ASSERT_EQ_UINT(
        mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(test_deep_elem_t)), 4);
    for (i = 0; i < 4; i++) {
        e.id = (int)i;
        MR_TEST_ASSERT_EQ_UINT(mr_fifo_push(&fifo, &e), 1);
    }

    MR_TEST_ASSERT_EQ_UINT(mr_fifo_skip(&fifo, 3), 3);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), 1);

    /* Skipping more than available clamps to the length */
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_skip(&fifo, 5), 1);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_is_empty(&fifo), MR_TRUE);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_skip(&fifo, 1), 0);

    e.id = 5;
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_push(&fifo, &e), 1);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_pop(&fifo, &out), 1);
    MR_TEST_ASSERT_EQ_INT(out.id, 5);
}

#if defined(MR_USE_MALLOC)
MR_TEST(fifo, ops_after_free) {
    mr_fifo_t fifo = {0};
    test_deep_elem_t e = {.id = 1};
    test_deep_elem_t out;
    test_deep_elem_t sentinel;
    test_deep_elem_t *p = &sentinel;
    mr_uint32_t cap;

    cap = mr_fifo_alloc(&fifo, 4, sizeof(test_deep_elem_t));
    MR_TEST_ASSERT_EQ_UINT(cap, 4);
    mr_fifo_free(&fifo);
    MR_TEST_ASSERT_EQ_PTR(fifo.buf, MR_NULL);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_size(&fifo), 4);

    MR_TEST_ASSERT_EQ_UINT(mr_fifo_push(&fifo, &e), 0);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_pop(&fifo, &out), 0);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_write(&fifo, &e, 1), 0);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_read(&fifo, &out, 1), 0);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_peek(&fifo, &out, 1), 0);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_overwrite(&fifo, &e, 1), 0);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_skip(&fifo, 1), 0);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_write_claim(&fifo, 0, (void **)&p, 1), 0);
    MR_TEST_ASSERT_EQ_PTR(p, &sentinel);
    mr_fifo_read_finish(&fifo, 1);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), 0);

    /* Finish helpers on an uninitialized fifo must be no-ops */
    MR_TEST_EXPECT_EQ_UINT(mr_fifo_write_finish(&fifo, 1), 0);
    MR_TEST_EXPECT_EQ_UINT(mr_fifo_len(&fifo), 0);
    MR_TEST_EXPECT_EQ_UINT(mr_fifo_overwrite_finish(&fifo, 1), 0);
    MR_TEST_EXPECT_EQ_UINT(mr_fifo_len(&fifo), 0);

    mr_fifo_reset(&fifo);

    /* A double free must be a no-op */
    mr_fifo_free(&fifo);
    MR_TEST_EXPECT_EQ_PTR(fifo.buf, MR_NULL);
}
#endif /* defined(MR_USE_MALLOC) */

#if defined(MR_USE_MALLOC)
MR_TEST(fifo, alloc_edge_numbers) {
    mr_fifo_t fifo = {0};
    test_deep_elem_t e = {.id = 1};
    test_deep_elem_t out;
    mr_uint32_t cap;

    /* Zero elements allocates nothing */
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_alloc(&fifo, 0, sizeof(test_deep_elem_t)),
                           0);
    MR_TEST_ASSERT_EQ_PTR(fifo.buf, MR_NULL);

    /* Beyond the internal capacity limit */
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_alloc(&fifo, (MR_UINT32_MAX / 2U) + 2U, 1),
                           0);
    MR_TEST_ASSERT_EQ_PTR(fifo.buf, MR_NULL);

    /* Element number times element size would overflow */
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_alloc(&fifo, 2, (MR_UINT32_MAX / 2U) + 1U),
                           0);
    MR_TEST_ASSERT_EQ_PTR(fifo.buf, MR_NULL);

    /* A single maximum sized element cannot be allocated */
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_alloc(&fifo, 1, MR_UINT32_MAX), 0);
    MR_TEST_ASSERT_EQ_PTR(fifo.buf, MR_NULL);

    /* Allocation still works after the failures */
    cap = mr_fifo_alloc(&fifo, 3, sizeof(test_deep_elem_t));
    MR_TEST_ASSERT_EQ_UINT(cap, 3);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_push(&fifo, &e), 1);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_pop(&fifo, &out), 1);
    MR_TEST_ASSERT_EQ_INT(out.id, 1);
    mr_fifo_free(&fifo);
    MR_TEST_ASSERT_EQ_PTR(fifo.buf, MR_NULL);
}
#endif /* defined(MR_USE_MALLOC) */

MR_TEST(fifo, long_run_cursor_stability) {
    mr_uint8_t buf[4 * sizeof(test_deep_elem_t)];
    mr_fifo_t fifo;
    test_deep_elem_t e;
    test_deep_elem_t out;
    mr_uint32_t r;
    mr_uint32_t i;
    int pushed = 0;
    int popped = 0;

    MR_TEST_ASSERT_EQ_UINT(
        mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(test_deep_elem_t)), 4);

    for (r = 0; r < 3; r++) {
        for (i = 0; i < 3; i++) {
            e.id = pushed++;
            MR_TEST_ASSERT_EQ_UINT(mr_fifo_push(&fifo, &e), 1);
            MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), i + 1);
        }
        for (i = 0; i < 3; i++) {
            MR_TEST_ASSERT_EQ_UINT(mr_fifo_pop(&fifo, &out), 1);
            MR_TEST_ASSERT_EQ_INT(out.id, popped++);
        }
        MR_TEST_ASSERT_EQ_INT(mr_fifo_is_empty(&fifo), MR_TRUE);
        MR_TEST_ASSERT_EQ_UINT(mr_fifo_avail(&fifo), 4);
    }
}

#endif /* defined(MR_USE_TEST) */
