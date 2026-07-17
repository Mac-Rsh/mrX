/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/drivers/dac/mr_dac.h>
#if defined(MR_USE_DRIVER_DAC)
#include <mrX/libc/mr_malloc.h>

/* DAC driver clazzes(static & dynamic) */
static mr_err_t dac_init(const mr_driver_t *drv);
static mr_err_t dac_deinit(const mr_driver_t *drv);
static void dac_release(const mr_driver_t *drv);
MR_DRIVER_CLAZZES_DEFINE(dac, dac_init, dac_deinit, dac_release);

mr_err_t mr_dac_configure(mr_driver_t *drv, mr_uint32_t channel,
                          mr_dac_config_t *config) {
    const mr_dac_ops_t *da_ops;
    mr_dac_data_t *da_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_DAC);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get DAC data and operations */
    da_data = (mr_dac_data_t *)MR_DRIVER_DATA(drv);
    da_ops = (mr_dac_ops_t *)MR_DRIVER_OPS(drv);
    MR_ASSERT(da_ops->channel_configure != MR_NULL);

    /* Check if channel is existed */
    if ((channel >= 32U) || (!(da_data->exist_mask & (1U << channel)))) {
        return -MR_EINVAL;
    }

    /* Configure channel */
    return da_ops->channel_configure(drv, channel, config);
}

mr_err_t mr_dac_configuration(mr_driver_t *drv, mr_uint32_t channel,
                              mr_dac_config_t *config) {
    const mr_dac_ops_t *da_ops;
    mr_dac_data_t *da_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_DAC);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get DAC data and operations */
    da_data = (mr_dac_data_t *)MR_DRIVER_DATA(drv);
    da_ops = (mr_dac_ops_t *)MR_DRIVER_OPS(drv);
    MR_ASSERT(da_ops->channel_configuration != MR_NULL);

    /* Check if channel is existed */
    if ((channel >= 32U) || (!(da_data->exist_mask & (1U << channel)))) {
        return -MR_EINVAL;
    }

    /* Get channel configuration */
    return da_ops->channel_configuration(drv, channel, config);
}

static mr_err_t dac_init(const mr_driver_t *drv) {
    mr_dac_data_t *da_data;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get DAC data */
    da_data = (mr_dac_data_t *)MR_DRIVER_DATA(drv);

    /* Init DAC event */
    ret = mr_waitable_init(&da_data->event);
    if (ret != 0) {
        return ret;
    }
    return 0;
}

static mr_err_t dac_deinit(const mr_driver_t *drv) {
    mr_dac_data_t *da_data;

    /* Check parameter */
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get DAC data */
    da_data = (mr_dac_data_t *)MR_DRIVER_DATA(drv);

    /* Deinit DAC event */
    mr_waitable_del(&da_data->event);
    return 0;
}

static void dac_release(const mr_driver_t *drv) {
    /* Free DAC data */
    mr_free(MR_DRIVER_DATA(drv));
}
#endif /* defined(MR_USE_DRIVER_DAC) */
