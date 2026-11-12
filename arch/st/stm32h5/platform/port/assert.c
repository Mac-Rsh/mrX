/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 */

#include <mrX/platform/mr_platform.h>
#include <mrX/port/mr_port_assert.h>
#include <mrX/port/mr_port_output.h>
#include <mrX/libc/mr_string.h>

static void assert_output_decimal(int value) {
    char buf[12];
    unsigned int number;
    int i;

    if (value < 0) {
        mr_port_output("-", 1U);
        number = (unsigned int)(-(value + 1)) + 1U;
    } else {
        number = (unsigned int)value;
    }

    i = (int)sizeof(buf);
    do {
        buf[--i] = (char)('0' + (number % 10U));
        number /= 10U;
    } while (number != 0U);
    mr_port_output(&buf[i], (mr_size_t)(sizeof(buf) - (unsigned int)i));
}

void mr_port_assert(const char *file, int line, const char *expr) {
    mr_port_output("\r\nmrX assertion failed: ", 24U);
    if (file) {
        mr_port_output(file, (mr_size_t)mr_strlen(file));
    }
    mr_port_output(":", 1U);
    assert_output_decimal(line);
    mr_port_output(" (", 2U);
    if (expr) {
        mr_port_output(expr, (mr_size_t)mr_strlen(expr));
    }
    mr_port_output(")\r\n", 3U);

    __disable_irq();
    while (1) {
        __NOP();
    }
}
