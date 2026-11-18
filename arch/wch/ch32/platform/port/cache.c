/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#include <mrX/port/mr_port_cache.h>

#if defined(MR_USE_DCACHE)
#error "The supported WCH CH32 families do not provide a CPU data cache"
#endif /* defined(MR_USE_DCACHE) */

#if defined(MR_USE_ICACHE)
#include <mrX/libc/mr_compiler.h>

mr_err_t mr_port_icache_enable(void) {
    return -MR_ENOSYS;
}

mr_err_t mr_port_icache_disable(void) {
    return -MR_ENOSYS;
}

mr_size_t mr_port_icache_line_size(void) {
    return 0;
}

mr_err_t mr_port_icache_invalidate(const void *addr, mr_size_t size) {
    MR_IGNORE(addr);

    /* QingKe synchronizes instruction fetches across the entire hart */
    if (size == 0U) {
        return 0;
    }
    return mr_port_icache_invalidate_all();
}

mr_err_t mr_port_icache_invalidate_all(void) {
    __asm__ volatile ("fence.i" ::: "memory");
    return 0;
}
#endif /* defined(MR_USE_ICACHE) */
