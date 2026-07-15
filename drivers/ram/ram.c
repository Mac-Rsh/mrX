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

/* RAM driver clazzes(static & dynamic) */
static mr_err_t ram_init(const mr_driver_t *drv);
static mr_err_t ram_deinit(const mr_driver_t *drv);
static void ram_release(const mr_driver_t *drv);
MR_DRIVER_CLAZZES_DEFINE(ram, ram_init, ram_deinit, ram_release);

static mr_err_t ram_init(const mr_driver_t *drv) {
    const mr_ram_ops_t *rm_ops;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get RAM operations */
    rm_ops = (mr_ram_ops_t *)MR_DRIVER_OPS(drv);

    /* Mount RAM if mount operation is provided */
    if (rm_ops->mount) {
        ret = rm_ops->mount(drv);
        if (ret != 0) {
            return ret;
        }
    }
    return 0;
}

static mr_err_t ram_deinit(const mr_driver_t *drv) {
    const mr_ram_ops_t *rm_ops;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get RAM operations */
    rm_ops = (mr_ram_ops_t *)MR_DRIVER_OPS(drv);

    /* Unmount RAM if unmount operation is provided */
    if (rm_ops->unmount) {
        ret = rm_ops->unmount(drv);
        if (ret != 0) {
            return ret;
        }
    }
    return 0;
}

static void ram_release(const mr_driver_t *drv) {
    /* Free RAM data */
    mr_free(MR_DRIVER_DATA(drv));
}
#endif /* defined(MR_USE_DRIVER_RAM) */
