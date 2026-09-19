/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-17        MacRsh        First version
 */

#include <mrX/drivers/i2c/mr_i2c.h>
#if defined(MR_USE_DRIVER_I2C)
#include <mrX/libc/mr_malloc.h>

/* I2C driver clazzes(static & dynamic) */
static mr_err_t i2c_init(const mr_driver_t *drv);
static mr_err_t i2c_deinit(const mr_driver_t *drv);
static void i2c_release(const mr_driver_t *drv);
MR_DRIVER_CLAZZES_DEFINE(i2c, i2c_init, i2c_deinit, i2c_release);

mr_err_t mr_i2c_configure(mr_driver_t *drv, const mr_i2c_config_t *config) {
    const mr_i2c_ops_t *i2c_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_I2C);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get I2C operations */
    i2c_ops = (const mr_i2c_ops_t *)MR_DRIVER_OPS(drv);
    MR_ASSERT(i2c_ops->configure != MR_NULL);

    /* Configure I2C */
    return i2c_ops->configure(drv, config);
}

mr_err_t mr_i2c_configuration(mr_driver_t *drv, mr_i2c_config_t *config) {
    const mr_i2c_ops_t *i2c_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_I2C);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get I2C operations */
    i2c_ops = (const mr_i2c_ops_t *)MR_DRIVER_OPS(drv);
    MR_ASSERT(i2c_ops->configuration != MR_NULL);

    /* Get I2C configuration */
    return i2c_ops->configuration(drv, config);
}

mr_ssize_t mr_i2c_transfer(mr_driver_t *drv, const mr_i2c_msg_t *msgs,
                           mr_size_t num) {
    const mr_i2c_ops_t *i2c_ops;
    mr_size_t i;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_I2C);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT((msgs != MR_NULL) || (num == 0U));

    /* Get I2C operations */
    i2c_ops = (const mr_i2c_ops_t *)MR_DRIVER_OPS(drv);

    /* Check whether operation is supported */
    if (!i2c_ops->transfer) {
        return -MR_ENOSYS;
    }

    /* Handle empty transfer */
    if (num == 0U) {
        return 0;
    }

    /* Check messages */
    for (i = 0; i < num; i++) {
        MR_ASSERT((msgs[i].buf != MR_NULL) || (msgs[i].len == 0U));
    }

    /* Transfer messages */
    return i2c_ops->transfer(drv, msgs, num);
}

static mr_err_t i2c_init(const mr_driver_t *drv) {
    mr_i2c_data_t *i2c_data;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get I2C data */
    i2c_data = (mr_i2c_data_t *)MR_DRIVER_DATA(drv);

    /* Init I2C event */
    ret = mr_waitable_init(&i2c_data->event);
    if (ret != 0) {
        return ret;
    }
    return 0;
}

static mr_err_t i2c_deinit(const mr_driver_t *drv) {
    mr_i2c_data_t *i2c_data;

    /* Check parameter */
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get I2C data */
    i2c_data = (mr_i2c_data_t *)MR_DRIVER_DATA(drv);

    /* Deinit I2C event */
    mr_waitable_del(&i2c_data->event);
    return 0;
}

static void i2c_release(const mr_driver_t *drv) {
    /* Free I2C data */
    mr_free(MR_DRIVER_DATA(drv));
}
#endif /* defined(MR_USE_DRIVER_I2C) */
