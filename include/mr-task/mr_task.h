/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_TASK_H__
#define __MR_TASK_H__

#include <mr-base/mr_errno.h>
#include <mr-base/mr_list.h>
#include <mr-base/mr_types.h>
#include <mr_config.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Task
 * @{
 */

/* Task stat definition */
#define MR_TASK_STAT_INIT               (0x00)
#define MR_TASK_STAT_SUSPEND            (0x01)
#define MR_TASK_STAT_READY              (0x02)
#define MR_TASK_STAT_RUNNING            (0x03)
#define MR_TASK_STAT_EXIT               (0x04)

/* Task flag definition */
#define MR_TASK_FLAG_YIELD              (0x08)
#define MR_TASK_FLAG_STATIC             (0x40)
#define MR_TASK_FLAG_MAGIC              (0x80)

/* Task type */
typedef struct mr_task {
    char name[MR_CFG_TASK_NAME_MAX];
    mr_uint8_t priority;
    mr_uint8_t flags;
    mr_uint8_t stat;
    mr_list_t list;
    mr_err_t error;

    /* Stack information */
    mr_ptr_t entry;
    mr_ptr_t args;
    mr_ptr_t stack;
    mr_ptr_t stack_addr;
    mr_size_t stack_size;
} mr_task_t;

/**
 * @brief This function initializes a task.
 *
 * @param task The task to be initialized.
 * @param name The name of the task.
 * @param entry The entry point of the task.
 * @param args The arguments of the task.
 * @param stack The stack of the task.
 * @param stack_size The size of the stack.
 * @param priority The priority of the task.
 */
void mr_task_init(mr_task_t *task, const char *name, void (*entry)(void *),
                  void *args, void *stack, mr_size_t stack_size,
                  mr_uint8_t priority);

/**
 * @brief This function creates a task.
 *
 * @param name The name of the task.
 * @param entry The entry point of the task.
 * @param args The arguments of the task.
 * @param stack_size The size of the stack.
 * @param priority The priority of the task.
 * @return The created task.
 */
mr_task_t *mr_task_create(const char *name, void (*entry)(void *), void *args,
                          mr_size_t stack_size, mr_uint8_t priority);

/**
 * @brief This function starts a task.
 *
 * @param task The task to be started.
 * @return MR_EOK on success, error code otherwise.
 */
mr_err_t mr_task_startup(mr_task_t *task);

/**
 * @brief This function resumes a task.
 *
 * @param task The task to be resumed.
 * @return MR_EOK on success, error code otherwise.
 */
mr_err_t mr_task_resume(mr_task_t *task);

/**
 * @brief This function yields the current task.
 */
void mr_task_yield(void);

/**
 * @brief This function suspends a task.
 *
 * @param task The task to be suspended.
 */
void mr_task_suspend(mr_task_t *task);

/**
 * @brief This function exits a task.
 *
 * @param task The task to be exited.
 */
void mr_task_exit(mr_task_t *task);

/**
 * @brief This function waits for a task.
 *
 * @param task The task to be waited.
 */
void mr_task_wait(mr_task_t *task);

/**
 * @brief This function gets the current task.
 *
 * @return The current task.
 */
mr_task_t *mr_task_self(void);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_TASK_H__ */
