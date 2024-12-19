/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include "mr-base/mr_autoinit.h"
#include "mr-base/mr_log.h"

static mr_err_t start(void) {
    return MR_EOK;
}
MR_INIT_EXPORT(start, "0.end");

static mr_err_t end(void) {
    return MR_EOK;
}
MR_INIT_EXPORT(end, "9.end");

void mr_autoinit(void) {
    const mr_initcall_t *ic;
    mr_err_t ret;

    /* Run initcalls */
    for (ic = &__mr_initcall_start; ic <= &__mr_initcall_end; ic++) {

        /* Run initcall */
        MR_LOG_D("Initcall '%s' start", ic->name);
        ret = ic->fn();
        if (ret != MR_EOK) {
            MR_LOG_E("Initcall '%s' failed: '%s'", ic->name, mr_strerror(-ret));
        }
        MR_LOG_D("Initcall '%s' end", ic->name);
    }
}
