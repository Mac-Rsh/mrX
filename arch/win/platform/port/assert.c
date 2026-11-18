/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-01-08        MacRsh        First version
 * @date 2026-09-07        MacRsh        Flush output before exit
 */

#include <mrX/port/mr_port_assert.h>
#include <stdio.h>
#include <stdlib.h>

void mr_port_assert(const char *file, int line, const char *expr) {
    printf("Assert: %s:%d: %s\n", file, line, expr);
    fflush(stdout);
    exit(1);
}
