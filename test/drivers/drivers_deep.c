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
#endif /* defined(MR_USE_DRIVER_RAM) */
#include <mrX/port/mr_port_irq.h>
#include <mrX/port/mr_port_output.h>
#if defined(MR_USE_CLOCK_HIRES)
#include <mrX/port/mr_port_clock.h>
#endif /* defined(MR_USE_CLOCK_HIRES) */

#if defined(MR_USE_DRIVER)

/* Driver mock state */
static int dd_driver_init_count;
static int dd_driver_deinit_count;
static int dd_driver_release_count;

static void dd_driver_reset(void) {
    dd_driver_init_count = 0;
    dd_driver_deinit_count = 0;
    dd_driver_release_count = 0;
}

static mr_err_t dd_driver_init(const mr_driver_t *drv) {
    MR_IGNORE(drv);
    dd_driver_init_count++;
    return 0;
}

static mr_err_t dd_driver_deinit(const mr_driver_t *drv) {
    MR_IGNORE(drv);
    dd_driver_deinit_count++;
    return 0;
}

static void dd_driver_release(const mr_driver_t *drv) {
    MR_IGNORE(drv);
    dd_driver_release_count++;
}

static mr_err_t dd_driver_init_fail(const mr_driver_t *drv) {
    MR_IGNORE(drv);
    dd_driver_init_count++;
    return -1;
}

static mr_err_t dd_driver_init_retry(const mr_driver_t *drv) {
    MR_IGNORE(drv);
    dd_driver_init_count++;
    return (dd_driver_init_count == 1) ? -MR_EIO : 0;
}

static const mr_driver_clazz_t dd_driver_clazz = MR_DRIVER_CLAZZ_INIT(
    "dd_driver", dd_driver_init, dd_driver_deinit, dd_driver_release);

static const mr_driver_clazz_t dd_driver_fail_clazz = MR_DRIVER_CLAZZ_INIT(
    "dd_driver_fail", dd_driver_init_fail, dd_driver_deinit, MR_NULL);

static const mr_driver_clazz_t dd_driver_retry_clazz = MR_DRIVER_CLAZZ_INIT(
    "dd_driver_retry", dd_driver_init_retry, dd_driver_deinit, MR_NULL);

/* A statically installed driver used to probe mr_driver_find */
static const mr_driver_clazz_t dd_deep_find_clazz = MR_DRIVER_CLAZZ_INIT(
    "dd_deep_find", dd_driver_init, dd_driver_deinit, dd_driver_release);

MR_DRIVER_INSTALL(dd_deep_find, &dd_deep_find_clazz,
                  MR_DRIVER_TYPE_RAM, MR_NULL, MR_NULL, MR_NULL, "3.7");

MR_TEST(driver, setup_busy_with_manual_lock) {
    mr_driver_t *drv = MR_NULL;

    dd_driver_reset();

    drv = mr_driver_create(&dd_driver_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);

    /* Manually mark the driver as locked to reach the busy branch */
    mr_atomic_fetch_or(&drv->flags, (mr_atomic_t)MR_DRIVER_FLAG_LOCK);
    MR_TEST_EXPECT_EQ_INT(mr_driver_setup(drv), -MR_EBUSY);
    MR_TEST_EXPECT_EQ_INT(dd_driver_init_count, 0);
    MR_TEST_EXPECT_EQ_INT(mr_driver_shutdown(drv), -MR_EBUSY);
    MR_TEST_EXPECT_EQ_INT(dd_driver_deinit_count, 0);

    /* Release the lock and verify the driver still sets up */
    mr_atomic_fetch_and(&drv->flags, (mr_atomic_t)~MR_DRIVER_FLAG_LOCK);
    MR_TEST_EXPECT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_EXPECT_EQ_INT(dd_driver_init_count, 1);

    mr_driver_del(drv);
}

