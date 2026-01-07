/**
 * @copyright (c) 2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2025-11-11        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#include <mrX/libc/mr_compiler.h>

void mr_port_assert(const char *file, int line, const char *expr) {
    MR_IGNORE(file);
    MR_IGNORE(line);
    MR_IGNORE(expr);
}
