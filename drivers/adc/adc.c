/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/drivers/adc/mr_adc.h>
#if defined(MR_USE_DRIVER_ADC)
#include <mrX/libc/mr_malloc.h>

/* ADC driver clazzes(static & dynamic) */
static mr_err_t adc_init(const mr_driver_t *drv);
static mr_err_t adc_statik_deinit(const mr_driver_t *drv);
static mr_err_t adc_dynamic_deinit(const mr_driver_t *drv);
MR_DRIVER_CLAZZES_DEFINE(adc, adc_init, adc_statik_deinit, adc_dynamic_deinit);

mr_err_t mr_adc_configure(mr_driver_t *drv, mr_uint32_t channel,
                          mr_adc_config_t *config) {
    const mr_adc_ops_t *ad_ops;
    mr_adc_data_t *ad_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ADC);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get ADC data and operations */
    ad_data = (mr_adc_data_t *)MR_DRIVER_DATA(drv);
    ad_ops = (mr_adc_ops_t *)MR_DRIVER_OPS(drv);
    MR_ASSERT(ad_ops->channel_configure != MR_NULL);

    /* Check if channel is existed */
    if ((channel >= 32U) || (!(ad_data->exist_mask & (1U << channel)))) {
        return -MR_EINVAL;
    }

    /* Configure channel */
    return ad_ops->channel_configure(drv, channel, config);
}

mr_err_t mr_adc_configuration(mr_driver_t *drv, mr_uint32_t channel,
                              mr_adc_config_t *config) {
    const mr_adc_ops_t *ad_ops;
    mr_adc_data_t *ad_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ADC);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get ADC data and operations */
    ad_data = (mr_adc_data_t *)MR_DRIVER_DATA(drv);
    ad_ops = (mr_adc_ops_t *)MR_DRIVER_OPS(drv);
    MR_ASSERT(ad_ops->channel_configure != MR_NULL);

    /* Check if channel is existed */
    if ((channel >= 32U) || (!(ad_data->exist_mask & (1U << channel)))) {
        return -MR_EINVAL;
    }

    /* Get channel configuration */
    return ad_ops->channel_configuration(drv, channel, config);
}

static mr_err_t adc_init(const mr_driver_t *drv) {
    mr_adc_data_t *ad_data;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get ADC data */
    ad_data = (mr_adc_data_t *)MR_DRIVER_DATA(drv);

    /* Init ADC event */
    ret = mr_waitable_init(&ad_data->event, MR_NULL, (void *)drv);
    if (ret != 0) {
        return ret;
    }
    return 0;
}

static mr_err_t adc_statik_deinit(const mr_driver_t *drv) {
    mr_adc_data_t *ad_data;

    /* Check parameter */
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get ADC data */
    ad_data = (mr_adc_data_t *)MR_DRIVER_DATA(drv);

    /* Deinit ADC event */
    mr_waitable_del(&ad_data->event);
    return 0;
}

static mr_err_t adc_dynamic_deinit(const mr_driver_t *drv) {
    mr_err_t ret;

    /* Deinit ADC */
    ret = adc_statik_deinit(drv);
    if (ret != 0) {
        return ret;
    }

    /* Free ADC data */
    mr_free(MR_DRIVER_DATA(drv));
    return 0;
}
#endif /* defined(MR_USE_DRIVER_ADC) */
