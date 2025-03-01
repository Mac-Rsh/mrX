/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <port/mr_port_output.h>
#include <rtthread.h>

#if !defined(RT_VERSION_CHECK)
#define RT_VERSION_CHECK(major, minor, revise)                                 \
    ((major * 10000) + (minor * 100) + revise)
#endif /* !defined(RT_VERSION_CHECK) */

int mr_port_output(const char *buf, mr_size_t size) {
    /* Output string */
#if (RTTHREAD_VERSION >= RT_VERSION_CHECK(4, 1, 0))
    return rt_kprintf("%s", buf);
#else
    rt_kprintf("%s", buf);
    return size;
#endif /* RTTHREAD_VERSION >= RT_VERSION_CHECK(4, 1, 0) */
}
