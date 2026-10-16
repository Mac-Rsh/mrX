/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/drivers/flash/mr_flash.h>
#if defined(MR_USE_DRIVER_FLASH)
#include <mrX/libc/mr_malloc.h>

/* FLASH driver clazzes(static & dynamic) */
static mr_err_t flash_init(const mr_driver_t *drv);
static void flash_release(const mr_driver_t *drv);
MR_DRIVER_CLAZZES_DEFINE(flash, flash_init, MR_NULL, flash_release);

static mr_err_t flash_init(const mr_driver_t *drv) {
    const mr_flash_ops_t *flash_ops;
    mr_flash_data_t *flash_data;
    mr_flash_info_t info;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get FLASH operations and data */
    flash_ops = (const mr_flash_ops_t *)MR_DRIVER_OPS(drv);
    flash_data = (mr_flash_data_t *)MR_DRIVER_DATA(drv);
    MR_ASSERT(flash_ops->probe != MR_NULL);

    /* Probe FLASH geometry */
    ret = flash_ops->probe(drv, &info);
    if (ret != 0) {
        return ret;
    }
    if ((info.size == 0U) || (info.program_size == 0U)
        || (info.regions_num == 0U)) {
        return -MR_EINVAL;
    }
    if ((info.page_size != 0U) && (info.page_size < info.program_size)) {
        return -MR_EINVAL;
    }

    /* Cache discovered FLASH information */
    flash_data->info = info;
    return 0;
}

static void flash_release(const mr_driver_t *drv) {
    /* Free FLASH data */
    mr_free(MR_DRIVER_DATA(drv));
}
#endif /* defined(MR_USE_DRIVER_FLASH) */
