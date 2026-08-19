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

/* Helpers */

typedef struct {
    int id;
} test_elem_t;

static test_elem_t big_buf[1024];

/* Tests */

MR_TEST(fifo, init) {
    mr_uint8_t buf[128];
    mr_uint32_t cap;
    mr_fifo_t fifo;

    cap = mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(test_elem_t));
    MR_TEST_ASSERT_EQ_INT(cap, sizeof(buf) / sizeof(test_elem_t));
    MR_TEST_ASSERT_EQ_INT(mr_fifo_len(&fifo), 0);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_is_empty(&fifo), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_is_full(&fifo), MR_FALSE);
}

MR_TEST(fifo, alloc_free) {
    mr_uint32_t cap;
    mr_fifo_t fifo = {0};

    cap = mr_fifo_alloc(&fifo, 8, sizeof(test_elem_t));
    MR_TEST_EXPECT_EQ_UINT(cap, 8);
    MR_TEST_EXPECT_EQ_UINT(mr_fifo_len(&fifo), 0);

    mr_fifo_free(&fifo);
    MR_TEST_EXPECT_EQ_PTR(fifo.buf, MR_NULL);

    cap = mr_fifo_alloc(&fifo, 7, sizeof(test_elem_t));
    MR_TEST_EXPECT_EQ_UINT(cap, 8);
    MR_TEST_EXPECT_EQ_UINT(mr_fifo_len(&fifo), 0);

    mr_fifo_free(&fifo);
    MR_TEST_EXPECT_EQ_PTR(fifo.buf, MR_NULL);

#if defined(MR_USE_MALLOC)
    cap = mr_fifo_alloc(&fifo, MR_UINT32_MAX >> 1, 1);
    MR_TEST_EXPECT_EQ_UINT(cap, 0);
#endif /* defined(MR_USE_MALLOC) */

    mr_fifo_free(&fifo);
}

MR_TEST(fifo, boundary_init_alloc) {
    mr_uint8_t buf[8];
    test_elem_t elem = {.id = 1};
    test_elem_t out;
    mr_uint32_t cap;
    mr_fifo_t fifo = {0};

    cap = mr_fifo_init(&fifo, buf, 0, sizeof(test_elem_t));
    MR_TEST_ASSERT_EQ_INT(cap, 0);
    MR_TEST_ASSERT_EQ_INT(fifo.elemsz, 0);
    MR_TEST_ASSERT_EQ_PTR(fifo.buf, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_push(&fifo, &elem), 0);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_pop(&fifo, &out), 0);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_write(&fifo, &elem, 1), 0);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_peek(&fifo, &out, 1), 0);

    cap = mr_fifo_init(&fifo, buf, 2, sizeof(test_elem_t));
    MR_TEST_ASSERT_EQ_INT(cap, 0);
    MR_TEST_ASSERT_EQ_INT(fifo.elemsz, 0);
    MR_TEST_ASSERT_EQ_PTR(fifo.buf, MR_NULL);

    cap = mr_fifo_init(&fifo, buf, sizeof(buf), 0);
    MR_TEST_ASSERT_EQ_INT(cap, 0);
    MR_TEST_ASSERT_EQ_INT(fifo.elemsz, 0);
    MR_TEST_ASSERT_EQ_PTR(fifo.buf, MR_NULL);

    cap = mr_fifo_init(&fifo, MR_NULL, sizeof(buf), sizeof(test_elem_t));
    MR_TEST_ASSERT_EQ_INT(cap, 0);
    MR_TEST_ASSERT_EQ_INT(fifo.elemsz, 0);
    MR_TEST_ASSERT_EQ_PTR(fifo.buf, MR_NULL);

    cap = mr_fifo_alloc(&fifo, 0, sizeof(test_elem_t));
    MR_TEST_ASSERT_EQ_INT(cap, 0);

    cap = mr_fifo_alloc(&fifo, 8, 0);
    MR_TEST_ASSERT_EQ_INT(cap, 0);

#if defined(MR_USE_MALLOC)
    cap = mr_fifo_alloc(&fifo, (MR_UINT32_MAX / sizeof(test_elem_t)) + 1,
                        sizeof(test_elem_t));
    MR_TEST_ASSERT_EQ_INT(cap, 0);
#endif /* defined(MR_USE_MALLOC) */
}

