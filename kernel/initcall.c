/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/mr_initcall.h>
#include <libc/mr_atomic.h>

/* Init status definition */
#define INIT_STATUS_UNINIT              (0)
#define INIT_STATUS_INITED              (1)

/* Kernel and user init status */
static mr_atomic_t kstatus = MR_ATOMIC_INIT(INIT_STATUS_UNINIT);
static mr_atomic_t ustatus = MR_ATOMIC_INIT(INIT_STATUS_UNINIT);

MR_INLINE void kernel_start(void) {
}
MR_INIT_EXPORT(kernel_start, "0.end");

MR_INLINE void kernel_end(void) {
}
MR_INIT_EXPORT(kernel_end, "1.end");

MR_INLINE void user_start(void) {
}
MR_INIT_EXPORT(user_start, "1.end");

MR_INLINE void user_end(void) {
}
MR_INIT_EXPORT(user_end, "9.end");

void mr_autoinit_kernel(void) {
    const mr_initcall_t *ic;
    mr_atomic_t inited;

    /* Check kernel init status */
    mr_atomic_store(&inited, INIT_STATUS_UNINIT);
    if (!mr_atomic_compare_exchange(&kstatus, &inited, INIT_STATUS_INITED)) {
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
    mr_atomic_t inited;

    /* Run autoinit(kernel) */
    mr_autoinit_kernel();

    /* Check user init status */
    mr_atomic_store(&inited, INIT_STATUS_UNINIT);
    if (!mr_atomic_compare_exchange(&ustatus, &inited, INIT_STATUS_INITED)) {
        /* User already init */
        return;
    }

    /* Run initcalls(user) */
    for (ic = &__mr_initcall_user_start; ic <= &__mr_initcall_user_end; ic++) {
        ic->entry();
    }
}
