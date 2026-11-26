/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_MODULE_XBUS)
#include <mrX/kernel/mr_clock.h>
#include <mrX/kernel/mr_work.h>
#include <mrX/libc/mr_string.h>
#include <mrX/modules/xbus/mr_xbus.h>

#if defined(MR_USE_TEST_PTHREAD)
#include <pthread.h>
#endif /* defined(MR_USE_TEST_PTHREAD) */

/* Test assertion helpers matching mr_test.h */
#define TEST_ASSERT_EQ_STR(_a, _b)                                             \
    MR_TEST_ASSERT_EQ_INT(mr_strcmp((_a), (_b)), 0)
#define TEST_ASSERT_TRUE(_cond) MR_TEST_ASSERT_NE_INT(((_cond) ? 1 : 0), 0)

/* Test notify context type */
typedef struct test_notify_ctx {
    mr_uint32_t count;
    mr_xbus_sub_t *last_sub;
} test_notify_ctx_t;

static void test_notify_cb(mr_xbus_sub_t *sub, void *param) {
    test_notify_ctx_t *ctx = (test_notify_ctx_t *)param;

    ctx->count++;
    ctx->last_sub = sub;
}

/* Test iterator context type */
typedef struct test_iter_ctx {
    mr_uint32_t count;
    mr_bool_t found_target;
    const char *target_name;
} test_iter_ctx_t;

static int test_topic_iter_cb(mr_xbus_topic_t *topic, void *param) {
    test_iter_ctx_t *ctx = (test_iter_ctx_t *)param;

    ctx->count++;
    if (ctx->target_name
        && (mr_strcmp(MR_XBUS_TOPIC_NAME(topic), ctx->target_name) == 0)) {
        ctx->found_target = MR_TRUE;
    }
    return 0;
}

MR_TEST(xbus, domain_lifecycle_and_context) {
    mr_xbus_domain_t *host = mr_xbus_domain_default();
    mr_xbus_domain_t d_statik;
    mr_xbus_domain_t *d_dyn, *found;
    mr_uint8_t saved_id;
    mr_err_t ret;

    /* Built-in host domain */
    MR_TEST_ASSERT_NE_PTR(host, MR_NULL);
    MR_TEST_ASSERT_EQ_UINT(MR_XBUS_DOMAIN_ID(host), MR_XBUS_DOMAIN_HOST_ID);
    TEST_ASSERT_EQ_STR(MR_XBUS_DOMAIN_NAME(host), "host");
    MR_TEST_ASSERT_EQ_PTR(mr_xbus_domain_current(), host);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_del(host), -MR_EPERM);

    /* Invalid domain names rejected */
    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_init(&d_statik, ""), -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_init(&d_statik, "."), -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_init(&d_statik, ".."), -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_init(&d_statik, "a/b"), -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_init(&d_statik, "a*"), -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_init(&d_statik, "a?"), -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_init(&d_statik, "a#"), -MR_EINVAL);

    /* Static domain init, duplicate rejection, and find */
    ret = mr_xbus_domain_init(&d_statik, "cpu0");
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_NE_UINT(MR_XBUS_DOMAIN_ID(&d_statik),
                           MR_XBUS_DOMAIN_HOST_ID);
    MR_TEST_ASSERT_EQ_PTR(d_statik.name, d_statik.iname);

    MR_TEST_ASSERT_EQ_PTR(mr_xbus_domain_create("cpu0"), MR_NULL);

    found = mr_xbus_domain_find("cpu0");
    MR_TEST_ASSERT_EQ_PTR(found, &d_statik);
    mr_xbus_domain_put(found);

    /* Long domain name spills beyond inline name storage */
    d_dyn = mr_xbus_domain_create(
        "very_long_virtual_cpu_domain_name_exceeding_inline_len");
    MR_TEST_ASSERT_NE_PTR(d_dyn, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(d_dyn->name, d_dyn->iname);

    /* Delete static domain and verify ID reuse */
    saved_id = MR_XBUS_DOMAIN_ID(&d_statik);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_del(&d_statik), 0);
    MR_TEST_ASSERT_EQ_PTR(mr_xbus_domain_find("cpu0"), MR_NULL);

    /* Freed domain ID is reusable */
    ret = mr_xbus_domain_init(&d_statik, "cpu0_reborn");
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_UINT(MR_XBUS_DOMAIN_ID(&d_statik), saved_id);

    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_del(&d_statik), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_del(d_dyn), 0);
}

MR_TEST(xbus, namespace_relative_and_acl) {
    mr_xbus_domain_t *host = mr_xbus_domain_default();
    mr_xbus_domain_t card0, card1;
    mr_xbus_topic_t t_c0, t_c1, t_host, t_shared;
    mr_xbus_topic_cfg_t cfg = {0};
    mr_xbus_sub_t sub_denied, sub_ok;
    mr_xbus_meta_t meta = {0};
    mr_uint32_t val = 0U, out = 0U;

    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_init(&card0, "card0"), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_init(&card1, "card1"), 0);

    /* Invalid topic paths rejected */
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_init(&t_c0, &card0, "", MR_NULL),
                          -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_init(&t_c0, &card0, "/", MR_NULL),
                          -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_init(&t_c0, &card0, "a/", MR_NULL),
                          -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_init(&t_c0, &card0, "/a//b", MR_NULL),
                          -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_init(&t_c0, &card0, "a/./b", MR_NULL),
                          -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_init(&t_c0, &card0, "a/../b", MR_NULL),
                          -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_init(&t_c0, &card0, "a/*", MR_NULL),
                          -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_init(&t_c0, &card0, "a/#", MR_NULL),
                          -MR_EINVAL);

    /* Relative topic auto-resolves under its owning domain and defaults private
     */
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_init(&t_c0, &card0, "com/status", MR_NULL), 0);
    TEST_ASSERT_EQ_STR(MR_XBUS_TOPIC_NAME(&t_c0), "/card0/com/status");

    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_init(&t_c1, &card1, "com/status", MR_NULL), 0);
    TEST_ASSERT_EQ_STR(MR_XBUS_TOPIC_NAME(&t_c1), "/card1/com/status");

    /* Private isolation: card1 cannot publish, read, or exact-subscribe card0
     */
    val = 0x11U;
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(&card0, &t_c0, &val, sizeof(val)), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(&card1, &t_c0, &val, sizeof(val)),
                          -MR_EPERM);
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_read(&card1, &t_c0, &out, sizeof(out), &meta), -MR_EPERM);
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_init(&sub_denied, &card1, "/card0/com/status", MR_NULL),
        -MR_EPERM);

    /* Grant read to card1 dynamically */
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_acl_grant(&t_c0, &card1, MR_TRUE, MR_FALSE), 0);
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_read(&card1, &t_c0, &out, sizeof(out), &meta),
        (mr_ssize_t)sizeof(out));
    MR_TEST_ASSERT_EQ_UINT(out, 0x11U);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(&card1, &t_c0, &val, sizeof(val)),
                          -MR_EPERM);

    /* Host-exported topic: host writes, all read */
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_init(&t_host, host, "/host/cpu_temp", MR_NULL), 0);
    val = 42U;
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(host, &t_host, &val, sizeof(val)), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(&card0, &t_host, &val, sizeof(val)),
                          -MR_EPERM);
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_read(&card0, &t_host, &out, sizeof(out), &meta),
        (mr_ssize_t)sizeof(out));
    MR_TEST_ASSERT_EQ_UINT(out, 42U);

    /* Shared topic with explicit ACL: card0 writes, card1 reads */
    cfg.max_size = sizeof(mr_uint32_t);
    cfg.flags = MR_XBUS_TOPIC_FIXED;
    cfg.type_id = 0x1234U;
    cfg.write_mask = MR_XBUS_ACL_DOMAIN(&card0);
    cfg.read_mask = MR_XBUS_ACL_DOMAIN(&card1);
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_init(&t_shared, host, "/shared/attitude", &cfg), 0);

    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_init(&sub_ok, &card1, "/shared/attitude", MR_NULL), 0);
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_init(&sub_denied, &card0, "/shared/attitude", MR_NULL),
        -MR_EPERM);

    val = 99U;
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(&card0, &t_shared, &val, sizeof(val)),
                          0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_read(&sub_ok, &out, sizeof(out), &meta),
                          (mr_ssize_t)sizeof(out));
    MR_TEST_ASSERT_EQ_UINT(out, 99U);
    MR_TEST_ASSERT_EQ_UINT(meta.type_id, 0x1234U);
    MR_TEST_ASSERT_EQ_UINT(meta.owner, MR_XBUS_DOMAIN_ID(&card0));

    /* Cleanup */
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_del(&sub_ok), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_del(&t_shared), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_del(&t_host), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_del(&t_c1), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_del(&t_c0), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_del(&card1), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_del(&card0), 0);
}

