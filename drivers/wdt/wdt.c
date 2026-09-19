/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-17        MacRsh        First version
 */

#include <mrX/drivers/wdt/mr_wdt.h>
#if defined(MR_USE_DRIVER_WDT)
#include <mrX/libc/mr_malloc.h>

/* WDT driver clazzes(static & dynamic) */
static mr_err_t wdt_init(const mr_driver_t *drv);
static mr_err_t wdt_deinit(const mr_driver_t *drv);
static void wdt_release(const mr_driver_t *drv);
MR_DRIVER_CLAZZES_DEFINE(wdt, wdt_init, wdt_deinit, wdt_release);

mr_err_t mr_wdt_configure(mr_driver_t *drv, const mr_wdt_config_t *config) {
    const mr_wdt_ops_t *wdt_ops;
    mr_bool_t busy;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_WDT);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get WDT operations */
    wdt_ops = (const mr_wdt_ops_t *)MR_DRIVER_OPS(drv);
    MR_ASSERT(wdt_ops->configure != MR_NULL);
    MR_ASSERT(wdt_ops->is_busy != MR_NULL);

    /* Check whether WDT is running */
    ret = wdt_ops->is_busy(drv, &busy);
    if (ret != 0) {
        return ret;
    }
    if (busy) {
        return -MR_EBUSY;
    }

    /* Configure WDT */
    return wdt_ops->configure(drv, config);
}

mr_err_t mr_wdt_configuration(mr_driver_t *drv, mr_wdt_config_t *config) {
    const mr_wdt_ops_t *wdt_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_WDT);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get WDT operations */
    wdt_ops = (const mr_wdt_ops_t *)MR_DRIVER_OPS(drv);
    MR_ASSERT(wdt_ops->configuration != MR_NULL);

    /* Get WDT configuration */
    return wdt_ops->configuration(drv, config);
}

static mr_err_t wdt_init(const mr_driver_t *drv) {
    mr_wdt_data_t *wdt_data;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get WDT data */
    wdt_data = (mr_wdt_data_t *)MR_DRIVER_DATA(drv);

    /* Init WDT event */
    ret = mr_waitable_init(&wdt_data->event);
    if (ret != 0) {
        return ret;
    }
    return 0;
}

static mr_err_t wdt_deinit(const mr_driver_t *drv) {
    mr_wdt_data_t *wdt_data;

    /* Check parameter */
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get WDT data */
    wdt_data = (mr_wdt_data_t *)MR_DRIVER_DATA(drv);

    /* Deinit WDT event */
    mr_waitable_del(&wdt_data->event);
    return 0;
}

static void wdt_release(const mr_driver_t *drv) {
    /* Free WDT data */
    mr_free(MR_DRIVER_DATA(drv));
}
#endif /* defined(MR_USE_DRIVER_WDT) */
