/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/kernel/mr_fifo.h>
#include <mrX/libc/mr_string.h>

typedef struct {
    int id;
} test_elem_t;

static test_elem_t big_buf[1024];

/* -------------------------------------------------------------------------- */
/* Test: Init with external buffer                                            */
static void test_fifo_init(void) {
    mr_uint8_t buf[128];
    mr_uint32_t cap;
    mr_fifo_t fifo;

    cap = mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(test_elem_t));
    MR_TEST_ASSERT_EQ_INT(cap, sizeof(buf) / sizeof(test_elem_t));
    MR_TEST_ASSERT_EQ_INT(mr_fifo_len(&fifo), 0);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_is_empty(&fifo), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_is_full(&fifo), MR_FALSE);
}
MR_TEST_EXPORT(fifo, init, test_fifo_init);

/* -------------------------------------------------------------------------- */
/* Test: Alloc / Free                                                         */
static void test_fifo_alloc_free(void) {
    mr_uint32_t cap;
    mr_fifo_t fifo;

    cap = mr_fifo_alloc(&fifo, 8, sizeof(test_elem_t));
    MR_TEST_ASSERT_EQ_INT(cap, 8);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_len(&fifo), 0);

    mr_fifo_free(&fifo);
    MR_TEST_ASSERT_EQ_PTR(fifo.buf, MR_NULL);

    cap = mr_fifo_alloc(&fifo, 7, sizeof(test_elem_t));
    MR_TEST_ASSERT_EQ_INT(cap, 8);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_len(&fifo), 0);

    mr_fifo_free(&fifo);
    MR_TEST_ASSERT_EQ_PTR(fifo.buf, MR_NULL);

#if defined(MR_USE_MALLOC)
    cap = mr_fifo_alloc(&fifo, MR_UINT32_MAX >> 1, 1);
    MR_TEST_ASSERT_EQ_INT(cap, 0);
#endif /* defined(MR_USE_MALLOC) */

    mr_fifo_free(&fifo);
}
MR_TEST_EXPORT(fifo, alloc_free, test_fifo_alloc_free);

/* -------------------------------------------------------------------------- */
/* Test: Single push / pop                                                    */
static void test_fifo_push_pop(void) {
    mr_uint8_t buf[128];
    mr_fifo_t fifo;
    test_elem_t in;
    test_elem_t out;
    mr_uint32_t ret;

    in.id = 42;
    mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(test_elem_t));

    ret = mr_fifo_push(&fifo, &in);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_len(&fifo), 1);

    ret = mr_fifo_pop(&fifo, &out);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_INT(out.id, 42);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_len(&fifo), 0);
}
MR_TEST_EXPORT(fifo, push_pop, test_fifo_push_pop);

/* -------------------------------------------------------------------------- */
/* Test: Fill and empty                                                       */
static void test_fifo_fill_empty(void) {
    mr_uint8_t buf[128];
    mr_fifo_t fifo;
    mr_uint32_t cap;
    mr_uint32_t i;
    test_elem_t e;

    cap = mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(test_elem_t));

    for (i = 0; i < cap; ++i) {
        e.id = (int)i;
        MR_TEST_ASSERT_EQ_INT(mr_fifo_push(&fifo, &e), 1);
    }
    MR_TEST_ASSERT_EQ_INT(mr_fifo_len(&fifo), cap);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_is_full(&fifo), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_push(&fifo, &e), 0);

    for (i = 0; i < cap; ++i) {
        MR_TEST_ASSERT_EQ_INT(mr_fifo_pop(&fifo, &e), 1);
        MR_TEST_ASSERT_EQ_INT(e.id, (int)i);
    }
    MR_TEST_ASSERT_EQ_INT(mr_fifo_len(&fifo), 0);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_is_empty(&fifo), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_pop(&fifo, &e), 0);
}
MR_TEST_EXPORT(fifo, fill_empty, test_fifo_fill_empty);

