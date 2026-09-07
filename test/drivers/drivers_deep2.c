/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#if defined(MR_USE_DRIVER)
#include <mrX/drivers/mr_driver.h>
#include <mrX/libc/mr_atomic.h>
#include <mrX/libc/mr_malloc.h>
#include <mrX/libc/mr_string.h>
#endif /* defined(MR_USE_DRIVER) */
#if defined(MR_USE_DRIVER_ADC)
#include <mrX/drivers/adc/mr_adc.h>
#include <mrX/libc/mr_malloc.h>
#endif /* defined(MR_USE_DRIVER_ADC) */
#if defined(MR_USE_DRIVER_DAC)
#include <mrX/drivers/dac/mr_dac.h>
#include <mrX/libc/mr_malloc.h>
#endif /* defined(MR_USE_DRIVER_DAC) */
#if defined(MR_USE_DRIVER_GPIO)
#include <mrX/drivers/gpio/mr_gpio.h>
#include <mrX/libc/mr_malloc.h>
#endif /* defined(MR_USE_DRIVER_GPIO) */
#if defined(MR_USE_DRIVER_RAM)
#include <mrX/drivers/ram/mr_ram.h>
#include <mrX/libc/mr_malloc.h>
#include <mrX/libc/mr_string.h>
#endif /* defined(MR_USE_DRIVER_RAM) */

#if defined(MR_USE_DRIVER)

/* Driver mock state */
static int dd2_driver_init_count;
static int dd2_driver_deinit_count;
static int dd2_driver_release_count;

static void dd2_driver_reset(void) {
    dd2_driver_init_count = 0;
    dd2_driver_deinit_count = 0;
    dd2_driver_release_count = 0;
}

static mr_err_t dd2_driver_init(const mr_driver_t *drv) {
    MR_IGNORE(drv);
    dd2_driver_init_count++;
    return 0;
}

static mr_err_t dd2_driver_deinit(const mr_driver_t *drv) {
    MR_IGNORE(drv);
    dd2_driver_deinit_count++;
    return 0;
}

static void dd2_driver_release(const mr_driver_t *drv) {
    MR_IGNORE(drv);
    dd2_driver_release_count++;
}

static const mr_driver_clazz_t dd2_driver_clazz = MR_DRIVER_CLAZZ_INIT(
    "dd2_driver", dd2_driver_init, dd2_driver_deinit, dd2_driver_release);

