/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mr-task/mr_scheduler.h>
#include <mr-task/mr_task.h>

static mr_uint8_t main_task_stack[MR_CFG_TASK_MAIN_STACK_SIZE];
static mr_task_t main_task;

static void s_main_entry(void *args) {
    extern int main(void);
    MR_UNUSED(args);

    /* Enter main function */
    main();
}

static void t_tmain_init(void) {
    /* Initialize task scheduler */
    mr_scheduler_init();

    /* Initialize main task */
    mr_task_init(&main_task, "main", s_main_entry, MR_NULL, main_task_stack,
                 MR_CFG_TASK_MAIN_STACK_SIZE, MR_CFG_TASK_MAIN_PRIORITY);

    /* Start main task */
    mr_task_startup(&main_task);

    /* Start task scheduler */
    mr_scheduler_start();
}

#if defined(__GNUC__) || defined(__clang__)
int mr_entry(void) {
    t_tmain_init();
    return 0;
}
#elif defined(__CC_ARM)
extern int $Super$$main(void);
int $Sub$$main(void) {
    t_tmain_init();
    return 0;
}
#endif /* defined(__GNUC__) */