MR_TEST(driver, dying_rejects_setup_and_shutdown) {
    mr_driver_t *drv = MR_NULL;
    mr_driver_t *held = MR_NULL;

    dd_driver_reset();

    drv = mr_driver_create(&dd_driver_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);

    held = mr_driver_get(drv);
    MR_TEST_ASSERT_EQ_PTR(held, drv);

    /* Delete with a live reference: driver dies but memory stays valid */
    MR_TEST_EXPECT_EQ_INT(mr_driver_del(drv), 0);
    MR_TEST_EXPECT_EQ_INT(dd_driver_release_count, 0);

    /* Operations on a dying driver must be rejected, not crash */
    MR_TEST_EXPECT_EQ_INT(mr_driver_setup(drv), -MR_EPERM);
    MR_TEST_EXPECT_EQ_INT(mr_driver_shutdown(drv), -MR_EPERM);
    MR_TEST_EXPECT_EQ_INT(dd_driver_init_count, 1);
    MR_TEST_EXPECT_EQ_INT(dd_driver_deinit_count, 0);

    /* A deleted driver is never ready while references keep it alive */
    MR_TEST_EXPECT_EQ_INT(mr_driver_is_ready(drv), MR_FALSE);

    /* Last put releases the driver and deinitializes it */
    MR_TEST_EXPECT_EQ_INT(mr_driver_put(held), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(dd_driver_deinit_count, 1);
    MR_TEST_EXPECT_EQ_INT(dd_driver_release_count, 1);
}

MR_TEST(driver, ops_on_uninited_object) {
    mr_driver_t uninit_drv;
    mr_driver_t *got = MR_NULL;
    mr_bool_t ret;

    dd_driver_reset();
    mr_memset(&uninit_drv, 0, sizeof(uninit_drv));

    /* All accessors must reject zeroed memory without asserting */
    got = mr_driver_get(&uninit_drv);
    MR_TEST_EXPECT_EQ_PTR(got, MR_NULL);

    ret = mr_driver_put(&uninit_drv);
    MR_TEST_EXPECT_EQ_INT(ret, MR_TRUE);

    MR_TEST_EXPECT_EQ_INT(mr_driver_is_ready(&uninit_drv), MR_FALSE);
}

MR_TEST(driver, find_registered_driver) {
    mr_driver_t *drv = MR_NULL;
    mr_driver_t *again = MR_NULL;

    dd_driver_reset();

    /* The driver is installed at init time and auto set up by the kernel */
    drv = mr_driver_find("dd_deep_find");
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_EXPECT_EQ_INT(mr_driver_is_ready(drv), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT((int)mr_driver_type(drv), MR_DRIVER_TYPE_RAM);
    MR_TEST_EXPECT_EQ_INT(MR_DRIVER_IS_SETUP(drv), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(mr_driver_state(drv), 0);

    /* A second find resolves to the same driver and takes a reference */
    again = mr_driver_find("dd_deep_find");
    MR_TEST_EXPECT_EQ_PTR(again, drv);

    /* Each find must be balanced by a put, the static entry still holds one */
    MR_TEST_EXPECT_EQ_INT(mr_driver_put(again), MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(mr_driver_put(drv), MR_FALSE);
}

MR_TEST(driver, failed_setup_then_shutdown) {
    mr_driver_t *drv = MR_NULL;

    dd_driver_reset();

    drv = mr_driver_create(&dd_driver_fail_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);

    MR_TEST_EXPECT_EQ_INT(mr_driver_setup(drv), -1);
    MR_TEST_EXPECT_EQ_INT(dd_driver_init_count, 1);

    /* A failed setup leaves no SETUP flag, shutdown is a no-op */
    MR_TEST_EXPECT_EQ_INT(mr_driver_shutdown(drv), 0);
    MR_TEST_EXPECT_EQ_INT(dd_driver_deinit_count, 0);
    MR_TEST_EXPECT_EQ_INT(mr_driver_is_ready(drv), MR_FALSE);

    /* Current behavior: state is not cleared by the no-op shutdown */
    MR_TEST_EXPECT_EQ_INT(mr_driver_state(drv), -1);

    /* Delete without SETUP must not call deinit either */
    MR_TEST_EXPECT_EQ_INT(mr_driver_del(drv), 0);
    MR_TEST_EXPECT_EQ_INT(dd_driver_deinit_count, 0);
}

MR_TEST(driver, setup_retry_after_init_failure) {
    mr_driver_t *drv = MR_NULL;

    dd_driver_reset();

    drv = mr_driver_create(&dd_driver_retry_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);

    MR_TEST_EXPECT_EQ_INT(mr_driver_setup(drv), -MR_EIO);
    MR_TEST_EXPECT_EQ_INT(dd_driver_init_count, 1);
    MR_TEST_EXPECT_EQ_INT(mr_driver_state(drv), -MR_EIO);
    MR_TEST_EXPECT_EQ_INT(mr_driver_is_ready(drv), MR_FALSE);

    /* A failed setup must not leak the lock or mark the driver as setup */
    MR_TEST_EXPECT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_EXPECT_EQ_INT(dd_driver_init_count, 2);
    MR_TEST_EXPECT_EQ_INT(mr_driver_state(drv), 0);
    MR_TEST_EXPECT_EQ_INT(mr_driver_is_ready(drv), MR_TRUE);

    mr_driver_del(drv);
    MR_TEST_EXPECT_EQ_INT(dd_driver_deinit_count, 1);
}

MR_TEST(driver, clazz_init_helper) {
    mr_driver_clazz_t clazz;

    dd_driver_reset();

    mr_driver_clazz_init(&clazz, "dd_clazz", dd_driver_init, dd_driver_deinit,
                         dd_driver_release);
    MR_TEST_EXPECT_EQ_INT(mr_strcmp(clazz.name, "dd_clazz"), 0);
    MR_TEST_EXPECT_EQ_PTR(clazz.init, dd_driver_init);
    MR_TEST_EXPECT_EQ_PTR(clazz.deinit, dd_driver_deinit);
    MR_TEST_EXPECT_EQ_PTR(clazz.release, dd_driver_release);
}

#endif /* defined(MR_USE_DRIVER) */

#if defined(MR_USE_DRIVER_ADC)

/* ADC mock state */
static int dd_adc_configure_count;
static int dd_adc_read_count;
static mr_uint32_t dd_adc_last_channel;
static mr_adc_seq_t dd_adc_last_seq;
static mr_err_t dd_adc_configure_ret;
static mr_err_t dd_adc_read_ret;

static void dd_adc_reset(void) {
    dd_adc_configure_count = 0;
    dd_adc_read_count = 0;
    dd_adc_last_channel = 0;
    dd_adc_last_seq = (mr_adc_seq_t){0};
    dd_adc_configure_ret = 0;
    dd_adc_read_ret = 0;
}

static mr_err_t dd_adc_channel_configure(const mr_driver_t *drv,
                                         mr_uint32_t channel,
                                         const mr_adc_config_t *config) {
    MR_IGNORE(drv);
    MR_IGNORE(config);
    dd_adc_configure_count++;
    dd_adc_last_channel = channel;
    return dd_adc_configure_ret;
}

static mr_ssize_t dd_adc_read(const mr_driver_t *drv, mr_adc_seq_t *seq) {
    MR_IGNORE(drv);
    dd_adc_read_count++;
    dd_adc_last_seq = *seq;
    return dd_adc_read_ret;
}

static const mr_adc_ops_t dd_adc_ops = {
    .channel_configure = dd_adc_channel_configure,
    .read = dd_adc_read,
#if defined(MR_USE_DRIVER_ADC_STREAM)
    .stream_start = MR_NULL,
    .stream_stop = MR_NULL,
    .stream_is_busy = MR_NULL,
    .stream_offset = MR_NULL,
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) */
};

static mr_driver_t *dd_adc_create(mr_uint32_t exist_mask) {
    mr_adc_data_t *data = mr_malloc(sizeof(*data));

    if (data == MR_NULL) {
        return MR_NULL;
    }
    data->ref_internal = 1200;
    data->exist_mask = exist_mask;
    return mr_adc_create(data, MR_NULL, (mr_adc_ops_t *)&dd_adc_ops);
}

static void dd_adc_waiter_entry(mr_waiter_t *waiter, void *param,
                                mr_wait_event_t event, const void *data) {
    MR_IGNORE(waiter);
    MR_IGNORE(event);
    MR_IGNORE(data);
    (*(int *)param)++;
}

MR_TEST(adc, read_zero_size_and_exist_free) {
    mr_driver_t *drv = MR_NULL;
    mr_adc_config_t config = {0};
    mr_adc_seq_t seq = {
        .channels = 1,
        .resolution = 32,
        .buf = MR_NULL,
        .size = 0,
    };

    dd_adc_reset();

    /* A driver without any existing channel still reads and configures */
    drv = dd_adc_create(0U);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);

    /* Configure on an empty mask is rejected for any channel */
    MR_TEST_EXPECT_EQ_INT(mr_adc_configure(drv, 0, &config), -MR_EINVAL);
    MR_TEST_EXPECT_EQ_INT(dd_adc_configure_count, 0);

    /* Current behavior: the read path does not check the exist mask and
     * passes the sequence through, a zero size buffer is legal */
    dd_adc_read_ret = 0;
    MR_TEST_EXPECT_EQ_INT(mr_adc_read(drv, &seq), 0);
    MR_TEST_EXPECT_EQ_INT(dd_adc_read_count, 1);
    MR_TEST_EXPECT_EQ_UINT(dd_adc_last_seq.size, 0U);
    MR_TEST_EXPECT_EQ_PTR(dd_adc_last_seq.buf, MR_NULL);

    mr_adc_del(drv);
}

MR_TEST(adc, event_edge_latch_and_rewait) {
    mr_driver_t *drv = MR_NULL;
    mr_waiter_t level_waiter;
    mr_waiter_t edge_waiter;
    int level_count = 0;
    int edge_count = 0;

    dd_adc_reset();

    drv = dd_adc_create(1U);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);

    /* Raising without any waiter is not an error */
    MR_TEST_EXPECT_EQ_INT(mr_adc_event_raise(drv, MR_ADC_WAIT_STREAM_HALF), 0);
    MR_TEST_EXPECT_EQ_INT(level_count, 0);

    MR_TEST_ASSERT_EQ_INT(mr_waiter_init(&level_waiter, dd_adc_waiter_entry,
                                         &level_count,
                                         MR_ADC_WAIT_STREAM_HALF),
                          0);
    MR_TEST_ASSERT_EQ_INT(mr_waiter_init(&edge_waiter, dd_adc_waiter_entry,
                                         &edge_count,
                                         MR_ADC_WAIT_STREAM_HALF
                                             | MR_WAIT_EDGE),
                          0);

    /* Attaching the same waiter twice must be rejected */
    MR_TEST_EXPECT_EQ_INT(mr_adc_event_attach(drv, &level_waiter), 0);
    MR_TEST_EXPECT_EQ_INT(mr_adc_event_attach(drv, &level_waiter), -MR_EBUSY);
    MR_TEST_EXPECT_EQ_INT(mr_adc_event_attach(drv, &edge_waiter), 0);

    /* Level waiter fires on every raise, edge waiter only on new events */
    MR_TEST_EXPECT_EQ_INT(mr_adc_event_raise(drv, MR_ADC_WAIT_STREAM_HALF), 0);
    MR_TEST_EXPECT_EQ_INT(level_count, 1);
    MR_TEST_EXPECT_EQ_INT(edge_count, 1);
    MR_TEST_EXPECT_EQ_INT(mr_adc_event_raise(drv, MR_ADC_WAIT_STREAM_HALF), 0);
    MR_TEST_EXPECT_EQ_INT(level_count, 2);
    MR_TEST_EXPECT_EQ_INT(edge_count, 1);

    /* An unrelated event triggers neither waiter */
    MR_TEST_EXPECT_EQ_INT(mr_adc_event_raise(drv, MR_ADC_WAIT_STREAM_FULL), 0);
    MR_TEST_EXPECT_EQ_INT(level_count, 2);
    MR_TEST_EXPECT_EQ_INT(edge_count, 1);

    mr_waiter_del(&level_waiter);
    mr_waiter_del(&edge_waiter);
    mr_adc_del(drv);
}

#endif /* defined(MR_USE_DRIVER_ADC) */

#if defined(MR_USE_DRIVER_DAC)

/* DAC mock state */
static int dd_dac_write_count;
static mr_dac_seq_t dd_dac_last_seq;
static mr_ssize_t dd_dac_write_ret;

static void dd_dac_reset(void) {
    dd_dac_write_count = 0;
    dd_dac_last_seq = (mr_dac_seq_t){0};
    dd_dac_write_ret = 0;
}

static mr_ssize_t dd_dac_write(const mr_driver_t *drv, mr_dac_seq_t *seq) {
    MR_IGNORE(drv);
    dd_dac_write_count++;
    dd_dac_last_seq = *seq;
    return dd_dac_write_ret;
}

static const mr_dac_ops_t dd_dac_ops = {
    .write = dd_dac_write,
#if defined(MR_USE_DRIVER_DAC_STREAM)
    .stream_start = MR_NULL,
    .stream_stop = MR_NULL,
    .stream_is_busy = MR_NULL,
    .stream_offset = MR_NULL,
#endif /* defined(MR_USE_DRIVER_DAC_STREAM) */
};

static mr_driver_t *dd_dac_create(mr_uint32_t exist_mask) {
    mr_dac_data_t *data = mr_malloc(sizeof(*data));

    if (data == MR_NULL) {
        return MR_NULL;
    }
    data->ref_internal = 3300;
    data->exist_mask = exist_mask;
    return mr_dac_create(data, MR_NULL, (mr_dac_ops_t *)&dd_dac_ops);
}

MR_TEST(dac, write_zero_size_passthrough) {
    mr_driver_t *drv = MR_NULL;
    mr_dac_seq_t seq = {
        .channels = 0x80000001U,
        .resolution = 8,
        .buf = MR_NULL,
        .size = 0,
    };

    dd_dac_reset();

    drv = dd_dac_create(0U);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);

    /* A zero size write with no buffer is dispatched untouched */
    MR_TEST_EXPECT_EQ_INT(mr_dac_write(drv, &seq), 0);
    MR_TEST_EXPECT_EQ_INT(dd_dac_write_count, 1);
    MR_TEST_EXPECT_EQ_UINT(dd_dac_last_seq.channels, 0x80000001U);
    MR_TEST_EXPECT_EQ_UINT(dd_dac_last_seq.size, 0U);
    MR_TEST_EXPECT_EQ_PTR(dd_dac_last_seq.buf, MR_NULL);

    mr_dac_del(drv);
}

