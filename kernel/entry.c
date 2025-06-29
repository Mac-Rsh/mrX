/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mr_config.h>
#if defined(MR_USE_ENTRY)
#include <mr-X/mr_initcall.h>

static void entry(void) {
    /* Kernel init */
    mr_kernel_init();
}

/* MR-X entry point */
#if defined(__ARMCC_VERSION)
extern int $Super$$main(void);
int $Sub$$main(void) {
    /* mr-X entry */
    entry();

    /* Real entry */
    $Super$$main();
    return 0;
}
#else
extern int main(void);
int mr_entry(void) {
    /* mr-X entry */
    entry();

    /* Real entry */
    main();
    return 0;
}
#endif /* defined(__ARMCC_VERSION) */
#endif /* defined(MR_USE_ENTRY) */