MR_TEST(driver, d2_reference_held_across_setup_shutdown_cycle) {
    mr_driver_t *drv = MR_NULL;
    mr_driver_t *held = MR_NULL;

    dd2_driver_reset();

    drv = mr_driver_create(&dd2_driver_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_EXPECT_EQ_INT(dd2_driver_init_count, 1);

    /* Hold a reference while the driver is shut down and set up again */
    held = mr_driver_get(drv);
    MR_TEST_ASSERT_EQ_PTR(held, drv);

    /* Shutdown succeeds even with an open reference and deinitializes */
    MR_TEST_EXPECT_EQ_INT(mr_driver_shutdown(drv), 0);
    MR_TEST_EXPECT_EQ_INT(dd2_driver_deinit_count, 1);
    MR_TEST_EXPECT_EQ_INT(dd2_driver_release_count, 0);
    MR_TEST_EXPECT_EQ_INT(mr_driver_is_ready(drv), MR_FALSE);

    /* A new setup reinitializes the driver while the reference is held */
    MR_TEST_EXPECT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_EXPECT_EQ_INT(dd2_driver_init_count, 2);
    MR_TEST_EXPECT_EQ_INT(mr_driver_is_ready(drv), MR_TRUE);

    /* Delete marks the driver dying, the final put releases it */
    MR_TEST_EXPECT_EQ_INT(mr_driver_del(drv), 0);
    MR_TEST_EXPECT_EQ_INT(dd2_driver_release_count, 0);
    MR_TEST_EXPECT_EQ_INT(mr_driver_put(held), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(dd2_driver_deinit_count, 2);
    MR_TEST_EXPECT_EQ_INT(dd2_driver_release_count, 1);
}

MR_TEST(driver, d2_double_shutdown_is_idempotent) {
    mr_driver_t *drv = MR_NULL;

    dd2_driver_reset();

    drv = mr_driver_create(&dd2_driver_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);

    /* The first shutdown deinitializes, the second is a no-op */
    MR_TEST_EXPECT_EQ_INT(mr_driver_shutdown(drv), 0);
    MR_TEST_EXPECT_EQ_INT(dd2_driver_deinit_count, 1);
    MR_TEST_EXPECT_EQ_INT(mr_driver_shutdown(drv), 0);
    MR_TEST_EXPECT_EQ_INT(dd2_driver_deinit_count, 1);
    MR_TEST_EXPECT_EQ_INT(mr_driver_is_ready(drv), MR_FALSE);

    /* Deleting a shut down driver must not deinitialize a second time */
    MR_TEST_EXPECT_EQ_INT(mr_driver_del(drv), 0);
    MR_TEST_EXPECT_EQ_INT(dd2_driver_deinit_count, 1);
}

MR_TEST(driver, d2_find_name_edges) {
    mr_driver_t *drv = MR_NULL;
    mr_driver_t *same = MR_NULL;

    dd2_driver_reset();

    /* The exact installed name resolves and keeps the static reference */
    drv = mr_driver_find("dd_deep_find");
    MR_TEST_EXPECT_NE_PTR(drv, MR_NULL);
    same = mr_driver_find("dd_deep_find");
    MR_TEST_EXPECT_EQ_PTR(same, drv);

    /* A partial name must not match the installed driver */
    MR_TEST_EXPECT_EQ_PTR(mr_driver_find("dd_deep"), MR_NULL);

    /* A dotted numeric string is an install level, never a driver name */
    MR_TEST_EXPECT_EQ_PTR(mr_driver_find("3.7"), MR_NULL);

    /* A name longer than the installed one must not prefix match */
    MR_TEST_EXPECT_EQ_PTR(mr_driver_find("dd_deep_find_extra"), MR_NULL);

    MR_TEST_EXPECT_EQ_INT(mr_driver_put(same), MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(mr_driver_put(drv), MR_FALSE);
}

MR_TEST(driver, d2_create_type_variants) {
    mr_driver_t *ram = MR_NULL;
    mr_driver_t *uart = MR_NULL;
    mr_driver_t *i2c = MR_NULL;
    mr_driver_t *dma = MR_NULL;

    dd2_driver_reset();

    /* Any major type except NONE is accepted at create time */
    ram = mr_driver_create(&dd2_driver_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(ram, MR_NULL);
    uart = mr_driver_create(&dd2_driver_clazz, MR_DRIVER_TYPE_UART, MR_NULL,
                            MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(uart, MR_NULL);
    i2c = mr_driver_create(&dd2_driver_clazz, MR_DRIVER_TYPE_I2C, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(i2c, MR_NULL);
    dma = mr_driver_create(&dd2_driver_clazz, MR_DRIVER_TYPE_DMA, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(dma, MR_NULL);

    /* The type accessors report the stored major type unchanged */
    MR_TEST_EXPECT_EQ_INT((int)mr_driver_type(ram), MR_DRIVER_TYPE_RAM);
    MR_TEST_EXPECT_EQ_INT((int)mr_driver_type(uart), MR_DRIVER_TYPE_UART);
    MR_TEST_EXPECT_EQ_INT(MR_DRIVER_IS_TYPE(i2c, MR_DRIVER_TYPE_I2C), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(MR_DRIVER_IS_TYPE(dma, MR_DRIVER_TYPE_GPIO), MR_FALSE);

    /* Drivers of every type set up and shut down through the same core */
    MR_TEST_EXPECT_EQ_INT(mr_driver_setup(uart), 0);
    MR_TEST_EXPECT_EQ_INT(dd2_driver_init_count, 1);
    MR_TEST_EXPECT_EQ_INT(mr_driver_shutdown(uart), 0);

    mr_driver_del(ram);
    mr_driver_del(uart);
    mr_driver_del(i2c);
    mr_driver_del(dma);
    MR_TEST_EXPECT_EQ_INT(dd2_driver_release_count, 4);
}

#endif /* defined(MR_USE_DRIVER) */

#if defined(MR_USE_DRIVER_ADC)

/* ADC mock state */
static int dd2_adc_read_count;
static int dd2_adc_stream_start_count;
static int dd2_adc_stream_stop_count;
static mr_adc_seq_t dd2_adc_last_seq;
static mr_bool_t dd2_adc_last_circular;
static mr_bool_t dd2_adc_busy;
static mr_ssize_t dd2_adc_read_ret;
static mr_err_t dd2_adc_stream_start_ret;

static void dd2_adc_reset(void) {
    dd2_adc_read_count = 0;
    dd2_adc_stream_start_count = 0;
    dd2_adc_stream_stop_count = 0;
    dd2_adc_last_seq = (mr_adc_seq_t){0};
    dd2_adc_last_circular = MR_FALSE;
    dd2_adc_busy = MR_FALSE;
    dd2_adc_read_ret = 0;
    dd2_adc_stream_start_ret = 0;
}

static mr_ssize_t dd2_adc_read(const mr_driver_t *drv, mr_adc_seq_t *seq) {
    MR_IGNORE(drv);
    dd2_adc_read_count++;
    dd2_adc_last_seq = *seq;
    return dd2_adc_read_ret;
}

#if defined(MR_USE_DRIVER_ADC_STREAM)
static mr_err_t dd2_adc_stream_start(const mr_driver_t *drv, mr_adc_seq_t *seq,
                                     mr_bool_t circular) {
    MR_IGNORE(drv);
    dd2_adc_stream_start_count++;
    dd2_adc_last_seq = *seq;
    dd2_adc_last_circular = circular;
    return dd2_adc_stream_start_ret;
}

static mr_err_t dd2_adc_stream_stop(const mr_driver_t *drv) {
    MR_IGNORE(drv);
    dd2_adc_stream_stop_count++;
    return 0;
}

static mr_err_t dd2_adc_stream_is_busy(const mr_driver_t *drv,
                                       mr_bool_t *busy) {
    MR_IGNORE(drv);
    *busy = dd2_adc_busy;
    return 0;
}
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) */

static const mr_adc_ops_t dd2_adc_ops = {
    .channel_configure = MR_NULL,
    .read = dd2_adc_read,
#if defined(MR_USE_DRIVER_ADC_STREAM)
    .stream_start = dd2_adc_stream_start,
    .stream_stop = dd2_adc_stream_stop,
    .stream_is_busy = dd2_adc_stream_is_busy,
    .stream_offset = MR_NULL,
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) */
};

static mr_driver_t *dd2_adc_create(void) {
    mr_adc_data_t *data = mr_malloc(sizeof(*data));

    if (data == MR_NULL) {
        return MR_NULL;
    }
    data->ref_internal = 2500;
    data->exist_mask = 0xffffffffU;
    return mr_adc_create(data, MR_NULL, (mr_adc_ops_t *)&dd2_adc_ops);
}

static void dd2_adc_waiter_entry(mr_waiter_t *waiter, void *param,
                                 mr_wait_event_t event, const void *data) {
    MR_IGNORE(waiter);
    MR_IGNORE(event);
    MR_IGNORE(data);
    (*(int *)param)++;
}

MR_TEST(adc, d2_read_exact_and_oversize_buffer) {
    mr_driver_t *drv = MR_NULL;
    mr_uint16_t small[1] = {0};
    mr_uint16_t large[8] = {0};
    mr_adc_seq_t exact = {
        .channels = 1,
        .resolution = 16,
        .buf = small,
        .size = sizeof(small),
    };
    mr_adc_seq_t oversize = {
        .channels = 1,
        .resolution = 16,
        .buf = large,
        .size = sizeof(large),
    };

    dd2_adc_reset();

    drv = dd2_adc_create();
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);

    /* The core dispatches the buffer size untouched, exact or oversize */
    dd2_adc_read_ret = (mr_ssize_t)sizeof(small);
    MR_TEST_EXPECT_EQ_INT(mr_adc_read(drv, &exact), (int)sizeof(small));
    MR_TEST_EXPECT_EQ_UINT(dd2_adc_last_seq.size, sizeof(small));
    MR_TEST_EXPECT_EQ_PTR(dd2_adc_last_seq.buf, small);
    dd2_adc_read_ret = (mr_ssize_t)sizeof(small);
    MR_TEST_EXPECT_EQ_INT(mr_adc_read(drv, &oversize), (int)sizeof(small));
    MR_TEST_EXPECT_EQ_UINT(dd2_adc_last_seq.size, sizeof(large));

    /* A short read from the backend propagates unchanged */
    dd2_adc_read_ret = 1;
    MR_TEST_EXPECT_EQ_INT(mr_adc_read(drv, &exact), 1);

    mr_adc_del(drv);
}

#if defined(MR_USE_DRIVER_ADC_STREAM)
MR_TEST(adc, d2_stream_start_zero_size_and_all_channels) {
    mr_driver_t *drv = MR_NULL;
    mr_bool_t busy;
    mr_adc_seq_t zero = {
        .channels = 0U,
        .resolution = 12,
        .buf = MR_NULL,
        .size = 0,
    };
    mr_adc_seq_t all = {
        .channels = 0xffffffffU,
        .resolution = 12,
        .buf = MR_NULL,
        .size = 0,
    };

    dd2_adc_reset();

    drv = dd2_adc_create();
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);

    /* A zero channel zero size stream reaches the backend untouched */
    MR_TEST_EXPECT_EQ_INT(mr_adc_stream_start(drv, &zero, MR_FALSE), 0);
    MR_TEST_EXPECT_EQ_INT(dd2_adc_stream_start_count, 1);
    MR_TEST_EXPECT_EQ_UINT(dd2_adc_last_seq.channels, 0U);
    MR_TEST_EXPECT_EQ_INT(dd2_adc_last_circular, MR_FALSE);

    /* An all channels mask passes through without exist mask filtering */
    MR_TEST_EXPECT_EQ_INT(mr_adc_stream_start(drv, &all, MR_TRUE), 0);
    MR_TEST_EXPECT_EQ_UINT(dd2_adc_last_seq.channels, 0xffffffffU);
    MR_TEST_EXPECT_EQ_INT(dd2_adc_last_circular, MR_TRUE);

    /* Stop after start, then a second stop, both succeed */
    MR_TEST_EXPECT_EQ_INT(mr_adc_stream_stop(drv), 0);
    MR_TEST_EXPECT_EQ_INT(dd2_adc_stream_stop_count, 1);
    MR_TEST_EXPECT_EQ_INT(mr_adc_stream_stop(drv), 0);
    MR_TEST_EXPECT_EQ_INT(dd2_adc_stream_stop_count, 2);
    MR_TEST_EXPECT_EQ_INT(mr_adc_stream_is_busy(drv, &busy), 0);
    MR_TEST_EXPECT_EQ_INT(busy, MR_FALSE);

    mr_adc_del(drv);
}
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) */

MR_TEST(adc, d2_event_wrong_type_waiter_never_fires) {
    mr_driver_t *drv = MR_NULL;
    mr_waiter_t waiter;
    int count = 0;

    dd2_adc_reset();

    drv = dd2_adc_create();
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);

    /* The waiter only listens to the full event */
    MR_TEST_ASSERT_EQ_INT(mr_waiter_init(&waiter, dd2_adc_waiter_entry, &count,
                                         MR_ADC_WAIT_STREAM_FULL),
                          0);
    MR_TEST_ASSERT_EQ_INT(mr_adc_event_attach(drv, &waiter), 0);

    /* A half full raise must not wake the full waiter */
    MR_TEST_EXPECT_EQ_INT(mr_adc_event_raise(drv, MR_ADC_WAIT_STREAM_HALF), 0);
    MR_TEST_EXPECT_EQ_INT(count, 0);

    /* The matching event wakes it exactly once */
    MR_TEST_EXPECT_EQ_INT(mr_adc_event_raise(drv, MR_ADC_WAIT_STREAM_FULL), 0);
    MR_TEST_EXPECT_EQ_INT(count, 1);

    /* A combined raise still wakes it only once */
    MR_TEST_EXPECT_EQ_INT(
        mr_adc_event_raise(drv, MR_ADC_WAIT_STREAM_HALF
                                    | MR_ADC_WAIT_STREAM_FULL), 0);
    MR_TEST_EXPECT_EQ_INT(count, 2);

    mr_waiter_del(&waiter);
    mr_adc_del(drv);
}

MR_TEST(adc, d2_conversion_resolution_bounds) {
    dd2_adc_reset();

    /* A one bit resolution divides the reference into two codes */
    MR_TEST_EXPECT_EQ_INT(mr_adc_raw2millivolt(3300, MR_ADC_GAIN_1, 1, 0), 0);
    MR_TEST_EXPECT_EQ_INT(mr_adc_raw2millivolt(3300, MR_ADC_GAIN_1, 1, 1),
                          1650);

    /* A 32 bit resolution with the maximum raw code stays in range */
    MR_TEST_EXPECT_EQ_INT(
        mr_adc_raw2millivolt(3300, MR_ADC_GAIN_1, 32, 4294967295LL), 3299);
    MR_TEST_EXPECT_EQ_INT(mr_adc_raw2millivolt(3300, MR_ADC_GAIN_1, 32, 0), 0);

    /* Negative raw values pass through with the gain applied, the
       integer division truncates toward zero so small negatives yield 0
       while a full scale negative code maps to the negative reference */
    MR_TEST_EXPECT_EQ_INT(mr_adc_raw2millivolt(3300, MR_ADC_GAIN_2, 12, -2),
                          0);
    MR_TEST_EXPECT_EQ_INT(
        mr_adc_raw2millivolt(3300, MR_ADC_GAIN_2, 12, -4096), -1650);
}

#endif /* defined(MR_USE_DRIVER_ADC) */

#if defined(MR_USE_DRIVER_DAC)

/* DAC mock state */
static int dd2_dac_write_count;
static int dd2_dac_stream_start_count;
static mr_dac_seq_t dd2_dac_last_seq;
static mr_ssize_t dd2_dac_write_ret;

static void dd2_dac_reset(void) {
    dd2_dac_write_count = 0;
    dd2_dac_stream_start_count = 0;
    dd2_dac_last_seq = (mr_dac_seq_t){0};
    dd2_dac_write_ret = 0;
}

static mr_ssize_t dd2_dac_write(const mr_driver_t *drv, mr_dac_seq_t *seq) {
    MR_IGNORE(drv);
    dd2_dac_write_count++;
    dd2_dac_last_seq = *seq;
    return dd2_dac_write_ret;
}

#if defined(MR_USE_DRIVER_DAC_STREAM)
static mr_err_t dd2_dac_stream_start(const mr_driver_t *drv, mr_dac_seq_t *seq,
                                     mr_bool_t circular) {
    MR_IGNORE(drv);
    MR_IGNORE(circular);
    dd2_dac_stream_start_count++;
    dd2_dac_last_seq = *seq;
    return 0;
}
#endif /* defined(MR_USE_DRIVER_DAC_STREAM) */

static const mr_dac_ops_t dd2_dac_ops = {
    .write = dd2_dac_write,
#if defined(MR_USE_DRIVER_DAC_STREAM)
    .stream_start = dd2_dac_stream_start,
    .stream_stop = MR_NULL,
    .stream_is_busy = MR_NULL,
    .stream_offset = MR_NULL,
#endif /* defined(MR_USE_DRIVER_DAC_STREAM) */
};

static mr_driver_t *dd2_dac_create(void) {
    mr_dac_data_t *data = mr_malloc(sizeof(*data));

    if (data == MR_NULL) {
        return MR_NULL;
    }
    data->ref_internal = 3300;
    data->exist_mask = 0x00000003U;
    return mr_dac_create(data, MR_NULL, (mr_dac_ops_t *)&dd2_dac_ops);
}

MR_TEST(dac, d2_write_all_channels_and_sizes) {
    mr_driver_t *drv = MR_NULL;
    mr_uint16_t samples[2] = {0x0aa, 0x155};
    mr_dac_seq_t all = {
        .channels = 0xffffffffU,
        .resolution = 12,
        .buf = samples,
        .size = sizeof(samples),
    };
    mr_dac_seq_t exact = {
        .channels = (1U << 0) | (1U << 1),
        .resolution = 12,
        .buf = samples,
        .size = sizeof(samples),
    };

    dd2_dac_reset();

    drv = dd2_dac_create();
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);

    /* An all channels write is dispatched without exist mask filtering */
    dd2_dac_write_ret = (mr_ssize_t)sizeof(samples);
    MR_TEST_EXPECT_EQ_INT(mr_dac_write(drv, &all), (int)sizeof(samples));
    MR_TEST_EXPECT_EQ_UINT(dd2_dac_last_seq.channels, 0xffffffffU);

    /* A write touching only existing channels is passed unchanged too */
    MR_TEST_EXPECT_EQ_INT(mr_dac_write(drv, &exact), (int)sizeof(samples));
    MR_TEST_EXPECT_EQ_UINT(dd2_dac_last_seq.channels, 0x3U);
    MR_TEST_EXPECT_EQ_UINT(dd2_dac_last_seq.size, sizeof(samples));

    mr_dac_del(drv);
}

#if defined(MR_USE_DRIVER_DAC_STREAM)
MR_TEST(dac, d2_stream_zero_size_and_optional_edges) {
    mr_driver_t *drv = MR_NULL;
    mr_bool_t busy;
    mr_dac_seq_t zero = {
        .channels = 0U,
        .resolution = 8,
        .buf = MR_NULL,
        .size = 0,
    };
    mr_size_t offset = 77;

    dd2_dac_reset();

    drv = dd2_dac_create();
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);

    /* A zero size stream start reaches the backend */
    MR_TEST_EXPECT_EQ_INT(mr_dac_stream_start(drv, &zero, MR_TRUE), 0);
    MR_TEST_EXPECT_EQ_INT(dd2_dac_stream_start_count, 1);
    MR_TEST_EXPECT_EQ_UINT(dd2_dac_last_seq.size, 0U);
    MR_TEST_EXPECT_EQ_PTR(dd2_dac_last_seq.buf, MR_NULL);

    /* Unimplemented optional stream operations fail gracefully */
    MR_TEST_EXPECT_EQ_INT(mr_dac_stream_stop(drv), -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_INT(mr_dac_stream_is_busy(drv, &busy), -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_INT(mr_dac_stream_offset(drv, &offset), -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_UINT(offset, 77U);

    mr_dac_del(drv);
}
#endif /* defined(MR_USE_DRIVER_DAC_STREAM) */

MR_TEST(dac, d2_conversion_no_clamp_beyond_full_scale) {
    dd2_dac_reset();

    /* The maximum in range value maps to the maximum raw code */
    MR_TEST_EXPECT_EQ_INT(mr_dac_millivolt2raw(3300, 12, 3300), 4095);

    /* Potential bug: one millivolt beyond full scale is not clamped to the
     * maximum raw code and overflows into the next code */
    MR_TEST_EXPECT_EQ_INT(mr_dac_millivolt2raw(3300, 12, 3301), 4096);

    /* Potential bug: a large overrange input keeps growing unclamped */
    MR_TEST_EXPECT_EQ_INT(mr_dac_millivolt2raw(3300, 12, 6600), 8190);

    /* A one bit resolution maps only the exact full scale value to one */
    MR_TEST_EXPECT_EQ_INT(mr_dac_millivolt2raw(3300, 1, 3300), 1);
    MR_TEST_EXPECT_EQ_INT(mr_dac_millivolt2raw(3300, 1, 1649), 0);
}

#endif /* defined(MR_USE_DRIVER_DAC) */

#if defined(MR_USE_DRIVER_GPIO)

/* GPIO mock state */
static int dd2_gpio_configure_count;
static int dd2_gpio_write_count;
static int dd2_gpio_event_attach_count;
static int dd2_gpio_waiter_count;
static mr_uint32_t dd2_gpio_last_pin;
static mr_uint8_t dd2_gpio_last_value;
static mr_uint32_t dd2_gpio_last_port_mask;
static mr_gpio_config_t dd2_gpio_last_config;
static mr_uint32_t dd2_gpio_port_read_ret_mask;

static void dd2_gpio_reset(void) {
    dd2_gpio_configure_count = 0;
    dd2_gpio_write_count = 0;
    dd2_gpio_event_attach_count = 0;
    dd2_gpio_waiter_count = 0;
    dd2_gpio_last_pin = 0;
    dd2_gpio_last_value = 0;
    dd2_gpio_last_port_mask = 0;
    dd2_gpio_last_config = (mr_gpio_config_t){0};
    dd2_gpio_port_read_ret_mask = 0;
}

static mr_err_t dd2_gpio_pin_configure(const mr_driver_t *drv, mr_uint32_t pin,
                                       const mr_gpio_config_t *config) {
    MR_IGNORE(drv);
    dd2_gpio_configure_count++;
    dd2_gpio_last_pin = pin;
    dd2_gpio_last_config = *config;
    return 0;
}

static mr_err_t dd2_gpio_pin_write(const mr_driver_t *drv, mr_uint32_t pin,
                                   mr_uint8_t value) {
    MR_IGNORE(drv);
    dd2_gpio_write_count++;
    dd2_gpio_last_pin = pin;
    dd2_gpio_last_value = value;
    return 0;
}

static mr_err_t dd2_gpio_pin_read(const mr_driver_t *drv, mr_uint32_t pin,
                                  mr_uint8_t *value) {
    MR_IGNORE(drv);
    MR_IGNORE(pin);
    *value = dd2_gpio_last_value;
    return 0;
}

static mr_err_t dd2_gpio_port_write(const mr_driver_t *drv, mr_uint32_t mask) {
    MR_IGNORE(drv);
    dd2_gpio_last_port_mask = mask;
    return 0;
}

static mr_err_t dd2_gpio_port_read(const mr_driver_t *drv, mr_uint32_t *mask) {
    MR_IGNORE(drv);
    *mask = dd2_gpio_port_read_ret_mask;
    return 0;
}

static mr_err_t dd2_gpio_event_attach(const mr_driver_t *drv, mr_uint32_t pin,
                                      mr_waiter_t *entry) {
    MR_IGNORE(drv);
    MR_IGNORE(entry);
    dd2_gpio_event_attach_count++;
    dd2_gpio_last_pin = pin;
    return 0;
}

static void dd2_gpio_waiter_entry(mr_waiter_t *waiter, void *param,
                                  mr_wait_event_t event, const void *data) {
    MR_IGNORE(waiter);
    MR_IGNORE(param);
    MR_IGNORE(event);
    MR_IGNORE(data);
}

static const mr_gpio_ops_t dd2_gpio_ops = {
    .pin_configure = dd2_gpio_pin_configure,
    .pin_write = dd2_gpio_pin_write,
    .pin_read = dd2_gpio_pin_read,
    .port_write = dd2_gpio_port_write,
    .port_read = dd2_gpio_port_read,
    .event_attach = dd2_gpio_event_attach,
};

static mr_driver_t *dd2_gpio_create(mr_uint32_t exist, mr_uint32_t invert) {
    mr_gpio_data_t *data = mr_malloc(sizeof(*data));

    if (data == MR_NULL) {
        return MR_NULL;
    }
    data->exist_mask = exist;
    data->invert_mask = invert;
    return mr_gpio_create(data, MR_NULL, (mr_gpio_ops_t *)&dd2_gpio_ops);
}

MR_TEST(gpio, d2_pin_number_boundaries) {
    mr_driver_t *drv = MR_NULL;
    mr_gpio_config_t config = {.mode = MR_GPIO_MODE_INPUT};
    mr_uint8_t value = 0;
    mr_waiter_t waiter;

    dd2_gpio_reset();

    /* Only the lowest and the highest pin exist */
    drv = dd2_gpio_create((1U << 0) | (1U << 31), 0U);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_ASSERT_EQ_INT(
        mr_waiter_init(&waiter, dd2_gpio_waiter_entry, MR_NULL, MR_WAIT_IN), 0);

    /* Both boundary pins configure, write and read successfully */
    MR_TEST_EXPECT_EQ_INT(mr_gpio_configure(drv, 0, &config), 0);
    MR_TEST_EXPECT_EQ_UINT(dd2_gpio_last_pin, 0U);
    MR_TEST_EXPECT_EQ_INT(mr_gpio_configure(drv, 31, &config), 0);
    MR_TEST_EXPECT_EQ_UINT(dd2_gpio_last_pin, 31U);
    MR_TEST_EXPECT_EQ_INT(mr_gpio_write(drv, 31, 1), (int)sizeof(value));
    MR_TEST_EXPECT_EQ_UINT(dd2_gpio_last_pin, 31U);
    MR_TEST_EXPECT_EQ_INT(dd2_gpio_last_value, 1);
    MR_TEST_EXPECT_EQ_INT(mr_gpio_read(drv, 0, &value), (int)sizeof(value));
    MR_TEST_EXPECT_EQ_INT(value, 1);

    /* A pin that does not exist is rejected by configure and event
       attach, pins at or above 32 assert inside the inline wrappers and
       must not be passed in by callers */
    MR_TEST_EXPECT_EQ_INT(mr_gpio_configure(drv, 32, &config), -MR_EINVAL);
    MR_TEST_EXPECT_EQ_INT(mr_gpio_configure(drv, 1, &config), -MR_EINVAL);
    MR_TEST_EXPECT_EQ_INT(mr_gpio_event_attach(drv, 1, &waiter), -MR_EINVAL);
    /* Potential bug: pin write and read skip the exist mask check so a
       non existing pin reaches the backend unchanged */
    MR_TEST_EXPECT_EQ_INT(mr_gpio_write(drv, 1, 1), (int)sizeof(value));
    MR_TEST_EXPECT_EQ_INT(dd2_gpio_last_pin, 1U);
    MR_TEST_EXPECT_EQ_INT(mr_gpio_read(drv, 1, &value), (int)sizeof(value));

    mr_waiter_del(&waiter);
    mr_gpio_del(drv);
}

MR_TEST(gpio, d2_output_input_mode_transitions) {
    mr_driver_t *drv = MR_NULL;

    dd2_gpio_reset();

    drv = dd2_gpio_create(0xffffffffU, 0U);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);

    /* Output puts the configured level into pin_configure */
    MR_TEST_EXPECT_EQ_INT(mr_gpio_output(drv, 3, MR_GPIO_OTYPE_PP, 1), 0);
    MR_TEST_EXPECT_EQ_INT(dd2_gpio_write_count, 0);
    MR_TEST_EXPECT_EQ_INT(dd2_gpio_last_config.level, 1);
    MR_TEST_EXPECT_EQ_INT(dd2_gpio_last_config.mode, MR_GPIO_MODE_OUTPUT);

    /* Switching to input writes nothing */
    MR_TEST_EXPECT_EQ_INT(mr_gpio_input(drv, 3, MR_GPIO_PULL_DOWN), 0);
    MR_TEST_EXPECT_EQ_INT(dd2_gpio_write_count, 0);
    MR_TEST_EXPECT_EQ_INT(dd2_gpio_last_config.mode, MR_GPIO_MODE_INPUT);
    MR_TEST_EXPECT_EQ_INT(dd2_gpio_last_config.pull, MR_GPIO_PULL_DOWN);

    /* Back to output reconfigures the requested level */
    MR_TEST_EXPECT_EQ_INT(mr_gpio_output(drv, 3, MR_GPIO_OTYPE_OD, 0), 0);
    MR_TEST_EXPECT_EQ_INT(dd2_gpio_write_count, 0);
    MR_TEST_EXPECT_EQ_INT(dd2_gpio_last_config.level, 0);
    MR_TEST_EXPECT_EQ_INT(dd2_gpio_last_config.otype, MR_GPIO_OTYPE_OD);

    /* An out of range pull value is masked to two bits */
    MR_TEST_EXPECT_EQ_INT(mr_gpio_input(drv, 3, (mr_gpio_pull_t)0x83U), 0);
    MR_TEST_EXPECT_EQ_INT(dd2_gpio_last_config.pull, 3);

    mr_gpio_del(drv);
}

MR_TEST(gpio, d2_port_mixed_exist_and_invert_masks) {
    mr_driver_t *drv = MR_NULL;
    mr_uint32_t mask = 0;

    dd2_gpio_reset();

    /* Complementary exist and invert masks over the whole port */
    drv = dd2_gpio_create(0xf0f0f0f0U, 0x0f0f0f0fU);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);

    /* The port write inverts then clamps to the existing pins */
    MR_TEST_EXPECT_EQ_INT(mr_gpio_port_write(drv, 0xffffffffU),
                          (int)sizeof(mask));
    MR_TEST_EXPECT_EQ_UINT(dd2_gpio_last_port_mask, 0xf0f0f0f0U);

    /* The port write XORs the invert mask first and then clamps to the
       exist mask, so a mask whose surviving exist bits were flipped by
       the inversion still sends ones */
    MR_TEST_EXPECT_EQ_INT(mr_gpio_port_write(drv, 0x0000ffffU),
                          (int)sizeof(mask));
    MR_TEST_EXPECT_EQ_UINT(dd2_gpio_last_port_mask, 0x0000f0f0U);

    /* The port read inverts and clamps the backend value the same way */
    dd2_gpio_port_read_ret_mask = 0x12345678U;
    MR_TEST_EXPECT_EQ_INT(mr_gpio_port_read(drv, &mask), (int)sizeof(mask));
    MR_TEST_EXPECT_EQ_UINT(mask, 0x10305070U);

    mr_gpio_del(drv);
}

MR_TEST(gpio, d2_event_attach_boundary_pin) {
    mr_driver_t *drv = MR_NULL;
    mr_waiter_t waiter;

    dd2_gpio_reset();

    drv = dd2_gpio_create(1U << 31, 0U);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);

    /* The highest existing pin accepts an event attachment */
    MR_TEST_ASSERT_EQ_INT(
        mr_waiter_init(&waiter, dd2_gpio_waiter_entry, &dd2_gpio_waiter_count,
                       MR_WAIT_IN), 0);
    MR_TEST_EXPECT_EQ_INT(mr_gpio_event_attach(drv, 31, &waiter), 0);
    MR_TEST_EXPECT_EQ_INT(dd2_gpio_event_attach_count, 1);
    MR_TEST_EXPECT_EQ_UINT(dd2_gpio_last_pin, 31U);

    /* A non existing pin is rejected before reaching the backend */
    MR_TEST_EXPECT_EQ_INT(mr_gpio_event_attach(drv, 0, &waiter), -MR_EINVAL);
    MR_TEST_EXPECT_EQ_INT(dd2_gpio_event_attach_count, 1);

    mr_waiter_del(&waiter);
    mr_gpio_del(drv);
}