MR_TEST(dac, conversion_boundaries) {
    dd_dac_reset();

    /* Zero input maps to zero raw for all conversion helpers */
    MR_TEST_EXPECT_EQ_INT(mr_dac_millivolt2raw(3300, 12, 0), 0);
    MR_TEST_EXPECT_EQ_INT(mr_dac_microvolt2raw(3300, 12, 0), 0);
    MR_TEST_EXPECT_EQ_INT((int)mr_dac_volt2raw(3300, 12, 0.0f), 0);

    /* Full scale reference maps to the full raw range */
    MR_TEST_EXPECT_EQ_INT(mr_dac_millivolt2raw(3300, 12, 3300), 4095);
    MR_TEST_EXPECT_EQ_INT(mr_dac_microvolt2raw(3300, 12, 3300000), 4095);

    /* Conversion truncates toward zero */
    MR_TEST_EXPECT_EQ_INT(mr_dac_millivolt2raw(3300, 12, 1), 1);
    MR_TEST_EXPECT_EQ_INT(mr_dac_microvolt2raw(3300, 12, 1), 0);

    /* Negative input yields negative raw without clamping */
    MR_TEST_EXPECT_EQ_INT(mr_dac_millivolt2raw(3300, 12, -3300), -4095);
}

#endif /* defined(MR_USE_DRIVER_DAC) */