MR_TEST(xbus, state_pull_retain_late_topic_and_coalesce) {
    mr_xbus_domain_t *host = mr_xbus_domain_default();
    mr_xbus_topic_t topic, sig_topic;
    mr_xbus_sub_t sub_early, sub_late, sub_sig;
    mr_xbus_sub_cfg_t scfg = {0};
    test_notify_ctx_t n_early = {0}, n_late = {0};
    mr_xbus_meta_t meta = {0};
    mr_uint32_t v = 0U, out = 0U;

    /* Subscribe BEFORE topic exists (init-order independence) */
    scfg.mode = MR_XBUS_SUB_STATE;
    scfg.flags = MR_XBUS_SUB_RETAIN;
    scfg.notify = test_notify_cb;
    scfg.param = &n_early;
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_init(&sub_early, host, "/shared/state_demo", &scfg), 0);
    MR_TEST_ASSERT_EQ_UINT(mr_xbus_sub_pending(&sub_early), 0U);
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_read(&sub_early, &out, sizeof(out), &meta), -MR_EAGAIN);

    /* Now create topic and publish 3 times before reader pulls */
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_init(&topic, host, "/shared/state_demo", MR_NULL), 0);
    v = 10U;
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(host, &topic, &v, sizeof(v)), 0);
    v = 20U;
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(host, &topic, &v, sizeof(v)), 0);
    v = 30U;
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(host, &topic, &v, sizeof(v)), 0);

    MR_TEST_ASSERT_EQ_UINT(n_early.count, 3U);
    MR_TEST_ASSERT_EQ_UINT(mr_xbus_sub_pending(&sub_early), 1U);

    /* Peek does not consume */
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_peek(&sub_early, &out, sizeof(out), &meta),
        (mr_ssize_t)sizeof(out));
    MR_TEST_ASSERT_EQ_UINT(out, 30U);
    MR_TEST_ASSERT_EQ_UINT(meta.seq, 3U);
    MR_TEST_ASSERT_EQ_UINT(meta.dropped, 2U);
    TEST_ASSERT_TRUE((meta.flags & MR_XBUS_FLAG_DROPPED) != 0U);
    MR_TEST_ASSERT_EQ_UINT(mr_xbus_sub_pending(&sub_early), 1U);

    /* Pull consumes */
    out = 0U;
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_read(&sub_early, &out, sizeof(out), &meta),
        (mr_ssize_t)sizeof(out));
    MR_TEST_ASSERT_EQ_UINT(out, 30U);
    MR_TEST_ASSERT_EQ_UINT(mr_xbus_sub_pending(&sub_early), 0U);
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_read(&sub_early, &out, sizeof(out), &meta), -MR_EAGAIN);

    /* Late subscriber with RETAIN + NOTIFY_RETAIN sees latest immediately */
    scfg.flags = MR_XBUS_SUB_RETAIN | MR_XBUS_SUB_NOTIFY_RETAIN;
    scfg.notify = test_notify_cb;
    scfg.param = &n_late;
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_init(&sub_late, host, "/shared/state_demo", &scfg), 0);
    MR_TEST_ASSERT_EQ_UINT(n_late.count, 1U);
    MR_TEST_ASSERT_EQ_UINT(mr_xbus_sub_pending(&sub_late), 1U);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_read(&sub_late, &out, sizeof(out), &meta),
                          (mr_ssize_t)sizeof(out));
    MR_TEST_ASSERT_EQ_UINT(out, 30U);
    MR_TEST_ASSERT_EQ_UINT(meta.seq, 3U);

    /* Zero-length pure signal event */
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_init(&sig_topic, host, "/shared/signal", MR_NULL), 0);
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_init(&sub_sig, host, "/shared/signal", MR_NULL), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(host, &sig_topic, MR_NULL, 0U), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_read(&sub_sig, MR_NULL, 0U, &meta), 0);
    MR_TEST_ASSERT_EQ_UINT(meta.len, 0U);
    MR_TEST_ASSERT_EQ_UINT(meta.seq, 1U);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_read(&sub_sig, MR_NULL, 0U, &meta),
                          -MR_EAGAIN);

    /* Clear discards pending state */
    v = 40U;
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(host, &topic, &v, sizeof(v)), 0);
    MR_TEST_ASSERT_EQ_UINT(mr_xbus_sub_pending(&sub_early), 1U);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_clear(&sub_early), 0);
    MR_TEST_ASSERT_EQ_UINT(mr_xbus_sub_pending(&sub_early), 0U);

    /* Cleanup */
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_del(&sub_sig), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_del(&sub_late), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_del(&sub_early), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_del(&sig_topic), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_del(&topic), 0);
}

MR_TEST(xbus, queue_mode_drop_and_overwrite) {
    mr_xbus_domain_t *host = mr_xbus_domain_default();
    mr_xbus_topic_t topic;
    mr_xbus_sub_t sub_drop, sub_ovw;
    mr_xbus_sub_cfg_t cfg = {0};
    mr_xbus_meta_t meta = {0};
    mr_uint32_t i, out;

    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_init(&topic, host, "/shared/events", MR_NULL), 0);

    /* Drop-newest queue (depth = 3) */
    cfg.mode = MR_XBUS_SUB_QUEUE;
    cfg.queue_depth = 3U;
    cfg.flags = 0U;
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_init(&sub_drop, host, "/shared/events", &cfg), 0);

    /* Overwrite-oldest queue (depth = 3) */
    cfg.flags = MR_XBUS_SUB_OVERWRITE;
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_init(&sub_ovw, host, "/shared/events", &cfg), 0);

    /* Publish 5 messages: 1, 2, 3, 4, 5 */
    for (i = 1U; i <= 5U; i++) {
        MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(host, &topic, &i, sizeof(i)), 0);
    }

    /* Drop-newest holds 1, 2, 3 with 2 dropped */
    MR_TEST_ASSERT_EQ_UINT(mr_xbus_sub_pending(&sub_drop), 3U);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_read(&sub_drop, &out, sizeof(out), &meta),
                          (mr_ssize_t)sizeof(out));
    MR_TEST_ASSERT_EQ_UINT(out, 1U);
    MR_TEST_ASSERT_EQ_UINT(meta.dropped, 2U);
    TEST_ASSERT_TRUE((meta.flags & MR_XBUS_FLAG_DROPPED) != 0U);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_read(&sub_drop, &out, sizeof(out), &meta),
                          (mr_ssize_t)sizeof(out));
    MR_TEST_ASSERT_EQ_UINT(out, 2U);
    MR_TEST_ASSERT_EQ_UINT(meta.dropped, 0U);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_read(&sub_drop, &out, sizeof(out), &meta),
                          (mr_ssize_t)sizeof(out));
    MR_TEST_ASSERT_EQ_UINT(out, 3U);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_read(&sub_drop, &out, sizeof(out), &meta),
                          -MR_EAGAIN);

    /* Overwrite-oldest holds 3, 4, 5 with 2 dropped */
    MR_TEST_ASSERT_EQ_UINT(mr_xbus_sub_pending(&sub_ovw), 3U);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_read(&sub_ovw, &out, sizeof(out), &meta),
                          (mr_ssize_t)sizeof(out));
    MR_TEST_ASSERT_EQ_UINT(out, 3U);
    MR_TEST_ASSERT_EQ_UINT(meta.dropped, 2U);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_read(&sub_ovw, &out, sizeof(out), &meta),
                          (mr_ssize_t)sizeof(out));
    MR_TEST_ASSERT_EQ_UINT(out, 4U);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_read(&sub_ovw, &out, sizeof(out), &meta),
                          (mr_ssize_t)sizeof(out));
    MR_TEST_ASSERT_EQ_UINT(out, 5U);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_read(&sub_ovw, &out, sizeof(out), &meta),
                          -MR_EAGAIN);

    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_del(&sub_ovw), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_del(&sub_drop), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_del(&topic), 0);
}

