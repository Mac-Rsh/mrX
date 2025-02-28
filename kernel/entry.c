/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mr_config.h>
#if defined(MR_USE_ENTRY)
#include <kernel/mr_initcall.h>

MR_INLINE void entry(void) {
    extern int main(void);

    /* Run autoinit(kernel) */
    mr_autoinit_kernel();

    /* Run main */
    main();
}

/* MR-X entry point */
#if defined(__GNUC__) || defined(__clang__)
int mr_entry(void) {
    entry();
    return 0;
}
#elif defined(__CC_ARM)
extern int $Super$$main(void);
int $Sub$$main(void) {
    entry();
    return 0;
}
#endif /* defined(__GNUC__) || defined(__clang__) */
#endif /* defined(MR_USE_ENTRY) */