/* -------------------------------------------------------------------------- */
/* Test: Write / Read                                                         */
static void test_fifo_write_read(void) {
    mr_uint8_t buf[128];
    mr_fifo_t fifo;
    test_elem_t src[4];
    test_elem_t dst[4];
    mr_uint32_t ret;

    mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(test_elem_t));

    src[0].id = 1;
    src[1].id = 2;
    src[2].id = 3;
    src[3].id = 4;
    ret = mr_fifo_write(&fifo, src, 4);
    MR_TEST_ASSERT_EQ_INT(ret, 4);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_len(&fifo), 4);

    ret = mr_fifo_read(&fifo, dst, 4);
    MR_TEST_ASSERT_EQ_INT(ret, 4);
    MR_TEST_ASSERT_EQ_INT(dst[0].id, 1);
    MR_TEST_ASSERT_EQ_INT(dst[1].id, 2);
    MR_TEST_ASSERT_EQ_INT(dst[2].id, 3);
    MR_TEST_ASSERT_EQ_INT(dst[3].id, 4);

    MR_TEST_ASSERT_EQ_INT(mr_fifo_write(&fifo, MR_NULL, 0), 0);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_write(&fifo, big_buf, 1024),
                          sizeof(buf) / sizeof(test_elem_t));
}
MR_TEST_EXPORT(fifo, write_read, test_fifo_write_read);

/* -------------------------------------------------------------------------- */
/* Test: Peek without consume                                                 */
static void test_fifo_peek(void) {
    mr_uint8_t buf[128];
    mr_fifo_t fifo;
    test_elem_t in;
    test_elem_t out;
    mr_uint32_t ret;

    mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(test_elem_t));

    in.id = 99;
    mr_fifo_push(&fifo, &in);

    ret = mr_fifo_peek(&fifo, &out, 1);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_INT(out.id, 99);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_len(&fifo), 1);

    ret = mr_fifo_pop(&fifo, &out);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_len(&fifo), 0);

    MR_TEST_ASSERT_EQ_INT(mr_fifo_peek(&fifo, MR_NULL, 0), 0);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_peek(&fifo, big_buf, 1024), 0);
}
MR_TEST_EXPORT(fifo, peek, test_fifo_peek);

/* -------------------------------------------------------------------------- */
/* Test: Skip elements                                                        */
static void test_fifo_skip(void) {
    mr_uint8_t buf[128];
    mr_fifo_t fifo;
    test_elem_t e[3];
    test_elem_t rem;
    mr_uint32_t ret;

    mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(test_elem_t));

    e[0].id = 1;
    e[1].id = 2;
    e[2].id = 3;

    mr_fifo_write(&fifo, e, 3);
    ret = mr_fifo_skip(&fifo, 2);
    MR_TEST_ASSERT_EQ_INT(ret, 2);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_len(&fifo), 1);

    ret = mr_fifo_pop(&fifo, &rem);
    MR_TEST_ASSERT_EQ_INT(ret, 1);
    MR_TEST_ASSERT_EQ_INT(rem.id, 3);
}
MR_TEST_EXPORT(fifo, skip, test_fifo_skip);

/* -------------------------------------------------------------------------- */
/* Test: Overwrite                                                            */
static void test_fifo_overwrite(void) {
    mr_uint8_t buf[128];
    mr_fifo_t fifo;
    test_elem_t src[4];
    test_elem_t dst[2];
    mr_uint32_t cap;
    mr_uint32_t i;

    cap = mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(test_elem_t));

    for (i = 0; i < cap; ++i) {
        test_elem_t e = {.id = (int)i};
        mr_fifo_push(&fifo, &e);
    }
    src[0].id = 100;
    src[1].id = 101;
    mr_fifo_overwrite(&fifo, src, 2);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_len(&fifo), cap);

    mr_fifo_skip(&fifo, cap - 2);
    mr_fifo_read(&fifo, dst, 2);
    MR_TEST_ASSERT_EQ_INT(dst[0].id, 100);
    MR_TEST_ASSERT_EQ_INT(dst[1].id, 101);

    MR_TEST_ASSERT_EQ_INT(mr_fifo_overwrite(&fifo, big_buf, 1024), 32);
}
MR_TEST_EXPORT(fifo, overwrite, test_fifo_overwrite);

