/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/libc/mr_assert.h>
#include <mrX/libc/mr_string.h>

static void start(volatile mr_uint32_t *__test_status) {
    MR_IGNORE(__test_status);
    /* User test start */
}
__MR_TEST_EXPORT(sys, start, start, "0.end");

static void end(volatile mr_uint32_t *__test_status) {
    MR_IGNORE(__test_status);
    /* User test end */
}
__MR_TEST_EXPORT(sys, end, end, "1.end");

static mr_bool_t testcase_run(const mr_testcase_t *cs) {
    volatile mr_uint32_t status = __MR_TEST_MAGIC_PASS;

    /* Check parameter */
    MR_ASSERT(cs->entry != MR_NULL);

    /* Test case begin */
    mr_printf("[ RUN      ] %s.%s\r\n", cs->suite, cs->name);

    /* Call test entry */
    cs->entry(&status);

    /* Test case end */
    if (status == __MR_TEST_MAGIC_PASS) {
        mr_printf("[       " MR_COLOR(MR_COLOR_GREEN, "OK") " ] %s.%s\r\n",
                  cs->suite, cs->name);
        return MR_TRUE;
    } else {
        mr_printf("[  " MR_COLOR(MR_COLOR_RED, "FAILED") "  ] %s.%s\r\n",
                  cs->suite, cs->name);
        return MR_FALSE;
    }
}

mr_bool_t mr_testcase_run(const char *suite, const char *name) {
    mr_uintptr_t start = (mr_uintptr_t)&__mr_testcase_sys_start;
    mr_uintptr_t end = (mr_uintptr_t)&__mr_testcase_sys_end;
    const mr_testcase_t *cs;
    mr_uintptr_t addr;

    /* Check link address */
    MR_ASSERT(start < end);

    /* Each test cases */
    for (addr = start + sizeof(*cs); addr < end; addr += sizeof(*cs)) {
        cs = (const mr_testcase_t *)addr;
        MR_ASSERT(cs->suite != MR_NULL);
        MR_ASSERT(cs->name != MR_NULL);

        /* Match testcase */
        if ((mr_strcmp(cs->suite, suite) != 0)
            || (mr_strcmp(cs->name, name) != 0)) {
            continue;
        }

        /* Run testcase */
        return testcase_run(cs);
    }

    /* Report a missing testcase */
    mr_printf("[ NOT FOUND ] %s.%s\r\n", suite, name);
    return MR_FALSE;
}

mr_bool_t mr_testsuite_run(const char *suite) {
    mr_uintptr_t start = (mr_uintptr_t)&__mr_testcase_sys_start;
    mr_uintptr_t end = (mr_uintptr_t)&__mr_testcase_sys_end;
    const mr_testcase_t *cs;
    mr_uintptr_t addr;
    int pass, fail;
    mr_bool_t ret;

    /* Check link address */
    MR_ASSERT(start < end);

    /* Test begin */
    mr_printf("[==========] Running '%s' suite\r\n", suite);

    /* Each test cases */
    for (pass = fail = 0, addr = start + sizeof(*cs); addr < end;
         addr += sizeof(*cs)) {
        cs = (const mr_testcase_t *)addr;
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
    mr_printf("[==========]\r\n");
    mr_printf("[  PASSED  ] %d tests\r\n", pass);
    mr_printf("[  FAILED  ] %d tests\r\n", fail);

    if ((pass + fail) == 0) {
        mr_printf("[ NOT FOUND ] %s\r\n", suite);
        return MR_FALSE;
    }
    return (fail == 0) ? MR_TRUE : MR_FALSE;
}

mr_bool_t mr_test_run(void) {
    mr_uintptr_t start = (mr_uintptr_t)&__mr_testcase_sys_start;
    mr_uintptr_t end = (mr_uintptr_t)&__mr_testcase_sys_end;
    const mr_testcase_t *cs;
    mr_uintptr_t addr;
    int pass, fail;
    mr_bool_t ret;

    /* Check link address */
    MR_ASSERT(start < end);

    /* Test begin */
    mr_printf("[==========] Running 'all' tests\r\n");

    /* Each test cases */
    for (pass = fail = 0, addr = start + sizeof(*cs); addr < end;
         addr += sizeof(*cs)) {
        cs = (const mr_testcase_t *)addr;
        /* Run testcase */
        ret = testcase_run(cs);
        if (ret) {
            pass++;
        } else {
            fail++;
        }
    }

    /* Test summary */
    mr_printf("[==========]\r\n");
    mr_printf("[  PASSED  ] %d tests\r\n", pass);
    mr_printf("[  FAILED  ] %d tests\r\n", fail);

    /* Return result */
    return (fail == 0) ? MR_TRUE : MR_FALSE;
}
#endif /* defined(MR_USE_TEST) */
