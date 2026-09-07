/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_DRIVER_ADC)
#include <mrX/drivers/adc/mr_adc.h>
#include <mrX/libc/mr_malloc.h>

static int adc_configure_count;
static int adc_configuration_count;
static int adc_read_count;
static int adc_stream_start_count;
static int adc_stream_stop_count;
static mr_uint32_t adc_last_channel;
static mr_adc_config_t adc_last_config;
static mr_adc_seq_t adc_last_seq;
static mr_bool_t adc_last_circular;
static mr_bool_t adc_busy;
static mr_size_t adc_offset;
static mr_err_t adc_configure_ret;
static mr_err_t adc_configuration_ret;
static mr_err_t adc_read_ret;
static mr_err_t adc_stream_start_ret;
static mr_err_t adc_stream_stop_ret;
static mr_err_t adc_stream_busy_ret;
static mr_err_t adc_stream_offset_ret;

static void adc_test_reset(void) {
    adc_configure_count = 0;
    adc_configuration_count = 0;
    adc_read_count = 0;
    adc_stream_start_count = 0;
    adc_stream_stop_count = 0;
    adc_last_channel = 0;
    adc_last_config = (mr_adc_config_t){0};
    adc_last_seq = (mr_adc_seq_t){0};
    adc_last_circular = MR_FALSE;
    adc_busy = MR_FALSE;
    adc_offset = 0;
    adc_configure_ret = 0;
    adc_configuration_ret = 0;
    adc_read_ret = 0;
    adc_stream_start_ret = 0;
    adc_stream_stop_ret = 0;
    adc_stream_busy_ret = 0;
    adc_stream_offset_ret = 0;
}

static mr_err_t adc_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_adc_config_t *config) {
    MR_IGNORE(drv);
    adc_configure_count++;
    adc_last_channel = channel;
    adc_last_config = *config;
    return adc_configure_ret;
}

static mr_err_t adc_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_adc_config_t *config) {
    MR_IGNORE(drv);
    adc_configuration_count++;
    adc_last_channel = channel;
    if (adc_configuration_ret == 0) {
        *config = adc_last_config;
    }
    return adc_configuration_ret;
}

static mr_ssize_t adc_read(const mr_driver_t *drv, mr_adc_seq_t *seq) {
    MR_IGNORE(drv);
    adc_read_count++;
    adc_last_seq = *seq;
    return adc_read_ret;
}

static mr_err_t adc_stream_start(const mr_driver_t *drv, mr_adc_seq_t *seq,
                                 mr_bool_t circular) {
    MR_IGNORE(drv);
    adc_stream_start_count++;
    adc_last_seq = *seq;
    adc_last_circular = circular;
    return adc_stream_start_ret;
}

static mr_err_t adc_stream_stop(const mr_driver_t *drv) {
    MR_IGNORE(drv);
    adc_stream_stop_count++;
    return adc_stream_stop_ret;
}

static mr_err_t adc_stream_is_busy(const mr_driver_t *drv, mr_bool_t *busy) {
    MR_IGNORE(drv);
    *busy = adc_busy;
    return adc_stream_busy_ret;
}

static mr_err_t adc_stream_get_offset(const mr_driver_t *drv,
                                      mr_size_t *offset) {
    MR_IGNORE(drv);
    *offset = adc_offset;
    return adc_stream_offset_ret;
}

static const mr_adc_ops_t adc_ops = {
    .channel_configure = adc_channel_configure,
    .channel_configuration = adc_channel_configuration,
    .read = adc_read,
#if defined(MR_USE_DRIVER_ADC_STREAM)
    .stream_start = adc_stream_start,
    .stream_stop = adc_stream_stop,
    .stream_is_busy = adc_stream_is_busy,
    .stream_offset = adc_stream_get_offset,
#endif
};

static mr_driver_t *adc_create(mr_uint32_t exist_mask) {
    mr_adc_data_t *data = mr_malloc(sizeof(*data));

    if (data == MR_NULL) {
        return MR_NULL;
    }
    data->ref_internal = 1200;
    data->exist_mask = exist_mask;
    return mr_adc_create(data, MR_NULL, (mr_adc_ops_t *)&adc_ops);
}

static void adc_waiter_entry(mr_waiter_t *waiter, void *param,
                             mr_wait_event_t event, const void *data) {
    int *count = (int *)param;

    MR_IGNORE(waiter);
    MR_IGNORE(event);
    MR_IGNORE(data);
    (*count)++;
}

MR_TEST(adc, lifecycle_and_reference) {
    mr_driver_t *drv;

    adc_test_reset();
    drv = adc_create(0xffffffffU);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_ASSERT_EQ_INT(mr_driver_is_ready(drv), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_adc_ref_internal(drv), 1200);
    MR_TEST_ASSERT_EQ_INT(mr_adc_del(drv), 0);
}