MR_TEST(xbus, dentry_style_inline_and_dynamic_payloads) {
    mr_xbus_domain_t *host = mr_xbus_domain_default();
    mr_xbus_topic_t t_var, t_fixed;
    mr_xbus_topic_cfg_t fcfg = {0};
    mr_xbus_sub_t sub_state, sub_q;
    mr_xbus_sub_cfg_t qcfg = {0};
    mr_xbus_meta_t meta = {0};
    mr_uint8_t small_buf[16];
    mr_uint8_t big_buf[256];
    mr_uint8_t rx_buf[256];
    mr_uint8_t short_buf[10];
    mr_size_t i;

    for (i = 0U; i < sizeof(small_buf); i++) {
        small_buf[i] = (mr_uint8_t)(i + 1U);
    }
    for (i = 0U; i < sizeof(big_buf); i++) {
        big_buf[i] = (mr_uint8_t)(0xa5U ^ i);
    }

    /* Variable-length topic starts with inline buffer */
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_init(&t_var, host, "/shared/var_blob", MR_NULL), 0);
    MR_TEST_ASSERT_EQ_PTR(t_var.buf.data, t_var.buf.idata);

    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_init(&sub_state, host, "/shared/var_blob", MR_NULL), 0);
    qcfg.mode = MR_XBUS_SUB_QUEUE;
    qcfg.queue_depth = 4U;
    qcfg.msg_size = 0U; /* Auto dentry-style inline/dynamic */
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_init(&sub_q, host, "/shared/var_blob", &qcfg), 0);

    /* Small payload stays inline on topic and queue slot */
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_publish(host, &t_var, small_buf, sizeof(small_buf)), 0);
    MR_TEST_ASSERT_EQ_PTR(t_var.buf.data, t_var.buf.idata);

    /* Large payload spills dynamically to heap on both topic and queue slot */
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_publish(host, &t_var, big_buf, sizeof(big_buf)), 0);
    MR_TEST_ASSERT_NE_PTR(t_var.buf.data, t_var.buf.idata);

    /* Truncated read sets MR_XBUS_FLAG_TRUNCATED and reports full meta.len */
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_peek(&sub_state, short_buf, sizeof(short_buf), &meta),
        (mr_ssize_t)sizeof(short_buf));
    MR_TEST_ASSERT_EQ_UINT(meta.len, sizeof(big_buf));
    TEST_ASSERT_TRUE((meta.flags & MR_XBUS_FLAG_TRUNCATED) != 0U);
    MR_TEST_ASSERT_EQ_INT(mr_memcmp(short_buf, big_buf, sizeof(short_buf)), 0);

    /* Full STATE read */
    mr_memset(rx_buf, 0, sizeof(rx_buf));
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_read(&sub_state, rx_buf, sizeof(rx_buf), &meta),
        (mr_ssize_t)sizeof(big_buf));
    MR_TEST_ASSERT_EQ_INT(mr_memcmp(rx_buf, big_buf, sizeof(big_buf)), 0);

    /* QUEUE reads small then large intact */
    mr_memset(rx_buf, 0, sizeof(rx_buf));
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_read(&sub_q, rx_buf, sizeof(rx_buf), &meta),
        (mr_ssize_t)sizeof(small_buf));
    MR_TEST_ASSERT_EQ_INT(mr_memcmp(rx_buf, small_buf, sizeof(small_buf)), 0);

    mr_memset(rx_buf, 0, sizeof(rx_buf));
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_read(&sub_q, rx_buf, sizeof(rx_buf), &meta),
        (mr_ssize_t)sizeof(big_buf));
    MR_TEST_ASSERT_EQ_INT(mr_memcmp(rx_buf, big_buf, sizeof(big_buf)), 0);

    /* Fixed-size topic rejects mismatched lengths */
    fcfg.max_size = 8U;
    fcfg.flags = MR_XBUS_TOPIC_FIXED;
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_init(&t_fixed, host, "/shared/fixed8", &fcfg), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(host, &t_fixed, small_buf, 4U),
                          -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(host, &t_fixed, small_buf, 12U),
                          -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(host, &t_fixed, small_buf, 8U), 0);

    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_del(&sub_q), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_del(&sub_state), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_del(&t_fixed), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_del(&t_var), 0);
}