MR_TEST(fifo, push_pop) {
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

MR_TEST(fifo, fill_empty) {
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

MR_TEST(fifo, write_read) {
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

MR_TEST(fifo, peek) {
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

MR_TEST(fifo, skip) {
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

MR_TEST(fifo, overwrite) {
    mr_uint8_t buf[128];
    mr_fifo_t fifo;
    test_elem_t src[4];
    test_elem_t dst[2];
    test_elem_t elem;
    mr_uint32_t cap;
    mr_uint32_t i;

    cap = mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(test_elem_t));

    for (i = 0; i < cap; ++i) {
        elem.id = (int)i;
        mr_fifo_push(&fifo, &elem);
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

MR_TEST(fifo, reset) {
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

MR_TEST(fifo, empty_full) {
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

MR_TEST(fifo, avail_len) {
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

MR_TEST(fifo, claim_finish) {
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

MR_TEST(fifo, wrap) {
    mr_uint8_t buf[32];
    mr_fifo_t fifo;
    test_elem_t elem;
    mr_uint32_t cap;
    mr_uint32_t i;

    cap = mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(test_elem_t));
    MR_TEST_ASSERT_GE_UINT(cap, 4);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_size(&fifo), cap);

    for (i = 0; i < cap; i++) {
        elem.id = (int)i;
        MR_TEST_ASSERT_EQ_UINT(mr_fifo_push(&fifo, &elem), 1);
    }
    for (i = 0; i < (cap / 2); i++) {
        MR_TEST_ASSERT_EQ_UINT(mr_fifo_pop(&fifo, &elem), 1);
        MR_TEST_ASSERT_EQ_INT(elem.id, (int)i);
    }
    for (i = cap; i < (cap + (cap / 2)); i++) {
        elem.id = (int)i;
        MR_TEST_ASSERT_EQ_UINT(mr_fifo_push(&fifo, &elem), 1);
    }
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), cap);
    for (i = cap / 2; i < (cap + (cap / 2)); i++) {
        MR_TEST_ASSERT_EQ_UINT(mr_fifo_pop(&fifo, &elem), 1);
        MR_TEST_ASSERT_EQ_INT(elem.id, (int)i);
    }
    MR_TEST_ASSERT_EQ_INT(mr_fifo_is_empty(&fifo), MR_TRUE);
}

MR_TEST(fifo, write_limited_and_skip) {
    mr_uint8_t buf[32];
    mr_fifo_t fifo;
    test_elem_t src[4];
    test_elem_t elem;
    mr_uint32_t cap;
    mr_uint32_t i;

    cap = mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(test_elem_t));
    for (i = 0; i < cap; i++) {
        elem.id = (int)i;
        mr_fifo_push(&fifo, &elem);
    }
    src[0].id = 90;
    src[1].id = 91;
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_write(&fifo, src, 2), 0);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_skip(&fifo, cap + 8), cap);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_is_empty(&fifo), MR_TRUE);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_skip(&fifo, 1), 0);
}

MR_TEST(fifo, overwrite_finish) {
    mr_uint8_t buf[32];
    mr_fifo_t fifo;
    test_elem_t *p;
    test_elem_t out[2];
    mr_uint32_t cap;
    mr_uint32_t ret;
    mr_uint32_t i;
    test_elem_t elem;

    cap = mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(test_elem_t));
    for (i = 0; i < cap; i++) {
        elem.id = (int)i;
        mr_fifo_push(&fifo, &elem);
    }

    ret = mr_fifo_overwrite_claim(&fifo, 0, (void **)&p, 2);
    MR_TEST_ASSERT_EQ_UINT(ret, 2);
    p[0].id = 200;
    p[1].id = 201;
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_overwrite_finish(&fifo, 2), 2);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_len(&fifo), cap);

    MR_TEST_ASSERT_EQ_UINT(mr_fifo_skip(&fifo, cap - 2), cap - 2);
    MR_TEST_ASSERT_EQ_UINT(mr_fifo_read(&fifo, out, 2), 2);
    MR_TEST_ASSERT_EQ_INT(out[0].id, 200);
    MR_TEST_ASSERT_EQ_INT(out[1].id, 201);
}

#if defined(MR_USE_TEST_PTHREAD)
#include <pthread.h>

#define FIFO_MT_COUNT (4096)

typedef struct fifo_mt_ctx {
    volatile mr_uint32_t *__test_status;
    mr_fifo_t *fifo;
    int count;
} fifo_mt_ctx_t;

static void fifo_mt_producer_run(fifo_mt_ctx_t *ctx) {
    test_elem_t elem;
    int i;

    for (i = 0; i < ctx->count; i++) {
        elem.id = i;
        while (mr_fifo_push(ctx->fifo, &elem) != 1) {
        }
    }
}

static void fifo_mt_consumer_run(fifo_mt_ctx_t *ctx) {
    volatile mr_uint32_t *__test_status = ctx->__test_status;
    test_elem_t elem;
    int got = 0;

    while (got < ctx->count) {
        if (mr_fifo_pop(ctx->fifo, &elem) == 1) {
            MR_TEST_EXPECT_EQ_INT(elem.id, got);
            got++;
        }
    }
}

static void *fifo_mt_producer(void *arg) {
    fifo_mt_producer_run(arg);
    return MR_NULL;
}

static void *fifo_mt_consumer(void *arg) {
    fifo_mt_consumer_run(arg);
    return MR_NULL;
}

MR_TEST(fifo, concurrent_spsc) {
    pthread_t producer;
    pthread_t consumer;
    fifo_mt_ctx_t ctx;
    mr_uint8_t buf[1024];
    mr_fifo_t fifo;
    int ret;

    MR_TEST_ASSERT_NE_UINT(
        mr_fifo_init(&fifo, buf, sizeof(buf), sizeof(test_elem_t)), 0);
    ctx.__test_status = __test_status;
    ctx.fifo = &fifo;
    ctx.count = FIFO_MT_COUNT;

    ret = pthread_create(&producer, MR_NULL, fifo_mt_producer, &ctx);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = pthread_create(&consumer, MR_NULL, fifo_mt_consumer, &ctx);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(pthread_join(producer, MR_NULL), 0);
    MR_TEST_EXPECT_EQ_INT(pthread_join(consumer, MR_NULL), 0);
    MR_TEST_ASSERT_EQ_INT(mr_fifo_is_empty(&fifo), MR_TRUE);
}
#endif /* defined(MR_USE_TEST_PTHREAD) */

#endif /* defined(MR_USE_TEST) */
