/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/drivers/dma/mr_dma.h>
#if defined(MR_USE_DRIVER_DMA)
#include <mrX/libc/mr_malloc.h>

/* DMA driver clazzes(static & dynamic) */
static void dma_release(const mr_driver_t *drv);
MR_DRIVER_CLAZZES_DEFINE(dma, MR_NULL, MR_NULL, dma_release);

mr_err_t mr_dma_configure(mr_driver_t *drv, mr_uint32_t channel,
                          const mr_dma_config_t *config) {
    const mr_dma_ops_t *dm_ops;
    mr_dma_data_t *dm_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_DMA);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get DMA operations and data */
    dm_ops = (const mr_dma_ops_t *)MR_DRIVER_OPS(drv);
    dm_data = (mr_dma_data_t *)MR_DRIVER_DATA(drv);
    MR_ASSERT(dm_ops->channel_configure != MR_NULL);

    /* Check whether the channel exists */
    if ((channel >= 64U) || (!(dm_data->exist_mask & (1ULL << channel)))) {
        return -MR_EINVAL;
    }

    /* Configure channel */
    return dm_ops->channel_configure(drv, channel, config);
}

mr_err_t mr_dma_configuration(mr_driver_t *drv, mr_uint32_t channel,
                              mr_dma_config_t *config) {
    const mr_dma_ops_t *dm_ops;
    mr_dma_data_t *dm_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_DMA);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get DMA operations and data */
    dm_ops = (const mr_dma_ops_t *)MR_DRIVER_OPS(drv);
    dm_data = (mr_dma_data_t *)MR_DRIVER_DATA(drv);
    MR_ASSERT(dm_ops->channel_configuration != MR_NULL);

    /* Check whether the channel exists */
    if ((channel >= 64U) || (!(dm_data->exist_mask & (1ULL << channel)))) {
        return -MR_EINVAL;
    }

    /* Get channel configuration */
    return dm_ops->channel_configuration(drv, channel, config);
}

static void dma_release(const mr_driver_t *drv) {
    /* Free DMA data */
    mr_free(MR_DRIVER_DATA(drv));
}
#endif /* defined(MR_USE_DRIVER_DMA) */