#endif /* defined(MR_USE_DRIVER_GPIO) */

#if defined(MR_USE_DRIVER_RAM)

/* Backing store RAM mock state */
static mr_uint8_t dd2_ram_store[64];
static int dd2_ram_mount_count;
static int dd2_ram_unmount_count;

static mr_err_t dd2_ram_mount(const mr_driver_t *drv) {
    MR_IGNORE(drv);
    dd2_ram_mount_count++;
    return 0;
}

static mr_err_t dd2_ram_unmount(const mr_driver_t *drv) {
    MR_IGNORE(drv);
    dd2_ram_unmount_count++;
    return 0;
}

static mr_ssize_t dd2_ram_write(const mr_driver_t *drv, mr_ssize_t offset,
                                const mr_uint8_t *buf, mr_size_t size) {
    MR_IGNORE(drv);
    if ((offset < 0) || ((mr_size_t)offset + size > sizeof(dd2_ram_store))) {
        return -MR_EINVAL;
    }
    mr_memcpy(&dd2_ram_store[offset], buf, size);
    return (mr_ssize_t)size;
}

static mr_ssize_t dd2_ram_read(const mr_driver_t *drv, mr_ssize_t offset,
                               mr_uint8_t *buf, mr_size_t size) {
    MR_IGNORE(drv);
    if ((offset < 0) || ((mr_size_t)offset + size > sizeof(dd2_ram_store))) {
        return -MR_EINVAL;
    }
    mr_memcpy(buf, &dd2_ram_store[offset], size);
    return (mr_ssize_t)size;
}

