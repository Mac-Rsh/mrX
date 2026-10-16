/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/drivers/spi/mr_spi.h>
#if defined(MR_USE_DRIVER_SPI)
#include <mrX/libc/mr_malloc.h>

/* SPI driver clazzes(static & dynamic) */
static mr_err_t spi_init(const mr_driver_t *drv);
static mr_err_t spi_deinit(const mr_driver_t *drv);
static void spi_release(const mr_driver_t *drv);
MR_DRIVER_CLAZZES_DEFINE(spi, spi_init, spi_deinit, spi_release);

mr_err_t mr_spi_configure(mr_driver_t *drv, mr_uint32_t cs,
                          const mr_spi_config_t *config) {
    const mr_spi_ops_t *spi_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_SPI);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get SPI operations */
    spi_ops = (const mr_spi_ops_t *)MR_DRIVER_OPS(drv);
    MR_ASSERT(spi_ops->configure != MR_NULL);

    /* Configure SPI chip select */
    return spi_ops->configure(drv, cs, config);
}

mr_err_t mr_spi_configuration(mr_driver_t *drv, mr_uint32_t cs,
                              mr_spi_config_t *config) {
    const mr_spi_ops_t *spi_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_SPI);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get SPI operations */
    spi_ops = (const mr_spi_ops_t *)MR_DRIVER_OPS(drv);
    MR_ASSERT(spi_ops->configuration != MR_NULL);

    /* Get SPI chip-select configuration */
    return spi_ops->configuration(drv, cs, config);
}

mr_ssize_t mr_spi_transfer(mr_driver_t *drv, mr_uint32_t cs,
                           const mr_spi_msg_t *msgs, mr_size_t num) {
    const mr_spi_ops_t *spi_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_SPI);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT((msgs != MR_NULL) || (num == 0U));

    /* Get SPI operations */
    spi_ops = (const mr_spi_ops_t *)MR_DRIVER_OPS(drv);

    /* Check whether operation is supported */
    if (!spi_ops->transfer) {
        return -MR_ENOSYS;
    }

    /* Handle empty transfer */
    if (num == 0U) {
        return 0;
    }

    /* Transfer messages */
    return spi_ops->transfer(drv, cs, msgs, num);
}

static mr_err_t spi_init(const mr_driver_t *drv) {
    mr_spi_data_t *spi_data;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get SPI data */
    spi_data = (mr_spi_data_t *)MR_DRIVER_DATA(drv);

    /* Init SPI event */
    ret = mr_waitable_init(&spi_data->event);
    if (ret != 0) {
        return ret;
    }
    return 0;
}

static mr_err_t spi_deinit(const mr_driver_t *drv) {
    mr_spi_data_t *spi_data;

    /* Check parameter */
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get SPI data */
    spi_data = (mr_spi_data_t *)MR_DRIVER_DATA(drv);

    /* Deinit SPI event */
    mr_waitable_del(&spi_data->event);
    return 0;
}

static void spi_release(const mr_driver_t *drv) {
    /* Free SPI data */
    mr_free(MR_DRIVER_DATA(drv));
}
#endif /* defined(MR_USE_DRIVER_SPI) */
