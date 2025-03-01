/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/mr_initcall.h>
#include <rtthread.h>

static int mr_x_kernel_init(void)
{
    /* Run autoinit(kernel) */
    mr_autoinit_kernel();
    return RT_EOK;
}
INIT_COMPONENT_EXPORT(mr_x_kernel_init);

static int mr_x_user_init(void)
{
    /* Run autoinit(user) */
    mr_autoinit();
    return RT_EOK;
}
INIT_APP_EXPORT(mr_x_user_init);
