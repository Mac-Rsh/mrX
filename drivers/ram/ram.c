/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/drivers/ram/mr_ram.h>
#if defined(MR_USE_DRIVER_RAM)
#include <mrX/libc/mr_malloc.h>
#include <mrX/libc/mr_string.h>

/* RAM driver clazzes(static & dynamic) */
static mr_err_t ram_init(const mr_driver_t *drv);
static mr_err_t ram_deinit(const mr_driver_t *drv);
static void ram_release(const mr_driver_t *drv);
MR_DRIVER_CLAZZES_DEFINE(ram, ram_init, ram_deinit, ram_release);

static mr_err_t ram_init(const mr_driver_t *drv) {
    const mr_ram_ops_t *rm_ops;
    mr_ram_data_t *ram_data;
    mr_ram_info_t info;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get RAM operations and data */
    rm_ops = (const mr_ram_ops_t *)MR_DRIVER_OPS(drv);
    ram_data = (mr_ram_data_t *)MR_DRIVER_DATA(drv);
    MR_ASSERT(rm_ops->probe != MR_NULL);

    /* Probe RAM geometry */
    mr_memset(&info, 0, sizeof(info));
    ret = rm_ops->probe(drv, &info);
    if (ret != 0) {
        return ret;
    }
    if (info.size == 0U) {
        return -MR_EINVAL;
    }
    if ((info.regions_num != 0U) && (!rm_ops->region)) {
        return -MR_EINVAL;
    }

    /* Cache discovered RAM information */
    ram_data->info = info;
    return 0;
}

static mr_err_t ram_deinit(const mr_driver_t *drv) {
    mr_ram_data_t *ram_data;

    /* Check parameter */
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get RAM data */
    ram_data = (mr_ram_data_t *)MR_DRIVER_DATA(drv);

    /* Clear discovered RAM information */
    mr_memset(&ram_data->info, 0, sizeof(ram_data->info));
    return 0;
}

static void ram_release(const mr_driver_t *drv) {
    /* Free RAM data */
    mr_free(MR_DRIVER_DATA(drv));
}
#endif /* defined(MR_USE_DRIVER_RAM) */