#if defined(MR_USE_DRIVER_GPIO)

/* GPIO mock state */
static int dd_gpio_configure_count;
static int dd_gpio_write_count;
static mr_uint32_t dd_gpio_last_pin;
static mr_uint8_t dd_gpio_last_value;
static mr_uint32_t dd_gpio_last_port_mask;
static mr_gpio_config_t dd_gpio_last_config;
static mr_err_t dd_gpio_configure_ret;
static mr_err_t dd_gpio_read_ret;
static mr_uint32_t dd_gpio_port_read_mask;

static void dd_gpio_reset(void) {
    dd_gpio_configure_count = 0;
    dd_gpio_write_count = 0;
    dd_gpio_last_pin = 0;
    dd_gpio_last_value = 0;
    dd_gpio_last_port_mask = 0;
    dd_gpio_last_config = (mr_gpio_config_t){0};
    dd_gpio_configure_ret = 0;
    dd_gpio_read_ret = 0;
    dd_gpio_port_read_mask = 0xa5a5a5a5U;
}

static mr_err_t dd_gpio_pin_configure(const mr_driver_t *drv, mr_uint32_t pin,
                                      const mr_gpio_config_t *config) {
    MR_IGNORE(drv);
    dd_gpio_configure_count++;
    dd_gpio_last_pin = pin;
    dd_gpio_last_config = *config;
    return dd_gpio_configure_ret;
}

