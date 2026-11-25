/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 */

#include <mrX.h>
#include <mrX/test/mr_test.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
    mr_bool_t ok;

    /* Unbuffer stdout so CI logs stream in order */
    setvbuf(stdout, NULL, _IONBF, 0);

    /* Initialize platform, kernel services and all initcalls */
    mr_user_init();

#if defined(MR_USE_TEST)
    if (argc == 2) {
        ok = mr_testsuite_run(argv[1]);
    } else if (argc >= 3) {
        ok = mr_testcase_run(argv[1], argv[2]);
    } else {
        ok = mr_test_run();
    }
    return ok ? 0 : 1;
#else
    MR_IGNORE(argc);
    MR_IGNORE(argv);
    return 0;
#endif /* defined(MR_USE_TEST) */
}
