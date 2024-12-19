/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_SCHEDULER_H__
#define __MR_SCHEDULER_H__

#include <mr-task/mr_task.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Scheduler
 * @{
 */

/**
 * @brief This function initializes the task scheduler.
 */
void mr_scheduler_init(void);

/**
 * @brief This function starts the task scheduler.
 */
void mr_scheduler_start(void);

/**
 * @brief This function executes the task scheduler.
 */
void mr_scheduler(void);

/**
 * @brief This function adds a task to the task scheduler.
 *
 * @param task The task to add.
 *
 * @note Task status changes to MR_TASK_STAT_READY.
 */
void mr_scheduler_add(mr_task_t *task);

/**
 * @brief This function deletes a task from the task scheduler.
 *
 * @param task The task to delete.
 *
 * @note Task status changes to MR_TASK_STAT_SUSPEND.
 */
void mr_scheduler_del(mr_task_t *task);

/**
 * @brief This function yields the current task.
 *
 * @param task The task to yield.
 */
void mr_scheduler_yield(mr_task_t *task);

/**
 * @brief This function exits the task scheduler.
 *
 * @param task The task to exit.
 *
 * @note Task status changes to MR_TASK_STAT_EXIT.
 */
void mr_scheduler_exit(mr_task_t *task);

/**
 * @brief This function cleans up the task scheduler.
 */
void mr_scheduler_cleanup(void);

/**
 * @brief This function gets the current task.
 *
 * @return The current task.
 */
mr_task_t *mr_scheduler_task_curr_get(void);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_SCHEDULER_H__ */