static const mr_ram_ops_t dd2_ram_backing_ops = {
    .mount = dd2_ram_mount,
    .unmount = dd2_ram_unmount,
    .write = dd2_ram_write,
    .read = dd2_ram_read,
};

/* Recorder RAM mock state */
static mr_ssize_t dd2_ram_offset_seen;
static mr_size_t dd2_ram_size_seen;
static const void *dd2_ram_buf_seen;

static mr_ssize_t dd2_ram_rec_write(const mr_driver_t *drv, mr_ssize_t offset,
                                    const mr_uint8_t *buf, mr_size_t size) {
    MR_IGNORE(drv);
    dd2_ram_offset_seen = offset;
    dd2_ram_size_seen = size;
    dd2_ram_buf_seen = buf;
    return (mr_ssize_t)size;
}

static const mr_ram_ops_t dd2_ram_rec_ops = {
    .mount = MR_NULL,
    .unmount = MR_NULL,
    .write = dd2_ram_rec_write,
    .read = MR_NULL,
};

static mr_driver_t *dd2_ram_backing_create(void) {
    mr_ram_data_t *data = mr_malloc(sizeof(*data));

    if (data == MR_NULL) {
        return MR_NULL;
    }
    data->regions = MR_NULL;
    data->regions_num = 0;
    return mr_ram_create(data, MR_NULL, (mr_ram_ops_t *)&dd2_ram_backing_ops);
}