MR_TEST(xbus, wildcard_subscriptions_and_acl_filtering) {
    mr_xbus_domain_t *host = mr_xbus_domain_default();
    mr_xbus_domain_t card0, card1;
    mr_xbus_topic_t t_c0_com, t_c1_com, t_c0_priv, t_c0_deep;
    mr_xbus_sub_t sub_star, sub_hash, sub_bad;
    mr_xbus_sub_cfg_t cfg = {0};
    mr_xbus_meta_t meta = {0};
    mr_uint32_t v = 0U, out = 0U;
    mr_uint32_t seen_mask = 0U;

    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_init(&card0, "card0"), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_init(&card1, "card1"), 0);

    /* Invalid wildcard patterns rejected */
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_init(&sub_bad, host, "/a/#/b", MR_NULL),
                          -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_init(&sub_bad, host, "/a#", MR_NULL),
                          -MR_EINVAL);

    /* Create topics: card0/com/status (readable by host), card0/secret
     * (private) */
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_init(&t_c0_com, &card0, "com/status", MR_NULL), 0);
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_acl_grant(&t_c0_com, host, MR_TRUE, MR_FALSE), 0);
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_init(&t_c0_priv, &card0, "secret", MR_NULL), 0);
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_init(&t_c0_deep, &card0, "mon/a/b", MR_NULL), 0);
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_acl_grant(&t_c0_deep, host, MR_TRUE, MR_FALSE), 0);

    /* Host subscribes with '*' across all cards' com/status */
    cfg.mode = MR_XBUS_SUB_STATE;
    cfg.flags = MR_XBUS_SUB_RETAIN;
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_init(&sub_star, host, "/*/com/status", &cfg), 0);

    /* Host subscribes with '#' under /card0/#; must NOT match private
     * /card0/secret */
    cfg.mode = MR_XBUS_SUB_QUEUE;
    cfg.queue_depth = 8U;
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_init(&sub_hash, host, "/card0/#", &cfg),
                          0);

    /* Add card1/com/status AFTER wildcard subscription exists */
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_init(&t_c1_com, &card1, "com/status", MR_NULL), 0);
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_acl_grant(&t_c1_com, host, MR_TRUE, MR_FALSE), 0);

    /* Publish to all four topics */
    v = 101U;
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(&card0, &t_c0_com, &v, sizeof(v)), 0);
    v = 202U;
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(&card1, &t_c1_com, &v, sizeof(v)), 0);
    v = 999U;
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(&card0, &t_c0_priv, &v, sizeof(v)),
                          0);
    v = 303U;
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(&card0, &t_c0_deep, &v, sizeof(v)),
                          0);

    /* sub_star (STATE) has 2 dirty topics: card0/com/status and
     * card1/com/status */
    MR_TEST_ASSERT_EQ_UINT(mr_xbus_sub_pending(&sub_star), 2U);
    while (mr_xbus_sub_read(&sub_star, &out, sizeof(out), &meta) >= 0) {
        if (meta.topic == &t_c0_com) {
            MR_TEST_ASSERT_EQ_UINT(out, 101U);
            seen_mask |= 1U;
        } else if (meta.topic == &t_c1_com) {
            MR_TEST_ASSERT_EQ_UINT(out, 202U);
            seen_mask |= 2U;
        }
    }
    MR_TEST_ASSERT_EQ_UINT(seen_mask, 3U);

    /* sub_hash (QUEUE /card0/#) received t_c0_com and t_c0_deep, NEVER
     * t_c0_priv */
    MR_TEST_ASSERT_EQ_UINT(mr_xbus_sub_pending(&sub_hash), 2U);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_read(&sub_hash, &out, sizeof(out), &meta),
                          (mr_ssize_t)sizeof(out));
    MR_TEST_ASSERT_EQ_PTR(meta.topic, &t_c0_com);
    MR_TEST_ASSERT_EQ_UINT(out, 101U);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_read(&sub_hash, &out, sizeof(out), &meta),
                          (mr_ssize_t)sizeof(out));
    MR_TEST_ASSERT_EQ_PTR(meta.topic, &t_c0_deep);
    MR_TEST_ASSERT_EQ_UINT(out, 303U);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_read(&sub_hash, &out, sizeof(out), &meta),
                          -MR_EAGAIN);

    /* Revoking ACL stops future wildcard deliveries */
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_acl_revoke(&t_c0_com, host, MR_TRUE, MR_FALSE), 0);
    v = 111U;
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(&card0, &t_c0_com, &v, sizeof(v)), 0);
    MR_TEST_ASSERT_EQ_UINT(mr_xbus_sub_pending(&sub_star), 0U);

    /* Cleanup */
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_del(&sub_hash), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_del(&sub_star), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_del(&t_c0_deep), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_del(&t_c0_priv), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_del(&t_c1_com), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_del(&t_c0_com), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_del(&card1), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_del(&card0), 0);
}

MR_TEST(xbus, domain_reset_stale_and_introspection) {
    mr_xbus_domain_t *host = mr_xbus_domain_default();
    mr_xbus_domain_t cpu0, cpu1;
    mr_xbus_topic_t t_att;
    mr_xbus_sub_t sub_cpu0, sub_cpu1;
    mr_xbus_sub_cfg_t scfg = {0};
    test_notify_ctx_t n_cpu1 = {0};
    test_iter_ctx_t iter_ctx = {0};
    mr_xbus_meta_t meta = {0};
    mr_uint32_t v = 77U, out = 0U;

    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_init(&cpu0, "cpu0"), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_init(&cpu1, "cpu1"), 0);
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_init(&t_att, host, "/shared/att", MR_NULL), 0);

    scfg.mode = MR_XBUS_SUB_STATE;
    scfg.flags = MR_XBUS_SUB_RETAIN;
    scfg.notify = test_notify_cb;
    scfg.param = &n_cpu1;
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_init(&sub_cpu1, &cpu1, "/shared/att", &scfg), 0);
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_init(&sub_cpu0, &cpu0, "/shared/att", MR_NULL), 0);

    /* cpu0 publishes, cpu1 reads */
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(&cpu0, &t_att, &v, sizeof(v)), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_read(&sub_cpu1, &out, sizeof(out), &meta),
                          (mr_ssize_t)sizeof(out));
    MR_TEST_ASSERT_EQ_UINT(out, 77U);
    MR_TEST_ASSERT_EQ_UINT(meta.flags & MR_XBUS_FLAG_STALE, 0U);

    /* Reset cpu0: cpu0 sub detached, t_att marked STALE and cpu1 notified */
    n_cpu1.count = 0U;
    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_reset(&cpu0), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_read(&sub_cpu0, &out, sizeof(out), &meta),
                          -MR_ECANCELED);
    MR_TEST_ASSERT_EQ_UINT(n_cpu1.count, 1U);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_read(&sub_cpu1, &out, sizeof(out), &meta),
                          (mr_ssize_t)sizeof(out));
    MR_TEST_ASSERT_EQ_UINT(out, 77U);
    TEST_ASSERT_TRUE((meta.flags & MR_XBUS_FLAG_STALE) != 0U);

    /* Fresh publish clears STALE */
    v = 88U;
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(&cpu0, &t_att, &v, sizeof(v)), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_read(&sub_cpu1, &out, sizeof(out), &meta),
                          (mr_ssize_t)sizeof(out));
    MR_TEST_ASSERT_EQ_UINT(out, 88U);
    MR_TEST_ASSERT_EQ_UINT(meta.flags & MR_XBUS_FLAG_STALE, 0U);

    /* Introspection via topic foreach */
    iter_ctx.target_name = "/shared/att";
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_foreach(test_topic_iter_cb, &iter_ctx),
                          0);
    TEST_ASSERT_TRUE(iter_ctx.found_target);

    /* Cleanup */
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_del(&sub_cpu0), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_del(&sub_cpu1), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_del(&t_att), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_del(&cpu1), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_del(&cpu0), 0);
}

/* Re-entrant notify context type */
typedef struct reentrant_ctx {
    mr_xbus_topic_t *out_topic;
    mr_uint32_t value;
    mr_bool_t self_del;
} reentrant_ctx_t;

static void reentrant_notify_cb(mr_xbus_sub_t *sub, void *param) {
    reentrant_ctx_t *ctx = (reentrant_ctx_t *)param;
    mr_uint32_t val = 0U;

    if (mr_xbus_sub_read(sub, &val, sizeof(val), MR_NULL)
        == (mr_ssize_t)sizeof(val)) {
        ctx->value = val;
        if (ctx->out_topic) {
            mr_uint32_t doubled = val * 2U;
            mr_xbus_publish(mr_xbus_domain_default(), ctx->out_topic, &doubled,
                            sizeof(doubled));
        }
    }
    if (ctx->self_del) {
        mr_xbus_sub_del(sub);
    }
}

MR_TEST(xbus, reentrant_notify_and_self_delete) {
    mr_xbus_domain_t *host = mr_xbus_domain_default();
    mr_xbus_topic_t t_in, t_out;
    mr_xbus_sub_t sub_in, sub_out;
    mr_xbus_sub_cfg_t cfg = {0};
    reentrant_ctx_t ctx = {0};
    mr_uint32_t v = 21U, out = 0U;

    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_init(&t_in, host, "/shared/re_in", MR_NULL), 0);
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_init(&t_out, host, "/shared/re_out", MR_NULL), 0);

    ctx.out_topic = &t_out;
    ctx.self_del = MR_TRUE;

    cfg.mode = MR_XBUS_SUB_STATE;
    cfg.flags = MR_XBUS_SUB_RETAIN;
    cfg.notify = reentrant_notify_cb;
    cfg.param = &ctx;
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_init(&sub_in, host, "/shared/re_in", &cfg), 0);
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_init(&sub_out, host, "/shared/re_out", MR_NULL), 0);

    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(host, &t_in, &v, sizeof(v)), 0);
    MR_TEST_ASSERT_EQ_UINT(ctx.value, 21U);

    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_read(&sub_out, &out, sizeof(out), MR_NULL),
        (mr_ssize_t)sizeof(out));
    MR_TEST_ASSERT_EQ_UINT(out, 42U);

    /* sub_in already deleted itself safely; subsequent publish is harmless */
    v = 100U;
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(host, &t_in, &v, sizeof(v)), 0);
    MR_TEST_ASSERT_EQ_UINT(ctx.value, 21U);

    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_del(&sub_out), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_del(&t_out), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_del(&t_in), 0);
}

