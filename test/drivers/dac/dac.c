/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_DRIVER_DAC)
#include <mrX/drivers/dac/mr_dac.h>
#include <mrX/libc/mr_malloc.h>

static int dac_configure_count;
static int dac_configuration_count;
static int dac_write_count;
static int dac_stream_start_count;
static int dac_stream_stop_count;
static mr_uint32_t dac_last_channel;
static mr_dac_config_t dac_last_config;
static mr_dac_seq_t dac_last_seq;
#if defined(MR_USE_DRIVER_DAC_STREAM)
static mr_dac_stream_config_t dac_last_stream_config;
#endif /* defined(MR_USE_DRIVER_DAC_STREAM) */
static mr_bool_t dac_busy;
static mr_size_t dac_offset;
static mr_err_t dac_configure_ret;
static mr_err_t dac_configuration_ret;
static mr_ssize_t dac_write_ret;
static mr_err_t dac_stream_start_ret;
static mr_err_t dac_stream_stop_ret;
static mr_err_t dac_stream_busy_ret;
static mr_err_t dac_stream_offset_ret;

static void dac_test_reset(void) {
    dac_configure_count = 0;
    dac_configuration_count = 0;
    dac_write_count = 0;
    dac_stream_start_count = 0;
    dac_stream_stop_count = 0;
    dac_last_channel = 0;
    dac_last_config = (mr_dac_config_t){0};
    dac_last_seq = (mr_dac_seq_t){0};
#if defined(MR_USE_DRIVER_DAC_STREAM)
    dac_last_stream_config = (mr_dac_stream_config_t){0};
#endif /* defined(MR_USE_DRIVER_DAC_STREAM) */
    dac_busy = MR_FALSE;
    dac_offset = 0;
    dac_configure_ret = 0;
    dac_configuration_ret = 0;
    dac_write_ret = 0;
    dac_stream_start_ret = 0;
    dac_stream_stop_ret = 0;
    dac_stream_busy_ret = 0;
    dac_stream_offset_ret = 0;
}

static mr_err_t dac_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_dac_config_t *config) {
    MR_IGNORE(drv);
    dac_configure_count++;
    dac_last_channel = channel;
    dac_last_config = *config;
    return dac_configure_ret;
}

static mr_err_t dac_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_dac_config_t *config) {
    MR_IGNORE(drv);
    dac_configuration_count++;
    dac_last_channel = channel;
    if (dac_configuration_ret == 0) {
        *config = dac_last_config;
    }
    return dac_configuration_ret;
}

static mr_ssize_t dac_write(const mr_driver_t *drv, mr_dac_seq_t *seq) {
    MR_IGNORE(drv);
    dac_write_count++;
    dac_last_seq = *seq;
    return dac_write_ret;
}

#if defined(MR_USE_DRIVER_DAC_STREAM)
static mr_err_t dac_stream_start(const mr_driver_t *drv, mr_dac_seq_t *seq,
                                 const mr_dac_stream_config_t *config) {
    MR_IGNORE(drv);
    dac_stream_start_count++;
    dac_last_seq = *seq;
    dac_last_stream_config = *config;
    return dac_stream_start_ret;
}

static mr_err_t dac_stream_stop(const mr_driver_t *drv) {
    MR_IGNORE(drv);
    dac_stream_stop_count++;
    return dac_stream_stop_ret;
}

static mr_err_t dac_stream_is_busy(const mr_driver_t *drv, mr_bool_t *busy) {
    MR_IGNORE(drv);
    *busy = dac_busy;
    return dac_stream_busy_ret;
}

static mr_err_t dac_stream_offset(const mr_driver_t *drv, mr_size_t *offset) {
    MR_IGNORE(drv);
    *offset = dac_offset;
    return dac_stream_offset_ret;
}
#endif /* defined(MR_USE_DRIVER_DAC_STREAM) */

static const mr_dac_ops_t dac_ops = {
    .channel_configure = dac_channel_configure,
    .channel_configuration = dac_channel_configuration,
    .write = dac_write,
#if defined(MR_USE_DRIVER_DAC_STREAM)
    .stream_start = dac_stream_start,
    .stream_stop = dac_stream_stop,
    .stream_is_busy = dac_stream_is_busy,
    .stream_offset = dac_stream_offset,
#endif
};

static mr_driver_t *dac_create(mr_uint32_t exist_mask) {
    mr_dac_data_t *data = mr_malloc(sizeof(*data));

    if (!data) {
        return MR_NULL;
    }
    data->ref_internal = 3300;
    data->exist_mask = exist_mask;
    return mr_dac_create(data, MR_NULL, (mr_dac_ops_t *)&dac_ops);
}