MR_TEST(adc, channel_configuration_paths) {
    mr_driver_t *drv;
    mr_adc_config_t config = {
        .diff = MR_ADC_DIFF_DIFF,
        .ref = MR_ADC_REF_EXTERNAL_1,
        .gain = MR_ADC_GAIN_16,
    };
    mr_adc_config_t saved = {0};

    adc_test_reset();
    drv = adc_create((1U << 0) | (1U << 31));
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);

    MR_TEST_ASSERT_EQ_INT(mr_adc_configure(drv, 1, &config), -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_adc_configure(drv, 32, &config), -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(adc_configure_count, 0);
    adc_configure_ret = -MR_EIO;
    MR_TEST_ASSERT_EQ_INT(mr_adc_configure(drv, 0, &config), -MR_EIO);
    MR_TEST_ASSERT_EQ_INT(adc_last_channel, 0);
    adc_configure_ret = 0;
    MR_TEST_ASSERT_EQ_INT(mr_adc_configure(drv, 31, &config), 0);
    MR_TEST_ASSERT_EQ_INT(adc_last_channel, 31);
    MR_TEST_ASSERT_EQ_INT(adc_last_config.gain, MR_ADC_GAIN_16);

    MR_TEST_ASSERT_EQ_INT(mr_adc_configuration(drv, 31, &saved), 0);
    MR_TEST_ASSERT_EQ_INT(adc_configuration_count, 1);
    MR_TEST_ASSERT_EQ_INT(saved.ref, MR_ADC_REF_EXTERNAL_1);
    adc_configuration_ret = -MR_EIO;
    MR_TEST_ASSERT_EQ_INT(mr_adc_configuration(drv, 31, &saved), -MR_EIO);
    MR_TEST_ASSERT_EQ_INT(mr_adc_configuration(drv, 30, &saved), -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_adc_del(drv), 0);
}

MR_TEST(adc, read_and_conversion) {
    mr_driver_t *drv;
    mr_uint16_t samples[2] = {100, 200};
    mr_adc_seq_t seq = {
        .channels = 3,
        .resolution = 12,
        .oversample = 2,
        .buf = samples,
        .size = sizeof(samples),
    };

    adc_test_reset();
    drv = adc_create(3U);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    adc_read_ret = sizeof(samples);
    MR_TEST_ASSERT_EQ_INT(mr_adc_read(drv, &seq), sizeof(samples));
    MR_TEST_ASSERT_EQ_INT(adc_read_count, 1);
    MR_TEST_ASSERT_EQ_UINT(adc_last_seq.channels, 3U);
    MR_TEST_ASSERT_EQ_INT(adc_last_seq.resolution, 12);
    adc_read_ret = -MR_EIO;
    MR_TEST_ASSERT_EQ_INT(mr_adc_read(drv, &seq), -MR_EIO);

    MR_TEST_ASSERT_EQ_INT(mr_adc_raw2millivolt(3300, MR_ADC_GAIN_1, 12, 2048),
                          1650);
    MR_TEST_ASSERT_EQ_INT(mr_adc_raw2millivolt(3300, MR_ADC_GAIN_2, 12, 2048),
                          825);
    MR_TEST_ASSERT_EQ_INT(mr_adc_raw2microvolt(3300, MR_ADC_GAIN_1_2, 12, 1),
                          1611);
    MR_TEST_ASSERT_EQ_INT(
        mr_adc_raw2volt(3300, MR_ADC_GAIN_1, 12, 4095) > 3.29f, MR_TRUE);
    mr_adc_del(drv);
}

#if defined(MR_USE_DRIVER_ADC_STREAM)
MR_TEST(adc, stream_paths) {
    mr_driver_t *drv;
    mr_bool_t busy;
    mr_uint16_t buffer[4] = {0};
    mr_adc_seq_t seq = {
        .channels = 1,
        .resolution = 16,
        .buf = buffer,
        .size = sizeof(buffer),
    };
    mr_size_t offset;

    adc_test_reset();
    drv = adc_create(1U);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    adc_busy = MR_TRUE;
    MR_TEST_ASSERT_EQ_INT(mr_adc_stream_is_busy(drv, &busy), 0);
    MR_TEST_ASSERT_EQ_INT(busy, MR_TRUE);
    adc_busy = MR_FALSE;
    adc_stream_busy_ret = -MR_EIO;
    MR_TEST_ASSERT_EQ_INT(mr_adc_stream_is_busy(drv, &busy), -MR_EIO);
    adc_stream_busy_ret = 0;
    MR_TEST_ASSERT_EQ_INT(mr_adc_stream_start(drv, &seq, MR_TRUE), 0);
    MR_TEST_ASSERT_EQ_INT(adc_stream_start_count, 1);
    MR_TEST_ASSERT_EQ_INT(adc_last_circular, MR_TRUE);
    adc_offset = sizeof(buffer) / 2;
    MR_TEST_ASSERT_EQ_INT(mr_adc_stream_offset(drv, &offset), 0);
    MR_TEST_ASSERT_EQ_INT(offset, sizeof(buffer) / 2);
    adc_stream_offset_ret = -MR_EIO;
    MR_TEST_ASSERT_EQ_INT(mr_adc_stream_offset(drv, &offset), -MR_EIO);
    adc_stream_stop_ret = -MR_EIO;
    MR_TEST_ASSERT_EQ_INT(mr_adc_stream_stop(drv), -MR_EIO);
    adc_stream_stop_ret = 0;
    MR_TEST_ASSERT_EQ_INT(mr_adc_stream_stop(drv), 0);
    MR_TEST_ASSERT_EQ_INT(adc_stream_stop_count, 2);
    mr_adc_del(drv);
}

