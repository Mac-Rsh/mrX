/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-01-08        MacRsh        First version
 * @date 2026-09-07        MacRsh        Write sized output bytes
 */

#include <mrX/port/mr_port_output.h>
#include <stdio.h>

int mr_port_output(const char *buf, mr_size_t size) {
    size_t n;

    n = fwrite(buf, 1, (size_t)size, stdout);
    if ((n != (size_t)size) && ferror(stdout)) {
        return -1;
    }
    return (int)n;
}