static mr_err_t dd_gpio_pin_write(const mr_driver_t *drv, mr_uint32_t pin,
                                  mr_uint8_t value) {
    MR_IGNORE(drv);
    dd_gpio_write_count++;
    dd_gpio_last_pin = pin;
    dd_gpio_last_value = value;
    return 0;
}

static mr_err_t dd_gpio_pin_read(const mr_driver_t *drv, mr_uint32_t pin,
                                 mr_uint8_t *value) {
    MR_IGNORE(drv);
    MR_IGNORE(pin);
    if (dd_gpio_read_ret != 0) {
        return dd_gpio_read_ret;
    }
    *value = dd_gpio_last_value;
    return 0;
}

static mr_err_t dd_gpio_port_write(const mr_driver_t *drv, mr_uint32_t mask) {
    MR_IGNORE(drv);
    dd_gpio_last_port_mask = mask;
    return 0;
}

static mr_err_t dd_gpio_port_read(const mr_driver_t *drv, mr_uint32_t *mask) {
    MR_IGNORE(drv);
    *mask = dd_gpio_port_read_mask;
    return 0;
}

static const mr_gpio_ops_t dd_gpio_ops = {
    .pin_configure = dd_gpio_pin_configure,
    .pin_write = dd_gpio_pin_write,
    .pin_read = dd_gpio_pin_read,
    .port_write = dd_gpio_port_write,
    .port_read = dd_gpio_port_read,
};

