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
static mr_err_t dma_dynamic_deinit(const mr_driver_t *drv);
MR_DRIVER_CLAZZES_DEFINE(dma, MR_NULL, MR_NULL, dma_dynamic_deinit);

mr_err_t mr_dma_configure(mr_driver_t *drv, mr_uint32_t channel,
                                  mr_dma_config_t *config) {
    const mr_dma_ops_t *dma_ops;
    mr_dma_data_t *dma_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_DMA);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);
    MR_ASSERT(channel < 32);

    /* Get DMA data and operations */
    dma_data = (mr_dma_data_t *)MR_DRIVER_DATA(drv);
    dma_ops = (mr_dma_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if channel is existed */
    if (!(dma_data->exist_mask & (1U << channel))) {
        return -MR_EINVAL;
    }

    /* Configure channel */
    return dma_ops->channel_config(drv, channel, config);
}

static mr_err_t dma_dynamic_deinit(const mr_driver_t *drv) {
    /* Free DMA data */
    mr_free(MR_DRIVER_DATA(drv));
    return 0;
}
#endif /* defined(MR_USE_DRIVER_DMA) */
