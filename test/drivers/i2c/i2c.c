/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_DRIVER_I2C)
#include <mrX/drivers/i2c/mr_i2c.h>
#include <mrX/libc/mr_malloc.h>

static mr_i2c_config_t i2c_config;
static mr_size_t i2c_msg_count;
static mr_uint16_t i2c_first_addr;
static mr_uint32_t i2c_events;
static mr_bool_t i2c_event_enable_state;

static mr_err_t i2c_configure(const mr_driver_t *drv,
                              const mr_i2c_config_t *config) {
    MR_IGNORE(drv);
    i2c_config = *config;
    return 0;
}

static mr_err_t i2c_configuration(const mr_driver_t *drv,
                                  mr_i2c_config_t *config) {
    MR_IGNORE(drv);
    *config = i2c_config;
    return 0;
}

static mr_ssize_t i2c_transfer(const mr_driver_t *drv, const mr_i2c_msg_t *msgs,
                               mr_size_t num) {
    MR_IGNORE(drv);
    i2c_msg_count = num;
    i2c_first_addr = msgs[0].addr;
    return (mr_ssize_t)num;
}

static mr_ssize_t i2c_slave_read(const mr_driver_t *drv, void *buf,
                                 mr_size_t size) {
    MR_IGNORE(drv);
    if (size != 0U) {
        ((mr_uint8_t *)buf)[0] = 0x5aU;
    }
    return (mr_ssize_t)size;
}

static mr_ssize_t i2c_slave_write(const mr_driver_t *drv, const void *buf,
                                  mr_size_t size) {
    MR_IGNORE(drv);
    MR_IGNORE(buf);
    return (mr_ssize_t)size;
}

static mr_err_t i2c_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                  mr_bool_t enable) {
    MR_IGNORE(drv);
    i2c_events = events;
    i2c_event_enable_state = enable;
    return 0;
}

static const mr_i2c_ops_t i2c_ops = {
    .configure = i2c_configure,
    .configuration = i2c_configuration,
    .transfer = i2c_transfer,
    .slave_read = i2c_slave_read,
    .slave_write = i2c_slave_write,
    .event_control = i2c_event_control,
};

static mr_driver_t *i2c_create(const mr_i2c_ops_t *ops) {
    mr_i2c_data_t *data = mr_malloc(sizeof(*data));

    if (data == MR_NULL) {
        return MR_NULL;
    }
    return mr_i2c_create(data, MR_NULL, (mr_i2c_ops_t *)ops);
}

static void i2c_waiter_entry(mr_waiter_t *waiter, void *param,
                             mr_wait_event_t event, const void *data) {
    int *count = (int *)param;

    MR_IGNORE(waiter);
    MR_IGNORE(event);
    MR_IGNORE(data);
    (*count)++;
}

MR_TEST(i2c, configuration_and_transfer_paths) {
    mr_driver_t *drv;
    mr_i2c_config_t config = {
        .clock_rate = 400000U,
        .own_addr = 0x42U,
        .addr10 = 0U,
        .role = MR_I2C_ROLE_MASTER,
    };
    mr_i2c_config_t saved = {0};
    mr_uint8_t tx = 0x11U;
    mr_uint8_t rx = 0U;
    mr_i2c_msg_t msgs[] = {
        {.addr = 0x50U,.flags = MR_I2C_MSG_WRITE,.buf = &tx,.len = 1U        },
        {.addr = 0x50U,
         .flags = MR_I2C_MSG_READ | MR_I2C_MSG_STOP,
         .buf = &rx,
         .len = 1U},
    };

    i2c_msg_count = 99U;
    drv = i2c_create(&i2c_ops);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_ASSERT_EQ_INT(mr_i2c_configure(drv, &config), 0);
    MR_TEST_ASSERT_EQ_INT(mr_i2c_configuration(drv, &saved), 0);
    MR_TEST_ASSERT_EQ_UINT(saved.clock_rate, 400000U);
    MR_TEST_ASSERT_EQ_INT(mr_i2c_transfer(drv, MR_NULL, 0U), 0);
    MR_TEST_ASSERT_EQ_UINT(i2c_msg_count, 99U);
    MR_TEST_ASSERT_EQ_INT(mr_i2c_transfer(drv, msgs, 2U), 2);
    MR_TEST_ASSERT_EQ_UINT(i2c_msg_count, 2U);
    MR_TEST_ASSERT_EQ_UINT(i2c_first_addr, 0x50U);
    MR_TEST_ASSERT_EQ_INT(mr_i2c_slave_read(drv, &rx, 1U), 1);
    MR_TEST_ASSERT_EQ_INT(rx, 0x5aU);
    MR_TEST_ASSERT_EQ_INT(mr_i2c_slave_write(drv, &tx, 1U), 1);
    MR_TEST_ASSERT_EQ_INT(mr_i2c_slave_read(drv, MR_NULL, 0U), 0);
    mr_i2c_del(drv);
}

MR_TEST(i2c, optional_operations_and_events) {
    mr_i2c_ops_t optional_ops = i2c_ops;
    mr_i2c_data_t data = {0};
    mr_driver_t drv;
    mr_waiter_t waiter;
    mr_uint8_t value = 0U;
    int event_count = 0;

    MR_TEST_ASSERT_EQ_INT(mr_i2c_init(&drv, &data, MR_NULL, &optional_ops), 0);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(&drv), 0);
    MR_TEST_ASSERT_EQ_INT(mr_waiter_init(&waiter, i2c_waiter_entry,
                                         &event_count, MR_I2C_WAIT_STOP),
                          0);
    MR_TEST_ASSERT_EQ_INT(mr_i2c_event_attach(&drv, &waiter), 0);
    MR_TEST_ASSERT_EQ_INT(mr_i2c_event_enable(&drv, MR_I2C_WAIT_STOP), 0);
    MR_TEST_ASSERT_EQ_UINT(i2c_events, MR_I2C_WAIT_STOP);
    MR_TEST_ASSERT_EQ_INT(i2c_event_enable_state, MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_i2c_event_disable(&drv, MR_I2C_WAIT_ERROR), 0);
    MR_TEST_ASSERT_EQ_INT(i2c_event_enable_state, MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(mr_i2c_event_raise(&drv, MR_I2C_WAIT_STOP), 0);
    MR_TEST_ASSERT_EQ_INT(event_count, 1);
    MR_TEST_ASSERT_EQ_INT(mr_waiter_del(&waiter), 0);

    optional_ops.transfer = MR_NULL;
    optional_ops.slave_read = MR_NULL;
    optional_ops.slave_write = MR_NULL;
    optional_ops.event_control = MR_NULL;
    MR_TEST_ASSERT_EQ_INT(mr_i2c_transfer(&drv, MR_NULL, 0U), -MR_ENOSYS);
    MR_TEST_ASSERT_EQ_INT(mr_i2c_slave_read(&drv, &value, 1U), -MR_ENOSYS);
    MR_TEST_ASSERT_EQ_INT(mr_i2c_slave_write(&drv, &value, 1U), -MR_ENOSYS);
    MR_TEST_ASSERT_EQ_INT(mr_i2c_event_enable(&drv, MR_I2C_WAIT_STOP),
                          -MR_ENOSYS);
    MR_TEST_ASSERT_EQ_PTR(mr_i2c_get(&drv), &drv);
    MR_TEST_EXPECT_EQ_INT(mr_i2c_del(&drv), 0);
    MR_TEST_EXPECT_EQ_INT(mr_i2c_put(&drv), MR_TRUE);
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_DRIVER_I2C) */
