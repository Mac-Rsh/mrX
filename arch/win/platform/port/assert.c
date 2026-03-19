/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-01-08        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#include <stdio.h>

void mr_port_assert(const char *file, int line, const char *expr) {
    printf("Assert: %s:%d: %s\n", file, line, expr);
    exit(1);
}