static mr_driver_t *dd_gpio_create(mr_uint32_t exist, mr_uint32_t invert) {
    mr_gpio_data_t *data = mr_malloc(sizeof(*data));

    if (data == MR_NULL) {
        return MR_NULL;
    }
    data->exist_mask = exist;
    data->invert_mask = invert;
    return mr_gpio_create(data, MR_NULL, (mr_gpio_ops_t *)&dd_gpio_ops);
}

MR_TEST(gpio, output_helper_invert_matrix) {
    mr_driver_t *drv = MR_NULL;

    dd_gpio_reset();

    drv = dd_gpio_create((1U << 0) | (1U << 1), 1U << 1);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);

    /* Helper helpers reject pins outside the exist mask */
    MR_TEST_EXPECT_EQ_INT(mr_gpio_output(drv, 5, MR_GPIO_OTYPE_PP, 1),
                          -MR_EINVAL);
    MR_TEST_EXPECT_EQ_INT(mr_gpio_input(drv, 5, MR_GPIO_PULL_UP), -MR_EINVAL);
    MR_TEST_EXPECT_EQ_INT(dd_gpio_configure_count, 0);

    /* The output helper masks the value to a single bit */
    MR_TEST_EXPECT_EQ_INT(mr_gpio_output(drv, 0, MR_GPIO_OTYPE_PP, 0xffU), 0);
    MR_TEST_EXPECT_EQ_INT(dd_gpio_last_config.mode, MR_GPIO_MODE_OUTPUT);
    MR_TEST_EXPECT_EQ_INT(dd_gpio_last_config.level, 1);
    MR_TEST_EXPECT_EQ_UINT(dd_gpio_last_pin, 0U);

    /* An inverted output pin configures the complement of the level */
    MR_TEST_EXPECT_EQ_INT(mr_gpio_output(drv, 1, MR_GPIO_OTYPE_PP, 0xffU), 0);
    MR_TEST_EXPECT_EQ_INT(dd_gpio_last_config.level, 0);
    MR_TEST_EXPECT_EQ_INT(mr_gpio_output(drv, 1, MR_GPIO_OTYPE_PP, 0), 0);
    MR_TEST_EXPECT_EQ_INT(dd_gpio_last_config.level, 1);
    MR_TEST_EXPECT_EQ_INT(mr_gpio_output(drv, 0, MR_GPIO_OTYPE_PP, 0), 0);
    MR_TEST_EXPECT_EQ_INT(dd_gpio_last_config.level, 0);

    mr_gpio_del(drv);
}

MR_TEST(gpio, write_read_invert_roundtrip) {
    mr_driver_t *drv = MR_NULL;
    mr_uint8_t value = 0;

    dd_gpio_reset();

    drv = dd_gpio_create(0xffffffffU, 1U << 0);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);

    /* Write of a logical high on an inverted pin drives a raw low */
    MR_TEST_EXPECT_EQ_INT(mr_gpio_write(drv, 0, 1), (int)sizeof(value));
    MR_TEST_EXPECT_EQ_INT(dd_gpio_last_value, 0);

    /* Read restores the logical value by inverting the raw level back */
    MR_TEST_EXPECT_EQ_INT(mr_gpio_read(drv, 0, &value), (int)sizeof(value));
    MR_TEST_EXPECT_EQ_INT(value, 1);

    mr_gpio_del(drv);
}

MR_TEST(gpio, port_mask_exist_clamp) {
    mr_driver_t *drv = MR_NULL;
    mr_uint32_t mask = 0;

    dd_gpio_reset();

    drv = dd_gpio_create(0x3U, 0x2U);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);

    /* The port write is inverted first and then clamped to existing pins */
    MR_TEST_EXPECT_EQ_INT(mr_gpio_port_write(drv, 0xffffffffU),
                          (int)sizeof(mask));
    MR_TEST_EXPECT_EQ_UINT(dd_gpio_last_port_mask, 0x1U);

    /* The port read is clamped and inverted in the same order */
    MR_TEST_EXPECT_EQ_INT(mr_gpio_port_read(drv, &mask), (int)sizeof(mask));
    MR_TEST_EXPECT_EQ_UINT(mask, 0x3U);

    mr_gpio_del(drv);
}

