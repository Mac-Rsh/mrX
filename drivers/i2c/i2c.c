/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/drivers/i2c/mr_i2c.h>
#if defined(MR_USE_DRIVER_I2C)
#include <mrX/libc/mr_malloc.h>

/* I2C driver clazzes(static & dynamic) */
static mr_err_t i2c_init(const mr_driver_t *drv);
static mr_err_t i2c_statik_deinit(const mr_driver_t *drv);
static mr_err_t i2c_dynamic_deinit(const mr_driver_t *drv);
MR_DRIVER_CLAZZES_DEFINE(i2c, i2c_init, i2c_statik_deinit, i2c_dynamic_deinit);

mr_err_t mr_i2c_configure(mr_driver_t *drv, mr_i2c_config_t *config) {
    mr_i2c_data_t *ic_data;
    mr_i2c_ops_t *ic_ops;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_I2C);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get I2C data and operations */
    ic_data = (mr_i2c_data_t *)MR_DRIVER_DATA(drv);
    ic_ops = (mr_i2c_ops_t *)MR_DRIVER_OPS(drv);

    /* Configure I2C */
    ret = ic_ops->configure(drv, config);
    if (ret == 0) {
        ic_data->config = *config;
    }
    return ret;
}

mr_err_t mr_i2c_configuration(mr_driver_t *drv, mr_i2c_config_t *config) {
    mr_i2c_data_t *ic_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_I2C);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get I2C data */
    ic_data = (mr_i2c_data_t *)MR_DRIVER_DATA(drv);

    /* Get I2C configuration */
    *config = ic_data->config;
    return 0;
}

static int i2c_xfer_poll(mr_driver_t *drv, mr_i2c_msg_t *msgs,
                         mr_uint32_t msgs_num) {
    const mr_i2c_ops_t *ic_ops;
    mr_uint32_t count, i;
    int ret;

    /* Get I2C operations */
    ic_ops = (mr_i2c_ops_t *)MR_DRIVER_OPS(drv);

    /* Transfer I2C message */
    for (i = 0, count = 0; i < msgs_num; i++, count += (mr_uint32_t)ret) {
        ret = ic_ops->xfer(drv, &msgs[count]);
        if (ret < 0) {
            return (count > 0) ? (int)count : ret;
        }
    }
    return (int)count;
}

static mr_err_t i2c_init(const mr_driver_t *drv) {
    const mr_i2c_ops_t *ic_ops;
    mr_i2c_data_t *ic_data;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get I2C data and operations */
    ic_data = (mr_i2c_data_t *)MR_DRIVER_DATA(drv);
    ic_ops = (mr_i2c_ops_t *)MR_DRIVER_OPS(drv);

    /* Init I2C transfer operations */
    if (ic_data->xfer_ops_mode == MR_I2C_OPS_MODE_INT) {
        return -MR_ENOSYS;
    } else {
        ic_data->xfer = i2c_xfer_poll;
    }

    /* Init I2C irq */
    ret = mr_waitable_init(&ic_data->irq, MR_NULL, (void *)drv);
    if (ret != 0) {
        return ret;
    }

    /* Configure I2C with default configuration */
    ret = ic_ops->configure(drv, &ic_data->config);
    if (ret != 0) {
        /* Mark configuration as invalid */
        ic_data->config.baud_rate = 0;
    }
    return ret;
}

static mr_err_t i2c_statik_deinit(const mr_driver_t *drv) {
    const mr_i2c_config_t config = {.baud_rate = 0};
    const mr_i2c_ops_t *ic_ops;
    mr_i2c_data_t *ic_data;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get I2C data and operations */
    ic_data = (mr_i2c_data_t *)MR_DRIVER_DATA(drv);
    ic_ops = (mr_i2c_ops_t *)MR_DRIVER_OPS(drv);

    /* Deinit I2C configuration */
    ret = ic_ops->configure(drv, &config);
    if (ret != 0) {
        return ret;
    }

    /* Deinit I2C irq */
    mr_waitable_del(&ic_data->irq);
    return 0;
}

static mr_err_t i2c_dynamic_deinit(const mr_driver_t *drv) {
    mr_err_t ret;

    /* Deinit I2C */
    ret = i2c_statik_deinit(drv);
    if (ret != 0) {
        return ret;
    }

    /* Free I2C data */
    mr_free(MR_DRIVER_DATA(drv));
    return 0;
}
#endif /* defined(MR_USE_DRIVER_I2C) */
