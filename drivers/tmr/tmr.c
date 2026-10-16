/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/drivers/tmr/mr_tmr.h>
#if defined(MR_USE_DRIVER_TMR)
#include <mrX/libc/mr_malloc.h>

/* TMR driver clazzes(static & dynamic) */
static mr_err_t tmr_init(const mr_driver_t *drv);
static mr_err_t tmr_deinit(const mr_driver_t *drv);
static void tmr_release(const mr_driver_t *drv);
MR_DRIVER_CLAZZES_DEFINE(tmr, tmr_init, tmr_deinit, tmr_release);

mr_err_t mr_tmr_configure(mr_driver_t *drv, const mr_tmr_config_t *config) {
    const mr_tmr_ops_t *hw_ops;
    mr_bool_t busy;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_TMR);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get TMR operations */
    hw_ops = (const mr_tmr_ops_t *)MR_DRIVER_OPS(drv);
    MR_ASSERT(hw_ops->configure != MR_NULL);
    MR_ASSERT(hw_ops->is_busy != MR_NULL);

    /* Check whether TMR is running */
    ret = hw_ops->is_busy(drv, &busy);
    if (ret != 0) {
        return ret;
    }
    if (busy) {
        return -MR_EBUSY;
    }

    /* Configure TMR */
    return hw_ops->configure(drv, config);
}

mr_err_t mr_tmr_configuration(mr_driver_t *drv, mr_tmr_config_t *config) {
    const mr_tmr_ops_t *hw_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_TMR);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get TMR operations */
    hw_ops = (const mr_tmr_ops_t *)MR_DRIVER_OPS(drv);
    MR_ASSERT(hw_ops->configuration != MR_NULL);

    /* Get TMR configuration */
    return hw_ops->configuration(drv, config);
}

static mr_err_t tmr_init(const mr_driver_t *drv) {
    mr_tmr_data_t *hw_data;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get TMR data */
    hw_data = (mr_tmr_data_t *)MR_DRIVER_DATA(drv);

    /* Init TMR event */
    ret = mr_waitable_init(&hw_data->event);
    if (ret != 0) {
        return ret;
    }
    return 0;
}

static mr_err_t tmr_deinit(const mr_driver_t *drv) {
    mr_tmr_data_t *hw_data;

    /* Check parameter */
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get TMR data */
    hw_data = (mr_tmr_data_t *)MR_DRIVER_DATA(drv);

    /* Deinit TMR event */
    mr_waitable_del(&hw_data->event);
    return 0;
}

static void tmr_release(const mr_driver_t *drv) {
    /* Free TMR data */
    mr_free(MR_DRIVER_DATA(drv));
}
#endif /* defined(MR_USE_DRIVER_TMR) */