MR_TEST(gpio, toggle_read_error_and_toggle) {
    mr_driver_t *drv = MR_NULL;

    dd_gpio_reset();

    drv = dd_gpio_create(0xffffffffU, 0U);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);

    /* A failing pin read aborts the toggle before any write */
    dd_gpio_read_ret = -MR_EIO;
    MR_TEST_EXPECT_EQ_INT(mr_gpio_toggle(drv, 2), -MR_EIO);
    MR_TEST_EXPECT_EQ_INT(dd_gpio_write_count, 0);
    dd_gpio_read_ret = 0;

    /* Toggle flips the raw pin level through read then write */
    MR_TEST_EXPECT_EQ_INT(mr_gpio_write(drv, 2, 1), 1);
    MR_TEST_EXPECT_EQ_INT(dd_gpio_last_value, 1);
    MR_TEST_EXPECT_EQ_INT(mr_gpio_toggle(drv, 2), 0);
    MR_TEST_EXPECT_EQ_INT(dd_gpio_last_value, 0);
    MR_TEST_EXPECT_EQ_INT(dd_gpio_write_count, 2);

    mr_gpio_del(drv);
}

#endif /* defined(MR_USE_DRIVER_GPIO) */

#if defined(MR_USE_DRIVER_RAM)

/* RAM mock state */
static int dd_ram_mount_count;
static int dd_ram_unmount_count;
static mr_ssize_t dd_ram_offset_seen;
static mr_size_t dd_ram_size_seen;
static const void *dd_ram_buf_seen;
static mr_err_t dd_ram_mount_ret;
static mr_err_t dd_ram_unmount_ret;
static mr_ssize_t dd_ram_write_ret;
static mr_ssize_t dd_ram_read_ret;

static void dd_ram_reset(void) {
    dd_ram_mount_count = 0;
    dd_ram_unmount_count = 0;
    dd_ram_offset_seen = 0;
    dd_ram_size_seen = 0;
    dd_ram_buf_seen = MR_NULL;
    dd_ram_mount_ret = 0;
    dd_ram_unmount_ret = 0;
    dd_ram_write_ret = 0;
    dd_ram_read_ret = 0;
}

static mr_err_t dd_ram_mount(const mr_driver_t *drv) {
    MR_IGNORE(drv);
    dd_ram_mount_count++;
    return dd_ram_mount_ret;
}

static mr_err_t dd_ram_unmount(const mr_driver_t *drv) {
    MR_IGNORE(drv);
    dd_ram_unmount_count++;
    return dd_ram_unmount_ret;
}

static mr_ssize_t dd_ram_write(const mr_driver_t *drv, mr_ssize_t offset,
                               const mr_uint8_t *buf, mr_size_t size) {
    MR_IGNORE(drv);
    dd_ram_offset_seen = offset;
    dd_ram_buf_seen = buf;
    dd_ram_size_seen = size;
    return dd_ram_write_ret;
}

static mr_ssize_t dd_ram_read(const mr_driver_t *drv, mr_ssize_t offset,
                              mr_uint8_t *buf, mr_size_t size) {
    MR_IGNORE(drv);
    dd_ram_offset_seen = offset;
    dd_ram_buf_seen = buf;
    dd_ram_size_seen = size;
    return dd_ram_read_ret;
}

static const mr_ram_ops_t dd_ram_ops = {
    .mount = dd_ram_mount,
    .unmount = dd_ram_unmount,
    .write = dd_ram_write,
    .read = dd_ram_read,
};

static mr_ram_region_t dd_ram_regions[2];

static mr_driver_t *dd_ram_create(void) {
    mr_ram_data_t *data = mr_malloc(sizeof(*data));

    if (data == MR_NULL) {
        return MR_NULL;
    }
    data->regions = dd_ram_regions;
    data->regions_num = 2;
    return mr_ram_create(data, MR_NULL, (mr_ram_ops_t *)&dd_ram_ops);
}

MR_TEST(ram, mount_recovery_after_failure) {
    mr_driver_t *drv = MR_NULL;

    dd_ram_reset();

    dd_ram_mount_ret = -MR_EIO;
    drv = dd_ram_create();
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_EXPECT_EQ_INT(mr_driver_setup(drv), -MR_EIO);
    MR_TEST_EXPECT_EQ_INT(dd_ram_mount_count, 1);
    MR_TEST_EXPECT_EQ_INT(mr_driver_is_ready(drv), MR_FALSE);

    /* A later setup retries the mount once the backend recovers */
    dd_ram_mount_ret = 0;
    MR_TEST_EXPECT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_EXPECT_EQ_INT(dd_ram_mount_count, 2);
    MR_TEST_EXPECT_EQ_INT(mr_driver_is_ready(drv), MR_TRUE);

    MR_TEST_EXPECT_EQ_INT(mr_driver_shutdown(drv), 0);
    MR_TEST_EXPECT_EQ_INT(dd_ram_unmount_count, 1);
    /* The driver is no longer set up so mr_ram_del would assert here */
    MR_TEST_EXPECT_EQ_INT(mr_driver_del(drv), 0);
}

