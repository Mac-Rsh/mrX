/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#include <mrX/port/mr_port_assert.h>
#include <mrX/port/mr_port_irq.h>
#include <mrX/port/mr_port_output.h>
#include <mrX/libc/mr_string.h>

/* Assertion context type */
typedef struct {
    const char *file;
    int line;
    const char *expr;
} assert_context_t;

/* Assertion context */
static volatile assert_context_t __assert_context;

static void assert_output_decimal(int value) {
    unsigned int number;
    char buf[12];
    int i;

    /* Convert the signed value to an unsigned magnitude */
    if (value < 0) {
        mr_port_output("-", 1U);
        number = (unsigned int)(-(value + 1)) + 1U;
    } else {
        number = (unsigned int)value;
    }

    /* Convert the decimal digits from the end of the buffer */
    i = (int)sizeof(buf);
    do {
        buf[--i] = (char)('0' + (number % 10U));
        number /= 10U;
    } while (number != 0U);

    /* Output the converted decimal string */
    mr_port_output(&buf[i], (mr_size_t)(sizeof(buf) - (unsigned int)i));
}

void mr_port_assert(const char *file, int line, const char *expr) {
    /* Mask interrupts before writing the assertion output */
    mr_port_irq_disable();

    /* Preserve the failure for a debugger even when output is unavailable */
    __assert_context.file = file;
    __assert_context.line = line;
    __assert_context.expr = expr;

    /* Output the assertion location */
    mr_port_output("\r\nmrX assertion failed: ",
                   sizeof("\r\nmrX assertion failed: ") - 1U);
    if (file) {
        mr_port_output(file, (mr_size_t)mr_strlen(file));
    }
    mr_port_output(":", 1U);
    assert_output_decimal(line);

    /* Output the failed expression */
    mr_port_output(" (", 2U);
    if (expr) {
        mr_port_output(expr, (mr_size_t)mr_strlen(expr));
    }
    mr_port_output(")\r\n", 3U);

    /* Stop execution after the assertion failure */
    while (1) {
        __NOP();
    }
}