/* -------------------------------------------------------------------------- */
/* Test: Reset                                                                */
static void test_fifo_reset(void) {
    mr_uint8_t buf[128];
    mr_fifo_t fifo;
    test_elem_t e;

    mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(test_elem_t));

    e.id = 11;
    mr_fifo_push(&fifo, &e);
    mr_fifo_reset(&fifo);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_len(&fifo), 0);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_is_empty(&fifo), MR_TRUE);
}
MR_TEST_EXPORT(fifo, reset, test_fifo_reset);

/* -------------------------------------------------------------------------- */
/* Test: Empty / Full transitions                                             */
static void test_fifo_empty_full(void) {
    mr_uint8_t buf[128];
    mr_fifo_t fifo;
    test_elem_t e;
    mr_uint32_t cap;
    mr_uint32_t i;

    cap = mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(test_elem_t));

    for (i = 0; i < cap; ++i) {
        e.id = (int)i;
        MR_TEST_ASSERT_EQ_INT(mr_fifo_push(&fifo, &e), 1);
    }
    MR_TEST_ASSERT_EQ_INT(mr_fifo_is_full(&fifo), MR_TRUE);

    for (i = 0; i < cap; ++i) {
        MR_TEST_ASSERT_EQ_INT(mr_fifo_pop(&fifo, &e), 1);
    }
    MR_TEST_ASSERT_EQ_INT(mr_fifo_is_empty(&fifo), MR_TRUE);
}
MR_TEST_EXPORT(fifo, empty_full, test_fifo_empty_full);

/* -------------------------------------------------------------------------- */
/* Test: Available / length consistency                                       */
static void test_fifo_avail_len(void) {
    mr_uint8_t buf[128];
    mr_fifo_t fifo;
    test_elem_t e;
    mr_uint32_t cap;
    mr_uint32_t i;

    cap = mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(test_elem_t));

    for (i = 0; i < cap; ++i) {
        e.id = (int)i;
        mr_fifo_push(&fifo, &e);
        MR_TEST_ASSERT_EQ_INT(mr_fifo_len(&fifo), i + 1);
        MR_TEST_ASSERT_EQ_INT(mr_fifo_avail(&fifo), cap - i - 1);
    }
}
MR_TEST_EXPORT(fifo, avail_len, test_fifo_avail_len);

static void test_fifo_claim_finsh(void) {
    mr_uint8_t buf[128];
    mr_uint32_t cap;
    mr_uint32_t ret;
    mr_fifo_t fifo;
    mr_uint8_t *p;
    mr_uint32_t i;

    cap = mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(test_elem_t));

    ret = mr_fifo_write_claim(&fifo, 0, (void **)&p, cap / 2);
    MR_TEST_ASSERT_EQ_UINT(ret, cap / 2);
    mr_memset(p, '#', ret);
    ret = mr_fifo_write_finish(&fifo, ret);
    MR_TEST_ASSERT_EQ_UINT(ret, cap / 2);

    ret = mr_fifo_read_claim(&fifo, 0, (void **)&p, cap / 2);
    MR_TEST_ASSERT_EQ_UINT(ret, cap / 2);
    for (i = 0; i < ret; i++) {
        MR_TEST_ASSERT_EQ_UINT(p[i], '#');
    }
    mr_fifo_read_finish(&fifo, ret);

    ret = mr_fifo_write_claim(&fifo, 0, (void **)&p, cap + 1);
    MR_TEST_ASSERT_EQ_UINT(ret, cap / 2);
    ret = mr_fifo_write_finish(&fifo, ret);
    MR_TEST_ASSERT_EQ_UINT(ret, cap / 2);

    ret = mr_fifo_write_claim(&fifo, 0, (void **)&p, cap / 4);
    MR_TEST_ASSERT_EQ_UINT(ret, cap / 4);
    ret = mr_fifo_write_claim(&fifo, ret, (void **)&p, cap / 4);
    MR_TEST_ASSERT_EQ_UINT(ret, cap / 4);
    mr_fifo_write_finish(&fifo, cap / 2);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), cap);

    ret = mr_fifo_overwrite_claim(&fifo, 0, (void **)&p, cap);
    MR_TEST_ASSERT_EQ_UINT(ret, cap / 2);
}
MR_TEST_EXPORT(fifo, claim_finish, test_fifo_claim_finsh);
#endif /* defined(MR_USE_TEST) */
