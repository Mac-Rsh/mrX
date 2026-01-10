/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-01-08        MacRsh        First version
 */

#include <mrX/libc/mr_compiler.h>
#include <mrX/libc/mr_types.h>
#include <stdio.h>

int mr_port_output(const char *buf, mr_size_t size) {
    MR_IGNORE(size);
    return printf("%.*s", (int)size, buf);
}
