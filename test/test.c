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
#include <libc/mr_string.h>

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

static mr_bool_t testcase_run(const mr_testcase_t *cs) {
    /* Check parameter */
    MR_ASSERT(cs->entry != MR_NULL);

    /* Test case begin */
    mr_printf("[ RUN      ] %s.%s\n", cs->suite, cs->name);

    /* Reset test status */
    __mr_test_status = __MR_TEST_MAGIC_PASS;

    /* Call test entry */
    cs->entry();

    /* Test case end */
    if (__mr_test_status == __MR_TEST_MAGIC_PASS) {
        mr_printf("[       " MR_COLOR(MR_COLOR_GREEN, "OK") " ] %s.%s\n",
                  cs->suite, cs->name);
        return MR_TRUE;
    } else {
        mr_printf("[  " MR_COLOR(MR_COLOR_RED, "FAILED") "  ] %s.%s\n",
                  cs->suite, cs->name);
        return MR_FALSE;
    }
}

void mr_testcase_run(const char *suite, const char *name) {
    const mr_testcase_t *start = &__mr_testcase_sys_start;
    const mr_testcase_t *end = &__mr_testcase_sys_end;
    const mr_testcase_t *cs;

    /* Each test cases */
    for (cs = start + 1; cs < end; cs++) {
        /* Match testcase */
        if ((mr_strcmp(cs->suite, suite) != 0)
            || (mr_strcmp(cs->name, name) != 0)) {
            continue;
        }

        /* Run testcase */
        testcase_run(cs);
    }
}

void mr_testsuite_run(const char *suite) {
    const mr_testcase_t *start = &__mr_testcase_sys_start;
    const mr_testcase_t *end = &__mr_testcase_sys_end;
    const mr_testcase_t *cs;
    int pass, fail;
    mr_bool_t ret;

    /* Test begin */
    mr_printf("[==========] Running '%s' suite\n", suite);

    /* Each test cases */
    for (pass = fail = 0, cs = start + 1; cs < end; cs++) {
        /* Match testcase */
        if (mr_strcmp(cs->suite, suite) != 0) {
            continue;
        }

        /* Run testcase */
        ret = testcase_run(cs);
        if (ret) {
            pass++;
        } else {
            fail++;
        }
    }

    /* Test summary */
    mr_printf("[==========]\n");
    mr_printf("[  PASSED  ] %d tests\n", pass);
    mr_printf("[  FAILED  ] %d tests\n", fail);
}

mr_bool_t mr_test_run(void) {
    const mr_testcase_t *start = &__mr_testcase_sys_start;
    const mr_testcase_t *end = &__mr_testcase_sys_end;
    const mr_testcase_t *cs;
    int pass, fail;
    mr_bool_t ret;

    /* Test begin */
    mr_printf("[==========] Running 'all' tests\n");

    /* Each test cases */
    for (pass = fail = 0, cs = start + 1; cs < end; cs++) {
        /* Run testcase */
        ret = testcase_run(cs);
        if (ret) {
            pass++;
        } else {
            fail++;
        }
    }

    /* Test summary */
    mr_printf("[==========]\n");
    mr_printf("[  PASSED  ] %d tests\n", pass);
    mr_printf("[  FAILED  ] %d tests\n", fail);

    /* Return result */
    return (fail == 0) ? MR_TRUE : MR_FALSE;
}
#endif /* defined(MR_USE_TEST) */