MR_TEST(xbus, acl_scrub_on_id_reuse_and_deleted_topic_queue) {
    mr_xbus_domain_t *host = mr_xbus_domain_default();
    mr_xbus_domain_t dom_old, dom_new;
    mr_xbus_topic_t t_acl;
    mr_xbus_topic_t *t_dyn;
    mr_xbus_topic_cfg_t tcfg = {0};
    mr_xbus_sub_t sub_q;
    mr_xbus_sub_cfg_t qcfg = {0};
    mr_xbus_meta_t meta = {0};
    mr_uint8_t old_id;
    mr_uint32_t v = 0xdeadbeefU, out = 0U;

    /* 1. Explicit ACL granted to dom_old must be scrubbed on domain_del */
    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_init(&dom_old, "old_dom"), 0);
    old_id = MR_XBUS_DOMAIN_ID(&dom_old);

    tcfg.read_mask = MR_XBUS_ACL_DOMAIN(&dom_old);
    tcfg.write_mask = MR_XBUS_ACL_DOMAIN(&dom_old) | MR_XBUS_ACL_DOMAIN(host);
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_init(&t_acl, host, "/shared/restricted", &tcfg), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(host, &t_acl, &v, sizeof(v)), 0);
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_read(&dom_old, &t_acl, &out, sizeof(out), &meta),
        (mr_ssize_t)sizeof(out));

    /* Delete dom_old and create dom_new reusing the exact same ID */
    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_del(&dom_old), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_init(&dom_new, "new_dom"), 0);
    MR_TEST_ASSERT_EQ_UINT(MR_XBUS_DOMAIN_ID(&dom_new), old_id);

    /* dom_new must NOT inherit dom_old's read or write permissions */
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_read(&dom_new, &t_acl, &out, sizeof(out), &meta),
        -MR_EPERM);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(&dom_new, &t_acl, &v, sizeof(v)),
                          -MR_EPERM);

    /* 2. Queued message from a deleted dynamic topic sets meta.topic = NULL */
    t_dyn = mr_xbus_topic_create(host, "/shared/ephemeral", MR_NULL);
    MR_TEST_ASSERT_NE_PTR(t_dyn, MR_NULL);

    qcfg.mode = MR_XBUS_SUB_QUEUE;
    qcfg.queue_depth = 2U;
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_init(&sub_q, host, "/shared/ephemeral", &qcfg), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(host, t_dyn, &v, sizeof(v)), 0);

    /* Delete dynamic topic before consumer pulls */
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_del(t_dyn), 0);

    out = 0U;
    meta.topic = &t_acl;
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_read(&sub_q, &out, sizeof(out), &meta),
                          (mr_ssize_t)sizeof(out));
    MR_TEST_ASSERT_EQ_UINT(out, 0xdeadbeefU);
    MR_TEST_ASSERT_EQ_PTR(meta.topic, MR_NULL);

    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_del(&sub_q), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_del(&t_acl), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_del(&dom_new), 0);
}

#if defined(MR_USE_WORK)
/* Simulated card type */
typedef struct sim_card {
    mr_workqueue_t wq;
    mr_xbus_domain_t domain;
    mr_xbus_topic_t com_topic;
    mr_xbus_sub_t com_id_sub;
    mr_work_t com_work;
    mr_bool_t com_up;
    mr_xbus_sub_t mdns_com_sub;
    mr_work_t mdns_work;
    mr_uint32_t mdns_up_count;
} sim_card_t;

/* Simulated front LED type */
typedef struct sim_front_led {
    mr_workqueue_t wq;
    mr_xbus_sub_t sub_cards;
    mr_xbus_sub_t sub_temp;
    mr_work_t work;
    mr_uint32_t up_cards;
    mr_uint32_t last_temp;
} sim_front_led_t;

static void sim_schedule_work_notify(mr_xbus_sub_t *sub, void *param) {
    mr_work_t *work = (mr_work_t *)param;

    MR_IGNORE(sub);
    /* Caller decides how to schedule; XBus has zero workqueue dependency */
    mr_work_schedule(work);
}

static void sim_com_work_entry(mr_work_t *work, void *param) {
    sim_card_t *card = (sim_card_t *)param;
    mr_uint32_t id_ready = 0U;
    mr_uint8_t status = 1U;

    MR_IGNORE(work);
    if (mr_xbus_sub_read(&card->com_id_sub, &id_ready, sizeof(id_ready),
                         MR_NULL)
        == (mr_ssize_t)sizeof(id_ready)) {
        if (id_ready != 0U) {
            card->com_up = MR_TRUE;
            /* Relative publish resolves under this card's domain */
            mr_xbus_publish_path(&card->domain, "com/status", &status,
                                 sizeof(status));
        }
    }
}

static void sim_mdns_work_entry(mr_work_t *work, void *param) {
    sim_card_t *card = (sim_card_t *)param;
    mr_uint8_t status = 0U;

    MR_IGNORE(work);
    if (mr_xbus_sub_read(&card->mdns_com_sub, &status, sizeof(status), MR_NULL)
        == (mr_ssize_t)sizeof(status)) {
        if (status == 1U) {
            card->mdns_up_count++;
        }
    }
}

static void sim_led_work_entry(mr_work_t *work, void *param) {
    sim_front_led_t *led = (sim_front_led_t *)param;
    mr_uint8_t st = 0U;
    mr_uint32_t temp = 0U;

    MR_IGNORE(work);
    while (mr_xbus_sub_read(&led->sub_cards, &st, sizeof(st), MR_NULL)
           == (mr_ssize_t)sizeof(st)) {
        if (st == 1U) {
            led->up_cards++;
        }
    }
    if (mr_xbus_sub_read(&led->sub_temp, &temp, sizeof(temp), MR_NULL)
        == (mr_ssize_t)sizeof(temp)) {
        led->last_temp = temp;
    }
}

static void sim_card_execute(sim_card_t *card) {
    mr_workqueue_execute(&card->wq);
}

