/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mr-X/mr_clock.h>
#include <libc/mr_assert.h>

/* Test status */
mr_uint32_t __mr_test_status = __MR_TEST_MAGIC_PASS;

static void start(void) {
    /* User test start */
}
__MR_TEST_EXPORT(sys, start, start, "0.end");

static void end(void) {
    /* User test end */
}
__MR_TEST_EXPORT(sys, end, end, "1.end");

int mr_test_all_run(void) {
    const mr_testcase_t *start = &__mr_testcase_sys_start;
    const mr_testcase_t *end = &__mr_testcase_sys_end;
    const mr_testcase_t *cs;
    int pass, fail;
    mr_tick_t tick;

    /* Test begin */
    mr_printf("[==========] Running tests\n");

    /* Each test cases */
    for (pass = fail = 0, cs = start + 1; cs < end; cs++) {
        MR_ASSERT(cs->entry != MR_NULL);

        /* Test case begin */
        mr_printf("[ RUN      ] %s.%s\n", cs->suite, cs->name);

        /* Reset test status */
        __mr_test_status = __MR_TEST_MAGIC_PASS;

        /* Get begin tick */
        tick = mr_clock_tick();

        /* Call test entry */
        cs->entry();

        /* Get difference tick */
        tick = mr_clock_tick() - tick;

        /* Test case end */
        if (__mr_test_status == __MR_TEST_MAGIC_PASS) {
            mr_printf("[       " MR_COLOR(MR_COLOR_GREEN,
                                          "OK") " ] %s.%s (%d tick)\n",
                      cs->suite, cs->name, tick);
            pass++;
        } else {
            mr_printf(
                "[  " MR_COLOR(MR_COLOR_RED, "FAILED") "  ] %s.%s (%d tick)\n",
                cs->suite, cs->name, tick);
            fail++;
        }
    }

    /* Test summary */
    mr_printf("[==========]\n");
    mr_printf("[  PASSED  ] %d tests\n", pass);
    mr_printf("[  FAILED  ] %d tests\n", fail);

    /* Test end */
    return 0;
}
#endif /* defined(MR_USE_TEST) */
