/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mr-task/mr_idle.h>
#include <mr-task/mr_scheduler.h>
#include <mr-task/mr_task.h>

static mr_uint8_t idle_task_stack[MR_CFG_TASK_IDLE_STACK_SIZE];
static mr_task_t idle_task;

static void s_idle_entry(void *args) {
    MR_UNUSED(args);

    while (1) {
        /* Clean up scheduler */
        mr_scheduler_cleanup();

        /* Yield current task */
        mr_task_yield();
    }
}

void mr_task_idle_init(void) {
    /* Initialize idle task */
    mr_task_init(&idle_task, "idle", s_idle_entry, MR_NULL, idle_task_stack,
                 MR_CFG_TASK_IDLE_STACK_SIZE, MR_CFG_TASK_PRIORITY_MAX - 1);

    /* Start idle task */
    mr_task_startup(&idle_task);
}
