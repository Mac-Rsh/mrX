/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-05-31        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_DRIVER_ADC)
#include <mrX/drivers/adc/mr_adc.h>

/* HardTree compatible */
#define MR_HT_COMPAT                    wch_ch32v_adc

/* Driver ADC operations */
static mr_err_t drv_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_adc_config_t *config);
static mr_err_t drv_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_adc_config_t *config);
static mr_ssize_t drv_read(const mr_driver_t *drv, mr_adc_seq_t *seq);
MR_UNUSED static const mr_adc_ops_t __drv_adc_ops = {
    .channel_configure = drv_channel_configure,
    .channel_configuration = drv_channel_configuration,
    .read = drv_read,
};

typedef struct drv_adc_channel {
    uint32_t CHANNELx;
    uint32_t GPIO_RCCx;
    GPIO_TypeDef *GPIOx;
    uint32_t PINx;
} drv_adc_channel_t;

/* Driver ADC private data type */
typedef struct drv_adc_priv {
    ADC_TypeDef *ADCx;
    uint32_t RCCx;
    drv_adc_channel_t *channels;
    mr_uint32_t channels_num;
    mr_bool_t configured;
} drv_adc_priv_t;

static mr_err_t drv_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_adc_config_t *config) {
    drv_adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    GPIO_InitTypeDef GPIO_InitStructure;
    ADC_InitTypeDef ADC_InitStructure;
    drv_adc_channel_t *CHx;

    /* Check channel */
    if (channel < priv->channels_num) {
        CHx = &priv->channels[channel];
    } else {
        return -MR_EINVAL;
    }

    /* Check diff mode */
    switch (config->diff) {
        case MR_ADC_DIFF_SINGLE: {
            break;
        }
        default: {
            return -MR_EINVAL;
        }
    }

    /* Check reference */
    switch (config->ref) {
        case MR_ADC_REF_INTERNAL: {
            break;
        }
        default: {
            return -MR_EINVAL;
        }
    }

    /* Check gain */
    switch (config->gain) {
        case MR_ADC_GAIN_1: {
            break;
        }
        default: {
            return -MR_EINVAL;
        }
    }

    /* Configure GPIO */
    if (CHx->GPIO_RCCx) {
        RCC_APB2PeriphClockCmd(CHx->GPIO_RCCx, ENABLE);
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
        GPIO_InitStructure.GPIO_Pin = CHx->PINx;
        GPIO_Init(CHx->GPIOx, &GPIO_InitStructure);
    }

    /* Configure ADC */
    if (!priv->configured) {
        RCC_APB2PeriphClockCmd(priv->RCCx, ENABLE);
#ifdef MR_USE_WCH_SERIES_V00X
        RCC_ADCCLKConfig(RCC_PCLK2_Div4);
#else
        RCC_ADCCLKConfig(RCC_PCLK2_Div8);
#endif /* defined(MR_USE_WCH_SERIES_V00X) */
        ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
        ADC_InitStructure.ADC_ScanConvMode = DISABLE;
        ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
        ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
        ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
        ADC_InitStructure.ADC_NbrOfChannel = 1;
        ADC_Init(priv->ADCx, &ADC_InitStructure);
        ADC_Cmd(priv->ADCx, ENABLE);

        /* Mark the ADC as configured */
        priv->configured = MR_TRUE;
    }

#ifndef MR_USE_WCH_SERIES_V00X
    /* Configure temp-sensor */
    if ((CHx->CHANNELx == ADC_Channel_16)
        || (CHx->CHANNELx == ADC_Channel_17)) {
        ADC_TempSensorVrefintCmd(ENABLE);
    }
#endif /* !defined(MR_USE_WCH_SERIES_V00X) */
    return 0;
}

static mr_err_t drv_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_adc_config_t *config) {
    drv_adc_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Check channel */
    if (channel >= priv->channels_num) {
        return -MR_EINVAL;
    }

    /* Get channel configuration */
    config->diff = MR_ADC_DIFF_SINGLE;
    config->ref = MR_ADC_REF_INTERNAL;
    config->gain = MR_ADC_GAIN_1;
    return 0;
}

static mr_ssize_t drv_read(const mr_driver_t *drv, mr_adc_seq_t *seq) {
    drv_adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint16_t *buf = seq->buf;
    drv_adc_channel_t *CHx;
    mr_uint32_t channel;
    mr_size_t count;

    /* Check parameter */
#if defined(MR_USE_WCH_SERIES_V00X)
    if (seq->resolution != 10U) {
        return -MR_EINVAL;
    }
#else
    if (seq->resolution != 12U) {
        return -MR_EINVAL;
    }
#endif /* defined(MR_USE_WCH_SERIES_V00X) */

    /* Check oversample */
    if (seq->oversample > 7) {
        return -MR_EINVAL;
    }

    /* No channels */
    if (seq->channels == 0U) {
        return 0;
    }

    /* Read samples */
    for (count = 0; (count + sizeof(mr_uint16_t)) <= seq->size;) {
        /* Scan enabled channels */
        for (channel = 0; channel < 32U; channel++) {
            /* Skip disabled channel */
            if (!(seq->channels & (1U << channel))) {
                continue;
            }

            /* Get channel */
            if (channel < priv->channels_num) {
                CHx = &priv->channels[channel];
            } else {
                return (count != 0) ? count : -MR_EINVAL;
            }

            /* Map oversampling count to ADC sample time */
            ADC_RegularChannelConfig(priv->ADCx, CHx->CHANNELx, 1,
                                     seq->oversample);

            /* Start conversion */
            ADC_ClearFlag(priv->ADCx, ADC_FLAG_EOC);
            ADC_SoftwareStartConvCmd(priv->ADCx, ENABLE);

            /* Wait for conversion complete */
            while (ADC_GetFlagStatus(priv->ADCx, ADC_FLAG_EOC) == RESET) {
            }

            /* Read conversion value */
            *buf = (mr_uint16_t)ADC_GetConversionValue(priv->ADCx);
            count += sizeof(mr_uint16_t);
            buf++;

            /* Check if all samples are read */
            if ((count + sizeof(mr_uint16_t)) > seq->size) {
                break;
            }
        }
    }
    return (mr_ssize_t)count;
}

/* Instance install */
#define __WCH_HT_ADC_CHANNEL_IMPL(_elem, _node, _prop)                         \
    {                                                                          \
        .CHANNELx = MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 0),              \
        .GPIO_RCCx = MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 1),             \
        .GPIOx = MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 2),                 \
        .PINx = MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 3),                  \
    }
#define WCH_HT_ADC_INSTALL(_id, _ops)                                          \
    static drv_adc_channel_t MR_CAT(__drv_adc_channel_, _id)[]                 \
        = {MR_HT_DRIVER_PROP_ELEM_FOREACH(_id, wch_channels,                   \
                                          __WCH_HT_ADC_CHANNEL_IMPL)};         \
    static drv_adc_priv_t MR_CAT(__drv_adc_priv_, _id)                         \
        = {.ADCx = MR_HT_DRIVER_PROP(_id, wch_adcx),                           \
           .RCCx = MR_HT_DRIVER_PROP(_id, wch_rccx),                           \
           .channels = MR_CAT(__drv_adc_channel_, _id),                        \
           .channels_num = MR_ARRAY_SIZE(MR_CAT(__drv_adc_channel_, _id))};    \
    MR_HT_ADC_INSTALL(_id, &MR_CAT(__drv_adc_priv_, _id), _ops)
MR_HT_DRIVER_FOREACH(WCH_HT_ADC_INSTALL, &__drv_adc_ops);
#endif /* defined(MR_USE_DRIVER_ADC) */