MR_TEST(adc, stream_optional_operations) {
    mr_driver_t *drv;
    mr_bool_t busy;
    mr_adc_ops_t empty_ops = adc_ops;
    mr_uint16_t buffer[2] = {0};
    mr_adc_seq_t seq = {
        .channels = 1,
        .resolution = 8,
        .buf = buffer,
        .size = sizeof(buffer),
    };
    mr_size_t offset = 99;

    adc_test_reset();
    empty_ops.stream_stop = MR_NULL;
    empty_ops.stream_is_busy = MR_NULL;
    empty_ops.stream_offset = MR_NULL;
    drv = adc_create(1U);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    drv->ops = (mr_ptr_t)&empty_ops;
    MR_TEST_ASSERT_EQ_INT(mr_adc_stream_stop(drv), -MR_ENOSYS);
    MR_TEST_ASSERT_EQ_INT(mr_adc_stream_is_busy(drv, &busy), -MR_ENOSYS);
    MR_TEST_ASSERT_EQ_INT(mr_adc_stream_offset(drv, &offset), -MR_ENOSYS);
    MR_TEST_ASSERT_EQ_UINT(offset, 99U);

    drv->ops = (mr_ptr_t)&adc_ops;
    adc_stream_start_ret = -MR_EIO;
    MR_TEST_ASSERT_EQ_INT(mr_adc_stream_start(drv, &seq, MR_FALSE), -MR_EIO);
    MR_TEST_ASSERT_EQ_INT(adc_stream_start_count, 1);
    mr_adc_del(drv);
}

MR_TEST(adc, event_paths) {
    mr_driver_t *drv;
    mr_waiter_t waiter;
    int event_count = 0;

    adc_test_reset();
    drv = adc_create(1U);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_ASSERT_EQ_INT(mr_waiter_init(&waiter, adc_waiter_entry,
                                         &event_count, MR_ADC_WAIT_STREAM_HALF),
                          0);
    MR_TEST_ASSERT_EQ_INT(mr_adc_event_attach(drv, &waiter), 0);
    MR_TEST_ASSERT_EQ_INT(mr_adc_event_raise(drv, MR_ADC_WAIT_STREAM_HALF), 0);
    MR_TEST_ASSERT_EQ_INT(event_count, 1);
    MR_TEST_ASSERT_EQ_INT(mr_waiter_del(&waiter), 0);
    mr_adc_del(drv);
}

#endif

MR_TEST(adc, init_static_and_reference) {
    mr_driver_t drv;
    mr_adc_data_t *data;

    adc_test_reset();
    data = mr_malloc(sizeof(*data));
    MR_TEST_ASSERT_NE_PTR(data, MR_NULL);
    *data = (mr_adc_data_t)MR_ADC_DATA_INIT(2500, 1U);

    /* A statically initialized driver shares the core lifecycle */
    MR_TEST_ASSERT_EQ_INT(
        mr_adc_init(&drv, data, MR_NULL, (mr_adc_ops_t *)&adc_ops), 0);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(&drv), 0);
    MR_TEST_ASSERT_EQ_INT(mr_driver_is_ready(&drv), MR_TRUE);
    MR_TEST_ASSERT_EQ_UINT(mr_adc_ref_internal(&drv), 2500U);

    /* A held reference delays the release until the final put */
    MR_TEST_ASSERT_EQ_PTR(mr_adc_get(&drv), &drv);
    MR_TEST_ASSERT_EQ_INT(mr_adc_del(&drv), 0);
    MR_TEST_ASSERT_EQ_INT(mr_driver_is_ready(&drv), MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(mr_adc_put(&drv), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_driver_is_ready(&drv), MR_FALSE);

    /* A statically initialized driver does not own the data, the module
       release hook only runs for dynamically created drivers */
    mr_free(data);
}

#endif