static mr_driver_t *dd2_ram_rec_create(void) {
    mr_ram_data_t *data = mr_malloc(sizeof(*data));

    if (data == MR_NULL) {
        return MR_NULL;
    }
    data->regions = MR_NULL;
    data->regions_num = 0;
    return mr_ram_create(data, MR_NULL, (mr_ram_ops_t *)&dd2_ram_rec_ops);
}

MR_TEST(ram, d2_backing_io_and_mount_cycles_preserve_data) {
    mr_driver_t *drv = MR_NULL;
    mr_uint8_t pattern[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    mr_uint8_t shadow[8] = {0};
    mr_uint8_t edge[4] = {0xa1, 0xa2, 0xa3, 0xa4};
    mr_uint8_t edge_back[4] = {0};

    dd2_ram_mount_count = 0;
    dd2_ram_unmount_count = 0;
    mr_memset(dd2_ram_store, 0, sizeof(dd2_ram_store));

    drv = dd2_ram_backing_create();
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_EXPECT_EQ_INT(dd2_ram_mount_count, 1);

    /* A write followed by a read returns the stored pattern */
    MR_TEST_EXPECT_EQ_INT(mr_ram_write(drv, 0, pattern, sizeof(pattern)),
                          (int)sizeof(pattern));
    MR_TEST_EXPECT_EQ_INT(mr_ram_read(drv, 0, shadow, sizeof(shadow)),
                          (int)sizeof(shadow));
    MR_TEST_EXPECT_EQ_INT(mr_memcmp(shadow, pattern, sizeof(pattern)), 0);

    /* A write ending exactly at the region end is accepted */
    MR_TEST_EXPECT_EQ_INT(mr_ram_write(drv, 60, edge, sizeof(edge)),
                          (int)sizeof(edge));
    MR_TEST_EXPECT_EQ_INT(mr_ram_read(drv, 60, edge_back, sizeof(edge_back)),
                          (int)sizeof(edge_back));
    MR_TEST_EXPECT_EQ_INT(mr_memcmp(edge_back, edge, sizeof(edge)), 0);

    /* Unmount then mount again must preserve the stored data */
    MR_TEST_EXPECT_EQ_INT(mr_driver_shutdown(drv), 0);
    MR_TEST_EXPECT_EQ_INT(dd2_ram_unmount_count, 1);
    MR_TEST_EXPECT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_EXPECT_EQ_INT(dd2_ram_mount_count, 2);
    MR_TEST_EXPECT_EQ_INT(mr_ram_read(drv, 0, shadow, sizeof(shadow)),
                          (int)sizeof(shadow));
    MR_TEST_EXPECT_EQ_INT(mr_memcmp(shadow, pattern, sizeof(pattern)), 0);

    mr_driver_del(drv);
}

MR_TEST(ram, d2_backing_rejects_out_of_range_io) {
    mr_driver_t *drv = MR_NULL;
    mr_uint8_t byte = 0x5a;

    dd2_ram_mount_count = 0;
    dd2_ram_unmount_count = 0;
    mr_memset(dd2_ram_store, 0, sizeof(dd2_ram_store));

    drv = dd2_ram_backing_create();
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);

    /* The backend rejects a write crossing the region end */
    MR_TEST_EXPECT_EQ_INT(mr_ram_write(drv, 61, &byte, 4), -MR_EINVAL);
    MR_TEST_EXPECT_EQ_INT(mr_ram_read(drv, 61, &byte, 4), -MR_EINVAL);

    /* A negative offset is rejected by the emulated backend */
    MR_TEST_EXPECT_EQ_INT(mr_ram_write(drv, -1, &byte, 1), -MR_EINVAL);
    MR_TEST_EXPECT_EQ_INT(mr_ram_read(drv, -1, &byte, 1), -MR_EINVAL);

    /* Potential bug: the core performs no bounds check against the region
     * metadata at all, every range decision is left to the backend */
    MR_TEST_EXPECT_EQ_INT(mr_driver_is_ready(drv), MR_TRUE);

    mr_driver_del(drv);
}

MR_TEST(ram, d2_offset_extremes_passthrough) {
    mr_driver_t *drv = MR_NULL;
    mr_uint8_t byte = 0x33;

    dd2_ram_offset_seen = 0;
    dd2_ram_size_seen = 0;
    dd2_ram_buf_seen = MR_NULL;

    drv = dd2_ram_rec_create();
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);

    /* Extreme positive and negative offsets reach the backend unwrapped */
    MR_TEST_EXPECT_EQ_INT(mr_ram_write(drv, 2147483647, &byte, 1), 1);
    MR_TEST_EXPECT_EQ_INT(dd2_ram_offset_seen, 2147483647);
    MR_TEST_EXPECT_EQ_UINT(dd2_ram_size_seen, 1U);
    MR_TEST_EXPECT_EQ_PTR(dd2_ram_buf_seen, &byte);
    MR_TEST_EXPECT_EQ_INT(mr_ram_write(drv, -2147483648, MR_NULL, 0), 0);
    MR_TEST_EXPECT_EQ_INT(dd2_ram_offset_seen, -2147483648);

    mr_driver_del(drv);
}

#endif /* defined(MR_USE_DRIVER_RAM) */

#endif /* defined(MR_USE_TEST) */
