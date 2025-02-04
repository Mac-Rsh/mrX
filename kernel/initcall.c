/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/mr_initcall.h>

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

    /* Run initcalls(kernel) */
    for (ic = &__mr_initcall_kernel_start; ic <= &__mr_initcall_kernel_end;
         ic++) {
        ic->entry();
    }
}

void mr_autoinit(void) {
    const mr_initcall_t *ic;

    /* Run initcalls(user) */
    for (ic = &__mr_initcall_user_start; ic <= &__mr_initcall_user_end;
         ic++) {
        ic->entry();
    }
}