MR_TEST(xbus, esarch_card_workqueue_scenario) {
    mr_xbus_domain_t *host = mr_xbus_domain_default();
    mr_xbus_topic_t t_temp, t_identify;
    sim_card_t cards[2];
    sim_front_led_t led;
    mr_xbus_sub_cfg_t cfg = {0};
    mr_uint32_t id_ready = 1U, temp = 68U;
    int i;

    mr_memset(cards, 0, sizeof(cards));
    mr_memset(&led, 0, sizeof(led));

    /* Host-level topics */
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_init(&t_temp, host, "/host/cpu_temp", MR_NULL), 0);
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_init(&t_identify, host, "/shared/identify", MR_NULL), 0);

    /* Board-level front_led subscribes to all cards' com/status via wildcard */
    MR_TEST_ASSERT_EQ_INT(mr_workqueue_init(&led.wq), 0);
    MR_TEST_ASSERT_EQ_INT(mr_work_init(&led.work, sim_led_work_entry, &led), 0);
    MR_TEST_ASSERT_EQ_INT(mr_work_bind(&led.work, &led.wq), 0);

    cfg.mode = MR_XBUS_SUB_STATE;
    cfg.flags = MR_XBUS_SUB_RETAIN;
    cfg.notify = sim_schedule_work_notify;
    cfg.param = &led.work;
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_init(&led.sub_cards, host, "/*/com/status", &cfg), 0);
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_init(&led.sub_temp, host, "/host/cpu_temp", &cfg), 0);

    /* Setup two virtual cards using identical relative paths */
    for (i = 0; i < 2; i++) {
        const char *name = (i == 0) ? "cardA" : "cardB";

        MR_TEST_ASSERT_EQ_INT(mr_workqueue_init(&cards[i].wq), 0);
        MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_init(&cards[i].domain, name), 0);
        MR_TEST_ASSERT_EQ_INT(
            mr_work_init(&cards[i].com_work, sim_com_work_entry, &cards[i]), 0);
        MR_TEST_ASSERT_EQ_INT(mr_work_bind(&cards[i].com_work, &cards[i].wq),
                              0);
        MR_TEST_ASSERT_EQ_INT(
            mr_work_init(&cards[i].mdns_work, sim_mdns_work_entry, &cards[i]),
            0);
        MR_TEST_ASSERT_EQ_INT(mr_work_bind(&cards[i].mdns_work, &cards[i].wq),
                              0);

        MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_init(&cards[i].com_topic,
                                                 &cards[i].domain, "com/status",
                                                 MR_NULL),
                              0);
        MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_acl_grant(&cards[i].com_topic, host,
                                                      MR_TRUE, MR_FALSE),
                              0);

        cfg.notify = sim_schedule_work_notify;
        cfg.param = &cards[i].com_work;
        MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_init(&cards[i].com_id_sub,
                                               &cards[i].domain,
                                               "/shared/identify", &cfg),
                              0);

        cfg.param = &cards[i].mdns_work;
        MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_init(&cards[i].mdns_com_sub,
                                               &cards[i].domain, "com/status",
                                               &cfg),
                              0);
    }

    /* Host publishes cpu_temp and identify completion */
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(host, &t_temp, &temp, sizeof(temp)),
                          0);
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_publish(host, &t_identify, &id_ready, sizeof(id_ready)), 0);

    /* Run card workqueues (round 1: com runs & publishes; round 2: mdns runs)
     */
    for (i = 0; i < 2; i++) {
        sim_card_execute(&cards[i]);
        sim_card_execute(&cards[i]);
    }
    mr_workqueue_execute(&led.wq);

    TEST_ASSERT_TRUE(cards[0].com_up);
    TEST_ASSERT_TRUE(cards[1].com_up);
    MR_TEST_ASSERT_EQ_UINT(cards[0].mdns_up_count, 1U);
    MR_TEST_ASSERT_EQ_UINT(cards[1].mdns_up_count, 1U);
    MR_TEST_ASSERT_EQ_UINT(led.up_cards, 2U);
    MR_TEST_ASSERT_EQ_UINT(led.last_temp, 68U);

    /* Cleanup */
    for (i = 0; i < 2; i++) {
        MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_del(&cards[i].mdns_com_sub), 0);
        MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_del(&cards[i].com_id_sub), 0);
        MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_del(&cards[i].com_topic), 0);
        MR_TEST_ASSERT_EQ_INT(mr_work_del(&cards[i].mdns_work), 0);
        MR_TEST_ASSERT_EQ_INT(mr_work_del(&cards[i].com_work), 0);
        MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_del(&cards[i].domain), 0);
        MR_TEST_ASSERT_EQ_INT(mr_workqueue_del(&cards[i].wq), 0);
    }
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_del(&led.sub_temp), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_del(&led.sub_cards), 0);
    MR_TEST_ASSERT_EQ_INT(mr_work_del(&led.work), 0);
    MR_TEST_ASSERT_EQ_INT(mr_workqueue_del(&led.wq), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_del(&t_identify), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_del(&t_temp), 0);
}
#endif /* defined(MR_USE_WORK) */

static void retain_self_delete_notify(mr_xbus_sub_t *sub, void *param) {
    mr_uint32_t *hits = (mr_uint32_t *)param;
    mr_uint32_t val = 0U;

    if (mr_xbus_sub_read(sub, &val, sizeof(val), MR_NULL)
        == (mr_ssize_t)sizeof(val)) {
        (*hits) += val;
    }
    mr_xbus_sub_del(sub);
}

MR_TEST(xbus, stale_domain_ref_denied_after_id_reuse) {
    mr_xbus_domain_t *host = mr_xbus_domain_default();
    mr_xbus_domain_t *dom_a, *dom_b;
    mr_xbus_topic_t t_priv, t_all;
    mr_xbus_topic_cfg_t tcfg = {0};
    mr_uint32_t val = 0x1234U, out = 0U;
    mr_uint8_t reused_id;

    dom_a = mr_xbus_domain_create("stale_a");
    TEST_ASSERT_TRUE(dom_a != MR_NULL);
    reused_id = MR_XBUS_DOMAIN_ID(dom_a);

    /* Hold an extra reference across domain deletion */
    TEST_ASSERT_TRUE(mr_xbus_domain_get(dom_a) == dom_a);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_del(dom_a), 0);

    /* Create a new domain that reuses dom_a's ID */
    dom_b = mr_xbus_domain_create("fresh_b");
    TEST_ASSERT_TRUE(dom_b != MR_NULL);
    MR_TEST_ASSERT_EQ_UINT(MR_XBUS_DOMAIN_ID(dom_b), reused_id);

    /* Create one topic owned by dom_b and one world-accessible topic */
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_init(&t_priv, dom_b, "secret", MR_NULL),
                          0);
    tcfg.read_mask = MR_XBUS_ACL_ALL;
    tcfg.write_mask = MR_XBUS_ACL_ALL;
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_init(&t_all, host, "/shared/open_acl", &tcfg), 0);

    /* dom_b works normally */
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(dom_b, &t_priv, &val, sizeof(val)),
                          0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(host, &t_all, &val, sizeof(val)), 0);

    /* Stale dom_a reference must be rejected on both topics, reset, and ACL */
    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_reset(dom_a), -MR_ENOENT);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(dom_a, &t_priv, &val, sizeof(val)),
                          -MR_ENOENT);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(dom_a, &t_all, &val, sizeof(val)),
                          -MR_ENOENT);
    MR_TEST_ASSERT_EQ_INT(
        (int)mr_xbus_topic_read(dom_a, &t_priv, &out, sizeof(out), MR_NULL),
        -MR_ENOENT);
    MR_TEST_ASSERT_EQ_INT(
        (int)mr_xbus_topic_read(dom_a, &t_all, &out, sizeof(out), MR_NULL),
        -MR_ENOENT);
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_acl_grant(&t_priv, dom_a, MR_TRUE, MR_TRUE), -MR_ENOENT);
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_acl_revoke(&t_priv, dom_a, MR_TRUE, MR_TRUE), -MR_ENOENT);

    /* dom_b can still read its topic */
    MR_TEST_ASSERT_EQ_INT(
        (int)mr_xbus_topic_read(dom_b, &t_priv, &out, sizeof(out), MR_NULL),
        (int)sizeof(out));
    MR_TEST_ASSERT_EQ_UINT(out, val);

    /* Cleanup */
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_del(&t_all), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_del(&t_priv), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_del(dom_b), 0);
    TEST_ASSERT_TRUE(mr_xbus_domain_put(dom_a));
}