MR_TEST(ram, region_metadata_and_zero_size_io) {
    mr_driver_t *drv = MR_NULL;
    mr_ram_data_t *data = MR_NULL;

    dd_ram_reset();

    drv = dd_ram_create();
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);

    /* The region metadata is preserved untouched by the core driver */
    data = (mr_ram_data_t *)MR_DRIVER_DATA(drv);
    MR_TEST_EXPECT_EQ_PTR(data->regions, dd_ram_regions);
    MR_TEST_EXPECT_EQ_UINT(data->regions_num, 2U);

    /* Zero size io with a null buffer reaches the backend unchanged */
    MR_TEST_EXPECT_EQ_INT(mr_ram_write(drv, 0, MR_NULL, 0), 0);
    MR_TEST_EXPECT_EQ_INT(dd_ram_offset_seen, 0);
    MR_TEST_EXPECT_EQ_UINT(dd_ram_size_seen, 0U);
    MR_TEST_EXPECT_EQ_PTR(dd_ram_buf_seen, MR_NULL);

    MR_TEST_EXPECT_EQ_INT(mr_ram_read(drv, 0, MR_NULL, 0), 0);
    MR_TEST_EXPECT_EQ_UINT(dd_ram_size_seen, 0U);

    /* Negative offsets are passed through without validation */
    MR_TEST_EXPECT_EQ_INT(mr_ram_write(drv, -8, MR_NULL, 0), 0);
    MR_TEST_EXPECT_EQ_INT(dd_ram_offset_seen, -8);

    mr_ram_del(drv);
}

#endif /* defined(MR_USE_DRIVER_RAM) */

MR_TEST(port, irq_disable_enable_nesting) {
    int m0, m1, m2, m3;

    /* Each disable returns the previous nesting level */
    m0 = mr_port_irq_disable();
    m1 = mr_port_irq_disable();
    m2 = mr_port_irq_disable();
    MR_TEST_EXPECT_EQ_INT(m1, m0 + 1);
    MR_TEST_EXPECT_EQ_INT(m2, m0 + 2);

    /* Enables must restore the level so a later disable matches again */
    mr_port_irq_enable(m2);
    mr_port_irq_enable(m1);
    m3 = mr_port_irq_disable();
    MR_TEST_EXPECT_EQ_INT(m3, m0 + 1);

    mr_port_irq_enable(m3);
    mr_port_irq_enable(m0);
}

MR_TEST(port, output_size_semantics) {
    /* The return value counts the characters actually printed */
    MR_TEST_EXPECT_EQ_INT(mr_port_output("mrX", 3), 3);
    MR_TEST_EXPECT_EQ_INT(mr_port_output("abcdef", 4), 4);

    /* A zero size must print nothing and return zero */
    MR_TEST_EXPECT_EQ_INT(mr_port_output("mrX", 0), 0);
}

#if defined(MR_USE_CLOCK_HIRES)
MR_TEST(port, clock_hires_monotonic_and_busy_wait) {
    mr_uint64_t t1, t2, t3, t4, t5;
    mr_int64_t delta;

    /* The host port initializes the hires clock to unix time in microseconds
     * at boot, so the tick must already be far beyond the epoch start */
    t1 = mr_port_clock_hires_tick();
    MR_TEST_EXPECT_GT_UINT(t1, 1000000000000ULL);

    /* A zero length busy wait must return immediately */
    mr_port_clock_busy_wait(0);
    t2 = mr_port_clock_hires_tick();
    MR_TEST_EXPECT_GE_UINT(t2, t1);

    /* The hires tick must advance by at least the busy wait duration */
    mr_port_clock_busy_wait(2000);
    t3 = mr_port_clock_hires_tick();
    delta = (mr_int64_t)(t3 - t2);
    MR_TEST_EXPECT_GE_INT(delta, 1000);

    /* Setting the tick moves the base, the clock must stay monotonic */
    t4 = mr_port_clock_hires_tick();
    mr_port_clock_hires_tick_set(t4);
    t5 = mr_port_clock_hires_tick();
    MR_TEST_EXPECT_GE_UINT(t5, t4);
}
#endif /* defined(MR_USE_CLOCK_HIRES) */

#endif /* defined(MR_USE_TEST) */
