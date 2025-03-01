/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/mr_initcall.h>

/* Init status definition */
#define INIT_STATUS_NO                  (0)
#define INIT_STATUS_START               (1)
#define INIT_STATUS_END                 (2)

/* Kernel and user init status */
static int kstatus = INIT_STATUS_NO;
static int ustatus = INIT_STATUS_NO;

MR_INLINE void kernel_start(void) {
    kstatus = INIT_STATUS_START;
}
MR_INIT_EXPORT(kernel_start, "0.end");

MR_INLINE void kernel_end(void) {
    kstatus = INIT_STATUS_END;
}
MR_INIT_EXPORT(kernel_end, "1.end");

MR_INLINE void user_start(void) {
    ustatus = INIT_STATUS_START;
}
MR_INIT_EXPORT(user_start, "1.end");

MR_INLINE void user_end(void) {
    ustatus = INIT_STATUS_END;
}
MR_INIT_EXPORT(user_end, "9.end");

void mr_autoinit_kernel(void) {
    const mr_initcall_t *ic;

    /* Check kernel init status */
    if (kstatus != INIT_STATUS_NO) {
        /* Kernel already init */
        return;
    }

    /* Run initcalls(kernel) */
    for (ic = &__mr_initcall_kernel_start; ic <= &__mr_initcall_kernel_end;
         ic++) {
        ic->entry();
    }
}

void mr_autoinit(void) {
    const mr_initcall_t *ic;

    /* Check kernel init status */
    if (kstatus == INIT_STATUS_NO) {
        /* Kernel not init, do it */
        mr_autoinit_kernel();
    }

    /* Check user init status */
    if (ustatus != INIT_STATUS_NO) {
        /* User already init */
        return;
    }

    /* Run initcalls(user) */
    for (ic = &__mr_initcall_user_start; ic <= &__mr_initcall_user_end; ic++) {
        ic->entry();
    }
}