MR_TEST(xbus, retain_notify_self_delete_during_create) {
    mr_xbus_domain_t *host = mr_xbus_domain_default();
    mr_xbus_topic_t topic;
    mr_xbus_sub_t static_sub;
    mr_xbus_sub_t *dyn_sub;
    mr_xbus_sub_cfg_t cfg = {0};
    mr_uint32_t val = 7U, hits = 0U;

    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_init(&topic, host, "/shared/retain_self_del", MR_NULL),
        0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(host, &topic, &val, sizeof(val)), 0);

    cfg.mode = MR_XBUS_SUB_STATE;
    cfg.flags = MR_XBUS_SUB_RETAIN | MR_XBUS_SUB_NOTIFY_RETAIN;
    cfg.notify = retain_self_delete_notify;
    cfg.param = &hits;

    /* Dynamic create must return MR_NULL instead of a dangling pointer */
    dyn_sub = mr_xbus_sub_create(host, "/shared/retain_self_del", &cfg);
    TEST_ASSERT_TRUE(dyn_sub == MR_NULL);
    MR_TEST_ASSERT_EQ_UINT(hits, 7U);

    /* Static init must return -MR_ENOENT when detached during retain notify */
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_init(&static_sub, host, "/shared/retain_self_del", &cfg),
        -MR_ENOENT);
    MR_TEST_ASSERT_EQ_UINT(hits, 14U);

    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_del(&topic), 0);
}

MR_TEST(xbus, publish_grow_preserves_var_queue_overwrite) {
    mr_xbus_domain_t *host = mr_xbus_domain_default();
    mr_xbus_topic_t topic;
    mr_xbus_sub_t sub;
    mr_xbus_sub_cfg_t cfg = {0};
    mr_uint8_t small_msg[4] = {1U, 2U, 3U, 4U};
    mr_uint8_t large_msg[48];
    mr_uint8_t rx[48];
    mr_size_t i;

    for (i = 0U; i < sizeof(large_msg); i++) {
        large_msg[i] = (mr_uint8_t)(0x80U + i);
    }

    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_init(&topic, host, "/shared/grow_overwrite", MR_NULL), 0);
    cfg.mode = MR_XBUS_SUB_QUEUE;
    cfg.queue_depth = 2U;
    cfg.msg_size = 0U;
    cfg.flags = MR_XBUS_SUB_OVERWRITE;
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_init(&sub, host, "/shared/grow_overwrite", &cfg), 0);

    /* Fill queue with small messages, then overwrite with larger messages */
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_publish(host, &topic, small_msg, sizeof(small_msg)), 0);
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_publish(host, &topic, small_msg, sizeof(small_msg)), 0);
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_publish(host, &topic, large_msg, sizeof(large_msg)), 0);

    /* First read gets second small message, second read gets large message */
    MR_TEST_ASSERT_EQ_INT(
        (int)mr_xbus_sub_read(&sub, rx, sizeof(rx), MR_NULL),
        (int)sizeof(small_msg));
    MR_TEST_ASSERT_EQ_INT(mr_memcmp(small_msg, rx, sizeof(small_msg)), 0);
    MR_TEST_ASSERT_EQ_INT(
        (int)mr_xbus_sub_read(&sub, rx, sizeof(rx), MR_NULL),
        (int)sizeof(large_msg));
    MR_TEST_ASSERT_EQ_INT(mr_memcmp(large_msg, rx, sizeof(large_msg)), 0);

    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_del(&sub), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_del(&topic), 0);
}

typedef struct del_window_ctx {
    mr_xbus_domain_t *closing_dom;
    mr_xbus_topic_t *open_topic;
    mr_uint8_t closing_id;
    mr_uint32_t invoked;
    mr_bool_t sub_blocked;
    mr_bool_t topic_blocked;
    mr_bool_t pub_blocked;
    mr_bool_t reset_blocked;
    mr_bool_t redel_blocked;
    mr_bool_t id_kept_reserved;
} del_window_ctx_t;

static void del_window_stale_notify(mr_xbus_sub_t *sub, void *param) {
    del_window_ctx_t *ctx = (del_window_ctx_t *)param;
    mr_xbus_domain_t *probe;
    mr_xbus_sub_t *escaped;
    mr_xbus_topic_t tmp_topic;
    mr_uint32_t dummy = 1U;

    MR_IGNORE(sub);
    ctx->invoked++;

    /* Creating sub/topic or publishing on closing domain must be rejected */
    escaped
        = mr_xbus_sub_create(ctx->closing_dom, "/shared/del_window_open",
                             MR_NULL);
    ctx->sub_blocked = (escaped == MR_NULL) ? MR_TRUE : MR_FALSE;
    ctx->topic_blocked
        = (mr_xbus_topic_init(&tmp_topic, ctx->closing_dom, "late_topic",
                              MR_NULL)
           == -MR_ENOENT)
            ? MR_TRUE
            : MR_FALSE;
    ctx->pub_blocked
        = (mr_xbus_publish(ctx->closing_dom, ctx->open_topic, &dummy,
                           sizeof(dummy))
           == -MR_ENOENT)
            ? MR_TRUE
            : MR_FALSE;
    ctx->reset_blocked
        = (mr_xbus_domain_reset(ctx->closing_dom) == -MR_ENOENT) ? MR_TRUE
                                                                 : MR_FALSE;
    ctx->redel_blocked
        = (mr_xbus_domain_del(ctx->closing_dom) == -MR_ENOENT) ? MR_TRUE
                                                               : MR_FALSE;

    /* Closing domain ID must stay reserved until cleanup finishes */
    probe = mr_xbus_domain_create("probe_during_del");
    if (probe != MR_NULL) {
        ctx->id_kept_reserved
            = (MR_XBUS_DOMAIN_ID(probe) != ctx->closing_id) ? MR_TRUE
                                                            : MR_FALSE;
        mr_xbus_domain_del(probe);
    }
}

MR_TEST(xbus, domain_del_blocks_reentrant_subscribe_and_id_reuse) {
    mr_xbus_domain_t *host = mr_xbus_domain_default();
    mr_xbus_domain_t *dom;
    mr_xbus_topic_t t_open;
    mr_xbus_topic_cfg_t tcfg = {0};
    mr_xbus_sub_t host_sub;
    mr_xbus_sub_cfg_t scfg = {0};
    del_window_ctx_t ctx;
    mr_uint32_t val = 0x55U;

    mr_memset(&ctx, 0, sizeof(ctx));

    dom = mr_xbus_domain_create("closing_dom");
    TEST_ASSERT_TRUE(dom != MR_NULL);

    tcfg.read_mask = MR_XBUS_ACL_ALL;
    tcfg.write_mask = MR_XBUS_ACL_ALL;
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_init(&t_open, host, "/shared/del_window_open", &tcfg), 0);

    /* Publish from dom so domain_del triggers stale notify on host_sub */
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(dom, &t_open, &val, sizeof(val)), 0);

    ctx.closing_dom = dom;
    ctx.open_topic = &t_open;
    ctx.closing_id = MR_XBUS_DOMAIN_ID(dom);

    scfg.mode = MR_XBUS_SUB_STATE;
    scfg.flags = 0U;
    scfg.notify = del_window_stale_notify;
    scfg.param = &ctx;
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_init(&host_sub, host, "/shared/del_window_open", &scfg), 0);

    /* Delete dom and verify all reentrant operations in stale callback failed */
    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_del(dom), 0);
    MR_TEST_ASSERT_EQ_UINT(ctx.invoked, 1U);
    TEST_ASSERT_TRUE(ctx.sub_blocked);
    TEST_ASSERT_TRUE(ctx.topic_blocked);
    TEST_ASSERT_TRUE(ctx.pub_blocked);
    TEST_ASSERT_TRUE(ctx.reset_blocked);
    TEST_ASSERT_TRUE(ctx.redel_blocked);
    TEST_ASSERT_TRUE(ctx.id_kept_reserved);

    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_del(&host_sub), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_del(&t_open), 0);
}