MR_TEST(dac, configuration_and_reference) {
    mr_driver_t *drv;
    mr_dac_config_t config = {.ref = MR_DAC_REF_VDD_1};
    mr_dac_config_t saved = {0};

    dac_test_reset();
    drv = dac_create((1U << 0) | (1U << 3) | (1U << 31));
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_ASSERT_EQ_UINT(mr_dac_ref_internal(drv), 3300);
    MR_TEST_ASSERT_EQ_INT(mr_dac_configure(drv, 1, &config), -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_dac_configure(drv, 32, &config), -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(dac_configure_count, 0);
    MR_TEST_ASSERT_EQ_INT(mr_dac_configure(drv, 31, &config), 0);
    MR_TEST_ASSERT_EQ_INT(dac_last_channel, 31);
    MR_TEST_ASSERT_EQ_INT(dac_last_config.ref, MR_DAC_REF_VDD_1);
    MR_TEST_ASSERT_EQ_INT(mr_dac_configuration(drv, 3, &saved), 0);
    MR_TEST_ASSERT_EQ_INT(dac_configuration_count, 1);
    MR_TEST_ASSERT_EQ_INT(saved.ref, MR_DAC_REF_VDD_1);
    dac_configuration_ret = -MR_EIO;
    MR_TEST_ASSERT_EQ_INT(mr_dac_configuration(drv, 3, &saved), -MR_EIO);
    mr_dac_del(drv);
}

#if defined(MR_USE_DRIVER_DAC_STREAM)
MR_TEST(dac, write_and_stream) {
    mr_driver_t *drv;
    mr_bool_t busy;
    mr_uint16_t samples[] = {1, 2, 3, 4};
    mr_dac_seq_t seq = {
        .channels = (1U << 0) | (1U << 3),
        .resolution = 12,
        .buf = samples,
        .size = sizeof(samples),
    };
    mr_dac_stream_config_t config = {
        .source = MR_DAC_STREAM_SOURCE_EXTERNAL,
        .circular = MR_TRUE,
    };
    mr_size_t offset;

    dac_test_reset();
    drv = dac_create(0xffffffffU);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    dac_write_ret = sizeof(samples);
    MR_TEST_ASSERT_EQ_INT(mr_dac_write(drv, &seq), sizeof(samples));
    MR_TEST_ASSERT_EQ_INT(dac_write_count, 1);
    MR_TEST_ASSERT_EQ_UINT(dac_last_seq.channels, seq.channels);
    MR_TEST_ASSERT_EQ_INT(mr_dac_stream_start(drv, &seq, &config), 0);
    MR_TEST_ASSERT_EQ_INT(dac_stream_start_count, 1);
    MR_TEST_ASSERT_EQ_INT(dac_last_stream_config.source,
                          MR_DAC_STREAM_SOURCE_EXTERNAL);
    MR_TEST_ASSERT_EQ_INT(dac_last_stream_config.circular, MR_TRUE);
    dac_busy = MR_TRUE;
    MR_TEST_ASSERT_EQ_INT(mr_dac_stream_is_busy(drv, &busy), 0);
    MR_TEST_ASSERT_EQ_INT(busy, MR_TRUE);
    dac_offset = 6;
    MR_TEST_ASSERT_EQ_INT(mr_dac_stream_offset(drv, &offset), 0);
    MR_TEST_ASSERT_EQ_UINT(offset, 6);
    MR_TEST_ASSERT_EQ_INT(mr_dac_stream_stop(drv), 0);
    MR_TEST_ASSERT_EQ_INT(dac_stream_stop_count, 1);
    mr_dac_del(drv);
}

MR_TEST(dac, stream_errors_and_conversions) {
    mr_driver_t *drv;
    mr_bool_t busy;
    mr_dac_ops_t empty_ops = dac_ops;
    mr_dac_seq_t seq = {.resolution = 8, .buf = MR_NULL, .size = 0};
    mr_dac_stream_config_t config = {
        .source = MR_DAC_STREAM_SOURCE_SOFTWARE,
        .circular = MR_FALSE,
    };
    mr_size_t offset = 99;

    dac_test_reset();
    empty_ops.stream_stop = MR_NULL;
    empty_ops.stream_offset = MR_NULL;
    empty_ops.stream_is_busy = MR_NULL;
    drv = dac_create(1U);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    drv->ops = (mr_ptr_t)&empty_ops;
    MR_TEST_ASSERT_EQ_INT(mr_dac_stream_stop(drv), -MR_ENOSYS);
    MR_TEST_ASSERT_EQ_INT(mr_dac_stream_offset(drv, &offset), -MR_ENOSYS);
    MR_TEST_ASSERT_EQ_INT(mr_dac_stream_is_busy(drv, &busy), -MR_ENOSYS);
    drv->ops = (mr_ptr_t)&dac_ops;
    dac_stream_busy_ret = -MR_EIO;
    MR_TEST_ASSERT_EQ_INT(mr_dac_stream_is_busy(drv, &busy), -MR_EIO);
    dac_stream_offset_ret = -MR_EIO;
    MR_TEST_ASSERT_EQ_INT(mr_dac_stream_offset(drv, &offset), -MR_EIO);
    dac_stream_start_ret = -MR_EIO;
    MR_TEST_ASSERT_EQ_INT(mr_dac_stream_start(drv, &seq, &config), -MR_EIO);
    MR_TEST_ASSERT_EQ_INT(mr_dac_millivolt2raw(3300, 12, 1650), 2047);
    MR_TEST_ASSERT_EQ_INT(mr_dac_microvolt2raw(3300, 12, 1650000), 2047);
    MR_TEST_ASSERT_EQ_INT(mr_dac_volt2raw(3300, 12, 3.3f), 4095);
    mr_dac_del(drv);
}
#endif /* defined(MR_USE_DRIVER_DAC_STREAM) */

static void dac_waiter_entry(mr_waiter_t *waiter, void *param,
                             mr_wait_event_t event, const void *data) {
    int *count = (int *)param;

    MR_IGNORE(waiter);
    MR_IGNORE(event);
    MR_IGNORE(data);
    (*count)++;
}

MR_TEST(dac, init_static_and_reference) {
    mr_driver_t drv;
    mr_dac_data_t *data;

    dac_test_reset();
    data = mr_malloc(sizeof(*data));
    MR_TEST_ASSERT_NE_PTR(data, MR_NULL);
    *data = (mr_dac_data_t)MR_DAC_DATA_INIT(3000, 1U);

    /* A statically initialized driver shares the core lifecycle */
    MR_TEST_ASSERT_EQ_INT(
        mr_dac_init(&drv, data, MR_NULL, (mr_dac_ops_t *)&dac_ops), 0);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(&drv), 0);
    MR_TEST_ASSERT_EQ_INT(mr_driver_is_ready(&drv), MR_TRUE);
    MR_TEST_ASSERT_EQ_UINT(mr_dac_ref_internal(&drv), 3000U);

    /* A held reference delays the release until the final put */
    MR_TEST_ASSERT_EQ_PTR(mr_dac_get(&drv), &drv);
    MR_TEST_ASSERT_EQ_INT(mr_dac_del(&drv), 0);
    MR_TEST_ASSERT_EQ_INT(mr_driver_is_ready(&drv), MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(mr_dac_put(&drv), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_driver_is_ready(&drv), MR_FALSE);

    /* A statically initialized driver does not own the data, the module
       release hook only runs for dynamically created drivers */
    mr_free(data);
}

MR_TEST(dac, event_attach_and_raise) {
    mr_driver_t *drv;
    mr_waiter_t waiter;
    int event_count = 0;

    dac_test_reset();
    drv = dac_create(1U);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_ASSERT_EQ_INT(mr_waiter_init(&waiter, dac_waiter_entry,
                                         &event_count, MR_DAC_WAIT_STREAM_DONE),
                          0);

    /* Raising without any waiter is not an error */
    MR_TEST_ASSERT_EQ_INT(mr_dac_event_raise(drv, MR_DAC_WAIT_STREAM_HALF), 0);
    MR_TEST_ASSERT_EQ_INT(event_count, 0);

    /* A raise that does not match the waiter mask never wakes it */
    MR_TEST_ASSERT_EQ_INT(mr_dac_event_attach(drv, &waiter), 0);
    MR_TEST_ASSERT_EQ_INT(mr_dac_event_raise(drv, MR_DAC_WAIT_STREAM_HALF), 0);
    MR_TEST_ASSERT_EQ_INT(event_count, 0);

    /* The matching event wakes the waiter exactly once */
    MR_TEST_ASSERT_EQ_INT(mr_dac_event_raise(drv, MR_DAC_WAIT_STREAM_DONE), 0);
    MR_TEST_ASSERT_EQ_INT(event_count, 1);
    MR_TEST_ASSERT_EQ_INT(mr_waiter_del(&waiter), 0);
    mr_dac_del(drv);
}

#endif
