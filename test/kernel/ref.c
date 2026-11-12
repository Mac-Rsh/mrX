/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/kernel/mr_ref.h>

static void test_ref_stub_release(mr_ref_t *ref) {
    *ref = 0xdeadbeef;
}

MR_TEST(ref, static_init) {
    static mr_ref_t r = MR_REF_INIT();

    MR_TEST_ASSERT_EQ_INT(MR_REF_IS_INITED(r), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(MR_REF_COUNT(r), 1);
}

MR_TEST(ref, init) {
    mr_ref_t r;

    mr_ref_init(&r);
    MR_TEST_ASSERT_EQ_INT(MR_REF_IS_INITED(r), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(MR_REF_COUNT(r), 1);
}

MR_TEST(ref, get) {
    mr_ref_t r;

    mr_ref_init(&r);
    MR_TEST_ASSERT_EQ_INT(mr_ref_get(&r), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(MR_REF_COUNT(r), 2);
    MR_TEST_ASSERT_EQ_INT(mr_ref_get(&r), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(MR_REF_COUNT(r), 3);
}

MR_TEST(ref, put) {
    mr_ref_t r;

    mr_ref_init(&r);
    MR_TEST_ASSERT_EQ_INT(mr_ref_put(&r, test_ref_stub_release), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(r & 0xffffffff, 0xdeadbeef);
    MR_TEST_ASSERT_EQ_INT(mr_ref_put(&r, test_ref_stub_release), MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(r & 0xffffffff, 0xdeadbeef - 1);
}

MR_TEST(ref, get_fail) {
    mr_ref_t r;

    mr_ref_init(&r);
    mr_ref_put(&r, test_ref_stub_release);
    MR_TEST_ASSERT_EQ_INT(mr_ref_get(&r), MR_FALSE);
}

MR_TEST(ref, put_not_last) {
    mr_ref_t r;

    mr_ref_init(&r);
    MR_TEST_ASSERT_EQ_INT(mr_ref_get(&r), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(MR_REF_COUNT(r), 2);
    MR_TEST_ASSERT_EQ_INT(mr_ref_put(&r, test_ref_stub_release), MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(MR_REF_IS_INITED(r), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(MR_REF_COUNT(r), 1);
    MR_TEST_ASSERT_EQ_INT(mr_ref_get(&r), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_ref_put(&r, MR_NULL), MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(mr_ref_put(&r, test_ref_stub_release), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(MR_REF_IS_INITED(r), MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(mr_ref_get(&r), MR_FALSE);
}

#if defined(MR_USE_TEST_PTHREAD)
#include <pthread.h>

#define REF_MT_THREADS (4)
#define REF_MT_ITERS   (20000)

static void *ref_mt_worker(void *arg) {
    mr_ref_t *ref = arg;
    int i;

    for (i = 0; i < REF_MT_ITERS; i++) {
        if (mr_ref_get(ref) == MR_TRUE) {
            mr_ref_put(ref, MR_NULL);
        }
    }
    return MR_NULL;
}

MR_TEST(ref, concurrent_get_put) {
    pthread_t threads[REF_MT_THREADS];
    mr_ref_t r;
    int created;
    int i;
    int ret;

    mr_ref_init(&r);
    for (created = i = 0; i < REF_MT_THREADS; i++) {
        ret = pthread_create(&threads[i], MR_NULL, ref_mt_worker, (void *)&r);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
        if (ret != 0) {
            break;
        }
        created++;
    }
    for (i = 0; i < created; i++) {
        MR_TEST_EXPECT_EQ_INT(pthread_join(threads[i], MR_NULL), 0);
    }
    if (created != REF_MT_THREADS) {
        return;
    }
    MR_TEST_ASSERT_EQ_INT(MR_REF_IS_INITED(r), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(MR_REF_COUNT(r), 1);
    MR_TEST_ASSERT_EQ_INT(mr_ref_put(&r, test_ref_stub_release), MR_TRUE);
}
#endif /* defined(MR_USE_TEST_PTHREAD) */

#endif /* defined(MR_USE_TEST) */