static void retain_reset_domain_notify(mr_xbus_sub_t *sub, void *param) {
    mr_xbus_domain_t *dom = (mr_xbus_domain_t *)param;

    MR_IGNORE(sub);
    mr_xbus_domain_reset(dom);
}

MR_TEST(xbus, retain_notify_domain_reset_unregisters_sub) {
    mr_xbus_domain_t *host = mr_xbus_domain_default();
    mr_xbus_domain_t *dom_dyn, *dom_statik;
    mr_xbus_topic_t topic;
    mr_xbus_sub_t static_sub;
    mr_xbus_sub_t *dyn_sub;
    mr_xbus_sub_cfg_t scfg = {0};
    mr_uint32_t val = 99U;

    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_init(&topic, host, "/shared/retain_dom_reset", MR_NULL),
        0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(host, &topic, &val, sizeof(val)), 0);

    /* Dynamic create: retain callback calls domain_reset (not sub_del) */
    dom_dyn = mr_xbus_domain_create("reset_dyn");
    TEST_ASSERT_TRUE(dom_dyn != MR_NULL);
    TEST_ASSERT_TRUE(mr_xbus_domain_get(dom_dyn) == dom_dyn);

    scfg.mode = MR_XBUS_SUB_STATE;
    scfg.flags = MR_XBUS_SUB_RETAIN | MR_XBUS_SUB_NOTIFY_RETAIN;
    scfg.notify = retain_reset_domain_notify;
    scfg.param = dom_dyn;
    dyn_sub = mr_xbus_sub_create(dom_dyn, "/shared/retain_dom_reset", &scfg);
    TEST_ASSERT_TRUE(dyn_sub == MR_NULL);

    /* Domain must release cleanly (proves dyn_sub released its domain ref) */
    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_del(dom_dyn), 0);
    TEST_ASSERT_TRUE(mr_xbus_domain_put(dom_dyn));

    /* Static init: retain callback calls domain_reset (not sub_del) */
    dom_statik = mr_xbus_domain_create("reset_statik");
    TEST_ASSERT_TRUE(dom_statik != MR_NULL);
    TEST_ASSERT_TRUE(mr_xbus_domain_get(dom_statik) == dom_statik);

    scfg.param = dom_statik;
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_init(&static_sub, dom_statik, "/shared/retain_dom_reset",
                         &scfg),
        -MR_ENOENT);

    /* Zero static_sub storage and publish again to ensure no dangling list node */
    mr_memset(&static_sub, 0, sizeof(static_sub));
    MR_TEST_ASSERT_EQ_INT(mr_xbus_publish(host, &topic, &val, sizeof(val)), 0);

    /* Domain must release cleanly (proves static_sub released its domain ref) */
    MR_TEST_ASSERT_EQ_INT(mr_xbus_domain_del(dom_statik), 0);
    TEST_ASSERT_TRUE(mr_xbus_domain_put(dom_statik));

    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_del(&topic), 0);
}

#if defined(MR_USE_TEST_PTHREAD)
/* Multithread stress payload type */
typedef struct mt_payload {
    mr_uint32_t seq;
    mr_uint32_t magic;
    mr_uint32_t inv_seq;
} mt_payload_t;

typedef struct mt_ctx {
    mr_xbus_domain_t *domain;
    mr_xbus_topic_t *topic;
    mr_xbus_sub_t *sub_state;
    mr_xbus_sub_t *sub_queue;
    mr_atomic_t done;
    mr_atomic_t errors;
    mr_uint32_t total_pubs;
} mt_ctx_t;

static void *mt_pub_thread(void *arg) {
    mt_ctx_t *ctx = (mt_ctx_t *)arg;
    mt_payload_t msg;
    mr_uint32_t i;

    for (i = 1U; i <= ctx->total_pubs; i++) {
        msg.seq = i;
        msg.magic = 0x5a5aa5a5U;
        msg.inv_seq = ~i;
        if (mr_xbus_publish(ctx->domain, ctx->topic, &msg, sizeof(msg)) != 0) {
            mr_atomic_fetch_add(&ctx->errors, 1);
        }
    }
    mr_atomic_store(&ctx->done, 1);
    return MR_NULL;
}

static void *mt_sub_thread(void *arg) {
    mt_ctx_t *ctx = (mt_ctx_t *)arg;
    mt_payload_t msg;
    mr_uint32_t last_state = 0U, last_q = 0U;
    mr_ssize_t n;

    while ((mr_atomic_load(&ctx->done) == 0)
           || (mr_xbus_sub_pending(ctx->sub_state) > 0U)
           || (mr_xbus_sub_pending(ctx->sub_queue) > 0U)) {
        n = mr_xbus_sub_read(ctx->sub_state, &msg, sizeof(msg), MR_NULL);
        if (n == (mr_ssize_t)sizeof(msg)) {
            if ((msg.magic != 0x5a5aa5a5U) || (msg.inv_seq != ~msg.seq)
                || (msg.seq < last_state)) {
                mr_atomic_fetch_add(&ctx->errors, 1);
            }
            last_state = msg.seq;
        }
        n = mr_xbus_sub_read(ctx->sub_queue, &msg, sizeof(msg), MR_NULL);
        if (n == (mr_ssize_t)sizeof(msg)) {
            if ((msg.magic != 0x5a5aa5a5U) || (msg.inv_seq != ~msg.seq)
                || (msg.seq <= last_q)) {
                mr_atomic_fetch_add(&ctx->errors, 1);
            }
            last_q = msg.seq;
        }
    }
    return MR_NULL;
}

MR_TEST(xbus, concurrent_publish_and_read) {
    mr_xbus_domain_t *host = mr_xbus_domain_default();
    mr_xbus_topic_t topic;
    mr_xbus_sub_t sub_state, sub_queue;
    mr_xbus_sub_cfg_t qcfg = {0};
    mt_ctx_t ctx;
    pthread_t t_pub, t_sub;

    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_topic_init(&topic, host, "/shared/mt_stress", MR_NULL), 0);
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_init(&sub_state, host, "/shared/mt_stress", MR_NULL), 0);
    qcfg.mode = MR_XBUS_SUB_QUEUE;
    qcfg.queue_depth = 32U;
    qcfg.flags = MR_XBUS_SUB_OVERWRITE;
    MR_TEST_ASSERT_EQ_INT(
        mr_xbus_sub_init(&sub_queue, host, "/shared/mt_stress", &qcfg), 0);

    ctx.domain = host;
    ctx.topic = &topic;
    ctx.sub_state = &sub_state;
    ctx.sub_queue = &sub_queue;
    mr_atomic_init(&ctx.done, 0);
    mr_atomic_init(&ctx.errors, 0);
    ctx.total_pubs = 2000U;

    MR_TEST_ASSERT_EQ_INT(pthread_create(&t_sub, MR_NULL, mt_sub_thread, &ctx),
                          0);
    MR_TEST_ASSERT_EQ_INT(pthread_create(&t_pub, MR_NULL, mt_pub_thread, &ctx),
                          0);
    pthread_join(t_pub, MR_NULL);
    pthread_join(t_sub, MR_NULL);

    MR_TEST_ASSERT_EQ_INT(mr_atomic_load(&ctx.errors), 0);

    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_del(&sub_queue), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_sub_del(&sub_state), 0);
    MR_TEST_ASSERT_EQ_INT(mr_xbus_topic_del(&topic), 0);
}
#endif /* defined(MR_USE_TEST_PTHREAD) */
#endif /* defined(MR_USE_TEST) && defined(MR_USE_MODULE_XBUS) */
